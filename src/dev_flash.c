#include "nano1g/devices.h"

enum {
    FLASH_MODE_ARRAY = 0,
    FLASH_MODE_ID = 1,
    FLASH_MODE_STATUS = 2,
    FLASH_MODE_CFI = 3,
};

static void save_window(n1g_state_t *s) {
    if (!s->flash.saved_window_valid) {
        for (uint32_t i = 0; i < sizeof(s->flash.saved_window); i++) {
            s->flash.saved_window[i] = s->flash.bytes[i];
        }
        s->flash.saved_window_valid = true;
    }
}

static void restore_window(n1g_state_t *s) {
    if (s->flash.saved_window_valid) {
        for (uint32_t i = 0; i < sizeof(s->flash.saved_window); i++) {
            s->flash.bytes[i] = s->flash.saved_window[i];
        }
        s->flash.saved_window_valid = false;
    }
}

static void put16(uint8_t *p, uint32_t offset, uint16_t value) {
    p[offset + 0u] = (uint8_t)value;
    p[offset + 1u] = (uint8_t)(value >> 8u);
}

static void enter_array(n1g_state_t *s) {
    restore_window(s);
    s->flash.mode = FLASH_MODE_ARRAY;
    s->flash.program_pending = false;
    s->flash.erase_pending = false;
}

static void enter_status(n1g_state_t *s) {
    save_window(s);
    s->flash.mode = FLASH_MODE_STATUS;
    s->flash.bytes[0] = s->flash.status;
}

static void enter_id(n1g_state_t *s) {
    save_window(s);
    s->flash.mode = FLASH_MODE_ID;
    for (uint32_t i = 0; i < sizeof(s->flash.saved_window); i++) {
        s->flash.bytes[i] = 0xffu;
    }
    put16(s->flash.bytes, 0x00u, 0x00bfu); /* SST manufacturer ID. */
    put16(s->flash.bytes, 0x02u, 0x273fu); /* SST39WF800A, 1 MiB PP-era NOR. */
}

static void enter_cfi(n1g_state_t *s) {
    save_window(s);
    s->flash.mode = FLASH_MODE_CFI;
    for (uint32_t i = 0; i < sizeof(s->flash.saved_window); i++) {
        s->flash.bytes[i] = 0;
    }

    /* x16 CFI query data appears at byte offsets 2 * word_address. */
    s->flash.bytes[0x20u] = 'Q';
    s->flash.bytes[0x22u] = 'R';
    s->flash.bytes[0x24u] = 'Y';
    put16(s->flash.bytes, 0x26u, 0x0001u); /* Intel command set. */
    put16(s->flash.bytes, 0x4eu, 0x0010u); /* 2^20 byte device size. */
}

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
    uint8_t cmd = (uint8_t)value;

    if (s->flash.status == 0) {
        s->flash.status = 0x80u;
    }

    if (s->flash.program_pending) {
        for (uint32_t i = 0; i < size && offset + i < N1G_FLASH_SIZE; i++) {
            uint8_t byte = (uint8_t)(value >> (8u * i));
            s->flash.bytes[offset + i] &= byte;
        }
        s->flash.status = 0x80u;
        s->flash.program_pending = false;
        enter_status(s);
        return;
    }

    if (s->flash.erase_pending) {
        if (cmd == 0xd0u) {
            uint32_t base = offset & ~0xffffu;
            for (uint32_t i = 0; i < 0x10000u && base + i < N1G_FLASH_SIZE; i++) {
                s->flash.bytes[base + i] = 0xffu;
            }
            s->flash.status = 0x80u;
            enter_status(s);
        } else {
            s->flash.erase_pending = false;
        }
        return;
    }

    switch (cmd) {
    case 0x10u:
    case 0x40u:
        restore_window(s);
        s->flash.program_pending = true;
        s->flash.mode = FLASH_MODE_ARRAY;
        break;
    case 0x20u:
        restore_window(s);
        s->flash.erase_pending = true;
        s->flash.mode = FLASH_MODE_ARRAY;
        break;
    case 0x50u:
        s->flash.status = 0x80u;
        break;
    case 0x70u:
        enter_status(s);
        break;
    case 0x90u:
        enter_id(s);
        break;
    case 0x98u:
        enter_cfi(s);
        break;
    case 0xffu:
    case 0xf0u:
        enter_array(s);
        break;
    default:
        break;
    }
}
