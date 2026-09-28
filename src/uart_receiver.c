/** @file uart_receiver.c
 * @brief UART commands remain receivable during recoverable fail-safe conditions.
 */
#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include "uart_receiver.h"
#include "blinker.h"
#include "motor_controller.h"
#include "pedal_control.h"
#include "servo.h"

static const struct device *const wheel_uart = DEVICE_DT_GET(DT_NODELABEL(usart1));
K_MSGQ_DEFINE_STATIC(wheel_rx_queue, sizeof(struct wheel_rx_packet), WHEEL_RX_QUEUE_SIZE, 8);
static K_THREAD_STACK_DEFINE(receiver_stack, WHEEL_RX_STACK_SIZE);
static struct k_thread receiver_thread;
static atomic_t fault_bits = ATOMIC_INIT(UART_FAULT_LINK);
static atomic_t receive_error;
static bool started;

/** @brief Parse complete frames in the ISR; corruption cannot refresh the watchdog.
 * @param dev USART1 device.
 * @param user_data Unused callback context.
 */
static void wheel_uart_callback(const struct device *dev, void *user_data)
{
	static struct uart_parser parser;
	struct wheel_rx_packet packet;
	uint8_t byte;

	ARG_UNUSED(user_data);
	uart_irq_update(dev);
	if (uart_err_check(dev) != 0) {
		parser.used = 0;
		atomic_set(&receive_error, 1);
	}
	if (uart_irq_rx_ready(dev) == 0) {
		return;
	}
	while (uart_fifo_read(dev, &byte, 1) == 1) {
		if (uart_frame_feed(&parser, byte, &packet.frame) &&
		    packet.frame.type == UART_FRAME_COMMAND) {
			packet.timestamp_ms = k_uptime_get();
			if (k_msgq_put(&wheel_rx_queue, &packet, K_NO_WAIT) != 0) {
				atomic_set(&receive_error, 1);
			}
		}
	}
}

/** @brief Apply braking and hazards before publishing an error state.
 * @param faults Current reason mask; hardware failures become latched.
 */
static void apply_faults(uint8_t faults)
{
	if (faults != 0U) {
		int motor_ret = motor_controller_inhibit();
		int servo_ret = servo_disable();

		if (motor_ret != 0 || servo_ret != 0) {
			faults |= UART_FAULT_HARDWARE;
		}
	}
	if (blinker_hazards(faults != 0U) != 0) {
		faults |= UART_FAULT_HARDWARE;
	}
	atomic_set(&fault_bits, faults);
}

/** @brief Supervise reception and dispatch only current, healthy actuator commands.
 * @param arg1 Unused thread argument.
 * @param arg2 Unused thread argument.
 * @param arg3 Unused thread argument.
 */
static void receiver_worker(void *arg1, void *arg2, void *arg3)
{
	int64_t last_packet = -(int64_t)UART_LINK_TIMEOUT_MS;
	int64_t first_press = -1;
	uint16_t previous_buttons = 0;
	uint16_t last_sequence = 0;
	uint8_t source_flags = 0;
	bool self_test = false;
	bool sequence_known = false;
	bool hardware_fault = false;

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);
	while (true) {
		struct wheel_rx_packet packet;
		bool received = k_msgq_get(&wheel_rx_queue, &packet,
					  K_MSEC(WHEEL_RX_POLL_MS)) == 0;
		int64_t now = k_uptime_get();
		uint16_t rising = 0;
		struct wheel_info wheel = {0};

		if (atomic_set(&receive_error, 0) != 0) {
			k_msgq_purge(&wheel_rx_queue);
			received = false;
			last_packet = now - UART_LINK_TIMEOUT_MS;
		}
		if (now - last_packet >= UART_LINK_TIMEOUT_MS) {
			sequence_known = false;
			first_press = -1;
		}
		if (received) {
			struct uart_frame *frame = &packet.frame;
			uint16_t advance = frame->sequence - last_sequence;
			uint16_t buttons = uart_get_u16(frame->payload + 6);

			received = now - packet.timestamp_ms < UART_LINK_TIMEOUT_MS &&
				(!sequence_known || (advance != 0U && advance < 0x8000U)) &&
				(buttons & ~WHEEL_BUTTONS_MASK) == 0U &&
				(frame->flags & ~UART_COMMAND_READY) == 0U;
			if (received) {
				sequence_known = true;
				last_sequence = frame->sequence;
				last_packet = packet.timestamp_ms;
				source_flags = frame->flags;
				wheel.steering = (int16_t)uart_get_u16(frame->payload);
				wheel.throttle = (int16_t)uart_get_u16(frame->payload + 2);
				wheel.brake = (int16_t)uart_get_u16(frame->payload + 4);
				wheel.buttons = buttons;
				if ((source_flags & UART_COMMAND_WHEEL_FRESH) != 0U) {
					rising = buttons & ~previous_buttons;
					previous_buttons = buttons;
					if ((rising & WHEEL_BUTTON_Y) != 0U) {
						if (first_press >= 0 &&
						    last_packet - first_press <=
							WHEEL_SELF_TEST_DOUBLE_MS) {
							self_test = false;
							first_press = -1;
						} else {
							self_test = true;
							first_press = last_packet;
						}
					}
				} else {
					first_press = -1;
				}
			}
		}
		hardware_fault |= motor_controller_faulted() ||
			(uart_receiver_faults() & UART_FAULT_HARDWARE) != 0U;
		uint8_t faults = hardware_fault ? UART_FAULT_HARDWARE : 0U;

		if (now - last_packet >= UART_LINK_TIMEOUT_MS) {
			faults |= UART_FAULT_LINK;
		}
		if (source_flags != UART_COMMAND_READY) {
			faults |= UART_FAULT_SOURCE;
		}
		if (self_test) {
			faults |= UART_FAULT_SELF_TEST;
		}
		apply_faults(faults);
		if (!received || uart_receiver_faults() != 0U) {
			continue;
		}
		/* Recovery uses this fresh packet, never a saved pre-fault drive command. */
		int ret = servo_set_steering(wheel.steering);

		if (ret == 0) {
			ret = motor_controller_set_target(
				pedal_control_target(wheel.throttle, wheel.brake),
				packet.timestamp_ms);
		}
		if (ret == 0 && (rising & WHEEL_BUTTON_LEFT_BLINKER) != 0U) {
			ret = blinker_toggle(BLINKER_LEFT);
		}
		if (ret == 0 && (rising & WHEEL_BUTTON_RIGHT_BLINKER) != 0U) {
			ret = blinker_toggle(BLINKER_RIGHT);
		}
		if (ret != 0) {
			apply_faults(UART_FAULT_HARDWARE);
			printk("Actuator update failed: %d; reset required\n", ret);
		} else {
			blinker_steering(servo_angle_mdeg(wheel.steering));
		}
	}
}

/** @brief Initialize the UART and recoverable fail-safe before starting reception.
 * @return 0 on success, or a negative driver/state error.
 */
int uart_receiver_start(void)
{
	if (started) {
		return -EALREADY;
	}
	if (!device_is_ready(wheel_uart)) {
		return -ENODEV;
	}
	apply_faults(UART_FAULT_LINK);
	if ((uart_receiver_faults() & UART_FAULT_HARDWARE) != 0U) {
		return -EIO;
	}
	int ret = uart_irq_callback_user_data_set(wheel_uart, wheel_uart_callback, NULL);

	if (ret != 0) {
		return ret;
	}
	started = true;
	uart_irq_rx_enable(wheel_uart);
	k_thread_create(&receiver_thread, receiver_stack, K_THREAD_STACK_SIZEOF(receiver_stack),
			receiver_worker, NULL, NULL, NULL, WHEEL_RX_PRIORITY, 0, K_NO_WAIT);
	return 0;
}

/** @brief Snapshot the current reason mask for the heartbeat.
 * @return UART_FAULT_* bitmask.
 */
uint8_t uart_receiver_faults(void)
{
	return (uint8_t)atomic_get(&fault_bits);
}
