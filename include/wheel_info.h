/** @file wheel_info.h
 * @brief Wheel info types and declarations.
 */
#ifndef WHEEL_INFO_H_
#define WHEEL_INFO_H_

#include <stdint.h>

/* Measured raw axis values. MIN/MAX refer to numeric values. */
/** @brief Measured steering min: (-32768); preserves the wheel's raw axis convention. */
#define WHEEL_STEERING_MIN          (-32768)
/** @brief Measured steering max: 32767; preserves the wheel's raw axis convention. */
#define WHEEL_STEERING_MAX          32767
/** @brief Measured steering left: WHEEL_STEERING_MIN; preserves the wheel's raw axis convention. */
#define WHEEL_STEERING_LEFT         WHEEL_STEERING_MIN
/** @brief Measured steering center: 0; preserves the wheel's raw axis convention. */
#define WHEEL_STEERING_CENTER       0
/**
 * @brief Measured steering right: WHEEL_STEERING_MAX; preserves the wheel's raw axis convention.
 */
#define WHEEL_STEERING_RIGHT        WHEEL_STEERING_MAX

/** @brief Measured throttle min: (-32768); preserves the wheel's raw axis convention. */
#define WHEEL_THROTTLE_MIN          (-32768)
/** @brief Measured throttle max: 32767; preserves the wheel's raw axis convention. */
#define WHEEL_THROTTLE_MAX          32767
/**
 * @brief Measured throttle released: WHEEL_THROTTLE_MAX; preserves the wheel's raw axis
 * convention.
 */
#define WHEEL_THROTTLE_RELEASED     WHEEL_THROTTLE_MAX
/**
 * @brief Measured throttle fully pressed: WHEEL_THROTTLE_MIN; preserves the wheel's raw axis
 * convention.
 */
#define WHEEL_THROTTLE_FULLY_PRESSED WHEEL_THROTTLE_MIN

/** @brief Measured brake min: (-32768); preserves the wheel's raw axis convention. */
#define WHEEL_BRAKE_MIN             (-32768)
/** @brief Measured brake max: 32767; preserves the wheel's raw axis convention. */
#define WHEEL_BRAKE_MAX             32767
/** @brief Measured brake released: WHEEL_BRAKE_MAX; preserves the wheel's raw axis convention. */
#define WHEEL_BRAKE_RELEASED        WHEEL_BRAKE_MAX
/* Observed with a firm press; this is the raw endpoint. */
/**
 * @brief Measured brake fully pressed: WHEEL_BRAKE_MIN; preserves the wheel's raw axis
 * convention.
 */
#define WHEEL_BRAKE_FULLY_PRESSED   WHEEL_BRAKE_MIN

/* Original wheel button indices, preserved as bit positions. */
/** @brief A button index 0, matching the recorded wheel mapping. */
#define WHEEL_BUTTON_A_INDEX             0U
/** @brief B button index 1, matching the recorded wheel mapping. */
#define WHEEL_BUTTON_B_INDEX             1U
/** @brief X button index 2, matching the recorded wheel mapping. */
#define WHEEL_BUTTON_X_INDEX             2U
/** @brief Y button index 3, matching the recorded wheel mapping. */
#define WHEEL_BUTTON_Y_INDEX             3U
/** @brief Right Blinker button index 4, matching the recorded wheel mapping. */
#define WHEEL_BUTTON_RIGHT_BLINKER_INDEX 4U
/** @brief Left Blinker button index 5, matching the recorded wheel mapping. */
#define WHEEL_BUTTON_LEFT_BLINKER_INDEX  5U
/** @brief Rsb button index 8, matching the recorded wheel mapping. */
#define WHEEL_BUTTON_RSB_INDEX           8U
/** @brief Lsb button index 9, matching the recorded wheel mapping. */
#define WHEEL_BUTTON_LSB_INDEX           9U

/* Masks for setting or checking individual buttons in wheel_info.buttons. */
/** @brief A mask uses its original wheel index as the bit position. */
#define WHEEL_BUTTON_A             (1U << WHEEL_BUTTON_A_INDEX)
/** @brief B mask uses its original wheel index as the bit position. */
#define WHEEL_BUTTON_B             (1U << WHEEL_BUTTON_B_INDEX)
/** @brief X mask uses its original wheel index as the bit position. */
#define WHEEL_BUTTON_X             (1U << WHEEL_BUTTON_X_INDEX)
/** @brief Y mask uses its original wheel index as the bit position. */
#define WHEEL_BUTTON_Y             (1U << WHEEL_BUTTON_Y_INDEX)
/** @brief Right Blinker mask uses its original wheel index as the bit position. */
#define WHEEL_BUTTON_RIGHT_BLINKER (1U << WHEEL_BUTTON_RIGHT_BLINKER_INDEX)
/** @brief Left Blinker mask uses its original wheel index as the bit position. */
#define WHEEL_BUTTON_LEFT_BLINKER  (1U << WHEEL_BUTTON_LEFT_BLINKER_INDEX)
/** @brief Rsb mask uses its original wheel index as the bit position. */
#define WHEEL_BUTTON_RSB           (1U << WHEEL_BUTTON_RSB_INDEX)
/** @brief Lsb mask uses its original wheel index as the bit position. */
#define WHEEL_BUTTON_LSB           (1U << WHEEL_BUTTON_LSB_INDEX)

/*
 * Wheel information sent from the Pi to the STM32 over UART.
 * Wire layout: 8 bytes, in field order, all fields little-endian.
 * Packed prevents compiler padding; it does not convert byte order.
 */
struct wheel_info {
	/* Raw steering: left -32768, center 0, right 32767. */
	int16_t steering;

	/* Raw throttle: released 32767, fully pressed -32768. */
	int16_t throttle;

	/* Raw brake: released 32767, fully pressed -32768. */
	int16_t brake;

	/* 1 = pressed. Uses original indices; 16 bits accommodates bits 8/9.
	 * Unused bits must be zero.
	 */
	uint16_t buttons;
} __attribute__((packed));

_Static_assert(sizeof(struct wheel_info) == 8, "wheel_info must be 8 bytes");

#endif /* WHEEL_INFO_H_ */
