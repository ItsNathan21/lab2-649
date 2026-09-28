/** @file pedal_control.c
 * @brief Calibrated pedal mapping and a time-based motor-duty ramp.
 */
#include "pedal_control.h"
#include "motor.h"
#include "wheel_info.h"

_Static_assert(PEDAL_DEADBAND_RAW < MOTOR_DUTY_FULL_SCALE, "Pedal deadband is too large");
_Static_assert(PEDAL_START_DUTY_PERMILLE < 1000U, "Start duty must leave room to accelerate");
_Static_assert(PEDAL_ACCEL_TIME_MS > 0U, "Acceleration time must be positive");

/**
 * @brief Normalize pedal travel after removing the released-end deadband.
 * @param raw Signed pedal reading; both pedals share the same measured endpoints.
 * @return Travel in 0..MOTOR_DUTY_FULL_SCALE.
 */
static uint32_t pedal_travel(int16_t raw)
{
	uint32_t travel = (int32_t)WHEEL_THROTTLE_RELEASED - (int32_t)raw;
	uint32_t range = MOTOR_DUTY_FULL_SCALE - PEDAL_DEADBAND_RAW;

	if (travel <= PEDAL_DEADBAND_RAW) {
		return 0U;
	}
	return ((uint64_t)(travel - PEDAL_DEADBAND_RAW) * MOTOR_DUTY_FULL_SCALE +
		range / 2U) / range;
}

/**
 * @brief Convert the measured starting threshold into the driver's raw duty scale.
 * @return Starting duty, rounded to the nearest raw duty step.
 */
static uint32_t starting_duty(void)
{
	return (MOTOR_DUTY_FULL_SCALE * PEDAL_START_DUTY_PERMILLE + 500U) / 1000U;
}

/**
 * @brief Remap throttle travel and multiply its duty by the unpressed brake fraction.
 * @param throttle Raw throttle in -32768..32767.
 * @param brake Raw brake in -32768..32767.
 * @return Target duty in 0..MOTOR_DUTY_FULL_SCALE.
 */
uint32_t pedal_control_target(int16_t throttle, int16_t brake)
{
	uint32_t travel = pedal_travel(throttle);
	uint32_t braking = pedal_travel(brake);
	uint32_t floor = starting_duty();
	uint32_t duty;

	if (travel == 0U) {
		return 0U;
	}
	duty = floor + ((uint64_t)travel * (MOTOR_DUTY_FULL_SCALE - floor) +
			MOTOR_DUTY_FULL_SCALE / 2U) / MOTOR_DUTY_FULL_SCALE;
	/* Apply brake reduction after the starting boost; never restore a minimum here. */
	return ((uint64_t)duty * (MOTOR_DUTY_FULL_SCALE - braking) +
		MOTOR_DUTY_FULL_SCALE / 2U) / MOTOR_DUTY_FULL_SCALE;
}

/**
 * @brief Skip the ineffective starting range and rate-limit subsequent duty increases.
 * @param current Previously applied duty in 0..MOTOR_DUTY_FULL_SCALE.
 * @param target Brake-adjusted requested duty in 0..MOTOR_DUTY_FULL_SCALE.
 * @param elapsed_ms Milliseconds since the previous update.
 * @return Next duty; decreases and zero targets take effect immediately.
 */
uint32_t pedal_control_ramp(uint32_t current, uint32_t target, uint32_t elapsed_ms)
{
	uint32_t floor = starting_duty();
	uint32_t step;

	if (target <= current) {
		return target;
	}
	if (current < floor) {
		current = target < floor ? target : floor;
	}
	if (elapsed_ms >= PEDAL_ACCEL_TIME_MS) {
		return target;
	}
	step = (uint64_t)(MOTOR_DUTY_FULL_SCALE - floor) * elapsed_ms /
		PEDAL_ACCEL_TIME_MS;
	return target - current <= step ? target : current + step;
}
