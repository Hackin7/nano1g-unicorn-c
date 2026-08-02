# Nano 1G Unicorn Emulator

This emulator is a standalone C/Unicorn bring-up for the iPod Nano 1G PP502x
target. It now boots the Rockbox `.ipod` payload far enough to drive the LCD,
use a FAT content disk, browse menus, inject click-wheel input, and launch
native Rockbox plugins from `.rockbox/rocks/`.

## Build

On the current Windows workspace:

```powershell
cmake --preset mingw-release
cmake --build --preset mingw-release
```

Focused checks used for the current Rockbox milestone:

```powershell
ctest --test-dir build-mingw -R "^audio_path_unit$" --output-on-failure
ctest --test-dir build-mingw -R "^smoke_rockbox_plugin_demo$" --output-on-failure
```

The Rockbox smoke tests use local fixtures under `../artifacts/` and skip or
fail clearly when those fixtures are missing.

## Run Rockbox With Web Controls

Create a GPT-wrapped content disk from the local Rockbox fixture:

```powershell
python tools/make_gpt_rockbox_disk.py `
  ../artifacts/images/ipodhd-rockbox-nano-content.img `
  tmp/ipodhd-rockbox-nano-content-gpt.img
```

Run the emulator with the browser UI:

```powershell
build-mingw/nano1g.exe --profile rockbox `
  --firmware ../artifacts/firmware/rockbox.ipod `
  --disk tmp/ipodhd-rockbox-nano-content-gpt.img `
  --disk-out tmp/rockbox-browser-session.img `
  --run-forever `
  --slice-insns 512 `
  --timer-divider 1 `
  --battery-percent 50 `
  --web 18080 `
  --web-no-hold
```

Open `http://127.0.0.1:18080/`. The page shows the live LCD framebuffer, native
execution counters, audio/DMA counters, click-wheel controls, and live Battery,
FireWire charger, USB power, and Hold controls. These controls update the PMU
ADC and GPIO pins seen by guest firmware; Hold is active-low on GPIOA bit 5,
raises PP502x GPIO IRQ 32, and suppresses optical-wheel packets while engaged.
The displayed LCD also follows guest backlight hardware: Rockbox-style PWM and
Apple’s Nano pulse dimmer/GPIOL power gate update the status feed, invalidate
the browser frame, and scale the rendered native framebuffer.

`--disk-out` is optional. With it, the emulator saves guest writes before a
browser Restart and at clean exit, then reloads that mutable image when the
owning preset returns. Without it, all writes remain in memory and the source
fixture is never modified. To continue the saved session in a new process, use
the prior output image as that run's `--disk` input.

## Input

The web UI uses server-side tap handling for buttons:

```text
/input?button=select&tap=1
```

That endpoint presses the selected button, holds it for a calibrated number of
device ticks, then releases it from the emulator tick loop. This avoids the
browser sending down/up pairs too quickly for Rockbox to interpret as a normal
short press.

Raw control endpoints are still available for scripts and debugging:

```text
/input?button=select&state=down
/input?button=select&state=up
/input?wheel=down
/input?wheel=up
```

Headless deterministic input is available through `--input SCRIPT`, where the
script accepts comma-separated `wait:N`, `NAME-down`, `NAME-up`, bare `NAME`,
`hold-down`, `hold-up`, `wheel:+D` / `wheel:-D`, and `frame:LABEL` tokens.
Frame checkpoints are written beside `--ppm` after the script's calibrated
settle waits. `--hold-switch` starts the emulator with Hold engaged.

## Verified Capability

- Rockbox `.ipod` payload loading and boot to the main menu.
- LCD framebuffer rendering through the native LCD2 path.
- Apple LCD DMA descriptor/block geometry accounting and hash-checked settled
  Language, main-menu, and Extras checkpoints.
- A slow Apple regression that executes 750 million guest instructions and
  repeats scroll, Menu exit, and native Extras re-entry three times in one
  uninterrupted hardware state.
- FAT disk reads from a GPT-wrapped Rockbox content image.
- Browser framebuffer polling and status counters.
- Browser click-wheel scroll and server-side tap select.
- Native Hold-switch GPIO/IRQ handling and firmware-rendered Apple lock icon.
- Runtime battery and charger controls backed by the existing PMU/GPIO model.
- PCF50605 interrupt status is read-only and read-to-clear, interrupt masks
  retain guest writes, and `OOCC1.GOSTDBY` requests are counted. Both direct
  device and guest-I2C regressions cover these semantics.
- ATA Device Control software reset aborts active transfers, holds BSY until a
  device tick after release, restores the reset signature, and clears multiple
  mode without raising an IRQ.
- Guest-controlled PWM/Nano backlight power and intensity, including Apple’s
  native timeout-off and input-wake cycle through the XMB self-refresh
  handshake.
- Deterministic scripted input through `--input`.
- Plugin browsing and native plugin loading from `.rockbox/rocks/`.
- Calculator plugin launch, with `build-mingw/calculator.bmp` captured during
  manual verification.
- Cube plugin launch, covered by `smoke_rockbox_plugin_demo`.
- Additional plugin launches observed while calibrating navigation: 2048,
  Calendar, and Battery Benchmark.
- RAM-to-I2S DMA plumbing with the `audio_path_unit` regression test and web
  status counters for `i2s_tx`, `i2s_drained`, `dma_audio_starts`,
  `dma_audio_done`, and `dma_audio_bytes`.

## Demonstration Paths

The plugin smoke test launches `cube.rock` from the content disk:

```powershell
ctest --test-dir build-mingw -R "^smoke_rockbox_plugin_demo$" --output-on-failure
```

Manual calculator demonstration path from the Rockbox root menu:

1. Scroll to `Plugins`.
2. Tap `Select`.
3. Scroll to `Applications`.
4. Tap `Select`.
5. Scroll to `calculator`.
6. Tap `Select`.

For browser automation, prefer `/input?button=select&tap=1` over separate
immediate down/up requests.

## Current Caveats

- Plugin support is native and working for the tested plugins, but the emulator
  is not yet a full hardware model. New plugins can expose missing device
  behavior.
- The audio path has a device-level regression test and visible counters, but
  audible MP3 playback has not been treated as complete.
- The Apple cold-boot path still requires a real Nano 1G boot ROM dump; updater
  ZIPs and wrapped firmware bundles are intentionally rejected for that route.
  Current interactive Apple UI acceptance is stage0-assisted and therefore
  does not claim a stock reset-vector cold boot.
- Apple Play-hold reaches the native `kP&H` responder and an interpreted
  122-byte callable at runtime `0x3265bc`. In current traces it does not call
  the Preferences writer or write `OOCC1`, so shutdown and persistence remain
  under investigation; the emulator does not synthesize either action.
- Changing an Apple setting reaches the Preferences dirty setter at runtime
  `0x2d408`, but neither short nor sustained Play-hold reaches the sleep/save
  callback at runtime `0x12c5dc`. The Preferences field at offset `0xb78` is
  filesystem flush state rather than a power flag, so forcing it would not be
  a valid persistence fix.
