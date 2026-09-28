/** @file uart_receiver.h
 * @brief Framed command reception, self-test, and link fail-safe.
 */
#ifndef UART_RECEIVER_H_
#define UART_RECEIVER_H_
#include <stdint.h>
#include "uart_protocol.h"
#include "wheel_info.h"

/** @brief Stack bytes; 2048 is an initial budget for command handling and diagnostics. */
#define WHEEL_RX_STACK_SIZE 2048
/** @brief Priority 3 handles faults ahead of motor PID and reporting threads. */
#define WHEEL_RX_PRIORITY 3
/** @brief Sixteen complete frames tolerate short scheduling delays. */
#define WHEEL_RX_QUEUE_SIZE 16
/** @brief A 5 ms watchdog check leaves margin below the 100 ms fail-safe deadline. */
#define WHEEL_RX_POLL_MS 5
/** @brief Two distinct Y press edges within 500 ms constitute a double press. */
#define WHEEL_SELF_TEST_DOUBLE_MS 500
/** @brief Accept only the eight recorded wheel buttons. */
#define WHEEL_BUTTONS_MASK \
	(WHEEL_BUTTON_A | WHEEL_BUTTON_B | WHEEL_BUTTON_X | WHEEL_BUTTON_Y | \
	 WHEEL_BUTTON_RIGHT_BLINKER | WHEEL_BUTTON_LEFT_BLINKER | \
	 WHEEL_BUTTON_RSB | WHEEL_BUTTON_LSB)

/** @brief Complete validated frame with its ISR reception timestamp. */
struct wheel_rx_packet {
	struct uart_frame frame;
	int64_t timestamp_ms;
};
/**
 * @brief Initialize UART reception in fail-safe until fresh commands arrive.
 * @return 0 on success, or a negative device/driver/state error.
 */
int uart_receiver_start(void);
/**
 * @brief Read the fault bitmask without blocking the status heartbeat worker.
 * @return UART_FAULT_* bits; zero means normal.
 */
uint8_t uart_receiver_faults(void);
#endif /* UART_RECEIVER_H_ */
