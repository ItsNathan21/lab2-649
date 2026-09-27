#ifndef WHEEL_INFO_H_
#define WHEEL_INFO_H_

#include <stdint.h>

/* Measured raw axis values. MIN/MAX refer to numeric values. */
#define WHEEL_STEERING_MIN          (-32768)
#define WHEEL_STEERING_MAX          32767
#define WHEEL_STEERING_LEFT         WHEEL_STEERING_MIN
#define WHEEL_STEERING_CENTER       0
#define WHEEL_STEERING_RIGHT        WHEEL_STEERING_MAX

#define WHEEL_THROTTLE_MIN          (-32768)
#define WHEEL_THROTTLE_MAX          32767
#define WHEEL_THROTTLE_RELEASED     WHEEL_THROTTLE_MAX
#define WHEEL_THROTTLE_FULLY_PRESSED WHEEL_THROTTLE_MIN

#define WHEEL_BRAKE_MIN             (-32768)
#define WHEEL_BRAKE_MAX             32767
#define WHEEL_BRAKE_RELEASED        WHEEL_BRAKE_MAX
/* Observed with a firm press; this is the raw endpoint. */
#define WHEEL_BRAKE_FULLY_PRESSED   WHEEL_BRAKE_MIN

/* Original wheel button indices, preserved as bit positions. */
#define WHEEL_BUTTON_A_INDEX             0U
#define WHEEL_BUTTON_B_INDEX             1U
#define WHEEL_BUTTON_X_INDEX             2U
#define WHEEL_BUTTON_Y_INDEX             3U
#define WHEEL_BUTTON_RIGHT_BLINKER_INDEX 4U
#define WHEEL_BUTTON_LEFT_BLINKER_INDEX  5U
#define WHEEL_BUTTON_RSB_INDEX           8U
#define WHEEL_BUTTON_LSB_INDEX           9U

/* Masks for setting or checking individual buttons in wheel_info.buttons. */
#define WHEEL_BUTTON_A             (1U << WHEEL_BUTTON_A_INDEX)
#define WHEEL_BUTTON_B             (1U << WHEEL_BUTTON_B_INDEX)
#define WHEEL_BUTTON_X             (1U << WHEEL_BUTTON_X_INDEX)
#define WHEEL_BUTTON_Y             (1U << WHEEL_BUTTON_Y_INDEX)
#define WHEEL_BUTTON_RIGHT_BLINKER (1U << WHEEL_BUTTON_RIGHT_BLINKER_INDEX)
#define WHEEL_BUTTON_LEFT_BLINKER  (1U << WHEEL_BUTTON_LEFT_BLINKER_INDEX)
#define WHEEL_BUTTON_RSB           (1U << WHEEL_BUTTON_RSB_INDEX)
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
