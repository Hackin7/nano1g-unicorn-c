#include "nano1g/devices.h"

uint32_t n1g_dev_i2c_read(n1g_state_t *s, uint32_t offset, uint32_t size) {
    (void)size;
    if (offset < sizeof(s->i2c.regs)) {
        return s->i2c.regs[offset / 4u];
    }
    return 0;
}

void n1g_dev_i2c_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value) {
    (void)size;
    if (offset < sizeof(s->i2c.regs)) {
        s->i2c.regs[offset / 4u] = value;
    }
}
