#ifndef LAB_QUADRATURE_H_
#define LAB_QUADRATURE_H_

#include <stdint.h>

/* State = (A << 1) | B. Positive sequence: 00 -> 01 -> 11 -> 10 -> 00.
 * Two-bit jumps are ambiguous: count no movement and record an error.
 * Repeated states are harmless (both GPIO IRQs can observe the same state).
 */
static inline int quadrature_step(uint8_t previous, uint8_t current)
{
	static const int8_t steps[16] = {
		 0,  1, -1,  0,
		-1,  0,  0,  1,
		 1,  0,  0, -1,
		 0, -1,  1,  0,
	};

	return steps[(previous << 2) | current];
}

#endif
