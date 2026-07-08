#ifndef NANO1G_MEMMAP_H
#define NANO1G_MEMMAP_H

#include <stdbool.h>
#include <stdint.h>

typedef enum n1g_mmap_access {
    N1G_MMAP_ACCESS_READ_DATA,
    N1G_MMAP_ACCESS_WRITE_DATA,
    N1G_MMAP_ACCESS_FETCH_CODE
} n1g_mmap_access_t;

typedef struct n1g_mmap_entry {
    uint32_t mask;
    uint32_t match;
    uint32_t target;
    bool enabled;
    bool read;
    bool write;
    bool data;
    bool code;
} n1g_mmap_entry_t;

n1g_mmap_entry_t n1g_mmap_decode(uint32_t mask_reg, uint32_t target_reg);
bool n1g_mmap_entry_matches(const n1g_mmap_entry_t *entry,
                            uint32_t addr,
                            n1g_mmap_access_t access);
uint32_t n1g_mmap_translate_addr(const n1g_mmap_entry_t *entry, uint32_t addr);
bool n1g_mmap_translate(const n1g_mmap_entry_t *entries,
                        uint32_t count,
                        uint32_t addr,
                        n1g_mmap_access_t access,
                        uint32_t *translated);

#endif
