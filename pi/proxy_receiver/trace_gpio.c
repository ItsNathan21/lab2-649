/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
/** @file trace_gpio.c
 * @brief Linux GPIO character-device v2 timing markers, without GPIO library dependencies.
 */
#include "trace_gpio.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int line_fd = -1;
static uint64_t levels;

/** @brief Claim the two output lines with default inactive levels.
 * @return 0 on success, -1 with errno set on failure.
 */
int trace_gpio_init(void)
{
	if (line_fd >= 0) {
		errno = EALREADY;
		return -1;
	}
	int chip = open(TRACE_GPIO_CHIP, O_RDONLY);

	if (chip < 0) {
		return -1;
	}
	struct gpio_v2_line_request request = {
		.offsets = {TRACE_GPIO_UDP_RX, TRACE_GPIO_CMD_TX},
		.consumer = "wheel-timing",
		.config.flags = GPIO_V2_LINE_FLAG_OUTPUT,
		.num_lines = 2,
	};
	int ret = ioctl(chip, GPIO_V2_GET_LINE_IOCTL, &request);
	int saved_errno = errno;

	close(chip);
	if (ret < 0) {
		errno = saved_errno;
		return -1;
	}
	line_fd = request.fd;
	levels = 0;
	return 0;
}

/** @brief Toggle an event output without sleeping or generating a timed pulse.
 * @param event Selected timing marker.
 * @return 0 on success, -1 with errno set on failure.
 */
int trace_gpio_toggle(enum trace_gpio_event event)
{
	if (event != TRACE_UDP_RX && event != TRACE_CMD_TX) {
		errno = EINVAL;
		return -1;
	}
	if (line_fd < 0) {
		return 0;
	}
	uint64_t next = levels ^ (UINT64_C(1) << event);
	struct gpio_v2_line_values values = {.bits = next, .mask = 3};

	if (ioctl(line_fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &values) < 0) {
		return -1;
	}
	levels = next;
	return 0;
}

/** @brief Release the requested lines after attempting to leave both low. */
void trace_gpio_close(void)
{
	if (line_fd >= 0) {
		struct gpio_v2_line_values values = {.bits = 0, .mask = 3};

		(void)ioctl(line_fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &values);
		close(line_fd);
		line_fd = -1;
	}
}
