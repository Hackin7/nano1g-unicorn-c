#include "nano1g/devices.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    n1g_state_t s;
    memset(&s, 0, sizeof(s));
    s.opts.rtc_usec_per_tick = 512u;
    s.opts.timer_divider = 1u;

    n1g_dev_timer_write(&s, 0x00u, 4, 0xc00003fcu);
    n1g_dev_timer_tick(&s);
    if (s.timer.val[0] != 508u || s.intc.cpu_status != 0u) {
        fprintf(stderr, "scaled timer first step failed: val=%u irq=0x%08x\n",
                s.timer.val[0], s.intc.cpu_status);
        return 1;
    }

    n1g_dev_timer_tick(&s);
    if (s.timer.val[0] != 1016u || (s.intc.cpu_status & 1u) == 0u) {
        fprintf(stderr, "scaled timer expiry failed: val=%u irq=0x%08x\n",
                s.timer.val[0], s.intc.cpu_status);
        return 1;
    }

    uint32_t value = n1g_dev_timer_read(&s, 0x04u, 4);
    if (value != 1016u || (s.intc.cpu_status & 1u) != 0u) {
        fprintf(stderr, "scaled timer acknowledge failed: val=%u irq=0x%08x\n",
                value, s.intc.cpu_status);
        return 1;
    }
    return 0;
}
