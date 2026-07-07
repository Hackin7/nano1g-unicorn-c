#include "nano1g/hle_boot.h"

#include "nano1g/cpu_unicorn.h"
#include "nano1g/firmware.h"
#include "nano1g/ram.h"
#include "nano1g/trace.h"

static void seed_sysinfo(n1g_state_t *s) {
    /* Minimal Nano-ish model seeds. These are placeholders for the real HLE
       sysinfo structure; they keep early firmware probes from seeing zeroes. */
    n1g_ram_write(s, 0x107057b0u, 4, 0x000c0000u);
    n1g_ram_write(s, 0x107057b4u, 4, 0x00050000u);
    n1g_ram_write(s, 0x107057b8u, 4, 0x00000012u);

    /* Hold is active-low; default released. LCD strap A:1 high selects a known
       Nano LCD module in the current local notes. */
    s->gpio.regs[(0x030u / 4u)] = 0x00000022u;
}

bool n1g_hle_boot(n1g_state_t *s) {
    if (!n1g_load_firmware(s)) {
        return false;
    }
    seed_sysinfo(s);
    uint32_t entry = s->opts.entry_set ? s->opts.entry : s->opts.load_addr;
    uint32_t sp = N1G_SDRAM_BASE + N1G_SDRAM_SIZE - 0x1000u;

    for (int c = 0; c < N1G_CORE_COUNT; c++) {
        n1g_cpu_set_reg(s, (n1g_core_t)c, 13, sp);
        n1g_cpu_set_reg(s, (n1g_core_t)c, 14, 0);
        n1g_cpu_set_reg(s, (n1g_core_t)c, 15, entry);
        n1g_cpu_set_reg(s, (n1g_core_t)c, UC_ARM_REG_CPSR, 0x00000013u);
    }
    s->cpu[N1G_CORE_COP].halted = true;
    n1g_log(s, "hle boot profile=%s entry=0x%08x sp=0x%08x",
            s->opts.profile == N1G_PROFILE_APPLE ? "apple" : "rockbox", entry, sp);
    return true;
}
