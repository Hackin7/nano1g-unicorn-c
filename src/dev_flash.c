#include "nano1g/devices.h"

uint32_t n1g_dev_flash_read(n1g_state_t *s, uint32_t offset, uint32_t size) {
    uint32_t v = 0;
    if (offset + size <= N1G_FLASH_SIZE) {
        for (uint32_t i = 0; i < size; i++) {
            v |= ((uint32_t)s->flash.bytes[offset + i]) << (8u * i);
        }
    }
    return v;
}

void n1g_dev_flash_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value) {
    (void)s;
    (void)offset;
    (void)size;
    (void)value;
}
