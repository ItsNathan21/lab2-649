#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>

#include "wheel_info.h"
#include "encoder.h"

#define WHEEL_RX_STACK_SIZE 1024
#define WHEEL_RX_PRIORITY   5
#define WHEEL_RX_QUEUE_SIZE 128

static const struct device *const wheel_uart = DEVICE_DT_GET(DT_NODELABEL(usart1));

/* Queue bytes so all diagnostic printing stays outside the UART ISR. */
K_MSGQ_DEFINE(wheel_rx_queue, sizeof(uint8_t), WHEEL_RX_QUEUE_SIZE, 1);

static void wheel_uart_callback(const struct device *dev, void *user_data)
{
	uint8_t byte;

	ARG_UNUSED(user_data);

	uart_irq_update(dev);

	if (!uart_irq_rx_ready(dev)) {
		return;
	}

	while (uart_fifo_read(dev, &byte, 1) == 1) {
		/* Temporary byte tracing: excess bytes are dropped if the queue fills. */
		(void)k_msgq_put(&wheel_rx_queue, &byte, K_NO_WAIT);
	}
}

static void wheel_receiver_thread(void *arg1, void *arg2, void *arg3)
{
	uint8_t packet[sizeof(struct wheel_info)];
	size_t received = 0;
	uint8_t byte;
	int ret;

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	if (!device_is_ready(wheel_uart)) {
		printk("USART1 is not ready\n");
		return;
	}

	ret = uart_irq_callback_user_data_set(wheel_uart, wheel_uart_callback, NULL);
	if (ret != 0) {
		printk("USART1 callback setup failed: %d\n", ret);
		return;
	}

	printk("Wheel receiver: USART1, 115200 baud, 8-byte packets\n");
	uart_irq_rx_enable(wheel_uart);

	while (1) {
		if (k_msgq_get(&wheel_rx_queue, &byte, K_SECONDS(2)) != 0) {
			printk("USART1: waiting for RX bytes (%u/8 buffered)\n",
			       (unsigned int)received);
			continue;
		}

		printk("USART1 RX byte: 0x%02x\n", (unsigned int)byte);
		packet[received++] = byte;
		if (received < sizeof(packet)) {
			continue;
		}
		received = 0;

		/* Decode the little-endian wire fields explicitly. */
		struct wheel_info wheel = {
			.steering = (int16_t)sys_get_le16(&packet[0]),
			.throttle = (int16_t)sys_get_le16(&packet[2]),
			.brake = (int16_t)sys_get_le16(&packet[4]),
			.buttons = sys_get_le16(&packet[6]),
		};

		printk("Wheel: steering=%d throttle=%d brake=%d buttons=0x%04x\n",
		       (int)wheel.steering, (int)wheel.throttle,
		       (int)wheel.brake, (unsigned int)wheel.buttons);
	}
}

K_THREAD_DEFINE(wheel_rx_thread, WHEEL_RX_STACK_SIZE,
		wheel_receiver_thread, NULL, NULL, NULL,
		WHEEL_RX_PRIORITY, 0, 0);

int main(void)
{
	printk("Lab 2: wheel UART receiver on %s\n", CONFIG_BOARD_TARGET);
	int ret = encoders_init();
	if (ret != 0) {
		printk("Encoder initialization failed: %d\n", ret);
		return ret;
	}
	printk("Encoders: left PC0=A PC1=B; right PC2=A PC3=B (x4)\n");
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
		printk("ENC L=%lld (%lld cps) R=%lld (%lld cps) "
		       "invalid=%u/%u read_errors=%u\n",
		       (long long)now.counts[0], (long long)left_cps,
		       (long long)now.counts[1], (long long)right_cps,
		       (unsigned int)now.invalid_transitions[0],
		       (unsigned int)now.invalid_transitions[1],
		       (unsigned int)now.read_errors);
		last = now;
		if (next < now.timestamp_ms) {
			next = now.timestamp_ms;
		}
	}
}
