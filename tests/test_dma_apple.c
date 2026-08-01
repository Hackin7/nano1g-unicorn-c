#include "nano1g/devices.h"
#include "nano1g/map.h"
#include "nano1g/ram.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define DMA_CH_BASE 0x1000u
#define DMA_CMD_WAIT_REQ (1u << 24)
#define DMA_STATUS_INTR (1u << 30)
#define APPLE_DMA_IRQ_BIT (1u << 26)
#define APPLE_DMA_MANAGER_PTR 0x10705ba0u
#define APPLE_DMA_ACTIVE2_HEAD 0x1070be1cu
#define APPLE_DMA_MANAGER_SERVICE2 52u

#define LCD_CNTL_RAM_ADDR_SET       0x21u
#define LCD_CNTL_HORIZ_RAM_ADDR_POS 0x44u
#define LCD_CNTL_VERT_RAM_ADDR_POS  0x45u

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

int main(void) {
    n1g_state_t s;
    memset(&s, 0, sizeof(s));
    s.opts.profile = N1G_PROFILE_APPLE;

    if (!n1g_ram_init(&s)) {
        fprintf(stderr, "failed to allocate RAM\n");
        return 1;
    }

    const uint32_t src = N1G_SDRAM_BASE + 0x2000u;
    const uint32_t manager = 0x10710000u;
    const uint32_t service = 0x10711000u;
    const uint32_t record = service + 16u;
    for (uint32_t i = 0; i < 4u; i++) {
        (void)n1g_ram_write(&s, src + i * 4u, 4, 0x12340000u + i);
    }
    (void)n1g_ram_write(&s, APPLE_DMA_MANAGER_PTR, 4, manager);
    (void)n1g_ram_write(&s, manager + APPLE_DMA_MANAGER_SERVICE2, 4, service);
    (void)n1g_ram_write(&s, service, 4, 0x00504cd8u);
    (void)n1g_ram_write(&s, service + 188u, 4, record);
    (void)n1g_ram_write(&s, record + 64u, 4, manager);
    (void)n1g_ram_write(&s, record + 84u, 1, 0u);
    (void)n1g_ram_write(&s, APPLE_DMA_ACTIVE2_HEAD, 4, 0u);

    n1g_dev_dma_write(&s, DMA_CH_BASE + 0x10u, 4, src);
    n1g_dev_dma_write(&s, DMA_CH_BASE + 0x18u, 4, N1G_LCD2_BASE + 0x100u);
    n1g_dev_dma_write(&s, DMA_CH_BASE, 4,
                      DMA_CMD_WAIT_REQ | (16u - 4u));
    n1g_dev_dma_tick(&s);
    int failed = expect_true(s.counters.lcd_words == 0u,
                             "Apple LCD DMA ran before an LCD2 block request");

    n1g_dev_lcd2_write(&s, 0x24u, 4, 0xc0010000u | (16u - 1u));
    n1g_dev_lcd2_write(&s, 0x20u, 4, 0x35000080u);
    failed = failed || expect_true(s.counters.lcd_words == 0u,
                             "Apple LCD DMA completed synchronously");
    n1g_dev_dma_tick(&s);

    uint32_t command = n1g_dev_dma_read(&s, DMA_CH_BASE, 4);
    failed = failed ||
        expect_true((command & DMA_CMD_WAIT_REQ) != 0u,
                    "Apple LCD DMA request configuration did not remain armed") ||
        expect_true(s.counters.lcd_words == 4u,
                    "Apple LCD DMA transferred the wrong word count") ||
        expect_true(s.lcd2.block_pixels == 8u,
                    "Apple LCD DMA counted the wrong number of block pixels") ||
        expect_true(s.lcd2.block_pixels_remaining == 0u,
                    "Apple LCD2 block did not complete") ||
        expect_true(!s.dma.lcd_request_armed[0],
                    "Apple LCD DMA request remained armed after completion") ||
        expect_true(s.dma.lcd_transfers[0] == 1u,
                    "Apple LCD DMA transfer count was incorrect") ||
        expect_true((s.intc.cpu_status & APPLE_DMA_IRQ_BIT) != 0u,
                    "Apple linked DMA service did not assert IRQ 26");

    uint32_t active_head = 0;
    uint32_t linked = 0;
    (void)n1g_ram_read(&s, APPLE_DMA_ACTIVE2_HEAD, 4, &active_head);
    (void)n1g_ram_read(&s, record + 84u, 1, &linked);
    failed = failed ||
        expect_true(active_head == record,
                    "Apple DMA service was not linked to controller 2") ||
        expect_true(linked == 1u,
                    "Apple DMA transfer record was not marked linked");

    uint32_t status = n1g_dev_dma_read(&s, DMA_CH_BASE + 0x04u, 4);
    failed = failed ||
        expect_true((status & DMA_STATUS_INTR) != 0u,
                    "Apple linked DMA completion status was not latched") ||
        expect_true((s.intc.cpu_status & APPLE_DMA_IRQ_BIT) == 0u,
                    "Apple linked DMA IRQ 26 was not acknowledged");

    uint64_t words_after_completion = s.counters.lcd_words;
    n1g_dev_lcd2_write(&s, 0x24u, 4, 0xc0010000u | (16u - 1u));
    n1g_dev_lcd2_write(&s, 0x20u, 4, 0x35000080u);
    n1g_dev_dma_tick(&s);
    failed = failed ||
        expect_true(s.counters.lcd_words == words_after_completion,
                    "Apple LCD DMA replayed a completed descriptor") ||
        expect_true(s.dma.lcd_transfers[0] == 1u,
                    "Apple LCD DMA counted a stale descriptor replay");

    n1g_dev_dma_write(&s, DMA_CH_BASE, 4,
                      DMA_CMD_WAIT_REQ | (16u - 4u));
    n1g_dev_lcd2_write(&s, 0x24u, 4, 3u);
    n1g_dev_lcd2_write(&s, 0x20u, 4, 0x35000080u);
    n1g_dev_dma_tick(&s);
    failed = failed ||
        expect_true(s.counters.lcd_words == words_after_completion,
                    "Apple LCD DMA ran against a mismatched setup block") ||
        expect_true(s.dma.lcd_request_armed[0],
                    "Apple LCD DMA disarmed while waiting for the real block");

    n1g_dev_lcd2_write(&s, 0x24u, 4, 16u - 1u);
    n1g_dev_lcd2_write(&s, 0x20u, 4, 0x35000080u);
    n1g_dev_dma_tick(&s);
    failed = failed ||
        expect_true(s.counters.lcd_words == words_after_completion + 4u,
                    "Apple LCD DMA did not run when block and descriptor matched") ||
        expect_true(!s.dma.lcd_request_armed[0],
                    "Apple LCD DMA remained armed after the deferred transfer") ||
        expect_true(s.dma.lcd_transfers[0] == 2u,
                    "Apple deferred LCD DMA transfer count was incorrect");

    memset(&s.lcd2, 0, sizeof(s.lcd2));
    lcd_command_data(&s, LCD_CNTL_HORIZ_RAM_ADDR_POS, 0xaf00u);
    lcd_command_data(&s, LCD_CNTL_VERT_RAM_ADDR_POS, 0x8700u);
    lcd_command_data(&s, LCD_CNTL_RAM_ADDR_SET, 0x0000u);
    n1g_dev_lcd2_write(&s, 0x24u, 4, 0x0001baffu);
    n1g_dev_lcd2_write(&s, 0x20u, 4, 0x35000080u);

    const uint32_t full_gram_words = N1G_LCD_W * N1G_LCD_GRAM_H / 2u;
    for (uint32_t i = 0; i < full_gram_words; i++) {
        uint32_t word = 0x33333333u;
        if (i == 0u) word = 0x22221111u;
        if (i == full_gram_words - 1u) word = 0x44443333u;
        n1g_dev_lcd2_write(&s, 0x100u, 4, word);
    }
    n1g_dev_lcd2_write(&s, 0x100u, 4, 0x66665555u);

    failed = failed ||
        expect_true(s.lcd2.window_y1 == N1G_LCD_GRAM_H - 1u,
                    "Apple LCD window was clamped to the visible panel") ||
        expect_true(s.lcd2.block_pixels_remaining == 0u,
                    "Apple full-GRAM transfer did not complete") ||
        expect_true(s.lcd2.pixels[0] == 0x1111u && s.lcd2.pixels[1] == 0x2222u,
                    "Apple hidden rows wrapped over the top of the visible panel") ||
        expect_true(s.lcd2.pixels[N1G_LCD_W * N1G_LCD_H] == 0x3333u,
                    "Apple first hidden GRAM row was not retained") ||
        expect_true(s.lcd2.pixels[N1G_LCD_W * N1G_LCD_GRAM_H - 1u] == 0x4444u,
                    "Apple final hidden GRAM pixel was not retained") ||
        expect_true(s.lcd2.pixels[0] == 0x1111u,
                    "LCD block overrun overwrote the window origin") ||
        expect_true(s.lcd2.block_overrun_words == 1u,
                    "LCD block overrun was not counted");

    n1g_ram_destroy(&s);
    return failed ? 1 : 0;
}
