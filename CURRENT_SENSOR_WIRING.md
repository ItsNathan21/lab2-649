# ACS712 wiring for this Nucleo-F401RE build

This plan assigns three ACS712 modules: left motor, right motor, and servo.
It assumes standard breakouts exposing VCC, GND, OUT and two current-path
terminals (IP+ / IP-). Follow each module's printed labels, not an assumed
left-to-right pin order. If a board adds an amplifier or output divider, its
transfer function must be checked before using the calibration below.

## Signal connections

| Module | VCC | Signal GND | OUT destination, through its own divider |
| --- | --- | --- | --- |
| Left motor | Nucleo +5V, CN6 pin 5 | Nucleo GND, CN6 pin 6 or 7 | A0 = PA0 = CN8 pin 1 = ADC1 channel 0 |
| Right motor | Same +5V rail | Same GND rail | A2 = PA4 = CN8 pin 3 = ADC1 channel 4 |
| Servo | Same +5V rail | Same GND rail | A3 = PB0 = CN8 pin 4 = ADC1 channel 8 |

Use the existing USB-powered Nucleo's +5V output for sensor electronics only.
The motor and servo keep their existing external power supplies. Do not connect
an external supply output onto this USB-derived +5V rail. Grounds of the existing
Nucleo, Pi, motor bridge and servo supply remain common.

For EACH sensor, use two **10 kohm, 1% resistors** (six resistors total):

```text
ACS712 OUT ---- 10k ----+---- selected ADC pin (A0, A2, or A3)
                       |
                      10k
                       |
                  common GND
```

Keep all three OUT/divider junctions separate. Put the divider near the Nucleo.
The selected divider gives ADC voltage = sensor OUT / 2. At zero current with
5.0 V sensor power, expect about 2.50 V on OUT and 1.25 V at the ADC junction.
Even 5.5 V on OUT divides to about 2.75 V, leaving margin below nominal 3.3 V.
The 20k resistive load is above the ACS712 datasheet's 4.7k minimum.
The ADC uses long acquisition time for the divider's approximately 5k source
resistance. Keep the analog wires short and away from motor/PWM wiring.

Provide 100 nF ceramic decoupling between VCC and GND close to each sensor if the
module does not already include it. The ACS712 is a 5 V device; powering it from
3.3 V is outside its specified operating supply range. Do not connect raw OUT
directly to the STM32 ADC. Power the Nucleo and sensor electronics together.

## Current-path connections (the screw terminals)

These go **in series with the load**, separately from VCC/GND/OUT above.
Make changes with USB and actuator supplies disconnected.

| Module | IP+ terminal | IP- terminal | Other load lead |
| --- | --- | --- | --- |
| Left motor | L298 OUT3 | Motor lead previously connected to OUT3 | Remains on OUT4 |
| Right motor | L298 OUT1 | Motor lead previously connected to OUT1 | Remains on OUT2 |
| Servo | Positive output of the existing servo-rated supply | Servo positive power lead | Servo ground stays on supply GND; signal stays on PA1 |

In other words, break one existing load wire and insert the sensor's two current
terminals into that wire. The motor sensors go in individual motor leads so they
measure winding current, including recirculation, rather than combined bridge
supply current. Keep the motor lead identities unchanged so rotation stays the
same. Positive current is IP+ to IP-; reverse/braking current may be negative.

**Never wire the two current terminals across a supply or across OUT1/OUT2 (or
OUT3/OUT4): the current path has very low resistance and would make a short.**
Do not connect either motor sensor's current terminal to signal ground. Use wire
and terminals rated for the actual load current, rather than routing motor current
through the signal breadboard. Sensor range does not establish the module's wire,
terminal, motor, or driver current rating.

## Pin conflict check

Checked against the application overlay, driver pin references, board definition,
and generated build configuration:

| Existing function | Pins kept |
| --- | --- |
| Left motor enable / direction | PB5 / PC6 / PC7 |
| Right motor enable / direction | PB4 / PC4 / PC5 |
| Left encoder A/B | PC0 / PC1 |
| Right encoder A/B | PC2 / PC3 |
| Left / right blinkers | PC8 / PC10 |
| Servo PWM | PA1 = A1 |
| Pi USART1 TX/RX | PB6 / PB7 |
| ST-LINK console USART2 TX/RX | PA2 / PA3 |
| SWD debugger | PA13 / PA14 |
| Existing TIM2 channel 1 / onboard LED pin | PA5 |
| New current ADC inputs | PA0 / PA4 / PB0 |

None of the three new inputs overlaps these assignments. A4/PC1 and A5/PC0 are
already left-encoder pins; A1/PA1 already drives the servo. Do not use those three
Arduino analog-header positions for current sensors. In particular, **A2 is PA4,
not PA2**, and **A3 is PB0, not PA3**. Morpho and Arduino labels referring to the
same MCU pin are the same electrical connection, not extra independent pins.
This checks the repository's configuration; physical wiring must match it.

## Calibration and first check

The ADC mapping is already configured in `boards/nucleo_f401re.overlay`.
`include/current_sensor.h` now uses nominal 1250000 uV zero offsets for this divider.
All three modules are confirmed 5A with 10k/10k dividers. Their sensitivities are
set to nominal 92500 uV/A; zero offsets and reference still require measurement.
A valid current bit means conversion is available, not that calibration accuracy
has been verified. Reference values for other variants:

| ACS712 marking | Nominal sensor sensitivity at 5 V | Set ADC-pin `*_UV_PER_AMP` with this divider |
| --- | --- | --- |
| 05B / 5A | 185 mV/A | 92500 |
| 20A | 100 mV/A | 50000 |
| 30A | 66 mV/A | 33000 |

These are nominal starting values, not measured calibration. Measure the actual
reference and each zero-current divider voltage. The ACS712 is ratiometric, so
supply voltage and resistor tolerances affect offset and gain. For a known
current, refine sensitivity as `(ADC_uV_loaded - ADC_uV_zero) / current_A`.
Leave the sensor electronics powered but the load-current path unpowered while
measuring zero; a powered servo can draw current even when stationary.

Before connecting the divider junctions to the ADC, measure their voltages
relative to Nucleo GND. With the load path unpowered, expect about 1.25 V. Then
connect them, build/flash, and check STM console raw counts and mV. Nominal zero
is around 1552 counts with the configured 3.3 V reference. Calibrate before
trusting amp readings. Sampling remains read-only; it does not limit current.

## Primary references

- [Allegro ACS712 datasheet](https://www.allegromicro.com/-/media/files/datasheets/acs712-datasheet.pdf): supply, output loading, zero point, current direction, and range sensitivities.
- [ST UM1724 Nucleo-64 manual](https://www.st.com/resource/en/user_manual/dm00105823.pdf): Table 16, NUCLEO-F401RE Arduino header mappings and power pins.

No physical wiring, voltage measurements, or flashing has been performed by this
software change.
