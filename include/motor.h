#ifndef LAB_MOTOR_H_
#define LAB_MOTOR_H_

#include <stdint.h>

#define MOTOR_DUTY_FULL_SCALE 65535U

enum motor_side { MOTOR_LEFT, MOTOR_RIGHT, MOTOR_COUNT };

/* Thread context only. Calls serialize; never invoke from a GPIO/UART ISR.
 * Errors are negative errno values. Initialization leaves both enables LOW.
 * This driver provides output control, not a PID loop or link-loss watchdog.
 */
int motors_init(void);

/* Signed duty in thousandths: +200 = 20%, -200 = reverse 20%, 0 = coast.
 * Out-of-range values are rejected without changing outputs.
 * -EPERM while brake latched, -EIO after a hardware fault.
 * Direct powered reversal returns -EBUSY: coast/brake and wait for the
 * encoder to show a stopped wheel before requesting the opposite direction.
 */
int motor_drive(enum motor_side side, int duty_permille);

/**
 * @brief Set signed motor duty with full 16-bit magnitude resolution.
 * @param side Motor to drive.
 * @param duty_raw -65535..65535; 0 coasts, 65535 is full forward duty.
 * @return 0 on success, negative errno on failure, as for motor_drive().
 * The pulse width is rounded to the nearest available PWM timer tick.
 */
int motor_drive_raw(enum motor_side side, int32_t duty_raw);

/* Latch braking on BOTH wheels: equal direction inputs and enable HIGH.
 * Subsequent drive commands cannot override it. Hardware failures attempt
 * to disable both enables instead; software cannot guarantee a failed output.
 */
int motors_brake(void);

/* Clear braking into coast, never automatically resume a previous duty. */
int motors_release_brake(void);

/* Disable both enables. Does not override a latched brake (-EPERM). */
int motors_coast(void);

#endif
