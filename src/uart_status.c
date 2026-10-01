/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
/** @file uart_status.c
 * @brief Absolute-period status transmission independent of command reception.
 */
#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include "uart_protocol.h"
#include "uart_receiver.h"
#include "uart_status.h"
#include "current_sensor.h"

_Static_assert(CURRENT_SENSOR_COUNT == UART_CURRENT_COUNT, "Current channel count mismatch");

static const struct device *const output = DEVICE_DT_GET(DT_NODELABEL(usart1));
static K_THREAD_STACK_DEFINE(status_stack, UART_STATUS_STACK_SIZE);
static struct k_thread status_thread;
static bool started;

/** @brief Send fault bits and a changing sequence every 20 ms, including during faults.
 * @param arg1 Unused thread argument.
 * @param arg2 Unused thread argument.
 * @param arg3 Unused thread argument.
 */
static void status_worker(void *arg1, void *arg2, void *arg3)
{
	struct uart_frame frame = {.type = UART_FRAME_STATUS};
	int64_t next_ms = k_uptime_get();

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);
	while (true) {
		uint8_t bytes[UART_FRAME_SIZE];
		struct current_sensor_snapshot sample;
		struct uart_current_status currents = {0};

		current_sensor_snapshot(&sample);
		currents.valid_mask = sample.valid_mask;
		for (unsigned int i = 0; i < UART_CURRENT_COUNT; i++) {
			currents.milliamps[i] = sample.milliamps[i];
		}
		uart_current_encode(&currents, frame.payload);
		frame.flags = uart_receiver_faults();
		uart_frame_encode(&frame, bytes);
		for (size_t i = 0; i < sizeof(bytes); i++) {
			uart_poll_out(output, bytes[i]);
		}
		frame.sequence++;
		next_ms += UART_LINK_PERIOD_MS;
		if (next_ms <= k_uptime_get()) {
			next_ms = k_uptime_get() + UART_LINK_PERIOD_MS;
		}
		k_sleep(K_TIMEOUT_ABS_MS(next_ms));
	}
}

/** @brief Start the dedicated heartbeat worker once.
 * @return 0 on success, or a negative state/device error.
 */
int uart_status_start(void)
{
	if (started) {
		return -EALREADY;
	}
	if (!device_is_ready(output)) {
		return -ENODEV;
	}
	started = true;
	k_thread_create(&status_thread, status_stack, K_THREAD_STACK_SIZEOF(status_stack),
			status_worker, NULL, NULL, NULL, UART_STATUS_PRIORITY, 0, K_NO_WAIT);
	return 0;
}
