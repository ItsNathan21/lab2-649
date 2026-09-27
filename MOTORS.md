# L298N motor driver bring-up

`src/motor.c` controls two L298 bridges using 10 kHz PWM on ENA/ENB.
`main.c` initializes both into **coast** and continues encoder/UART diagnostics.
No motor starts automatically, and received wheel packets do not drive it yet.
This is the output driver; PID control, brake-pedal decoding, link-loss handling,
and the lab's startup error/braking state still belong in the application.

## Wiring selected for the F401RE

| L298N signal | STM32 pin | Purpose |
| --- | --- | --- |
| ENA | PB4 | Left enable, TIM3 channel 1 PWM |
| IN1 | PC4 | Left direction input 1 |
| IN2 | PC5 | Left direction input 2 |
| ENB | PB5 | Right enable, TIM3 channel 2 PWM |
| IN3 | PC6 | Right direction input 1 |
| IN4 | PC7 | Right direction input 2 |
| GND | GND | Common signal ground |

PB4/PB5 are on CN7; PC4-PC7 are on CN10. Refer to the supplied Morpho pin
diagram. PB4 is shared with JTAG NJTRST; use the normal ST-LINK **SWD** debugger.
The pins avoid the encoders (PC0-PC3), USART1 (PB6/PB7), and console (PA2/PA3).
Reserve TIM3 for both motor channels: they share a single PWM period.

Wire the left motor power pair to OUT1/OUT2 and the right pair to OUT3/OUT4.
The kit's 12 V adapter powers the L298 motor-supply terminal, with its negative
connected to common ground. Keep the Nucleo powered through USB.

Before connecting PB4/PB5, **remove ENA/ENB jumpers that tie those enables HIGH**.
These are different from the module's 5 V regulator jumper. The regulator
jumper and 5 V logic-supply connection depend on your exact module; identify
them from its documentation or a photo before applying power. Do not connect
the module's 5 V terminal to an STM32 GPIO.

Use external 10 kohm pull-downs on ENA and ENB to ground to hold the bridge
disabled while the MCU resets or is unpowered. Firmware only controls the
pins after initialization. Check enable levels with motor power disconnected.

## API (thread context only)

Check every return value: zero is success, a negative errno is a failure.

| Call | Result |
| --- | --- |
| `motors_init()` | Initialize once; both enables LOW |
| `motor_drive(MOTOR_LEFT, 200)` | Left forward at 20% duty |
| `motor_drive(MOTOR_RIGHT, -200)` | Right reverse at 20% duty |
| `motor_drive(MOTOR_LEFT, 0)` | Left coasts |
| `motors_coast()` | Both coast, unless braking is latched |
| `motors_brake()` | Both dynamically brake; latch brake priority |
| `motors_release_brake()` | Release brake into coast; no automatic restart |

Duty is signed thousandths, from -1000 through +1000. Out-of-range commands
return `-EINVAL` without changing outputs. Braking rejects drive/coast commands
with `-EPERM` until explicitly released. Hardware errors latch a fault, attempt
to disable both enables, and require reset before more drive commands. A failed
electrical output cannot be made safe by software alone.

A direct powered direction reversal returns `-EBUSY`. Coast or brake first,
then use encoder feedback to verify the wheel has stopped before reversing.
The driver does not itself measure wheel speed or enforce a stopped-wheel delay.
If positive duty turns a wheel backward, change its `polarity[]` entry in
`src/motor.c` to -1, or swap that motor's OUT leads with power disconnected.

The L298 truth table is:

| Enable | IN1/IN2 (or IN3/IN4) | Behavior |
| --- | --- | --- |
| LOW | Any | Coast |
| PWM | HIGH/LOW | Drive one direction |
| PWM | LOW/HIGH | Drive the other direction |
| HIGH continuously | LOW/LOW | Dynamic braking |

Brake stops PWM switching by setting a constant HIGH enable; setting zero
duty instead would coast. Brake does not mechanically hold a stationary wheel.
The driver serializes calls with a mutex. It waits one PWM period after disabling
enables before changing direction, because STM32 timer compares use preload
registers. The lab's 2 ms response requirement still needs scope measurement
after command handling and scheduling are integrated.

## First powered test

An opt-in build waits for a fresh press/release of the blue USER button, runs
the LEFT motor at 20% for 500 ms, then brakes both wheels and prints encoder
deltas. It runs once per reset and never starts motion automatically on boot.
Do not halt the debugger during motion: the CPU must run to execute the stop.

```sh
west build -b nucleo_f401re/stm32f401xe -d build-motor-test . -- -DLAB_MOTOR_TEST=ON
west flash -d build-motor-test --runner openocd
```

The normal build defaults to this test OFF. Reset re-arms the test button;
to remove the test from its build, rebuild with `-DLAB_MOTOR_TEST=OFF` and flash.

Keep wheels raised and verify wiring/jumpers before applying motor power.
First scope ENA/ENB after boot: both should remain LOW. To run a short test,
call this from application thread context after initialization (it is NOT
enabled in the shipped main):

```c
int ret = motor_drive(MOTOR_LEFT, 200); /* 20%, left wheel only */
if (ret == 0) {
    k_sleep(K_MSEC(500));
    ret = motors_brake();
}
if (ret != 0) {
    printk("Motor test failed: %d\n", ret);
}
```

The brake remains latched at the end. Test one wheel at a time, verify forward
motion matches increasing encoder counts, and compare coast with braking.
20% duty is a starting point, not a guarantee the motor will overcome friction.
Do not leave a motor powered and stalled if it fails to move.

Both encoders are calibrated to **1320 decoded counts per wheel revolution**.
The console now prints millirpm (`mRPM`): 60000 mRPM = 60 RPM.

## Build and verification

From this application directory, with the Zephyr environment active:

```sh
west build -b nucleo_f401re/stm32f401xe -d build-encoder .
west flash -d build-encoder --runner openocd
```

The existing build directory name is retained. Flashing this build initializes
the outputs but does not run the example above.

Host tests compile the actual driver against fake GPIO/PWM, including delayed
PWM updates and injected failures:

```sh
cc -std=c11 -Wall -Wextra -Werror -Itests/motor_stubs -Iinclude src/motor.c tests/test_motor.c -o /tmp/lab2-test-motor
/tmp/lab2-test-motor
/tmp/lab2-test-motor gpio-fault
/tmp/lab2-test-motor init-fault
/tmp/lab2-test-motor brake-fault
```

These cover direction, duty limits, reversal rejection, brake priority, coast,
release without restart, startup failure, and latched hardware faults. They do
not verify actual wiring, timer waveforms, interrupt scheduling, or motor motion.

References: [ST L298 datasheet](https://www.st.com/resource/en/datasheet/l298.pdf),
[Zephyr PWM API](https://docs.zephyrproject.org/latest/doxygen/html/group__pwm__interface.html).
