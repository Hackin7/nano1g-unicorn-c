#include "nano1g/boot_reset.h"

#include "nano1g/cpu_unicorn.h"
#include "nano1g/firmware.h"
#include "nano1g/trace.h"

static void set_banked_sp(n1g_state_t *s, n1g_core_t core, uint32_t cpsr, uint32_t sp) {
    n1g_cpu_set_reg(s, core, UC_ARM_REG_CPSR, cpsr);
    n1g_cpu_set_reg(s, core, UC_ARM_REG_SP, sp);
}

static void set_arm_reset_regs(n1g_state_t *s, n1g_core_t core, uint32_t entry) {
    set_banked_sp(s, core, 0x10u, 0x40017bfcu); /* User */
    set_banked_sp(s, core, 0x11u, 0x400177fcu); /* FIQ */
    set_banked_sp(s, core, 0x12u, 0x400173fcu); /* IRQ */
    set_banked_sp(s, core, 0x13u, 0x40016ffcu); /* Supervisor */
    set_banked_sp(s, core, 0x17u, 0x40016bfcu); /* Abort */
    set_banked_sp(s, core, 0x1bu, 0x400167fcu); /* Undefined */

    n1g_cpu_set_reg(s, core, UC_ARM_REG_CPSR, 0x000000d3u);
    n1g_cpu_set_reg(s, core, UC_ARM_REG_LR, 0);
    n1g_cpu_set_reg(s, core, UC_ARM_REG_PC, entry);
}

bool n1g_boot_reset(n1g_state_t *s) {
    if (s->opts.boot_mode == N1G_BOOT_DIRECT) {
        if (s->opts.firmware_from_disk) {
            if (!n1g_load_firmware_from_disk(s)) {
                return false;
            }
        } else if (!n1g_load_firmware(s)) {
            return false;
        }
    }

    uint32_t entry = s->opts.entry_set
                         ? s->opts.entry
                         : (s->opts.boot_mode == N1G_BOOT_FLASH ? N1G_FLASH_BASE : s->opts.load_addr);
    for (int c = 0; c < N1G_CORE_COUNT; c++) {
        set_arm_reset_regs(s, (n1g_core_t)c, entry);
    }

    s->cpu[N1G_CORE_COP].halted = true;
    s->cpucon.ctl[N1G_CORE_COP] = 0x80000000u;
    n1g_info(s, "boot reset profile=%s mode=%s entry=0x%08x cpsr=0x%08x svc_sp=0x%08x",
             s->opts.profile == N1G_PROFILE_APPLE ? "apple" : "rockbox",
             s->opts.boot_mode == N1G_BOOT_FLASH ? "flash" : "direct",
             entry,
             n1g_cpu_get_reg(s, N1G_CORE_CPU, UC_ARM_REG_CPSR),
             n1g_cpu_get_reg(s, N1G_CORE_CPU, UC_ARM_REG_SP));
    return true;
}
