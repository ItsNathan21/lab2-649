#include <assert.h>
#include <stdio.h>
#include "quadrature.h"

int main(void)
{
	/* Exhaust all input pairs: same state, one-bit edge, ambiguous jump. */
	for (uint8_t old = 0; old < 4; old++) {
		for (uint8_t now = 0; now < 4; now++) {
			int delta = quadrature_step(old, now);
			if (old == now || (old ^ now) == 3) {
				assert(delta == 0);
			} else {
				assert(delta == 1 || delta == -1);
				assert(delta == -quadrature_step(now, old));
			}
		}
	}
	const uint8_t forward[] = {0, 1, 3, 2, 0};
	int count = 0;
	for (unsigned int i = 1; i < sizeof(forward); i++) {
		count += quadrature_step(forward[i - 1], forward[i]);
	}
	assert(count == 4); /* One electrical cycle yields four counts. */
	for (unsigned int i = sizeof(forward) - 1; i > 0; i--) {
		count += quadrature_step(forward[i], forward[i - 1]);
	}
	assert(count == 0); /* Returning through the reverse cycle cancels. */
	/* Contact-like bouncing or reversal at an edge must not accumulate. */
	assert(quadrature_step(0, 1) + quadrature_step(1, 0) == 0);
	/* After a skipped transition, the next valid edge can be decoded. */
	assert(quadrature_step(0, 3) == 0);
	assert(quadrature_step(3, 2) == 1);
	puts("quadrature tests passed");
	return 0;
}
