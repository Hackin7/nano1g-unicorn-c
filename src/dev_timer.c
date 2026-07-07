#include "nano1g/devices.h"

uint32_t n1g_dev_timer_read(n1g_state_t *s, uint32_t offset, uint32_t size) {
    (void)size;
    switch (offset) {
    case 0x00: return s->timer.cfg[0];
    case 0x04: return s->timer.val[0];
    case 0x08: return s->timer.cfg[1];
    case 0x0c: return s->timer.val[1];
    case 0x10: return s->timer.usec;
    case 0x14: return s->timer.usec / 1000000u;
    default: return 0;
    }
}

void n1g_dev_timer_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value) {
    (void)size;
    switch (offset) {
    case 0x00: s->timer.cfg[0] = value; break;
    case 0x04: s->timer.val[0] = value; break;
    case 0x08: s->timer.cfg[1] = value; break;
    case 0x0c: s->timer.val[1] = value; break;
    default: break;
    }
}

void n1g_dev_timer_tick(n1g_state_t *s) {
    s->timer.usec++;
    for (int i = 0; i < 2; i++) {
        if (s->timer.cfg[i] & 0x80000000u) {
            if (s->timer.val[i] > 0) {
                s->timer.val[i]--;
            } else {
                s->intc.cpu_status |= (1u << i);
                s->intc.cop_status |= (1u << i);
                s->timer.val[i] = s->timer.cfg[i] & 0x00ffffffu;
            }
        }
    }
}
