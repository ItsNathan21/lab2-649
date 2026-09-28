/** @file main.c
 * @brief Driver initialization and worker startup.
 */
#include <zephyr/sys/printk.h>
#include "blinker.h"
#include "encoder.h"
#include "encoder_monitor.h"
#include "motor.h"
#include "uart_receiver.h"

/**
 * @brief Initialize drivers and start UART motor control and encoder monitoring.
 * @return 0 after startup, or a negative initialization error.
 */
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
	ret = blinker_init();
	if (ret != 0) {
		printk("Blinker initialization failed: %d\n", ret);
		return ret;
	}
	/* UART control starts only after both drivers are initialized. */
	ret = uart_receiver_start();
	if (ret != 0) {
		printk("UART receiver start failed: %d\n", ret);
		return ret;
	}
	ret = encoder_monitor_start();
	if (ret != 0) {
		printk("Encoder monitor start failed: %d\n", ret);
	}
	return ret;
}
