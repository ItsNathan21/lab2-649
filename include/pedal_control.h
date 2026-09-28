/** @file pedal_control.h
 * @brief Pedal calibration and duty-ramp API.
 */
#ifndef PEDAL_CONTROL_H_
#define PEDAL_CONTROL_H_

#include <stdint.h>

/** @brief Start at 50% duty, matching the observed motor start near raw throttle zero. */
#define PEDAL_START_DUTY_PERMILLE 500U
/** @brief Ignore the first 2% of either pedal; initial choice to reject released-pedal jitter. */
#define PEDAL_DEADBAND_RAW 1311U
/** @brief Ramp from 50% to 100% duty in 1000 ms; initial acceleration tuning choice. */
#define PEDAL_ACCEL_TIME_MS 1000U

/**
 * @brief Map throttle to 50..100% duty and proportionally reduce it with the brake.
 * @param throttle Raw throttle: 32767 released, -32768 fully pressed.
 * @param brake Raw brake: 32767 released, -32768 fully pressed.
 * @return Duty in 0..MOTOR_DUTY_FULL_SCALE; released throttle or full brake gives zero.
 */
uint32_t pedal_control_target(int16_t throttle, int16_t brake);

/**
 * @brief Raise duty gradually above the starting floor; apply decreases immediately.
 * @param current Previously applied duty in 0..MOTOR_DUTY_FULL_SCALE.
 * @param target Requested duty in 0..MOTOR_DUTY_FULL_SCALE, already reduced by braking.
 * @param elapsed_ms Time since the previous update, in milliseconds.
 * @return Next duty, never above target; brake-reduced targets may be below the start floor.
 */
uint32_t pedal_control_ramp(uint32_t current, uint32_t target, uint32_t elapsed_ms);

#endif /* PEDAL_CONTROL_H_ */
