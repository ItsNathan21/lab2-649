/** @file current_sensor.h
 * @brief Read-only left-motor, right-motor, and servo current monitoring.
 */
#ifndef CURRENT_SENSOR_H_
#define CURRENT_SENSOR_H_

#include <stdint.h>

/** @brief Three sensors in wire order: left motor, right motor, servo. */
#define CURRENT_SENSOR_COUNT 3U
/** @brief Sample each input every 10 ms; initial rate for bring-up, not measured timing. */
#define CURRENT_SENSOR_PERIOD_MS 10U
/** @brief Reject data older than 100 ms even if the sampling worker stops progressing. */
#define CURRENT_SENSOR_STALE_MS 100U
/** @brief Nominal ADC reference; replace with measured VDDA for accurate conversion. */
#define CURRENT_SENSOR_REFERENCE_UV 3300000
/** @brief STM32F401 ADC resolution selected for these channels. */
#define CURRENT_SENSOR_RESOLUTION 12U
/** @brief Average four scans (40 ms window) to reduce PWM noise during bring-up. */
#define CURRENT_SENSOR_AVERAGE_SAMPLES 4U
/** @brief Priority 5 keeps sampling below heartbeat, fault handling, and motor PID. */
#define CURRENT_SENSOR_PRIORITY 5
/** @brief Initial sampling-thread stack budget, not a measured high-water mark. */
#define CURRENT_SENSOR_STACK_SIZE 1536
/** @brief Print at 10 Hz so the recorder gets ~10 readings per 1 s stall test. */
#define CURRENT_SENSOR_PRINT_MS 100U

/* ACS712 powered at 5 V, with separate 10k/10k dividers on OUT: ADC voltage = OUT/2.
 * Confirmed 5A modules use nominal 185 mV/A, divided to 92500 uV/A at the ADC.
 * Nominal zero is 1.25 V at the ADC; replace with each measured zero-current voltage.
 * Nominal ADC-pin sensitivities: 5A=92500, 20A=50000, 30A=33000 uV/A.
 * See CURRENT_SENSOR_WIRING.md. Do not use undivided ACS712 sensitivities here.
 */
/** @brief Left zero: idle mean after fixing the buck ground on 10/01, 12 V on, wheels stopped. */
#define CURRENT_SENSOR_LEFT_ZERO_UV 1321300
/** @brief Nominal 185 mV/A halved by 10k/10k; negative: left stall lowered OUT by 61 mV. */
#define CURRENT_SENSOR_LEFT_UV_PER_AMP (-92500)
/** @brief Right zero: idle mean after fixing the buck ground on 10/01, 12 V on, wheels stopped. */
#define CURRENT_SENSOR_RIGHT_ZERO_UV 1334000
/** @brief Nominal 185 mV/A halved by 10k/10k; right stall raised OUT by 96 mV. */
#define CURRENT_SENSOR_RIGHT_UV_PER_AMP 92500
/** @brief Servo zero: idle mean after the buck ground fix on 10/01; includes holding current. */
#define CURRENT_SENSOR_SERVO_ZERO_UV 1325000
/** @brief Nominal 185 mV/A halved by 10k/10k; negative because servo stall lowered OUT. */
#define CURRENT_SENSOR_SERVO_UV_PER_AMP (-92500)

/** @brief Coherent scan; valid bits refer to calibrated current, not sensor presence. */
struct current_sensor_snapshot {
	uint16_t raw[CURRENT_SENSOR_COUNT];
	int32_t voltage_uv[CURRENT_SENSOR_COUNT];
	int16_t milliamps[CURRENT_SENSOR_COUNT];
	uint8_t valid_mask;
	uint8_t sampled_mask;
	int64_t timestamp_ms;
};

/**
 * @brief Configure ADC inputs and start the read-only worker once from main.
 * @return 0 on success or negative initialization error; no actuator state is changed.
 */
int current_sensor_start(void);
/**
 * @brief Copy the latest scan, clearing current validity if stale or not yet sampled.
 * @param out Non-null destination; may be read from thread context before startup.
 */
void current_sensor_snapshot(struct current_sensor_snapshot *out);

#endif /* CURRENT_SENSOR_H_ */
