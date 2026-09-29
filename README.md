# Lab 2: throttle-controlled motors

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
gcc -std=c11 -Wall -Wextra -O2 -I../../include receiver.c ../../src/uart_protocol.c -o proxy_receiver
hostname -I
./proxy_receiver /dev/serial0
```

The Pi UART must be enabled with its serial login console disabled.
`/dev/serial0` currently resolves to `/dev/ttyS0` on this Pi; the program prints
the resolved device at startup. Use only one forwarder at a time.

Configure the laptop wheel GUI to send to the Pi's reachable IP from `hostname -I`,
UDP port **8000**, then start sending. Start with the throttle released and wheels
raised. Either endpoint may start first; drive stays braked until both links and wheel input are fresh.

The Pi prints each decoded UDP packet and a heartbeat/fault summary twice per second.
Diagnostic output is nonblocking, so a stalled terminal can drop log lines without
stalling UART traffic.
The STM32 prints lines such as:

```text
PID target=... mRPM L=... mRPM duty=.../1000 R=... mRPM duty=.../1000
ENC L=... (... cps, ... mRPM) R=... (... cps, ... mRPM) invalid=0/0 read_errors=0
```

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
| Left blinker output | PC8 |
| Right blinker output | PC10 |
| Steering servo signal | PA1 / TIM2 channel 2 |

Left motor connects to **OUT1/OUT2**, right motor to **OUT3/OUT4**.
Remove **ENA/ENB jumpers** when using PWM; use 10 kohm enable pull-downs to ground.
Power the motor bridge from its motor supply, the Nucleo through USB, and the Pi
from its own supply. The module's regulator jumper is separate from ENA/ENB;
follow its supply requirements. UART and encoder signals must be 3.3 V compatible.

USART1 uses 115200 baud, 8N1, no flow control. Debug prints use USART2 through
ST-LINK USB. Both directions use 16-byte frames: sync A5 5A, type, flags,
little-endian 16-bit sequence, eight payload bytes, then little-endian CRC-16/CCITT-FALSE
over the first 14 bytes. Command type is 0x11; status type is 0x12.
The command payload remains signed 16-bit steering, throttle, brake, then an unsigned
16-bit button mask (bits 0-5, 8, 9), all little-endian. Status payload is reserved/zero.
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

| Wheel input | Default pulse |
| --- | --- |
| -32768 (left) | 500 us |
| 0 (center) | 1500 us |
| 32767 (right) | 2500 us |

Edit pulse limits and `SERVO_REVERSED` in `include/servo.h` to match linkage travel.
The repetition period is set to 20 ms (50 Hz) in the overlay's `steering_servo.pwms`.
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

## Blinkers

Press left blinker (button 5) or right blinker (button 4) once to enable that side;
press again to disable it. Holding a button does not repeatedly toggle it.
Both sides can blink independently. Each starts on, then alternates 500 ms on and
500 ms off (1 Hz, 50% duty). Change `BLINKER_RATE_HZ` in `include/blinker.h` to
adjust the rate; use a value that divides 500 for whole-millisecond half-periods.

PC8 drives the left LED group and PC10 the right, active high. Keep these output
pins separate; share ground, not the outputs. Use suitable LED current limiting
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
within the driver's rating; software has no current measurement.

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
