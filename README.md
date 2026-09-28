# Lab 2: throttle-controlled motors

Wheel GUI -> UDP port 8000 -> Raspberry Pi UART -> STM32 USART1 -> both motors.
Released throttle gives 0% duty. Beyond a 2% release deadband, throttle maps to
approximately 50..100% duty: the 50% starting floor matches the observed motor
start near raw throttle `0` with the previous mapping. Raw `0` now requests about
74.5% duty. Duty jumps to the starting floor, then rises from 50% to 100% in about
one second; reductions apply immediately.

Brake travel proportionally reduces the mapped duty on both motors, even while
throttle is pressed. Fully pressed brake commands zero duty (coast). This reduces
drive power; it does not engage the driver's latched electrical brake. Steering and non-blinker buttons remain unused. PWM is 10 kHz, rounded to the nearest timer tick;
this is duty control, not closed-loop speed or acceleration control.

Tune the starting floor, deadband, and ramp time in `include/pedal_control.h`.
The 2% deadband and one-second ramp are initial choices; the 50% threshold may
need adjustment for each motor and its load. No Pi or packet-format change is needed.

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
Throttle=32767 brake=32767 duty=0.00% (both motors)
Throttle=-32768 brake=32767 duty=100.00% (both motors)
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
- `src/uart_receiver.c`: UART reception, periodic duty updates, and link timeout.
- `src/pedal_control.c`, `include/pedal_control.h`: pedal mapping and acceleration tuning.
- `src/encoder_monitor.c`: periodic encoder reporting.
- `src/quadrature.c`: x4 quadrature transition decoding.
- `HOW_TO_CODE.md`: module layout, encapsulation, and documentation rules.
- `src/motor.c`, `src/encoder.c`: motor output and encoder drivers.
- `include/wheel_info.h`: packet fields and measured wheel values.
- `boards/nucleo_f401re.overlay`: motor/encoder pins and PWM settings.
- `pi/proxy_receiver/receiver.c`: UDP-to-UART forwarder.

The supplied lab PDFs remain the assignment/reference documents.
