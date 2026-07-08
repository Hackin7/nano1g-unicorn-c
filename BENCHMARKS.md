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
