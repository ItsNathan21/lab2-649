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

/* Front/rear are separate electrical outputs with one timing state per side. */
static const struct gpio_dt_spec outputs[BLINKER_COUNT][2] = {
	[BLINKER_LEFT] = {
		GPIO_DT_SPEC_GET(DT_NODELABEL(left_blinker), gpios),
		GPIO_DT_SPEC_GET(DT_NODELABEL(rear_left_blinker), gpios),
	},
	[BLINKER_RIGHT] = {
		GPIO_DT_SPEC_GET(DT_NODELABEL(right_blinker), gpios),
		GPIO_DT_SPEC_GET(DT_NODELABEL(rear_right_blinker), gpios),
	},
};
static struct blinker_state states[BLINKER_COUNT];
static K_MUTEX_DEFINE(blinker_lock);
static K_SEM_DEFINE(blinker_changed, 0, 1);
static K_THREAD_STACK_DEFINE(blinker_stack, BLINKER_STACK_SIZE);
static struct k_thread blinker_thread;
_Static_assert(BLINKER_HAZARD_RATE_HZ > 0U &&
	       1000U % (2U * BLINKER_HAZARD_RATE_HZ) == 0U, "Invalid hazard rate");
_Static_assert(BLINKER_CANCEL_DEGREES > 0U && BLINKER_CANCEL_DEGREES < 180U,
	       "Invalid cancel threshold");
static bool initialized;
static bool hazards;
static bool cancel_armed[BLINKER_COUNT];
static int64_t hazard_epoch;

/** @brief Write both native STM32 outputs without preemption between front and rear.
 * @param side Left or right side; caller holds blinker_lock.
 * @param level Desired common logic level.
 * @return First GPIO error, or zero; both outputs are attempted.
 */
static int set_side_outputs(enum blinker_side side, bool level)
{
	unsigned int key = irq_lock();
	int front = gpio_pin_set_dt(&outputs[side][0], level);
	int rear = gpio_pin_set_dt(&outputs[side][1], level);

	irq_unlock(key);
	return front != 0 ? front : rear;
}

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
		int ret = set_side_outputs(side, state->level);

		if (ret != 0) {
			state->enabled = false;
			state->active = false;
			state->level = false;
			int off_ret = set_side_outputs(side, false);

			printk("Blinker %d GPIO error: %d; off=%d\n", side, ret, off_ret);
		}
	}
}

/**
 * @brief Select one side's normal state while holding the lock; enabling cancels the other side.
 * @param side Output index to change.
 * @param enabled True to blink this side, false to turn it off.
 */
static void select_side(enum blinker_side side, bool enabled)
{
	states[side].enabled = enabled;
	cancel_armed[side] = false;
	if (enabled) {
		enum blinker_side other = side == BLINKER_LEFT ? BLINKER_RIGHT : BLINKER_LEFT;

		states[other].enabled = false;
		cancel_armed[other] = false;
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
			if (hazards) {
				int64_t half = 1000U / (2U * BLINKER_HAZARD_RATE_HZ);
				bool level = ((now - hazard_epoch) / half % 2) == 0;
				int ret = set_side_outputs(side, level);

				if (ret != 0) {
					printk("Hazard GPIO %d failed: %d\n", side, ret);
				}
				states[side].active = true;
				states[side].level = level;
				states[side].next_edge_ms = hazard_epoch +
					((now - hazard_epoch) / half + 1) * half;
			} else {
				update_output(side, now);
			}
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
		for (unsigned int end = 0; end < 2; end++) {
			if (!gpio_is_ready_dt(&outputs[side][end])) {
				return -ENODEV;
			}
			int ret = gpio_pin_configure_dt(&outputs[side][end], GPIO_OUTPUT_INACTIVE);

			if (ret != 0) {
				return ret;
			}
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
	if (!hazards) {
		select_side(side, enabled);
	}
	k_mutex_unlock(&blinker_lock);
	k_sem_give(&blinker_changed);
	return 0;
}

/** @brief Select synchronized hazards and clear normal blinker requests.
 * @param enabled True to enter the fault indication.
 * @return 0 on success, or -ENODEV before initialization.
 */
int blinker_hazards(bool enabled)
{
	if (!initialized) {
		return -ENODEV;
	}
	k_mutex_lock(&blinker_lock, K_FOREVER);
	if (enabled != hazards) {
		hazards = enabled;
		hazard_epoch = k_uptime_get();
		for (unsigned int side = 0; side < BLINKER_COUNT; side++) {
			states[side].enabled = false;
			cancel_armed[side] = false;
		}
	}
	k_mutex_unlock(&blinker_lock);
	k_sem_give(&blinker_changed);
	return 0;
}

/** @brief Toggle a normal indicator without duplicating its state in the receiver.
 * @param side Indicator to toggle.
 * @return 0 on success, or a negative argument/state error.
 */
int blinker_toggle(enum blinker_side side)
{
	if ((unsigned int)side >= BLINKER_COUNT) {
		return -EINVAL;
	}
	if (!initialized) {
		return -ENODEV;
	}
	k_mutex_lock(&blinker_lock, K_FOREVER);
	if (!hazards) {
		select_side(side, !states[side].enabled);
	}
	k_mutex_unlock(&blinker_lock);
	k_sem_give(&blinker_changed);
	return 0;
}

/** @brief Cancel only after entering and leaving the enabled side's turn region.
 * @param angle_mdeg Estimated logical servo angle relative to center.
 */
void blinker_steering(int32_t angle_mdeg)
{
	k_mutex_lock(&blinker_lock, K_FOREVER);
	if (!hazards) {
		for (unsigned int side = 0; side < BLINKER_COUNT; side++) {
			int32_t travel = side == BLINKER_LEFT ? -angle_mdeg : angle_mdeg;
			int32_t threshold = BLINKER_CANCEL_DEGREES * 1000;

			if (!states[side].enabled) {
				continue;
			}
			if (travel > threshold) {
				cancel_armed[side] = true;
			} else if (travel < threshold && cancel_armed[side]) {
				states[side].enabled = false;
				cancel_armed[side] = false;
			}
		}
	}
	k_mutex_unlock(&blinker_lock);
	k_sem_give(&blinker_changed);
}
