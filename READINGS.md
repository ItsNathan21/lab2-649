# Current sensor readings (Lab 2, section 3.5)

Three ACS712 5A modules: left motor, right motor, and servo. Each output goes through
a 10k/10k divider to the ADC (left A0/PA0, right A2/PA4, servo A3/PB0). At the ADC pin
the nominal sensitivity is 92.5 mV/A, so **1 A = 92.5 mV**.

Recorded on 10/01–10/02 with `tools/record_current.py`, at 10 readings per second. Raw
CSVs are in `recordings/`, which is not committed.

## Summary

| Sensor | Rest | Running (free-spinning) | Stalled |
| --- | --- | --- | --- |
| Left motor | ≈ 0 A (±0.1 A noise) | ≈ 0.1 A at half throttle | **≈ 1.5 A** (peak ≈ 1.8 A) at 91% duty |
| Right motor | ≈ 0 A (±0.1 A noise) | ≈ 0.05 A at half throttle, ≈ 0.3 A at full | **≈ 2.7 A** at 96% duty (likely overstated, see below) |
| Servo (5 V supply) | ≈ 0 A plus holding current | not measured separately | **≈ 0.7 A** (earlier tries: 0.5–0.8 A) |

Stall values are the change in ADC voltage from an idle recording, divided by 92.5 mV/A.
Using the change cancels the zero-point drift described below.

## Final stall run (10/02, after fixing the buck converter ground)

The drive stalls were at full throttle (target 120 RPM), not light throttle.

| Recording | n | Left mV | Right mV | Servo mV | PID state |
| --- | --- | --- | --- | --- | --- |
| Idle | 20 | 1297.7 | 1307.9 | 1295.2 | target 0 |
| Left wheel stalled | 7 | 1156.7 | 1336.6 | 1417.9 | L 41.7 RPM @ 911/1000, R 142 RPM @ 600/1000 |
| Right wheel stalled | 8 | 1338.2 | 1555.0 | 1411.4 | L 142.4 RPM @ 603/1000, R 19.3 RPM @ 964/1000 |
| Both stalled | 10 | 1227.3 | 1502.4 | 1446.4 | L 27.6 RPM @ 922/1000, R 27.0 RPM @ 924/1000 |
| Servo stalled (front wheels held) | 15 | 1257.8 | 1281.1 | 1233.1 | target 0 |

Change from idle, and the equivalent current:

| Recording | Left | Right | Servo |
| --- | --- | --- | --- |
| Left stall | **−141 mV (1.5 A)** | +29 mV (0.3 A, right wheel spinning) | +123 mV (crosstalk) |
| Right stall | +41 mV (crosstalk) | **+247 mV (2.7 A)** | +116 mV (crosstalk) |
| Both stalled | **−70 mV (0.8 A)** | **+195 mV (2.1 A)** | +151 mV (crosstalk) |
| Servo stall | −40 mV | −27 mV | **−62 mV (0.7 A)** |

The left sensor's voltage falls under forward load and the right sensor's rises. The
firmware handles this with signed sensitivities: left and servo are `-92500`, right is
`92500` in `include/current_sensor.h`.

## Known measurement problems

1. **Crosstalk during motor stalls.** The servo channel moved by 116–151 mV, which reads as
   1.3–1.6 A, while only a drive motor was stalled and the servo was idle. The left channel
   also rose 41 mV during the right stall while the left wheel spun freely. Large motor
   currents shift every sensor's reference. The likely causes are that the sensor grounds
   share a wire carrying motor return current, and that the sensors' 5 V supply comes from
   the 12 V side and sags under load. The stalled-channel values above include this shift,
   so treat them as accurate to about ±1 A. The right-stall 2.7 A is probably high, because
   the shift adds in the same direction.
2. **Zero-point drift.** The idle ADC voltage changed between sessions (left: 1297, 1321
   and 1332 mV) because the ACS712 zero sits at half its supply voltage, which varies with
   load. This is about ±0.1–0.4 A of offset. That is why the stall values are measured
   against an idle recording taken in the same session.
3. **Noise.** A single reading varies by about ±15–50 mV (±0.15–0.5 A) with PWM running.
   The firmware averages 4 ADC scans over 40 ms. More averaging would steady the readings
   but delay stall detection.
4. **Earlier readings are unreliable.** Readings taken before the buck converter ground was
   connected (10/01 evening) are not trustworthy. The half-throttle running values above
   come from that period, but they are consistent with free-spinning motors drawing little
   current.

## Fault-threshold notes (for later labs)

- Disconnected motor while driven: about 0 A, which is indistinguishable from rest at this
  noise level.
- Running, unloaded: 0.05–0.3 A.
- Stalled: about 1.5–2.7 A at 90%+ duty. That is well separated from running, so a stall
  threshold around 1 A sustained for a few hundred ms would be reasonable once the
  crosstalk is fixed.

## To improve these readings

- Power all three ACS712 VCC pins from the Nucleo +5V (CN6 pin 5) instead of the 12 V side.
- Run each sensor's GND and divider bottom directly to a Nucleo GND pin, separate from the
  motor and servo return wiring.
- Re-zero afterwards: record about 3 s at idle and set the `*_ZERO_UV` values.
