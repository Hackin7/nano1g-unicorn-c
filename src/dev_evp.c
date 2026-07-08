#include "nano1g/devices.h"

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

void n1g_dev_evp_init(n1g_state_t *s) {
    for (uint32_t i = 0; i < 8u; i++) {
        s->evp.regs[i] = i * 4u;
    }
}

uint32_t n1g_dev_evp_read(n1g_state_t *s, uint32_t offset, uint32_t size) {
    uint32_t aligned = offset & ~3u;
    if (aligned >= sizeof(s->evp.regs)) {
        return 0;
    }
    return read_part(s->evp.regs[aligned / 4u], offset, size);
}

void n1g_dev_evp_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value) {
    uint32_t aligned = offset & ~3u;
    if (aligned >= sizeof(s->evp.regs)) {
        return;
    }
    uint32_t *reg = &s->evp.regs[aligned / 4u];
    *reg = merge_write(*reg, offset, size, value);
}
