#include "nano1g/devices.h"

#include "nano1g/trace.h"

#include <stdio.h>

uint32_t n1g_dev_lcd2_read(n1g_state_t *s, uint32_t offset, uint32_t size) {
    (void)size;
    if (offset < sizeof(s->lcd2.regs)) {
        return s->lcd2.regs[offset / 4u];
    }
    return 0;
}

void n1g_dev_lcd2_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value) {
    (void)size;
    if (offset < sizeof(s->lcd2.regs)) {
        s->lcd2.regs[offset / 4u] = value;
    }
    if (offset == 0x100) {
        uint32_t idx = s->lcd2.cursor++ % (N1G_LCD_W * N1G_LCD_H / 2u);
        s->lcd2.pixels[idx * 2u] = (uint16_t)(value & 0xffffu);
        s->lcd2.pixels[idx * 2u + 1u] = (uint16_t)(value >> 16);
        s->lcd2.words++;
        s->counters.lcd_words++;
        s->lcd2.dirty = true;
    } else if (offset == 0x20) {
        if ((value & 0xff000000u) != 0) {
            s->lcd2.cursor = 0;
        }
    }
}

static void rgb565(uint16_t p, unsigned *r, unsigned *g, unsigned *b) {
    *r = ((p >> 11) & 0x1f) * 255u / 31u;
    *g = ((p >> 5) & 0x3f) * 255u / 63u;
    *b = (p & 0x1f) * 255u / 31u;
}

bool n1g_dev_lcd2_write_ppm(n1g_state_t *s, const char *path) {
    if (!path) {
        return true;
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        n1g_log(s, "failed to open ppm: %s", path);
        return false;
    }
    fprintf(f, "P6\n%u %u\n255\n", N1G_LCD_W, N1G_LCD_H);
    for (uint32_t i = 0; i < N1G_LCD_W * N1G_LCD_H; i++) {
        unsigned r, g, b;
        rgb565(s->lcd2.pixels[i], &r, &g, &b);
        fputc((int)r, f);
        fputc((int)g, f);
        fputc((int)b, f);
    }
    fclose(f);
    n1g_log(s, "wrote ppm %s lcd_words=%llu", path, (unsigned long long)s->lcd2.words);
    return true;
}
