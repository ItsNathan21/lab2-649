#ifndef LAB_ENCODER_H_
#define LAB_ENCODER_H_

#include <stdint.h>

enum encoder_side { ENCODER_LEFT, ENCODER_RIGHT, ENCODER_COUNT };

struct encoder_snapshot {
	int64_t counts[ENCODER_COUNT];
	uint32_t invalid_transitions[ENCODER_COUNT];
	uint32_t read_errors;
	int64_t timestamp_ms;
};

/* Call once with wheels stationary. Counts start at zero, not absolute angle. */
int encoders_init(void);
/* Thread-context coherent snapshot of both wheels; caller supplies storage. */
void encoders_snapshot(struct encoder_snapshot *out);

#endif
