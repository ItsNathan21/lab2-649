# How to code this project

## Modules and ownership

- Give each worker thread, driver, or device module a matching `.c` and `.h` pair: for example, `src/uart_receiver.c` and `include/uart_receiver.h`.
- Keep implementations, private helper functions, device handles, thread stacks, kernel objects, and mutable state in the `.c` file. Make private functions and objects `static`.
- Put the public function declarations, types, typedefs, and named constants in the matching `.h` file. Never expose variables with `extern`; communicate through functions instead. Prefer macros or enum values for public constants.
- Each header must include what its declarations need. Include the module's own header in its implementation.
- Keep `main.c` focused on initialization and starting workers through their APIs. A worker owns its runtime loop; callers must not manipulate its thread or queue directly.
- Document API call context, initialization order, units, valid ranges, and error returns where relevant. Keep interrupts short; hand off longer work through private kernel objects.
- Header-only data contracts such as `wheel_info.h` need no empty `.c` file. The application entry point needs no artificial API header.

## Header guards

Use the filename in uppercase, including underscores, with `_H_` at the end:

```c
#ifndef UART_RECEIVER_H_
#define UART_RECEIVER_H_

/* Includes, documented constants/types, and public function declarations. */

#endif /* UART_RECEIVER_H_ */
```

## Short Doxygen comments

Start each file with `@file` and a brief description. Give every function, including static helpers and thread entries, a short `@brief` and one `@param` per actual parameter. Add `@return` for a returned result. Functions taking `void` need no fake parameter entry. Document public declarations in their header and implementations in their source.

```c
/**
 * @brief Set the requested motor duty.
 * @param side Motor to control.
 * @param duty_raw Signed duty in -65535..65535; zero coasts.
 * @return 0 on success, or a negative error.
 */
int motor_drive_raw(enum motor_side side, int32_t duty_raw);
```

Every non-guard `#define` gets roughly one sentence explaining its purpose and why that value was selected. If arbitrary or an initial estimate, say so; do not present an unmeasured value as proven.

```c
/** @brief Stack bytes; 1536 is an initial budget, not a measured maximum. */
#define WHEEL_RX_STACK_SIZE 1536
```

Use tabs for C indentation, `snake_case`, braces on separate lines for functions, and braces around control-flow bodies. Keep lines near 100 columns and comments short. Do not pack multiple operations onto a single line.

## Current layout

- `main.c`: initializes drivers and starts speed PID, UART, blinkers, and encoder reporting.
- `uart_receiver.c/.h`: USART1 interrupt, private packet queue, command worker, and start API. It sends speed requests to motor_controller, steering to servo, and enable states to blinker.
- `uart_status.c/.h`: dedicated periodic status heartbeat worker with current telemetry.
- `current_sensor.c/.h`: read-only ADC worker, per-channel calibration, and coherent snapshots.
- `current_conversion.c/.h`: portable signed voltage-to-current conversion.
- `uart_protocol.c/.h`: portable framing shared with the Pi.
- `servo.c/.h`: hardware PWM steering mapping; main initializes it, then UART owns updates.
- `blinker.c/.h`: independent indicator GPIO timing and enable/disable API.
- `encoder_monitor.c/.h`: reporting worker and start API.
- `pedal_control.c/.h`: pure throttle/brake mapping into wheel-speed requests.
- `motor_controller.c/.h`: independent speed PID loops, output ownership, and tuning constants.
- `motor.c/.h`, `encoder.c/.h`: device APIs with private hardware state.
- `quadrature.c/.h`: pure transition decoder.
- `wheel_info.h`: shared eight-byte wheel payload and measured wheel constants.
- `pi/proxy_receiver/receiver.c/.h`: UDP-to-UART program and protocol constants.

Add new implementation files to `CMakeLists.txt`; adding a header alone does not compile its `.c` file. Keep generated `build/` files and the upstream Zephyr tree out of application refactors.
