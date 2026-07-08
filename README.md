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
- `--map-flash-zero`: in direct mode, keep the firmware payload at
  `--load-addr` but map the modeled NOR flash at `0x00000000` instead of the
  low SDRAM alias. This is useful for RAM-loaded updater probes that still issue
  flash commands to address zero.
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
- `tools/audit_boot_sources.py`: classify local boot-source candidates as
  wrapped firmware bundles, updater-style flash images, or raw reset-vector
  candidates without treating any of them as Apple boot ROMs by guesswork.
- `tools/inspect_aupd_batch.py`: scan files for the native AUPD updater's
  `FwUp` 28-byte command records and required `!dnE` end marker.
- `tools/inspect_aupd_layout.py`: decode the decrypted AUPD reset relocation
  constants and probe the parser destination area.
- `tools/inspect_payload_refs.py`: scan extracted Apple payloads for word-aligned
  raw references, ARM literal-load references, and simple `adr` references to
  boot/sysinfo marker strings without treating unaligned byte coincidences as
  code evidence.
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
- PP peripheral identity now reports the Nano-class PP5022-style `PP20` /
  `20D ` values, so Apple direct boot chooses the 128 KiB fast-RAM handoff slot
  at `0x4001ff18`.
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
- Basic NOR read-array, software-ID, status, CFI, program, and block erase
  commands are modeled for boot ROM probing. The software-ID response now uses
  the SST39WF800A-style `0x00bf/0x273f` identity that appears in AUPD's native
  flash table.
- Direct boot has an opt-in `--map-flash-zero` mode for RAM-loaded firmware
  payloads that need modeled NOR at address zero. A fast-RAM smoke verifies the
  guest can issue a NOR read-ID command through this mapping.
- The PP502x EVP exception-vector registers at `0x6000f000..0x6000f01f` are
  latched, and `CACHE_CTL` bit 4 selects the local SWI/IRQ vectors. A native
  smoke verifies SVC dispatch through the modeled local vector.
- The PP502x memory-controller MMAP registers at `0xf000f000..0xf000f03f`
  latch guest writes and apply the documented iPod-style remap case: logical
  zero can be remapped to SDRAM, while NOR flash can be exposed at
  `0x20000000`. A native smoke starts with flash at zero, programs MMAP, then
  verifies low SDRAM and the flash alias from guest code.
- The Unicorn SWI hook logs Apple updater `svc 0x123456` diagnostic text in
  verbose mode without replacing guest exception handling.
- Apple firmware starts without instruction shims, synthetic sysinfo/model RAM,
  or synthetic framebuffer output. It currently stops early because the real
  boot metadata/sysinfo source is not modeled yet. The native Language screen is
  not working.
- The decrypted Apple `aupd` payload can run in flash mode as native ARM code
  far enough to probe ATA and emit its own diagnostic text, but it behaves like
  an updater waiting for an update command stream and currently reports
  `END MARKER - NOT FOUND`. A read-only parser hook shows it is pointed at
  uninitialized SDRAM (`0x1003bb50`, filled with `0x2d`), not a stream read from
  the current disk fixture. It is not a stock boot ROM replacement.
- Running decrypted `aupd` as a direct RAM payload at `0x10000000` reaches its
  relocated `Pyld` / `FwUp` command records natively. `smoke_aupd_direct_no_handoff`
  verifies that this path sees the expected native `FwUp/flsh` record but still
  leaves the Apple `osos` fast-RAM handoff slot untouched. In verbose mode the
  same smoke now also logs the native AUPD software-ID/read-array writes
  to low memory; with the normal direct low-zero SDRAM alias, those writes hit
  `low0_map=1` rather than the modeled NOR device, and the immediate read-ID
  readback sees SDRAM/vector bytes instead of SST ID data. Combining the same
  payload with `--map-flash-zero` exposes the other side of that hardware
  question: the updater reads the modeled SST `0x00bf/0x273f` software ID, but
  also uses the low ARM SWI vector for diagnostics while NOR flash
  commands/readback occupy the same address range. With blank modeled flash at
  zero, the first `svc 0x123456` vectors to `0x00000008` and fetches
  `0xffffffff`; an MMIO trace and follow-up MMAP register dump show AUPD does
  not program MMAP or enable local vector remap before this diagnostic, so this
  path stops before any Apple UI code.
- `tools/inspect_payload_refs.py` currently finds no ARM literal-load, `adr`, or
  nearby function-start references from extracted `osos` code to the early
  `booting!`, `IsyS`, or `SysI` marker strings. The few `booting!` address
  matches are raw aligned table/data hits, plus unaligned coincidences, so they
  are not treated as a native handoff producer.
- Apple verbose mode now has a read-only early handoff probe around
  `0x00001388..0x000013b4`. It records the PP selector, selected fast-RAM
  handoff slot, handoff tag, sysinfo pointer, and whether that pointer is
  RAM-backed. `smoke_apple_handoff_probe` verifies the current direct `osos`
  path still sees the untouched RAM fill pattern rather than seeded sysinfo.
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
  and analysis files, but no obvious stock boot ROM image; CTest runs
  `tools/audit_boot_sources.py` to keep that boot-source classification checked;
- complete IDE command sequencing;
- broader PMU/RTC/input-device register behavior once native firmware probes
  more of those devices;
- more exact cache and memory-controller side effects once native boot code
  requires them;
- scripted input injection.

See `BENCHMARKS.md` for current timing and framebuffer smoke results.
