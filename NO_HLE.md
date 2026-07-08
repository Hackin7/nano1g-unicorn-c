# No-HLE Contract

This emulator exists to run the real iPod Nano 1G firmware on modeled PP502x
hardware. HLE is out of scope because it would replace the behavior we are
trying to observe.

This is a hard project rule, not just a current implementation preference.
Temporary host-side shortcuts are not acceptable milestone scaffolding for
Apple progress.

Allowed:

- model hardware registers, memories, buses, timers, IRQ delivery, DMA, flash,
  IDE, GPIO, I2C, LCD, and other devices;
- load user-supplied firmware, disk, and flash/ROM images at configured physical
  addresses;
- initialize architectural CPU reset state such as PC, CPSR, banked SP values,
  and exception vectors;
- add read-only tracing, dumps, assertions, and smoke probes;
- use Rockbox as a native guest canary.

Forbidden:

- fabricate Apple sysinfo/model blocks in guest RAM;
- draw synthetic Apple UI, LCD descriptors, framebuffer contents, or Language
  screens;
- skip guest instructions, rewrite PC for progress, or patch firmware services;
- emulate firmware calls with host-side kcall/syscall/SVC shims;
- report screenshots as Apple parity unless pixels came from native guest
  writes through modeled hardware.

The Apple milestone is valid only when native firmware reaches the screen
through hardware-semantic device behavior. Boot metadata/sysinfo must be
produced by native boot ROM/bootloader execution or by modeled hardware media
that the guest reads itself. Until then, Apple tests are no-HLE contract checks
and blocker probes, not UI acceptance tests.
