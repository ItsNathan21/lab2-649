/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
/** @file trace_gpio.h
 * @brief Optional Raspberry Pi 4B timing markers for logic-analyser measurements.
 */
#ifndef TRACE_GPIO_H_
#define TRACE_GPIO_H_

/** @brief Pi 4B header GPIO controller; change if the OS assigns another chip number. */
#define TRACE_GPIO_CHIP "/dev/gpiochip0"
/** @brief GPIO23 (header pin 16) is an initially selected spare UDP reception marker. */
#define TRACE_GPIO_UDP_RX 23U
/** @brief GPIO24 (header pin 18) is an initially selected spare command-write marker. */
#define TRACE_GPIO_CMD_TX 24U

/** @brief Indices of independently toggled timing outputs. */
enum trace_gpio_event {
	TRACE_UDP_RX,
	TRACE_CMD_TX,
};

/** @brief Claim both markers as initially low outputs; call once before the loop.
 * @return 0 on success, -1 with errno set on failure.
 */
int trace_gpio_init(void);
/** @brief Toggle one marker; does nothing when tracing has not been initialized.
 * @param event Marker to toggle.
 * @return 0 on success, -1 with errno set on failure.
 */
int trace_gpio_toggle(enum trace_gpio_event event);
/** @brief Drive markers low and release their GPIO request at process exit. */
void trace_gpio_close(void);

#endif /* TRACE_GPIO_H_ */
