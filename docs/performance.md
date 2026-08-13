# Fidelity-First Performance

The normal emulator keeps a 512-instruction CPU timing quantum for the Apple
and Rockbox presets. Larger slices are an explicit experiment because they
change interrupt delivery points and can change firmware-visible state.

## Benchmarking

`tools/benchmark_emulator.py` covers startup, menus, active input, idle, audio,
and browser polling for both primary firmware targets. Each workload receives
one warm-up process followed by seven measured processes by default. Results
include median and p95 wall time, semantic framebuffer milestone times,
framebuffer hashes, CPU/COP calls, scheduled and executed instructions, halted
and fast-forwarded ticks, MMIO callbacks, device boundaries, storage, LCD, and
audio totals.

Stop other emulator instances and CPU-heavy applications before recording a
baseline. Absolute timings depend on the host and are deliberately written to
a local JSON file rather than committed as universal expectations.

```powershell
python tools/benchmark_emulator.py `
  --suite all `
  --runs 7 `
  --output tmp/benchmark-baseline.json

python tools/benchmark_emulator.py `
  --suite all `
  --runs 7 `
  --baseline tmp/benchmark-baseline.json `
  --output tmp/benchmark-current.json
```

Use repeated `--workload NAME` arguments for a focused candidate measurement.
The baseline comparison reports per-workload improvement, framebuffer equality,
the 2% regression limit, active Apple/Rockbox medians, and idle speedup.

## Runtime Modes

`--apple-diagnostics`, `--verbose`, `--trace-pc`, `--trace-mmio`, and
`--host-profile` are performance-distorting diagnostic modes. Named presets and
browser restarts clear them; pass one explicitly after `--run PRESET` when it is
needed. Normal browser presets do not inherit a diagnostic mode from the prior
image.

The summary, host profile, and `/state` distinguish scheduled instruction
budget, active CPU/COP calls, active slices, halted ticks, fast-forwarded ticks,
MMIO callbacks, device ticks, and timing boundaries. The leading
`guest_insns=` summary field remains compatible with existing consumers.

`NANO1G_ENABLE_IPO` enables portable Release interprocedural optimization when
CMake and the compiler support it. It is currently off by default because the
local MinGW Release measurement regressed the primary Apple workload; enable it
for a candidate build and retain it only after the benchmark threshold passes.
No default uses `-march=native` or platform-specific tuning.

## Retained Optimization

When both cores are halted, the bus can advance directly to the nearest safe
PMU, RTC, scripted-input, and web-poll boundary. It refuses to skip while a
timer, interrupt, CPU wake, DMA, ATA, I2C, I2S, serial, optical, or input event
needs an intermediate tick. Active execution remains at the normal 512
instruction quantum.

`smoke_idle_fast_forward` compares this path with `--no-idle-fast-forward` and
requires identical final scheduled budget, device ticks, timer, RTC, MMIO, IRQ,
and unrouted-access state. A local 8,388,608-tick sample measured about 9.1x
faster while producing identical compared state.

## Rejected Candidates

- A page-indexed C MMIO dispatch experiment passed focused correctness tests
  but changed the Apple benchmark median from 3.479 seconds to 3.587 seconds,
  so it was reverted.
- Release IPO did not meet the 3% retention threshold on the local MinGW build,
  so compiler support remains available without becoming the default.
- Larger Unicorn slices increased headline throughput but changed interrupt
  timing and firmware state, so faithful presets remain at 512 instructions.
- Direct Unicorn shadow mappings for timer and interrupt pages were not retained
  because reads and acknowledgements share guest-visible side effects. They need
  a separately proven design before they can satisfy differential MMIO tests.

The active Apple and Rockbox 10% target remains open. Profiling shows normal
active execution is dominated by Unicorn rather than bus dispatch, so further
work likely needs an upstream Unicorn improvement or a newly measured hot path,
not firmware-address-specific shortcuts.
