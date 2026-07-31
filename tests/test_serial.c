#include "nano1g/devices.h"

#include <stdio.h>
#include <string.h>

#define UART_DLL_THR 0x00u
#define UART_DLM_IER 0x04u
#define UART_IIR_FCR 0x08u
#define UART_LCR     0x0cu
#define UART_LSR     0x14u

static int expect_u32(uint32_t actual, uint32_t expected, const char *message) {
    if (actual != expected) {
        fprintf(stderr, "%s: got 0x%08x, expected 0x%08x\n", message, actual, expected);
        return 1;
    }
    return 0;
}

int main(void) {
    n1g_state_t s;
    memset(&s, 0, sizeof(s));

    if (expect_u32(n1g_dev_serial_read(&s, 0, UART_LSR, 4), 0x60u, "default LSR") ||
        expect_u32(n1g_dev_serial_read(&s, 0, UART_IIR_FCR, 4), 0x01u, "default IIR"))
        return 1;

    n1g_dev_serial_write(&s, 0, UART_LCR, 4, 0x80u);
    n1g_dev_serial_write(&s, 0, UART_DLL_THR, 4, 0x12u);
    n1g_dev_serial_write(&s, 0, UART_DLM_IER, 4, 0x34u);
    if (expect_u32(n1g_dev_serial_read(&s, 0, UART_DLL_THR, 4), 0x12u, "DLL readback") ||
        expect_u32(n1g_dev_serial_read(&s, 0, UART_DLM_IER, 4), 0x34u, "DLM readback"))
        return 1;

    n1g_dev_serial_write(&s, 0, UART_LCR, 4, 0x03u);
    n1g_dev_serial_write(&s, 0, UART_IIR_FCR, 4, 0x07u);
    n1g_dev_serial_write(&s, 0, UART_DLM_IER, 4, 0x02u);
    if (expect_u32(s.intc.hi_cpu_status, 1u << 4, "serial 0 high IRQ") ||
        expect_u32(n1g_dev_serial_read(&s, 0, UART_IIR_FCR, 4), 0xc2u, "THRE IIR") ||
        expect_u32(s.intc.hi_cpu_status, 0u, "IIR clears THRE interrupt"))
        return 1;

    n1g_dev_serial_write(&s, 1, UART_DLM_IER, 4, 0x02u);
    if (expect_u32(s.intc.hi_cpu_status, 1u << 5, "serial 1 high IRQ"))
        return 1;
    (void)n1g_dev_serial_read(&s, 1, UART_IIR_FCR, 4);
    return expect_u32(s.intc.hi_cpu_status, 0u, "serial 1 THRE clear");
}
