/** @file servo.c
 * @brief Bounded steering-to-pulse conversion using hardware PWM on PA1.
 */
#include <errno.h>
#include <stdbool.h>
#include <zephyr/drivers/pwm.h>
#include "servo.h"
#include "wheel_info.h"

_Static_assert(SERVO_MIN_PULSE_US > 0U && SERVO_MIN_PULSE_US < SERVO_CENTER_PULSE_US &&
	       SERVO_CENTER_PULSE_US < SERVO_MAX_PULSE_US, "Invalid servo pulse limits");
_Static_assert(SERVO_MAX_PULSE_US <= UINT32_MAX / 1000U, "Servo pulse exceeds PWM range");
_Static_assert(SERVO_REVERSED == 0 || SERVO_REVERSED == 1, "Servo reversal must be 0 or 1");

static const struct pwm_dt_spec output = PWM_DT_SPEC_GET(DT_NODELABEL(steering_servo));
static bool initialized;
static uint32_t applied_pulse_ns;

/**
 * @brief Convert signed steering with separate half-ranges to preserve exact center and ends.
 * @param steering Signed raw wheel position.
 * @return Pulse width in nanoseconds, bounded by the calibrated minimum and maximum.
 */
static uint32_t steering_pulse(int16_t steering)
{
	uint32_t left = SERVO_REVERSED ? SERVO_MAX_PULSE_US : SERVO_MIN_PULSE_US;
	uint32_t right = SERVO_REVERSED ? SERVO_MIN_PULSE_US : SERVO_MAX_PULSE_US;
	int64_t center = (int64_t)SERVO_CENTER_PULSE_US * 1000;
	int64_t endpoint;
	int32_t travel;
	int32_t range;

	if (steering < WHEEL_STEERING_CENTER) {
		endpoint = (int64_t)left * 1000;
		travel = -(int32_t)steering;
		range = -(int32_t)WHEEL_STEERING_LEFT;
	} else {
		endpoint = (int64_t)right * 1000;
		travel = steering;
		range = WHEEL_STEERING_RIGHT;
	}
	return center + (endpoint - center) * travel / range;
}

/**
 * @brief Configure center PWM before the UART worker can send steering requests.
 * @return 0 on success, -EALREADY if initialized, or a negative configuration/PWM error.
 */
int servo_init(void)
{
	if (initialized) {
		return -EALREADY;
	}
	if (!pwm_is_ready_dt(&output)) {
		return -ENODEV;
	}
	if (PWM_USEC(SERVO_MAX_PULSE_US) >= output.period) {
		return -EINVAL;
	}
	int ret = pwm_set_pulse_dt(&output, PWM_USEC(SERVO_CENTER_PULSE_US));

	if (ret == 0) {
		applied_pulse_ns = PWM_USEC(SERVO_CENTER_PULSE_US);
		initialized = true;
	}
	return ret;
}

/**
 * @brief Apply one steering position; only the UART worker calls this after initialization.
 * @param steering Raw signed steering from the wheel packet.
 * @return 0 on success, -ENODEV before initialization, or a negative PWM error.
 */
int servo_set_steering(int16_t steering)
{
	if (!initialized) {
		return -ENODEV;
	}
	uint32_t pulse = steering_pulse(steering);

	if (pulse == applied_pulse_ns) {
		return 0;
	}
	int ret = pwm_set_pulse_dt(&output, pulse);

	if (ret == 0) {
		applied_pulse_ns = pulse;
	}
	return ret;
}

/**
 * @brief Disable pulses when UART control stops; only the UART worker calls this.
 * @return 0 on success, -ENODEV before initialization, or a negative PWM error.
 */
int servo_disable(void)
{
	if (!initialized) {
		return -ENODEV;
	}
	int ret = pwm_set_pulse_dt(&output, 0U);

	if (ret == 0) {
		applied_pulse_ns = 0U;
	}
	return ret;
}

/**
 * @brief Estimate logical steering angle from calibrated PWM displacement.
 * @param steering Signed wheel position.
 * @return Signed millidegrees relative to center; this is not measured feedback.
 */
int32_t servo_angle_mdeg(int16_t steering)
{
	int64_t displacement = (int64_t)steering_pulse(steering) -
		(int64_t)SERVO_CENTER_PULSE_US * 1000;

	return displacement * SERVO_MDEG_PER_US / 1000 * (SERVO_REVERSED ? -1 : 1);
}
