#include "nano1g/devices.h"

#include "nano1g/map.h"
#include "nano1g/trace.h"

/* PP502x IIS controller (Rockbox pp5020.h register layout).
 *
 * Playback model: the guest (directly or via the DMA engine) pushes 16-bit
 * sample halfwords into the TX FIFO at IISFIFO_WR. When IIS_TXFIFOEN is set,
 * the FIFO drains at the modeled sample rate against the guest microsecond
 * clock, so track position advances in guest time instead of instantaneously.
 */

#define IIS_RESET    (1u << 31)
#define IIS_TXFIFOEN (1u << 29)
#define IIS_RXFIFOEN (1u << 28)
#define IIS_IRQTX    (1u << 1)
#define IIS_IRQRX    (1u << 0)

#define IIS_FIFO_TXCLR (1u << 8)
#define IIS_FIFO_RXCLR (1u << 12)

#define IIS_IRQ_BIT (1u << 10)

/* 16-bit halfword slots in the TX FIFO. Rockbox reserves "16*4" bytes as one
 * FIFO's worth of data, i.e. 32 halfwords. */
#define N1G_I2S_TX_DEPTH 32u

/* Stereo 16-bit at 44.1 kHz: 88200 halfwords per second of guest time. */
#define N1G_I2S_HALFWORDS_PER_SEC 88200u

uint32_t n1g_dev_i2s_tx_free(const n1g_state_t *s) {
    return N1G_I2S_TX_DEPTH - s->i2s.tx_fill;
}

static void i2s_update_irq(n1g_state_t *s) {
    /* Level-triggered TX interrupt: asserted while the FIFO has room and the
     * guest enabled IIS_IRQTX. Used by FIFO-mode PCM drivers. */
    bool assert_tx = (s->i2s.config & IIS_IRQTX) != 0 &&
                     (s->i2s.config & IIS_TXFIFOEN) != 0 &&
                     n1g_dev_i2s_tx_free(s) >= 2u;
    if (assert_tx) {
        s->intc.cpu_status |= IIS_IRQ_BIT;
        s->intc.cop_status |= IIS_IRQ_BIT;
    } else {
        s->intc.cpu_status &= ~IIS_IRQ_BIT;
        s->intc.cop_status &= ~IIS_IRQ_BIT;
    }
}

uint32_t n1g_dev_i2s_read(n1g_state_t *s, uint32_t offset, uint32_t size) {
    (void)size;
    switch (offset & ~3u) {
    case 0x00:
        return s->i2s.config;
    case 0x08:
        return s->i2s.clk;
    case 0x0c: {
        uint32_t free = n1g_dev_i2s_tx_free(s);
        if (free > 0x3fu) {
            free = 0x3fu;
        }
        /* RXFull[29:24] stays 0 (no modeled capture source). */
        return (s->i2s.fifo_cfg & 0xffu) | (free << 16);
    }
    case 0x40: /* IISFIFO_WR reads back as empty */
    case 0x80: /* IISFIFO_RD: no modeled capture data */
        return 0;
    default:
        return 0;
    }
}

void n1g_dev_i2s_push_tx(n1g_state_t *s, uint32_t size) {
    uint32_t slots = (size == 4u) ? 2u : 1u;
    for (uint32_t i = 0; i < slots; i++) {
        if (s->i2s.tx_fill < N1G_I2S_TX_DEPTH) {
            s->i2s.tx_fill++;
            s->i2s.tx_halfwords++;
        }
    }
    i2s_update_irq(s);
}

void n1g_dev_i2s_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value) {
    switch (offset & ~3u) {
    case 0x00: {
        static uint32_t cfg_logs;
        if (cfg_logs < 16u) {
            cfg_logs++;
            n1g_log(s, "i2s config write value=0x%08x tx_fill=%u", value, s->i2s.tx_fill);
        }
        if (value & IIS_RESET) {
            s->i2s.tx_fill = 0;
            s->i2s.drain_acc = 0;
            s->i2s.tx_halfwords = 0;
            s->i2s.tx_drained_halfwords = 0;
        }
        s->i2s.config = value & ~IIS_RESET;
        i2s_update_irq(s);
        break;
    }
    case 0x08:
        s->i2s.clk = value;
        break;
    case 0x0c:
        if (value & IIS_FIFO_TXCLR) {
            s->i2s.tx_fill = 0;
            s->i2s.drain_acc = 0;
        }
        s->i2s.fifo_cfg = value & ~(IIS_FIFO_TXCLR | IIS_FIFO_RXCLR) & 0xffu;
        i2s_update_irq(s);
        break;
    case 0x40:
        n1g_dev_i2s_push_tx(s, size);
        break;
    default:
        break;
    }
}

void n1g_dev_i2s_tick(n1g_state_t *s) {
    if ((s->i2s.config & IIS_TXFIFOEN) == 0 || s->i2s.tx_fill == 0) {
        return;
    }
    s->i2s.drain_acc += s->opts.rtc_usec_per_tick * N1G_I2S_HALFWORDS_PER_SEC;
    while (s->i2s.drain_acc >= 1000000u && s->i2s.tx_fill > 0) {
        s->i2s.drain_acc -= 1000000u;
        s->i2s.tx_fill--;
        s->i2s.tx_drained_halfwords++;
    }
    if (s->i2s.tx_fill == 0) {
        s->i2s.drain_acc = 0;
    }
    i2s_update_irq(s);
}
