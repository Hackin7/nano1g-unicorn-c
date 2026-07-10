#include "nano1g/devices.h"

#include "nano1g/ram.h"
#include "nano1g/trace.h"

#define DMA_CH_BASE 0x1000u
#define DMA_CH_STRIDE 0x20u
#define DMA_CH_COUNT 4u
#define DMA_CMD_START 0x01000000u
#define DMA_STATUS_READY 0x04000000u
#define LCD2_DMA_END (N1G_LCD2_BASE + 0x200u)

/* Rockbox pp5020.h DMA channel semantics, used by the peripheral-paced
 * audio path (RAM -> IISFIFO_WR). */
#define RB_CMD_SIZE_MASK 0x0000ffffu
#define RB_CMD_INTR (1u << 30)
#define RB_CMD_START (1u << 31)
#define RB_STATUS_INTR (1u << 30)
#define RB_STATUS_BUSY (1u << 31)
#define DMA_IRQ_BIT (1u << 26)

static uint32_t ch_reg(const n1g_state_t *s, uint32_t channel, uint32_t reg) {
    return s->dma.regs[(DMA_CH_BASE + channel * DMA_CH_STRIDE + reg) / 4u];
}

static bool ch_targets_i2s(const n1g_state_t *s, uint32_t channel) {
    uint32_t per_addr = ch_reg(s, channel, 0x18u);
    return per_addr >= N1G_I2S_BASE && per_addr <= N1G_I2S_BASE + 0xffu;
}

static uint32_t audio_ch_status(const n1g_state_t *s, uint32_t channel) {
    uint32_t status = ch_reg(s, channel, 0x04u) & RB_STATUS_INTR;
    if (s->dma.ch[channel].active) {
        status |= RB_STATUS_BUSY;
        if (s->dma.ch[channel].bytes_left >= 4u) {
            status |= (s->dma.ch[channel].bytes_left - 4u) & RB_CMD_SIZE_MASK;
        }
    } else {
        /* Hardware reloads the size field from CMD once a transfer ends. */
        status |= ch_reg(s, channel, 0x00u) & RB_CMD_SIZE_MASK;
    }
    return status;
}

static void dma_irq_sync(n1g_state_t *s) {
    bool pending = false;
    for (uint32_t channel = 0; channel < DMA_CH_COUNT; channel++) {
        if (ch_targets_i2s(s, channel) &&
            (ch_reg(s, channel, 0x04u) & RB_STATUS_INTR) != 0) {
            pending = true;
            break;
        }
    }
    if (pending) {
        s->intc.cpu_status |= DMA_IRQ_BIT;
        s->intc.cop_status |= DMA_IRQ_BIT;
    } else {
        s->intc.cpu_status &= ~DMA_IRQ_BIT;
        s->intc.cop_status &= ~DMA_IRQ_BIT;
    }
}

/* Move words from RAM into the IIS TX FIFO while it has room. The FIFO
 * drain rate in dev_i2s paces this pipeline, standing in for the real
 * DMA_CMD_WAIT_REQ request line. */
static void audio_ch_run(n1g_state_t *s, uint32_t channel) {
    if (!s->dma.ch[channel].active || !ch_targets_i2s(s, channel)) {
        return;
    }
    while (s->dma.ch[channel].bytes_left > 0 && n1g_dev_i2s_tx_free(s) >= 2u) {
        uint32_t word = 0;
        if (!n1g_ram_read(s, s->dma.ch[channel].cur_addr, 4, &word)) {
            s->dma.ch[channel].active = false;
            return;
        }
        n1g_dev_i2s_push_tx(s, 4u);
        s->dma.ch[channel].bytes_pushed += 4u;
        s->dma.ch[channel].cur_addr += 4u;
        s->dma.ch[channel].bytes_left =
            s->dma.ch[channel].bytes_left >= 4u ? s->dma.ch[channel].bytes_left - 4u : 0;
    }
    if (s->dma.ch[channel].bytes_left == 0) {
        uint32_t cmd = ch_reg(s, channel, 0x00u);
        s->dma.ch[channel].active = false;
        s->dma.ch[channel].completions++;
        if (cmd & RB_CMD_INTR) {
            s->dma.regs[(DMA_CH_BASE + channel * DMA_CH_STRIDE + 0x04u) / 4u] |= RB_STATUS_INTR;
            dma_irq_sync(s);
        }
    }
}

static void audio_ch_command(n1g_state_t *s, uint32_t channel, uint32_t cmd) {
    if (!ch_targets_i2s(s, channel)) {
        return;
    }
    if (cmd & RB_CMD_START) {
        s->dma.ch[channel].cur_addr = ch_reg(s, channel, 0x10u);
        s->dma.ch[channel].bytes_left = (cmd & RB_CMD_SIZE_MASK) + 4u;
        s->dma.ch[channel].active = true;
        s->dma.ch[channel].starts++;
        static uint32_t start_logs;
        if (start_logs < 16u) {
            start_logs++;
            n1g_log(s, "dma audio start ch=%u ram=0x%08x bytes=%u cmd=0x%08x",
                    channel, s->dma.ch[channel].cur_addr, s->dma.ch[channel].bytes_left, cmd);
        }
        audio_ch_run(s, channel);
    } else {
        /* Guest cleared START mid-transfer (dma_tx_stop). */
        s->dma.ch[channel].active = false;
        if ((cmd & RB_CMD_INTR) == 0) {
            s->dma.regs[(DMA_CH_BASE + channel * DMA_CH_STRIDE + 0x04u) / 4u] &= ~RB_STATUS_INTR;
            dma_irq_sync(s);
        }
    }
}

uint32_t n1g_dev_dma_read(n1g_state_t *s, uint32_t offset, uint32_t size) {
    (void)size;
    if (offset >= DMA_CH_BASE && offset < DMA_CH_BASE + DMA_CH_COUNT * DMA_CH_STRIDE &&
        (offset % DMA_CH_STRIDE) == 0x04u) {
        uint32_t channel = (offset - DMA_CH_BASE) / DMA_CH_STRIDE;
        if (ch_targets_i2s(s, channel)) {
            uint32_t status = audio_ch_status(s, channel);
            /* Reading STATUS acknowledges the completion interrupt. */
            s->dma.regs[offset / 4u] &= ~RB_STATUS_INTR;
            dma_irq_sync(s);
            return status;
        }
        return s->dma.regs[offset / 4u] | DMA_STATUS_READY;
    }
    if (offset < sizeof(s->dma.regs)) {
        return s->dma.regs[offset / 4u];
    }
    return 0;
}

static void dma_try_lcd_transfer(n1g_state_t *s, uint32_t channel) {
    uint32_t base = DMA_CH_BASE + channel * DMA_CH_STRIDE;
    uint32_t cmd = s->dma.regs[(base + 0x00u) / 4u];
    uint32_t ram_addr = s->dma.regs[(base + 0x10u) / 4u];
    uint32_t per_addr = s->dma.regs[(base + 0x18u) / 4u];
    uint32_t byte_count = (cmd & 0xffffu) + 4u;

    if ((cmd & DMA_CMD_START) == 0 || byte_count == 0 ||
        per_addr < N1G_LCD2_BASE || per_addr >= LCD2_DMA_END) {
        return;
    }

    if (s->opts.profile == N1G_PROFILE_APPLE) {
        static uint32_t transfer_logs;
        if (transfer_logs < 16u) {
            transfer_logs++;
            n1g_log(s, "apple dma lcd ch=%u ram=0x%08x per=0x%08x cmd=0x%08x bytes=%u",
                    channel, ram_addr, per_addr, cmd, byte_count);
        }
    }

    for (uint32_t off = 0; off < byte_count; off += 4u) {
        uint32_t word = 0;
        if (!n1g_ram_read(s, ram_addr + off, 4, &word)) {
            break;
        }
        n1g_dev_lcd2_write(s, per_addr - N1G_LCD2_BASE, 4, word);
    }

    s->dma.regs[(base + 0x00u) / 4u] = cmd & ~DMA_CMD_START;
    s->dma.regs[(base + 0x04u) / 4u] |= DMA_STATUS_READY;
}

void n1g_dev_dma_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value) {
    (void)size;
    if (offset < sizeof(s->dma.regs)) {
        bool audio_status = offset >= DMA_CH_BASE &&
                            offset < DMA_CH_BASE + DMA_CH_COUNT * DMA_CH_STRIDE &&
                            (offset % DMA_CH_STRIDE) == 0x04u &&
                            ch_targets_i2s(s, (offset - DMA_CH_BASE) / DMA_CH_STRIDE);
        if (!audio_status) {
            s->dma.regs[offset / 4u] = value;
        }
        if (s->opts.verbose && (offset < 0x10u || offset >= DMA_CH_BASE)) {
            static uint32_t rb_dma_logs;
            if (rb_dma_logs < 64u) {
                rb_dma_logs++;
                n1g_log(s, "dma reg write offset=0x%04x size=%u value=0x%08x", offset, size, value);
            }
        }
        if (s->opts.profile == N1G_PROFILE_APPLE && offset >= DMA_CH_BASE &&
            offset < DMA_CH_BASE + DMA_CH_COUNT * DMA_CH_STRIDE) {
            static uint32_t dma_logs;
            if (dma_logs < 64u) {
                uint32_t channel = (offset - DMA_CH_BASE) / DMA_CH_STRIDE;
                uint32_t reg = (offset - DMA_CH_BASE) % DMA_CH_STRIDE;
                dma_logs++;
                n1g_log(s, "apple dma write ch=%u reg=0x%02x value=0x%08x cmd=0x%08x ram=0x%08x per=0x%08x",
                        channel,
                        reg,
                        value,
                        s->dma.regs[(DMA_CH_BASE + channel * DMA_CH_STRIDE + 0x00u) / 4u],
                        s->dma.regs[(DMA_CH_BASE + channel * DMA_CH_STRIDE + 0x10u) / 4u],
                        s->dma.regs[(DMA_CH_BASE + channel * DMA_CH_STRIDE + 0x18u) / 4u]);
            }
        }
        if (offset >= DMA_CH_BASE && offset < DMA_CH_BASE + DMA_CH_COUNT * DMA_CH_STRIDE) {
            uint32_t channel = (offset - DMA_CH_BASE) / DMA_CH_STRIDE;
            if ((offset % DMA_CH_STRIDE) == 0x00u) {
                audio_ch_command(s, channel, value);
            }
            dma_try_lcd_transfer(s, channel);
        }
    }
}

void n1g_dev_dma_tick(n1g_state_t *s) {
    for (uint32_t channel = 0; channel < DMA_CH_COUNT; channel++) {
        dma_try_lcd_transfer(s, channel);
        audio_ch_run(s, channel);
    }
}
