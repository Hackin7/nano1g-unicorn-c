#include "nano1g/devices.h"

/* PP502x UARTs use a 16550-compatible register set with 32-bit spacing. */
#define UART_RBR_THR_DLL 0x00u
#define UART_IER_DLM     0x04u
#define UART_IIR_FCR     0x08u
#define UART_LCR         0x0cu
#define UART_MCR         0x10u
#define UART_LSR         0x14u
#define UART_MSR         0x18u
#define UART_SPR         0x1cu

#define UART_LCR_DLAB 0x80u
#define UART_IER_RX   0x01u
#define UART_IER_THRE 0x02u
#define UART_IER_LINE 0x04u
#define UART_IIR_NONE 0x01u
#define UART_IIR_THRE 0x02u
#define UART_IIR_RX   0x04u
#define UART_IIR_LINE 0x06u
#define UART_IIR_FIFO 0xc0u
#define UART_FCR_ENABLE   0x01u
#define UART_FCR_RX_RESET 0x02u
#define UART_FCR_TX_RESET 0x04u
#define UART_LSR_DATA_READY 0x01u
#define UART_LSR_ERROR_MASK 0x1eu
#define UART_LSR_THRE 0x20u
#define UART_LSR_TEMT 0x40u

static uint32_t mask_for_size(uint32_t size) {
    if (size == 1u) return 0xffu;
    if (size == 2u) return 0xffffu;
    return 0xffffffffu;
}

static uint32_t byte_result(uint8_t value, uint32_t offset, uint32_t size) {
    if ((offset & 3u) != 0) return 0;
    return value & mask_for_size(size);
}

static uint8_t serial_iir(const n1g_serial_channel_t *uart) {
    uint8_t fifo = (uart->fcr & UART_FCR_ENABLE) != 0 ? UART_IIR_FIFO : 0;
    if ((uart->ier & UART_IER_LINE) && (uart->lsr & UART_LSR_ERROR_MASK))
        return fifo | UART_IIR_LINE;
    if ((uart->ier & UART_IER_RX) && (uart->lsr & UART_LSR_DATA_READY))
        return fifo | UART_IIR_RX;
    if ((uart->ier & UART_IER_THRE) && uart->thre_irq_pending)
        return fifo | UART_IIR_THRE;
    return fifo | UART_IIR_NONE;
}

static void serial_update_irq(n1g_state_t *s, unsigned channel) {
    uint32_t bit = 1u << (4u + channel);
    bool pending = (serial_iir(&s->serial.channel[channel]) & UART_IIR_NONE) == 0;
    if (pending) {
        s->intc.hi_cpu_status |= bit;
        s->intc.hi_cop_status |= bit;
    } else {
        s->intc.hi_cpu_status &= ~bit;
        s->intc.hi_cop_status &= ~bit;
    }
}

uint32_t n1g_dev_serial_read(n1g_state_t *s, unsigned channel, uint32_t offset, uint32_t size) {
    if (channel >= 2u || offset >= 0x40u) return 0;
    n1g_serial_channel_t *uart = &s->serial.channel[channel];
    uint32_t reg = offset & ~3u;
    uint8_t value = 0;
    switch (reg) {
    case UART_RBR_THR_DLL:
        value = (uart->lcr & UART_LCR_DLAB) ? uart->dll : 0;
        break;
    case UART_IER_DLM:
        value = (uart->lcr & UART_LCR_DLAB) ? uart->dlm : uart->ier;
        break;
    case UART_IIR_FCR:
        value = serial_iir(uart);
        if ((value & 0x0fu) == UART_IIR_THRE) {
            uart->thre_irq_pending = false;
            serial_update_irq(s, channel);
        }
        break;
    case UART_LCR: value = uart->lcr; break;
    case UART_MCR: value = uart->mcr; break;
    case UART_LSR: value = uart->lsr | UART_LSR_THRE | UART_LSR_TEMT; break;
    case UART_MSR: value = uart->msr; break;
    case UART_SPR: value = uart->spr; break;
    default: value = uart->extended[reg - 0x20u]; break;
    }
    return byte_result(value, offset, size);
}

void n1g_dev_serial_write(n1g_state_t *s, unsigned channel, uint32_t offset, uint32_t size, uint32_t value) {
    (void)size;
    if (channel >= 2u || offset >= 0x40u || (offset & 3u) != 0) return;
    n1g_serial_channel_t *uart = &s->serial.channel[channel];
    uint32_t reg = offset & ~3u;
    uint8_t byte = (uint8_t)value;
    switch (reg) {
    case UART_RBR_THR_DLL:
        if (uart->lcr & UART_LCR_DLAB) {
            uart->dll = byte;
        } else {
            uart->tx_bytes++;
            uart->thre_irq_pending = (uart->ier & UART_IER_THRE) != 0;
        }
        break;
    case UART_IER_DLM:
        if (uart->lcr & UART_LCR_DLAB) {
            uart->dlm = byte;
        } else {
            bool enable_thre = !(uart->ier & UART_IER_THRE) && (byte & UART_IER_THRE);
            uart->ier = byte & 0x0fu;
            if (enable_thre) uart->thre_irq_pending = true;
        }
        break;
    case UART_IIR_FCR:
        uart->fcr = byte & UART_FCR_ENABLE;
        if ((byte & UART_FCR_TX_RESET) && (uart->ier & UART_IER_THRE))
            uart->thre_irq_pending = true;
        if (byte & UART_FCR_RX_RESET) uart->lsr &= ~UART_LSR_DATA_READY;
        break;
    case UART_LCR: uart->lcr = byte; break;
    case UART_MCR: uart->mcr = byte; break;
    case UART_LSR:
    case UART_MSR:
        break;
    case UART_SPR: uart->spr = byte; break;
    default: uart->extended[reg - 0x20u] = byte; break;
    }
    serial_update_irq(s, channel);
}

void n1g_dev_serial_tick(n1g_state_t *s) {
    serial_update_irq(s, 0);
    serial_update_irq(s, 1);
}
