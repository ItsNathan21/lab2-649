#include <stdbool.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>

#include "wheel_info.h"
#include "encoder.h"
#include "motor.h"

#ifdef LAB_MOTOR_TEST
int motor_test_once(void);
#endif

#define WHEEL_RX_STACK_SIZE 1536
#define WHEEL_RX_PRIORITY   5
#define WHEEL_RX_QUEUE_SIZE 16
#define WHEEL_LINK_TIMEOUT_MS 150
#define WHEEL_BUTTONS_MASK (WHEEL_BUTTON_A | WHEEL_BUTTON_B | WHEEL_BUTTON_X | \
	WHEEL_BUTTON_Y | WHEEL_BUTTON_RIGHT_BLINKER | WHEEL_BUTTON_LEFT_BLINKER | \
	WHEEL_BUTTON_RSB | WHEEL_BUTTON_LSB)

static const struct device *const wheel_uart = DEVICE_DT_GET(DT_NODELABEL(usart1));

struct wheel_rx_packet {
	uint8_t bytes[sizeof(struct wheel_info)];
	int64_t timestamp_ms;
};

K_MSGQ_DEFINE(wheel_rx_queue, sizeof(struct wheel_rx_packet), WHEEL_RX_QUEUE_SIZE, 8);
K_SEM_DEFINE(motor_control_ready, 0, 1);
static atomic_t wheel_rx_fault;

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

static void wheel_receiver_thread(void *arg1, void *arg2, void *arg3)
{
	struct wheel_rx_packet packet;
	int64_t last_packet_ms = 0;
	int64_t last_print_ms = -100;
	bool have_packet = false;
	const char *stop_reason = "motor output error";
	int ret;

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	ret = k_sem_take(&motor_control_ready, K_FOREVER);
	if (ret != 0) {
		return;
	}
	if (!device_is_ready(wheel_uart)) {
		printk("USART1 is not ready; motors remain in coast\n");
		return;
	}
	ret = uart_irq_callback_user_data_set(wheel_uart, wheel_uart_callback, NULL);
	if (ret != 0) {
		printk("USART1 callback setup failed: %d\n", ret);
		return;
	}

	printk("Throttle: USART1 115200, both motors, released=0%% pressed=100%%\n");
	uart_irq_rx_enable(wheel_uart);

	while (1) {
		/* Short waits also notice ISR faults when no complete packet arrives. */
		int64_t remaining = have_packet ? WHEEL_LINK_TIMEOUT_MS -
			(k_uptime_get() - last_packet_ms) : 20;

		if (remaining <= 0) {
			stop_reason = "UART command timeout";
			break;
		}
		ret = k_msgq_get(&wheel_rx_queue, &packet, K_MSEC(MIN(remaining, 20)));
		if (atomic_get(&wheel_rx_fault) != 0) {
			stop_reason = "UART error or RX queue overflow";
			break;
		}
		if (ret != 0) {
			continue;
		}
		/* Apply the newest queued command, not a backlog of old throttle values. */
		for (size_t i = 0; i < WHEEL_RX_QUEUE_SIZE; i++) {
			struct wheel_rx_packet newer;

			if (k_msgq_get(&wheel_rx_queue, &newer, K_NO_WAIT) != 0) {
				break;
			}
			packet = newer;
		}
		if (atomic_get(&wheel_rx_fault) != 0) {
			stop_reason = "UART error or RX queue overflow";
			break;
		}
		if (k_uptime_get() - packet.timestamp_ms >= WHEEL_LINK_TIMEOUT_MS) {
			stop_reason = "stale UART command";
			break;
		}
		struct wheel_info wheel = {
			.steering = (int16_t)sys_get_le16(&packet.bytes[0]),
			.throttle = (int16_t)sys_get_le16(&packet.bytes[2]),
			.brake = (int16_t)sys_get_le16(&packet.bytes[4]),
			.buttons = sys_get_le16(&packet.bytes[6]),
		};

		if ((wheel.buttons & ~WHEEL_BUTTONS_MASK) != 0U) {
			stop_reason = "invalid button bits / packet alignment";
			break;
		}
		/* Exact mapping: 32767 -> 0, -32768 -> 65535. No float or deadband. */
		uint32_t duty = (int32_t)WHEEL_THROTTLE_RELEASED - (int32_t)wheel.throttle;

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
		last_packet_ms = packet.timestamp_ms;
		have_packet = true;

		/* Limit console traffic; every received command still updates PWM. */
		int64_t now = k_uptime_get();

		if (now - last_print_ms >= 100) {
			uint32_t percent_x100 = (duty * 10000U + MOTOR_DUTY_FULL_SCALE / 2U) /
						MOTOR_DUTY_FULL_SCALE;

			printk("Throttle=%d duty=%u.%02u%% (both motors)\n",
			       (int)wheel.throttle, (unsigned int)(percent_x100 / 100U),
			       (unsigned int)(percent_x100 % 100U));
			last_print_ms = now;
		}
	}

	uart_irq_rx_disable(wheel_uart);
	ret = motors_coast();
	printk("Throttle stopped: %s; coast=%d. Reset to restart reception.\n",
	       stop_reason, ret);
}

K_THREAD_DEFINE(wheel_rx_thread, WHEEL_RX_STACK_SIZE,
		wheel_receiver_thread, NULL, NULL, NULL,
		WHEEL_RX_PRIORITY, 0, 0);

int main(void)
{
	printk("Lab 2: wheel UART receiver on %s\n", CONFIG_BOARD_TARGET);
	int ret = motors_init();
	if (ret != 0) {
		printk("Motor initialization failed: %d\n", ret);
		return ret;
	}
	printk("Motors initialized in coast\n");
	ret = encoders_init();
	if (ret != 0) {
		printk("Encoder initialization failed: %d\n", ret);
		return ret;
	}
	printk("Encoders: left PC0=A PC1=B; right PC2=A PC3=B (x4)\n");
#ifdef LAB_MOTOR_TEST
	ret = motor_test_once();
	if (ret != 0) {
		printk("Motor test failed: %d\n", ret);
		return ret;
	}
#else
	/* UART control starts only after both drivers are initialized. */
	k_sem_give(&motor_control_ready);
#endif
	struct encoder_snapshot last, now;
	encoders_snapshot(&last);
	int64_t next = last.timestamp_ms;

	while (1) {
		/* Absolute schedule avoids adding print time to each period. */
		next += 100;
		k_sleep(K_TIMEOUT_ABS_MS(next));
		encoders_snapshot(&now);
		int64_t elapsed = now.timestamp_ms - last.timestamp_ms;
		if (elapsed <= 0) {
			continue;
		}
		int64_t left_cps = (now.counts[ENCODER_LEFT] -
			last.counts[ENCODER_LEFT]) * 1000 / elapsed;
		int64_t right_cps = (now.counts[ENCODER_RIGHT] -
			last.counts[ENCODER_RIGHT]) * 1000 / elapsed;
		/* Integer millirpm avoids float printf and preserves low-speed detail. */
		int64_t left_mrpm = (now.counts[0] - last.counts[0]) * 60000000LL /
			(ENCODER_COUNTS_PER_REV * elapsed);
		int64_t right_mrpm = (now.counts[1] - last.counts[1]) * 60000000LL /
			(ENCODER_COUNTS_PER_REV * elapsed);
		printk("ENC L=%lld (%lld cps, %lld mRPM) R=%lld (%lld cps, %lld mRPM) "
		       "invalid=%u/%u read_errors=%u\n",
		       (long long)now.counts[0], (long long)left_cps,
		       (long long)left_mrpm,
		       (long long)now.counts[1], (long long)right_cps,
		       (long long)right_mrpm,
		       (unsigned int)now.invalid_transitions[0],
		       (unsigned int)now.invalid_transitions[1],
		       (unsigned int)now.read_errors);
		last = now;
		if (next < now.timestamp_ms) {
			next = now.timestamp_ms;
		}
	}
}
