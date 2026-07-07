#include "nano1g/devices.h"

uint32_t n1g_dev_gpio_read(n1g_state_t *s, uint32_t offset, uint32_t size) {
    (void)size;
    if (offset < sizeof(s->gpio.regs)) {
        return s->gpio.regs[offset / 4u];
    }
    return 0;
}

void n1g_dev_gpio_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value) {
    (void)size;
    if (offset < sizeof(s->gpio.regs)) {
        s->gpio.regs[offset / 4u] = value;
    }
}
