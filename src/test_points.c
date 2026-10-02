/** @file test_points.c
 * @brief Native STM32 GPIO markers; each pin has one serialized writer.
 */
#include <errno.h>
#include <zephyr/drivers/gpio.h>
#include "test_points.h"

static const struct gpio_dt_spec command_rx =
	GPIO_DT_SPEC_GET(DT_NODELABEL(test_cmd_rx), gpios);
static const struct gpio_dt_spec pwm_set =
	GPIO_DT_SPEC_GET(DT_NODELABEL(test_pwm_set), gpios);

/** @brief Initialize both native GPIO outputs before any producer starts.
 * @return 0 on success, or a negative GPIO error.
 */
int test_points_init(void)
{
	if (!gpio_is_ready_dt(&command_rx) || !gpio_is_ready_dt(&pwm_set)) {
		return -ENODEV;
	}
	int ret = gpio_pin_configure_dt(&command_rx, GPIO_OUTPUT_INACTIVE);

	if (ret == 0) {
		ret = gpio_pin_configure_dt(&pwm_set, GPIO_OUTPUT_INACTIVE);
	}
	return ret;
}

/** @brief Mark a complete command before queueing, including subsequently rejected commands. */
void test_points_command_received(void)
{
	(void)gpio_pin_toggle_dt(&command_rx);
}

/** @brief Mark successful right-motor writes, including brake/coast and unchanged values. */
void test_points_pwm_written(void)
{
	(void)gpio_pin_toggle_dt(&pwm_set);
}
