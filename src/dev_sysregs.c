#include "nano1g/devices.h"

static uint32_t mask_for_size(uint32_t size) {
    if (size == 1u) return 0xffu;
    if (size == 2u) return 0xffffu;
    return 0xffffffffu;
}

static uint32_t read_reg(const uint32_t *regs, uint32_t bytes, uint32_t offset, uint32_t size) {
    uint32_t aligned = offset & ~3u;
    if (aligned >= bytes) return 0u;
    return (regs[aligned / 4u] >> ((offset & 3u) * 8u)) & mask_for_size(size);
}

static void write_reg(uint32_t *regs, uint32_t bytes, uint32_t offset, uint32_t size, uint32_t value) {
    uint32_t aligned = offset & ~3u;
    if (aligned >= bytes) return;
    uint32_t shift = (offset & 3u) * 8u;
    uint32_t mask = mask_for_size(size) << shift;
    regs[aligned / 4u] = (regs[aligned / 4u] & ~mask) | ((value << shift) & mask);
}

uint32_t n1g_dev_sysreg_60003000_read(n1g_state_t *s, uint32_t offset, uint32_t size) {
    return read_reg(s->sysregs.pp_60003000, sizeof(s->sysregs.pp_60003000), offset, size);
}

void n1g_dev_sysreg_60003000_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value) {
    write_reg(s->sysregs.pp_60003000, sizeof(s->sysregs.pp_60003000), offset, size, value);
}

uint32_t n1g_dev_sysreg_70003800_read(n1g_state_t *s, uint32_t offset, uint32_t size) {
    return read_reg(s->sysregs.periph_70003800, sizeof(s->sysregs.periph_70003800), offset, size);
}

void n1g_dev_sysreg_70003800_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value) {
    write_reg(s->sysregs.periph_70003800, sizeof(s->sysregs.periph_70003800), offset, size, value);
}
