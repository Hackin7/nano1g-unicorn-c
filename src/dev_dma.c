#include "nano1g/devices.h"

#include "nano1g/ram.h"

uint32_t n1g_dev_dma_read(n1g_state_t *s, uint32_t offset, uint32_t size) {
    (void)size;
    if (offset < sizeof(s->dma.regs)) {
        return s->dma.regs[offset / 4u];
    }
    return 0;
}

void n1g_dev_dma_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value) {
    (void)size;
    if (offset < sizeof(s->dma.regs)) {
        s->dma.regs[offset / 4u] = value;
    }
}

void n1g_dev_dma_tick(n1g_state_t *s) {
    (void)s;
}
