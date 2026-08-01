#include "nano1g/devices.h"

#include "nano1g/map.h"
#include "nano1g/trace.h"

#include <limits.h>
#include <string.h>

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

uint32_t n1g_dev_i2s_tx_free(const n1g_state_t *s) {
    return N1G_I2S_TX_DEPTH - s->i2s.tx_fill;
}

static uint32_t sample_rate(const n1g_state_t *s) {
    return s->i2c.wm8975_sample_rate != 0u ? s->i2c.wm8975_sample_rate : 44100u;
}

static int16_t codec_output_sample(const n1g_state_t *s, int16_t sample, uint32_t channel) {
    if (!s->i2c.wm8975_output_enabled) {
        return 0;
    }
    int64_t scaled = (int64_t)sample * s->i2c.wm8975_gain_q15[channel & 1u];
    scaled >>= 15u;
    if (scaled > 32767) {
        return 32767;
    }
    if (scaled < -32768) {
        return -32768;
    }
    return (int16_t)scaled;
}

static void clear_tx_fifo(n1g_state_t *s) {
    s->i2s.tx_fill = 0;
    s->i2s.tx_head = 0;
    s->i2s.tx_tail = 0;
    s->i2s.drain_acc = 0;
}

static void clear_pcm_ring(n1g_state_t *s) {
    memset(s->i2s.pcm_ring, 0, sizeof(s->i2s.pcm_ring));
    s->i2s.pcm_produced_halfwords = 0;
    s->i2s.pcm_nonzero_halfwords = 0;
    s->i2s.pcm_silenced_halfwords = 0;
    s->i2s.underruns = 0;
    s->i2s.host_dropped_halfwords = 0;
    s->i2s.pcm_peak = 0;
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

void n1g_dev_i2s_push_tx(n1g_state_t *s, uint32_t size, uint32_t value) {
    uint32_t slots = (size == 4u) ? 2u : 1u;
    for (uint32_t i = 0; i < slots; i++) {
        if (s->i2s.tx_fill < N1G_I2S_TX_DEPTH) {
            int16_t sample = (int16_t)((value >> (i * 16u)) & 0xffffu);
            s->i2s.tx_fifo[s->i2s.tx_tail] = sample;
            s->i2s.tx_tail = (s->i2s.tx_tail + 1u) % N1G_I2S_TX_DEPTH;
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
            clear_tx_fifo(s);
            clear_pcm_ring(s);
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
            clear_tx_fifo(s);
        }
        s->i2s.fifo_cfg = value & ~(IIS_FIFO_TXCLR | IIS_FIFO_RXCLR) & 0xffu;
        i2s_update_irq(s);
        break;
    case 0x40:
        n1g_dev_i2s_push_tx(s, size, value);
        break;
    default:
        break;
    }
}

void n1g_dev_i2s_tick(n1g_state_t *s) {
    if ((s->i2s.config & IIS_TXFIFOEN) == 0) {
        return;
    }
    s->i2s.drain_acc += (uint64_t)s->opts.rtc_usec_per_tick * sample_rate(s) * 2u;
    while (s->i2s.drain_acc >= 1000000u && s->i2s.tx_fill > 0) {
        s->i2s.drain_acc -= 1000000u;
        int16_t input = s->i2s.tx_fifo[s->i2s.tx_head];
        uint32_t channel = (uint32_t)(s->i2s.tx_drained_halfwords & 1u);
        int16_t output = codec_output_sample(s, input, channel);
        s->i2s.tx_head = (s->i2s.tx_head + 1u) % N1G_I2S_TX_DEPTH;
        s->i2s.tx_fill--;
        s->i2s.tx_drained_halfwords++;
        uint64_t produced = s->i2s.pcm_produced_halfwords;
        s->i2s.pcm_ring[produced % N1G_AUDIO_RING_HALFWORDS] = output;
        s->i2s.pcm_produced_halfwords = produced + 1u;
        if (output != 0) {
            uint32_t magnitude = output == INT16_MIN ? 32768u :
                                 (uint32_t)(output < 0 ? -output : output);
            s->i2s.pcm_nonzero_halfwords++;
            if (magnitude > s->i2s.pcm_peak) {
                s->i2s.pcm_peak = magnitude;
            }
        } else if (input != 0) {
            s->i2s.pcm_silenced_halfwords++;
        }
    }
    if (s->i2s.tx_fill == 0) {
        if (s->i2s.drain_acc >= 1000000u) {
            s->i2s.underruns++;
        }
        s->i2s.drain_acc = 0;
    }
    i2s_update_irq(s);
}
