/** @file pedal_control.h
 * @brief Pedal calibration and wheel-speed request API.
 */
#ifndef PEDAL_CONTROL_H_
#define PEDAL_CONTROL_H_

#include <stdint.h>

/** @brief Ignore the first 2% of full raw travel to reject released-pedal jitter. */
#define PEDAL_DEADBAND_RAW 1311U
/** @brief Full 16-bit pedal travel spans 65535 increments between the measured endpoints. */
#define PEDAL_TRAVEL_FULL_SCALE 65535U

/** @brief Raw 20000 gives full braking, matching the stiff pedal's requested usable travel. */
#define PEDAL_BRAKE_FULL_RAW 20000

/**
 * @brief Map the shorter brake travel to electrical braking duty.
 * @param brake Raw reading: 32767 released, 20000 or lower fully braked.
 * @return Duty in 0..65535 with the released-end deadband removed.
 */
uint32_t pedal_control_brake(int16_t brake);

/**
 * @brief Map throttle to wheel speed; any brake request overrides forward drive.
 * @param throttle Raw throttle: 32767 released, -32768 fully pressed.
 * @param brake Raw brake: 32767 released, 20000 or lower fully braked.
 * @return Millirpm in 0..MOTOR_PID_MAX_RPM * 1000; released throttle or any braking gives zero.
 */
uint32_t pedal_control_target(int16_t throttle, int16_t brake);

#endif /* PEDAL_CONTROL_H_ */
