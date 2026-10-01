# Running Lab 2 on macOS and Windows

This guide covers building/flashing the STM32, starting the Raspberry Pi forwarder,
and sending wheel commands. Run commands on the device and in the terminal named
in each section. You do not run the STM32 C files directly on your computer.

```text
Windows wheel GUI -- UDP port 8000 --> Raspberry Pi forwarder
                                      | UART, 115200 baud
                                      v
                                 STM32 Nucleo-F401RE
                                 motors / servo / blinkers
```

A Mac can build, flash, and monitor the STM32. The existing G920 wheel GUI is a
Windows application/script; this repository does not include `proxy_gui.py`, its
SDK, or its dependency list. A complete run using a Mac for firmware still needs
the existing Windows wheel setup. A macOS wheel sender has not been verified.

## 1. Before starting

- Use the Nucleo-F401RE and connect its ST-LINK USB connector with a data cable.
- Follow [WIRING.md](WIRING.md) for actuator/sensor power and all connections.
- Raise the driven wheels and release the throttle before testing.
- Stop the Pi forwarder before flashing, resetting, or debugging the STM32.
- Use matching versions of this repository on the firmware computer and Pi.
  The old raw UART sender is incompatible with the current framed protocol.

The Pi-to-STM32 UART wiring is:

| Raspberry Pi | STM32 |
| --- | --- |
| GPIO14 TX, physical pin 8 | PB7 RX |
| GPIO15 RX, physical pin 10 | PB6 TX |
| GND | GND |

UART uses 115200 baud, 8N1, no flow control. The ST-LINK USB serial console is
separate from the Pi UART.

## 2. Development environment: existing versus new

If Zephyr, its Python environment, and SDK are already installed, skip to section
3 (Mac) or 4 (Windows). Sylvia's existing Mac workspace is:

```text
/Users/sylvialyu/Desktop/VSCode/18649/zephyrproject/
    .venv/
    .west/
    zephyr/
    zephyr-sdk/
    lab2-649/
```

For a new computer, install the host prerequisites from the official
[Zephyr getting-started guide](https://docs.zephyrproject.org/latest/develop/getting_started/).
Select macOS for a Mac, or Ubuntu for Windows running WSL2. Install Python, Git,
CMake, Ninja, and the other host dependencies listed there before continuing.
Do not copy a Mac Python environment or SDK into WSL; install Linux versions there.

The inspected local workspace uses Zephyr revision
`1ee3b93134be23c16ab16b9eb4183bfef63447a6` (4.4.99) and SDK 1.0.1. The following
creates a separate matching workspace at `~/zephyrproject`; only use it if that
folder does not already hold a workspace. Run in macOS Terminal or Ubuntu/WSL:

```bash
mkdir -p ~/zephyrproject
cd ~/zephyrproject
python3 -m venv .venv
source .venv/bin/activate
python -m pip install west
west init -m https://github.com/zephyrproject-rtos/zephyr --mr 1ee3b93134be23c16ab16b9eb4183bfef63447a6 .
west update
west zephyr-export
python -m pip install -r zephyr/scripts/requirements.txt
python -m pip install pyserial
west sdk install --version 1.0.1 --install-dir "$PWD/zephyr-sdk"
git clone https://github.com/ItsNathan21/lab2-649.git lab2-649
```

Use your GitHub credentials if prompted. If the SDK installer reuses an existing
installation elsewhere, use that reported directory for `ZEPHYR_SDK_INSTALL_DIR`
in the steps below. The SDK download can be large.

## 3. macOS: build, flash, and monitor

### 3.1 Open a terminal and activate the environment

In VS Code, choose **Terminal > New Terminal**, or use macOS Terminal:

```bash
cd /Users/sylvialyu/Desktop/VSCode/18649/zephyrproject
source .venv/bin/activate
export ZEPHYR_BASE="$PWD/zephyr"
export ZEPHYR_SDK_INSTALL_DIR="$PWD/zephyr-sdk"
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
cd lab2-649
```

For a new installation from section 2, replace the first command with
`cd ~/zephyrproject`. Repeat this environment setup in every new terminal.
The repository's `setup.sh` assumes `~/zephyrproject`; it does not match Sylvia's
Desktop workspace, so use the explicit commands above.

### 3.2 Build

```bash
west build -p always -b nucleo_f401re/stm32f401xe -d build .
```

Wait for a successful finish. The configuration should identify
`boards/nucleo_f401re.overlay`. The firmware output is `build/zephyr/zephyr.elf`.

### 3.3 Flash

Connect the Nucleo to the Mac and close any debugger or STM32CubeProgrammer session:

```bash
west flash -d build --runner openocd --verify
```

The board starts running after flashing. Actuators remain in fail-safe until fresh
wheel input and both UART directions are healthy.

### 3.4 Open the console

```bash
python -m serial.tools.list_ports
```

Find the Nucleo/ST-LINK device, normally `/dev/cu.usbmodem...`. Replace the example
below with the actual listed port:

```bash
python -m serial.tools.miniterm /dev/cu.usbmodemXXXX 115200
```

Press the board reset button to see startup output, with the Pi forwarder stopped.
Leave this terminal open while testing. Exit miniterm with **Ctrl+]**.

Continue with section 5 on the Pi and section 6 on the Windows wheel computer.

## 4. Windows: build, flash, and monitor using WSL2

This guide uses Ubuntu under WSL2 for firmware work. Run the wheel GUI in native
Windows, not in WSL. Keep the steering wheel attached to Windows; only pass the
ST-LINK device through to WSL.

### 4.1 One-time Windows setup

If WSL is not installed, run in **Administrator PowerShell**:

```powershell
wsl --install -d Ubuntu
```

Restart if requested, open Ubuntu, and finish creating its Linux user. Install
`usbipd-win` using Microsoft's
[USB devices in WSL guide](https://learn.microsoft.com/en-us/windows/wsl/connect-usb).
Use WSL2 and complete section 2 inside Ubuntu if Zephyr is not already installed.
Keep the workspace inside Ubuntu's home directory.

### 4.2 Attach ST-LINK to WSL

Connect the Nucleo and close Windows programs using ST-LINK. Keep an Ubuntu terminal
open. In PowerShell:

```powershell
usbipd list
```

Find the ST-LINK bus ID. Replace `BUSID` in the following commands with that value,
for example `1-4`. If its state is **Not shared**, run once in
**Administrator PowerShell**:

```powershell
usbipd bind --busid BUSID
```

Then in regular PowerShell:

```powershell
usbipd attach --wsl --busid BUSID
```

Skip attachment if already attached. While attached, the device belongs to WSL
and cannot also be used by a Windows serial monitor. After unplugging or rebooting,
check the bus ID and attach again.

### 4.3 Build and flash in Ubuntu/WSL

These commands assume `~/zephyrproject/lab2-649`. If your existing application is
at `~/zephyrproject/zephyr/lab2`, change only the last `cd` to `cd zephyr/lab2`.

```bash
cd ~/zephyrproject
source .venv/bin/activate
export ZEPHYR_BASE="$PWD/zephyr"
export ZEPHYR_SDK_INSTALL_DIR="$PWD/zephyr-sdk"
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
cd lab2-649
west build -p always -b nucleo_f401re/stm32f401xe -d build .
west flash -d build --runner openocd --verify
```

Repeat the environment commands when opening a new Ubuntu terminal.

### 4.4 Open the console in Ubuntu/WSL

```bash
python -m serial.tools.list_ports
python -m serial.tools.miniterm /dev/ttyACM0 115200
```

Replace `/dev/ttyACM0` if the listing shows another port. Exit with **Ctrl+]**.
For serial permission errors, add your Ubuntu user to `dialout`:

```bash
sudo usermod -a -G dialout "$USER"
```

Restart the WSL session so group membership takes effect; reattach ST-LINK if
needed. For OpenOCD USB permission errors, install the SDK's udev rules following
[Zephyr SDK setup](https://docs.zephyrproject.org/latest/develop/toolchains/zephyr_sdk.html).

To return ST-LINK to Windows after finishing, run in PowerShell:

```powershell
usbipd detach --busid BUSID
```

## 5. Raspberry Pi: compile and run the forwarder

These steps are the same whether firmware was built on Mac or Windows. Use a Pi
terminal or SSH into the Pi. The Pi needs a C compiler and a copy of this repository;
it does not need the Zephyr SDK.

### 5.1 One-time Pi setup

On Raspberry Pi OS:

```bash
sudo apt update
sudo apt install build-essential git
sudo raspi-config
```

In the serial-port settings under Interface Options, answer **No** to a serial
login shell and **Yes** to enabling serial hardware. Reboot if requested. See the
[Raspberry Pi configuration documentation](https://www.raspberrypi.com/documentation/computers/configuration.html)
if your model needs additional UART configuration.

If the repository is not already on the Pi:

```bash
cd ~
git clone https://github.com/ItsNathan21/lab2-649.git
```

For an existing clean checkout, update it with `git pull --ff-only` from its folder.
Preserve local edits before updating. Use the same application revision as the
STM32 firmware.

### 5.2 Compile and start

Adjust the first path if your Pi checkout is elsewhere:

```bash
cd ~/lab2-649/pi/proxy_receiver
gcc -std=c11 -Wall -Wextra -O2 -I../../include receiver.c ../../src/uart_protocol.c -o proxy_receiver
hostname -I
ls -l /dev/serial0
./proxy_receiver /dev/serial0
```

Record the Pi IP address reachable from the wheel computer. Keep this program
running; only one instance should use the UART and UDP port 8000. It prints the
resolved serial device and heartbeat/fault diagnostics.

If opening the UART reports permission denied:

```bash
sudo usermod -a -G dialout "$USER"
```

Log out and back in before retrying. If `/dev/serial0` is missing, check UART
configuration rather than guessing a different device.

## 6. Windows wheel GUI: send commands

The GUI source and its Logitech SDK/dependencies are not included in this
repository. Use the team's existing working `proxy_gui.py` installation and its
Python environment. Dependency installation cannot be specified reliably from
this checkout alone.

1. Connect the G920 wheel and pedals to the Windows computer.
2. Open PowerShell in the folder containing `proxy_gui.py`, with the GUI's existing
   Python environment active, and launch it using:

   ```powershell
   python .\proxy_gui.py
   ```

3. Connect/select the wheel in the GUI. The README describes a version that requests
   900 degrees of G920 travel; use that updated copy.
4. Set the destination IP to the Pi address from `hostname -I` and UDP port **8000**.
5. Start sending with throttle released. Both computers must be on a network that
   permits traffic between them. If using a firewall, permit this UDP flow.
6. Confirm that the Pi prints incoming wheel packets before trying the pedals.

A Mac running the STM32 console and the Windows wheel computer can be different
machines. No wheel GUI needs to run on the Mac in this arrangement.

## 7. Verify operation

With the Pi forwarder and GUI sending, the STM32 should print `PID`, `ENC`, and
current-sensor diagnostics. Gently test the throttle, brake, and steering with the
wheels raised. Full-throttle speed and PID gains still need hardware tuning.

| Observation | Meaning / action |
| --- | --- |
| PID target/speed values use mRPM | Divide by 1000 to get RPM |
| Duty or brake is `500/1000` | 50% duty |
| Pi receives no wheel packets | Check GUI destination IP, port, sending state, and network |
| Pi has packets but STM stays in fail-safe | Check both UART directions, common ground, and matching protocol versions |
| Current is `unavailable` | Its validity bit is clear or the heartbeat is stale; do not interpret as zero amps |
| Current `valid=0x07` | All three readings are available; this does not prove calibration accuracy |

Current settings use nominal sensitivity for confirmed 5A ACS712 modules. Measure
reference and zero offsets before trusting current values; see
[CURRENT_SENSOR_WIRING.md](CURRENT_SENSOR_WIRING.md).

Controls:

- Throttle requests wheel speed; released throttle and brake allow coasting.
- Brake overrides throttle and applies proportional electrical braking.
- Steering commands the servo.
- Left/right blinker buttons toggle their respective sides (buttons 5/4).
- **Y** (button 3) once latches self-test braking and hazards. Two distinct presses
  within 500 ms, releasing between them, clear self-test if no other fault is active.
- Link faults can recover automatically. Release throttle before reconnecting:
  fresh commands can resume drive without requiring a pedal release.
- Hardware/PID faults require investigation and reset; a Y double press cannot
  override them.

## 8. Stop, restart, and rebuild

1. Release throttle and brake the wheels to a stop.
2. Stop GUI transmission, then press **Ctrl+C** in the Pi forwarder terminal.
3. Exit the STM32 console with **Ctrl+]** when finished.
4. Disconnect actuator power before handling wiring or using debugger breakpoints.
   Hardware PWM can continue while the CPU is halted.

For source-only updates, from the application folder with the environment active:

```bash
west build -d build
west flash -d build --runner openocd --verify
```

Stop the Pi forwarder first. Use the full `west build -p always ...` command after
changing board configuration or when an old build directory causes errors. Rebuild
the Pi executable too if its source or shared UART protocol changes. After flashing,
reopen the console, restart the Pi forwarder, and start GUI transmission.

## 9. Common setup problems

| Problem | What to check |
| --- | --- |
| `west: command not found` | Activate the workspace's `.venv` in this terminal |
| Zephyr/SDK cannot be found | Check `ZEPHYR_BASE` and `ZEPHYR_SDK_INSTALL_DIR` point to real directories on this OS |
| Build refers to a different computer's paths | Reconfigure using `west build -p always` |
| OpenOCD cannot find ST-LINK | USB data cable, correct connector, no competing debugger; on Windows, WSL USB attachment |
| Serial port is missing | List ports again; reconnect/reattach the board and use the actual device name |
| `Address already in use` on Pi | Stop the other forwarder using UDP 8000 |
| Motors stay braked and hazards flash | Check fresh GUI input, bidirectional UART, self-test state, and printed faults |
| A motor spins backward or PID faults | Stop and compare motor/encoder wiring and feedback signs with the wiring guide |

These commands were checked against the repository and available local tools.
They are not a record of an end-to-end hardware test on either operating system.
