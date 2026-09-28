# Lab 2: throttle-controlled motors

Wheel GUI -> UDP port 8000 -> Raspberry Pi UART -> STM32 USART1 -> both motors.
Throttle requests wheel speed, and independent encoder-feedback PID loops adjust
PWM for each motor. After a 2% release deadband, throttle maps to 0..120 wheel RPM.
**120 RPM is an unmeasured initial limit**, editable in `include/motor_controller.h`.
Brake travel proportionally lowers the request; full brake or released throttle
commands coast and clears integral buildup. Reverse drive and latched electrical
braking are not used by the speed controller.

Targets initially rise at 120 RPM/s and decrease immediately. A 50% feedforward
offset retains the observed starting effort, but it is not a PWM minimum: feedback
can reduce PWM all the way to zero. The PID adds or removes duty as measured speed
changes under load, up to the configured PWM limit. It cannot maintain speed if
the motor lacks sufficient torque or supply power. Steering and non-blinker
buttons remain unused. PWM remains 10 kHz; the controller runs every 20 ms.

No Pi or packet-format change is needed.

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
gcc -std=c11 -Wall -Wextra -O2 receiver.c -o proxy_receiver
hostname -I
./proxy_receiver /dev/serial0
```

The Pi UART must be enabled with its serial login console disabled.
`/dev/serial0` currently resolves to `/dev/ttyS0` on this Pi; the program prints
the resolved device at startup. Use only one forwarder at a time.

Configure the laptop wheel GUI to send to the Pi's reachable IP from `hostname -I`,
UDP port **8000**, then start sending. Start with the throttle released and wheels
raised. The STM32 must be running before the Pi starts forwarding bytes.

The Pi prints each decoded UDP packet and the eight bytes written to UART.
The STM32 prints lines such as:

```text
Throttle=32767 brake=32767 request=0 mRPM (both motors)
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

Left motor connects to **OUT1/OUT2**, right motor to **OUT3/OUT4**.
Remove **ENA/ENB jumpers** when using PWM; use 10 kohm enable pull-downs to ground.
Power the motor bridge from its motor supply, the Nucleo through USB, and the Pi
from its own supply. The module's regulator jumper is separate from ENA/ENB;
follow its supply requirements. UART and encoder signals must be 3.3 V compatible.

USART1 uses 115200 baud, 8N1, no flow control. Debug prints use USART2 through
ST-LINK USB. UART carries exactly eight little-endian bytes: signed 16-bit
steering, throttle, brake, then an unsigned 16-bit button mask (bits 0-5, 8, 9).

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

## Blinkers

Press left blinker (button 5) or right blinker (button 4) once to enable that side;
press again to disable it. Holding a button does not repeatedly toggle it.
Both sides can blink independently. Each starts on, then alternates 250 ms on and
250 ms off (2 Hz, 50% duty). Change `BLINKER_RATE_HZ` in `include/blinker.h` to
adjust the rate; use a value that divides 500 for whole-millisecond half-periods.

PC8 drives the left LED group and PC10 the right, active high. Keep these output
pins separate; share ground, not the outputs. Use suitable LED current limiting
and a transistor driver if a group exceeds the GPIO current rating.
UART sends only enable/disable requests to the module. A UART control fault or
timeout turns both blinkers off along with stopping motor control.

## Stopping and restarting

Released throttle commands coast. After the first command, 150 ms without a fresh
complete packet coasts both motors and disables reception until reset. Detected
UART errors, queue overflow, or invalid button bits also stop control.
The PID also coasts both motors and latches off on an encoder read fault, excessive
invalid transitions, a control timing gap, sustained reverse feedback, or a stall.
Initially, stall detection means at least 5 RPM demand, at least 50% duty, and less
than 2 RPM measured speed for 1.5 seconds. These are tuning defaults, not current
or thermal protection. A disconnected encoder can trigger the same stop.
To restart: stop the Pi forwarder, reset STM32, then restart forwarding.
The packet format has no sync marker or checksum; it cannot detect every corrupt
or misaligned packet. Releasing throttle is the normal stop for this iteration;
full brake also commands coast.

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
- `src/main.c`: initialize drivers and start UART motor control and encoder monitoring.
- `src/uart_receiver.c`: UART reception, speed requests, blinkers, and link timeout.
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
