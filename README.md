# Lab 2: Zephyr bring-up

Minimal firmware for the Nucleo F401RE (`nucleo_f401re/stm32f401xe`), as specified in the supplied flashing guide. It prints a startup banner and `Hello World! count=N` once per second. The sleep yields the CPU to Zephyr instead of busy-waiting.

## Layout

- `src/main.c`: application entry point; add application modules under `src/`.
- `include/`: shared application headers.
- `drivers/`: future sensor/actuator driver implementations.
- `boards/`: future board-specific devicetree overlays.
- `prj.conf`: console and source-level debug settings.
- `CMakeLists.txt`: build definition; explicitly add new C sources here as they are implemented.

No custom overlay is needed for the default console. The board devicetree selects USART2 on PA2/PA3, connected to the ST-LINK virtual COM port. Reserve that console when designing the future Pi link, or deliberately move debug output to another interface.

## Build in WSL / Ubuntu

From this `lab2` directory, in each new shell:

```bash
source ./setup.sh
west build -b nucleo_f401re/stm32f401xe -d build .
```

Use `west build -p always -b nucleo_f401re/stm32f401xe -d build .` for a pristine rebuild when changing boards or configuration.

## Flash and see output

Connect the board through its ST-LINK USB connector. For commands running in WSL, the USB device must be attached to WSL.

```bash
west flash -d build --runner openocd
python -m serial.tools.miniterm /dev/ttyACM0 115200
```

The actual serial device may differ; check `ls /dev/ttyACM*`. Use 115200 baud, 8 data bits, no parity, 1 stop bit, and no flow control. Exit miniterm with Ctrl+]. A Windows serial terminal can use the board's COM port when USB is owned by Windows instead.

Expected application output:

```text
Lab 2: Zephyr bring-up on nucleo_f401re/stm32f401xe
Hello World! count=0
Hello World! count=1
```

The repeated line remains visible even if the terminal opens after boot. Press RESET to see the startup banner again. Output appears in the serial terminal, independently of the GDB debug console.

The default `west flash -d build` runner is STM32CubeProgrammer if you have that installed; the explicit OpenOCD command above uses the alternative runner supported by this board.

## Source-level debugging

```bash
west debug -d build --runner openocd
```

This starts the debug server and GDB together. In GDB, use `break main` and `continue`. For separate terminals, use `west debugserver -d build --runner openocd` in one and `west attach -d build --runner openocd` in the other; do not start a second debug server with `west debug`.

The original lab writeup and flashing guide remain in this directory. This starter only establishes console output; the lab's blinky checkpoint and sensor/actuator bring-up are subsequent steps.
