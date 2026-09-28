/** @file quadrature.h
 * @brief Quadrature API and constants.
 */
#ifndef QUADRATURE_H_
#define QUADRATURE_H_

#include <stdint.h>

/**
 * @brief Decode one x4 edge; repeated states and ambiguous two-bit jumps count zero.
 * @param previous Previous (A << 1) | B state, in 0..3.
 * @param current Current (A << 1) | B state, in 0..3.
 * @return +1 for 00->01->11->10->00, -1 in reverse, otherwise 0.
 */
int quadrature_step(uint8_t previous, uint8_t current);

#endif /* QUADRATURE_H_ */
