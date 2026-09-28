/** @file uart_receiver.h
 * @brief UART receiver API and constants.
 */
#ifndef UART_RECEIVER_H_
#define UART_RECEIVER_H_

#include <stdint.h>
#include "wheel_info.h"

/** @brief Stack bytes; 1536 is the initial budget, not a measured maximum. */
#define WHEEL_RX_STACK_SIZE   1536
/** @brief Preemptive priority 5 keeps command handling above routine reporting. */
#define WHEEL_RX_PRIORITY     5
/** @brief Buffer 16 packets to tolerate short scheduling delays; initial chosen capacity. */
#define WHEEL_RX_QUEUE_SIZE   16
/** @brief Stop after 150 ms without a fresh command, allowing about two sender periods. */
#define WHEEL_LINK_TIMEOUT_MS 150
/** @brief Check receive faults at least every 20 ms; initial chosen polling interval. */
#define WHEEL_RX_POLL_MS 20
/** @brief Limit throttle logging to 10 Hz to reduce console traffic. */
#define WHEEL_RX_PRINT_MS 100
/** @brief Accept only the eight recorded wheel buttons to reject invalid packet bits. */
#define WHEEL_BUTTONS_MASK \
	(WHEEL_BUTTON_A | WHEEL_BUTTON_B | WHEEL_BUTTON_X | WHEEL_BUTTON_Y | \
	 WHEEL_BUTTON_RIGHT_BLINKER | WHEEL_BUTTON_LEFT_BLINKER | \
	 WHEEL_BUTTON_RSB | WHEEL_BUTTON_LSB)

/** @brief Queued wire bytes with their ISR reception time for freshness checks. */
struct wheel_rx_packet {
	uint8_t bytes[sizeof(struct wheel_info)];
	int64_t timestamp_ms;
};

/**
 * @brief Start UART throttle control once, after motor and encoder initialization.
 * Call from main only; a receive fault stops the worker until reset.
 * @return 0 when started, or -EALREADY if already started.
 */
int uart_receiver_start(void);

#endif /* UART_RECEIVER_H_ */
