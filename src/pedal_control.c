/** @file pedal_control.c
 * @brief Convert pedal positions into a shared forward wheel-speed request.
 */
#include "pedal_control.h"
#include "motor_controller.h"
#include "motor.h"
#include "wheel_info.h"

_Static_assert(PEDAL_DEADBAND_RAW < PEDAL_TRAVEL_FULL_SCALE, "Pedal deadband is too large");

/**
 * @brief Normalize pedal travel after removing the released-end deadband.
 * @param raw Signed pedal reading; the throttle uses the full measured axis range.
 * @return Travel in 0..PEDAL_TRAVEL_FULL_SCALE.
 */
static uint32_t pedal_travel(int16_t raw)
{
	uint32_t travel = (int32_t)WHEEL_THROTTLE_RELEASED - (int32_t)raw;
	uint32_t range = PEDAL_TRAVEL_FULL_SCALE - PEDAL_DEADBAND_RAW;

	if (travel <= PEDAL_DEADBAND_RAW) {
		return 0U;
	}
	return ((uint64_t)(travel - PEDAL_DEADBAND_RAW) * PEDAL_TRAVEL_FULL_SCALE +
		range / 2U) / range;
}

_Static_assert(PEDAL_BRAKE_FULL_RAW >= INT16_MIN &&
	       PEDAL_BRAKE_FULL_RAW < WHEEL_BRAKE_RELEASED - (int32_t)PEDAL_DEADBAND_RAW,
	       "Invalid brake calibration");

/**
 * @brief Scale the brake's shorter calibrated travel into dynamic braking duty.
 * @param brake Raw signed brake reading.
 * @return Duty in 0..MOTOR_DUTY_FULL_SCALE, saturated at the calibrated endpoint.
 */
uint32_t pedal_control_brake(int16_t brake)
{
	int32_t start = WHEEL_BRAKE_RELEASED - (int32_t)PEDAL_DEADBAND_RAW;

	if (brake >= start) {
		return 0U;
	}
	if (brake <= PEDAL_BRAKE_FULL_RAW) {
		return MOTOR_DUTY_FULL_SCALE;
	}
	uint32_t range = start - PEDAL_BRAKE_FULL_RAW;
	uint32_t travel = start - brake;

	return ((uint64_t)travel * MOTOR_DUTY_FULL_SCALE + range / 2U) / range;
}

/**
 * @brief Scale throttle into RPM demand, giving brake priority over forward drive.
 * @param throttle Raw throttle in -32768..32767.
 * @param brake Raw brake in -32768..32767.
 * @return Speed in millirpm; any nonzero brake request suppresses drive.
 */
uint32_t pedal_control_target(int16_t throttle, int16_t brake)
{
	if (pedal_control_brake(brake) != 0U) {
		return 0U;
	}
	uint32_t travel = pedal_travel(throttle);

	return ((uint64_t)travel * MOTOR_PID_MAX_RPM * 1000U +
		PEDAL_TRAVEL_FULL_SCALE / 2U) / PEDAL_TRAVEL_FULL_SCALE;
}
