/** @file current_test.c
 * @brief Host regression tests for calibration and current status framing.
 */
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "current_conversion.h"
#include "uart_protocol.h"

/** @brief Check missing calibration, signed polarity, and overflow without hardware. */
static void test_conversion(void)
{
	/* Synthetic test calibration, not a claim about the installed sensor. */
	struct current_calibration calibration = {1650000, 100000};
	int16_t value = 123;

	assert(current_conversion_ma(1650000, &calibration, &value) && value == 0);
	assert(current_conversion_ma(1750000, &calibration, &value) && value == 1000);
	assert(current_conversion_ma(1550000, &calibration, &value) && value == -1000);
	calibration.uv_per_amp = -100000;
	assert(current_conversion_ma(1750000, &calibration, &value) && value == -1000);
	calibration.uv_per_amp = 0;
	assert(!current_conversion_ma(1750000, &calibration, &value) && value == 0);
	calibration.zero_uv = 0;
	calibration.uv_per_amp = 1000;
	assert(current_conversion_ma(32767, &calibration, &value) && value == INT16_MAX);
	assert(current_conversion_ma(-32768, &calibration, &value) && value == INT16_MIN);
	assert(!current_conversion_ma(32768, &calibration, &value) && value == 0);
	assert(!current_conversion_ma(-32769, &calibration, &value) && value == 0);
	calibration.zero_uv = INT32_MIN;
	calibration.uv_per_amp = 1;
	assert(!current_conversion_ma(INT32_MAX, &calibration, &value) && value == 0);
}

/** @brief Check byte order, invalid channel clearing, legacy rejection, and CRC integrity. */
static void test_status(void)
{
	struct uart_current_status input = {{1250, -2000, 0}, UART_CURRENT_VALID_MASK};
	struct uart_current_status output;
	struct uart_frame frame = {.type = UART_FRAME_STATUS, .flags = UART_FAULT_LINK,
		.sequence = 65535};
	struct uart_frame decoded;
	struct uart_parser parser = {0};
	uint8_t bytes[UART_FRAME_SIZE];

	uart_current_encode(&input, frame.payload);
	assert(frame.payload[0] == 0xe2 && frame.payload[1] == 0x04);
	assert(frame.payload[2] == 0x30 && frame.payload[3] == 0xf8);
	assert(frame.payload[6] == 7 && frame.payload[7] == UART_CURRENT_VERSION);
	uart_frame_encode(&frame, bytes);
	for (unsigned int i = 0; i < UART_FRAME_SIZE; i++) {
		assert(uart_frame_feed(&parser, bytes[i], &decoded) == (i == UART_FRAME_SIZE - 1));
	}
	assert(decoded.flags == UART_FAULT_LINK && decoded.sequence == 65535);
	assert(uart_current_decode(decoded.payload, &output));
	assert(output.valid_mask == 7);
	assert(output.milliamps[0] == 1250 && output.milliamps[1] == -2000);
	assert(output.milliamps[2] == 0); /* Valid zero is distinct from unavailable. */
	input.valid_mask = 1;
	uart_current_encode(&input, frame.payload);
	assert(uart_current_decode(frame.payload, &output));
	assert(output.valid_mask == 1 && output.milliamps[1] == 0);
	memset(frame.payload, 0, sizeof(frame.payload));
	assert(!uart_current_decode(frame.payload, &output) && output.valid_mask == 0);
	frame.payload[7] = UART_CURRENT_VERSION;
	frame.payload[6] = 0x80;
	assert(!uart_current_decode(frame.payload, &output) && output.valid_mask == 0);
	input.milliamps[0] = INT16_MIN;
	input.milliamps[1] = INT16_MAX;
	input.valid_mask = 7;
	uart_current_encode(&input, frame.payload);
	assert(uart_current_decode(frame.payload, &output));
	assert(output.milliamps[0] == INT16_MIN && output.milliamps[1] == INT16_MAX);
	bytes[7] ^= 1;
	memset(&parser, 0, sizeof(parser));
	for (unsigned int i = 0; i < UART_FRAME_SIZE; i++) {
		assert(!uart_frame_feed(&parser, bytes[i], &decoded));
	}
	/* Recover alignment when a good frame immediately follows corruption. */
	uart_frame_encode(&frame, bytes);
	for (unsigned int i = 0; i < UART_FRAME_SIZE; i++) {
		assert(uart_frame_feed(&parser, bytes[i], &decoded) == (i == UART_FRAME_SIZE - 1));
	}
}

/** @brief Run portable tests; exits unsuccessfully on any failed assertion.
 * @return Zero after all checks pass.
 */
int main(void)
{
	test_conversion();
	test_status();
	puts("Current conversion and UART telemetry tests passed");
	return 0;
}
