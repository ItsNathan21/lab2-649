/** @file current_sensor.c
 * @brief Periodic ADC sampling, voltage averaging, and read-only current telemetry.
 */
#include <errno.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "current_conversion.h"
#include "current_sensor.h"

static const struct adc_dt_spec inputs[CURRENT_SENSOR_COUNT] = {
	ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0),
	ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 1),
	ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 2),
};
static const struct current_calibration calibration[CURRENT_SENSOR_COUNT] = {
	{ CURRENT_SENSOR_LEFT_ZERO_UV, CURRENT_SENSOR_LEFT_UV_PER_AMP },
	{ CURRENT_SENSOR_RIGHT_ZERO_UV, CURRENT_SENSOR_RIGHT_UV_PER_AMP },
	{ CURRENT_SENSOR_SERVO_ZERO_UV, CURRENT_SENSOR_SERVO_UV_PER_AMP },
};
static struct current_sensor_snapshot latest;
static struct k_spinlock snapshot_lock;
static K_THREAD_STACK_DEFINE(sensor_stack, CURRENT_SENSOR_STACK_SIZE);
static struct k_thread sensor_thread;
static bool started;

_Static_assert(CURRENT_SENSOR_AVERAGE_SAMPLES > 0U, "Empty current averaging window");
_Static_assert(CURRENT_SENSOR_REFERENCE_UV > 0, "Invalid ADC reference");

/**
 * @brief Sample all channels outside the heartbeat thread and publish coherent telemetry.
 * @param arg1 Unused thread argument.
 * @param arg2 Unused thread argument.
 * @param arg3 Unused thread argument.
 */
static void sensor_worker(void *arg1, void *arg2, void *arg3)
{
	uint16_t history[CURRENT_SENSOR_COUNT][CURRENT_SENSOR_AVERAGE_SAMPLES] = {0};
	unsigned int count[CURRENT_SENSOR_COUNT] = {0};
	unsigned int cursor[CURRENT_SENSOR_COUNT] = {0};
	int64_t next_ms = k_uptime_get();
	int64_t last_print_ms = next_ms;

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);
	while (true) {
		struct current_sensor_snapshot sample = {0};

		for (unsigned int i = 0; i < CURRENT_SENSOR_COUNT; i++) {
			uint16_t raw = 0;
			struct adc_sequence sequence = {
				.buffer = &raw,
				.buffer_size = sizeof(raw),
			};
			int ret = adc_sequence_init_dt(&inputs[i], &sequence);

			if (ret == 0) {
				ret = adc_read_dt(&inputs[i], &sequence);
			}
			if (ret != 0) {
				count[i] = 0;
				cursor[i] = 0;
				continue;
			}
			sample.raw[i] = raw;
			sample.sampled_mask |= BIT(i);
			/* Rail readings cannot establish current; reset smoothing after clipping. */
			if (raw == 0 || raw >= BIT(CURRENT_SENSOR_RESOLUTION) - 1U) {
				sample.voltage_uv[i] = (int64_t)raw * CURRENT_SENSOR_REFERENCE_UV /
					BIT(CURRENT_SENSOR_RESOLUTION);
				count[i] = 0;
				cursor[i] = 0;
				continue;
			}
			history[i][cursor[i]] = raw;
			cursor[i] = (cursor[i] + 1U) % CURRENT_SENSOR_AVERAGE_SAMPLES;
			count[i] = MIN(count[i] + 1U, CURRENT_SENSOR_AVERAGE_SAMPLES);
			uint32_t sum = 0;

			for (unsigned int j = 0; j < count[i]; j++) {
				sum += history[i][j];
			}
			sample.voltage_uv[i] = (int64_t)sum * CURRENT_SENSOR_REFERENCE_UV /
				(count[i] * BIT(CURRENT_SENSOR_RESOLUTION));
			if (current_conversion_ma(sample.voltage_uv[i], &calibration[i],
						  &sample.milliamps[i])) {
				sample.valid_mask |= BIT(i);
			}
		}
		sample.timestamp_ms = k_uptime_get();
		k_spinlock_key_t key = k_spin_lock(&snapshot_lock);

		latest = sample;
		k_spin_unlock(&snapshot_lock, key);
		if (sample.timestamp_ms - last_print_ms >= CURRENT_SENSOR_PRINT_MS) {
			printk("CURRENT L/R/S raw=%u/%u/%u mV=%d/%d/%d mA=%d/%d/%d "
			       "sampled=0x%02x valid=0x%02x\n",
			       sample.raw[0], sample.raw[1], sample.raw[2],
			       sample.voltage_uv[0] / 1000, sample.voltage_uv[1] / 1000,
			       sample.voltage_uv[2] / 1000, sample.milliamps[0],
			       sample.milliamps[1], sample.milliamps[2],
			       sample.sampled_mask, sample.valid_mask);
			last_print_ms = sample.timestamp_ms;
		}
		next_ms += CURRENT_SENSOR_PERIOD_MS;
		if (next_ms <= k_uptime_get()) {
			next_ms = k_uptime_get() + CURRENT_SENSOR_PERIOD_MS;
		}
		k_sleep(K_TIMEOUT_ABS_MS(next_ms));
	}
}

/** @brief Configure the three ADC channels, then start sampling without blocking control.
 * @return 0 on success, or a negative ADC/state error.
 */
int current_sensor_start(void)
{
	if (started) {
		return -EALREADY;
	}
	for (unsigned int i = 0; i < CURRENT_SENSOR_COUNT; i++) {
		if (!adc_is_ready_dt(&inputs[i])) {
			return -ENODEV;
		}
		if (inputs[i].resolution != CURRENT_SENSOR_RESOLUTION) {
			return -EINVAL;
		}
		int ret = adc_channel_setup_dt(&inputs[i]);

		if (ret != 0) {
			return ret;
		}
	}
	started = true;
	k_thread_create(&sensor_thread, sensor_stack, K_THREAD_STACK_SIZEOF(sensor_stack),
			sensor_worker, NULL, NULL, NULL, CURRENT_SENSOR_PRIORITY, 0, K_NO_WAIT);
	return 0;
}

/** @brief Snapshot without waiting for an ADC conversion; stale currents are unavailable.
 * @param out Destination snapshot, accessible from thread context.
 */
void current_sensor_snapshot(struct current_sensor_snapshot *out)
{
	k_spinlock_key_t key = k_spin_lock(&snapshot_lock);

	*out = latest;
	k_spin_unlock(&snapshot_lock, key);
	if (k_uptime_get() - out->timestamp_ms >= CURRENT_SENSOR_STALE_MS) {
		out->valid_mask = 0;
		out->sampled_mask = 0;
	}
}
