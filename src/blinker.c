/** @file blinker.c
 * @brief GPIO blinker worker with independent timing and private state.
 */
#include <errno.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "blinker.h"

_Static_assert(BLINKER_RATE_HZ > 0U && BLINKER_RATE_HZ <= 500U, "Invalid blink rate");
_Static_assert(1000U % (2U * BLINKER_RATE_HZ) == 0U, "Blink rate needs whole-ms phases");

static const struct gpio_dt_spec outputs[BLINKER_COUNT] = {
	GPIO_DT_SPEC_GET(DT_NODELABEL(left_blinker), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(right_blinker), gpios),
};
static struct blinker_state states[BLINKER_COUNT];
static K_MUTEX_DEFINE(blinker_lock);
static K_SEM_DEFINE(blinker_changed, 0, 1);
static K_THREAD_STACK_DEFINE(blinker_stack, BLINKER_STACK_SIZE);
static struct k_thread blinker_thread;
static bool initialized;

/**
 * @brief Apply enable changes and overdue edges for one output while holding the lock.
 * @param side Output index.
 * @param now Current uptime in milliseconds.
 */
static void update_output(enum blinker_side side, int64_t now)
{
	struct blinker_state *state = &states[side];
	bool old_level = state->level;

	if (state->enabled != state->active) {
		state->active = state->enabled;
		state->level = state->enabled;
		state->next_edge_ms = now + BLINKER_HALF_PERIOD_MS;
	} else if (state->active && now >= state->next_edge_ms) {
		/* Advance the absolute schedule without producing a burst of missed edges. */
		int64_t edges = (now - state->next_edge_ms) / BLINKER_HALF_PERIOD_MS + 1;

		if ((edges & 1) != 0) {
			state->level = !state->level;
		}
		state->next_edge_ms += edges * BLINKER_HALF_PERIOD_MS;
	}
	if (state->level != old_level) {
		int ret = gpio_pin_set_dt(&outputs[side], state->level);

		if (ret != 0) {
			state->enabled = false;
			state->active = false;
			state->level = false;
			int off_ret = gpio_pin_set_dt(&outputs[side], 0);

			printk("Blinker %d GPIO error: %d; off=%d\n", side, ret, off_ret);
		}
	}
}

/**
 * @brief Blink enabled outputs and sleep until the next edge or API request.
 * @param arg1 Unused Zephyr thread argument.
 * @param arg2 Unused Zephyr thread argument.
 * @param arg3 Unused Zephyr thread argument.
 */
static void blinker_worker(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (true) {
		int64_t deadline = INT64_MAX;

		k_mutex_lock(&blinker_lock, K_FOREVER);
		int64_t now = k_uptime_get();

		for (enum blinker_side side = BLINKER_LEFT; side < BLINKER_COUNT; side++) {
			update_output(side, now);
			if (states[side].active && states[side].next_edge_ms < deadline) {
				deadline = states[side].next_edge_ms;
			}
		}
		k_mutex_unlock(&blinker_lock);
		k_timeout_t wait = deadline == INT64_MAX
					   ? K_FOREVER
					   : K_MSEC(MAX(deadline - k_uptime_get(), 0));

		/* Timeout means an edge is due; a semaphore signal means settings changed. */
		(void)k_sem_take(&blinker_changed, wait);
	}
}

/**
 * @brief Configure outputs off and create the blinker thread once from main.
 * @return 0 on success, -EALREADY if started, or a negative GPIO error.
 */
int blinker_init(void)
{
	if (initialized) {
		return -EALREADY;
	}
	for (enum blinker_side side = BLINKER_LEFT; side < BLINKER_COUNT; side++) {
		if (!gpio_is_ready_dt(&outputs[side])) {
			return -ENODEV;
		}
		int ret = gpio_pin_configure_dt(&outputs[side], GPIO_OUTPUT_INACTIVE);

		if (ret != 0) {
			return ret;
		}
	}
	initialized = true;
	k_thread_create(&blinker_thread, blinker_stack, K_THREAD_STACK_SIZEOF(blinker_stack),
			blinker_worker, NULL, NULL, NULL, BLINKER_PRIORITY, 0, K_NO_WAIT);
	return 0;
}

/**
 * @brief Send an enable state to the worker without exposing its timing or GPIO objects.
 * @param side Left or right output.
 * @param enabled Desired blinking state.
 * @return 0 on success, -EINVAL for an invalid side, or -ENODEV before initialization.
 */
int blinker_set(enum blinker_side side, bool enabled)
{
	if ((unsigned int)side >= BLINKER_COUNT) {
		return -EINVAL;
	}
	if (!initialized) {
		return -ENODEV;
	}
	k_mutex_lock(&blinker_lock, K_FOREVER);
	states[side].enabled = enabled;
	k_mutex_unlock(&blinker_lock);
	k_sem_give(&blinker_changed);
	return 0;
}
