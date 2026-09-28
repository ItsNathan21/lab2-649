#include <errno.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "encoder.h"
#include "motor.h"

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

static int wait_button(int pressed)
{
	int stable = 0;
	while (stable < 3) {
		int value = gpio_pin_get_dt(&button);
		if (value < 0) {
			return value;
		}
		stable = value == pressed ? stable + 1 : 0;
		k_sleep(K_MSEC(10));
	}
	return 0;
}

int motor_test_once(void)
{
	struct encoder_snapshot before, after;
	if (!gpio_is_ready_dt(&button)) {
		return -ENODEV;
	}
	int ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret != 0) {
		return ret;
	}
	printk("TEST: wheels raised, ENA/ENB jumpers removed.\n");
	printk("Press/release BLUE USER: left 50%% for 500 ms, then brake.\n");
	/* Require a fresh press after boot, with 30 ms debounce at each level. */
	ret = wait_button(0);
	if (ret == 0) { ret = wait_button(1); }
	if (ret == 0) { ret = wait_button(0); }
	if (ret != 0) { return ret; }
	encoders_snapshot(&before);
	ret = motor_drive(MOTOR_LEFT, 500);
	if (ret != 0) { return ret; }
	/* No printing in the powered interval. Do not halt the debugger here. */
	k_sleep(K_MSEC(500));
	ret = motors_brake();
	encoders_snapshot(&after);
	printk("TEST ended: brake=%d delta L=%lld R=%lld invalid=%u/%u read_errors=%u\n",
	       ret, (long long)(after.counts[0] - before.counts[0]),
	       (long long)(after.counts[1] - before.counts[1]),
	       (unsigned int)(after.invalid_transitions[0] - before.invalid_transitions[0]),
	       (unsigned int)(after.invalid_transitions[1] - before.invalid_transitions[1]),
	       (unsigned int)(after.read_errors - before.read_errors));
	printk("No repeat. Reset re-arms USER button; brake remains latched.\n");
	return ret;
}
