# Lab 2: left to do

As of 10/02. The handout deadline was 10/01, 11:59pm, so check with the TA about late
checkoff or submission.

## Done
- Blinkers cancel each other (left turns right off, and vice versa)
- Servo pulse limits narrowed to 910–2100 us; no buzzing at full lock
- Buck converter ground fixed, which cured the servo dropouts and the left-wheel twitching
- Current sensors calibrated; rest, running and stall readings in [READINGS.md](READINGS.md)
- Console rows update in place; fault changes are logged; current prints at 10 Hz
- `tools/record_current.py` recorder (`s` to start and stop, `q` to quit)
- Cause of the random hazard flashes identified: Wi-Fi dropouts (`FAULT 0x00 -> 0x02`)

## Must do
1. **Commit and push.** All of the work above is uncommitted.
2. **Test-point GPIO toggles (checkoff item 10, timing).** `CMD_RX` and `PWM_SET` on the
   STM32, and `UDP_RX` and `CMD_TX` on the Pi. Without them the TAs cannot measure
   throttle, brake or steering response. This needs 2 free STM32 pins and 2 free Pi GPIOs.
3. **Wi-Fi hazard flashes.** Run `sudo iw wlan0 set power_save off` on the Pi, or connect
   the laptop and Pi with Ethernet. Otherwise the hazards may flash randomly during the
   demo.
4. **PID overshoot.** The wheels ran 11–100% above the target speed, worst at low throttle.
   Checkoff step 3 is "velocity rises and holds". The feedforward is likely too high and
   the integral too slow (`include/motor_controller.h`).

## Should do
5. **Four blinker outputs** (FL, FR, RL, RR). There are only 2 pins (PC8, PC10); ask the TA
   whether pairs sharing a pin are acceptable.
6. **Bad-input demo (checkoff item 8).** A way to send a rejected command, for example a Pi
   flag that sends a malformed frame.
7. **Probe breadboard.** 13 labeled rows, in the handout's order.
8. **Current sensor rewiring** to remove the crosstalk: ACS712 VCC to the Nucleo +5V, and
   separate sensor grounds. Re-zero afterwards. Optional if it is explained at checkoff.
9. **Full checkoff dry run.** Cold start, steering, throttle, brake, blinkers and
   auto-cancel, Y single and double press, UART unplug, currents.

## Writeup (Gradescope)
10. Block diagram
11. Schematic with power rails and ground, **as actually wired**: the servo on the 5 V buck,
    the sensors' real power source, the motor − leads through the sensors, and the buck
    ground.
12. Team values table: steering buzz limits −20220/+20703, pedal ranges, button indices,
    120 RPM max, current readings.
13. Task table: name, period, priority, deadline.
14. One-page writeup on how this becomes three zones on CAN.

## Doc fixes
15. [WIRING.md](WIRING.md) still says 6 V for the servo, sensors in the + lead, and sensor
    power from the Nucleo +5V. Update it to match the actual wiring so the docs and the
    schematic agree.
