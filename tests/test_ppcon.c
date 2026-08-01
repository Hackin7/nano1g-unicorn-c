#include "nano1g/devices.h"

#include <stdio.h>
#include <string.h>

static int expect_u32(uint32_t actual, uint32_t expected, const char *message) {
    if (actual == expected) {
        return 0;
    }
    fprintf(stderr, "%s: got 0x%08x, expected 0x%08x\n", message, actual, expected);
    return 1;
}

int main(void) {
    n1g_state_t s;
    memset(&s, 0, sizeof(s));

    int failed = expect_u32(n1g_dev_ppcon_read(&s, 0x3cu, 4u),
                            0u,
                            "XMB RAM config did not reset to zero");

    n1g_dev_ppcon_write(&s, 0x3cu, 4u, 0x00400000u);
    failed |= expect_u32(n1g_dev_ppcon_read(&s, 0x3cu, 4u),
                         0x40400000u,
                         "self-refresh request did not assert status");

    n1g_dev_ppcon_write(&s, 0x3eu, 2u, 0x0000u);
    failed |= expect_u32(n1g_dev_ppcon_read(&s, 0x3cu, 4u),
                         0u,
                         "clearing self-refresh request did not clear status");

    n1g_dev_ppcon_write(&s, 0x3cu, 4u, 0x40001234u);
    failed |= expect_u32(n1g_dev_ppcon_read(&s, 0x3cu, 4u),
                         0x00001234u,
                         "guest-controlled status bit was not treated as read-only");

    if (!failed) {
        puts("ppcon unit ok");
    }
    return failed ? 1 : 0;
}
