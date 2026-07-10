#include "nano1g/devices.h"
#include "nano1g/map.h"
#include "nano1g/ram.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define DMA_CH_BASE 0x1000u
#define DMA_CH_STRIDE 0x20u
#define RB_CMD_INTR (1u << 30)
#define RB_CMD_START (1u << 31)
#define RB_STATUS_BUSY (1u << 31)

#define IIS_CONFIG 0x00u
#define IISFIFO_WR 0x40u
#define IIS_TXFIFOEN (1u << 29)

static int expect_true(bool cond, const char *message) {
    if (!cond) {
        fprintf(stderr, "%s\n", message);
        return 1;
    }
    return 0;
}

int main(void) {
    n1g_state_t s;
    memset(&s, 0, sizeof(s));
    s.opts.rtc_usec_per_tick = 1000u;

    if (!n1g_ram_init(&s)) {
        fprintf(stderr, "failed to allocate RAM\n");
        return 1;
    }

    const uint32_t src = N1G_SDRAM_BASE + 0x1000u;
    for (uint32_t i = 0; i < 64u; i++) {
        (void)n1g_ram_write(&s, src + i * 4u, 4, 0x10000000u + i);
    }

    n1g_dev_i2s_write(&s, IIS_CONFIG, 4, IIS_TXFIFOEN);
    n1g_dev_dma_write(&s, DMA_CH_BASE + 0x10u, 4, src);
    n1g_dev_dma_write(&s, DMA_CH_BASE + 0x18u, 4, N1G_I2S_BASE + IISFIFO_WR);
    n1g_dev_dma_write(&s, DMA_CH_BASE + 0x00u, 4, RB_CMD_START | RB_CMD_INTR | (256u - 4u));

    if (expect_true(s.dma.ch[0].starts == 1u, "DMA audio transfer did not start") ||
        expect_true(s.dma.ch[0].bytes_pushed > 0u, "DMA audio transfer did not push bytes") ||
        expect_true(s.i2s.tx_halfwords > 0u, "I2S FIFO did not accept samples")) {
        n1g_ram_destroy(&s);
        return 1;
    }

    for (uint32_t i = 0; i < 10000u && s.dma.ch[0].active; i++) {
        n1g_dev_i2s_tick(&s);
        n1g_dev_dma_tick(&s);
    }

    uint32_t status = n1g_dev_dma_read(&s, DMA_CH_BASE + 0x04u, 4);
    int failed =
        expect_true(s.dma.ch[0].completions == 1u, "DMA audio transfer did not complete") ||
        expect_true((status & RB_STATUS_BUSY) == 0u, "DMA audio status remained busy") ||
        expect_true(s.i2s.tx_drained_halfwords > 0u, "I2S FIFO did not drain in guest time") ||
        expect_true(s.dma.ch[0].bytes_pushed == 256u, "DMA audio transfer pushed the wrong byte count");

    n1g_ram_destroy(&s);
    return failed ? 1 : 0;
}
