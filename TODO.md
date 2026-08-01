# Nano 1G Unicorn Emulator TODO

This file tracks the remaining work required to turn the current bring-up
environment into a complete, hardware-faithful iPod Nano 1G emulator.

## Current Baseline

- Apple firmware boots through the native ARM stage0 SysInfo/OSOS handoff.
- Apple menus render through the modeled LCD2 and DMA paths.
- Click-wheel and button input reach the native Apple input stack.
- The Language, main-menu, and Extras/Clock transitions render without LCD
  block overruns.
- Rockbox boots, browses FAT media, loads tested plugins, and drives LCD2.
- The experimental iPod Linux preset reaches early kernel platform startup;
  PP502x handoff and loader2 boot remain incomplete.
- Apple browses the seeded iTunesDB, opens Now Playing, programs the WM8975,
  and feeds decoded PCM through DMA to I2S.
- ATA reads/writes, interrupt delivery, timers, flash commands, basic PMU/I2C,
  serial ports, and RAM-to-I2S DMA have focused tests.
- The current Apple path is stage0-assisted. It is not a stock cold boot.

The no-HLE rule remains mandatory: hardware behavior may be modeled, but Apple
or Rockbox firmware behavior must execute as guest code. Host code must not
seed UI state, synthesize firmware framebuffers, or fabricate boot metadata as
an acceptance-path shortcut.

## P0: Lock In The Current Apple UI Milestone

- [x] Add a deterministic Apple regression covering Language -> main menu ->
      Extras with Clock highlighted.
- [x] Assert `lcd_overruns=0` throughout that navigation sequence.
- [ ] Assert every accepted Apple DMA descriptor matches the active LCD block
      pixel count.
- [ ] Capture and validate settled framebuffer artifacts at each transition.
- [ ] Add a long interactive stress test with repeated menu entry, exit,
      scrolling, and animation.
- [ ] Distinguish settled frames from intermediate animation frames in tests.
- [ ] Keep the equivalent Rockbox LCD and menu tests passing.

Completion signal: the formerly corrupt Language and Extras transitions are
reproducible in automation, remain aligned after long navigation, and report no
stale DMA replay or LCD block overrun.

## P0: Native Apple Cold Boot

- [ ] Obtain and identify a genuine iPod Nano 1G NOR/boot-ROM dump.
- [ ] Record its provenance, size, reset vectors, hashes, and expected flash
      geometry without committing proprietary firmware to this repository.
- [ ] Boot from reset vector `0x00000000` through modeled NOR flash.
- [ ] Verify native reset code copies/remaps SDRAM using the PP502x MMAP path.
- [ ] Verify native boot code constructs the `IsyS` SysInfo handoff at
      `0x4001ff18`.
- [ ] Verify native boot code loads wrapped `osos` from the disk firmware
      partition and enters it without the stage0 probe.
- [ ] Remove the stage0 canary from the primary Apple acceptance route once the
      stock cold-boot route is proven.
- [x] Preserve stage0 as a native diagnostic fixture rather than deleting it.
- [x] Use modeled PP502x ATA DMA for the stage0 fixture's bulk `osos` load
      instead of spending millions of guest instructions in a PIO copy loop.

Completion signal: the `Apple official boot` preset reaches the same working
menus from a real reset-vector ROM without host-created boot metadata or native
stage0 assistance.

External blocker: the current local updater ZIPs, wrapped firmware bundles, and
HDD images do not contain a stock Nano 1G reset-vector NOR dump.

## P1: Apple Storage, Media, And Persistence

- [ ] Inventory ATA commands issued during long Apple menu, settings, and media
      sessions; implement commands that still reach the unsupported path.
- [x] Model ATA PIO data-in command and inter-sector phases as
      `BSY -> DRQ+IRQ`, with Status acknowledgement and no extra completion IRQ
      after the final read block.
- [ ] Validate ATA status, DRQ, error, IRQ acknowledgement, DMA pacing, and
      multi-sector boundary behavior against firmware expectations.
- [ ] Validate sector writes, cache/flush behavior, and transfer interruption.
- [ ] Make browser-launched sessions optionally persist disk changes through a
      clearly selected `--disk-out` image.
- [ ] Ensure source fixtures remain read-only by default.
- [ ] Verify settings and preferences survive an emulator restart.
- [x] Verify the Apple firmware reads the seeded iTunesDB and media files.
- [ ] Verify playlists, tracks, metadata, seeking, and play counts.
- [ ] Verify database and preference updates survive a disk snapshot reload.
- [ ] Add corruption and out-of-range LBA tests.

Completion signal: Apple can browse and update a realistic media library, and
all intended disk changes persist across clean emulator restarts.

## P1: Audible Audio Playback

- [x] Trace Apple codec setup over I2C and identify the codec register model.
- [x] Implement the required codec power, routing, mute, gain, and sample-rate
      registers.
- [ ] Complete I2S format, clock, FIFO, and DMA pacing semantics used by Apple.
- [x] Add a host audio sink with bounded buffering and deterministic shutdown.
- [x] Handle device-level underruns, sample-rate changes, and pause/resume.
- [ ] Verify seeking and track transitions through the firmware and host sink.
- [x] Expose useful audio state and underrun counters through `status.json`.
- [ ] Verify audible Apple playback from the seeded media image.
- [ ] Verify audible Rockbox MP3 playback through the same hardware path.
- [x] Add device-level tests that do not depend on a host sound device.

Completion signal: Apple and Rockbox both produce correctly paced, audible
stereo output with stable playback controls and no unbounded buffering.

## P1: Power, PMU, RTC, And User Controls

- [ ] Expand the PCF/PMU model beyond the registers exercised by early boot.
- [ ] Model battery voltage, capacity, charging state, charger detection, and
      low-battery behavior as coherent hardware state.
- [ ] Model RTC date/time persistence and alarm behavior.
- [ ] Model sleep, wake, power-off, and wake-source transitions.
- [ ] Verify hold-switch semantics and button suppression.
- [ ] Model PWM-controlled backlight state and brightness.
- [ ] Model clicker output or expose it as a host event.
- [ ] Add browser controls for battery, charger, hold, and wake events without
      embedding firmware policy in the frontend.

Completion signal: native firmware can change brightness, sleep, wake, retain
time, and react correctly to charger, battery, hold, and alarm events.

## P2: Remaining LCD Fidelity

- [ ] Continue long Apple traces and determine whether the real LCD1 register
      bank at `0x70003000` is used after current menu coverage.
- [ ] Identify the purpose of accesses around `0x70003800`.
- [ ] Route and model LCD1 if evidence shows that firmware relies on it.
- [ ] Trace and implement LCD entry-mode, scan-direction, driver-output,
      start-line, and scroll commands that are currently ignored.
- [ ] Resolve direct-GRAM versus block-DMA byte-order semantics if Apple begins
      using the direct path in later screens.
- [ ] Validate hidden GRAM rows, visible crop, cursor wrap, and partial windows
      across animations, photos, games, and unusual screens.
- [ ] Publish browser frames at a well-defined completed-block or completed-
      transaction boundary if intermediate logical frames remain distracting.

Completion signal: all tested Apple and Rockbox screens render correctly, and
verbose runs show no unexplained LCD register traffic, cursor mismatch, or
pixel-path disagreement.

## P2: USB And Host Connectivity

- [ ] Replace the current mostly latched USB register window with functional
      controller behavior.
- [ ] Model endpoint state, interrupts, reset, suspend, resume, and enumeration.
- [ ] Implement enough device behavior for native USB mode transitions.
- [ ] Investigate disk mode and firmware update paths.
- [ ] Connect USB presence to charging and power-management state.
- [ ] Decide whether to expose a virtual USB device or a higher-level host
      transport while preserving guest-visible controller semantics.

Completion signal: a host can enumerate the emulated device and native firmware
can enter, use, and leave its expected USB modes without register stubs.

## P2: CPU, COP, Interrupt, DMA, And Timing Accuracy

- [x] Add opt-in host boundary timing and exact-address MMIO frequency
      profiling without adding normal Apple code hooks.
- [x] Remove read-only Apple progress hooks from normal execution and expose
      them explicitly through `--apple-diagnostics` so probes do not distort
      firmware timing.
- [ ] Raise normal Apple throughput from the measured 14.1 MIPS early-boot
      rate toward the modeled 64 MIPS rate without reducing peripheral timing
      resolution or skipping guest-visible work.
- [ ] Investigate Unicorn MMIO transition cost and the official firmware's
      sustained ATA PIO traffic without intercepting or skipping guest loops.
- [ ] Audit CPU/COP scheduling and mailbox behavior under sustained dual-core
      workloads.
- [ ] Improve interrupt priority, masking, forced-interrupt, and acknowledgement
      semantics where traces disagree with hardware.
- [ ] Expand DMA channel lifecycle, request pacing, chaining, and completion
      behavior beyond the currently tested LCD and audio paths.
- [ ] Validate cache-control and self-modifying-code behavior across more native
      firmware paths.
- [ ] Expand PP502x MMAP/remap cases beyond the documented iPod boot mapping.
- [ ] Calibrate timers and RTC progression against guest-visible time rather
      than relying on preset-specific tuning.
- [ ] Reduce sensitivity to `--slice-insns`, `--timer-divider`, and
      `--rtc-usec-per-tick` while retaining deterministic tests.
- [ ] Add long-run invariants for lost interrupts, stuck DMA requests, timer
      drift, and CPU/COP starvation.

Completion signal: the same firmware images behave consistently across a
reasonable range of host speeds and instruction-slice settings.

## P2: Broader Functional Coverage

- [ ] Exercise every Apple top-level menu and settings page.
- [ ] Test Music browsing, playback, shuffle, repeat, and now-playing screens.
- [ ] Test Photos and photo transitions once storage decoding paths are ready.
- [ ] Test Clock, alarms, stopwatch, calendar, contacts, and notes.
- [ ] Test bundled games and identify any new hardware assumptions.
- [ ] Test settings changes followed by restart and disk reload.
- [ ] Expand Rockbox plugin coverage beyond the currently verified plugins.
- [ ] Replace the direct iPod Linux kernel preset with the planned native
      stage0 -> loader2 -> kernel chain and reach the loader2 menu.

Completion signal: major user-visible features have deterministic navigation
tests, expected framebuffer checks, and no newly unrouted critical MMIO.

## P3: Frontend And Developer Experience

- [ ] Show LCD DMA, overrun, audio, disk-write, interrupt, and power counters in
      a compact diagnostics view.
- [ ] Add explicit controls for starting, stopping, resetting, and selecting a
      persistent disk snapshot.
- [ ] Make transient animation frames distinguishable from settled output in
      diagnostics and automated captures.
- [ ] Add downloadable logs and framebuffer captures without blocking the
      emulation loop.
- [ ] Keep the frontend usable on desktop and mobile viewports.
- [ ] Add clear offline and crashed-emulator states.
- [ ] Ensure browser input cannot leave buttons stuck after a disconnect.

## P3: Tests, CI, And Documentation

- [ ] Update `README.md` and `docs/unicorn_emulator.md` to reflect that the
      stage0-assisted Apple path now reaches interactive menus and LCD output.
- [ ] Document the distinction between stage0-assisted UI success and genuine
      stock cold boot.
- [ ] Document required local fixtures and which tests skip without them.
- [ ] Run the complete CTest suite regularly, not only focused smoke subsets.
- [ ] Add CI coverage for fixture-free unit tests and generated ARM probes.
- [ ] Keep proprietary firmware and disk fixtures outside Git while recording
      reproducible hashes and extraction steps.
- [ ] Add regression notes whenever a new hardware behavior is inferred from
      traces or decompilation.
- [ ] Keep the worktree free of generated `Testing/`, framebuffer, log, and disk
      artifacts.

## Definition Of Done

The emulator can be considered broadly complete when:

- [ ] A genuine Nano 1G ROM cold-boots native Apple firmware without stage0 or
      host-created firmware state.
- [ ] Apple and Rockbox provide stable LCD, click-wheel, storage, audio, RTC,
      power, and sleep/wake behavior.
- [ ] Apple media browsing and audible playback work from a persistent disk.
- [ ] Major Apple applications and settings survive automated navigation and
      restart tests.
- [ ] USB behavior is functional enough for the native firmware modes being
      claimed as supported.
- [ ] Long runs have no unexplained critical MMIO, LCD overruns, stale DMA
      replay, stuck interrupts, or unbounded timing drift.
- [ ] Acceptance paths comply with the no-HLE rule and are covered by
      reproducible tests.
