#include "nano1g/devices.h"

enum {
    DEVCON_DEV_RS = 0x04u,
    DEVCON_DEV_RS2 = 0x08u,
    DEVCON_DEV_EN = 0x0cu,
    DEVCON_DEV_EN2 = 0x10u,
    DEVCON_CLOCK_SOURCE = 0x20u,
    DEVCON_PLL_CONTROL = 0x34u,
    DEVCON_PLL_STATUS = 0x3cu,
    DEVCON_CACHE_PRIORITY = 0x44u
};

#define DEVCON_PLL_LOCKED 0x80000000u

static uint32_t mask_for_size(uint32_t size) {
    if (size == 1u) {
        return 0xffu;
    }
    if (size == 2u) {
        return 0xffffu;
    }
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

uint32_t n1g_dev_devcon_read(n1g_state_t *s, uint32_t offset, uint32_t size) {
    uint32_t aligned = offset & ~3u;
    if (aligned >= sizeof(s->devcon.regs)) {
        return 0;
    }

    uint32_t value = s->devcon.regs[aligned / 4u];
    if (aligned == DEVCON_PLL_STATUS) {
        value |= DEVCON_PLL_LOCKED;
    }

    return read_part(value, offset, size);
}

void n1g_dev_devcon_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value) {
    uint32_t aligned = offset & ~3u;
    if (aligned >= sizeof(s->devcon.regs)) {
        return;
    }

    uint32_t *reg = &s->devcon.regs[aligned / 4u];
    *reg = merge_write(*reg, offset, size, value);

    if (aligned == DEVCON_PLL_CONTROL) {
        s->devcon.regs[DEVCON_PLL_STATUS / 4u] |= DEVCON_PLL_LOCKED;
    }
}
