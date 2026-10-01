/** @file main.c
 * @brief Driver initialization and worker startup.
 */
#include <zephyr/sys/printk.h>
#include "blinker.h"
#include "encoder.h"
#include "encoder_monitor.h"
#include "motor.h"
#include "motor_controller.h"
#include "uart_receiver.h"
#include "uart_status.h"
#include "servo.h"
#include "current_sensor.h"

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
	ret = servo_init();
	if (ret != 0) {
		printk("Servo initialization failed: %d\n", ret);
		return ret;
	}
	printk("Servo: PA1, trial center=%u us\n", SERVO_CENTER_PULSE_US);
	ret = motor_controller_start();
	if (ret != 0) {
		printk("Speed controller start failed: %d\n", ret);
		return ret;
	}
	/* UART submits commands only after drivers and the PID worker are ready. */
	ret = uart_receiver_start();
	if (ret != 0) {
		printk("UART receiver start failed: %d\n", ret);
		return ret;
	}
	ret = uart_status_start();
	if (ret != 0) {
		int brake_ret = motor_controller_stop();

		printk("UART status start failed: %d; brake=%d\n", ret, brake_ret);
		return ret;
	}
	ret = current_sensor_start();
	if (ret != 0) {
		/* Lab 2 current sensing is read-only; heartbeat reports unavailable data. */
		printk("Current sensor start failed: %d; telemetry unavailable\n", ret);
	}
	ret = encoder_monitor_start();
	if (ret != 0) {
		printk("Encoder monitor start failed: %d\n", ret);
	}
	return ret;
}
