#include "nano1g/devices.h"

enum {
    PPCON_PP_VER1 = 0x00u,
    PPCON_PP_VER2 = 0x04u,
    PPCON_STRAP_A = 0x08u,
    PPCON_STRAP_B = 0x0cu
};

static uint32_t mask_for_size(uint32_t size) {
    if (size == 1u) {
        return 0xffu;
    }
    if (size == 2u) {
        return 0xffffu;
    }
    return 0xffffffffu;
}

static uint32_t read_part(uint32_t value, uint32_t offset, uint32_t size) {
    uint32_t shift = (offset & 3u) * 8u;
    return (value >> shift) & mask_for_size(size);
}

static uint32_t merge_write(uint32_t old_value, uint32_t offset, uint32_t size, uint32_t value) {
    uint32_t shift = (offset & 3u) * 8u;
    uint32_t mask = mask_for_size(size) << shift;
    return (old_value & ~mask) | ((value << shift) & mask);
}

static uint32_t default_value(uint32_t aligned) {
    switch (aligned) {
    case PPCON_PP_VER1:
        return 0x30325050u; /* "PP20": PP5022-class Nano fast-RAM layout. */
    case PPCON_PP_VER2:
        return 0x20443032u; /* "20D " */
    case PPCON_STRAP_A:
    case PPCON_STRAP_B:
        return 0;
    case 0x28u:
        return 0x80u;
    case 0x30u:
        return 0x08000000u;
    default:
        return 0;
    }
}

uint32_t n1g_dev_ppcon_read(n1g_state_t *s, uint32_t offset, uint32_t size) {
    uint32_t aligned = offset & ~3u;
    if (aligned >= sizeof(s->ppcon.regs)) {
        return 0;
    }

    uint32_t value = s->ppcon.regs[aligned / 4u];
    if (value == 0) {
        value = default_value(aligned);
    }
    return read_part(value, offset, size);
}

void n1g_dev_ppcon_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value) {
    uint32_t aligned = offset & ~3u;
    if (aligned >= sizeof(s->ppcon.regs)) {
        return;
    }

    uint32_t *reg = &s->ppcon.regs[aligned / 4u];
    uint32_t old_value = *reg;
    if (old_value == 0) {
        old_value = default_value(aligned);
    }
    *reg = merge_write(old_value, offset, size, value);
}
