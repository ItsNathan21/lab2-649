/** @file encoder_monitor.c
 * @brief Periodic encoder count and speed reporting.
 */
#include <errno.h>
#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "encoder.h"
#include "encoder_monitor.h"

static K_THREAD_STACK_DEFINE(monitor_stack, ENCODER_MONITOR_STACK_SIZE);
static struct k_thread monitor_thread;
static bool started;

/**
 * @brief Print encoder counts and speed on an absolute schedule.
 * @param arg1 Unused Zephyr thread argument.
 * @param arg2 Unused Zephyr thread argument.
 * @param arg3 Unused Zephyr thread argument.
 */
static void encoder_monitor_thread(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);
	struct encoder_snapshot last, now;
	encoders_snapshot(&last);
	int64_t next = last.timestamp_ms;

	while (1) {
		/* Absolute schedule avoids adding print time to each period. */
		next += ENCODER_MONITOR_PERIOD_MS;
		k_sleep(K_TIMEOUT_ABS_MS(next));
		encoders_snapshot(&now);
		int64_t elapsed = now.timestamp_ms - last.timestamp_ms;
		if (elapsed <= 0) {
			continue;
		}
		int64_t left_cps =
			(now.counts[ENCODER_LEFT] - last.counts[ENCODER_LEFT]) * 1000 / elapsed;
		int64_t right_cps =
			(now.counts[ENCODER_RIGHT] - last.counts[ENCODER_RIGHT]) * 1000 / elapsed;
		/* Integer millirpm avoids float printf and preserves low-speed detail. */
		int64_t left_mrpm = (now.counts[0] - last.counts[0]) * 60000000LL /
				    (ENCODER_COUNTS_PER_REV * elapsed);
		int64_t right_mrpm = (now.counts[1] - last.counts[1]) * 60000000LL /
				     (ENCODER_COUNTS_PER_REV * elapsed);
		printk("ENC L=%lld (%lld cps, %lld mRPM) R=%lld (%lld cps, %lld mRPM) "
		       "invalid=%u/%u read_errors=%u\n",
		       (long long)now.counts[0], (long long)left_cps, (long long)left_mrpm,
		       (long long)now.counts[1], (long long)right_cps, (long long)right_mrpm,
		       (unsigned int)now.invalid_transitions[0],
		       (unsigned int)now.invalid_transitions[1], (unsigned int)now.read_errors);
		last = now;
		if (next < now.timestamp_ms) {
			next = now.timestamp_ms;
		}
	}
}

/**
 * @brief Start the reporting worker once after encoder initialization.
 * @return 0 when started, or -EALREADY if already started.
 */
int encoder_monitor_start(void)
{
	if (started) {
		return -EALREADY;
	}
	started = true;
	k_thread_create(&monitor_thread, monitor_stack, K_THREAD_STACK_SIZEOF(monitor_stack),
			encoder_monitor_thread, NULL, NULL, NULL, ENCODER_MONITOR_PRIORITY, 0,
			K_NO_WAIT);
	return 0;
}
