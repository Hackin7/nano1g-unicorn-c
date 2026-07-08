#include "nano1g/devices.h"

#include "nano1g/trace.h"

#define CPUCON_SLEEP 0x80000000u
#define CPUCON_WAIT_CNT 0x40000000u
#define CPUCON_WAKE_INT 0x20000000u
#define CPUCON_CNT_SEC 0x00800000u
#define CPUCON_CNT_MSEC 0x01000000u
#define CPUCON_CNT_USEC 0x02000000u
#define CPUCON_CNT_CLKS 0x08000000u
#define CPUCON_CNT_MASK 0x0f800000u

static uint32_t wait_count_ticks(uint32_t ctl) {
    uint32_t count = ctl & 0xffu;
    if (count == 0) {
        count = 1;
    }
    if (ctl & CPUCON_CNT_MSEC) {
        return count * 1000u;
    }
    if (ctl & CPUCON_CNT_SEC) {
        return count * 1000000u;
    }
    return count;
}

static void update_control(n1g_state_t *s, n1g_core_t core, uint32_t value) {
    s->cpucon.ctl[core] = value;
    s->cpucon.wait_ticks[core] = 0;
    s->cpu[core].halted = (value & (CPUCON_SLEEP | CPUCON_WAIT_CNT)) != 0;
    if ((value & CPUCON_WAIT_CNT) != 0 && (value & CPUCON_CNT_MASK) != 0) {
        s->cpucon.wait_ticks[core] = wait_count_ticks(value);
    }
}

uint32_t n1g_dev_cpucon_read(n1g_state_t *s, uint32_t offset, uint32_t size) {
    (void)size;
    if (offset == 0x00) return s->cpucon.ctl[N1G_CORE_CPU];
    if (offset == 0x04) return s->cpucon.ctl[N1G_CORE_COP];
    return 0;
}

void n1g_dev_cpucon_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value) {
    (void)size;
    if (offset == 0x00) {
        if (s->opts.profile == N1G_PROFILE_APPLE) {
            static uint32_t cpu_logs;
            if (cpu_logs < 24u) {
                cpu_logs++;
                n1g_log(s, "apple cpucon CPU write value=0x%08x", value);
            }
        }
        update_control(s, N1G_CORE_CPU, value);
    } else if (offset == 0x04) {
        if (s->opts.profile == N1G_PROFILE_APPLE) {
            static uint32_t cop_logs;
            if (cop_logs < 24u) {
                cop_logs++;
                n1g_log(s, "apple cpucon COP write value=0x%08x", value);
            }
        }
        update_control(s, N1G_CORE_COP, value);
    }
}

void n1g_dev_cpucon_tick(n1g_state_t *s) {
    for (n1g_core_t core = N1G_CORE_CPU; core < N1G_CORE_COUNT; core++) {
        uint32_t ctl = s->cpucon.ctl[core];
        if ((ctl & CPUCON_WAIT_CNT) == 0 || !s->cpu[core].halted) {
            continue;
        }
        if ((ctl & CPUCON_CNT_MASK) == 0) {
            continue;
        }

        uint32_t count = s->cpucon.wait_ticks[core];
        if (count == 0) {
            continue;
        }
        count--;
        s->cpucon.wait_ticks[core] = count;
        if (count == 0) {
            s->cpucon.ctl[core] &= ~(CPUCON_SLEEP | CPUCON_WAIT_CNT | CPUCON_WAKE_INT | 0xffu);
            s->cpu[core].halted = false;
        }
    }
}
