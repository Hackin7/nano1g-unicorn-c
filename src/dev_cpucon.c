#include "nano1g/devices.h"

uint32_t n1g_dev_cpucon_read(n1g_state_t *s, uint32_t offset, uint32_t size) {
    (void)size;
    if (offset == 0x00) return s->cpucon.ctl[N1G_CORE_CPU];
    if (offset == 0x04) return s->cpucon.ctl[N1G_CORE_COP];
    return 0;
}

void n1g_dev_cpucon_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value) {
    (void)size;
    if (offset == 0x00) {
        s->cpucon.ctl[N1G_CORE_CPU] = value;
        s->cpu[N1G_CORE_CPU].halted = (value & 0xc0000000u) != 0;
    } else if (offset == 0x04) {
        s->cpucon.ctl[N1G_CORE_COP] = value;
        s->cpu[N1G_CORE_COP].halted = (value & 0xc0000000u) != 0;
    }
}
