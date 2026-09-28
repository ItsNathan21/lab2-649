/** @file encoder.h
 * @brief Encoder API and constants.
 */
#ifndef ENCODER_H_
#define ENCODER_H_

#include <stdint.h>

/** @brief 1320 measured x4 counts per wheel revolution; do not multiply by four again. */
#define ENCODER_COUNTS_PER_REV 1320

/** @brief Indices for the two wheel channels and their count. */
enum encoder_side {
	ENCODER_LEFT,
	ENCODER_RIGHT,
	ENCODER_COUNT
};

struct encoder_snapshot {
	int64_t counts[ENCODER_COUNT];
	uint32_t invalid_transitions[ENCODER_COUNT];
	uint32_t read_errors;
	int64_t timestamp_ms;
};

/* Call once with wheels stationary. Counts start at zero, not absolute angle. */
/**
 * @brief Initialize both encoders once with the wheels stationary.
 * @return 0 on success, or a negative initialization error.
 */
int encoders_init(void);
/* Thread-context coherent snapshot of both wheels; caller supplies storage. */
/**
 * @brief Copy a coherent encoder snapshot from thread context.
 * @param out Non-null caller-owned destination.
 */
void encoders_snapshot(struct encoder_snapshot *out);

#endif /* ENCODER_H_ */
