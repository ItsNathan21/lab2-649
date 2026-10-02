/** @file test_points.h
 * @brief Scope markers for command reception and right-motor timer writes.
 */
#ifndef TEST_POINTS_H_
#define TEST_POINTS_H_

/** @brief Configure markers low before motor initialization or UART interrupts.
 * @return 0 on success, or a negative GPIO error.
 */
int test_points_init(void);
/** @brief Toggle CMD_RX from the UART ISR after a CRC-valid command frame. */
void test_points_command_received(void);
/** @brief Toggle PWM_SET immediately after a successful right-motor timer write. */
void test_points_pwm_written(void);

#endif /* TEST_POINTS_H_ */
