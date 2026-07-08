#include "nano1g/devices.h"

#include "nano1g/cpu_unicorn.h"
#include "nano1g/trace.h"

#define CPUCON_SLEEP_BITS 0xe0000000u

static uint32_t intc_hi_summary(n1g_state_t *s, n1g_core_t core) {
    uint32_t status = core == N1G_CORE_CPU ? s->intc.hi_cpu_status : s->intc.hi_cop_status;
    return status != 0 ? (1u << 30) : 0;
}

static uint32_t mask_for_size(uint32_t size) {
    if (size == 1u) return 0xffu;
    if (size == 2u) return 0xffffu;
    return 0xffffffffu;
}

static uint32_t read_part(uint32_t value, uint32_t offset, uint32_t size) {
    uint32_t shift = (offset & 3u) * 8u;
    return (value >> shift) & mask_for_size(size);
}

static bool intc_irq_pending(n1g_state_t *s, n1g_core_t core) {
    if (core == N1G_CORE_CPU) {
        uint32_t lo = s->intc.cpu_status & s->intc.cpu_enable;
        uint32_t hi = s->intc.hi_cpu_status & s->intc.hi_cpu_enable;
        return lo != 0 || ((s->intc.cpu_enable & (1u << 30)) && hi != 0);
    }

    uint32_t lo = s->intc.cop_status & s->intc.cop_enable;
    uint32_t hi = s->intc.hi_cop_status & s->intc.hi_cop_enable;
    return lo != 0 || ((s->intc.cop_enable & (1u << 30)) && hi != 0);
}

uint32_t n1g_dev_intc_read(n1g_state_t *s, uint32_t offset, uint32_t size) {
    uint32_t aligned = offset & ~3u;
    uint32_t value = 0;
    switch (aligned) {
    case 0x000: value = s->intc.cpu_status | intc_hi_summary(s, N1G_CORE_CPU); break;
    case 0x004: value = s->intc.cop_status | intc_hi_summary(s, N1G_CORE_COP); break;
    case 0x008: value = 0; break;
    case 0x00c: value = 0; break;
    case 0x010:
        value = s->intc.cpu_status | s->intc.cop_status |
               intc_hi_summary(s, N1G_CORE_CPU) | intc_hi_summary(s, N1G_CORE_COP);
        break;
    case 0x014: value = 0; break;
    case 0x020: value = s->intc.cpu_enable; break;
    case 0x024: value = s->intc.cpu_enable; break;
    case 0x028: value = s->intc.cpu_enable; break;
    case 0x030: value = s->intc.cop_enable; break;
    case 0x034: value = s->intc.cop_enable; break;
    case 0x038: value = s->intc.cop_enable; break;
    case 0x100: value = s->intc.hi_cpu_status; break;
    case 0x104: value = s->intc.hi_cop_status; break;
    case 0x108: value = 0; break;
    case 0x10c: value = 0; break;
    case 0x110: value = s->intc.hi_cpu_status | s->intc.hi_cop_status; break;
    case 0x114: value = 0; break;
    case 0x120: value = s->intc.hi_cpu_enable; break;
    case 0x124: value = s->intc.hi_cpu_enable; break;
    case 0x128: value = s->intc.hi_cpu_enable; break;
    case 0x130: value = s->intc.hi_cop_enable; break;
    case 0x134: value = s->intc.hi_cop_enable; break;
    case 0x138: value = s->intc.hi_cop_enable; break;
    default: value = 0; break;
    }
    return read_part(value, offset, size);
}

void n1g_dev_intc_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value) {
    (void)size;
    if (s->opts.profile == N1G_PROFILE_APPLE) {
        static uint32_t intc_logs;
        if (intc_logs < 96u) {
            intc_logs++;
            n1g_log(s,
                    "apple intc write offset=0x%03x value=0x%08x cpu=%08x/%08x cop=%08x/%08x hi_cpu=%08x/%08x hi_cop=%08x/%08x",
                    offset,
                    value,
                    s->intc.cpu_status,
                    s->intc.cpu_enable,
                    s->intc.cop_status,
                    s->intc.cop_enable,
                    s->intc.hi_cpu_status,
                    s->intc.hi_cpu_enable,
                    s->intc.hi_cop_status,
                    s->intc.hi_cop_enable);
        }
    }
    switch (offset) {
    case 0x024: s->intc.cpu_enable |= value; break;
    case 0x028: s->intc.cpu_enable &= ~value; break;
    case 0x034: s->intc.cop_enable |= value; break;
    case 0x038: s->intc.cop_enable &= ~value; break;
    case 0x124: s->intc.hi_cpu_enable |= value; break;
    case 0x128: s->intc.hi_cpu_enable &= ~value; break;
    case 0x134: s->intc.hi_cop_enable |= value; break;
    case 0x138: s->intc.hi_cop_enable &= ~value; break;
    default: break;
    }
}

void n1g_dev_intc_tick(n1g_state_t *s) {
    if (intc_irq_pending(s, N1G_CORE_CPU)) {
        if (s->opts.profile == N1G_PROFILE_APPLE) {
            static uint32_t cpu_irq_logs;
            if (cpu_irq_logs < 48u) {
                cpu_irq_logs++;
                n1g_log(s,
                        "apple intc deliver CPU status=0x%08x enable=0x%08x hi_status=0x%08x hi_enable=0x%08x ctl=0x%08x",
                        s->intc.cpu_status,
                        s->intc.cpu_enable,
                        s->intc.hi_cpu_status,
                        s->intc.hi_cpu_enable,
                        s->cpucon.ctl[N1G_CORE_CPU]);
            }
        }
        s->cpu[N1G_CORE_CPU].halted = false;
        s->cpucon.ctl[N1G_CORE_CPU] &= ~CPUCON_SLEEP_BITS;
        n1g_cpu_raise_irq(s, N1G_CORE_CPU);
    }
    if (intc_irq_pending(s, N1G_CORE_COP)) {
        if (s->opts.profile == N1G_PROFILE_APPLE) {
            static uint32_t cop_irq_logs;
            if (cop_irq_logs < 48u) {
                cop_irq_logs++;
                n1g_log(s,
                        "apple intc deliver COP status=0x%08x enable=0x%08x hi_status=0x%08x hi_enable=0x%08x ctl=0x%08x",
                        s->intc.cop_status,
                        s->intc.cop_enable,
                        s->intc.hi_cop_status,
                        s->intc.hi_cop_enable,
                        s->cpucon.ctl[N1G_CORE_COP]);
            }
        }
        s->cpu[N1G_CORE_COP].halted = false;
        s->cpucon.ctl[N1G_CORE_COP] &= ~CPUCON_SLEEP_BITS;
        n1g_cpu_raise_irq(s, N1G_CORE_COP);
    }
}
