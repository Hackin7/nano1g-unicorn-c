#include "nano1g/devices.h"

#include "nano1g/ram.h"
#include "nano1g/trace.h"

#define DMA_CH_BASE 0x1000u
#define DMA_CH_STRIDE 0x20u
#define DMA_CH_COUNT 4u
#define DMA_CMD_START 0x01000000u
#define DMA_STATUS_READY 0x04000000u
#define LCD2_DMA_END (N1G_LCD2_BASE + 0x200u)

uint32_t n1g_dev_dma_read(n1g_state_t *s, uint32_t offset, uint32_t size) {
    (void)size;
    if (offset >= DMA_CH_BASE && offset < DMA_CH_BASE + DMA_CH_COUNT * DMA_CH_STRIDE &&
        (offset % DMA_CH_STRIDE) == 0x04u) {
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
        s->dma.regs[offset / 4u] = value;
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
            dma_try_lcd_transfer(s, (offset - DMA_CH_BASE) / DMA_CH_STRIDE);
        }
    }
}

void n1g_dev_dma_tick(n1g_state_t *s) {
    for (uint32_t channel = 0; channel < DMA_CH_COUNT; channel++) {
        dma_try_lcd_transfer(s, channel);
    }
}
