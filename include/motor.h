/** @file motor.h
 * @brief Motor API and constants.
 */
#ifndef MOTOR_H_
#define MOTOR_H_

#include <stdint.h>

/** @brief Full duty is 65535 to preserve every step of the 16-bit throttle range. */
#define MOTOR_DUTY_FULL_SCALE 65535U

/** @brief Indices for the two wheel channels and their count. */
enum motor_side {
	MOTOR_LEFT,
	MOTOR_RIGHT,
	MOTOR_COUNT
};

/* Thread context only. Calls serialize; never invoke from a GPIO/UART ISR.
 * Errors are negative errno values. Initialization leaves both enables LOW.
 * This driver provides output control, not a PID loop or link-loss watchdog.
 */
/**
 * @brief Initialize both motor outputs in coast; call from thread context.
 * @return 0 on success, or a negative initialization error.
 */
int motors_init(void);

/* Signed duty in thousandths: +200 = 20%, -200 = reverse 20%, 0 = coast.
 * Out-of-range values are rejected without changing outputs.
 * -EPERM while brake latched, -EIO after a hardware fault.
 * Direct powered reversal returns -EBUSY: coast/brake and wait for the
 * encoder to show a stopped wheel before requesting the opposite direction.
 */
/**
 * @brief Set motor duty in signed thousandths from thread context.
 * @param side MOTOR_LEFT or MOTOR_RIGHT.
 * @param duty_permille Duty in -1000..1000; zero coasts.
 * @return 0 on success, or a negative error; see motor.h for interlocks.
 */
int motor_drive(enum motor_side side, int duty_permille);

/**
 * @brief Set signed duty, rounded to the nearest available PWM tick.
 * @param side MOTOR_LEFT or MOTOR_RIGHT.
 * @param duty_raw Signed duty in -65535..65535; zero coasts.
 * @return 0 on success, or a negative error; see motor.h for interlocks.
 */
int motor_drive_raw(enum motor_side side, int32_t duty_raw);

/* Latch braking on BOTH wheels: equal direction inputs and enable HIGH.
 * Subsequent drive commands cannot override it. Hardware failures attempt
 * to disable both enables instead; software cannot guarantee a failed output.
 */
/**
 * @brief Latch braking on both wheels until explicitly released.
 * @return 0 on success, or a negative motor error.
 */
int motors_brake(void);

/**
 * @brief Latch proportional dynamic braking on both wheels using enable PWM.
 * @param duty_raw Brake duty in 1..65535; release through motors_release_brake().
 * @return 0 on success, or a negative argument/driver error.
 */
int motors_brake_raw(uint32_t duty_raw);

/* Clear braking into coast, never automatically resume a previous duty. */
/**
 * @brief Clear the brake latch into coast without resuming old duty.
 * @return 0 on success, or a negative motor error.
 */
int motors_release_brake(void);

/* Disable both enables. Does not override a latched brake (-EPERM). */
/**
 * @brief Disable both enables without overriding a latched brake.
 * @return 0 on success, -EPERM while braking, or a driver error.
 */
int motors_coast(void);

#endif /* MOTOR_H_ */
