#!/usr/bin/env python3
"""Record STM32 current readings between presses of 's' and print their averages.

Run from WSL with the Zephyr venv active (it provides pyserial), instead of miniterm:

    python tools/record_current.py            # auto-detects /dev/ttyACM*
    python tools/record_current.py --port /dev/ttyACM1

Keys: s = start/stop recording, q = quit. Each stopped recording prints per-sensor
statistics and is saved as a CSV under recordings/. Only one program can read the
console port at a time, so close miniterm first.
"""
import argparse
import csv
import glob
import os
import re
import select
import statistics
import sys
import termios
import time
import tty

import serial

ANSI = re.compile(rb"\x1b(?:\[[0-9;?]*[A-Za-z]|[78])")
CURRENT = re.compile(
    r"CURRENT L/R/S raw=(\d+)/(\d+)/(\d+) mV=(-?\d+)/(-?\d+)/(-?\d+) "
    r"mA=(-?\d+)/(-?\d+)/(-?\d+) sampled=0x([0-9a-fA-F]+) valid=0x([0-9a-fA-F]+)")
PID = re.compile(
    r"PID target=(\d+) mRPM L=(-?\d+) mRPM duty=(\d+)/1000 "
    r"R=(-?\d+) mRPM duty=(\d+)/1000 brake=(\d+)/1000")
SENSORS = ("left", "right", "servo")
CSV_FIELDS = ["t_s", "left_mA", "right_mA", "servo_mA", "left_mV", "right_mV", "servo_mV",
              "valid_mask", "pid_target_mrpm", "pid_left_mrpm", "pid_left_duty",
              "pid_right_mrpm", "pid_right_duty", "pid_brake"]


class Recorder:
    """Parse console lines, track the latest values, and collect samples while recording."""

    def __init__(self):
        self.pid = None
        self.current = None
        self.recording = False
        self.start_time = 0.0
        self.samples = []

    def feed_line(self, line, now):
        """Handle one console line; return it if it is an event message worth showing."""
        match = PID.search(line)
        if match:
            self.pid = tuple(int(value) for value in match.groups())
            return None
        match = CURRENT.search(line)
        if match:
            groups = match.groups()
            self.current = {
                "mV": tuple(int(value) for value in groups[3:6]),
                "mA": tuple(int(value) for value in groups[6:9]),
                "valid": int(groups[10], 16),
            }
            if self.recording:
                self.samples.append((now - self.start_time, self.current, self.pid))
            return None
        if line.startswith("ENC") or not line.strip():
            return None
        return line

    def toggle(self, now):
        """Start or stop recording; return the finished samples when stopping."""
        self.recording = not self.recording
        if self.recording:
            self.start_time = now
            self.samples = []
            return None
        return self.samples


def summarize(samples):
    """Return printable per-sensor statistics for a finished recording."""
    if not samples:
        return ["No CURRENT readings recorded (they arrive 10 times per second)."]
    duration = samples[-1][0] - samples[0][0]
    lines = [f"Recorded {len(samples)} readings over {duration:.1f} s"]
    for index, name in enumerate(SENSORS):
        values = [current["mA"][index] for _, current, _ in samples
                  if current["valid"] & (1 << index)]
        volts = [current["mV"][index] for _, current, _ in samples]
        if not values:
            lines.append(f"  {name:5}: no valid readings")
            continue
        spread = statistics.pstdev(values) if len(values) > 1 else 0.0
        lines.append(f"  {name:5}: avg {statistics.mean(values):7.1f} mA  sd {spread:6.1f}  "
                     f"min {min(values):6d}  max {max(values):6d}  "
                     f"(avg {statistics.mean(volts):.1f} mV, n={len(values)})")
    pids = [pid for _, _, pid in samples if pid is not None]
    if pids:
        lines.append("  PID  : target avg {:.1f} RPM, left {:.1f} RPM @ {:.0f}/1000, "
                     "right {:.1f} RPM @ {:.0f}/1000, brake {:.0f}/1000".format(
                         statistics.mean(p[0] for p in pids) / 1000,
                         statistics.mean(p[1] for p in pids) / 1000,
                         statistics.mean(p[2] for p in pids),
                         statistics.mean(p[3] for p in pids) / 1000,
                         statistics.mean(p[4] for p in pids),
                         statistics.mean(p[5] for p in pids)))
    return lines


def save_csv(samples, directory):
    """Write samples to a timestamped CSV and return its path."""
    os.makedirs(directory, exist_ok=True)
    path = os.path.join(directory, time.strftime("current_%Y%m%d_%H%M%S.csv"))
    with open(path, "w", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(CSV_FIELDS)
        for elapsed, current, pid in samples:
            writer.writerow([f"{elapsed:.3f}", *current["mA"], *current["mV"],
                             f"0x{current['valid']:02x}", *(pid or ("",) * 6)])
    return path


def find_port():
    """Return the first /dev/ttyACM* device that is actively printing."""
    for path in sorted(glob.glob("/dev/ttyACM*")):
        try:
            with serial.Serial(path, 115200, timeout=1.0) as port:
                if port.read(200):
                    return path
        except serial.SerialException:
            continue
    return None


def status_text(recorder):
    """Build the one-line live display."""
    state = (f"REC {len(recorder.samples)} samples" if recorder.recording
             else "idle (s=record, q=quit)")
    if recorder.current is None:
        return f"[{state}] waiting for CURRENT readings..."
    left, right, servo = recorder.current["mA"]
    text = f"[{state}] mA L={left} R={right} S={servo}"
    if recorder.pid is not None:
        text += f" | target={recorder.pid[0] / 1000:.1f} RPM brake={recorder.pid[5]}/1000"
    return text


def show(message):
    """Print a message above the live status line."""
    sys.stdout.write("\r\033[2K" + message + "\n")


def main():
    """Run the interactive recorder until q or Ctrl+C."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", help="console device, e.g. /dev/ttyACM0")
    parser.add_argument("--out", default="recordings", help="CSV output directory")
    args = parser.parse_args()
    if not sys.stdin.isatty():
        sys.exit("Run this in an interactive terminal so it can read the 's' key.")
    port_name = args.port or find_port()
    if port_name is None:
        sys.exit("No active /dev/ttyACM* console found. Is the board attached and miniterm closed?")

    recorder = Recorder()
    pending = b""
    old_tty = termios.tcgetattr(sys.stdin)
    with serial.Serial(port_name, 115200, timeout=0) as port:
        print(f"Reading {port_name}. Press s to start/stop recording, q to quit.")
        try:
            tty.setcbreak(sys.stdin.fileno())
            while True:
                ready, _, _ = select.select([sys.stdin, port], [], [], 0.2)
                now = time.monotonic()
                if sys.stdin in ready:
                    key = sys.stdin.read(1).lower()
                    if key == "q":
                        break
                    if key == "s":
                        finished = recorder.toggle(now)
                        if finished is None:
                            show("Recording started.")
                        else:
                            for line in summarize(finished):
                                show(line)
                            if finished:
                                show(f"  saved {save_csv(finished, args.out)}")
                if port in ready:
                    pending += port.read(4096)
                    parts = re.split(rb"\n", ANSI.sub(b"\n", pending))
                    pending = parts[-1]
                    for part in parts[:-1]:
                        message = recorder.feed_line(part.decode(errors="replace").strip(), now)
                        if message:
                            show(message)
                sys.stdout.write("\r\033[2K" + status_text(recorder))
                sys.stdout.flush()
        except KeyboardInterrupt:
            pass
        finally:
            termios.tcsetattr(sys.stdin, termios.TCSADRAIN, old_tty)
            if recorder.recording and recorder.samples:
                show("Stopped while recording:")
                for line in summarize(recorder.samples):
                    show(line)
                show(f"  saved {save_csv(recorder.samples, args.out)}")
            sys.stdout.write("\n")


if __name__ == "__main__":
    main()
