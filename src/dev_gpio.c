#include "nano1g/devices.h"

static uint32_t mask_for_size(uint32_t size) {
    if (size == 1) return 0xffu;
    if (size == 2) return 0xffffu;
    return 0xffffffffu;
}

static bool is_input_value_reg(uint32_t aligned) {
    uint32_t local = aligned & 0x7fu;
    return local >= 0x30u && local <= 0x3cu;
}

uint32_t n1g_dev_gpio_read(n1g_state_t *s, uint32_t offset, uint32_t size) {
    uint32_t aligned = offset & ~3u;
    if (offset < sizeof(s->gpio.regs)) {
        uint32_t shift = (offset & 3u) * 8u;
        uint32_t value = is_input_value_reg(aligned) ? 0xffffffffu : s->gpio.regs[offset / 4u];
        return (value >> shift) & mask_for_size(size);
    }
    return 0;
}

void n1g_dev_gpio_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value) {
    if (offset >= sizeof(s->gpio.regs)) {
        return;
    }

    if (offset >= 0x800u) {
        uint32_t target = offset - 0x800u;
        if (target < sizeof(s->gpio.regs)) {
            if (is_input_value_reg(target & ~3u)) {
                return;
            }
            uint32_t *reg = &s->gpio.regs[target / 4u];
            uint32_t shift = (target & 3u) * 8u;
            uint32_t mask = ((value >> 8) & 0xffu) << shift;
            uint32_t bits = (value & 0xffu) << shift;
            *reg = (*reg & ~mask) | (bits & mask);
        }
        return;
    }

    if (is_input_value_reg(offset & ~3u)) {
        return;
    }

    uint32_t shift = (offset & 3u) * 8u;
    uint32_t mask = mask_for_size(size) << shift;
    uint32_t bits = (value & mask_for_size(size)) << shift;
    s->gpio.regs[offset / 4u] = (s->gpio.regs[offset / 4u] & ~mask) | (bits & mask);
}
