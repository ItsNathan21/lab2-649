# Complete wiring guide

This guide matches the project's current STM32 Nucleo-F401RE pin assignments and
the wiring selected during bring-up. It documents the intended connections, not
an inspection of the physical assembly. Disconnect USB, motor power, and servo
power before changing wiring. Use the board's printed pin labels.

## 1. Each motor's six-pin connector

The motor contains two separate electrical systems:

- **Motor power + and -:** receive switched power from the L298N H-bridge.
- **Encoder supply, ground, A and B:** power the rotation sensor and report motion
  to the STM32. A/B are inputs to the STM32, not motor drive signals.

The positions below are **left to right in the motor diagram supplied by the
team**, looking at the connector as pictured. They are diagram positions, not
manufacturer pin numbers. Looking at the mating cable can mirror the order.
Colored markers in that diagram do not necessarily match actual wire colors.

| Diagram position / label | Left motor connection | Right motor connection |
| --- | --- | --- |
| 1: Motor power cable - (black marker) | H-bridge **OUT4** | H-bridge **OUT2** |
| 2: Sensor signal negative (cyan marker) | Common **GND** | Common **GND** |
| 3: Sensor B phase (blue marker) | STM32 **PC1** | STM32 **PC3** |
| 4: Sensor A phase (purple marker) | STM32 **PC0** | STM32 **PC2** |
| 5: Sensor supply 5V/3.3V (yellow marker) | Nucleo **3.3V** | Nucleo **3.3V** |
| 6: Motor power cable + (red marker) | Left ACS712 **IP-** | Right ACS712 **IP-** |

Use 3.3 V for the encoder supply because the supplied motor diagram explicitly
supports 5 V/3.3 V, and A/B connect to STM32 inputs. This choice applies to that
motor/encoder model. Do not use the motor's 12 V supply for the encoder.

**Motor power - is NOT the encoder ground.** Motor power - goes to OUT2 or OUT4;
encoder ground goes to common GND. Neither motor power lead goes directly to an
STM32 pin.

## 2. H-bridge controls and power

| L298N connection | Connect to | Purpose |
| --- | --- | --- |
| ENA | STM32 **PB4** | Right motor PWM enable |
| IN1 | STM32 **PC4** | Right motor direction/brake control |
| IN2 | STM32 **PC5** | Right motor direction/brake control |
| ENB | STM32 **PB5** | Left motor PWM enable |
| IN3 | STM32 **PC6** | Left motor direction/brake control |
| IN4 | STM32 **PC7** | Left motor direction/brake control |
| 12V / VS / Vmotor | **12 V adapter positive** | Motor power input |
| GND | **12 V adapter negative and common GND** | Power return and logic reference |
| 5V logic terminal | **Module-dependent; see below** | Logic supply / regulator output |

Remove the **ENA and ENB jumpers** so the STM32 can apply PWM. Add one **10 kohm
resistor from ENA to GND**, and one from **ENB to GND**. These two enable pull-downs
are separate from the six current-sensor divider resistors.

The **5V-EN regulator jumper** is different from ENA/ENB. On common L298N modules,
with the onboard regulator enabled, the 5V terminal is an output; with it disabled,
external regulated 5 V must supply the logic. The specific module/jumper arrangement
has not been identified. Confirm its labeling or schematic before powering up;
do not join that terminal to the Nucleo's 5V rail while its regulator is driving it.

IN pins are low-current commands. OUT terminals carry motor current. For either
motor, opposite IN levels select direction, equal IN levels brake while enabled,
and a LOW enable allows coasting. OUT terminals are switched outputs, not permanent
positive/ground rails.

## 3. OUT1-OUT4 and the motor current sensors

```text
OUT3 --> LEFT ACS712 IP+ --> IP- --> LEFT motor power +
OUT4 ----------------------------> LEFT motor power -

OUT1 --> RIGHT ACS712 IP+ --> IP- --> RIGHT motor power +
OUT2 -----------------------------> RIGHT motor power -
```

Each sensor is inserted **in series in one motor lead**. Its two current terminals
replace a section of wire. Never connect IP+ and IP- across a supply or across the
two H-bridge outputs: the low-resistance current path would short them.

Use the sensor's terminal labels rather than assuming a physical left/right order.
Swapping IP+ and IP- reverses the reported current sign. Preserve motor lead
orientation from the working setup if it differs from the nominal +/- assignment
above; swapping the motor leads reverses rotation and may disagree with PID feedback.
Use load-current-rated wiring and terminals for these paths, not signal breadboard
rails. The sensor's signal GND is separate from its IP terminals.

## 4. Steering servo and its current sensor

The project identifies the servo as **Hiwonder LD-1501MG**, specified for 6-8.4 V.
The selected bring-up supply is a regulated **6 V buck output**. Measure the output
with the servo disconnected before reconnecting it.

```text
12 V adapter + --> Buck IN+
12 V adapter - --> Buck IN-

Buck OUT+ --> SERVO ACS712 IP+ --> IP- --> Servo RED (power +)
Buck OUT- ------------------------------> Servo BLACK (ground)
STM32 PA1 / A1 -------------------------> Servo WHITE (signal)
```

Servo ground must also share STM32 GND. The red lead carries servo power; the white
lead carries only PWM control. Do not power the servo from the Nucleo or the 12 V
motor rail. No 10k divider resistors go in the servo power path.

If the servo buzzes continuously, jitters, or cannot move, turn off its supply and
check voltage under load, terminal connections, and linkage binding. Current
firmware allows 500-2500 us pulses; mechanical linkage may need narrower limits.

## 5. All three ACS712 signal connections

These small VCC/GND/OUT pins are separate from the high-current IP+/IP- terminals.

| Sensor | VCC | GND | OUT, through its own divider |
| --- | --- | --- | --- |
| Left motor ACS712 | Nucleo **+5V**, CN6 pin 5 | Common GND | **PA0 / A0**, CN8 pin 1 |
| Right motor ACS712 | Nucleo **+5V**, CN6 pin 5 | Common GND | **PA4 / A2**, CN8 pin 3 |
| Servo ACS712 | Nucleo **+5V**, CN6 pin 5 | Common GND | **PB0 / A3**, CN8 pin 4 |

Nucleo GND is available at CN6 pin 6 or 7. Use the USB-powered board's +5V output
for sensor electronics only. Do not tie an external positive supply to this rail.

Build this divider separately for EACH sensor using two **10 kohm resistors**,
preferably 1% tolerance:

```text
ACS712 OUT ---- 10k ----+---- its assigned STM32 ADC pin
                       |
                      10k
                       |
                  common GND
```

Three sensors need **six divider resistors total**. Each ADC wire connects to the
junction between its two resistors. Do not join the three junctions together.
Do not connect an undivided ACS712 output directly to the ADC.

At zero load current, nominal readings with 5 V sensor power are approximately
2.5 V at OUT and 1.25 V at the divider junction. Provide 100 nF VCC-to-GND bypassing
at each module if it does not already include it. Keep analog wires short and away
from motor current/PWM wiring.

All three modules are confirmed 5A with nominal 92500 uV/A sensitivity after
the 10k/10k divider. Amperage conversion is enabled; zero offsets and the ADC
reference still require measurement. See [CURRENT_SENSOR_WIRING.md](CURRENT_SENSOR_WIRING.md)
for calibration settings and telemetry details.

## 6. Raspberry Pi and laptop

| Connection | Destination |
| --- | --- |
| Pi GPIO14 / TX, physical **pin 8** | STM32 **PB7 / USART1 RX** |
| Pi GPIO15 / RX, physical **pin 10** | STM32 **PB6 / USART1 TX** |
| Pi GND, e.g. physical **pin 6** | Common **GND** |
| Steering wheel USB | Laptop |
| Laptop command stream | Pi over network, UDP port **8000** |

TX and RX cross. Use the Pi's own USB-C supply; do not power the Pi from the Nucleo.
Nucleo ST-LINK USB provides programming and the console; its USART2 uses PA2/PA3
internally. No extra console UART wires are needed for the standard USB connection.

## 7. Blinkers

| Group | STM32 control pin |
| --- | --- |
| Left front and rear | **PC8**, active high |
| Right front and rear | **PC10**, active high |

Each LED needs its own current-limiting resistor. Retain the existing transistor
or other output-driver circuit if fitted; share ground, not the left/right control
outputs. The repository does not identify LED ratings, resistor values, or the
physical driver circuit, so those details remain to be confirmed before rebuilding
the LED wiring. Do not assume a GPIO can directly power an arbitrary LED group.

## 8. Power and common ground summary

| Rail | Supplies |
| --- | --- |
| 12 V adapter | H-bridge motor supply and buck input |
| Buck regulated 6 V output | Servo, through its ACS712 current path |
| Nucleo USB supply | Nucleo |
| Nucleo +5V output | Three ACS712 electronic VCC inputs |
| Nucleo 3.3V output | Two encoder supply inputs |
| Pi's own USB-C supply | Pi |

Join Nucleo GND, Pi GND, H-bridge GND, adapter negative, buck/servo ground, encoder
grounds, ACS712 signal grounds, divider bottoms, and blinker driver grounds.
Keep high motor/servo return currents out of thin STM32/sensor ground jumpers.
**Share ground; do not join different positive-voltage rails.**

## 9. Pin conflict check and remaining confirmations

The assigned ADC inputs PA0, PA4 and PB0 do not overlap motor controls, encoders,
blinkers, servo PWM, Pi UART, console UART, or SWD debug pins in the current build.

- **A0 = PA0**: left current sensor.
- **A1 = PA1**: servo PWM, already occupied.
- **A2 = PA4**: right current sensor; this is NOT PA2.
- **A3 = PB0**: servo current sensor; this is NOT PA3.
- **A4 = PC1, A5 = PC0** in the board's default mapping: left encoder, occupied.
- PA2/PA3: ST-LINK console; PA13/PA14: debugger. Leave these alone.
- PA5 is already assigned to TIM2 channel 1 in the board PWM configuration and
  also connects to the onboard LED; it is not a new sensor input.

Arduino-header and Morpho-header connections with the same MCU pin name are the
same electrical signal, not independent pins.

Before applying power after rewiring, confirm the L298N logic-regulator/jumper
configuration and measure the buck and ADC-divider voltages. Exact LED circuitry remains unresolved. Physical operation, servo travel limits,
and current calibration still require bench testing.

## References

- [Board overlay](boards/nucleo_f401re.overlay): application GPIO, PWM and ADC assignments.
- Team-supplied six-pin motor diagram: motor/encoder labels and 5V/3.3V encoder supply.
- [ST Nucleo-64 manual, UM1724](https://www.st.com/resource/en/user_manual/dm00105823.pdf): NUCLEO-F401RE header mappings.
- [Allegro ACS712 datasheet](https://www.allegromicro.com/-/media/files/datasheets/acs712-datasheet.pdf): sensor supply, output and current-path characteristics.
- [Hiwonder chassis specifications](https://www.hiwonder.com/products/ackermann-steering-chassis): LD-1501MG servo voltage rating.
