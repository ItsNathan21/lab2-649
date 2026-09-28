/** @file blinker.h
 * @brief Independent left/right GPIO blinkers.
 */
#ifndef BLINKER_H_
#define BLINKER_H_

#include <stdbool.h>
#include <stdint.h>

/** @brief Normal indicators blink once per second; choose a positive divisor of 500 Hz. */
#define BLINKER_RATE_HZ        1U
/** @brief Equal half-periods give 50% duty; 2 Hz gives 250 ms per phase. */
#define BLINKER_HALF_PERIOD_MS (1000U / (2U * BLINKER_RATE_HZ))
/** @brief Stack bytes; 1024 is an initial budget for GPIO updates and error reporting. */
#define BLINKER_STACK_SIZE     1024
/** @brief Priority 3 lets hazard changes run promptly ahead of routine reporting. */
#define BLINKER_PRIORITY       3

/** @brief The two independently enabled indicator outputs. */
enum blinker_side {
	BLINKER_LEFT,
	BLINKER_RIGHT,
	BLINKER_COUNT
};

/** @brief Per-output timing state; module-owned instances are private. */
struct blinker_state {
	bool enabled;
	bool active;
	bool level;
	int64_t next_edge_ms;
};

/**
 * @brief Initialize both outputs off and start the worker; call once from main.
 * @return 0 on success, -EALREADY if started, or a negative GPIO error.
 */
int blinker_init(void);

/**
 * @brief Request blinking or steady off; the worker applies changes when scheduled.
 * @param side BLINKER_LEFT or BLINKER_RIGHT.
 * @param enabled True to blink, false to turn off; repeated values preserve blink phase.
 * @return 0 on success, -EINVAL for an invalid side, or -ENODEV before initialization.
 */
int blinker_set(enum blinker_side side, bool enabled);

/** @brief Fault hazards use the required 2 Hz rate, independent of normal indicators. */
#define BLINKER_HAZARD_RATE_HZ 2U
/** @brief Ten degrees is the initial estimated servo displacement for auto-cancel. */
#define BLINKER_CANCEL_DEGREES 10U
/**
 * @brief Override normal indicators with synchronized hazards, clearing normal selections.
 * @param enabled True during any fail-safe or self-test.
 * @return 0 on success, or -ENODEV before initialization.
 */
int blinker_hazards(bool enabled);
/**
 * @brief Toggle a normal indicator using module-owned state; ignored during hazards.
 * @param side Left or right indicator.
 * @return 0 on success, or a negative argument/state error.
 */
int blinker_toggle(enum blinker_side side);
/**
 * @brief Arm on a turn past the threshold and cancel on its return below the threshold.
 * @param angle_mdeg Commanded servo angle: negative left, positive right.
 */
void blinker_steering(int32_t angle_mdeg);

#endif /* BLINKER_H_ */
