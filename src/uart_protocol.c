/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
/** @file uart_protocol.c
 * @brief Portable UART framing and bytewise resynchronization.
 */
#include <string.h>
#include "uart_protocol.h"

/**
 * @brief Compute CRC-16/CCITT-FALSE over header and payload.
 * @param bytes Input bytes.
 * @param length Number of input bytes.
 * @return Sixteen-bit checksum.
 */
static uint16_t checksum(const uint8_t *bytes, size_t length)
{
	uint16_t crc = UART_CRC_INITIAL;

	for (size_t i = 0; i < length; i++) {
		crc ^= (uint16_t)bytes[i] << 8;
		for (unsigned int bit = 0; bit < 8U; bit++) {
			crc = (crc & 0x8000U) != 0U
				? (uint16_t)((crc << 1) ^ UART_CRC_POLYNOMIAL)
				: (uint16_t)(crc << 1);
		}
	}
	return crc;
}

/** @brief Decode a little-endian word.
 * @param bytes Two input bytes.
 * @return Decoded word.
 */
uint16_t uart_get_u16(const uint8_t *bytes)
{
	return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

/** @brief Encode a little-endian word.
 * @param bytes Two output bytes.
 * @param value Word to store.
 */
void uart_put_u16(uint8_t *bytes, uint16_t value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
}

/** @brief Serialize current telemetry into the existing status payload.
 * @param status Signed milliamps and channel validity.
 * @param payload Eight-byte output buffer.
 */
void uart_current_encode(const struct uart_current_status *status, uint8_t *payload)
{
	for (unsigned int i = 0; i < UART_CURRENT_COUNT; i++) {
		int16_t value = (status->valid_mask & (1U << i)) ? status->milliamps[i] : 0;

		uart_put_u16(payload + 2U * i, (uint16_t)value);
	}
	payload[6] = status->valid_mask & UART_CURRENT_VALID_MASK;
	payload[7] = UART_CURRENT_VERSION;
}

/** @brief Decode versioned currents and clear unavailable values.
 * @param payload Eight status bytes.
 * @param status Decoded currents and validity, zeroed on malformed/legacy payloads.
 * @return True when the current payload format is supported.
 */
bool uart_current_decode(const uint8_t *payload, struct uart_current_status *status)
{
	memset(status, 0, sizeof(*status));
	if (payload[7] != UART_CURRENT_VERSION || (payload[6] & ~UART_CURRENT_VALID_MASK) != 0U) {
		return false;
	}
	status->valid_mask = payload[6];
	for (unsigned int i = 0; i < UART_CURRENT_COUNT; i++) {
		if ((status->valid_mask & (1U << i)) != 0U) {
			uint16_t raw = uart_get_u16(payload + 2U * i);

			status->milliamps[i] = raw <= INT16_MAX ? (int32_t)raw : (int32_t)raw - 65536;
		}
	}
	return true;
}

/** @brief Serialize a fixed-size frame and checksum.
 * @param frame Source fields.
 * @param bytes Output buffer of UART_FRAME_SIZE bytes.
 */
void uart_frame_encode(const struct uart_frame *frame, uint8_t *bytes)
{
	bytes[0] = UART_SYNC_FIRST;
	bytes[1] = UART_SYNC_SECOND;
	bytes[2] = frame->type;
	bytes[3] = frame->flags;
	uart_put_u16(bytes + 4, frame->sequence);
	memcpy(bytes + 6, frame->payload, UART_PAYLOAD_SIZE);
	uart_put_u16(bytes + 14, checksum(bytes, 14));
}

/** @brief Scan a rolling window for a checksum-valid frame.
 * @param parser Persistent parser state.
 * @param byte Received byte.
 * @param frame Decoded output.
 * @return True when a complete frame is available.
 */
bool uart_frame_feed(struct uart_parser *parser, uint8_t byte, struct uart_frame *frame)
{
	parser->bytes[parser->used++] = byte;
	if (parser->used < UART_FRAME_SIZE) {
		return false;
	}
	uint8_t *bytes = parser->bytes;
	bool valid = bytes[0] == UART_SYNC_FIRST && bytes[1] == UART_SYNC_SECOND &&
		(bytes[2] == UART_FRAME_COMMAND || bytes[2] == UART_FRAME_STATUS) &&
		uart_get_u16(bytes + 14) == checksum(bytes, 14);

	if (valid) {
		frame->type = bytes[2];
		frame->flags = bytes[3];
		frame->sequence = uart_get_u16(bytes + 4);
		memcpy(frame->payload, bytes + 6, UART_PAYLOAD_SIZE);
		parser->used = 0;
	} else {
		memmove(bytes, bytes + 1, UART_FRAME_SIZE - 1U);
		parser->used--;
	}
	return valid;
}
