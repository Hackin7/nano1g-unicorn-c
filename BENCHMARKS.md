# Benchmarks

Measurements from the current Windows workspace. Firmware and disk images are
loaded from `../artifacts/` and are not committed to this repo.

For future ad hoc runs, write generated PPM/log outputs under `tmp/`, which is
gitignored. Historical commands below may show root-level output names from
earlier bring-up runs.

## 2026-07-07

Build:

```bash
cmake --build --preset mingw-release
```

The build succeeds. GCC still warns about Unicorn hook callback casts under
`-Wpedantic`.

CTest after removing synthetic boot data and adding native bootloader, flash
boot, disk-firmware, NOR command, GPIO atomic-write, and ATA identify/read
canaries, a DMA-to-LCD2 pixel-transfer canary, and cache/memory-controller
MMIO latch coverage, I2C/PMU register-pointer read/write coverage, and
timer-to-interrupt-controller coverage:

```text
ctest --test-dir build-mingw --output-on-failure
100% tests passed, 0 tests failed out of 13
Total Test time (real) = 5.53 sec
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
the Intel manufacturer ID:

```text
dump32 addr=0x40000100 0x00000089 ...
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

Firmware wrapper extraction with `tools/extract_firmware_image.py`:

```text
image name=osos index=0 file_off=0x4600 len=0x54ef30 addr=0x10000000 entry=0x00000000 checksum=0x1f75b246
image name=rsrc index=1 file_off=0x553800 len=0x1000000 addr=0x10000000 entry=0x00000000 checksum=0x7b63b30c
image name=aupd index=2 file_off=0x1553a00 len=0x851dc addr=0x10000000 entry=0x00000000 checksum=0x04835edd
```

Byte-pattern checks against the extracted images:

```text
apple-osos.bin: IsyS offset=5120, SysI offset=158556, booting! offset=4096, diskmode offset=6009, diskscan offset=6018, softupdt offset=6027, retailOS offset=6036
apple-aupd.bin: IsyS/SysI/boot strings not found; first bytes disassemble as high-entropy/non-code data
apple-rsrc.bin: IsyS/SysI/boot strings not found
```

So `aupd` is not an obvious native boot-metadata producer from these local
artifacts. The real missing piece is still the boot ROM/bootloader path that
sets the fast-RAM handoff table before `osos` executes.

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
0x10001394 calls that helper with offset 0x18, producing 0x40017f18 on the current run.
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
