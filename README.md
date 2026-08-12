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

See `docs/unicorn_emulator.md` for the current Rockbox emulator runbook,
web-control endpoints, verified plugin status, and caveats.

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
  --web 8080 \
  --ppm tmp/rockbox-menu.ppm

build-mingw/nano1g --profile apple \
  --firmware ../artifacts/firmware/apple_nano_14.5.3.1_fw.bin \
  --disk ../artifacts/images/ipodhd-apple-nano-sysinfo-preferences-probe.img \
  --max-insns 175000000 \
  --ppm tmp/apple-language.ppm
```

Open `http://127.0.0.1:8080/` while the emulator is running. With `--web`
enabled, the process keeps serving the final LCD frame after execution stops;
use Ctrl+C to exit, or add `--web-no-hold` for script/test runs.

To run Rockbox with the content-injected disk (adds `.rockbox/rocks/` plugins,
including `calculator.rock`, plus `Music/`) and a mocked half-charged,
unplugged battery:

```bash
python tools/make_gpt_rockbox_disk.py \
  ../artifacts/images/ipodhd-rockbox-nano-content.img \
  tmp/ipodhd-rockbox-nano-content-gpt.img

build-mingw/nano1g --profile rockbox \
  --firmware ../artifacts/firmware/rockbox.ipod \
  --disk tmp/ipodhd-rockbox-nano-content-gpt.img \
  --run-forever \
  --slice-insns 512 \
  --timer-divider 1 \
  --battery-percent 50 \
  --web 8080 \
  --ppm tmp/rockbox-content.ppm
```

The content disk is produced by `clicky/scripts/rawhd/inject_rockbox_content.sh`
and is a local, gitignored fixture under `../artifacts/images/`; regenerate it
if missing (see that script for `PLUGIN`/`MUSIC_DIR` inputs).

Useful options:

- `--run rockbox|ipodlinux|apple-stage0|apple-direct|apple-official`: start from a named
  preset. `apple-official` is the real cold-boot route. `apple-flash` remains
  accepted as a compatibility alias for the same path.
- The `ipodlinux` preset directly loads the local ZeroSlackr kernel and
  userland fixtures from `../artifacts/ipodlinux/`. It is an early bring-up
  target: the kernel reaches platform startup but does not render a UI yet.
- `--boot-mode direct|flash`: direct loads `--firmware` into guest RAM; flash
  maps `--flash-rom` at reset vector `0x00000000` and does not preload `osos`.
- `--firmware-from-disk`: in direct mode, find a wrapped firmware image in the
  loaded disk image and load its `osos` entry instead of using `--firmware`.
- `--disk-out PATH`: opt into a mutable output image. The source passed with
  `--disk` remains untouched; guest changes are saved before browser preset
  restarts and at clean exit. Restarting the owning preset, or an Apple preset
  that shares the same source disk, reloads the output. Unrelated presets keep
  their own disks and cannot overwrite it.
- `--flash-rom PATH`: load a NOR/boot-ROM image, required with
  `--boot-mode flash`.
- Web preset `Apple official boot` uses `NANO1G_APPLE_BOOTROM` when set, otherwise
  `../artifacts/firmware/apple_nano_1g_bootrom.bin`. This preset is the
  intended official cold-boot route, but it requires a real Apple Nano 1G flash
  ROM dump and will fail cleanly until one is available. Updater ZIPs and
  wrapped `osos`/`rsrc`/`aupd` firmware bundles are rejected for this path. The
  preset enables `--virtual-memmap` because the reset-vector path is expected to
  copy into SDRAM and remap logical zero while flash remains readable as data.
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
- `--rtc-usec-per-tick N`: microseconds added to the free-running RTC at
  `0x60005010` per device tick, default `1`. When running large
  `--slice-insns` values, set this near the slice size so guest delay loops and
  bootloader menu timeouts still make progress without returning to
  `--slice-insns 1`.
- `--pcf-state PATH`: persist the battery-backed PCF50605 register bank, RTC,
  and alarm programming in a versioned state file. On the next emulator
  process launch, the RTC advances by the host time elapsed while the emulator
  was stopped. Writes use a temporary file followed by replacement so a
  partial save does not destroy the previous state.
- `--ram-fill-zero`: initialize guest RAM with zeroes instead of the default
  diagnostic `0x2d` pattern. This is only a reset-state experiment knob; it
  does not seed Apple boot metadata or firmware-owned structures.
- `--apple-diagnostics`: enable the read-only Apple progress hooks and counters
  used by diagnostic smoke tests. Normal runs omit these hooks so they do not
  split hot translated blocks or distort firmware timing.
- `--verbose`: enable the fuller Apple probe/logging hook set and extra tracing;
  it implies Apple diagnostics.
- `--host-profile`: report high-resolution host time spent in CPU, COP,
  device-tick, input, web, and residual run-loop work, followed by the busiest
  exact MMIO addresses. High-resolution timing and histogram collection are
  disabled unless this option is enabled.
- `--trace-pc`: log translated basic blocks. This is very slow.
- `--trace-mmio`: log MMIO accesses. This is very slow.
- `--load-addr ADDR`: override firmware load address, default `0x10000000`.
- `--entry ADDR`: override initial PC, default follows `--load-addr`.
- `--input SCRIPT`: deterministic scripted button/wheel injection, delivered
  one event per device tick from the main loop (`src/input_script.c`).
  Comma-separated tokens:
  - `wait:N` - pause N device ticks before the next event.
  - `NAME-down` / `NAME-up`: press/release a button (`select`, `left`/`prev`,
    `right`/`next`, `play`/`down`, `menu`).
  - `hold-down` / `hold-up`: engage/disengage the active-low physical Hold
    switch through GPIOA, including its interrupt and input-suppression path.
  - `NAME` (bare): press, hold `2000` ticks, release.
  - `wheel:+D` / `wheel:-D`: move the click wheel by `D` raw units (the web
    frontend's `wheel=down`/`wheel=up` buttons send `+4`/`-4`).
  - `frame:LABEL`: write a deterministic LCD checkpoint beside `--ppm`; for
    example, `--ppm tmp/run.ppm` and `frame:main` produce
    `tmp/run-main.ppm`. This serializes native framebuffer state without
    changing guest execution.

  Timing is finicky and was calibrated empirically against the Rockbox
  build in `../artifacts/firmware/rockbox.ipod` (see `BENCHMARKS.md`,
  2026-07-09 plugin-launch entry, for the full worked example):
  - A press shorter than roughly 5000-20000 ticks is not registered as a
    short-press "enter"; a press held past ~50000+ ticks with no release
    starts reading as a long-press (context menu / Quick Screen) instead.
    Hold 20000-50000 ticks for reliable short-press navigation.
  - Click-wheel scroll sensitivity is not constant across a run - Rockbox's
    own scroll acceleration means later wheel bursts in the same session
    move the cursor further per event than earlier ones did. Calibrate
    per-list rather than assuming a fixed ticks-per-item ratio.
- `--battery-percent N`: mock the PCF PMU battery ADC (`ADCS1`/`ADCS2`,
  `src/dev_i2c.c`) to read as `N` percent (0-100), interpolated from
  Rockbox's real IPOD_NANO discharge-voltage table so the status-bar battery
  icon and low-battery/shutoff logic respond correctly. Default `100`.
- `--main-charger` / `--usb-charger`: report the FireWire/main or USB charger
  as connected on `GPIOL_INPUT_VAL` (`src/dev_gpio.c`), matching real
  hardware's `power_input_status()` bits. Default: neither connected. Every
  other GPIO input pin still reads idle-high (`0xffffffff`), unrelated to
  these two flags.
- `--hold-switch`: start with the active-low GPIOA Hold switch engaged. The
  optical wheel controller suppresses buttons and wheel movement while held,
  and firmware receives the native GPIO interrupt on each switch transition.
- `--web PORT`: serve a local browser frontend on `127.0.0.1:PORT`. The page
  polls native emulator counters and the LCD framebuffer as a BMP image, and
  exposes `/input?button=NAME&state=down|up` and `/input?wheel=down|up` for
  live interactive control (used by the click wheel UI on the page). Enable the
  page's Audio checkbox to start Web Audio output after a browser user gesture.
  The Battery slider and FireWire, USB power, and Hold checkboxes update live
  guest-visible hardware through `/hardware`; those external states survive a
  browser preset restart.
  Guest writes to the PP502x PWM channel and Nano dimmer/GPIOL power path drive
  the displayed backlight level. Backlight changes advance `frame_seq`, and
  `/frame.rgba` plus `/frame.bmp` apply the current intensity to the native LCD
  pixels without changing the guest framebuffer.
  `/audio.pcm?cursor=N` exposes a bounded, cursor-based stereo signed-16-bit PCM
  stream. The status feed reports codec output state, sample rate, nonzero and
  silenced sample counts, peak level, underruns, host drops, and the existing
  I2S/DMA counters so playback progress is visible in scripted checks.
- `--web-no-hold`: when `--web` is enabled, exit immediately after emulation
  instead of keeping the final frame available in the browser.

Useful audit tools:

- `tools/inspect_boot_sources.py`: compare updater ZIP, wrapped firmware, and
  disk firmware payloads while labeling boot/sysinfo-looking byte patterns by
  source region.
- `tools/audit_boot_sources.py`: classify local boot-source candidates as
  wrapped firmware bundles, disk/container images with embedded wrapped
  firmware, updater-style flash images, or raw reset-vector candidates without
  treating any of them as Apple boot ROMs by guesswork. Use
  `--zip-member all` to enumerate every updater ZIP member.
- `tools/find_bootrom_candidates.py`: recursively scan fixture directories and
  ZIP members for raw reset-vector candidates while reporting wrapped firmware
  bundles and disk/container images separately.
- `tools/inspect_aupd_batch.py`: scan files for the native AUPD updater's
  `FwUp` 28-byte command records and required `!dnE` end marker.
- `tools/inspect_aupd_layout.py`: decode the decrypted AUPD reset relocation
  constants and probe the parser destination area.
- `tools/inspect_payload_refs.py`: scan extracted Apple payloads for word-aligned
  raw references, ARM literal-load references, and simple `adr` references to
  boot/sysinfo marker strings without treating unaligned byte coincidences as
  code evidence.
- `tools/inspect_fat32.py`: inspect the FAT32 data partition and map absolute
  disk offsets back to file paths. It can also extract files by path with
  `--extract PATH --output FILE`.
- `tools/inspect_sysinfo.py`: inspect an extracted `iPod_Control/Device/SysInfo`
  file and print the `IsyS` tag, declared size, board string, serial string, and
  native `osos` model word at `+0xe0`.
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
- PP502x PWM channel 1 and the Nano channel-8 pulse dimmer, including Apple’s
  GPIOL bit-7 backlight power gate. The browser status feed and rendered frame
  reflect guest-selected power and brightness.
- PP502x PWM channel 0 decodes the Nano piezo enable, duty, and period fields;
  native clicker start/stop edges are available in the run summary and web
  status feed. The opt-in browser audio path recreates completed PWM pulses at
  the firmware-selected pitch without mixing them into codec DMA.
- XMB RAM self-refresh request/status behavior at `0x7000003c`, allowing
  Apple’s low-power transition routine to complete instead of spinning in its
  copied fast-RAM wait loop.
- Boot reset loader that loads the requested firmware image into guest memory
  and initializes CPU handoff registers.
- Cache-control and memory-controller MMIO ranges are decoded through the
  central bus and preserve byte, halfword, and word register accesses.
  Cache-control writes also flush Unicorn translation blocks so native guest
  cache-maintenance operations can affect translated code.
- Timer and interrupt-controller reads preserve byte, halfword, and word
  access semantics; a native ARM smoke covers timer expiry, interrupt status,
  timer-value acknowledge, and one-shot disable behavior.
- The free-running PP502x usec RTC at `0x60005010` has an explicit
  `--rtc-usec-per-tick` scale so native bootloader delay loops do not become
  artificially slow when Unicorn runs larger instruction slices. The default is
  still `1` for compatibility; `smoke_timer_rtc_scale` covers the scaled path.
- Apple LCD DMA records accepted and mismatched descriptor geometry. Native
  acceptance asserts every completed transfer had equal descriptor and active
  block pixel counts; calibrated input checkpoints preserve hash-checked
  Language, main-menu, and Extras settled frames from one execution. A separate
  750-million-instruction stress gate repeats Extras scrolling, Menu exits, and
  native re-entry three times without resetting accumulated hardware state.
- Apple's second DMA register bank at `0x60008000..0x60009fff` is routed with
  independent master state and two ready service channels, matching the native
  firmware's controller construction and startup reset sequence. Its eventual
  transfer targets remain deliberately unmodeled until traces identify them.
- Headless LCD PPM output.
- Local browser frontend via `--web PORT`, backed by the same native LCD
  framebuffer that PPM output uses, with live Rockbox input controls and audio
  activity counters.
- Rockbox canary reaches a nonblack framebuffer.
- Rockbox core reaches the main menu with native firmware execution using the
  GPT-wrapped disk fixture, `--slice-insns 512`, and `--timer-divider 1`.
- Rockbox can browse the content-injected disk's `Music/` directory through the
  native Files UI; `tests/smoke_rockbox_music_browse.cmake` records the current
  manual regression path for the shipped MP3 fixtures.
- The modeled DMA-to-I2S path preserves RAM-backed 16-bit stereo samples,
  drains the TX FIFO against the codec-selected guest sample rate, applies the
  WM8975 output power/routing/mute/volume state, and raises DMA completion
  status. Empty enabled FIFOs preserve fractional clock phase and emit counted
  silence; codec rate changes start a new PCM stream epoch so the browser drops
  incompatible queued buffers. `audio_path_unit` and `i2c_codec_unit` cover
  that device-level path; the Apple playback smoke verifies nonzero PCM from
  the official firmware.
- The standalone Rockbox bootloader fixture runs from fast RAM and reaches a
  nonblack framebuffer without synthetic sysinfo handoff state.
- The local `../artifacts/firmware/bootloader.bin` fixture is useful as a
  native Rockbox bootloader canary, but it is not an Apple-aware loader. With
  RTC scaling enabled it times out into its native Rockbox load path and reports
  `File not found: /.rockbox/rockbox.ipod` on an Apple disk image. The separate
  `../clicky/resources/ipodloader2/loader.bin` image is Apple-aware, but direct
  execution of that loader currently faults early on the same missing
  boot-ROM/sysinfo handoff state (`0x2d2d2d2d`) that blocks direct Apple `osos`.
- Flash boot mode can load and map an external NOR/boot-ROM image at
  `0x00000000`. The current local fixtures do not include a stock Apple boot
  ROM dump; the Apple updater ZIP and HDD images contain wrapped firmware
  partitions instead of reset-vector NOR bytes.
- Direct boot can also source wrapped firmware from the disk image firmware
  partition, matching the local Apple/Rockbox media layout more closely.
- ATA Identify Device and PIO read-sector transfers latch the guest-visible
  command/LBA registers, expose a device-ticked BSY preparation phase, then
  assert DRQ and IRQ for each data block. Status reads acknowledge the IRQ;
  draining the final read block clears DRQ without generating a second
  completion interrupt. Native ARM smokes cover both one- and two-sector
  phase transitions.
- PIO writes expose first-block DRQ without IRQ, interrupt when subsequent
  blocks become writable, and enter a device-ticked BSY completion phase after
  the final data word. One- and two-sector ARM smokes verify the status/IRQ
  sequence and read the written data back through ATA.
- ATA non-data commands enter BSY and complete on a device tick with an IRQ;
  unsupported commands and disabled READ/WRITE MULTIPLE requests complete with
  ABRT. SET MULTIPLE MODE validates the requested block count, supports the
  ATA-defined zero-count disable operation, and updates IDENTIFY word 59. A
  native ARM probe covers disabled, enabled, and rejected negotiation paths.
- ATA Device Control SRST cancels active PIO, DMA, and non-data transfers,
  holds BSY while asserted, restores the ATA reset signature after release,
  clears multiple mode, and does not raise an interrupt. A native ARM probe
  verifies the transition and signature.
- Native Apple Preferences persistence is verified end to end: the firmware
  changes Repeat, executes its own writer and ATA sector updates, restarts from
  the mutable `--disk-out` snapshot, and renders the saved value after boot.
- PIO and DMA requests are bounded by the capacity reported in IDENTIFY words
  60-61. Transfers that begin beyond the medium or cross its final sector stop
  with IDNF and an IRQ, while the taskfile LBA/count identify the first failing
  sector. DMA copies only the valid prefix instead of fabricating data beyond
  the advertised medium.
- DMA-to-LCD2 transfers route through the modeled LCD2 register window and are
  covered by a native ARM smoke that produces real PPM pixels.
- The I2C/PMU path models basic PCF register pointer, default reads, guest
  writes, busy polling, and readback through native MMIO transactions. RTC and
  alarm state survive browser firmware restarts, while `--pcf-state` extends
  that battery-backed behavior across emulator process restarts.
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
- The PP502x USB controller window at `0xc5000000..0xc5000fff` is mapped as
  latched MMIO. Rockbox identifies this range as `USB_BASE`; Apple probes
  `USB_BASE+0x184` during early UI bring-up, so mapping this as hardware MMIO
  avoids an unmapped-memory fault without shimming firmware behavior.
- Flash boot can now be combined with `--virtual-memmap`; the Apple official
  preset enables it by default. A native smoke starts
  at the flash reset vector, copies code into SDRAM, programs PP MMAP, and
  verifies Unicorn fetches virtual zero from SDRAM while data reads still see
  modeled NOR flash. This is the mode a real Apple boot ROM path is expected to
  need.
- The Unicorn SWI hook logs Apple updater `svc 0x123456` diagnostic text in
  verbose mode without replacing guest exception handling.
- Apple `osos` direct boot starts without instruction shims, synthetic
  sysinfo/model RAM, or synthetic framebuffer output. It intentionally remains
  a diagnostic path: with the current local artifacts it stops after 384 guest
  instructions at `0x000013b4` because the real boot metadata/sysinfo producer
  has not run. The official cold-boot target is `--boot-mode flash` with a real
  Nano 1G ROM dump.
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
- `tools/inspect_osos_handoff.py` checks the native `osos` opcodes that define
  this contract: Nano selector `0x36` chooses fast-RAM slot `0x4001ff18`, the
  expected tag is `0x53797349` / `IsyS`, the sysinfo pointer is read from
  `[handoff+0x4]`, and the first required model word is read from
  `[sysinfo+0xe0]`. That word is bucketed by native code at `0x0e6c` using
  thresholds `0, 0x80000, 0x100000, ... 0x8000000`, then packed into bits 8..11
  of the early boot config word. The current blocker is therefore a missing
  native producer for that handoff/sysinfo state, not an unknown Apple OS entry
  address.
- The `ipodhd-apple-nano-sysinfo-preferences-probe.img` fixture contains a real
  FAT file at `iPod_Control/Device/SysInfo`. `smoke_disk_sysinfo_contract`
  extracts it and verifies `tag=IsyS`, declared size `0x184`, board `nano1g`,
  and model word `0x02000000` at `+0xe0`. This is useful evidence for the data
  Apple expects, but it is not itself a boot path: current native execution does
  not yet load that file into RAM or publish `[0x4001ff18]` through guest code.
- `smoke_native_sysinfo_handoff` proves the modeled hardware path can do that
  publication without host-side HLE: a tiny native ARM probe reads the real FAT
  `SysInfo` sector through ATA into fast RAM at `0x40018000`, then writes
  `IsyS` and the pointer into `0x4001ff18`. This is a boot-stage capability
  check, not an Apple Language-screen success condition.
- `smoke_stage0_sysinfo_osos` extends that no-HLE canary into Apple firmware:
  a native ARM stage0 reads the real FAT `SysInfo` sector, publishes the Nano
  fast-RAM handoff table, loads the full wrapped Apple `osos` payload from the
  disk firmware partition through modeled PP502x ATA DMA, then branches into
  `osos`. The smoke
  verifies the native Apple handoff probe sees `IsyS`, the real SysInfo pointer,
  and model word `0x02000000`. With USB MMIO mapped, the same smoke now reaches
  the native language loop, view delivery, and LCD dirty/post path. It still is
  not a Language-screen success condition because no native LCD flush reaches
  the modeled LCD yet.
- CTest smoke coverage verifies Rockbox nonblack framebuffer output, menu
  navigation, plugin loading, and the DMA/I2S audio path. The stage0-assisted
  Apple acceptance path stays native/no-HLE, hash-checks settled Language,
  main-menu, and Extras frames, and stress-tests repeated navigation. This is
  interactive Apple UI success, not a stock reset-vector cold boot.
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

Still expected before full official-firmware parity:

- native boot ROM/bootloader path, or modeled hardware media path, that produces
  Apple boot metadata/sysinfo through guest execution;
- exact interrupt/timer semantics under larger `--slice-insns` values, beyond
  the currently modeled free-running RTC scale knob;
- native Apple boot ROM or flash dump. The current local artifact set still
  contains Apple wrapped firmware/disk images, patched experiments, screenshots,
  and analysis files, but no obvious stock boot ROM image; CTest runs
  `tools/audit_boot_sources.py` across the updater ZIP, firmware fixture,
  bootloader fixture, and Apple disk images, and runs
  `tools/find_bootrom_candidates.py` across the local firmware/image fixture
  directories, to keep that boot-source classification checked;
- complete IDE command sequencing;
- broader PMU/RTC/input-device register behavior once native firmware probes
  more of those devices;
- more exact cache and memory-controller side effects once native boot code
  requires them;
- scripted input injection.

See `BENCHMARKS.md` for current timing and framebuffer smoke results.
