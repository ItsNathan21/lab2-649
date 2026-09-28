/** @file motor_controller.c
 * @brief Independent fixed-point wheel-speed PID loops with bounded drive output.
 */
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "encoder.h"
#include "motor.h"
#include "motor_controller.h"

_Static_assert(MOTOR_COUNT == ENCODER_COUNT, "Motor/encoder counts must match");
_Static_assert(MOTOR_PID_MAX_RPM > 0U && MOTOR_PID_MAX_RPM <= MOTOR_PID_MAX_FEEDBACK_RPM,
	       "Invalid target speed limit");
_Static_assert(MOTOR_PID_PERIOD_MS > 0U && MOTOR_PID_PERIOD_MS < MOTOR_PID_COMMAND_TIMEOUT_MS,
	       "Invalid PID period");
_Static_assert(MOTOR_PID_MAX_DUTY_PERMILLE > 0 && MOTOR_PID_MAX_DUTY_PERMILLE <= 1000,
	       "Invalid PWM limit");
_Static_assert(MOTOR_PID_LEFT_ENCODER_SIGN * MOTOR_PID_LEFT_ENCODER_SIGN == 1 &&
	       MOTOR_PID_RIGHT_ENCODER_SIGN * MOTOR_PID_RIGHT_ENCODER_SIGN == 1,
	       "Encoder signs must be +1 or -1");

_Static_assert(MOTOR_PID_ACCEL_RPM_PER_SEC > 0U && MOTOR_PID_ACCEL_RPM_PER_SEC <= 10000U,
	       "Invalid acceleration limit");
_Static_assert(MOTOR_PID_FF_START_PERMILLE >= 0 && MOTOR_PID_FF_START_PERMILLE <= 1000 &&
	       MOTOR_PID_FF_SPAN_PERMILLE >= 0 && MOTOR_PID_FF_SPAN_PERMILLE <= 1000 &&
	       MOTOR_PID_I_LIMIT_PERMILLE >= 0 && MOTOR_PID_I_LIMIT_PERMILLE <= 1000,
	       "Invalid feedforward or integral limits");
_Static_assert(MOTOR_PID_MAX_FEEDBACK_RPM <= 10000U && MOTOR_PID_COMMAND_TIMEOUT_MS <= 10000U,
	       "Feedback and timing bounds exceed fixed-point arithmetic limits");
_Static_assert(MOTOR_PID_LEFT_KP_MILLI >= 0 && MOTOR_PID_LEFT_KP_MILLI <= 1000000,
	       "Invalid left KP gain");
_Static_assert(MOTOR_PID_LEFT_KI_MILLI >= 0 && MOTOR_PID_LEFT_KI_MILLI <= 1000000,
	       "Invalid left KI gain");
_Static_assert(MOTOR_PID_LEFT_KD_MILLI >= 0 && MOTOR_PID_LEFT_KD_MILLI <= 1000000,
	       "Invalid left KD gain");
_Static_assert(MOTOR_PID_RIGHT_KP_MILLI >= 0 && MOTOR_PID_RIGHT_KP_MILLI <= 1000000,
	       "Invalid right KP gain");
_Static_assert(MOTOR_PID_RIGHT_KI_MILLI >= 0 && MOTOR_PID_RIGHT_KI_MILLI <= 1000000,
	       "Invalid right KI gain");
_Static_assert(MOTOR_PID_RIGHT_KD_MILLI >= 0 && MOTOR_PID_RIGHT_KD_MILLI <= 1000000,
	       "Invalid right KD gain");

static const struct motor_pid_gains gains[MOTOR_COUNT] = {
	{ MOTOR_PID_LEFT_KP_MILLI, MOTOR_PID_LEFT_KI_MILLI, MOTOR_PID_LEFT_KD_MILLI },
	{ MOTOR_PID_RIGHT_KP_MILLI, MOTOR_PID_RIGHT_KI_MILLI, MOTOR_PID_RIGHT_KD_MILLI },
};
static const int encoder_sign[MOTOR_COUNT] = {
	MOTOR_PID_LEFT_ENCODER_SIGN, MOTOR_PID_RIGHT_ENCODER_SIGN,
};
static struct motor_pid_state states[MOTOR_COUNT];
static K_MUTEX_DEFINE(controller_lock);
static K_THREAD_STACK_DEFINE(controller_stack, MOTOR_PID_STACK_SIZE);
static struct k_thread controller_thread;
static bool started;
static bool faulted;
static bool have_command;
static uint32_t requested_mrpm;
static uint32_t ramped_mrpm;
static int64_t command_ms;

/**
 * @brief Bound a signed fixed-point quantity.
 * @param value Input quantity.
 * @param low Inclusive lower bound.
 * @param high Inclusive upper bound.
 * @return Clamped value.
 */
static int64_t clamp_value(int64_t value, int64_t low, int64_t high)
{
	return value < low ? low : value > high ? high : value;
}

/**
 * @brief Clear accumulated drive and stall state while retaining measured speed.
 */
static void reset_drive(void)
{
	ramped_mrpm = 0U;
	for (unsigned int side = 0; side < MOTOR_COUNT; side++) {
		states[side].integral = 0;
		states[side].stall_ms = 0;
		states[side].reverse_ms = 0;
		states[side].duty = 0U;
	}
}

/**
 * @brief Latch a fault and coast both outputs while holding controller_lock.
 * @param reason Short diagnostic describing why control stopped.
 */
static void stop_on_fault(const char *reason)
{
	faulted = true;
	reset_drive();
	int ret = motors_coast();

	printk("PID stopped: %s; coast=%d. Reset to restart.\n", reason, ret);
}

/**
 * @brief Compute one wheel's PID output using filtered derivative on measurement.
 * @param side Wheel index.
 * @param target Target speed in millirpm.
 * @param elapsed Sample interval in milliseconds.
 * @return Duty in the motor driver's unsigned raw scale.
 */
static uint32_t calculate_duty(unsigned int side, uint32_t target, int64_t elapsed)
{
	struct motor_pid_state *state = &states[side];
	const struct motor_pid_gains *gain = &gains[side];
	int64_t error = (int64_t)target - state->speed_mrpm;
	/* All terms below use thousandths of one duty-permille: full duty = 1000000. */
	int64_t p = (int64_t)gain->kp * error / 1000;
	int64_t d = -(int64_t)gain->kd * state->derivative_mrpm_per_s / 1000;
	int64_t ff = (int64_t)MOTOR_PID_FF_START_PERMILLE * 1000 +
		(int64_t)MOTOR_PID_FF_SPAN_PERMILLE * target / MOTOR_PID_MAX_RPM;
	int64_t limit = (int64_t)MOTOR_PID_MAX_DUTY_PERMILLE * 1000;
	int64_t i_limit = (int64_t)MOTOR_PID_I_LIMIT_PERMILLE * 1000;
	int64_t candidate = clamp_value(state->integral +
		(int64_t)gain->ki * error * elapsed / 1000000, -i_limit, i_limit);
	int64_t output = ff + p + candidate + d;

	/* Integrate only if unsaturated, or if the error is pulling out of saturation. */
	if ((output >= 0 && output <= limit) || (output > limit && error < 0) ||
	    (output < 0 && error > 0)) {
		state->integral = candidate;
	}
	output = clamp_value(ff + p + state->integral + d, 0, limit);
	return (output * MOTOR_DUTY_FULL_SCALE + 500000) / 1000000;
}

/**
 * @brief Sample encoders and regulate both wheels independently at the configured period.
 * @param arg1 Unused Zephyr thread argument.
 * @param arg2 Unused Zephyr thread argument.
 * @param arg3 Unused Zephyr thread argument.
 */
static void controller_worker(void *arg1, void *arg2, void *arg3)
{
	struct encoder_snapshot previous, sample;
	int64_t last_print_ms = 0;

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);
	encoders_snapshot(&previous);
	int64_t next_ms = previous.timestamp_ms;

	while (true) {
		next_ms += MOTOR_PID_PERIOD_MS;
		k_sleep(K_TIMEOUT_ABS_MS(next_ms));
		encoders_snapshot(&sample);
		int64_t elapsed = sample.timestamp_ms - previous.timestamp_ms;

		k_mutex_lock(&controller_lock, K_FOREVER);
		if (faulted) {
			k_mutex_unlock(&controller_lock);
			return;
		}
		if (elapsed <= 0 || elapsed >= MOTOR_PID_COMMAND_TIMEOUT_MS) {
			stop_on_fault("control-loop timing gap");
			k_mutex_unlock(&controller_lock);
			return;
		}
		if (have_command && k_uptime_get() - command_ms >= MOTOR_PID_COMMAND_TIMEOUT_MS) {
			stop_on_fault("UART command timeout");
		}
		if (!faulted && sample.read_errors != previous.read_errors) {
			stop_on_fault("encoder GPIO read error");
		}
		for (unsigned int side = 0; side < MOTOR_COUNT && !faulted; side++) {
			struct motor_pid_state *state = &states[side];
			int64_t delta = (sample.counts[side] - previous.counts[side]) *
				encoder_sign[side];
			int64_t speed = delta * 60000000LL / (ENCODER_COUNTS_PER_REV * elapsed);

			if (sample.invalid_transitions[side] - previous.invalid_transitions[side] >
			    MOTOR_PID_MAX_INVALID_EDGES ||
			    speed > (int64_t)MOTOR_PID_MAX_FEEDBACK_RPM * 1000 ||
			    speed < -(int64_t)MOTOR_PID_MAX_FEEDBACK_RPM * 1000) {
				stop_on_fault("invalid encoder feedback");
				break;
			}
			int64_t old_speed = state->speed_mrpm;

			state->speed_mrpm += (speed - state->speed_mrpm) * elapsed /
				(MOTOR_PID_SPEED_FILTER_MS + elapsed);
			int64_t derivative = (state->speed_mrpm - old_speed) * 1000 / elapsed;

			state->derivative_mrpm_per_s +=
				(derivative - state->derivative_mrpm_per_s) * elapsed /
				(MOTOR_PID_D_FILTER_MS + elapsed);
		}
		if (!faulted && (!have_command || requested_mrpm == 0U)) {
			reset_drive();
			if (motors_coast() != 0) {
				stop_on_fault("motor coast failed");
			}
		} else if (!faulted) {
			if (requested_mrpm < ramped_mrpm) {
				ramped_mrpm = requested_mrpm;
				/* A lower pedal request must not retain the previous load's integral. */
				for (unsigned int side = 0; side < MOTOR_COUNT; side++) {
					states[side].integral = 0;
				}
			} else {
				ramped_mrpm = MIN(requested_mrpm, ramped_mrpm +
					(uint32_t)(MOTOR_PID_ACCEL_RPM_PER_SEC * elapsed));
			}
			for (unsigned int side = 0; side < MOTOR_COUNT; side++) {
				struct motor_pid_state *state = &states[side];

				state->duty = calculate_duty(side, ramped_mrpm, elapsed);
				bool stalled = ramped_mrpm >= MOTOR_PID_STALL_TARGET_RPM * 1000U &&
					state->speed_mrpm < (int64_t)MOTOR_PID_STALL_RPM * 1000 &&
					state->duty >= (uint64_t)MOTOR_PID_STALL_DUTY_PERMILLE *
						MOTOR_DUTY_FULL_SCALE / 1000U;
				bool reversed = state->speed_mrpm < -(int64_t)MOTOR_PID_REVERSE_RPM * 1000;

				state->stall_ms = stalled ? state->stall_ms + elapsed : 0;
				state->reverse_ms = reversed ? state->reverse_ms + elapsed : 0;
				if (state->reverse_ms >= MOTOR_PID_REVERSE_TIMEOUT_MS) {
					stop_on_fault(side == MOTOR_LEFT ? "left encoder direction reversed" :
						      "right encoder direction reversed");
					break;
				}
				if (state->stall_ms >= MOTOR_PID_STALL_TIMEOUT_MS) {
					stop_on_fault(side == MOTOR_LEFT ? "left wheel stalled" :
						      "right wheel stalled");
					break;
				}
				if (motor_drive_raw((enum motor_side)side, state->duty) != 0) {
					stop_on_fault("motor PWM update failed");
					break;
				}
			}
		}
		uint32_t target = ramped_mrpm;
		int32_t left_speed = states[MOTOR_LEFT].speed_mrpm;
		int32_t right_speed = states[MOTOR_RIGHT].speed_mrpm;
		uint32_t left_duty = states[MOTOR_LEFT].duty * 1000U / MOTOR_DUTY_FULL_SCALE;
		uint32_t right_duty = states[MOTOR_RIGHT].duty * 1000U / MOTOR_DUTY_FULL_SCALE;
		bool stopped = faulted;

		k_mutex_unlock(&controller_lock);
		if (stopped) {
			return;
		}
		if (sample.timestamp_ms - last_print_ms >= MOTOR_PID_PRINT_MS) {
			printk("PID target=%u mRPM L=%d mRPM duty=%u/1000 R=%d mRPM duty=%u/1000\n",
			       target, left_speed, left_duty, right_speed, right_duty);
			last_print_ms = sample.timestamp_ms;
		}
		previous = sample;
		if (next_ms < sample.timestamp_ms) {
			next_ms = sample.timestamp_ms;
		}
	}
}

/**
 * @brief Start the worker after drivers are initialized; call once from main.
 * @return 0 on success, or -EALREADY if already started.
 */
int motor_controller_start(void)
{
	if (started) {
		return -EALREADY;
	}
	started = true;
	k_thread_create(&controller_thread, controller_stack,
			K_THREAD_STACK_SIZEOF(controller_stack), controller_worker,
			NULL, NULL, NULL, MOTOR_PID_PRIORITY, 0, K_NO_WAIT);
	return 0;
}

/**
 * @brief Accept a fresh, bounded speed command; zero immediately coasts both wheels.
 * @param target_mrpm Wheel-speed demand in millirpm.
 * @param received_ms Actual reception uptime of the command packet.
 * @return 0 on success, or a negative argument, freshness, state, or driver error.
 */
int motor_controller_set_target(uint32_t target_mrpm, int64_t received_ms)
{
	int ret = 0;

	if (target_mrpm > MOTOR_PID_MAX_RPM * 1000U) {
		return -EINVAL;
	}
	k_mutex_lock(&controller_lock, K_FOREVER);
	int64_t now = k_uptime_get();

	if (!started) {
		ret = -ENODEV;
	} else if (faulted) {
		ret = -EIO;
	} else if (received_ms > now || now - received_ms >= MOTOR_PID_COMMAND_TIMEOUT_MS ||
		   (have_command && received_ms < command_ms)) {
		ret = -ESTALE;
	} else {
		requested_mrpm = target_mrpm;
		command_ms = received_ms;
		have_command = true;
		if (target_mrpm == 0U) {
			reset_drive();
			ret = motors_coast();
			if (ret != 0) {
				faulted = true;
			}
		}
	}
	k_mutex_unlock(&controller_lock);
	return ret;
}

/**
 * @brief Permanently disable speed control for this boot and coast both outputs.
 * @return 0 on successful coast, or a negative motor-driver error.
 */
int motor_controller_stop(void)
{
	k_mutex_lock(&controller_lock, K_FOREVER);
	faulted = true;
	reset_drive();
	int ret = motors_coast();

	k_mutex_unlock(&controller_lock);
	return ret;
}
