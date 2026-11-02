#include "nano1g/devices.h"
#include "nano1g/map.h"
#include "nano1g/ram.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define DMA_CH_BASE 0x1000u
#define DMA_CH_STRIDE 0x20u
#define DMA_CMD_WAIT_REQ (1u << 24)
#define DMA_CMD_SINGLE (1u << 26)
#define DMA_CMD_RAM_TO_PER (1u << 27)
#define DMA_CMD_INTR (1u << 30)
#define DMA_CMD_START (1u << 31)
#define DMA_STATUS_PRESENT (1u << 26)
#define DMA_STATUS_INTR (1u << 30)
#define DMA_STATUS_BUSY (1u << 31)
#define DMA_IRQ_BIT (1u << 26)
#define DMA_REQ_IIS (2u << 16)

#define GUEST_MANAGER 0x10705ba0u
#define GUEST_ACTIVE_HEAD 0x1070be1cu

#define LCD_CNTL_RAM_ADDR_SET 0x21u
#define LCD_CNTL_HORIZ_RAM_ADDR_POS 0x44u
#define LCD_CNTL_VERT_RAM_ADDR_POS 0x45u

static int expect_true(bool cond, const char *message) {
    if (!cond) {
        fprintf(stderr, "%s\n", message);
        return 1;
    }
    return 0;
}

static void lcd_command_data(n1g_state_t *s, uint16_t command, uint16_t data) {
    n1g_dev_lcd2_write(s, 0x0cu, 4, 0x80000000u | command);
    n1g_dev_lcd2_write(s, 0x0cu, 4, 0x81000000u | (data >> 8u));
    n1g_dev_lcd2_write(s, 0x0cu, 4, 0x81000000u | (data & 0xffu));
}

static void reset_devices(n1g_state_t *s, n1g_profile_t profile) {
    n1g_ram_t ram = s->ram;
    memset(s, 0, sizeof(*s));
    s->ram = ram;
    s->opts.profile = profile;
}

static int check_register_model(n1g_state_t *s) {
    const uint32_t src = N1G_SDRAM_BASE + 0x2000u;
    const uint32_t canary = 0xa5a5a5a5u;
    for (uint32_t i = 0; i < 64u; i++) {
        (void)n1g_ram_write(s, src + i * 4u, 4, 0x12340000u + i);
    }
    (void)n1g_ram_write(s, GUEST_MANAGER, 4, canary);
    (void)n1g_ram_write(s, GUEST_ACTIVE_HEAD, 4, canary);

    n1g_dev_dma_write(s, DMA_CH_BASE + 0x10u, 4, src);
    n1g_dev_dma_write(s, DMA_CH_BASE + 0x18u, 4, N1G_LCD2_BASE + 0x100u);
    n1g_dev_dma_write(s, DMA_CH_BASE, 4, DMA_CMD_WAIT_REQ | (16u - 4u));
    n1g_dev_dma_tick(s);
    int failed = expect_true(s->counters.lcd_words == 0u,
                             "LCD DMA ran before the block request") ||
                 expect_true(s->dma.ch[0].active, "flow-controlled DMA did not go busy when armed") ||
                 expect_true((n1g_dev_dma_read(s, DMA_CH_BASE + 0x04u, 4) & DMA_STATUS_BUSY) != 0u,
                             "armed DMA status did not report busy");

    n1g_dev_lcd2_write(s, 0x24u, 4, 0xc0010000u | (16u - 1u));
    n1g_dev_lcd2_write(s, 0x20u, 4, 0x35000080u);
    failed = failed || expect_true(s->counters.lcd_words == 0u,
                                   "LCD DMA completed inside the LCD register write");
    n1g_dev_dma_tick(s);

    uint32_t command = n1g_dev_dma_read(s, DMA_CH_BASE, 4);
    uint32_t manager = 0;
    uint32_t active_head = 0;
    (void)n1g_ram_read(s, GUEST_MANAGER, 4, &manager);
    (void)n1g_ram_read(s, GUEST_ACTIVE_HEAD, 4, &active_head);
    failed = failed ||
             expect_true((command & DMA_CMD_WAIT_REQ) != 0u,
                         "WAIT_REQ was cleared out of the command register") ||
             expect_true(s->counters.lcd_words == 4u, "LCD DMA transferred the wrong word count") ||
             expect_true(s->lcd2.block_pixels == 8u, "LCD DMA counted the wrong number of block pixels") ||
             expect_true(s->lcd2.block_pixels_remaining == 0u, "LCD2 block did not complete") ||
             expect_true(!s->dma.ch[0].active, "DMA channel stayed busy after the buffer completed") ||
             expect_true(s->dma.lcd_transfers[0] == 1u, "LCD DMA transfer count was incorrect") ||
             expect_true(s->dma.lcd_geometry_accepts == 1u && s->dma.lcd_geometry_mismatches == 0u,
                         "matching LCD DMA geometry was not accepted") ||
             expect_true(s->dma.lcd_descriptor_pixels == 8u && s->dma.lcd_block_pixels == 8u,
                         "LCD DMA geometry totals diverged") ||
             expect_true((s->intc.cpu_status & DMA_IRQ_BIT) != 0u,
                         "DMA completion did not assert IRQ 26") ||
             expect_true(manager == canary && active_head == canary,
                         "DMA engine wrote guest DMA-manager memory");

    uint32_t status = n1g_dev_dma_read(s, DMA_CH_BASE + 0x04u, 4);
    failed = failed ||
             expect_true((status & DMA_STATUS_INTR) != 0u, "completion interrupt was not latched") ||
             expect_true((status & DMA_STATUS_PRESENT) != 0u, "channel-present status bit was clear") ||
             expect_true((status & DMA_STATUS_BUSY) == 0u, "completed channel still reported busy") ||
             expect_true((status & 0xffffu) == (16u - 4u), "completed status size was not reloaded from CMD") ||
             expect_true((s->intc.cpu_status & DMA_IRQ_BIT) == 0u,
                         "reading STATUS did not acknowledge IRQ 26");

    uint64_t words_after = s->counters.lcd_words;
    n1g_dev_lcd2_write(s, 0x24u, 4, 0xc0010000u | (16u - 1u));
    n1g_dev_lcd2_write(s, 0x20u, 4, 0x35000080u);
    n1g_dev_dma_tick(s);
    failed = failed ||
             expect_true(s->counters.lcd_words == words_after,
                         "a completed descriptor was replayed") ||
             expect_true(s->dma.lcd_transfers[0] == 1u, "a replay was counted as another transfer");

    /* The no-replay block is still open. Close it, then arm against a
     * two-pixel request so only one beat is pulled. */
    n1g_dev_lcd2_write(s, 0x24u, 4, 0u);
    n1g_dev_lcd2_write(s, 0x20u, 4, 0x35000080u);
    n1g_dev_dma_write(s, DMA_CH_BASE, 4, DMA_CMD_WAIT_REQ | (16u - 4u));
    n1g_dev_lcd2_write(s, 0x24u, 4, 3u);
    n1g_dev_lcd2_write(s, 0x20u, 4, 0x35000080u);
    n1g_dev_dma_tick(s);
    failed = failed ||
             expect_true(s->counters.lcd_words == words_after + 1u,
                         "a shorter LCD request did not consume one DMA word") ||
             expect_true(s->dma.ch[0].active, "DMA went idle while bytes remained") ||
             expect_true((s->intc.cpu_status & DMA_IRQ_BIT) == 0u,
                         "a partial LCD beat raised the completion IRQ") ||
             expect_true(s->dma.lcd_geometry_mismatches == 1u,
                         "a mismatched LCD request was not recorded once");
    n1g_dev_dma_tick(s);
    failed = failed || expect_true(s->dma.lcd_geometry_mismatches == 1u,
                                   "a polled geometry mismatch was counted again");

    n1g_dev_dma_write(s, DMA_CH_BASE, 4, 0u);
    failed = failed || expect_true(!s->dma.ch[0].active, "clearing CMD left the channel busy");

    n1g_dev_dma_write(s, DMA_CH_BASE + 0x10u, 4, src);
    n1g_dev_dma_write(s, DMA_CH_BASE + 0x18u, 4, N1G_LCD2_BASE + 0x100u);
    n1g_dev_dma_write(s, DMA_CH_BASE, 4, DMA_CMD_WAIT_REQ | (16u - 4u));
    n1g_dev_lcd2_write(s, 0x24u, 4, 16u - 1u);
    n1g_dev_lcd2_write(s, 0x20u, 4, 0x35000080u);
    n1g_dev_dma_tick(s);
    failed = failed ||
             expect_true(s->counters.lcd_words == words_after + 1u + 4u,
                         "re-armed LCD DMA did not finish the matching block") ||
             expect_true(s->dma.lcd_transfers[0] == 2u, "re-armed LCD DMA was not counted") ||
             expect_true(s->dma.lcd_geometry_accepts == 2u, "re-armed LCD DMA was not accepted");

    uint32_t audio_cmd = DMA_CMD_START | DMA_CMD_INTR | DMA_REQ_IIS | DMA_CMD_WAIT_REQ |
                         DMA_CMD_SINGLE | DMA_CMD_RAM_TO_PER | (256u - 4u);
    n1g_dev_dma_write(s, DMA_CH_BASE + 0x10u, 4, src);
    n1g_dev_dma_write(s, DMA_CH_BASE + 0x18u, 4, N1G_I2S_BASE + 0x40u);
    n1g_dev_dma_write(s, DMA_CH_BASE, 4, audio_cmd);
    uint32_t audio_status = n1g_dev_dma_read(s, DMA_CH_BASE + 0x04u, 4);
    failed = failed ||
             expect_true(s->dma.ch[0].starts == 1u, "I2S DMA did not start") ||
             expect_true(s->dma.ch[0].bytes_pushed == 64u, "I2S DMA did not stop at the FIFO") ||
             expect_true((audio_status & DMA_STATUS_BUSY) != 0u, "I2S DMA was not busy") ||
             expect_true((audio_status & 0xffffu) == (192u - 4u),
                         "I2S DMA remaining size did not use the CMD encoding") ||
             expect_true((s->intc.cpu_status & DMA_IRQ_BIT) == 0u,
                         "reading in-progress STATUS raised a stale completion IRQ");

    uint32_t stopped = audio_cmd & ~(DMA_CMD_START | DMA_CMD_INTR);
    n1g_dev_dma_write(s, DMA_CH_BASE, 4, stopped);
    uint32_t stopped_status = n1g_dev_dma_read(s, DMA_CH_BASE + 0x04u, 4);
    failed = failed ||
             expect_true(!s->dma.ch[0].active, "clearing START did not stop the channel") ||
             expect_true((n1g_dev_dma_read(s, DMA_CH_BASE, 4) & DMA_CMD_WAIT_REQ) != 0u,
                         "stopping cleared WAIT_REQ") ||
             expect_true((stopped_status & (DMA_STATUS_BUSY | DMA_STATUS_INTR)) == 0u,
                         "stopped channel still reported busy or an interrupt");

    n1g_dev_dma_write(s, DMA_CH_BASE + DMA_CH_STRIDE + 0x10u, 4, src);
    n1g_dev_dma_write(s, DMA_CH_BASE + DMA_CH_STRIDE + 0x18u, 4, N1G_I2S_BASE + 0x40u);
    n1g_dev_dma_write(s, DMA_CH_BASE + DMA_CH_STRIDE, 4,
                      DMA_CMD_START | DMA_CMD_WAIT_REQ | DMA_CMD_SINGLE | (16u - 4u));
    failed = failed ||
             expect_true((s->intc.cpu_status & DMA_IRQ_BIT) == 0u,
                         "a START transfer without CMD.INTR raised IRQ 26");
    return failed;
}

int main(void) {
    n1g_state_t s;
    memset(&s, 0, sizeof(s));

    n1g_dev_dma_secondary_write(&s, 0x00u, 4, 0x80000000u);
    n1g_dev_dma_secondary_write(&s, DMA_CH_BASE, 4, 0x81234567u);
    n1g_dev_dma_write(&s, 0x00u, 4, 0x00000001u);
    int failed =
        expect_true(n1g_dev_dma_secondary_read(&s, 0x00u, 4) == 0x80000000u,
                    "secondary DMA master register did not latch") ||
        expect_true(n1g_dev_dma_secondary_read(&s, DMA_CH_BASE, 4) == 0x81234567u,
                    "secondary DMA channel command did not latch") ||
        expect_true((n1g_dev_dma_secondary_read(&s, DMA_CH_BASE + 0x04u, 4) &
                     DMA_STATUS_PRESENT) != 0u,
                    "secondary DMA channel 0 did not report present") ||
        expect_true((n1g_dev_dma_secondary_read(&s, DMA_CH_BASE + DMA_CH_STRIDE + 0x04u, 4) &
                     DMA_STATUS_PRESENT) != 0u,
                    "secondary DMA channel 1 did not report present") ||
        expect_true(n1g_dev_dma_read(&s, 0x00u, 4) == 0x00000001u,
                    "primary and secondary DMA master registers aliased");

    if (!n1g_ram_init(&s)) {
        fprintf(stderr, "failed to allocate RAM\n");
        return 1;
    }

    reset_devices(&s, N1G_PROFILE_ROCKBOX);
    failed = failed || check_register_model(&s);
    reset_devices(&s, N1G_PROFILE_APPLE);
    failed = failed || check_register_model(&s);

    memset(&s.lcd2, 0, sizeof(s.lcd2));
    lcd_command_data(&s, LCD_CNTL_HORIZ_RAM_ADDR_POS, 0xaf00u);
    lcd_command_data(&s, LCD_CNTL_VERT_RAM_ADDR_POS, 0x8700u);
    lcd_command_data(&s, LCD_CNTL_RAM_ADDR_SET, 0x0000u);
    n1g_dev_lcd2_write(&s, 0x24u, 4, 0x0001baffu);
    n1g_dev_lcd2_write(&s, 0x20u, 4, 0x35000080u);

    const uint32_t full_gram_words = N1G_LCD_W * N1G_LCD_GRAM_H / 2u;
    for (uint32_t i = 0; i < full_gram_words; i++) {
        uint32_t word = 0x33333333u;
        if (i == 0u) {
            word = 0x22221111u;
        }
        if (i == full_gram_words - 1u) {
            word = 0x44443333u;
        }
        n1g_dev_lcd2_write(&s, 0x100u, 4, word);
    }
    n1g_dev_lcd2_write(&s, 0x100u, 4, 0x66665555u);

    failed = failed ||
             expect_true(s.lcd2.window_y1 == N1G_LCD_GRAM_H - 1u,
                         "LCD window was clamped to the visible panel") ||
             expect_true(s.lcd2.block_pixels_remaining == 0u, "full-GRAM transfer did not complete") ||
             expect_true(s.lcd2.pixels[0] == 0x1111u && s.lcd2.pixels[1] == 0x2222u,
                         "hidden rows wrapped over the top of the visible panel") ||
             expect_true(s.lcd2.pixels[N1G_LCD_W * N1G_LCD_H] == 0x3333u,
                         "first hidden GRAM row was not retained") ||
             expect_true(s.lcd2.pixels[N1G_LCD_W * N1G_LCD_GRAM_H - 1u] == 0x4444u,
                         "final hidden GRAM pixel was not retained") ||
             expect_true(s.lcd2.block_overrun_words == 1u, "LCD block overrun was not counted");

    n1g_ram_destroy(&s);
    return failed ? 1 : 0;
}
