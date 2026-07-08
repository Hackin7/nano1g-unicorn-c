#include "nano1g/memmap.h"

#include <stdio.h>

static int expect_true(bool value, const char *what) {
    if (value) {
        return 0;
    }
    fprintf(stderr, "expected true: %s\n", what);
    return 1;
}

static int expect_false(bool value, const char *what) {
    if (!value) {
        return 0;
    }
    fprintf(stderr, "expected false: %s\n", what);
    return 1;
}

static int expect_u32(uint32_t actual, uint32_t expected, const char *what) {
    if (actual == expected) {
        return 0;
    }
    fprintf(stderr, "%s: got 0x%08x expected 0x%08x\n", what, actual, expected);
    return 1;
}

int main(void) {
    int failed = 0;

    n1g_mmap_entry_t all_sdram = n1g_mmap_decode(0x00003a00u, 0x10003f84u);
    failed |= expect_true(all_sdram.enabled, "all-access SDRAM map enabled");
    failed |= expect_true(n1g_mmap_entry_matches(&all_sdram, 0x00000120u, N1G_MMAP_ACCESS_READ_DATA),
                          "all-access map matches data read");
    failed |= expect_true(n1g_mmap_entry_matches(&all_sdram, 0x00000120u, N1G_MMAP_ACCESS_WRITE_DATA),
                          "all-access map matches data write");
    failed |= expect_true(n1g_mmap_entry_matches(&all_sdram, 0x00000120u, N1G_MMAP_ACCESS_FETCH_CODE),
                          "all-access map matches code fetch");
    failed |= expect_u32(n1g_mmap_translate_addr(&all_sdram, 0x00000120u),
                         0x10000120u,
                         "all-access low0 to SDRAM translation");

    n1g_mmap_entry_t data_write_low0 = n1g_mmap_decode(0x00003a00u, 0x00003a88u);
    failed |= expect_true(data_write_low0.enabled, "data-write-only low0 map enabled");
    failed |= expect_false(n1g_mmap_entry_matches(&data_write_low0, 0x00000120u, N1G_MMAP_ACCESS_READ_DATA),
                           "data-write-only map rejects data read");
    failed |= expect_true(n1g_mmap_entry_matches(&data_write_low0, 0x00000120u, N1G_MMAP_ACCESS_WRITE_DATA),
                          "data-write-only map matches data write");
    failed |= expect_false(n1g_mmap_entry_matches(&data_write_low0, 0x00000120u, N1G_MMAP_ACCESS_FETCH_CODE),
                           "data-write-only map rejects code fetch");

    n1g_mmap_entry_t entries[2];
    entries[0] = n1g_mmap_decode(0x00003a00u, 0x20003f84u);
    entries[1] = n1g_mmap_decode(0x00003a00u, 0x10003f84u);
    uint32_t translated = 0;
    failed |= expect_true(n1g_mmap_translate(entries,
                                             2u,
                                             0x00000120u,
                                             N1G_MMAP_ACCESS_READ_DATA,
                                             &translated),
                          "priority translate matches");
    failed |= expect_u32(translated, 0x20000120u, "first matching mmap entry wins");

    return failed ? 1 : 0;
}
