/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
/** @file uart_status.h
 * @brief Periodic STM-to-Pi status heartbeat worker.
 */
#ifndef UART_STATUS_H_
#define UART_STATUS_H_
/** @brief Stack bytes; 1024 is an initial budget for the short status serializer. */
#define UART_STATUS_STACK_SIZE 1024
/** @brief Priority 2 keeps heartbeat transmission ahead of control and console reporting. */
#define UART_STATUS_PRIORITY 2
/**
 * @brief Start status transmission after uart_receiver_start; call once.
 * @return 0 on success, or a negative state/device error.
 */
int uart_status_start(void);
#endif /* UART_STATUS_H_ */
