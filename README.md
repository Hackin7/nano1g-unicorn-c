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

On the current Windows workspace, use the checked-in MinGW preset:

```bash
cmake --preset mingw-release
cmake --build --preset mingw-release
```

Run the registered smoke tests:

```bash
ctest --test-dir build-mingw --output-on-failure
```

The Rockbox and Apple tests use fixtures from `../artifacts/` and skip when
those local files are absent.

Manual emulator outputs should go under `tmp/`, which is gitignored. For
example, use `--ppm tmp/apple-language.ppm` rather than writing PPMs into the
repo root.

## Run

```bash
python tools/make_gpt_rockbox_disk.py \
  ../artifacts/images/ipodhd-rockbox-nano.img \
  tmp/ipodhd-rockbox-nano-gpt.img

build-mingw/nano1g --profile rockbox \
  --firmware ../artifacts/firmware/rockbox.ipod \
  --disk tmp/ipodhd-rockbox-nano-gpt.img \
  --max-insns 10000000000 \
  --slice-insns 512 \
  --timer-divider 1 \
  --ppm tmp/rockbox-menu.ppm

build-mingw/nano1g --profile apple \
  --firmware ../artifacts/firmware/apple_nano_14.5.3.1_fw.bin \
  --disk ../artifacts/images/ipodhd-apple-nano-sysinfo-preferences-probe.img \
  --max-insns 175000000 \
  --ppm tmp/apple-language.ppm
```

Useful options:

- `--boot-mode direct|flash`: direct loads `--firmware` into guest RAM; flash
  maps `--flash-rom` at reset vector `0x00000000` and does not preload `osos`.
- `--firmware-from-disk`: in direct mode, find a wrapped firmware image in the
  loaded disk image and load its `osos` entry instead of using `--firmware`.
- `--flash-rom PATH`: load a NOR/boot-ROM image, required with
  `--boot-mode flash`.
- `--dump32 ADDR --dump-count N`: print up to 32 words after execution, useful
  for small hardware smoke probes.
- `--slice-insns N`: Unicorn instructions per device tick, default `1`.
- `--timer-divider N`: configurable timer ticks per device tick, default `20`.
  Use `1` for Rockbox timing experiments where timer1 should track the
  microsecond timer more closely.
- `--verbose`: enable Apple probe logs and extra tracing hooks. Normal runs keep
  only status lines.
- `--trace-pc`: log translated basic blocks. This is very slow.
- `--trace-mmio`: log MMIO accesses. This is very slow.
- `--load-addr ADDR`: override firmware load address, default `0x10000000`.
- `--entry ADDR`: override initial PC, default follows `--load-addr`.
- `--input "wheel-down,wait:250,select"`: accepted and logged; device injection
  is a later milestone.

Useful audit tools:

- `tools/inspect_boot_sources.py`: compare updater ZIP, wrapped firmware, and
  disk firmware payloads while labeling boot/sysinfo-looking byte patterns by
  source region.
- `tools/inspect_fat32.py`: inspect the FAT32 data partition and map absolute
  disk offsets back to file paths.
- `tools/decrypt_aupd.py`: decrypt the wrapped Apple `aupd` payload using the
  flash security block.
- `tools/inspect_flash_image.py`: inspect/extract directory entries inside a
  decrypted flash-style image.

## Current Status

Implemented foundation:

- CMake project and CLI.
- Unicorn CPU wrapper with CPU/COP instances.
- RAM and fast RAM mapping.
- Central physical bus with MMIO forwarding hooks.
- Initial device stubs for interrupt controller, timers, CPU control, DMA,
  I2C/opto, LCD2, flash, and ATA-like disk reads.
- GPIO register latching plus PP502x `+0x800` atomic bitwise mirror writes.
- Boot reset loader that loads the requested firmware image into guest memory
  and initializes CPU handoff registers.
- Cache-control and memory-controller MMIO ranges are decoded through the
  central bus and preserve byte, halfword, and word register accesses.
  Cache-control writes also flush Unicorn translation blocks so native guest
  cache-maintenance operations can affect translated code.
- Timer and interrupt-controller reads preserve byte, halfword, and word
  access semantics; a native ARM smoke covers timer expiry, interrupt status,
  timer-value acknowledge, and one-shot disable behavior.
- Headless LCD PPM output.
- Rockbox canary reaches a nonblack framebuffer.
- Rockbox core reaches the main menu with native firmware execution using the
  GPT-wrapped disk fixture, `--slice-insns 512`, and `--timer-divider 1`.
- The standalone Rockbox bootloader fixture runs from fast RAM and reaches a
  nonblack framebuffer without synthetic sysinfo handoff state.
- Flash boot mode can load and map an external NOR/boot-ROM image at
  `0x00000000`. The current local fixtures do not include a stock Apple boot
  ROM dump.
- Direct boot can also source wrapped firmware from the disk image firmware
  partition, matching the local Apple/Rockbox media layout more closely.
- ATA Identify Device and read-sector transfers now latch the guest-visible
  command/LBA registers, assert DRQ while data is available, and clear DRQ when
  the guest drains the transfer.
- DMA-to-LCD2 transfers route through the modeled LCD2 register window and are
  covered by a native ARM smoke that produces real PPM pixels.
- The I2C/PMU path models basic PCF register pointer, default reads, guest
  writes, busy polling, and readback through native MMIO transactions.
- Basic Intel-style NOR read-array, read-ID, status, CFI, program, and block
  erase commands are modeled for boot ROM probing.
- Apple firmware starts without instruction shims, synthetic sysinfo/model RAM,
  or synthetic framebuffer output. It currently stops early because the real
  boot metadata/sysinfo source is not modeled yet. The native Language screen is
  not working.
- CTest smoke coverage verifies Rockbox nonblack framebuffer output and checks
  that the Apple smoke path stays native/no-HLE. The Apple smoke is not a
  Language-screen acceptance test yet.
- `tools/inspect_disk_image.py` verifies the local Apple/Rockbox HDD images
  carry wrapped firmware partitions at LBA 2048.

No-HLE rule:

- Hardware behavior may be modeled; firmware behavior must run as guest code.
- No synthetic Apple sysinfo/model blocks in guest RAM.
- No fabricated UI/LCD descriptors, framebuffer fills, or Language-screen
  drawing.
- No instruction skips, firmware service shims, or PC rewrites to force
  progress.
- Reset setup is limited to loading the requested bytes into the configured
  memory device and setting architectural CPU reset state. Anything beyond that
  must come from modeled hardware or native firmware execution.

See `NO_HLE.md` for the project contract.

Still expected before real Apple Language-screen parity:

- native boot ROM/bootloader path, or modeled hardware media path, that produces
  Apple boot metadata/sysinfo through guest execution;
- native Apple framebuffer rendering for the Language screen;
- Apple LCD/DMA path validation against the native Apple framebuffer;
- exact interrupt/timer semantics under larger `--slice-insns` values;
- native Apple boot ROM or flash dump. The current local artifact set still
  contains Apple wrapped firmware/disk images, patched experiments, screenshots,
  and analysis files, but no obvious stock boot ROM image;
- complete IDE command sequencing;
- broader PMU/RTC/input-device register behavior once native firmware probes
  more of those devices;
- more exact cache and memory-controller side effects once native boot code
  requires them;
- scripted input injection.

See `BENCHMARKS.md` for current timing and framebuffer smoke results.
