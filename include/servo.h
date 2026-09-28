/** @file servo.h
 * @brief Steering-servo calibration and PWM API.
 */
#ifndef SERVO_H_
#define SERVO_H_

#include <stdint.h>

/** @brief LD-1501MG nominal 0-degree endpoint is 500 us; restrict it for linkage clearance. */
#define SERVO_MIN_PULSE_US    500U
/** @brief LD-1501MG nominal midpoint is 1500 us; adjust for mechanical centering. */
#define SERVO_CENTER_PULSE_US 1500U
/** @brief LD-1501MG nominal 180-degree endpoint is 2500 us; restrict it for linkage clearance. */
#define SERVO_MAX_PULSE_US    2500U
/** @brief Set to 1 to reverse steering; initial 0 maps wheel-left to the shorter pulse. */
#define SERVO_REVERSED        0

/**
 * @brief Initialize the PWM output at the configured center; call once from main.
 * @return 0 on success, -EALREADY if initialized, or a negative device/configuration error.
 */
int servo_init(void);

/**
 * @brief Map wheel steering into the configured pulse limits; hardware repeats the PWM.
 * @param steering Signed wheel position: -32768 left, 0 center, 32767 right.
 * @return 0 on success, -ENODEV before initialization, or a negative PWM error.
 */
int servo_set_steering(int16_t steering);

/**
 * @brief Stop sending servo pulses; mechanical holding behavior depends on the servo model.
 * @return 0 on success, -ENODEV before initialization, or a negative PWM error.
 */
int servo_disable(void);

/** @brief LD-1501MG nominal travel is 180 degrees over a 2000 us pulse span. */
#define SERVO_MDEG_PER_US 90
/**
 * @brief Estimate commanded angle relative to center, without physical feedback.
 * @param steering Signed wheel position.
 * @return Millidegrees: negative for logical left, positive for logical right.
 */
int32_t servo_angle_mdeg(int16_t steering);

#endif /* SERVO_H_ */
