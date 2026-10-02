# Lab 2 probe board setup

Based on the handout's Test points section (pages 10-11). Firmware instrumentation
is implemented; wiring, Pi GPIO operation, and timing still require hardware verification.

## Wire the mini breadboard

Power off before wiring. Use one connected five-hole strip (a-e) per numbered row,
on the same side of the center gap. Put the signal wire in hole a and leave b-e
free for probes. Label the rows on tape. Do not join different signal rows.
Both ground rows connect to the common Pi/STM32/driver ground.
Probe only logic signals here, never the bridge motor output terminals.

| Row | Tape label | Connect to |
| --- | --- | --- |
| 1 | GND | Common ground |
| 2 | GND | Common ground (a second probe-ground connection) |
| 3 | UDP_RX | Pi GPIO23, physical header pin 16 |
| 4 | CMD_TX | Pi GPIO24, physical header pin 18 |
| 5 | CMD_RX | STM32 PC9 |
| 6 | PWM_SET | STM32 PC11 |
| 7 | PWM_OUT | PB4, the existing ENA/right-motor PWM wire |
| 8 | DIR_A | PC4, the existing IN1/right-motor direction wire |
| 9 | SRV | PA1, the existing servo signal wire |
| 10 | FL | PC8, front-left blinker signal |
| 11 | FR | PC10, front-right blinker signal |
| 12 | RL | PC12, rear-left blinker signal |
| 13 | RR | PD2, rear-right blinker signal |

Rows 7-9 branch off existing signal wires: keep the actuators connected.
Rows 10-13 branch off the four separate blinker control wires described below.
PC9 and PC11 are new outputs. Use the STM32 port labels in
[nucleo_f401re_morpho.png](nucleo_f401re_morpho.png), viewed with USB at the top:
PC11 is at the top of CN7's inner column, PC9 at the top of CN10's inner column.
The overlay disables unused I2C3 because the board default assigns its SDA to PC9.

**Separate the rear blinkers:** keep front-left on PC8 and front-right on PC10.
Disconnect rear-left control from PC8 and reconnect it to PC12. Disconnect rear-right
control from PC10 and reconnect it to PD2. Each lamp needs its own suitable resistor
or driver channel; connect the GPIO to the driver input if you use transistor drivers.
Do not tie PC8 to PC12 or PC10 to PD2. Grounds remain shared. The code drives front/rear
on each side from the same timing state and prevents preemption between their writes;
measure the actual skew on the scope. Normal blinking, hazards, and auto-cancel all
apply to both lamps on the selected side.

With USB at the top, PC12 and PD2 are the second pair down on CN7, directly below
PC10 and PC11 in the [pinout image](nucleo_f401re_morpho.png).

## Build and run

### STM32

Use the environment setup in [RUNNING.md](RUNNING.md). From the application directory:

```sh
west build -b nucleo_f401re/stm32f401xe -d build-current .
west flash -d build-current --runner openocd --verify
```

Stop the Pi forwarder before flashing and keep the wheels raised. The Mac checkout
has been built; it has not been flashed by this setup task.

### Raspberry Pi

GPIO23 and GPIO24 must be unused. Use `gpioinfo` to identify the GPIO chip containing
header lines GPIO23/24; chip numbering varies across Pi models and OS versions.
Check the physical numbering with `pinout`. GPIO offsets 23/24 are not physical pins 23/24.

From `pi/proxy_receiver` on the Pi:

```sh
gcc -std=c11 -Wall -Wextra -Werror -O2 -I../../include \
  receiver.c trace_gpio.c ../../src/uart_protocol.c -o proxy_receiver
./proxy_receiver /dev/serial0 --trace-gpio
```

The Pi 4B default is `/dev/gpiochip0`; if the verified chip differs, edit
`TRACE_GPIO_CHIP` in `pi/proxy_receiver/trace_gpio.h` before building. The current user needs access to
the UART and GPIO chip (normally the dialout/gpio groups). The code uses the Linux
GPIO v2 API and requires matching Linux headers/kernel (5.10 or newer). No libgpiod
link dependency is needed; `gpioinfo` is only a setup tool. A busy pin, unsupported
GPIO API, or permissions error stops startup rather than silently omitting markers.

The optional `--trace-gpio` argument enables the Pi markers. Running only
`./proxy_receiver /dev/serial0` forwards commands without GPIO markers. Rebuild and restart the Pi program as well as flashing the STM32.

## What each marker means

These are toggles, not fixed-width pulses: **both rising and falling edges are events**.
A 20 ms event interval produces a 40 ms full square-wave period.

- UDP_RX toggles after each 276-byte wheel datagram receive, before validation and
  logging. Invalid values and duplicate sequences still make an edge, but wrong-sized
  datagrams do not. This marks userspace receipt, not arrival at the network hardware.
- CMD_TX toggles after all bytes of a command frame have been written to the kernel
  UART queue. It does not mean the last bit has left the UART. Userspace scheduling
  and GPIO ioctl overhead affect the marker's position relative to the physical wire.
- CMD_RX toggles in the STM32 UART ISR after a complete CRC-valid command frame,
  before queueing and worker validation. Rejected sequences/flags and queue-full cases
  still have an edge. Frames with a bad CRC do not.
- PWM_SET toggles immediately after each successful **right-motor** timer write.
  This includes startup, coast, brake, and writes of the same duty. A marker is not
  proof of a changed waveform: correlate it with PB4. Left-motor writes do not toggle
  this point, so the marker and PWM_OUT refer to the same channel.

## Scope measurements

Use high-impedance probes with ground clips on rows 1/2. The signals are 3.3 V logic;
start with DC coupling, a trigger near 1.5 V, and about 1 ms/div for motor response.
Capture a deliberate throttle or brake step after the link is healthy.

| Measurement | Channel 1 | Channel 2 | What to record |
| --- | --- | --- | --- |
| Pi forwarding | UDP_RX | CMD_TX | Receipt to completed UART queue write; commands run every 20 ms |
| UART path | CMD_TX | CMD_RX | Completed queue write to STM32 frame receipt; not an exact wire-time measurement |
| Throttle software | CMD_RX | PWM_SET | Command reception to the corresponding new right-motor duty write |
| Throttle hardware | PWM_SET | PWM_OUT | Timer write to actual changed duty; zoom to 20 us/div for 10 kHz PWM |
| Brake | CMD_RX | DIR_A | Command to PC4 falling from forward-drive high to brake low |
| Steering | CMD_RX | SRV | Command to changed servo pulse width; 50 Hz, 910-2100 us pulses |
| Blinkers | FL / FR | RL / RR | Period, duty, activation delay, and front/rear alignment |

The handout requires throttle/brake within **2 ms**, steering within **50 ms**, and
blinker activation within **100 ms**, with **1 Hz +/-10%** and **50% duty** in normal mode.
Measure blinkers over several periods. Fault hazards intentionally run at 2 Hz.

**Current timing risk:** the PID worker runs every 20 ms. A throttle update can wait
for its next cycle, so instrumentation does not establish compliance with 2 ms.
Periodic PWM writes can occur after CMD_RX without using that command. Use an isolated
input step, verify the actual duty change, and capture several phases of the worker cycle;
do not just report the nearest marker edge. The periodic Pi sender can also transmit
multiple commands for one UDP update. For brake timing, start from forward drive:
PC4 is already low when coasting from some states or already braking.

Record measured values and screenshots; no scope measurements have been made here.

References: [Linux GPIO v2 line requests](https://www.kernel.org/doc/html/latest/userspace-api/gpio/gpio-v2-get-line-ioctl.html),
[GPIO value writes](https://www.kernel.org/doc/html/latest/userspace-api/gpio/gpio-v2-line-set-values-ioctl.html),
and [Raspberry Pi GPIO documentation](https://www.raspberrypi.com/documentation/computers/raspberry-pi.html).
