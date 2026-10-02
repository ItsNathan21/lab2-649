# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Zephyr RTOS firmware for an STM32 Nucleo-F401RE driving a small car (18-449/649 Lab 2), plus a
Raspberry Pi UDP-to-UART forwarder. Data path: wheel GUI (laptop) → UDP :8000 → Pi
(`pi/proxy_receiver`) → framed UART → STM32 USART1 → two L298 motors (speed PID with encoder
feedback), steering servo, and blinker LEDs. `README.md` is the authoritative operator guide
(wiring table, protocol details, PID tuning procedure, fault behavior); read it before changing
behavior. `HOW_TO_CODE.md` holds mandatory coding rules (summarized below). `RUNNING.md` is the
step-by-step run guide (Mac/WSL/Pi/wheel GUI); `WIRING.md` and `CURRENT_SENSOR_WIRING.md` are
the physical wiring references and must stay consistent with the overlay.

## Build / flash / run

Most verification happens on hardware. The only automated test is a host-side C test of the
portable code (current conversion + UART framing), run from the repo root:

```bash
cc -std=c11 -Wall -Wextra -Werror -Iinclude tests/current_test.c \
  src/current_conversion.c src/uart_protocol.c -o /tmp/lab2-current-test && /tmp/lab2-current-test
```

```bash
source ./setup.sh                      # activates ~/zephyrproject venv + zephyr-env.sh
west build -p always -b nucleo_f401re/stm32f401xe -d build .   # pristine build
west build -d build                    # incremental rebuild after source-only changes
west flash -d build --runner openocd --verify
python -m serial.tools.miniterm /dev/ttyACM0 115200   # USART2 debug console via ST-LINK
west debug -d build --runner openocd   # GDB (disconnect motor power first; PWM keeps running)
```

In WSL the ST-LINK must first be attached with `usbipd attach --wsl --busid <id>` from Windows.
The README's paths say `~/zephyrproject/zephyr/lab2`; this checkout lives elsewhere, so run
from the repo root.

Host tools: `python tools/record_current.py` (venv active, miniterm closed) reads the STM32
console; `s` toggles recording and prints per-sensor current averages, saving CSVs to
`recordings/` (gitignored). It parses the PID/CURRENT console formats, so update its regexes if
those printk formats change.

Pi forwarder (built on the Pi, shares `src/uart_protocol.c` with the firmware):

```bash
cd pi/proxy_receiver
gcc -std=c11 -Wall -Wextra -O2 -I../../include receiver.c ../../src/uart_protocol.c -o proxy_receiver
./proxy_receiver /dev/serial0
```

## Architecture

- **Devicetree drives hardware config.** `CMakeLists.txt` forces `boards/nucleo_f401re.overlay`,
  which defines custom nodes (`lab,l298-motors`, `lab,quadrature-encoders`, `lab,steering-servo`,
  bindings in `dts/bindings/`) plus `gpio-leds` blinkers and three ADC1 current-sensor channels
  exposed via `zephyr,user` `io-channels`. Motors use TIM3 @10 kHz; the servo is deliberately on
  TIM2 @50 Hz so the two periods cannot interfere. Pin changes go in the overlay.
- **Motor index ≠ overlay index.** Physical left motor is bridge B (overlay `motors` index 1:
  ENB/IN3/IN4/OUT3-4); right is bridge A (index 0). `src/motor.c` maps `MOTOR_LEFT`/`MOTOR_RIGHT`
  explicitly with designated initializers; keep that mapping when touching motor arrays.
- **Worker threads, each owned by its module** and started from `main.c` in dependency order
  (motors → encoders → blinker → servo → PID → UART receiver → UART status → current sensor →
  encoder monitor). Priorities (lower = higher): `uart_status` 2 (absolute-schedule 20 ms
  heartbeat), `uart_receiver` 3 and `blinker` 3, `motor_controller` 4, `current_sensor` 5,
  `encoder_monitor` 6. Priority/stack macros live in each module's header. A current-sensor
  start failure is non-fatal (telemetry just reports unavailable).
- **Command flow.** USART1 ISR → private queue → `uart_receiver` worker, which validates frames,
  arbitrates faults/self-test (Y button), and fans out: `pedal_control` (pure throttle/brake →
  target mRPM + brake duty) → `motor_controller_set_target()`; steering → `servo_set_steering()`;
  buttons → blinker API. `motor_controller` is the sole owner of motor outputs.
- **Safety layering.** 60 ms UART command timeout (checked every 5 ms) and an independent PID
  command watchdog both brake. Link/source faults auto-recover on fresh healthy frames
  (`motor_controller_inhibit`); encoder/PID/driver faults latch until reset
  (`motor_controller_stop`). Faults drive full electrical braking + 2 Hz hazards.
- **Shared protocol.** `uart_protocol.c/.h` is portable C compiled into both the firmware and the
  Pi program: 16-byte frames (A5 5A sync, type, flags, LE seq, 8-byte payload, CRC-16/CCITT-FALSE).
  Any protocol change must update both endpoints together. `wheel_info.h` defines the command
  payload layout and measured wheel constants (1320 encoder counts/rev). The status payload is
  versioned current telemetry (3× int16 mA + validity mask + version byte) via
  `uart_current_encode/decode`; a clear validity bit means unavailable, never 0 A.
- **Current sensing is read-only.** `current_sensor` samples 3 ACS712s (through 10k/10k dividers)
  every 10 ms, averages 4 scans, and publishes a spinlock-protected snapshot that `uart_status`
  copies without blocking. Nothing in control/fault logic reads it. `current_conversion.c` is
  kept Zephyr-free so the host test can compile it.
- **Console output.** Periodic diagnostics (PID, ENC, CURRENT) go through
  `console_status_print()`, which redraws pinned top rows with ANSI escapes and never blocks the
  caller (skips a redraw if the console is busy). One-off events use plain `printk` and scroll
  below. `CONSOLE_STATUS_INPLACE 0` restores plain scrolling lines.
- **Fixed-point everywhere.** PID math uses millirpm and duty-permille (0..1000); gains are
  real gain × 1000. All tunables are macros in `include/motor_controller.h`, `pedal_control.h`,
  `servo.h`, `blinker.h`, `current_sensor.h` (nominal, uncalibrated zero/sensitivity values). Motor direction is `polarity[]` in `src/motor.c`; encoder direction is
  `direction[]` in `src/encoder.c`; PID feedback sign is separate (`MOTOR_PID_*_ENCODER_SIGN`).

## Coding rules (from HOW_TO_CODE.md)

- One `.c`/`.h` pair per module; all state, handles, stacks, kernel objects, and helpers are
  `static` in the `.c`. No `extern` variables — expose functions only. Headers hold public
  declarations, types, and constants and include what they need.
- `main.c` only initializes and starts workers; workers own their loops.
- Header guards: `FILENAME_H_` with `#endif /* FILENAME_H_ */`.
- Doxygen: `@file` + `@brief` per file; every function (including static helpers and thread
  entries) gets `@brief`, one `@param` per parameter, and `@return` when non-void.
- Every non-guard `#define` gets a ~one-sentence doc explaining the value's rationale; label
  unmeasured/initial values as such — never present an untuned value as proven.
- Tabs, `snake_case`, function braces on their own line, braces on all control bodies,
  ~100-column lines, one operation per line.
- New `.c` files must be added to `target_sources` in `CMakeLists.txt`.
- When behavior, pins, or tunables change, keep `README.md`, the wiring docs, and the layout
  list in `HOW_TO_CODE.md` in sync.
