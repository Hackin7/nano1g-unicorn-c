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
#define RB_CMD_REQ_IIS (2u << 16)
#define RB_CMD_WAIT_REQ (1u << 24)
#define RB_CMD_SINGLE (1u << 26)
#define RB_CMD_RAM_TO_PER (1u << 27)
#define RB_STATUS_BUSY (1u << 31)

#define IIS_CONFIG 0x00u
#define IIS_FIFO_CFG 0x0cu
#define IISFIFO_WR 0x40u
#define IIS_TXFIFOEN (1u << 29)
#define IIS_FIFO_TXCLR (1u << 8)
#define WM8975_ADDR_WRITE (0x1au << 1u)
#define I2C_CTRL_START 0x80u
#define I2C_CTRL_TWO_BYTES 0x02u

static int expect_true(bool cond, const char *message) {
    if (!cond) {
        fprintf(stderr, "%s\n", message);
        return 1;
    }
    return 0;
}

static void write_codec(n1g_state_t *s, uint8_t reg, uint16_t value) {
    uint8_t first = (uint8_t)((reg << 1u) | ((value >> 8u) & 1u));
    uint32_t packed = (uint32_t)first | ((uint32_t)(value & 0xffu) << 8u);
    n1g_dev_i2c_write(s, 0x04u, 1u, WM8975_ADDR_WRITE);
    n1g_dev_i2c_write(s, 0x0cu, 2u, packed);
    n1g_dev_i2c_write(s, 0x00u, 1u, I2C_CTRL_START | I2C_CTRL_TWO_BYTES);
}

int main(void) {
    n1g_state_t s;
    memset(&s, 0, sizeof(s));
    s.opts.rtc_usec_per_tick = 8u;

    if (!n1g_ram_init(&s)) {
        fprintf(stderr, "failed to allocate RAM\n");
        return 1;
    }

    const uint32_t src = N1G_SDRAM_BASE + 0x1000u;
    for (uint32_t i = 0; i < 64u; i++) {
        (void)n1g_ram_write(&s, src + i * 4u, 4, 0x10000000u + i);
    }

    write_codec(&s, 0x0fu, 0u);
    write_codec(&s, 0x06u, 0x067u);
    write_codec(&s, 0x04u, 0x012u);
    write_codec(&s, 0x05u, 0x000u);
    write_codec(&s, 0x08u, 0x023u);
    write_codec(&s, 0x09u, 0x001u);
    write_codec(&s, 0x02u, 0x1d8u);

    n1g_dev_i2s_write(&s, IIS_CONFIG, 4, IIS_TXFIFOEN);
    n1g_dev_dma_write(&s, DMA_CH_BASE + 0x10u, 4, src);
    n1g_dev_dma_write(&s, DMA_CH_BASE + 0x18u, 4, N1G_I2S_BASE + IISFIFO_WR);
    n1g_dev_dma_write(&s, DMA_CH_BASE + 0x00u, 4,
                      RB_CMD_START | RB_CMD_INTR | RB_CMD_REQ_IIS |
                      RB_CMD_WAIT_REQ | RB_CMD_SINGLE | RB_CMD_RAM_TO_PER |
                      (256u - 4u));

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
        expect_true(s.dma.ch[0].bytes_pushed == 256u, "DMA audio transfer pushed the wrong byte count") ||
        expect_true(s.i2s.pcm_produced_halfwords == s.i2s.tx_drained_halfwords,
                    "PCM sink did not receive every drained halfword") ||
        expect_true(s.i2s.pcm_nonzero_halfwords > 0u, "PCM sink discarded the sample values") ||
        expect_true(s.i2s.pcm_peak > 0u, "PCM sink peak stayed zero") ||
        expect_true(s.i2c.wm8975_output_enabled, "codec output path was not enabled") ||
        expect_true(s.i2c.wm8975_sample_rate == 44100u, "codec sample rate was not applied") ||
        expect_true(s.i2s.underruns == 0u, "paced DMA transfer underran the I2S FIFO");

    while (s.i2s.tx_fill > 0u) {
        n1g_dev_i2s_tick(&s);
    }
    uint64_t produced_before_mute = s.i2s.pcm_produced_halfwords;
    uint64_t nonzero_before_mute = s.i2s.pcm_nonzero_halfwords;
    write_codec(&s, 0x05u, 0x008u);
    n1g_dev_i2s_write(&s, IISFIFO_WR, 4, 0x40004000u);
    for (uint32_t i = 0; i < 16u &&
         s.i2s.pcm_produced_halfwords < produced_before_mute + 2u; i++) {
        n1g_dev_i2s_tick(&s);
    }
    failed = failed ||
             expect_true(!s.i2c.wm8975_output_enabled, "codec mute did not disable output") ||
             expect_true(s.i2s.pcm_produced_halfwords == produced_before_mute + 2u,
                         "muted PCM did not preserve stream timing") ||
             expect_true(s.i2s.pcm_nonzero_halfwords == nonzero_before_mute,
                         "muted PCM leaked nonzero samples") ||
             expect_true(s.i2s.pcm_silenced_halfwords >= 2u,
                         "muted PCM samples were not recorded as silenced");

    n1g_dev_i2s_write(&s, IIS_CONFIG, 4, 0u);
    uint64_t produced_while_paused = s.i2s.pcm_produced_halfwords;
    for (uint32_t i = 0; i < 8u; i++) {
        n1g_dev_i2s_tick(&s);
    }
    failed = failed ||
             expect_true(s.i2s.pcm_produced_halfwords == produced_while_paused,
                         "disabled I2S clock produced PCM");

    uint64_t underruns_before_resume = s.i2s.underruns;
    uint64_t underrun_samples_before_resume = s.i2s.underrun_halfwords;
    n1g_dev_i2s_write(&s, IIS_CONFIG, 4, IIS_TXFIFOEN);
    n1g_dev_i2s_tick(&s);
    failed = failed ||
             expect_true(s.i2s.pcm_produced_halfwords == produced_while_paused,
                         "fractional I2S clock advanced too early") ||
             expect_true(s.i2s.drain_acc != 0u,
                         "empty FIFO discarded fractional I2S clock phase");
    n1g_dev_i2s_tick(&s);
    n1g_dev_i2s_tick(&s);
    failed = failed ||
             expect_true(s.i2s.pcm_produced_halfwords == produced_while_paused + 2u,
                         "resumed I2S clock did not emit underflow silence") ||
             expect_true(s.i2s.underruns == underruns_before_resume + 1u,
                         "I2S underrun event was not edge-counted") ||
             expect_true(s.i2s.underrun_halfwords == underrun_samples_before_resume + 2u,
                         "I2S underrun sample count was wrong");
    for (uint32_t i = 0; i < 3u; i++) {
        n1g_dev_i2s_tick(&s);
    }
    failed = failed ||
             expect_true(s.i2s.underruns == underruns_before_resume + 1u,
                         "continuous starvation counted duplicate underrun events");

    uint64_t drained_before_recovery = s.i2s.tx_drained_halfwords;
    n1g_dev_i2s_write(&s, IISFIFO_WR, 4, 0x22221111u);
    for (uint32_t i = 0; i < 8u &&
         s.i2s.tx_drained_halfwords < drained_before_recovery + 2u; i++) {
        n1g_dev_i2s_tick(&s);
    }
    failed = failed ||
             expect_true(s.i2s.tx_drained_halfwords == drained_before_recovery + 2u,
                         "I2S did not recover after an underrun");
    for (uint32_t i = 0; i < 3u; i++) {
        n1g_dev_i2s_tick(&s);
    }
    failed = failed ||
             expect_true(s.i2s.underruns == underruns_before_resume + 2u,
                         "new starvation did not start a new underrun event");

    n1g_dev_i2s_write(&s, IIS_CONFIG, 4, 0u);
    n1g_dev_i2s_write(&s, IIS_FIFO_CFG, 4, IIS_FIFO_TXCLR);
    uint64_t overruns_before = s.i2s.tx_overruns;
    for (uint32_t i = 0; i < 17u; i++) {
        n1g_dev_i2s_write(&s, IISFIFO_WR, 4, i | (i << 16u));
    }
    failed = failed ||
             expect_true(s.i2s.tx_fill == N1G_I2S_TX_DEPTH,
                         "I2S FIFO exceeded its hardware depth") ||
             expect_true(s.i2s.tx_overruns == overruns_before + 2u,
                         "I2S FIFO overrun samples were not counted");
    n1g_dev_i2s_write(&s, IIS_FIFO_CFG, 4, IIS_FIFO_TXCLR);

    write_codec(&s, 0x05u, 0x000u);
    write_codec(&s, 0x08u, 0x041u);
    uint32_t stream_before_rate_change = s.i2s.pcm_stream_id;
    uint64_t stream_start = s.i2s.pcm_produced_halfwords;
    n1g_dev_i2s_write(&s, IIS_CONFIG, 4, IIS_TXFIFOEN);
    n1g_dev_i2s_tick(&s);
    failed = failed ||
             expect_true(s.i2c.wm8975_sample_rate == 48000u,
                         "codec sample-rate transition was not decoded") ||
             expect_true(s.i2s.pcm_sample_rate == 48000u,
                         "I2S clock did not adopt the codec sample rate") ||
             expect_true(s.i2s.pcm_stream_id == stream_before_rate_change + 1u,
                         "sample-rate transition did not start a new PCM stream") ||
             expect_true(s.i2s.pcm_stream_start_halfword >= stream_start,
                         "new PCM stream started before the rate transition");

    n1g_ram_destroy(&s);
    return failed ? 1 : 0;
}
