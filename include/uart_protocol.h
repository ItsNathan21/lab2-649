/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
/** @file uart_protocol.h
 * @brief Portable framed UART contract shared by the Pi and STM32.
 */
#ifndef UART_PROTOCOL_H_
#define UART_PROTOCOL_H_
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Both endpoints transmit every 20 ms to meet the heartbeat requirement. */
#define UART_LINK_PERIOD_MS 20U
/** @brief Three missed 20 ms updates allow 60 ms before declaring link loss. */
#define UART_LINK_TIMEOUT_MS (3U * UART_LINK_PERIOD_MS)
/** @brief Sixteen bytes cover sync, type, flags, sequence, wheel payload, and CRC. */
#define UART_FRAME_SIZE 16U
/** @brief Eight payload bytes preserve the existing wheel field layout. */
#define UART_PAYLOAD_SIZE 8U
/** @brief Distinct sync bytes permit recovery after missing or corrupt bytes. */
#define UART_SYNC_FIRST 0xa5U
/** @brief The second sync byte completes the chosen two-byte marker. */
#define UART_SYNC_SECOND 0x5aU
/** @brief Version-one command type identifies Pi-to-STM wheel frames. */
#define UART_FRAME_COMMAND 0x11U
/** @brief Version-one status type identifies STM-to-Pi heartbeat frames. */
#define UART_FRAME_STATUS 0x12U
/** @brief Bit zero confirms that the Pi wheel source is still fresh. */
#define UART_COMMAND_WHEEL_FRESH 0x01U
/** @brief Bit one confirms that STM heartbeats reach the Pi. */
#define UART_COMMAND_STATUS_FRESH 0x02U
/** @brief Both directions and the wheel source must be healthy to drive. */
#define UART_COMMAND_READY (UART_COMMAND_WHEEL_FRESH | UART_COMMAND_STATUS_FRESH)
/** @brief Bit zero reports missing command updates. */
#define UART_FAULT_LINK 0x01U
/** @brief Bit one reports stale wheel input or missing return heartbeats. */
#define UART_FAULT_SOURCE 0x02U
/** @brief Bit two reports the latched Y-button self-test. */
#define UART_FAULT_SELF_TEST 0x04U
/** @brief Bit three reports a hardware or PID fault requiring reset. */
#define UART_FAULT_HARDWARE 0x08U
/** @brief CRC-16/CCITT polynomial detects corruption of the frame contents. */
#define UART_CRC_POLYNOMIAL 0x1021U
/** @brief CCITT-FALSE starts at all ones; both endpoints use the same seed. */
#define UART_CRC_INITIAL 0xffffU

/** @brief Decoded frame; fields are explicitly serialized, never memcpy'd to the wire. */
struct uart_frame {
	uint8_t type;
	uint8_t flags;
	uint16_t sequence;
	uint8_t payload[UART_PAYLOAD_SIZE];
};
/** @brief Sliding parser state owned by each endpoint. */
struct uart_parser {
	uint8_t bytes[UART_FRAME_SIZE];
	size_t used;
};
/**
 * @brief Serialize a frame with a CCITT-FALSE checksum.
 * @param frame Source fields.
 * @param bytes Destination of UART_FRAME_SIZE bytes.
 */
void uart_frame_encode(const struct uart_frame *frame, uint8_t *bytes);
/**
 * @brief Consume one byte, scanning for a valid frame after any corruption.
 * @param parser Persistent parser initialized to zero.
 * @param byte Received byte.
 * @param frame Destination when a valid frame completes.
 * @return True only for a complete, checksum-valid known frame type.
 */
bool uart_frame_feed(struct uart_parser *parser, uint8_t byte, struct uart_frame *frame);
/**
 * @brief Read a little-endian word without alignment assumptions.
 * @param bytes Two input bytes.
 * @return Unsigned word.
 */
uint16_t uart_get_u16(const uint8_t *bytes);
/**
 * @brief Store a word in little-endian order.
 * @param bytes Two output bytes.
 * @param value Word to serialize.
 */
void uart_put_u16(uint8_t *bytes, uint16_t value);
#endif /* UART_PROTOCOL_H_ */
