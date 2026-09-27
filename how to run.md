# How to run

Run in WSL/Ubuntu.

## Setup and build

```bash
cd ~/zephyrproject/zephyr/lab2
source ./setup.sh
west build -p always -b nucleo_f401re/stm32f401xe -d build .
```

## Flash

```bash
west flash -d build --runner openocd --verify
```

## Open debugger

```bash
west debug -d build --runner openocd
```

In GDB:

```gdb
break main
monitor reset halt
continue
```

`next`: step over. `step`: step into. `continue`: resume. Ctrl+C: pause. `quit`: exit.
