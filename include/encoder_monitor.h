/** @file encoder_monitor.h
 * @brief Encoder monitor API and constants.
 */
#ifndef ENCODER_MONITOR_H_
#define ENCODER_MONITOR_H_

/** @brief Report every 100 ms to retain the existing 10 Hz console rate. */
#define ENCODER_MONITOR_PERIOD_MS 100
/** @brief Stack bytes; 1536 is an initial budget for snapshot calculations and printing. */
#define ENCODER_MONITOR_STACK_SIZE 1536
/** @brief Priority 6 lets the priority-5 UART worker run before reporting. */
#define ENCODER_MONITOR_PRIORITY 6

/**
 * @brief Start reporting once, after encoders_init(); call from main only.
 * @return 0 when started, or -EALREADY if already started.
 */
int encoder_monitor_start(void);

#endif /* ENCODER_MONITOR_H_ */
