/** @file current_conversion.c
 * @brief Fixed-point, calibrated current conversion shared with host tests.
 */
#include "current_conversion.h"

/**
 * @brief Convert ADC-pin voltage using signed sensitivity and reject wire overflow.
 * @param voltage_uv ADC-pin voltage in microvolts.
 * @param calibration Per-channel offset and sensitivity at the ADC pin.
 * @param milliamps Signed output in milliamps; zero when unavailable.
 * @return True if calibrated and representable, otherwise false.
 */
bool current_conversion_ma(int32_t voltage_uv, const struct current_calibration *calibration,
			   int16_t *milliamps)
{
	*milliamps = 0;
	if (calibration->uv_per_amp == 0) {
		return false;
	}
	int64_t value = ((int64_t)voltage_uv - calibration->zero_uv) * 1000 /
		calibration->uv_per_amp;

	if (value < INT16_MIN || value > INT16_MAX) {
		return false;
	}
	*milliamps = (int16_t)value;
	return true;
}
