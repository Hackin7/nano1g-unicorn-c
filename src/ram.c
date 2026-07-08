#include "nano1g/ram.h"

#include "nano1g/trace.h"

#include <stdlib.h>
#include <string.h>

bool n1g_ram_init(n1g_state_t *s) {
    s->ram.sdram = (uint8_t *)malloc(N1G_SDRAM_SIZE);
    s->ram.fastram = (uint8_t *)malloc(N1G_FASTRAM_SIZE);
    if (!s->ram.sdram || !s->ram.fastram) {
        return false;
    }
    uint8_t fill = s->opts.ram_fill_zero ? 0x00u : 0x2du;
    memset(s->ram.sdram, fill, N1G_SDRAM_SIZE);
    memset(s->ram.fastram, fill, N1G_FASTRAM_SIZE);
    n1g_info(s, "ram fill byte=0x%02x", fill);
    return true;
}

void n1g_ram_destroy(n1g_state_t *s) {
    free(s->ram.sdram);
    free(s->ram.fastram);
    s->ram.sdram = NULL;
    s->ram.fastram = NULL;
}

uint8_t *n1g_ram_ptr(n1g_state_t *s, uint32_t addr, size_t size) {
    if (addr >= N1G_SDRAM_BASE && addr + size - 1 <= N1G_SDRAM_MIRROR_END) {
        uint32_t off = (addr - N1G_SDRAM_BASE) % N1G_SDRAM_SIZE;
        if (off + size <= N1G_SDRAM_SIZE) {
            return s->ram.sdram + off;
        }
    }
    if (addr >= N1G_SDRAM_ALIAS_BASE && addr + size - 1 <= N1G_SDRAM_ALIAS_END) {
        uint32_t off = (addr - N1G_SDRAM_ALIAS_BASE) % N1G_SDRAM_SIZE;
        if (off + size <= N1G_SDRAM_SIZE) {
            return s->ram.sdram + off;
        }
    }
    if (s->low0_map == N1G_LOW0_RAM && addr + size - 1 < N1G_SDRAM_SIZE) {
        return s->ram.sdram + addr;
    }
    if (addr >= N1G_FASTRAM_BASE && addr + size - 1 < N1G_FASTRAM_BASE + N1G_FASTRAM_SIZE) {
        return s->ram.fastram + (addr - N1G_FASTRAM_BASE);
    }
    return NULL;
}

static uint32_t load_le(const uint8_t *p, uint32_t size) {
    uint32_t v = 0;
    for (uint32_t i = 0; i < size; i++) {
        v |= ((uint32_t)p[i]) << (8u * i);
    }
    return v;
}

static void store_le(uint8_t *p, uint32_t size, uint32_t value) {
    for (uint32_t i = 0; i < size; i++) {
        p[i] = (uint8_t)(value >> (8u * i));
    }
}

bool n1g_ram_read(n1g_state_t *s, uint32_t addr, uint32_t size, uint32_t *out) {
    uint8_t *p = n1g_ram_ptr(s, addr, size);
    if (!p) {
        return false;
    }
    *out = load_le(p, size);
    return true;
}

bool n1g_ram_write(n1g_state_t *s, uint32_t addr, uint32_t size, uint32_t value) {
    uint8_t *p = n1g_ram_ptr(s, addr, size);
    if (!p) {
        return false;
    }
    store_le(p, size, value);
    return true;
}
