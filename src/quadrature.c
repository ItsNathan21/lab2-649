/** @file quadrature.c
 * @brief Pure x4 quadrature transition decoding.
 */
#include "quadrature.h"

/**
 * @brief Decode one x4 quadrature transition.
 * @param previous Previous (A << 1) | B state, in 0..3.
 * @param current Current (A << 1) | B state, in 0..3.
 * @return Signed step; repeated states and two-bit jumps give zero.
 */
int quadrature_step(uint8_t previous, uint8_t current)
{
	static const int8_t steps[16] = {
		0, 1, -1, 0, -1, 0, 0, 1, 1, 0, 0, -1, 0, -1, 1, 0,
	};

	return steps[(previous << 2) | current];
}
