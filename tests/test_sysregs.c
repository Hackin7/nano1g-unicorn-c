#include "nano1g/devices.h"

#include <stdio.h>
#include <string.h>

static int expect_u32(uint32_t actual, uint32_t expected, const char *message) {
    if (actual == expected) return 0;
    fprintf(stderr, "%s: got 0x%08x, expected 0x%08x\n", message, actual, expected);
    return 1;
}

int main(void) {
    n1g_state_t s;
    memset(&s, 0, sizeof(s));

    n1g_dev_sysreg_60003000_write(&s, 0x10u, 4u, 0x12345678u);
    n1g_dev_sysreg_60003000_write(&s, 0x11u, 1u, 0xa5u);
    int failed = expect_u32(n1g_dev_sysreg_60003000_read(&s, 0x10u, 4u),
                            0x1234a578u,
                            "0x60003000 bank did not preserve a byte merge");

    n1g_dev_sysreg_70003800_write(&s, 0x24u, 4u, 0x80000040u);
    n1g_dev_sysreg_70003800_write(&s, 0x26u, 2u, 0x55aau);
    failed |= expect_u32(n1g_dev_sysreg_70003800_read(&s, 0x24u, 4u),
                         0x55aa0040u,
                         "0x70003800 bank did not preserve a halfword merge");
    failed |= expect_u32(n1g_dev_sysreg_70003800_read(&s, 0x24u, 1u),
                         0x40u,
                         "0x70003800 bank byte read was incorrect");

    if (!failed) puts("sysregs unit ok");
    return failed ? 1 : 0;
}
