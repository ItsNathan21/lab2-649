/** @file pedal_control.c
 * @brief Convert pedal positions into a shared forward wheel-speed request.
 */
#include "pedal_control.h"
#include "motor_controller.h"
#include "wheel_info.h"

_Static_assert(PEDAL_DEADBAND_RAW < PEDAL_TRAVEL_FULL_SCALE, "Pedal deadband is too large");

/**
 * @brief Normalize pedal travel after removing the released-end deadband.
 * @param raw Signed pedal reading; both pedals share the same measured endpoints.
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

/**
 * @brief Scale throttle into RPM demand, then reduce demand by the brake fraction.
 * @param throttle Raw throttle in -32768..32767.
 * @param brake Raw brake in -32768..32767.
 * @return Requested speed in millirpm; the motor controller owns PWM and acceleration.
 */
uint32_t pedal_control_target(int16_t throttle, int16_t brake)
{
	uint32_t travel = pedal_travel(throttle);
	uint32_t braking = pedal_travel(brake);
	uint32_t speed = ((uint64_t)travel * MOTOR_PID_MAX_RPM * 1000U +
		PEDAL_TRAVEL_FULL_SCALE / 2U) / PEDAL_TRAVEL_FULL_SCALE;

	return ((uint64_t)speed * (PEDAL_TRAVEL_FULL_SCALE - braking) +
		PEDAL_TRAVEL_FULL_SCALE / 2U) / PEDAL_TRAVEL_FULL_SCALE;
}
