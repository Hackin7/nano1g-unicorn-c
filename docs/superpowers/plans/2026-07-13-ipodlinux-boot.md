# iPod Linux on nano1g-unicorn-c Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Boot iPod Linux (uClinux 2.4.32-ipod + podzilla) to an interactive UI on the nano1g-unicorn-c emulator, launched through the native ipodloader2 boot chain, viewable/controllable in the `--web` frontend.

**Architecture:** Reuse the proven native stage0 path (`tests/stage0_sysinfo_osos_probe.S`) that publishes the SysInfo handoff and branches into whatever `osos` image sits in the disk's firmware partition. Replace that `osos` with a `make_fw`-built image whose first-boot entry is ipodloader2 (`loader.bin`), which then chain-loads Apple OS, Rockbox, or the Linux kernel from disk — all as native guest code, honoring the repo's NO_HLE contract. Device gaps that Linux exposes are fixed in the emulator's modeled hardware, never by shimming firmware.

**Tech Stack:** C/Unicorn emulator (MinGW build), WSL for cross tools (`make_fw`, mtools, arm toolchains), ipodloader2 source in `../clicky/resources/ipodloader2/`, iPodLinux uClinux kernel (prebuilt from ZeroSlackr/archives, source fallback), existing Python disk tools in `tools/`.

**Repo layout note:** All paths below are relative to `nano1g-unicorn-c/` unless prefixed with `../`. The emulator must always be launched with CWD = `nano1g-unicorn-c/` (web presets use `../artifacts/...` relative paths).

---

## Why this route (decisions already made)

- **Boot chain:** `stage0 → ipodloader2 (as osos) → kernel.bin` — NOT direct kernel load. Reasons: (a) loader2 is the canonical iPodLinux installer layout, so a real kernel expects the machine state loader2 leaves behind; (b) stage0 → osos already works end-to-end for Apple firmware (Language screen renders), so the only new native code is loader2 itself; (c) loader2 gives us a visible on-LCD menu very early, which is a cheap intermediate canary.
- **Prior blocker removed:** old README note said direct `loader.bin` execution faults on the missing boot-ROM/sysinfo handoff (`0x2d2d2d2d`). That predates `stage0_sysinfo_osos_probe.S`, which now publishes exactly that handoff (`IsyS` @ `0x4001ff18`). Loader2 must be re-tested behind stage0 before assuming any loader2 bugs.
- **Kernel:** iPodLinux on PP5020/PP5022 is uClinux (ARM7TDMI, no MMU) 2.4.32-ipod. Prefer a prebuilt Nano-1G-capable `kernel.bin` + FAT32 root (ZeroSlackr layout) over building the 2.4 toolchain from scratch. Building from source is the fallback (Task 2b), not the main path.
- **Root filesystem:** FAT32-root (ZeroSlackr style, everything under the existing data partition) — avoids adding an ext2 partition to the GPT disk fixture and reuses the FAT tooling we already have (mtools at offset `@@$((12288*512))`).
- **NO_HLE:** all fixes happen in modeled hardware (`src/dev_*.c`) or in fixtures on disk. No instruction skips, no synthetic kernel state.

## Known device inventory vs. what Linux will touch

Already modeled and exercised by Rockbox/Apple: ATA reads+writes, LCD2 bridge + DMA, click wheel opto, I2C/PCF50605 PMU (+RTC), timers + usec RTC, IRQ/FIQ intc, dual-core CPU/COP mailboxes (enough for Rockbox), cache-flush→TB-flush, GPIO, USB MMIO latch.

Expected gap candidates (checked in Milestone 3): IDE IRQ-driven transfers (Rockbox mostly polls), timer semantics under Linux's tick, COP sleep/wake as Linux parks it, PP mailbox/IDE controller registers Linux's `pp5020.h` maps that Rockbox never reads, serial console registers if we enable `console=ttyS0` diagnostics.

---

## Milestone 0 — Fixture acquisition & feasibility audit

### Task 0.1: Inventory + build make_fw and loader2 in WSL

**Files:**
- Read: `../clicky/resources/ipodloader/make_fw.c` (v3 nano support confirmed: `-g nano` sets fw_version 3, image addr `0x10000000`)
- Read: `../clicky/resources/ipodloader2/Makefile`, `ipodhw.c` (Nano detection)
- Output: `tmp/ipodlinux/make_fw` (host binary), `tmp/ipodlinux/loader.bin`

- [ ] **Step 1: Compile make_fw natively in WSL**

```bash
wsl -e bash -c 'mkdir -p /tmp/ipl && gcc -o /tmp/ipl/make_fw \
  /mnt/c/Users/zunmun/Documents/Stuff/Workspace/2026/homelab/ipod_nano_1g/clicky/resources/ipodloader/make_fw.c \
  && /tmp/ipl/make_fw -h | head -20'
```
Expected: usage text `make_fw [-v] -g gen [-r rev] -o outfile [-i img_from_-e]* [-l raw_img]* ldr_img`.

- [ ] **Step 2: Try the checked-in prebuilt loader.bin first**

```bash
ls -l "C:/Users/zunmun/Documents/Stuff/Workspace/2026/homelab/ipod_nano_1g/clicky/resources/ipodloader2/loader.bin"
cp .../ipodloader2/loader.bin nano1g-unicorn-c/tmp/ipodlinux/loader.bin
```
Only rebuild loader2 from source (needs `arm-none-eabi-gcc` or the old `arm-elf` toolchain in WSL; check `Makefile` for `CROSS=`) if Milestone 1 shows the prebuilt binary misbehaving *and* symbol-level debugging requires a `.elf` with symbols. `loader.elf` is also checked in — try `arm-none-eabi-nm loader.elf` for symbols before rebuilding anything.

- [ ] **Step 3: Commit nothing yet** (fixtures live in gitignored `tmp/`; only note findings in the plan checkboxes).

### Task 0.2: Acquire an iPodLinux kernel + userland for Nano 1G

**This step downloads files — get explicit user approval for each source before fetching.**

- [ ] **Step 1: Candidate sources, in preference order**

1. ZeroSlackr (SourceForge `zeroslackr` project files) — prebuilt loader2 + `kernel.bin` (2.4.32-ipod) + FAT32 root with podzilla2; explicitly supported Nano 1G.
2. ipodlinux.org installer archives via web.archive.org (`kernel.bin`, `rootfs.ext2`).
3. GitHub mirrors of `linux-2.4.32-ipod` source + ZeroSlackr `build-tools` (arm-uclinux-elf toolchain) — source-build fallback (Task 2b).

- [ ] **Step 2: Stage artifacts locally**

Place under `../artifacts/ipodlinux/` (create dir): `kernel.bin`, plus userland tree (ZeroSlackr's `ZeroSlackr/` FAT directory or `rootfs.ext2`). Record exact source URLs and sha256 in `../artifacts/ipodlinux/SOURCES.md`.

- [ ] **Step 3: Sanity-check the kernel image**

```bash
python tools/inspect_firmware.py ../artifacts/ipodlinux/kernel.bin || xxd ../artifacts/ipodlinux/kernel.bin | head
```
Expected: raw ARM image (first words are ARM vectors/branches, not a wrapped `{{~~/-header). `make_fw -l` loads raw images at `0x28000000` with entry `0x00000000` — confirm kernel link address matches (2.4.32-ipod PP5020 kernels are linked for `0x28000000` physical — verify against `System.map` if available; if the kernel expects `0x10000000`, use `-i` with a wrapped image instead).

### Task 0.3: Extract the current osos and rebuild a loader2 firmware partition image

**Files:**
- Use: `tools/extract_osos.py`, `tools/inspect_disk_image.py`
- Create: `tmp/ipodlinux/fw_loader2.bin` (new firmware partition payload)

- [ ] **Step 1: Extract Apple osos from the Apple disk fixture**

```bash
python tools/extract_osos.py ../artifacts/images/ipodhd-apple-nano-sysinfo-preferences-probe.img tmp/ipodlinux/apple_osos.fw
```
(Adjust to the tool's actual CLI — run with `-h` first; it was written for exactly this extraction.)

- [ ] **Step 2: Build the combined image with make_fw (in WSL)**

```bash
wsl -e bash -c 'cd /mnt/c/.../nano1g-unicorn-c/tmp/ipodlinux && \
  /tmp/ipl/make_fw -v -g nano -o fw_loader2.bin -i apple_osos.fw loader.bin'
```
Expected: verbose output listing images; first-boot image = loader2, image 2 = Apple osos (loader2 reads the boot table to offer both).

- [ ] **Step 3: Verify the layout**

```bash
python tools/inspect_boot_sources.py tmp/ipodlinux/fw_loader2.bin
```
Expected: classified as wrapped firmware bundle with 2 images.

---

## Milestone 1 — Native loader2 menu on the emulator LCD

### Task 1.1: Build a Linux experiment disk fixture

**Files:**
- Create: `tools/make_ipodlinux_disk.py` (new; clone of `make_gpt_rockbox_disk.py` structure)
- Output: `tmp/ipodhd-ipodlinux-gpt.img`

- [ ] **Step 1: Write the disk builder**

Start from `tools/make_gpt_rockbox_disk.py` (same GPT wrapper, FAT at LBA 12288). Input = the Apple probe image (so `SysInfo` stays present for stage0), with two mutations: (a) overwrite the firmware partition (LBA 2048) payload with `tmp/ipodlinux/fw_loader2.bin`, (b) mcopy `kernel.bin`, `ipodloader.conf`, and the userland tree into the FAT partition. `ipodloader.conf` contents (loader2 syntax):

```
# ipodloader.conf
backlight = 1
timeout = 10
default = 1
# menu entries
Apple OS @ ramimg
Rockbox @ (hd0,1)/.rockbox/rockbox.ipod
iPod Linux @ (hd0,1)/kernel.bin
```
(`ramimg` = boot table image 2; exact directive names must be checked against `../clicky/resources/ipodloader2/config.c` — do that while writing the tool, the parser is ~200 lines.)

- [ ] **Step 2: Build and verify the fixture**

```bash
python tools/make_ipodlinux_disk.py   # writes tmp/ipodhd-ipodlinux-gpt.img
wsl -e bash -c 'mdir -i .../tmp/ipodhd-ipodlinux-gpt.img@@6291456 ::/'
```
Expected: `kernel.bin`, `ipodloader.conf`, userland dirs listed; `tools/inspect_disk_image.py` still reports a firmware partition at LBA 2048.

- [ ] **Step 3: Commit the tool**

```bash
git add tools/make_ipodlinux_disk.py && git commit -m "Add iPod Linux experiment disk builder"
```

### Task 1.2: Boot stage0 → loader2, get the menu on screen

- [ ] **Step 1: Run it**

```bash
build-mingw/nano1g --run apple-stage0 \
  --disk tmp/ipodhd-ipodlinux-gpt.img \
  --run-forever --slice-insns 512 --timer-divider 1 \
  --web 8099 --ppm tmp/ipodlinux-loader2.ppm
```
stage0 loads whatever `osos` the firmware partition holds — now loader2. Success canary: nonblack framebuffer showing the loader2 text menu (loader2 draws with its own `fb.c`, no Apple dependencies).

- [ ] **Step 2: If it faults — debug with the Rockbox-proven workflow**

Use `--probe-pc` + symbols from `loader.elf` (`arm-none-eabi-nm`), `--trace-mmio` for the first fault. Likely first suspects: loader2's `ipodhw.c` hardware-revision detection reading sysinfo (stage0 publishes it — verify the probe log shows `IsyS`), LCD type detection for Nano, keypad init on opto. Apply fixes ONLY in `src/dev_*.c` (modeled hardware) per NO_HLE. Use superpowers:systematic-debugging.

- [ ] **Step 3: Menu navigation canary**

Scripted input: `--input "wait:200000,play,wait:100000,select"` (wheel/button semantics identical to Rockbox runs; loader2 uses prev/next for menu). Verify PPM changes selection highlight. 

- [ ] **Step 4: Add smoke + commit**

Create `tests/smoke_loader2_menu.cmake` modeled on `tests/smoke_rockbox_menu.cmake`: run with `--max-insns` bound, assert nonblack PPM via `tools/check_ppm.py`. Skip when `tmp/ipodhd-ipodlinux-gpt.img` absent (fixture is local-only, like the Rockbox content smokes).

```bash
git add tests/smoke_loader2_menu.cmake CMakeLists.txt && git commit -m "Add loader2 menu smoke"
```

### Task 1.3: Regression gate — loader2 chain-loads Apple OS and Rockbox

- [ ] **Step 1: From the loader2 menu, boot entry "Apple OS"** (boot-table image 2). Expected: Language screen renders as it does today via direct stage0. This proves loader2's boot-table handoff and cache/branch behavior in the emulator.
- [ ] **Step 2: Boot entry "Rockbox"** (`rockbox.ipod` from FAT — put `.rockbox/` on the Linux disk too, reusing the content image tree). Expected: Rockbox main menu. This proves loader2's FAT32 + ATA read path end to end.
- [ ] **Step 3: Commit fixture/tool tweaks needed to pass both.**

Milestone 1 exit criteria: all three menu entries work; only Linux still unproven.

---

## Milestone 2 — Kernel loads and speaks

### Task 2.1: First kernel boot attempt + early-console visibility

- [ ] **Step 1: Select "iPod Linux" in loader2.** loader2 reads `kernel.bin` into RAM (2.4.32-ipod convention: load `0x28000000`-linked image at physical `0x10000000`? — loader2's `loader.c` `boot_linux()` shows the exact address; read it first), sets machine info, jumps.
- [ ] **Step 2: Expected first wall — silent hang.** The kernel's earliest output is the framebuffer console only after `fbcon` init; before that, nothing is visible. Instrument natively (allowed — host-side observation, not guest shims):
  - `--probe-pc` on kernel symbols. Get `System.map` from the same kernel build/package; if the prebuilt package lacks it, extract symbols from the zImage's embedded table or fall back to landmark addresses (`start_kernel`, `printk`, `panic`).
  - Add a host-side `printk` ring-buffer reader: a `--probe-pc` hook on `printk`'s return that dumps the `log_buf` guest memory region (pure read-only host tracing — same pattern as the Apple handoff probe). This makes every `printk` visible before fbcon exists.
- [ ] **Step 3: Triage the first 3 faults.** For each: record PC/symbol, MMIO address touched, and classify: missing device register (fix in `src/dev_*.c`), timing assumption (`--slice-insns`/`--rtc-usec-per-tick` tuning), or CPU semantics (Unicorn TB/cache — reuse the `tb_flush_pending` deferred-flush machinery that fixed Rockbox codec loads, since the kernel copies itself and relocates). Fix, re-run, iterate with superpowers:systematic-debugging. Commit each device fix separately with a native smoke where practical.

Milestone 2 exit criteria: `printk` trace shows the kernel reaching `VFS: Mounted root` — or a specific, documented blocker with symbol-level diagnosis.

### Task 2b (fallback, only if no usable prebuilt kernel): build 2.4.32-ipod from source

- [ ] Source: `linux-2.4.32-ipod` mirror + ZeroSlackr `build-tools` (arm-uclinux-elf-tools). Build in WSL. Config: `ipod_defconfig` equivalent for PP5020, `CONFIG_FB_IPOD=y`, `CONFIG_VFAT_FS=y`, root on FAT (`root=/dev/hda2 rootfstype=vfat`). This is a multi-day toolchain archaeology task — timebox it and prefer widening the prebuilt search first. Building also unlocks `System.map` + custom `printk` instrumentation, which materially helps Milestone 2/3 debugging.

---

## Milestone 3 — Root mounts, podzilla on screen

### Task 3.1: Root filesystem mount

- [ ] **Step 1:** Kernel cmdline via loader2 config (`kernel.bin root=/dev/hda2 ...` appended on the menu line — syntax per loader2 `config.c`). `/dev/hda2` = the FAT data partition on our GPT disk. Watch for the GPT-vs-MBR wrinkle: 2.4 kernels only parse MBR/mac partition tables. Our GPT fixture keeps a protective MBR — if the kernel can't see partition 2, extend `tools/make_ipodlinux_disk.py` to write a hybrid MBR entry for the FAT partition (fixture change, not emulator change).
- [ ] **Step 2:** Expected device work: IDE driver does IRQ-driven PIO (Rockbox polls). If mount stalls, check `dev_ata.c` raises IDE IRQ 23 on data-ready and honors `nIEN`; add a native ARM smoke (`tests/ata_irq_probe.S` + `tests/smoke_ata_irq.cmake`) reproducing IRQ-driven sector read before fixing.
- [ ] **Step 3:** Exit canary: `printk` trace shows `VFS: Mounted root (vfat filesystem)` then `init` exec.

### Task 3.2: fbcon/podzilla rendering + input

- [ ] **Step 1:** fbcon output on the modeled LCD2 — kernel fb driver drives the same LCD bridge Rockbox does; expect at most register-ordering fixes in `src/dev_lcd2.c`(/`dev_dma.c`).
- [ ] **Step 2:** podzilla launches from init scripts (ZeroSlackr rootfs does this by default). Canary: podzilla menu PPM, nonblack + text rows.
- [ ] **Step 3:** Click wheel/buttons: kernel keyboard driver uses the same opto path; verify `--input "select"` and wheel events navigate podzilla. Fix `dev_opto.c` packet-format issues only with evidence from the kernel driver source (it's in the 2.4.32-ipod tree, `drivers/input/ipod_keyb.c`).

### Task 3.3: Wire it into the product surface + docs

- [ ] **Step 1:** Add web preset: `apply_run_preset()` in `src/main.c` gets `"ipodlinux"` → stage0 firmware + `tmp/ipodhd-ipodlinux-gpt.img`, and `src/web_frontend.c` gets the `<option>` + `valid_restart_preset` entry. (Depends on the preset-failure-exit bug being fixed — task already chipped — otherwise a missing fixture kills the process from the dropdown.)
- [ ] **Step 2:** `tests/smoke_ipodlinux_podzilla.cmake` — bounded-instruction run asserting the podzilla PPM, skip-if-fixture-absent.
- [ ] **Step 3:** Document the whole path in `docs/unicorn_emulator.md` (runbook section) and update `README.md` Current Status + `BENCHMARKS.md` (insn counts to menu/kernel/podzilla).
- [ ] **Step 4:** Final commit + run full `ctest --test-dir build-mingw --output-on-failure` to prove no Rockbox/Apple regressions.

---

## Risk register (ordered)

1. **No usable prebuilt Nano-1G kernel found** → Task 2b toolchain build; mitigate by accepting any PP5020-family kernel first (4g/mini images) just to burn down emulator gaps, since PP5020 vs PP5022 device deltas are small and visible via probe traces.
2. **uClinux flat-binary vs zImage confusion** — iPodLinux kernels for ARM7TDMI boot as raw images; if the artifact is an ELF or zImage, extract/objcopy accordingly (document in SOURCES.md).
3. **Unicorn stale-TB during kernel decompress/relocate** — same failure class already solved for Rockbox codec loads; the deferred `tb_flush_pending` hook may need to also trigger on the kernel's cache-op addresses (`0xf000f044` CACHE_OPERATION already covered).
4. **2.4 IDE driver timeouts under scaled timers** — keep `--timer-divider 1` and tune `--rtc-usec-per-tick`; timing knobs are already parameterized.
5. **GPT vs 2.4 partition parsing** (see Task 3.1) — fixture-side hybrid MBR, cheap.
6. **loader2 prebuilt binary was built for a different hw rev** — rebuild from source with the checked-in Makefile (needs arm toolchain in WSL; `startup.o`/`.o` files present suggest it last built fine from this tree).

## Explicitly out of scope

- Real Apple cold boot (`apple-official`) — still gated on a physical NOR dump.
- Audio under Linux, USB/disk-mode, ext2 root, wheel scroll acceleration tuning.
- Upstreaming loader2/kernel patches.
