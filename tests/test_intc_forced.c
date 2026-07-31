#include "nano1g/cpu_unicorn.h"
#include "nano1g/devices.h"
#include "nano1g/trace.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

void n1g_cpu_raise_irq(n1g_state_t *s, n1g_core_t core) {
    (void)s;
    (void)core;
}

void n1g_cpu_raise_fiq(n1g_state_t *s, n1g_core_t core) {
    (void)s;
    (void)core;
}

void n1g_log(n1g_state_t *s, const char *fmt, ...) {
    (void)s;
    (void)fmt;
}

static int expect_u32(uint32_t actual, uint32_t expected, const char *label) {
    if (actual == expected) {
        return 0;
    }
    fprintf(stderr, "%s: got 0x%08x expected 0x%08x\n", label, actual, expected);
    return 1;
}

int main(void) {
    n1g_state_t s;
    memset(&s, 0, sizeof(s));

    n1g_dev_intc_write(&s, 0x018u, 4, 1u << 5);
    if (expect_u32(n1g_dev_intc_read(&s, 0x014u, 4), 1u << 5, "low forced status") ||
        expect_u32(n1g_dev_intc_read(&s, 0x000u, 4), 1u << 5, "CPU low status") ||
        expect_u32(n1g_dev_intc_read(&s, 0x004u, 4), 1u << 5, "COP low status")) {
        return 1;
    }

    s.intc.cpu_status |= 1u << 5;
    n1g_dev_intc_write(&s, 0x01cu, 4, 1u << 5);
    if (expect_u32(n1g_dev_intc_read(&s, 0x014u, 4), 0u, "low forced clear") ||
        expect_u32(n1g_dev_intc_read(&s, 0x000u, 4), 1u << 5, "device IRQ preserved")) {
        return 1;
    }

    s.intc.cpu_status = 0;
    n1g_dev_intc_write(&s, 0x118u, 4, 1u << 7);
    if (expect_u32(n1g_dev_intc_read(&s, 0x114u, 4), 1u << 7, "high forced status") ||
        expect_u32(n1g_dev_intc_read(&s, 0x100u, 4), 1u << 7, "CPU high status") ||
        expect_u32(n1g_dev_intc_read(&s, 0x104u, 4), 1u << 7, "COP high status") ||
        expect_u32(n1g_dev_intc_read(&s, 0x000u, 4), 1u << 30, "high summary")) {
        return 1;
    }

    n1g_dev_intc_write(&s, 0x11cu, 4, 1u << 7);
    return expect_u32(n1g_dev_intc_read(&s, 0x114u, 4), 0u, "high forced clear");
}
