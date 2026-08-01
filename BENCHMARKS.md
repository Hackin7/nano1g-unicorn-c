# Benchmarks

Measurements from the current Windows workspace. Firmware and disk images are
loaded from `../artifacts/` and are not committed to this repo.

For future ad hoc runs, write generated PPM/log outputs under `tmp/`, which is
gitignored. Historical commands below may show root-level output names from
earlier bring-up runs.

## 2026-08-02: Native backlight and Apple timeout wake

The previously unrouted PP502x PWM bank at `0x7000a000` and Nano pulse-dimmer
bank at `0x7000c300` now have guest-visible register models. Rockbox-style PWM
channel 1 uses its enable and duty fields; Apple’s Nano path tracks channel-8
width-127/width-1 pulses, the 0-32 level, and GPIOL bit 7 as the physical power
gate. Browser BMP/RGBA output scales native LCD pixels by that hardware state,
and backlight transitions advance `frame_seq` even when LCD GRAM is unchanged.

A live Apple stage0 run navigated Language -> Settings -> Backlight Timer,
selected two seconds, and observed `mode=nano`, `on=false`. The first run then
stalled at copied fast-RAM PC `0x40000534`; matching those bytes in the Apple
IDB identified `sub_534C4C`, which requests XMB RAM self-refresh by setting bit
22 at `0x7000003c` and waits for status bit 30. Modeling that request/status
handshake lets the routine return. A subsequent native Menu tap was consumed
with an empty optical queue and changed the backlight to `on=true` before the
two-second timer expired again.

`backlight_unit`, `ppcon_unit`, and `smoke_backlight` cover device behavior and
native MMIO routing. `smoke_web_backlight` runs a guest ARM LCD/PWM probe through
the real HTTP server and verifies lit, extinguished, and restored RGBA frames,
status fields, and frame sequencing. The focused six-test gate passed in 2.98
seconds. The post-change native acceptance pair also passed:
`smoke_rockbox_menu` in 7.66 seconds and `smoke_apple_menu_navigation` in
164.94 seconds. The final serial regression gate passed all 69 tests in 545.71
seconds; its Apple menu and audio cases took 201.00 and 197.35 seconds,
respectively.

## 2026-08-02: Native Hold switch and runtime power controls

The physical Hold switch is now modeled as active-low GPIOA bit 5. Transitions
latch the configured GPIO level, assert PP502x high interrupt source 0 (IRQ
32), and follow the guest's level-toggle/status-clear acknowledgement flow.
Engaging Hold releases any pressed optical-wheel button and suppresses later
button and wheel packets until release. Main/FireWire and USB charger changes
use the same runtime GPIO edge path, while the existing PCF battery ADC can now
be adjusted without restarting.

`gpio_power_unit` covers active-low levels, both Hold edges, IRQ assertion and
acknowledgement, pressed-button release, suppression counters, charger levels,
and charger interrupts. `smoke_web_hardware_controls` drives the real HTTP
server, checks initial options survive preset application, mutates all four
controls, verifies held browser input is suppressed, preserves the external
state through a browser preset restart, and rejects an invalid battery
percentage. The focused five-test gate passed in 2.23 seconds.

The Apple menu acceptance route now engages Hold after navigating Language ->
main menu -> Extras. Native firmware renders the lock icon with final pixel
SHA-256 `5207b561de71793b8e1998d3f5f7bc7182b0773b5957992493c74886e0346a5f`;
the 315-million-instruction capture completed with 55 LCD DMA transfers and no
LCD block overruns.

The final serial regression gate passed all 65 tests in 507.62 seconds.
`smoke_apple_menu_navigation` took 164.89 seconds and
`smoke_web_hardware_controls` took 1.44 seconds in that run.

## 2026-08-02: Browser disk persistence and preset isolation

`--disk-out` previously saved only at final process exit. A web Restart first
destroyed the current device state, losing guest writes, while preset
application also discarded the output path. Disk persistence now records its
owning source path and preset, saves before every owning-preset teardown, and
reloads the mutable output on restart. Presets with unrelated disks retain
their normal media and do not save over that output. Runs without `--disk-out`
still modify only the in-memory copy.

`smoke_web_disk_persistence` starts a live HTTP server around a native ARM ATA
writer. It observes 256 data-register writes through `status.json`, requests a
Rockbox restart, verifies the source sector is unchanged and the output begins
with guest marker `22 11 44 33`, and confirms the output is reloaded. It then
switches to the 8 MiB iPod Linux disk and back, proving the 512-byte Rockbox
output remains owned and intact. A second finite run verifies the clean-exit
save path while preserving its separate seed image.

The final serial gate passed all 63 tests in 490.06 seconds; the persistence
smoke test took 1.12 seconds.

## 2026-08-02: ATA media bounds and failing-LBA taskfile state

PIO reads beyond the backing medium previously returned `0xffff`, PIO writes
were silently dropped, and DMA reads synthesized `0xff` bytes while every path
reported success. The model now bounds 28-bit media commands by the capacity
reported in IDENTIFY words 60-61. An initially invalid LBA enters BSY and
completes with `ERR+IDNF`; a multi-sector command transfers valid sectors, then
terminates at the first invalid LBA. LBA, Device, and Sector Count taskfile
registers advance with completed sectors and retain the failing address and
residual count on error. DMA copies only the valid prefix and reports controller
error state rather than touching RAM beyond the medium.

`smoke_ata_bounds` executes initial-invalid and boundary-crossing PIO read,
PIO write, and DMA read cases as native ARM code against a one-sector disk. It
checks BSY/DRQ/error phases, IDNF, IRQ delivery, failing LBA 1, residual count
1, a 512-byte DMA-address advance, and the transferred prefix. All eleven ATA
tests passed, followed by all 62 registered regressions in 485.34 seconds.

## 2026-08-02: ATA non-data phases and multiple-mode negotiation

SET FEATURES, SET MULTIPLE MODE, STANDBY IMMEDIATE, FLUSH CACHE, and rejected
commands previously completed synchronously inside the command-register write.
They now expose BSY immediately, complete on the next device tick, and raise a
completion IRQ that a Status read acknowledges. Rejected commands finish with
ERR and ABRT instead of publishing an immediate error state.

The disk now resets with multiple mode disabled and advertises word 59 as
`0x0100`. A valid SET MULTIPLE MODE count of one changes it to `0x0101`; count
zero disables the mode, and a count above IDENTIFY word 47's advertised maximum
is rejected without changing the active setting. READ/WRITE MULTIPLE commands
ABRT until a successful nonzero negotiation.

`smoke_ata_nondata_phase` observes SET FEATURES transition from `0x80` to
`0x50`, completion IRQ assertion, Status acknowledgement, and a clear Error
register. `smoke_ata_multiple_mode` verifies the disabled-command ABRT path,
word 59 before and after negotiation, successful READ MULTIPLE data, and state
preservation after an invalid count. All ten focused ATA tests passed. Eight
firmware-level checks also passed, including Apple menu navigation, Apple
audio, Apple no-HLE/handoff paths, and Rockbox boot, core, and menu tests.
The final serial regression gate passed all 61 tests in 498.73 seconds.

## 2026-08-02: ATA PIO data-out phase timing

PIO writes previously exposed `DRQ+IRQ` synchronously with the command write,
kept DRQ asserted across sectors, and completed in the same Data-register
access as the final word. The device now enters BSY for each preparation phase.
The first writable block exposes DRQ without IRQ; subsequent writable blocks
expose DRQ with IRQ; after the final word, a separate device tick clears BSY
and raises the command-completion IRQ.

`smoke_ata_pio_write_phase` verifies the single-sector command, first-block,
completion, Status-acknowledgement, and readback sequence. The two-sector
`smoke_ata_pio_write_multisector` additionally verifies the interrupting second
block boundary and reads sector one back through a PIO read command.

## 2026-08-02: ATA PIO data-in phase timing

ATA Identify Device and READ SECTORS previously exposed `DRQ` and raised IDE
IRQ synchronously inside the command-register write. READ SECTORS also kept
`DRQ` asserted across sector boundaries and raised another IRQ after the final
word. The model now enters `BSY` on command receipt, exposes `DRQ+IRQ` on the
next device tick, repeats that phase at each READ SECTORS block boundary, and
finishes without a second read-completion interrupt.

`smoke_ata_pio_phase` observes the command phase, data-ready IRQ, Status
acknowledgement, first data word, and final idle state from native ARM code.
`smoke_ata_pio_multisector` repeats the checks across a two-sector command and
verifies that each sector has a distinct `BSY -> DRQ+IRQ` transition.

A 20,000,000-instruction verbose Apple stage0 run issued five SET FEATURES
commands and twenty `0x20` READ SECTORS commands; every observed read requested
one sector. With the phased PIO model, all twenty reads completed and the
checkpoint framebuffer remained byte-identical to the prior run:
`05C3CAF0B619476F51D428EEE4C779CAD35C15D4FFDC14AEC8827DE263A65AA4`.

## 2026-08-02: Host profiling and native DMA stage0

Added opt-in `--host-profile` timing around CPU, COP, device-tick, input, web,
and residual run-loop work, plus a 4096-slot exact-address MMIO histogram. The
option does not install code hooks or alter guest state. The fixture-free ATA
DMA smoke verifies both the timing summary and MMIO ranking output.

A 175,000,000-instruction Apple stage0 run showed that 30.361 of 30.465 profiled
seconds (99.66%) were inside CPU slice execution. Device ticks consumed only
0.065 seconds. The first complete MMIO profile found 3,162,370 reads of the ATA
data port across its convenience address `0xc3000000` and hardware task-file
address `0xc30001e0`, out of 3,342,906 total MMIO reads. A direct EIDE dispatch
experiment measured 30.355 seconds against a 30.372-second baseline, which was
noise; that code was removed rather than retaining an ineffective fast path.

The dominant `0xc3000000` traffic came from the native ARM stage0 canary loading
the 10,872-sector `osos` payload one halfword at a time. The canary now programs
the modeled PP502x ATA DMA control, length, destination, task-file, and READ DMA
command registers in chunks of at most 256 sectors, then polls command status
until the device tick completes each transfer. This remains guest-executed ARM
code and does not copy firmware through a host shim.

The DMA canary reaches a valid `IsyS` handoff within a 100,000-instruction run
(0.388 seconds in the measured diagnostic invocation), versus more than 11
million guest instructions inherent in the old PIO copy loop. At five million
instructions it reported the same 2,788,096 disk words transferred while ATA
MMIO traffic fell to roughly 12,000 accesses. A 175,000,000-instruction run
produced the same framebuffer SHA-256 as the PIO canary:
`258B8B954716187DCAB9C8807D6F1CA01F57357B501A5658C9D8E23BAB2BD822`.

The Apple firmware later performs its own PIO through `0xc30001e0`; that traffic
remains modeled exactly. Fixed total-instruction wall times after this change
are not directly comparable because removing stage0 copy instructions gives
the Apple firmware more of the same instruction budget.

## 2026-08-02: Apple diagnostic-hook isolation

Normal Apple runs previously installed roughly sixty read-only
`UC_HOOK_CODE` ranges on both the CPU and COP. Several ranges covered hot
scheduler and UI paths, splitting translated blocks and invoking host callbacks
even when neither `--verbose` nor tracing was requested. The hooks only read
guest registers, RAM, and device state; their writes are confined to host-side
diagnostic counters.

The progress hooks are now opt-in with `--apple-diagnostics`; `--verbose`
continues to install the fuller diagnostic set. An 80,000,000-instruction
Apple stage0 run using `--slice-insns 512 --rtc-usec-per-tick 8` measured:

| Mode | Wall time | Throughput |
| --- | ---: | ---: |
| Previous always-on progress hooks | 19.323 s | 4.140 MIPS |
| Normal run, hooks omitted | 5.662 s | 14.130 MIPS |
| Explicit `--apple-diagnostics` | 19.434 s | 4.116 MIPS |

The normal path is about 3.4 times faster in this early-boot sample. This is
still below the preset's modeled 64 MIPS rate, so it removes a major source of
observer-induced timing distortion rather than establishing real-time
execution.

For behavioral equivalence, matching 175,000,000-instruction runs with and
without `--apple-diagnostics` produced byte-identical PPM files with SHA-256
`258B8B954716187DCAB9C8807D6F1CA01F57357B501A5658C9D8E23BAB2BD822` and
identical guest instruction, tick, MMIO, LCD, DMA, disk, IRQ, PC, audio, and
core-state summaries. The Apple menu-navigation smoke explicitly opts into the
progress counters and still passes.

## 2026-07-10: Web tap controls + calculator plugin demonstration

Rockbox was stalling in a cache-maintenance MMIO loop after writing
`0x6000c000`; the Unicorn MMIO callback now advances the guest PC before
stopping for a translation-block flush, which lets execution resume at the next
instruction instead of replaying the same store forever.

The browser controls now use `/input?button=NAME&tap=1` for click-wheel button
taps. The emulator holds the virtual button for a calibrated number of device
ticks and releases it from the tick loop, matching the timing Rockbox expects
for a short press. Manual verification opened the Rockbox `Plugins` menu from
the web UI and captured `build-mingw/web-click-open.bmp`.

The calculator plugin was launched from the content disk via
`Plugins -> Applications -> calculator`; the verification capture is
`build-mingw/calculator.bmp`. Additional native plugin launches observed during
calibration included `cube.rock`, `2048`, `calendar`, and `battery_bench`.

## 2026-07-09: Battery ADC mock + GPIO charger-detect fix

Added `--battery-percent N` (0-100), `--main-charger`, and `--usb-charger`
CLI flags.

**Battery ADC (`src/dev_i2c.c`):** the PCF PMU's `ADCS1`/`ADCS2` I2C
registers previously had no working battery model — `ADCS1` (0x30, upper 8
bits of the 10-bit reading) wasn't implemented at all, and `ADCS2` (0x31) was
hardcoded to `0x82`, decoding to a near-zero raw ADC value. Fixed by
interpolating Rockbox's real IPOD_NANO `percent_to_volt_discharge` table
(`firmware/target/arm/ipod/powermgmt-ipod-pcf.c` upstream: 0%=3230mV,
10%=3620mV, ..., 100%=4160mV, shutoff at <=3230mV) and inverting
`battery_adc_voltage()`'s `mv = (raw * 6000) >> 10` to get the raw ADC value
for a requested percent. An earlier attempt that scaled linearly from 0mV at
0% was wrong — real 0% is 3230mV, so a naive 50% guess landed below the
actual shutoff floor and triggered Rockbox's "Battery empty! RECHARGE!
Shutting down..." screen instead of the menu. Default (no flag) is 100%.

**GPIO charger-detect (`src/dev_gpio.c`):** every GPIO input-value register
was (and, for all pins except the one below, still is) hardcoded to read
`0xffffffff` regardless of which pin — a blanket idle-high stub. This
included `GPIOL_INPUT_VAL` (offset 0x13c off `N1G_GPIO_BASE`), which real
iPod Nano 1g hardware wires to charger presence
(`power-ipod.c::power_input_status()`: bit 3/0x08 = main/FireWire charger,
active low; bit 4/0x10 = USB charger, active high). The blanket stub left
bit 4 stuck high, so Rockbox always believed a USB charger was connected —
this is what was masking the battery-ADC bug above (Rockbox suppresses the
percentage-fill icon while charging) and is a plausible reason nobody had
hit the near-zero default battery value before. `--main-charger` /
`--usb-charger` now drive those two bits explicitly; default is neither
connected. All other GPIO input pins are unaffected and still read
`0xffffffff` (locked in by `smoke_gpio_idle_inputs`).

Verified against the Rockbox main-menu status bar (`--disk
tmp/ipodhd-rockbox-nano-content-gpt.img`): default boot shows no charging
glyph; `--battery-percent 10` renders a nearly-empty icon, `--battery-percent
50` a half icon, `--battery-percent 90` a nearly-full icon — matching the
requested percentage. Full 44-test ctest suite passes throughout.

## 2026-07-09: Rockbox plugin launch (native, no-HLE)

Added deterministic scripted input injection (`src/input_script.c`,
`include/nano1g/input_script.h`) wired into the `--input` flag, which was
previously parsed but never delivered to the guest (`src/main.c`). The grammar
is `wait:N` (device ticks), `NAME-down`/`NAME-up`, bare `NAME` (tap), and
`wheel:+D`/`wheel:-D`.

This was driven by a `/goal`-directed session testing Rockbox interactively
through the web frontend, then reproducing the same navigation deterministically
headless. Two calibration facts fell out of that process, both specific to this
firmware build and worth keeping in mind for future scripted input:

- Button presses shorter than ~5000-20000 ticks are not registered as a
  short-press "enter"; presses held well past that (tens of thousands of ticks
  with no release) start reading as a long-press (context menu / Quick Screen)
  instead. Scripts that need reliable "enter" semantics should hold for at
  least 20000-50000 ticks before releasing.
- Click-wheel scroll sensitivity is not constant across a run: Rockbox's own
  scroll acceleration means repeated wheel bursts later in the same session
  move the list cursor much further per wheel event than earlier bursts did
  (e.g. 14 `wheel:4` events moved 1 item in the root menu, but a later 28-event
  burst in a nested list moved roughly 10 items). Calibrate empirically per
  list rather than assuming a fixed ticks-per-item ratio.

Deterministic run that boots to the Rockbox main menu, browses
Plugins -> Demos, and launches `cube.rock` from the actual FAT-mounted content
disk (`ipodhd-rockbox-nano-content.img`, which carries a full `.rockbox/rocks/`
tree including `calculator.rock` and the demos/games/apps/viewers plugin set):

```bash
build-mingw/nano1g.exe --profile rockbox \
  --firmware ../artifacts/firmware/rockbox.ipod \
  --disk tmp/ipodhd-rockbox-nano-content-gpt.img \
  --max-insns 4500000000 --slice-insns 512 --timer-divider 1 \
  --input "wait:2000000,<14x wheel:4/wait:50000>,wait:100000,select-down,wait:50000,select-up,wait:100000,<28x wheel:4/wait:50000>,wait:100000,select-down,wait:50000,select-up,wait:100000,<5x wheel:4/wait:50000>,wait:100000,select-down,wait:50000,select-up" \
  --ppm tmp/rb-cube-launch.ppm
```

```text
input inject button=select state=down
input inject button=select state=up
summary guest_insns=152716800 ticks=8703131 mmio_r=28812144 mmio_w=2307965 lcd_words=396510 disk_reads=81664 irq=923 pc=0x00087d24
```

`disk_reads` rises from the 78592 boot/menu baseline to 81664 (loading
`cube.rock` off the FAT filesystem) and `lcd_words` climbs well past the plain
menu smoke's baseline, confirming real plugin-load I/O and LCD rendering. No
`invalid memory` or `uc_emu_start ... failed` appeared at any point; the run
returns cleanly to a normally-rendering root menu afterward (the plugin exits
almost immediately after launch, most likely because its first input poll
observes our own launch keypress's release edge as an "any key" exit condition
— demo plugins commonly treat any button activity as an exit trigger).

This is now a permanent regression test, `smoke_rockbox_plugin_demo`
(`tests/smoke_rockbox_plugin_demo.cmake`), asserting the disk-read and
LCD-activity thresholds above plus the absence of any fault/panic string, and
skipping cleanly if the content-disk fixture is absent.

## 2026-07-07

Build:

```bash
cmake --build --preset mingw-release
```

The build succeeds. GCC still warns about Unicorn hook callback casts under
`-Wpedantic`.

CTest after removing synthetic boot data and adding native bootloader, flash
boot, disk-firmware, boot-source audit, NOR command, GPIO atomic-write, and ATA
identify/read canaries, a DMA-to-LCD2 pixel-transfer canary, and
cache/memory-controller MMIO latch coverage, I2C/PMU register-pointer
read/write coverage, timer-to-interrupt-controller coverage, direct-mode
flash-at-zero coverage, EVP local-vector coverage, and a memory-controller MMAP
remap smoke. The current full run also includes a payload-reference smoke for
the extracted Apple `osos` marker scan and asserts the direct AUPD low-memory
flash-command probe plus the flash-zero SST ID path:

```text
ctest --test-dir build-mingw --output-on-failure
100% tests passed, 0 tests failed out of 26
Total Test time (real) = 22.85 sec
```

The Apple smoke test now asserts the no-HLE contract: no instruction shims, no
synthetic framebuffer messages, no UI/LCD context seeding from code hooks, and
no synthetic LCD descriptor dimensions. It does not assert that the Language
screen is reached or rendered yet.

### Rockbox Canary

Command:

```bash
python tools/time_command.py build-mingw/nano1g.exe \
  --profile rockbox \
  --firmware ../artifacts/firmware/rockbox_nano_fw.bin \
  --disk ../artifacts/images/ipodhd-rockbox-nano.img \
  --max-insns 20000000 \
  --slice-insns 512 \
  --ppm rockbox-sourcefill-timed.ppm
```

Result:

```text
elapsed_sec=0.655
summary guest_insns=13591552 ticks=39063 mmio_r=859385 mmio_w=69926 lcd_words=69732 disk_reads=0 irq=0 pc=0x40005448
ppm nonblack=21643 unique_colors=2
```

### Rockbox Bootloader Canary

Command:

```bash
build-mingw/nano1g.exe \
  --profile rockbox \
  --firmware ../artifacts/firmware/bootloader.bin \
  --disk ../artifacts/images/ipodhd-rockbox-nano.img \
  --load-addr 0x40000000 \
  --entry 0x40000000 \
  --max-insns 1000000 \
  --slice-insns 1 \
  --ppm bootloader-fast-1m.ppm
```

Result:

```text
summary guest_insns=1000000 ticks=1000000 mmio_r=48195 mmio_w=46655 lcd_words=46488 disk_reads=0 irq=0 pc=0x4000a288
ppm nonblack=21713 unique_colors=2
```

This is a native fast-RAM bootloader path. It does not use synthetic sysinfo
handoff state and is now covered by `smoke_bootloader`.

A longer `--max-insns 20000000 --slice-insns 512` run remains a display/CPU
canary, not a storage canary yet:

```text
summary guest_insns=13591552 ticks=39063 mmio_r=859386 mmio_w=69926 lcd_words=69732 disk_reads=0 irq=0 pc=0x40005448
ppm nonblack=21643 unique_colors=2
```

### Flash Boot Plumbing

Command:

```bash
build-mingw/nano1g.exe \
  --profile rockbox \
  --boot-mode flash \
  --flash-rom ../artifacts/firmware/bootloader.bin \
  --disk ../artifacts/images/ipodhd-rockbox-nano.img \
  --max-insns 1000 \
  --slice-insns 1 \
  --ppm flash-bootloader-map.ppm
```

Result:

```text
loaded flash ROM ../artifacts/firmware/bootloader.bin size=50484 at 0x00000000
boot reset profile=rockbox mode=flash entry=0x00000000 cpsr=0x000000d3 svc_sp=0x40016ffc
summary guest_insns=1000 ticks=1000 mmio_r=0 mmio_w=0 lcd_words=0 disk_reads=0 irq=0 pc=0x00000000
```

The local `bootloader.bin` fixture is linked for fast RAM, so this is only a
mapping/CLI canary. It proves that flash mode no longer preloads `osos` into
SDRAM and can map external NOR bytes at the reset vector. A stock Apple boot ROM
dump is still needed for a real Apple no-HLE boot handoff.

`smoke_flash_commands` assembles a tiny ARM probe that copies its flash-command
routine into fast RAM, sends the NOR read-ID command, and verifies the guest sees
the SST manufacturer ID used by the native AUPD flash table:

```text
dump32 addr=0x40000100 0x000000bf ...
```

`smoke_map_flash_zero` assembles a tiny fast-RAM direct-mode probe, enables
`--map-flash-zero`, sends the NOR read-ID command to address zero, then returns
to read-array mode:

```text
dump32 addr=0x40000100 0x000000bf 0xffffffff
```

`smoke_disk_firmware` proves direct boot can source `osos` from the firmware
partition inside the loaded disk image instead of from a sidecar firmware file:

```text
loaded wrapped osos fmt=3 file_off=0x4600 addr=0x10000000 entry=0x10000000 len=5566256
loaded disk wrapped firmware image_start=0x100000
```

`smoke_gpio_atomic` assembles a tiny ARM probe that writes the PP502x GPIO
`+0x800` atomic bitwise mirror and verifies the base GPIO register updates with
mask/data semantics:

```text
dump32 addr=0x40000100 0x00000055 0x00000005 0x0000000f
```

`smoke_ata_identify_read` assembles a tiny ARM probe that issues ATA Identify
Device, then reads sector 0 through the modeled EIDE registers. It verifies DRQ
assertion, identify word 0, sector data, and DRQ clear after the guest drains
one sector:

```text
dump32 addr=0x40000100 0x00000048 0x00000040 0x00000048 0x0000314e 0x00002147 0x00000040
```

`smoke_dma_lcd` assembles a tiny ARM probe that fills fast RAM with packed
RGB565 pixels, starts DMA channel 0 toward the LCD2 FIFO window, and verifies
the DMA status/command registers plus nonblack PPM output:

```text
dump32 addr=0x40000100 0x04000000 0x000000fc
summary ... lcd_words=64 ...
```

`smoke_syscon_latches` assembles a tiny ARM probe that writes and reads the
cache-control range at `0x6000c000` and memory-controller range at
`0xf0000000`, including byte and halfword sub-register access:

```text
dump32 addr=0x40000100 0xa55a3344 0x0000a55a 0x55669988 0x00000099
```

`smoke_evp_vectors` writes the SWI handler register at `0x6000f008`, enables
`CACHE_CTL` bit 4, executes `svc`, and verifies Unicorn dispatches through the
modeled local exception vector rather than the low-memory vector:

```text
dump32 addr=0x40000100 0x5a17e0e5
```

`smoke_mmap_remap` starts with `--map-flash-zero`, writes iPod-style MMAP
entries to expose SDRAM at logical zero and NOR flash at `0x20000000`, then
verifies both mappings from native ARM code:

```text
dump32 addr=0x40000100 0x13572468 0x000000bf
```

`smoke_i2c_pmu` assembles a tiny ARM probe that sets the PMU register pointer
over I2C, reads the default `0x38` value, writes a replacement value, resets the
pointer, and reads the guest-written value back:

```text
dump32 addr=0x40000100 0x00000040 0x00000000 0x00000004 0x00000055
```

`smoke_timer_irq` assembles a tiny ARM probe that enables timer-0 interrupts,
arms a one-shot timer, waits for modeled device ticks to expire it, observes
interrupt-controller status bit 0, reads the timer value to acknowledge it, and
verifies the status clears:

```text
dump32 addr=0x40000100 0x00000001 0x00000000 0x00000000 0x00000000
```

### Apple Native Progress Check

Command:

```bash
python tools/time_command.py build-mingw/nano1g.exe \
  --profile apple \
  --firmware ../artifacts/firmware/apple_nano_14.5.3.1_fw.bin \
  --disk ../artifacts/images/ipodhd-apple-nano-sysinfo-preferences-probe.img \
  --max-insns 20000000 \
  --slice-insns 1 \
  --ppm apple-native-20m.ppm
```

Historical result after removing synthetic Language-screen output and Apple
instruction shims, but before removing synthetic sysinfo/model boot data:

```text
summary guest_insns=4430194 ticks=20000000 mmio_r=7195 mmio_w=1725 lcd_words=0 disk_reads=0 irq=879 pc=0x00024da4
apple_ui_hits ... lang_loop_4ee20=1 ... lcd_dirty_53b18=1 ...
apple_lcd_task_hits ... dirty_53b18=1 post_53b20=1 submit_53b38=0 wait_53db8=0 flush_53f28=0 ...
ppm nonblack=0 unique_colors=1
```

This run is retained only as old evidence. It reached the Apple language loop
and the LCD dirty handler natively, but did not reach LCD submit/flush and did
not write LCD2 pixels. The framebuffer artifact was black. It was not a
Language-screen render.

Current strict no-HLE Apple check after removing synthetic sysinfo/model boot
data and adding device/clock, PP peripheral-init/GPO latches, opt-in flash boot
support, disk-partition firmware loading, basic NOR command handling, and GPIO
atomic mirror semantics:

```bash
build-mingw/nano1g.exe \
  --profile apple \
  --verbose \
  --firmware ../artifacts/firmware/apple_nano_14.5.3.1_fw.bin \
  --disk ../artifacts/images/ipodhd-apple-nano-sysinfo-preferences-probe.img \
  --max-insns 20000000 \
  --slice-insns 1 \
  --ppm apple-gpio-strict-20m.ppm
```

```text
loaded wrapped osos fmt=3 file_off=0x4600 addr=0x10000000 entry=0x10000000 len=5566256
boot reset profile=apple entry=0x10000000 cpsr=0x000000d3 svc_sp=0x40016ffc
invalid memory type=19 pc=0x000013b4 addr=0x2d2d2e0d size=4 value=0x00000000 r0=0x2d2d2d2d r4=0x40017f18
wrote ppm apple-gpio-strict-20m.ppm lcd_words=0
summary guest_insns=455 ticks=455 mmio_r=51 mmio_w=48 lcd_words=0 disk_reads=0 irq=0 pc=0x000013b4
ppm nonblack=0 unique_colors=1
```

After fixing PP peripheral identity to report the Nano-class `PP20` / `20D `
values, direct `osos` now chooses the PP5022 128 KiB fast-RAM handoff slot:

```text
invalid memory type=19 pc=0x000013b4 addr=0x2d2d2e0d size=4 value=0x00000000 r0=0x2d2d2d2d r4=0x4001ff18
dump32 addr=0x4001ff18 0x2d2d2d2d 0x2d2d2d2d 0x2d2d2d2d 0x2d2d2d2d
summary guest_insns=456 ticks=456 mmio_r=55 mmio_w=48 lcd_words=0 disk_reads=0 irq=0 pc=0x000013b4
```

That is a hardware-ID correction only. The handoff table is still empty, and
filling it from the host would violate `NO_HLE.md`.

Current direct `osos` check with Unicorn virtual MMAP enabled reaches the same
handoff blocker before any disk or LCD activity:

```bash
build-mingw\nano1g.exe \
  --profile apple \
  --firmware ..\artifacts\firmware\apple_nano_14.5.3.1_fw.bin \
  --disk ..\artifacts\images\ipodhd-apple-nano-sysinfo-preferences-probe.img \
  --virtual-memmap \
  --max-insns 120000 \
  --slice-insns 1 \
  --dump32 0x4001ff18 \
  --dump-count 4 \
  --ppm tmp\apple-current-blocker.ppm
```

```text
dump32 addr=0x4001ff18 0x2d2d2d2d 0x2d2d2d2d 0x2d2d2d2d 0x2d2d2d2d
summary guest_insns=456 ticks=456 mmio_r=55 mmio_w=48 lcd_words=0 disk_reads=0 irq=0 pc=0x000013b4
apple_ui_hits create_24c48=0 ... lang_loop_4ee20=0 ... lcd_dirty_53b18=0 ...
apple_lcd_task_hits entry_53580=0 ... dirty_53b18=0 post_53b20=0 submit_53b38=0 wait_53db8=0 flush_53f28=0 ...
```

Loading the same Apple `osos` from the disk firmware partition gives the same
honest blocker:

```bash
build-mingw/nano1g.exe \
  --profile apple \
  --firmware-from-disk \
  --disk ../artifacts/images/ipodhd-apple-nano-sysinfo-preferences-probe.img \
  --max-insns 20000000 \
  --slice-insns 1 \
  --ppm apple-diskfw-strict-20m.ppm
```

```text
loaded disk ../artifacts/images/ipodhd-apple-nano-sysinfo-preferences-probe.img size=134217728
loaded wrapped osos fmt=3 file_off=0x4600 addr=0x10000000 entry=0x10000000 len=5566256
loaded disk wrapped firmware image_start=0x100000
summary guest_insns=455 ticks=455 mmio_r=51 mmio_w=48 lcd_words=0 disk_reads=0 irq=0 pc=0x000013b4
ppm nonblack=0 unique_colors=1
```

This is the honest current Apple blocker: firmware expects boot metadata at the
fast-RAM sysinfo pointer area, but the emulator must not fabricate it. The next
Apple task is to obtain and execute the real boot ROM/bootloader path, or model
the hardware media path it reads, so that metadata is produced by native guest
execution. The available Apple update image contains `osos`, `rsrc`, and `aupd`,
but no boot ROM image; the separate `bootloader.bin` fixture is a Rockbox
bootloader.

The local artifact sweep still does not show an obvious stock Apple boot ROM or
full flash image. The Apple-side files under `../artifacts/firmware/` are wrapped
firmware images or patched experiments, while `../artifacts/images/` contains
disk images and `../artifacts/analysis/` contains extracted/disassembled `osos`
material.

Boot-source audit with `tools/inspect_boot_sources.py`:

```text
zip=..\iPod_14.1.3.1.zip members=Firmware-14.5.3.1,manifest.plist
zip_member=Firmware-14.5.3.1 size=22907904 sha256=6fb166e09ca6c3452ca090d519fa058986b1f9b4326dde360e176e602154cfa2
zip_compare=..\artifacts\firmware\apple_nano_14.5.3.1_fw.bin equal=True

..\artifacts\firmware\apple_nano_14.5.3.1_fw.bin wrapped=yes base=0x0 entries=3
  image[00] name=osos file_off=0x4600 len=0x54ef30 addr=0x10000000 entry=0x10000000 entropy=6.101 sha256=2d34721c1d36f592
  image[01] name=rsrc file_off=0x553800 len=0x1000000 addr=0x10000000 entry=0x10000000 entropy=7.734 sha256=3b2d2c9a5da96eec
  image[02] name=aupd file_off=0x1553a00 len=0x851dc addr=0x10000000 entry=0x10000000 entropy=7.997 sha256=8796d0e12daac236
```

The updater ZIP member is exactly the same file as the committed local fixture.
The wrapped `aupd` entry is encrypted in-place; decrypting it with
`tools/decrypt_aupd.py` yields native ARM updater code.

The boot-source audit classifies both the Apple updater ZIP member and the local
Apple firmware fixture as `wrapped-firmware-bundle` with `osos,rsrc,aupd`
entries, and does not classify the Rockbox/iPodLinux `bootloader.bin` fixture as
an Apple boot ROM. This keeps direct `osos` execution framed as a blocker probe,
not a valid physical Apple boot.

The stricter audit now enumerates every ZIP member and the Apple disk-image
containers:

```text
zip=..\iPod_14.1.3.1.zip members=Firmware-14.5.3.1,manifest.plist
file=..\iPod_14.1.3.1.zip:Firmware-14.5.3.1 ... kind=wrapped-firmware-bundle apple_boot_rom=no ... wrapped_entries=osos,rsrc,aupd
file=..\iPod_14.1.3.1.zip:manifest.plist ... kind=raw-payload apple_boot_rom=no ...
file=..\artifacts\firmware\apple_nano_14.5.3.1_fw.bin ... kind=wrapped-firmware-bundle apple_boot_rom=no ... wrapped_entries=osos,rsrc,aupd
file=..\artifacts\firmware\bootloader.bin ... kind=raw-payload apple_boot_rom=no ... reset_vector_hits=1
file=..\artifacts\images\ipodhd-apple-nano.img ... kind=container-with-wrapped-firmware apple_boot_rom=no ... embedded_wrapped_starts=0x100000
file=..\artifacts\images\ipodhd-apple-nano-sysinfo-preferences-probe.img ... kind=container-with-wrapped-firmware apple_boot_rom=no ... embedded_wrapped_starts=0x100000
```

So `iPod_14.1.3.1.zip` is being inspected, but its firmware member is the same
wrapped bundle as `apple_nano_14.5.3.1_fw.bin`; it is not the missing native
boot ROM/bootloader producer for the `osos` handoff table.

Recursive boot-ROM candidate scan with `tools/find_bootrom_candidates.py` over
the updater ZIP plus local firmware/image fixture directories:

```text
zip=..\iPod_14.1.3.1.zip members=Firmware-14.5.3.1,manifest.plist
file=..\iPod_14.1.3.1.zip:Firmware-14.5.3.1 kind=wrapped-firmware-bundle entries=osos,rsrc,aupd ...
file=..\artifacts\firmware\apple_nano_14.5.3.1_fw.bin kind=wrapped-firmware-bundle entries=osos,rsrc,aupd ...
file=..\artifacts\firmware\rockbox_nano_fw.bin kind=wrapped-firmware-bundle entries=osos ...
file=..\artifacts\images\ipodhd-apple-nano.img kind=container-with-wrapped-firmware embedded_wrapped_starts=0x100000 ...
file=..\artifacts\images\ipodhd-apple-nano-sysinfo-preferences-probe.img kind=container-with-wrapped-firmware embedded_wrapped_starts=0x100000 ...
summary wrapped=22 containers=9 reset_candidates=0 other=3 skipped=0
```

The scan is intentionally conservative: if a future raw dump starts with an
ARM-looking exception-vector table it will be reported as a reset-vector
candidate for provenance review, but the current local fixtures produce zero
such candidates.

Payload-reference audit with `tools/inspect_payload_refs.py` on the extracted
`../artifacts/analysis/apple_osos.bin` fixture:

```text
needle=booting! off=0x1000 addr=0x10001000 refs=3 raw_unaligned=18
  kind=raw32 ref_off=0x2bb714 ref_addr=0x102bb714 fn=unknown target=0x10001000
  kind=raw32 ref_off=0x2bb7ec ref_addr=0x102bb7ec fn=unknown target=0x10001000
  kind=raw32 ref_off=0x317b44 ref_addr=0x10317b44 fn=unknown target=0x10001000
needle=IsyS off=0x1400 addr=0x10001400 refs=0 raw_unaligned=1
needle=SysI off=0x26b5c addr=0x10026b5c refs=0 raw_unaligned=0
```

The current scanner finds no ARM literal-load, simple `adr`, or nearby
function-start references to these early markers. That is negative evidence: it
does not prove what the missing boot handoff code is, but it keeps the visible
strings in `osos` from being misread as an available native handoff producer.

The verbose Apple handoff probe now captures the exact early direct-`osos`
contract before the first invalid read:

```text
apple handoff entry pc=0x00001388 core=0 cpuid=0x55555555 ppver=0x30325050 pp_selector=0x32 expected_slot=0x4001ff18
apple handoff probe pc=0x00001398 handoff=0x4001ff18 tag=0x2d2d2d2d sysinfo=0x2d2d2d2d sysinfo_ram=no ... words=0x2d2d2d2d,0x2d2d2d2d,0x2d2d2d2d,0x2d2d2d2d
apple handoff probe pc=0x000013ac handoff=0x4001ff18 tag=0x2d2d2d2d sysinfo=0x2d2d2d2d sysinfo_ram=no ... words=0x2d2d2d2d,0x2d2d2d2d,0x2d2d2d2d,0x2d2d2d2d
apple handoff probe pc=0x000013b4 handoff=0x4001ff18 tag=0x2d2d2d2d sysinfo=0x2d2d2d2d sysinfo_ram=no ... words=0x2d2d2d2d,0x2d2d2d2d,0x2d2d2d2d,0x2d2d2d2d
invalid memory type=19 pc=0x000013b4 addr=0x2d2d2e0d size=4 ...
```

`smoke_apple_handoff_probe` asserts those logs stay visible and that the
handoff table remains the untouched RAM fill pattern. This is intentionally a
blocker probe, not a boot fix.

Extracting the decrypted AUPD flash directory produces `diskmode`, `diagmode`,
and `logo` payloads. The current audit does not classify those extracted payloads
as reset-vector Apple boot ROMs either: `diskmode` starts as raw loaded code
rather than an exception-vector table, while `diagmode` starts with erased
`0xff` space. They remain useful flash-update evidence, not a boot handoff
producer.

Disassembly of the native AUPD parser shows the updater expects up to eight
28-byte `FwUp` records followed by the big-endian `!dnE` marker. The record
fields are:

```text
0x00 magic    0x46775570, shown as "FwUp"
0x04 offset   payload offset delta
0x08 key      command/payload selector, for example "CmdF", "T30G", "Flsh"
0x0c size     payload size
0x10 arg0
0x14 arg1
0x18 checksum byte-sum expected by native code
```

`tools/inspect_aupd_batch.py` scans for that structure. The missing `!dnE`
diagnostic is therefore not a timer problem; AUPD is genuinely not seeing a
valid updater batch command stream in the local fixture set.

Current scan:

```text
apple-aupd-dec.bin: magic_hits=3 shown_streams=3 valid_streams=0
apple_nano_14.5.3.1_fw.bin: magic_hits=0 shown_streams=0 valid_streams=0
ipodhd-apple-nano-sysinfo-preferences-probe.img: magic_hits=0 shown_streams=0 valid_streams=0
```

Runtime parser hook evidence from flash-mode AUPD:

```text
aupd parser pc=0x10001760 r0=0x1003bb50 ... r0_words=0x2d2d2d2d,0x2d2d2d2d,0x2d2d2d2d,0x2d2d2d2d ascii="--------------------------------"
aupd parser pc=0x10001afc ... r4=0x1000b178 ...
```

So the updater is not reading a batch stream from the current disk model before
the parser runs. It is pointed at the emulator's untouched SDRAM fill pattern,
which is why the native parser reports `END MARKER - NOT FOUND`.

Running the decrypted AUPD as a RAM-loaded image at its wrapper-declared
`0x10000000` load address is different. The reset relocation code copies the
`Pyld` section from `0x1000b178` to `0x1003bb50`; the parser destination then
contains a real payload header and `FwUp` records:

```text
dump32 addr=0x1003bb50 0x50796c64 0x00000010 0x00200000 0x00000000 0x46775570 0x0000001c 0x666c7368 0x00002000
```

This is not Apple OS boot yet, but it corrects the AUPD launch model: the
updater entry is a RAM-loaded firmware payload, not a standalone NOR image.
`smoke_aupd_direct_no_handoff` now covers this with a native 3M-instruction
probe:

```text
aupd parser pc=0x10001760 ... r0_words=0x46775570,0x0000001c,0x666c7368,0x00002000
apple low0 write pc=0x10004790 addr=0x0000aaaa size=2 value=0x00009090 flash_cmd=read-id low0_map=1
apple low0 read pc=0x10004794 addr=0x00000000 size=2 value=0x00000006 low0_map=1 flash_mode=0
dump32 addr=0x4001ff18 0x2d2d2d2d 0x2d2d2d2d 0x2d2d2d2d 0x2d2d2d2d ...
```

So direct AUPD execution is useful updater evidence, but it is not the native
producer of the Apple `osos` fast-RAM handoff table. The low-memory write probe
also shows a concrete hardware-semantic gap: with the direct-mode SDRAM alias at
zero, AUPD's native software-ID flash command sequence is not reaching the NOR
model. The readback confirms it sees SDRAM/vector bytes (`0x0006`) instead of
the modeled SST manufacturer ID (`0x00bf`).

Adding `--map-flash-zero` keeps that RAM-loaded updater shape while exposing the
modeled NOR device at address zero. The run reaches the native parser and real
`FwUp` stream, then stops earlier at the updater's diagnostic `svc 0x123456`
because the low SWI vector now fetches erased flash:

```bash
build-mingw\nano1g.exe \
  --profile apple \
  --firmware tmp\apple-aupd-dec.bin \
  --disk ..\artifacts\images\ipodhd-apple-nano-sysinfo-preferences-probe.img \
  --load-addr 0x10000000 \
  --entry 0x10000000 \
  --map-flash-zero \
  --max-insns 12000000 \
  --slice-insns 1 \
  --timer-divider 1 \
  --verbose \
  --ppm tmp\apple-aupd-direct-flashzero.ppm \
  --dump32 0x0 \
  --dump-count 8
```

```text
flash map ptr addr=0x00000000 size=0x100000
apple low0 read pc=0x10004794 addr=0x00000000 size=2 value=0x000000bf low0_map=2 flash_mode=1
apple low0 read pc=0x100047a4 addr=0x00000002 size=2 value=0x0000273f low0_map=2 flash_mode=1
aupd parser pc=0x10001760 r0=0x1003bb60 ... r0_words=0x46775570,0x0000001c,0x666c7368,0x00002000
swi diag string ptr=0x40016f65 "0"
swi exception core=0 int=2 insn=0xef123456 lr=0x10001d18 vector=0x00000008 cpsr=0x600000d3
uc_emu_start core=0 pc=0x00000008 failed: Invalid instruction (UC_ERR_INSN_INVALID)
dump32 addr=0x00000000 0xffffffff 0xffffffff 0xffffffff 0xffffffff 0xffffffff 0xffffffff 0xffffffff 0xffffffff
summary guest_insns=2267861 ticks=2267861 mmio_r=1584 mmio_w=66 lcd_words=0 disk_reads=0 irq=0 pc=0x00000008
```

This is hardware evidence, not UI progress. The next AUPD question is how the
real target arbitrates low exception vectors versus NOR command/read space when
a RAM-loaded updater is running.

An MMIO trace of the hybrid AUPD run shows the updater clears `0x6000c000` at
startup and does not program the `0xf000f000` MMAP registers or enable
`CACHE_CTL_VECT_REMAP` before its first diagnostic `svc`. A follow-up run after
adding MMAP support still dumps all MMAP registers as zero at the stop point:

```text
dump32 addr=0xf000f000 0x00000000 0x00000000 0x00000000 0x00000000 0x00000000 0x00000000 0x00000000 0x00000000 0x00000000 0x00000000 0x00000000 0x00000000 0x00000000 0x00000000 0x00000000 0x00000000
summary guest_insns=2267861 ticks=2267861 mmio_r=1592 mmio_w=66 lcd_words=0 disk_reads=0 irq=0 pc=0x00000008
```

The corrected SST software-ID model lets AUPD match a native flash table entry,
so the remaining flash-zero stop is no longer a generic-ID problem. The EVP and
MMAP models are useful hardware plumbing, but they do not by themselves move
AUPD past the erased low-vector fetch. That missing setup likely belongs to
earlier bootloader state, not to host-side sysinfo or UI fabrication.

```text
aupd index=2 file_off=0x1553a00 len=0x851dc addr=0x10000000 entry=0x10000000 checksum=0x04835edd
marker[0]=0xaabff224 key=0xe8d56465
candidate[0] ... enabled=1 key=0xe8d56465 order=be score=2326 head=060000eafeffffea3f0000ea4b0000ea
```

`tools/inspect_flash_image.py` finds updater-managed flash image entries for
`diskmode`, `diagmode`, and `logo`. Running the decrypted updater in flash mode
is useful hardware evidence, but it is not the Apple Language-screen boot path.
It executes native ARM code from reset, reaches ATA Identify and Set Features,
then prints its own diagnostic through `svc 0x123456`:

```text
ata identify sectors=262144 word5_sector_bytes=512 status=0x58
ata command complete cmd=0xef feature=0x02 count=0 lba=0 status=0x50
ata command complete cmd=0xef feature=0x03 count=34 lba=0 status=0x50
ata command complete cmd=0xef feature=0x03 count=12 lba=0 status=0x50
ata command complete cmd=0xe0 feature=0x00 count=0 lba=0 status=0x50
swi diag string ptr=0x10001ab8 "SCALE-"
swi diag string ptr=0x10001b64 "END MARKER -"
swi diag string ptr=0x10001b50 "NOT FOUND"
dump32 addr=0x4001ff18 0x2d2d2d2d 0x2d2d2d2d 0x2d2d2d2d 0x2d2d2d2d
summary guest_insns=30000000 ticks=58594 mmio_r=2134052 mmio_w=60 lcd_words=0 disk_reads=0 irq=0 pc=0x100043b4
```

The `!dnE` marker (`0x456e6421`) is present only as the updater's expected
constant in the decrypted AUPD code and was not found in the local Apple disk
image or wrapped firmware. This points toward AUPD being an updater command
stream consumer, not a stock boot ROM replacement.

Firmware wrapper extraction with `tools/extract_firmware_image.py`:

```text
image name=osos index=0 file_off=0x4600 len=0x54ef30 addr=0x10000000 entry=0x00000000 checksum=0x1f75b246
image name=rsrc index=1 file_off=0x553800 len=0x1000000 addr=0x10000000 entry=0x00000000 checksum=0x7b63b30c
image name=aupd index=2 file_off=0x1553a00 len=0x851dc addr=0x10000000 entry=0x00000000 checksum=0x04835edd
```

Byte-pattern checks against the extracted images:

```text
apple-osos.bin: IsyS offset=5120, SysI offset=158556, booting! offset=4096, diskmode offset=6009, diskscan offset=6018, softupdt offset=6027, retailOS offset=6036
apple-aupd.bin: encrypted in wrapper; decrypted payload starts with ARM vectors and updater code
apple-rsrc.bin: IsyS/SysI/boot strings not found
```

So `aupd` is now understood as native updater/flash evidence, not an obvious
native Language-screen metadata producer. The real missing piece is still the
boot ROM/bootloader path that sets the fast-RAM handoff table before `osos`
executes.

The probe disk's FAT partition does contain an `iPod_Control/Device/SysInfo`
file, but this is not the early fast-RAM handoff table. It is absent from the
pristine `ipodhd-apple-nano.img`, and direct `osos` execution fails before any
disk read:

```text
python tools/inspect_fat32.py ..\artifacts\images\ipodhd-apple-nano-sysinfo-preferences-probe.img --partition 1 --offset 0x23f1a00 --list

fat32 image=..\artifacts\images\ipodhd-apple-nano-sysinfo-preferences-probe.img partition=1 part_lba=67584 part_off=0x2100000 sectors=194560 bps=512 spc=1 reserved=32 fats=2 fatsz=2993 root_cluster=2 data_off=0x23f0400
offset=0x23f1a00 path=iPod_Control/Device/SysInfo file_off=0x0 size=388 cluster=13
file cluster=13 size=388 iPod_Control/Device/SysInfo
file cluster=14 size=251 iPod_Control/Device/Preferences

python tools/inspect_fat32.py ..\artifacts\images\ipodhd-apple-nano.img --partition 1 --offset 0x23f1a00

offset=0x23f1a00 path=not-found
```

Loading this FAT file into fast RAM would be host-side sysinfo fabrication and
is therefore not an acceptable Apple milestone path.

Disk image inspection with `tools/inspect_disk_image.py`:

```text
ipodhd-apple-nano-sysinfo-preferences-probe.img:
mbr[0] boot=0x80 type=0x00 start_lba=2048 sectors=65536 byte_off=0x100000 byte_len=0x2000000
mbr[1] boot=0x00 type=0x0b start_lba=67584 sectors=194560 byte_off=0x2100000 byte_len=0x5f00000
wrapped_image start=0x100000 magic=0x100100 fmt=3 entries=0x104200
  image[00] name=osos file_off=0x104600 len=0x54ef30 addr=0x10000000 entry=0x00000000 checksum=0x1f75b246
  image[01] name=rsrc file_off=0x653800 len=0x1000000 addr=0x10000000 entry=0x00000000 checksum=0x7b63b30c
  image[02] name=aupd file_off=0x1653a00 len=0x851dc addr=0x10000000 entry=0x00000000 checksum=0x04835edd
```

Relevant native code:

```text
0x10001084 chooses base 0x40017f00 or 0x4001ff00 from PP version/strap checks.
0x10001394 calls that helper with offset 0x18, producing 0x4001ff18 on the current run.
0x100013ac loads [handoff + 4], the sysinfo pointer.
0x100013b4 reads [sysinfo_ptr + 0xe0].
```

Previous no-HLE/no-SVC-skip 175M check, before removing synthetic sysinfo/model
boot data:

```bash
python tools/time_command.py build-mingw/nano1g.exe \
  --profile apple \
  --firmware ../artifacts/firmware/apple_nano_14.5.3.1_fw.bin \
  --disk ../artifacts/images/ipodhd-apple-nano-sysinfo-preferences-probe.img \
  --max-insns 175000000 \
  --slice-insns 1 \
  --ppm apple-175m.ppm
```

```text
elapsed_sec=40.959
summary guest_insns=89293027 ticks=175000000 mmio_r=89596 mmio_w=18442 lcd_words=0 disk_reads=0 irq=8477 pc=0x00024da4
cores cpu_pc=0x00024da4 cop_pc=0x000e9d60 cpu_halted=1 cop_halted=0 cpu_ctl=0x80000000 cop_ctl=0x00000000 cpu_insns=19515838 cop_insns=69777189
apple_lcd_task_hits ... dirty_53b18=1 post_53b20=0 submit_53b38=0 wait_53db8=0 flush_53f28=0 ...
ppm nonblack=0 unique_colors=1
```

This was still not the Language screen. The useful old signal was that COP could
run native code after replacing the magic SVC skip with a real ARM SWI exception
entry. With synthetic sysinfo removed, Apple now stops earlier and this benchmark
must be rerun only after real boot metadata is modeled.

### Apple Slice Sweep

The old slice sweep used a synthetic Language-screen fill and should not be used
as parity evidence. Rerun this section only after native framebuffer rendering
works.

```text
slice=1    elapsed_sec=7.633  ticks=20000000 guest_insns=12697392
slice=8    elapsed_sec=0.996  ticks=2500000  guest_insns=3048360
slice=32   elapsed_sec=0.885  ticks=625000   guest_insns=3356928
slice=128  elapsed_sec=0.695  ticks=156250   guest_insns=3664128
slice=512  elapsed_sec=0.960  ticks=39063    guest_insns=6011904
slice=2048 elapsed_sec=1.445  ticks=9766     guest_insns=15441920
```

Current milestone smoke:

```bash
python tools/time_command.py build-mingw/nano1g.exe \
  --profile apple \
  --firmware ../artifacts/firmware/apple_nano_14.5.3.1_fw.bin \
  --disk ../artifacts/images/ipodhd-apple-nano-sysinfo-preferences-probe.img \
  --max-insns 175000000 \
  --slice-insns 128 \
  --ppm apple-native-slice128-175m.ppm
```

Previous fast-smoke result before removing synthetic Language-screen output:

```text
elapsed_sec=0.707
summary guest_insns=3735296 ticks=1367188 mmio_r=70194 mmio_w=1249 lcd_words=11624 disk_reads=0 irq=66 pc=0x00054d7c
ppm nonblack=23232 unique_colors=1
```

Important caveat: larger `--slice-insns` values are fast smoke modes, not exact
cycle-by-cycle parity. Device ticks currently happen once per Unicorn slice, so
larger slices intentionally reduce device tick cadence.
