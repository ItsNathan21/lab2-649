/** @file uart_receiver.c
 * @brief USART1 packet reception and pedal control worker.
 */
#include <errno.h>
#include <stdbool.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>

#include "blinker.h"
#include "wheel_info.h"
#include "uart_receiver.h"
#include "motor.h"
#include "pedal_control.h"

static const struct device *const wheel_uart = DEVICE_DT_GET(DT_NODELABEL(usart1));

K_MSGQ_DEFINE_STATIC(wheel_rx_queue, sizeof(struct wheel_rx_packet), WHEEL_RX_QUEUE_SIZE, 8);
static K_THREAD_STACK_DEFINE(receiver_stack, WHEEL_RX_STACK_SIZE);
static struct k_thread receiver_thread;
static bool started;
static atomic_t wheel_rx_fault;

/**
 * @brief Queue complete UART packets and latch receive faults.
 * @param dev USART1 device.
 * @param user_data Callback context; unused.
 */
static void wheel_uart_callback(const struct device *dev, void *user_data)
{
	static struct wheel_rx_packet packet;
	static size_t received;
	uint8_t byte;

	ARG_UNUSED(user_data);
	uart_irq_update(dev);

	/* A lost/corrupt byte destroys alignment in the fixed-size wire format. */
	if (uart_err_check(dev) != 0) {
		atomic_set(&wheel_rx_fault, 1);
		uart_irq_rx_disable(dev);
		return;
	}
	if (uart_irq_rx_ready(dev) == 0) {
		return;
	}
	while (uart_fifo_read(dev, &byte, 1) == 1) {
		packet.bytes[received++] = byte;
		if (received == sizeof(packet.bytes)) {
			packet.timestamp_ms = k_uptime_get();
			if (k_msgq_put(&wheel_rx_queue, &packet, K_NO_WAIT) != 0) {
				atomic_set(&wheel_rx_fault, 1);
				uart_irq_rx_disable(dev);
				return;
			}
			received = 0;
		}
	}
}

/**
 * @brief Toggle indicator enable states on button press edges, never on held packets.
 * @param buttons Validated wheel button bitmask from one packet in arrival order.
 * @return 0 on success, or a negative blinker API error.
 */
static int update_blinker_buttons(uint16_t buttons)
{
	static uint16_t previous_buttons;
	static bool left_enabled;
	static bool right_enabled;
	uint16_t pressed = buttons & (uint16_t)~previous_buttons;
	int ret;

	previous_buttons = buttons;
	if ((pressed & WHEEL_BUTTON_LEFT_BLINKER) != 0U) {
		left_enabled = !left_enabled;
		ret = blinker_set(BLINKER_LEFT, left_enabled);
		if (ret != 0) {
			return ret;
		}
	}
	if ((pressed & WHEEL_BUTTON_RIGHT_BLINKER) != 0U) {
		right_enabled = !right_enabled;
		ret = blinker_set(BLINKER_RIGHT, right_enabled);
		if (ret != 0) {
			return ret;
		}
	}
	return 0;
}

/**
 * @brief Apply fresh pedal packets and update both motor duties until a fault.
 * @param arg1 Unused Zephyr thread argument.
 * @param arg2 Unused Zephyr thread argument.
 * @param arg3 Unused Zephyr thread argument.
 */
static void wheel_receiver_thread(void *arg1, void *arg2, void *arg3)
{
	struct wheel_rx_packet packet;
	int64_t last_packet_ms = 0;
	int64_t last_print_ms = -WHEEL_RX_PRINT_MS;
	int64_t last_update_ms = k_uptime_get();
	uint32_t duty = 0U;
	struct wheel_info wheel = {
		.throttle = WHEEL_THROTTLE_RELEASED,
		.brake = WHEEL_BRAKE_RELEASED,
	};
	bool have_packet = false;
	const char *stop_reason = "motor output error";
	int ret;

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	if (!device_is_ready(wheel_uart)) {
		printk("USART1 is not ready; motors remain in coast\n");
		return;
	}
	ret = uart_irq_callback_user_data_set(wheel_uart, wheel_uart_callback, NULL);
	if (ret != 0) {
		printk("USART1 callback setup failed: %d\n", ret);
		return;
	}

	printk("Pedals: USART1 115200, both motors; brake reduces duty\n");
	uart_irq_rx_enable(wheel_uart);

	while (1) {
		/* Short waits also notice ISR faults when no complete packet arrives. */
		int64_t remaining =
			have_packet ? WHEEL_LINK_TIMEOUT_MS - (k_uptime_get() - last_packet_ms)
				    : WHEEL_RX_POLL_MS;

		if (remaining <= 0) {
			stop_reason = "UART command timeout";
			break;
		}
		ret = k_msgq_get(&wheel_rx_queue, &packet, K_MSEC(MIN(remaining, WHEEL_RX_POLL_MS)));
		if (atomic_get(&wheel_rx_fault) != 0) {
			stop_reason = "UART error or RX queue overflow";
			break;
		}
		if (ret == 0) {
			/* Inspect every queued button edge; only the newest pedals drive PWM. */
			bool packet_fault = false;

			for (size_t i = 0; i < WHEEL_RX_QUEUE_SIZE; i++) {
				if (atomic_get(&wheel_rx_fault) != 0) {
					stop_reason = "UART error or RX queue overflow";
					packet_fault = true;
					break;
				}
				if (k_uptime_get() - packet.timestamp_ms >= WHEEL_LINK_TIMEOUT_MS) {
					stop_reason = "stale UART command";
					packet_fault = true;
					break;
				}
				wheel = (struct wheel_info) {
					.steering = (int16_t)sys_get_le16(&packet.bytes[0]),
					.throttle = (int16_t)sys_get_le16(&packet.bytes[2]),
					.brake = (int16_t)sys_get_le16(&packet.bytes[4]),
					.buttons = sys_get_le16(&packet.bytes[6]),
				};
				if ((wheel.buttons & ~WHEEL_BUTTONS_MASK) != 0U) {
					stop_reason = "invalid button bits / packet alignment";
					packet_fault = true;
					break;
				}
				ret = update_blinker_buttons(wheel.buttons);
				if (ret != 0) {
					stop_reason = "blinker command failed";
					packet_fault = true;
					break;
				}
				if (i + 1U == WHEEL_RX_QUEUE_SIZE ||
				    k_msgq_get(&wheel_rx_queue, &packet, K_NO_WAIT) != 0) {
					break;
				}
			}
			if (packet_fault || atomic_get(&wheel_rx_fault) != 0) {
				if (!packet_fault) {
					stop_reason = "UART error or RX queue overflow";
				}
				break;
			}
			if (!have_packet) {
				last_update_ms = k_uptime_get();
			}
			last_packet_ms = packet.timestamp_ms;
			have_packet = true;
		}
		if (!have_packet) {
			continue;
		}
		int64_t now = k_uptime_get();

		/* A timed queue wait must not ramp motors after the command has expired. */
		if (now - last_packet_ms >= WHEEL_LINK_TIMEOUT_MS) {
			stop_reason = "UART command timeout";
			break;
		}
		uint32_t target = pedal_control_target(wheel.throttle, wheel.brake);

		duty = pedal_control_ramp(duty, target, (uint32_t)(now - last_update_ms));
		last_update_ms = now;

		for (enum motor_side side = MOTOR_LEFT; side < MOTOR_COUNT; side++) {
			ret = motor_drive_raw(side, (int32_t)duty);
			if (ret != 0) {
				break;
			}
		}
		if (ret != 0) {
			printk("Throttle drive failed: %d\n", ret);
			break;
		}

		/* Limit console traffic while ramp updates continue between packets. */

		if (now - last_print_ms >= WHEEL_RX_PRINT_MS) {
			uint32_t percent_x100 = (duty * 10000U + MOTOR_DUTY_FULL_SCALE / 2U) /
						MOTOR_DUTY_FULL_SCALE;

			printk("Throttle=%d brake=%d duty=%u.%02u%% (both motors)\n",
			       (int)wheel.throttle, (int)wheel.brake,
			       (unsigned int)(percent_x100 / 100U),
			       (unsigned int)(percent_x100 % 100U));
			last_print_ms = now;
		}
	}

	uart_irq_rx_disable(wheel_uart);
	ret = motors_coast();
	int left_ret = blinker_set(BLINKER_LEFT, false);
	int right_ret = blinker_set(BLINKER_RIGHT, false);

	if (left_ret != 0 || right_ret != 0) {
		printk("Blinker shutdown failed: left=%d right=%d\n", left_ret, right_ret);
	}
	printk("Throttle stopped: %s; coast=%d. Reset to restart reception.\n", stop_reason, ret);
}

/**
 * @brief Start the UART worker once after the drivers are initialized.
 * @return 0 when started, or -EALREADY if already started.
 */
int uart_receiver_start(void)
{
	if (started) {
		return -EALREADY;
	}
	started = true;
	k_thread_create(&receiver_thread, receiver_stack, K_THREAD_STACK_SIZEOF(receiver_stack),
			wheel_receiver_thread, NULL, NULL, NULL, WHEEL_RX_PRIORITY, 0, K_NO_WAIT);
	return 0;
}
