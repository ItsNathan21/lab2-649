# Encoder bring-up (Nucleo F401RE)

The firmware counts two quadrature encoders with both-edge GPIO interrupts
(x4 decoding). It leaves the existing USART1 receiver running and prints a
snapshot every 100 ms over the ST-LINK USART2 console at 115200 baud.

## Connections

Use the PC labels on the supplied `nucleo_f401re_morpho.png` diagram and board
headers. Left/right are viewed from the car's forward-facing direction.

| Encoder signal | STM32 pin | Morpho connector |
| --- | --- | --- |
| Left A | PC0 | CN7 |
| Left B | PC1 | CN7 |
| Right A | PC2 | CN7 |
| Right B | PC3 | CN7 |
| Both encoder grounds | GND | GND |

PC0/PC1 are also exposed as Arduino A5/A4. Reserve all four pins for these
encoders; do not enable ADC functions on them. They use separate EXTI lines
0, 1, 2, 3 and avoid USART1 (PB6/PB7) and USART2 (PA2/PA3).

**Confirm the motor/encoder model before connecting supply or signal wires.**
The motor power pair and encoder supply pair serve different circuits. Do not
infer their function from wire color alone. Use the encoder's specified supply
and verify A/B are compatible with 3.3 V GPIO inputs; level-shift if necessary.
Encoder ground and Nucleo ground must be common. The overlay enables weak
pull-downs to keep disconnected inputs quiet; open-collector outputs require
appropriate pull-ups to 3.3 V instead, after checking the encoder specification.

For the first test, keep motor power disconnected, wheels raised and stationary
at reset. The code does not configure motor outputs or implement braking.

## Build and test

From this application's directory with your Zephyr environment active:

```sh
west build -b nucleo_f401re/stm32f401xe -d build-encoder .
west flash -d build-encoder --runner openocd
```

The board overlay is selected automatically. Open the ST-LINK serial port at
115200 baud. On macOS use your `/dev/cu.usbmodem...` device; on Linux use your
`/dev/ttyACM...` device.

1. With both wheels still, counts should stay constant, and errors stay zero.
2. Turn only the left wheel: only L should change. Repeat for the right wheel.
3. Reverse each wheel: its count must reverse. Forward travel should give a
   positive count on BOTH wheels. Change that side's `direction[]` entry in
   `src/encoder.c` to -1 if needed, then rebuild.
4. Mark the tire, rotate exactly ten wheel revolutions, and divide the absolute
   count difference by ten. Repeat to establish decoded counts per wheel
   revolution (CPR), including the gearbox and x4 decoding.

Example format (illustrative values):

```text
ENC L=120 (40 cps) R=0 (0 cps) invalid=0/0 read_errors=0
```

`cps` means signed counts per second, calculated from the actual elapsed
milliseconds, not an assumed exact 100 ms. Once each wheel's CPR is measured:

```text
left_rev_s  = left_cps / left_CPR
right_rev_s = right_cps / right_CPR
control_velocity_rev_s = (left_rev_s + right_rev_s) / 2
RPM = rev_s * 60
```

No guessed CPR is baked into the firmware. The snapshot API exposes counts
and time so the future motor controller can sample at its own period.

## How it works and limits

`quadrature.h` describes the sequence 00 -> 01 -> 11 -> 10 -> 00. Each legal
edge adds +1; reverse edges add -1. A two-bit jump is ambiguous and adds an
invalid-transition diagnostic instead of guessing motion. The observed state
becomes the next baseline so counting can resume. Repeated states add zero.

`encoder.c` reads the whole GPIO port in one operation, updates both decoders,
and never prints from the ISR. Thread snapshots briefly mask interrupts on this
single-core MCU to read the 64-bit counters consistently. A GPIO read error is
recorded; the next successful read establishes a new baseline.

This is a bring-up implementation: sufficiently fast transitions can outrun GPIO
interrupt handling. Error counters detect some missed edges, but cannot detect
every lost full cycle. Validate at full motor speed before using it for control;
STM32 timer encoder mode is a possible later improvement. Keep UART byte tracing
and other diagnostic output modest when measuring timing. The current 100 ms
print interval is not a chosen PID control-loop period.

Host decoder test (does not exercise real GPIO interrupts or electrical wiring):

```sh
cc -std=c11 -Wall -Wextra -Werror -Iinclude tests/test_quadrature.c -o /tmp/lab2-test-quadrature
/tmp/lab2-test-quadrature
```

GPIO API reference: https://docs.zephyrproject.org/latest/doxygen/html/group__gpio__interface.html
