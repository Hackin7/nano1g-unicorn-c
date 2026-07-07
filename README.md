# nano1g-unicorn-c

Standalone C/Unicorn emulator bring-up for the iPod Nano 1G / PP502x target.
This repository is intentionally detached from the local `clicky/` tree. Clicky
and the local docs are references and test oracles only; implementation code in
this repo is original C.

## Build

Install Unicorn 2.x and point CMake at it if it is not on the default search
path:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DUNICORN_ROOT=/path/to/unicorn
cmake --build build
```

On the current Windows workspace, MinGW GCC and CMake are available, but Unicorn
headers/libraries were not found in the visible MinGW prefix during scaffolding.

## Run

```bash
build/nano1g --profile rockbox \
  --firmware ../artifacts/firmware/rockbox_nano_fw.bin \
  --disk ../artifacts/images/ipodhd-rockbox-nano.img \
  --max-insns 20000000 \
  --ppm rockbox.ppm

build/nano1g --profile apple \
  --firmware ../artifacts/firmware/apple_nano_14.5.3.1_fw.bin \
  --disk ../artifacts/images/ipodhd-apple-nano-sysinfo-preferences-probe.img \
  --max-insns 175000000 \
  --ppm apple-language.ppm
```

Useful options:

- `--slice-insns N`: Unicorn instructions per device tick, default `1`.
- `--load-addr ADDR`: override firmware load address, default `0x10000000`.
- `--entry ADDR`: override initial PC, default follows `--load-addr`.
- `--input "wheel-down,wait:250,select"`: accepted and logged; device injection
  is a later milestone.

## Current Status

Implemented foundation:

- CMake project and CLI.
- Unicorn CPU wrapper with CPU/COP instances.
- RAM and fast RAM mapping.
- Central physical bus with MMIO forwarding hooks.
- Initial device stubs for interrupt controller, timers, CPU control, DMA, GPIO,
  I2C/opto, LCD2, flash, and ATA-like disk reads.
- HLE boot seed that loads firmware into guest memory and initializes registers.
- Headless LCD PPM output.

Still expected before real Apple Language-screen parity:

- exact firmware wrapper parsing and boot vectors;
- richer HLE sysinfo/model state;
- complete IDE command sequencing;
- exact interrupt/timer semantics;
- Apple LCD/DMA path validation;
- scripted input injection.
