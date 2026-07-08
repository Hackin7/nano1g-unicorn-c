#include "nano1g/memmap.h"

enum {
    MMAP_MASK_BITS = 0x3fffu,
    MMAP_ADDR_BITS = 0x3fff0000u,
    MMAP_READ = 1u << 8,
    MMAP_WRITE = 1u << 9,
    /* The public PP5024 note's 0x3a88 example is data-write-only. */
    MMAP_CODE = 1u << 10,
    MMAP_DATA = 1u << 11
};

n1g_mmap_entry_t n1g_mmap_decode(uint32_t mask_reg, uint32_t target_reg) {
    n1g_mmap_entry_t entry;
    entry.mask = (mask_reg & MMAP_MASK_BITS) << 16;
    entry.match = mask_reg & MMAP_ADDR_BITS;
    entry.target = target_reg & MMAP_ADDR_BITS;
    entry.read = (target_reg & MMAP_READ) != 0;
    entry.write = (target_reg & MMAP_WRITE) != 0;
    entry.data = (target_reg & MMAP_DATA) != 0;
    entry.code = (target_reg & MMAP_CODE) != 0;
    entry.enabled = entry.mask != 0 && (entry.read || entry.write) && (entry.data || entry.code);
    return entry;
}

static bool access_matches(const n1g_mmap_entry_t *entry, n1g_mmap_access_t access) {
    switch (access) {
    case N1G_MMAP_ACCESS_READ_DATA:
        return entry->read && entry->data;
    case N1G_MMAP_ACCESS_WRITE_DATA:
        return entry->write && entry->data;
    case N1G_MMAP_ACCESS_FETCH_CODE:
        return entry->read && entry->code;
    default:
        return false;
    }
}

bool n1g_mmap_entry_matches(const n1g_mmap_entry_t *entry,
                            uint32_t addr,
                            n1g_mmap_access_t access) {
    if (!entry || !entry->enabled || !access_matches(entry, access)) {
        return false;
    }
    return (addr & entry->mask) == (entry->match & entry->mask);
}

uint32_t n1g_mmap_translate_addr(const n1g_mmap_entry_t *entry, uint32_t addr) {
    return (addr & ~entry->mask) | (entry->target & entry->mask);
}

bool n1g_mmap_translate(const n1g_mmap_entry_t *entries,
                        uint32_t count,
                        uint32_t addr,
                        n1g_mmap_access_t access,
                        uint32_t *translated) {
    if (!entries || !translated) {
        return false;
    }
    for (uint32_t i = 0; i < count; i++) {
        if (n1g_mmap_entry_matches(&entries[i], addr, access)) {
            *translated = n1g_mmap_translate_addr(&entries[i], addr);
            return true;
        }
    }
    return false;
}
