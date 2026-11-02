#include "nano1g/devices.h"

#include "nano1g/ram.h"
#include "nano1g/trace.h"

/*
 * PP502x DMA, one register model for every guest.
 *
 * The APB engine at 0x6000a000 (four channels, stride 0x20, IRQ 26) and the
 * AHB engine at 0x60008000 (two channels) follow the command/status layout
 * Rockbox programs in pcm-pp.c and that Clicky documents from the Tegra APB
 * DMA: CMD +0x00, STATUS +0x04, RAM +0x10, FLAGS +0x14, peripheral +0x18,
 * INCR +0x1c. Master control, status, and request-enable sit at +0x00/+0x04/+0x08.
 *
 * A channel becomes busy when CMD.START is written, or when CMD.WAIT_REQ is
 * written onto a channel that was not already started. Clearing START stops
 * the channel and does not arm WAIT_REQ in that same write (Rockbox's
 * dma_tx_stop keeps WAIT_REQ set). WAIT_REQ paces beats on a peripheral
 * request: I2S TX FIFO space (request 2) or an open LCD2 block (request 11).
 * The peripheral port is the programmed address, so a command that names the
 * LCD2 or I2S FIFO still runs when the request-id field is left at 0.
 *
 * Completion reloads the status size field from CMD. CMD.SINGLE clears START.
 * End-of-count sets STATUS.INTR and IRQ 26 when CMD.INTR is set. A flow
 * transfer armed with WAIT_REQ and without START also completes with that
 * interrupt: that is the LCD programming shape, and the interrupt is what
 * lets the guest retire its own transfer record. Reading APB STATUS returns
 * the pending interrupt and then clears it. AHB STATUS keeps the flag until
 * software writes 1 to the interrupt bit. Bit 26 of STATUS is the
 * channel-present bit Apple's controller probe reads back on an idle channel.
 *
 * Nothing here reads or writes guest DMA-manager lists. Those lists are
 * firmware. The old host-side linker was papering over a missing busy flag
 * and a missing completion interrupt.
 */

#define DMA_CH_BASE 0x1000u
#define DMA_CH_STRIDE 0x20u
#define DMA_APB_CH_COUNT 4u
#define DMA_AHB_CH_COUNT 2u

#define DMA_CMD_SIZE_MASK 0xffffu
#define DMA_CMD_REQ_SHIFT 16u
#define DMA_CMD_WAIT_REQ (1u << 24)
#define DMA_CMD_SINGLE (1u << 26)
#define DMA_CMD_INTR (1u << 30)
#define DMA_CMD_START (1u << 31)

#define DMA_STATUS_PRESENT (1u << 26)
#define DMA_STATUS_INTR (1u << 30)
#define DMA_STATUS_BUSY (1u << 31)

#define DMA_REQ_IIS 2u
#define DMA_REQ_LCD2 11u
#define DMA_IRQ_BIT (1u << 26)

#define LCD2_DMA_END (N1G_LCD2_BASE + 0x200u)

typedef enum dma_sink {
    DMA_SINK_NONE = 0,
    DMA_SINK_LCD,
    DMA_SINK_I2S,
    DMA_SINK_RAM
} dma_sink_t;

static uint32_t sized_value(uint32_t value, uint32_t offset, uint32_t size) {
    if (size >= 4u) {
        return value;
    }
    uint32_t shift = (offset & 3u) * 8u;
    uint32_t mask = size == 2u ? 0xffffu : 0xffu;
    return (value >> shift) & mask;
}

static uint32_t ch_cmd(const uint32_t *regs, uint32_t channel) {
    return regs[(DMA_CH_BASE + channel * DMA_CH_STRIDE) / 4u];
}

static uint32_t ch_field(const uint32_t *regs, uint32_t channel, uint32_t reg) {
    return regs[(DMA_CH_BASE + channel * DMA_CH_STRIDE + reg) / 4u];
}

static void dma_irq_sync(n1g_state_t *s) {
    bool pending = false;
    for (uint32_t channel = 0; channel < DMA_APB_CH_COUNT; channel++) {
        if (s->dma.ch[channel].eoc) {
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

static dma_sink_t classify_sink(n1g_state_t *s, uint32_t cmd, uint32_t per_addr) {
    uint32_t req = (cmd >> DMA_CMD_REQ_SHIFT) & 0xfu;
    bool lcd_port = per_addr >= N1G_LCD2_BASE && per_addr < LCD2_DMA_END;
    bool i2s_port = per_addr >= N1G_I2S_BASE && per_addr < N1G_I2S_BASE + 0x100u;
    /* The programmed port is the beat target. Request ids 2 and 11 select the
     * same sinks when the port itself was left pointing at that device. */
    if (lcd_port || (req == DMA_REQ_LCD2 && lcd_port)) {
        return DMA_SINK_LCD;
    }
    if (i2s_port || (req == DMA_REQ_IIS && i2s_port)) {
        return DMA_SINK_I2S;
    }
    if (n1g_ram_ptr(s, per_addr, 4u) != NULL) {
        return DMA_SINK_RAM;
    }
    return DMA_SINK_NONE;
}

static bool sink_ready(const n1g_state_t *s, dma_sink_t sink) {
    if (sink == DMA_SINK_LCD) {
        return s->lcd2.block_pixels_remaining > 0u;
    }
    if (sink == DMA_SINK_I2S) {
        return n1g_dev_i2s_tx_free(s) >= 2u;
    }
    return sink == DMA_SINK_RAM;
}

static void note_lcd_mismatch(n1g_state_t *s, n1g_dma_channel_t *ch, uint32_t programmed_bytes) {
    uint32_t block_pixels = s->lcd2.block_pixels_remaining;
    uint32_t desc_pixels = programmed_bytes / 2u;
    if (block_pixels == 0u || block_pixels == desc_pixels) {
        return;
    }
    if (ch->mismatch_block == s->lcd2.block_starts) {
        return;
    }
    ch->mismatch_block = s->lcd2.block_starts;
    s->dma.lcd_geometry_mismatches++;
}

static bool transfer_beat(n1g_state_t *s, n1g_dma_channel_t *ch, dma_sink_t sink,
                          uint32_t per_addr, bool count_audio) {
    uint32_t word = 0;
    if (!n1g_ram_read(s, ch->cur_addr, 4u, &word)) {
        return false;
    }
    if (sink == DMA_SINK_LCD) {
        if (ch->burst_bytes == 0u) {
            ch->lcd_block_pixels = s->lcd2.block_pixels_remaining;
        }
        n1g_dev_lcd2_write(s, per_addr - N1G_LCD2_BASE, 4u, word);
        ch->burst_bytes += 4u;
    } else if (sink == DMA_SINK_I2S) {
        n1g_dev_i2s_push_tx(s, 4u, word);
        if (count_audio) {
            ch->bytes_pushed += 4u;
        }
    } else if (sink == DMA_SINK_RAM) {
        if (!n1g_ram_write(s, per_addr + (ch->programmed_bytes - ch->bytes_left), 4u, word)) {
            return false;
        }
    } else {
        return false;
    }
    ch->cur_addr += 4u;
    ch->bytes_left = ch->bytes_left >= 4u ? ch->bytes_left - 4u : 0u;
    return true;
}

static void complete_buffer(n1g_state_t *s, uint32_t *regs, n1g_dma_channel_t *ch,
                            uint32_t channel, bool raise_irq, bool count_audio,
                            dma_sink_t sink) {
    uint32_t cmd_index = (DMA_CH_BASE + channel * DMA_CH_STRIDE) / 4u;
    uint32_t cmd = regs[cmd_index];
    bool flow_arm = (cmd & DMA_CMD_START) == 0u && (cmd & DMA_CMD_WAIT_REQ) != 0u;
    bool want_eoc = (cmd & DMA_CMD_INTR) != 0u || flow_arm;
    bool reload = (cmd & DMA_CMD_START) != 0u && (cmd & DMA_CMD_SINGLE) == 0u;

    if (sink == DMA_SINK_LCD && channel < DMA_APB_CH_COUNT && raise_irq) {
        uint32_t desc_pixels = ch->programmed_bytes / 2u;
        s->dma.lcd_transfers[channel]++;
        if (desc_pixels != 0u && ch->lcd_block_pixels == desc_pixels &&
            ch->burst_bytes == ch->programmed_bytes) {
            s->dma.lcd_geometry_accepts++;
            s->dma.lcd_descriptor_pixels += desc_pixels;
            s->dma.lcd_block_pixels += ch->lcd_block_pixels;
        }
        static uint32_t lcd_logs;
        if (lcd_logs < 16u) {
            lcd_logs++;
            n1g_log(s,
                    "dma lcd done ch=%u bytes=%u block_pixels=%u eoc=%u cmd=0x%08x",
                    channel,
                    ch->programmed_bytes,
                    ch->lcd_block_pixels,
                    want_eoc ? 1u : 0u,
                    cmd);
        }
    }

    if ((cmd & DMA_CMD_SINGLE) != 0u) {
        regs[cmd_index] = cmd & ~DMA_CMD_START;
    }
    if (reload) {
        ch->cur_addr = ch_field(regs, channel, 0x10u);
        ch->bytes_left = ch->programmed_bytes;
        ch->burst_bytes = 0u;
        ch->active = true;
    } else {
        ch->active = false;
        ch->bytes_left = 0u;
    }
    if (want_eoc) {
        ch->eoc = true;
    }
    if (count_audio && sink == DMA_SINK_I2S) {
        ch->completions++;
    }
    if (raise_irq) {
        dma_irq_sync(s);
    }
}

static void service_channel(n1g_state_t *s, uint32_t *regs, n1g_dma_channel_t *ch,
                            uint32_t channel, bool raise_irq, bool count_audio) {
    if (!ch->active || ch->bytes_left == 0u) {
        return;
    }
    uint32_t cmd = ch_cmd(regs, channel);
    uint32_t per_addr = ch_field(regs, channel, 0x18u);
    dma_sink_t sink = classify_sink(s, cmd, per_addr);
    if (sink == DMA_SINK_LCD) {
        note_lcd_mismatch(s, ch, ch->programmed_bytes);
    }
    bool paced = (cmd & DMA_CMD_WAIT_REQ) != 0u;
    while (ch->active && ch->bytes_left > 0u) {
        if (paced && !sink_ready(s, sink)) {
            break;
        }
        if (!transfer_beat(s, ch, sink, per_addr, count_audio)) {
            ch->active = false;
            break;
        }
    }
    if (ch->active && ch->bytes_left == 0u) {
        complete_buffer(s, regs, ch, channel, raise_irq, count_audio, sink);
    }
}

static void arm_channel(n1g_state_t *s, uint32_t *regs, n1g_dma_channel_t *ch,
                        uint32_t channel, bool raise_irq, bool count_audio) {
    uint32_t cmd = ch_cmd(regs, channel);
    uint32_t bytes = (cmd & 0xfffcu) + 4u;
    ch->cur_addr = ch_field(regs, channel, 0x10u);
    ch->bytes_left = bytes;
    ch->programmed_bytes = bytes;
    ch->burst_bytes = 0u;
    ch->lcd_block_pixels = 0u;
    ch->active = true;
    ch->eoc = false;
    if (count_audio) {
        uint32_t per_addr = ch_field(regs, channel, 0x18u);
        dma_sink_t sink = classify_sink(s, cmd, per_addr);
        if (sink == DMA_SINK_I2S) {
            ch->starts++;
        } else if (sink == DMA_SINK_LCD) {
            static uint32_t arm_logs;
            if (arm_logs < 16u) {
                arm_logs++;
                n1g_log(s, "dma lcd arm ch=%u ram=0x%08x per=0x%08x bytes=%u cmd=0x%08x",
                        channel, ch->cur_addr, per_addr, bytes, cmd);
            }
        }
    }
    if (raise_irq) {
        dma_irq_sync(s);
    }
    service_channel(s, regs, ch, channel, raise_irq, count_audio);
}

static void on_command(n1g_state_t *s, uint32_t *regs, n1g_dma_channel_t *ch,
                       uint32_t channel, uint32_t old_cmd, uint32_t new_cmd,
                       bool raise_irq, bool count_audio) {
    bool old_start = (old_cmd & DMA_CMD_START) != 0u;
    bool new_start = (new_cmd & DMA_CMD_START) != 0u;
    if (new_start) {
        arm_channel(s, regs, ch, channel, raise_irq, count_audio);
        return;
    }
    if (old_start) {
        ch->active = false;
        if ((new_cmd & DMA_CMD_INTR) == 0u) {
            ch->eoc = false;
            if (raise_irq) {
                dma_irq_sync(s);
            }
        }
        return;
    }
    if ((new_cmd & DMA_CMD_WAIT_REQ) != 0u) {
        arm_channel(s, regs, ch, channel, raise_irq, count_audio);
        return;
    }
    ch->active = false;
}

static uint32_t compose_status(const uint32_t *regs, const n1g_dma_channel_t *ch,
                               uint32_t channel) {
    uint32_t cmd = ch_cmd(regs, channel);
    uint32_t status = DMA_STATUS_PRESENT;
    if (ch->eoc) {
        status |= DMA_STATUS_INTR;
    }
    if (ch->active) {
        status |= DMA_STATUS_BUSY;
        if (ch->bytes_left >= 4u) {
            status |= (ch->bytes_left - 4u) & DMA_CMD_SIZE_MASK;
        }
    } else {
        status |= cmd & DMA_CMD_SIZE_MASK;
    }
    return status;
}

static bool decode_channel(uint32_t offset, uint32_t count, uint32_t *channel, uint32_t *reg) {
    uint32_t aligned = offset & ~3u;
    if (aligned < DMA_CH_BASE) {
        return false;
    }
    uint32_t rel = aligned - DMA_CH_BASE;
    uint32_t index = rel / DMA_CH_STRIDE;
    if (index >= count) {
        return false;
    }
    *channel = index;
    *reg = rel % DMA_CH_STRIDE;
    return true;
}

static uint32_t engine_read(n1g_state_t *s, uint32_t *regs, n1g_dma_channel_t *channels,
                            uint32_t count, bool apb_irq, uint32_t offset) {
    uint32_t aligned = offset & ~3u;
    uint32_t channel = 0;
    uint32_t reg = 0;
    if (decode_channel(aligned, count, &channel, &reg) && reg == 0x04u) {
        uint32_t status = compose_status(regs, &channels[channel], channel);
        if (apb_irq && channels[channel].eoc) {
            channels[channel].eoc = false;
            dma_irq_sync(s);
        }
        return status;
    }
    if (aligned == 0x04u) {
        uint32_t status = 0;
        for (uint32_t index = 0; index < count; index++) {
            if (channels[index].active) {
                status |= 1u << (24u + index);
            }
        }
        return status;
    }
    if (aligned < 0x2000u) {
        return regs[aligned / 4u];
    }
    return 0;
}

static void engine_write(n1g_state_t *s, uint32_t *regs, n1g_dma_channel_t *channels,
                         uint32_t count, bool apb_irq, bool count_audio,
                         uint32_t offset, uint32_t value) {
    uint32_t aligned = offset & ~3u;
    if (aligned >= 0x2000u) {
        return;
    }
    uint32_t channel = 0;
    uint32_t reg = 0;
    bool is_channel = decode_channel(aligned, count, &channel, &reg);
    if (is_channel && reg == 0x04u) {
        if ((value & DMA_STATUS_INTR) != 0u) {
            channels[channel].eoc = false;
            if (apb_irq) {
                dma_irq_sync(s);
            }
        }
        return;
    }
    if (aligned == 0x04u) {
        return;
    }

    uint32_t old_cmd = 0;
    if (is_channel && reg == 0x00u) {
        old_cmd = regs[aligned / 4u];
    }
    regs[aligned / 4u] = value;

    if (s->opts.verbose && (aligned < 0x10u || aligned >= DMA_CH_BASE)) {
        static uint32_t dma_logs;
        if (dma_logs < 64u) {
            dma_logs++;
            n1g_log(s, "dma reg write bank=%s offset=0x%04x value=0x%08x",
                    apb_irq ? "apb" : "ahb", aligned, value);
        }
    }

    if (is_channel && reg == 0x00u) {
        on_command(s, regs, &channels[channel], channel, old_cmd, value, apb_irq, count_audio);
    }
}

static void engine_tick(n1g_state_t *s, uint32_t *regs, n1g_dma_channel_t *channels,
                        uint32_t count, bool apb_irq, bool count_audio) {
    for (uint32_t channel = 0; channel < count; channel++) {
        service_channel(s, regs, &channels[channel], channel, apb_irq, count_audio);
    }
}

uint32_t n1g_dev_dma_read(n1g_state_t *s, uint32_t offset, uint32_t size) {
    return sized_value(engine_read(s, s->dma.regs, s->dma.ch, DMA_APB_CH_COUNT, true, offset),
                       offset, size);
}

void n1g_dev_dma_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value) {
    (void)size;
    engine_write(s, s->dma.regs, s->dma.ch, DMA_APB_CH_COUNT, true, true, offset, value);
}

uint32_t n1g_dev_dma_secondary_read(n1g_state_t *s, uint32_t offset, uint32_t size) {
    return sized_value(
        engine_read(s, s->dma.secondary_regs, s->dma.secondary_ch, DMA_AHB_CH_COUNT, false, offset),
        offset, size);
}

void n1g_dev_dma_secondary_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value) {
    (void)size;
    engine_write(s, s->dma.secondary_regs, s->dma.secondary_ch, DMA_AHB_CH_COUNT, false, false,
                 offset, value);
}

void n1g_dev_dma_tick(n1g_state_t *s) {
    engine_tick(s, s->dma.regs, s->dma.ch, DMA_APB_CH_COUNT, true, true);
    engine_tick(s, s->dma.secondary_regs, s->dma.secondary_ch, DMA_AHB_CH_COUNT, false, false);
}
