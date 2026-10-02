# Lab 2: throttle-controlled motors

**All 13 checkoff probe rows:** [TEST_POINTS.md](TEST_POINTS.md) has the wiring and scope guide.

**Run instructions (macOS and Windows):** [RUNNING.md](RUNNING.md) covers setup, build/flash, the Pi forwarder, and the wheel GUI.

**Full wiring guide:** [WIRING.md](WIRING.md) covers motor connector pins, H-bridge, servo, sensors, power, and UART.

Wheel GUI -> UDP port 8000 -> Raspberry Pi UART -> STM32 USART1 -> both motors.
Throttle requests wheel speed, and independent encoder-feedback PID loops adjust
PWM for each motor. After a 2% release deadband, throttle maps to 0..120 wheel RPM.
**120 RPM is an unmeasured initial limit**, editable in `include/motor_controller.h`.
Brake travel applies proportional electrical braking and overrides throttle.
Braking ramps from zero after the released-end deadband to full duty at raw **20000**;
lower raw readings remain fully braked. Released throttle with brake released coasts.
Braking clears integral buildup. Faults and self-test instead apply full
electrical braking to both motors.

Targets initially rise at 120 RPM/s and decrease immediately. A 50% feedforward
offset retains the observed starting effort, but it is not a PWM minimum: feedback
can reduce PWM all the way to zero. The PID adds or removes duty as measured speed
changes under load, up to the configured PWM limit. It cannot maintain speed if
the motor lacks sufficient torque or supply power. Steering controls the servo; Y controls self-test. PWM remains 10 kHz; the controller runs every 20 ms.

**Update both the Pi receiver and STM firmware together: the framed UART protocol
is incompatible with the old raw eight-byte sender.** The wheel GUI UDP format is unchanged.

## 1. Attach ST-LINK to WSL

Close STM32CubeProgrammer and any existing debugger. In Windows PowerShell:

```powershell
usbipd list
usbipd attach --wsl --busid <ST-LINK-BUSID>
```

Use the ID from the list. If the device is `Not shared`, first run
`usbipd bind --busid <ST-LINK-BUSID>` in Administrator PowerShell.
Skip attachment if it is already `Attached`.

## 2. Build, flash, and open the STM32 console

Stop the Pi forwarder before resetting/flashing. In WSL:

```bash
cd ~/zephyrproject/zephyr/lab2
source ./setup.sh
west build -p always -b nucleo_f401re/stm32f401xe -d build .
west flash -d build --runner openocd --verify
python -m serial.tools.miniterm /dev/ttyACM0 115200
```

For subsequent source-only changes, use `west build -d build`, then flash again.
CMake selects `boards/nucleo_f401re.overlay`; the build should report that overlay.
If the console device differs, check `ls /dev/ttyACM*`. Exit miniterm with Ctrl+].

## 3. Run the Pi forwarder and wheel GUI

On the Pi, from this repository's `pi/proxy_receiver` directory:

```bash
gcc -std=c11 -Wall -Wextra -O2 -I../../include receiver.c trace_gpio.c ../../src/uart_protocol.c -o proxy_receiver
hostname -I
./proxy_receiver /dev/serial0
```

For scope timing on a Pi 4B, run `./proxy_receiver /dev/serial0 --trace-gpio`.
This claims GPIO23 (physical pin 16) for `UDP_RX` and GPIO24 (physical pin 18)
for `CMD_TX`; each event toggles its line, so use both rising and falling edges.
Connect the analyser ground to Pi GND (e.g. physical pin 6).
UART data remains on GPIO14/15 (physical pins 8/10).
The markers require Linux GPIO v2 headers/kernel (5.10+) and access to
`/dev/gpiochip0`; Raspberry Pi OS normally grants this through the `gpio` group.
If needed, add your user with `sudo usermod -aG gpio "$USER"` and log in again.
Pin and chip settings are in `pi/proxy_receiver/trace_gpio.h`; reserve these pins
for measurement. No external GPIO library is required.

`UDP_RX` marks return from each 276-byte UDP receive, even if later rejected as
stale or invalid. `CMD_TX` marks completion of a whole command's writes to the
kernel UART queue (every 20 ms), not electrical transmission completion.
Userspace scheduling and GPIO ioctl overhead affect these timestamps.
For exact serial timing, probe GPIO14 directly. Markers are disabled unless
`--trace-gpio` is supplied and are released on normal exit.

STM32 `CMD_RX` is PC9 (complete CRC-valid command before queueing), and `PWM_SET`
is PC11 (successful right-motor timer write). Pair those with PB4 (`PWM_OUT`, ENA)
and PC4 (`DIR_A`, IN1). The probe-board motor signals consistently use the right wheel.
Markers toggle on both rising and falling edges. See [TEST_POINTS.md](TEST_POINTS.md).

The Pi UART must be enabled with its serial login console disabled.
`/dev/serial0` currently resolves to `/dev/ttyS0` on this Pi; the program prints
the resolved device at startup. Use only one forwarder at a time.

Configure the laptop wheel GUI to send to the Pi's reachable IP from `hostname -I`,
UDP port **8000**, then start sending. Start with the throttle released and wheels
raised. Either endpoint may start first; drive stays braked until both links and wheel input are fresh.

The Pi prints each decoded UDP packet and a heartbeat/fault summary twice per second.
Diagnostic output is nonblocking, so a stalled terminal can drop log lines without
stalling UART traffic.
The STM32 pins these three diagnostic rows to the top of the terminal and redraws
them in place (ANSI escapes); fault and startup messages scroll beneath them:

```text
PID target=... mRPM L=... mRPM duty=.../1000 R=... mRPM duty=.../1000 brake=.../1000
ENC L=... (... cps, ... mRPM) R=... (... cps, ... mRPM) invalid=0/0 read_errors=0
CURRENT L/R/S raw=.../.../... mV=.../.../... mA=.../.../... sampled=0x07 valid=0x07
```

Rows wider than the terminal are cut off, so widen the window (about 110 columns).
Set `CONSOLE_STATUS_INPLACE` to 0 in `include/console_status.h` for plain scrolling lines.

Both encoders use 1320 decoded counts per wheel revolution; 1000 mRPM = 1 RPM.

## Connections

Use STM32 GPIO labels, not similarly named Arduino RX/TX labels.

| Signal | STM32 connection |
| --- | --- |
| Pi TX (GPIO14, physical pin 8) | PB7 / USART1 RX |
| Pi RX (GPIO15, physical pin 10) | PB6 / USART1 TX |
| Pi, bridge, encoder grounds | Common GND |
| L298 ENA / IN1 / IN2 | PB4 / PC4 / PC5 |
| L298 ENB / IN3 / IN4 | PB5 / PC6 / PC7 |
| Left encoder A / B | PC0 / PC1 |
| Right encoder A / B | PC2 / PC3 |
| Front-left / rear-left blinker | PC8 / PC12 |
| Front-right / rear-right blinker | PC10 / PD2 |
| CMD_RX / PWM_SET markers | PC9 / PC11 |
| Steering servo signal | PA1 / TIM2 channel 2 |

Left motor connects to **OUT3/OUT4** (ENB / IN3 / IN4);
right motor connects to **OUT1/OUT2** (ENA / IN1 / IN2).
The motor driver maps each PID output to that physical wheel's bridge.
Encoder channels remain left PC0/PC1 and right PC2/PC3.
Remove **ENA/ENB jumpers** when using PWM; use 10 kohm enable pull-downs to ground.
Power the motor bridge from its motor supply, the Nucleo through USB, and the Pi
from its own supply. The module's regulator jumper is separate from ENA/ENB;
follow its supply requirements. UART and encoder signals must be 3.3 V compatible.

USART1 uses 115200 baud, 8N1, no flow control. Debug prints use USART2 through
ST-LINK USB. Both directions use 16-byte frames: sync A5 5A, type, flags,
little-endian 16-bit sequence, eight payload bytes, then little-endian CRC-16/CCITT-FALSE
over the first 14 bytes. Command type is 0x11; status type is 0x12.
The command payload remains signed 16-bit steering, throttle, brake, then an unsigned
16-bit button mask (bits 0-5, 8, 9), all little-endian. Status payload carries three signed current readings in mA, a validity mask, and a payload version (see Current sensors below).
Command flags: bit 0 = fresh wheel input; bit 1 = fresh STM heartbeat.
Status flags: 0x01 link timeout, 0x02 stale source/return link, 0x04 self-test,
0x08 hardware/PID fault. Zero means normal. Sequences wrap modulo 65536;
duplicates/backward frames cannot refresh a live connection. After timeout, a new
sequence baseline permits endpoint restart. CRC and sliding synchronization reject
corrupt frames and recover alignment without resetting.

## PID tuning

Edit `include/motor_controller.h`, then rebuild and flash yourself. All controller
math is fixed-point; no floating-point configuration is needed.

| Setting | Meaning |
| --- | --- |
| `MOTOR_PID_MAX_RPM` | Full-throttle wheel RPM; initial **120 is unmeasured** |
| `MOTOR_PID_LEFT_KP_MILLI` / `RIGHT_KP_MILLI` | Proportional gain x1000; initial 4000 means 4 duty-permille per RPM error |
| `MOTOR_PID_LEFT_KI_MILLI` / `RIGHT_KI_MILLI` | Integral gain x1000; initial 800 means 0.8 duty-permille per RPM-second |
| `MOTOR_PID_LEFT_KD_MILLI` / `RIGHT_KD_MILLI` | Derivative gain x1000; initial 20 means 0.02 duty-permille per RPM/second |
| `MOTOR_PID_MAX_DUTY_PERMILLE` | PWM ceiling, 0..1000 scale; initial 1000 means 100% |
| `MOTOR_PID_FF_START_PERMILLE` / `FF_SPAN_PERMILLE` | Estimated baseline duty: initially 500 + 300 * target/max-speed |
| `MOTOR_PID_ACCEL_RPM_PER_SEC` | Upward speed-target ramp; initial 120 RPM/s |
| `MOTOR_PID_SPEED_FILTER_MS` / `D_FILTER_MS` | Speed and derivative smoothing time constants |
| `MOTOR_PID_I_LIMIT_PERMILLE` | Integral contribution limit; initial +/-400 means +/-40% duty |
| `MOTOR_PID_LEFT_ENCODER_SIGN` / `RIGHT_ENCODER_SIGN` | Use +1 or -1 so forward rotation gives positive feedback |
| `MOTOR_PID_STALL_*` | Stall speed, demand, duty, and time thresholds |

The control law is `duty = feedforward + Kp*error + integral - Kd*d(measured RPM)/dt`.
Each wheel has its own integral and derivative state. Conditional integration and
an integral clamp prevent windup at the output limits. Derivative acts on filtered
measurement rather than target changes. A zero target clears integral/drive state;
a decreasing target clears the old integral and skips the upward acceleration ramp.

1. Verify forward encoder counts/signs and the 1320 counts/revolution calibration.
2. Set a realistic maximum speed from measurements; lower the PWM ceiling during tuning.
3. Set Ki and Kd to zero. Tune feedforward and Kp first, using the PID console lines.
4. Add Ki gradually to remove steady error under load. Add Kd only as needed to damp oscillation.
5. Tune each motor independently; adjust filters and stall limits based on measurements.

The left PID feedback sign is -1 and the right is +1, matching observed counts
under forward drive. Raw `ENC` logs still show negative left counts; normalized
`PID` speed should be positive. This changes feedback interpretation, not motor direction.

The initial gains are placeholders, not hardware-tuned values. The console uses
millirpm (`60000 mRPM = 60 RPM`) and duty-permille (`500/1000 = 50%`).

## Steering servo

Connect the servo signal to **PA1**, with servo supply ground connected to STM32 GND.
Use a supply suited to the servo's rating; do not power the servo from a GPIO.
The servo is a Hiwonder LD-1501MG positional servo. Its specified pulse range is
500..2500 us for 0..180 degrees. Check full travel with the linkage disconnected;
the chassis can require narrower limits than the servo itself.

The module starts centered and maps wheel input as follows:

| Wheel input | Pulse |
| --- | --- |
| -32768 (left) | 910 us |
| 0 (center) | 1500 us |
| 32767 (right) | 2100 us |

The limits were narrowed on 10/01 from the servo's 500..2500 us: buzzing against the
linkage stops began at about 883 us (left) and 2132 us (right), so each limit keeps
about 30 us of margin. Full wheel lock still maps exactly to each limit.

Edit pulse limits and `SERVO_REVERSED` in `include/servo.h` to match linkage travel.
The repetition period is set to 20 ms (50 Hz) in the overlay's `steering_servo.pwms`.
Initialization checks the period and active-high polarity; pulse limits must stay
within the model's 500..2500 us range. `SERVO_UPDATE_DEADBAND_US` initially ignores
changes smaller than 3 us relative to the last applied pulse to reduce command
jitter (set it to 0 to disable). Exact center/endpoints and recovery after a
disabled output apply immediately. There is no added smoothing delay. The
blinker angle remains an estimate from the requested pulse, within this deadband
of the applied command. This does not measure actual shaft position or correct
power-supply dips or mechanical binding.
TIM2 is separate from the motors' TIM3, so steering updates do not change motor PWM.
The Windows `proxy_gui.py` now requests and verifies 900 degrees of G920 operating
range on connect. Restart that GUI to apply it. The wheel has 450 degrees per side
from center; that entire input range maps monotonically to the configured servo
pulse range. The STM32 mapping was already monotonic and needs no firmware change
for this wheel-range correction. If steering raw values plateau at -32768/32767
before physical lock, correct the driver range/calibration; firmware cannot recover
position beyond a saturated input. If raw values continue changing but the servo
stops moving, check servo/linkage travel instead.

Hardware generates the pulses continuously; no extra servo thread is needed.
UART passes the latest validated steering value to `servo_set_steering()`. UART
fail-safe disables the signal rather than commanding a sudden recenter; behavior
without pulses depends on the servo. Fresh steering resumes after recovery.

## Current sensors (bring-up; calibration pending)

Lab 2 section 3.5 requires three sensors (left motor, right motor, servo), reported
in each status heartbeat. Readings are informational: they do not alter PID,
braking, hazards, or the fault state. The sensors are **ACS712**; the 5A/20A/30A
variant is still needed to select amperage scaling.

Use the complete [ACS712 wiring plan](CURRENT_SENSOR_WIRING.md). Assigned inputs:

| Sensor | Arduino header | STM32 pin | ADC1 channel |
| --- | --- | --- | --- |
| Left motor | A0 / CN8 pin 1 | PA0 | 0 |
| Right motor | A2 / CN8 pin 3 | PA4 | 4 |
| Servo | A3 / CN8 pin 4 | PB0 | 8 |

These avoid existing actuator, encoder, communication, and debug assignments.
Each sensor uses 5 V power and its own 10k/10k output divider; do not connect
an undivided ACS712 OUT to these ADC pins. The wiring document distinguishes
sensor signal wiring from the separate series current path through its terminals.

`current_sensor` runs at priority 5, below UART status (2), command handling (3),
and motor PID (4). It reads each input sequentially every 10 ms using 12-bit ADC
conversions, with maximum acquisition time as an initial source-impedance allowance.
It averages the latest four successful scans, a nominal 40 ms window with roughly
15 ms group delay once full. Read errors and ADC rail clipping reset that channel's
filter. Actual timing and PWM-noise rejection still need bench measurement.
The heartbeat copies a short, spinlock-protected snapshot and never waits for ADC
conversions. Data older than 100 ms is invalidated. Current sampling failure does
not stop actuator control or heartbeat transmission.

Calibration lives in `include/current_sensor.h`:

- `CURRENT_SENSOR_REFERENCE_UV`: nominal 3300000; replace with measured ADC reference.
- Each `*_ZERO_UV`: nominal 1250000 after the divider; replace with measured ADC-pin voltage at zero load current.
- Each `*_UV_PER_AMP`: signed voltage change at the ADC pin per amp, including any
  divider. **All three are set to nominal 92500 uV/A for confirmed 5A modules.**
  With the selected divider, nominal values are 92500 (5A), 50000 (20A), or
  33000 (30A) uV/A; check each module independently.

The conversion is `mA = (ADC_pin_uV - zero_uV) * 1000 / uV_per_A`.
Voltage uses `average_raw * reference_uV / 4096`; nominal voltage resolution is
about 806 uV/count. Current resolution is `reference_uV / 4096 / abs(uV_per_A)`
amps/count. A negative sensitivity supports reversed sensor orientation.
Do not automatically zero at boot: the motors/servo may already be drawing current.
Floating/disconnected analog inputs cannot be reliably identified by software;
validity means a fresh calibrated, unclipped conversion, not verified sensor presence.

The STM console prints raw counts, averaged mV, converted mA, and masks 10 times per
second, always ordered left/right/servo. `sampled=0x07` means all three ADC reads
succeeded. With the nominal 5A settings, `valid=0x07` is expected for fresh,
unclipped, representable readings; it does not certify measured calibration accuracy. **An mA value
without its validity bit is unavailable, not a measurement of zero current.**

The existing 16-byte status frame and CRC are unchanged. Its eight payload bytes are:

| Payload bytes | Meaning |
| --- | --- |
| 0..1 | Left motor signed int16 mA, little-endian |
| 2..3 | Right motor signed int16 mA, little-endian |
| 4..5 | Servo signed int16 mA, little-endian |
| 6 | Validity bits 0=left, 1=right, 2=servo; other bits zero |
| 7 | Current payload version = 1 |

Unavailable channels are encoded as zero with their bit clear. Uncalibrated,
failed, stale, clipped, or out-of-int16-range readings are unavailable. Fault flags
in the frame header retain their previous meaning. The updated Pi forwarder prints
`current L=... R=... S=...`, displaying `unavailable` for invalid readings or a stale
heartbeat. Legacy all-zero payloads are recognized as unavailable. Rebuild the Pi
forwarder with the shared `uart_protocol.c` to see the new diagnostics.

Before checkoff, confirm the sensor range and completed wiring, calibrate with measured
reference/zero/current values, and record rest, running, and brief-stall readings
for all three channels as the handout requests. None of those hardware measurements
has been performed by this change; current sensing is not ready for checkoff yet.

To average readings for a test condition, close miniterm and run
`python tools/record_current.py` from WSL with the venv active. Press **s** to start and
**s** again to stop: it prints each sensor's average, spread, min/max, and the PID state,
and saves the samples to `recordings/` as CSV. **q** quits. Readings arrive 10 times per second.

Portable regression tests (no board required):

```bash
cc -std=c11 -Wall -Wextra -Werror -Iinclude tests/current_test.c \
  src/current_conversion.c src/uart_protocol.c -o /tmp/lab2-current-test
/tmp/lab2-current-test
```

## Blinkers

Press left blinker (button 5) or right blinker (button 4) once to enable that side;
press again to disable it. Holding a button does not repeatedly toggle it.
Only one side blinks at a time: enabling one side cancels the other. Each starts on, then alternates 500 ms on and
500 ms off (1 Hz, 50% duty). Change `BLINKER_RATE_HZ` in `include/blinker.h` to
adjust the rate; use a value that divides 500 for whole-millisecond half-periods.

PC8/PC12 drive front-left/rear-left and PC10/PD2 front-right/rear-right, active high.
All four pins are separate electrical outputs; front/rear share a timing state per side.
Keep these output pins separate; share ground, not the outputs. Use suitable LED current limiting
and a transistor driver if a group exceeds the GPIO current rating.
The module owns toggle state. An enabled side arms auto-cancel when its estimated
servo angle exceeds `BLINKER_CANCEL_DEGREES` (initially 10 degrees from center on
that side), then turns off when the angle returns below that threshold. Equality
does not count as a crossing. This uses commanded PWM angle, not measured servo
feedback. Toggle again normally after cancellation.

During a fault, both GPIO banks flash together at **2 Hz, 50% duty**; this overrides
and clears normal selections. Configure `BLINKER_HAZARD_RATE_HZ` separately from
normal `BLINKER_RATE_HZ` in `include/blinker.h`.

## Stopping and restarting

Released throttle with the brake released coasts. Pressing the brake overrides drive:
both bridge inputs go low, and enable PWM controls the fraction of time spent braking.
At raw **20000 or lower**, both enables stay fully high for full dynamic braking.
Faults and self-test still request full braking regardless of pedal positions.

Tune `PEDAL_BRAKE_FULL_RAW` in `include/pedal_control.h` (initially 20000).
The existing 1311-count deadband ignores readings from 31456 through 32767.
Approximately 25728 gives 50% brake PWM; this is duty, not calibrated braking torque.
Releasing the brake resumes the current throttle request with the existing acceleration
ramp. Normal pedal braking does not activate hazards. The PID console now includes
`brake=.../1000`, where 1000 is full brake duty.

This uses the L298's [dynamic braking mode](https://www.st.com/resource/en/datasheet/l298.pdf):
it shorts the motor terminals through the bridge rather than applying reverse drive.
It does not provide powered holding torque at zero speed. Braking current must remain
within the driver's rating; current telemetry is read-only and does not impose current limits.

- Pi commands and STM status heartbeats run every **20 ms**, including during faults.
- **60 ms without a valid advancing command** (three missed updates) triggers fail-safe.
  The receiver checks every 5 ms; the PID also has an independent command watchdog.
- The Pi marks wheel input stale after **100 ms** without a valid advancing UDP packet
  (`PI_WHEEL_TIMEOUT_MS`). It marks the return link stale after three missed STM statuses.
  These unhealthy commands keep the connection alive but cannot drive actuators.
- Press **Y (button 3)** once to latch self-test immediately: brakes and hazards.
  Two distinct press edges within **500 ms**, with a release between, clear self-test.
  The first edge is never delayed to wait for a possible second edge. Once a pair
  completes, a third press starts a new single press.
- Reconnecting automatically clears link/source faults. Current throttle, brake,
  and steering are applied from a fresh healthy packet, with the existing PID
  acceleration ramp. **Pedal release is not required.** Commands received during a
  fault are not applied or saved for later replay; only Y edges remain actionable.
- A double press cannot override an active link or hardware fault. Self-test stays
  latched across reconnect until cleared by a double press.
- Encoder/PID/driver faults still require reset. They request brakes and hazards;
  a failed output driver may prevent physical braking and needs investigation.

The STM heartbeat has a dedicated priority-2 worker with an absolute schedule.
The 20 ms +/-10% heartbeat and under-100 ms brake/hazard response are timing targets
to verify on hardware, including under load; they have not been measured here.
Monitor PB6 on a scope for 18..22 ms frame spacing. Unplug/reconnect either UART
direction and check braking/hazards and recovery. Check Y single/double presses and
both auto-cancel directions. No build or flash is performed by these instructions.

If a wheel runs backward, change its `polarity[]` in `src/motor.c`, or swap its
OUT leads with power disconnected. Encoder direction is set separately by
`direction[]` in `src/encoder.c`.

## Debugging

Disconnect motor power before using breakpoints: hardware PWM can continue while
the CPU and software timeout are halted. Stop the Pi forwarder during reset/stepping.
In another WSL terminal:

```bash
cd ~/zephyrproject/zephyr/lab2
source ./setup.sh
west debug -d build --runner openocd
```

In GDB:

```gdb
break main
monitor reset halt
continue
```

`next` steps over, `step` steps into, `continue` resumes, Ctrl+C pauses, `quit` exits.
Continue past startup before restarting the Pi sender; prints appear in miniterm.

## Source map

- `src/current_sensor.c`, `include/current_sensor.h`: ADC sampling, calibration settings, and snapshots.
- `src/current_conversion.c`, `include/current_conversion.h`: portable voltage-to-current conversion.
- `tests/current_test.c`: calibration and current telemetry regression tests.
- `src/console_status.c`, `include/console_status.h`: pinned in-place PID/ENC/CURRENT console rows.
- `src/blinker.c`, `include/blinker.h`: GPIO blinker worker and timing configuration.
- `src/servo.c`, `include/servo.h`: steering PWM mapping and pulse calibration.
- `src/main.c`: initialize drivers and start UART motor control and encoder monitoring.
- `src/uart_receiver.c`: commands, self-test, fault arbitration, and automatic recovery.
- `src/uart_status.c`, `include/uart_status.h`: independent 20 ms status worker.
- `src/uart_protocol.c`, `include/uart_protocol.h`: framing shared by STM and Pi.
- `src/pedal_control.c`, `include/pedal_control.h`: pedal deadband and speed-request mapping.
- `src/motor_controller.c`, `include/motor_controller.h`: speed PID worker and all PID tuning.
- `src/encoder_monitor.c`: periodic encoder reporting.
- `src/quadrature.c`: x4 quadrature transition decoding.
- `HOW_TO_CODE.md`: module layout, encapsulation, and documentation rules.
- `src/motor.c`, `src/encoder.c`: motor output and encoder drivers.
- `include/wheel_info.h`: packet fields and measured wheel values.
- `boards/nucleo_f401re.overlay`: motor/encoder pins and PWM settings.
- `pi/proxy_receiver/receiver.c`: UDP-to-UART forwarder.

The supplied lab PDFs remain the assignment/reference documents.
