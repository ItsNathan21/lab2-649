/** @file pedal_control.h
 * @brief Pedal calibration and wheel-speed request API.
 */
#ifndef PEDAL_CONTROL_H_
#define PEDAL_CONTROL_H_

#include <stdint.h>

/** @brief Ignore the first 2% of either pedal; initial choice to reject released-pedal jitter. */
#define PEDAL_DEADBAND_RAW 1311U
/** @brief Full 16-bit pedal travel spans 65535 increments between the measured endpoints. */
#define PEDAL_TRAVEL_FULL_SCALE 65535U

/**
 * @brief Map throttle to wheel speed and reduce the request proportionally with brake travel.
 * @param throttle Raw throttle: 32767 released, -32768 fully pressed.
 * @param brake Raw brake: 32767 released, -32768 fully pressed.
 * @return Millirpm in 0..MOTOR_PID_MAX_RPM * 1000; released throttle or full brake gives zero.
 */
uint32_t pedal_control_target(int16_t throttle, int16_t brake);

#endif /* PEDAL_CONTROL_H_ */
