#include "nano1g/devices.h"

#include "nano1g/state.h"

static uint32_t mask_for_size(uint32_t size) {
    if (size == 1u) return 0xffu;
    if (size == 2u) return 0xffffu;
    return 0xffffffffu;
}

static uint32_t merge_write(uint32_t old_value, uint32_t offset, uint32_t size, uint32_t value) {
    uint32_t shift = (offset & 3u) * 8u;
    uint32_t mask = mask_for_size(size) << shift;
    return (old_value & ~mask) | ((value << shift) & mask);
}

static uint32_t read_part(uint32_t value, uint32_t offset, uint32_t size) {
    uint32_t shift = (offset & 3u) * 8u;
    return (value >> shift) & mask_for_size(size);
}

uint32_t n1g_dev_timer_read(n1g_state_t *s, uint32_t offset, uint32_t size) {
    uint32_t aligned = offset & ~3u;
    uint32_t value = 0;
    switch (aligned) {
    case 0x00: value = s->timer.cfg[0]; break;
    case 0x04:
        s->intc.cpu_status &= ~(1u << 0);
        s->intc.cop_status &= ~(1u << 0);
        value = s->timer.val[0];
        break;
    case 0x08: value = s->timer.cfg[1]; break;
    case 0x0c:
        s->intc.cpu_status &= ~(1u << 1);
        s->intc.cop_status &= ~(1u << 1);
        value = s->timer.val[1];
        break;
    case 0x10: value = s->timer.usec; break;
    case 0x14: value = s->timer.usec / 1000000u; break;
    default: value = 0; break;
    }
    return read_part(value, offset, size);
}

void n1g_dev_timer_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value) {
    uint32_t aligned = offset & ~3u;
    switch (aligned) {
    case 0x00:
        value = merge_write(s->timer.cfg[0], offset, size, value);
        if ((value & 0x80000000u) && !(s->timer.cfg[0] & 0x80000000u)) {
            s->timer.val[0] = value & 0x1fffffffu;
        }
        s->timer.cfg[0] = value;
        break;
    case 0x04: s->timer.val[0] = merge_write(s->timer.val[0], offset, size, value); break;
    case 0x08:
        value = merge_write(s->timer.cfg[1], offset, size, value);
        if ((value & 0x80000000u) && !(s->timer.cfg[1] & 0x80000000u)) {
            s->timer.val[1] = value & 0x1fffffffu;
        }
        s->timer.cfg[1] = value;
        break;
    case 0x0c: s->timer.val[1] = merge_write(s->timer.val[1], offset, size, value); break;
    default: break;
    }
}

void n1g_dev_timer_tick(n1g_state_t *s) {
    s->timer.usec++;
    s->timer.cfg_tick_phase = (s->timer.cfg_tick_phase + 1u) % s->opts.timer_divider;
    if (s->timer.cfg_tick_phase != 0u) {
        return;
    }
    for (int i = 0; i < 2; i++) {
        uint32_t counter = s->timer.cfg[i] & 0x1fffffffu;
        bool repeat = (s->timer.cfg[i] & 0x40000000u) != 0;
        bool enabled = (s->timer.cfg[i] & 0x80000000u) != 0;

        if (enabled && counter != 0 && s->timer.val[i] != 0) {
            s->timer.val[i]--;
            if (s->timer.val[i] == 0) {
                s->intc.cpu_status |= (1u << i);
                s->intc.cop_status |= (1u << i);
                if (repeat) {
                    s->timer.val[i] = counter;
                } else {
                    s->timer.cfg[i] &= ~0x80000000u;
                }
            }
        }
    }
}
