/** @file current_conversion.h
 * @brief Portable conversion of ADC-pin voltage to signed current.
 */
#ifndef CURRENT_CONVERSION_H_
#define CURRENT_CONVERSION_H_

#include <stdbool.h>
#include <stdint.h>

/** @brief Per-sensor calibration measured at the MCU pin, after any voltage divider. */
struct current_calibration {
	int32_t zero_uv;
	int32_t uv_per_amp;
};

/**
 * @brief Convert voltage to milliamps without inventing an unknown calibration.
 * @param voltage_uv Voltage at the ADC pin in microvolts.
 * @param calibration Zero-current offset and signed sensitivity; zero sensitivity is unset.
 * @param milliamps Output, cleared on failure; limited to the signed 16-bit wire range.
 * @return True on success, false for missing calibration or unrepresentable current.
 */
bool current_conversion_ma(int32_t voltage_uv, const struct current_calibration *calibration,
			   int16_t *milliamps);

#endif /* CURRENT_CONVERSION_H_ */
