#include "nano1g/devices.h"

#include "nano1g/ram.h"
#include "nano1g/trace.h"

#include <stdio.h>

#define LCD_CNTL_RAM_ADDR_SET       0x21u
#define LCD_CNTL_WRITE_TO_GRAM      0x22u
#define LCD_CNTL_HORIZ_RAM_ADDR_POS 0x44u
#define LCD_CNTL_VERT_RAM_ADDR_POS  0x45u

#define LCD2_CMD_MASK    0x80000000u
#define LCD2_BLOCK_READY 0x04000000u
#define LCD2_BLOCK_TXOK  0x01000000u
#define LCD2_DATA_MASK   0x81000000u

uint32_t n1g_dev_lcd2_read(n1g_state_t *s, uint32_t offset, uint32_t size) {
    (void)size;
    if (offset == 0x00 || offset == 0x04) {
        return s->lcd2.block_ctrl | LCD2_BLOCK_READY | LCD2_BLOCK_TXOK;
    }
    if (offset == 0x0c) {
        return 0;
    }
    if (offset == 0x20) {
        return (s->lcd2.block_ctrl & ~0x40u) | LCD2_BLOCK_READY | LCD2_BLOCK_TXOK;
    }
    if (offset == 0x24) {
        return s->lcd2.block_config & 0x3fffffffu;
    }
    if (offset < sizeof(s->lcd2.regs)) {
        return s->lcd2.regs[offset / 4u];
    }
    return 0;
}

static void lcd2_init_window(n1g_state_t *s) {
    if (s->lcd2.window_x1 == 0 && s->lcd2.window_y1 == 0) {
        s->lcd2.window_x0 = 0;
        s->lcd2.window_y0 = 0;
        s->lcd2.window_x1 = (uint8_t)(N1G_LCD_W - 1u);
        s->lcd2.window_y1 = (uint8_t)(N1G_LCD_H - 1u);
    }
}

static uint16_t swap16(uint16_t value) {
    return (uint16_t)((value >> 8u) | (value << 8u));
}

static void lcd2_clamp_window(n1g_state_t *s) {
    if (s->lcd2.window_x0 >= N1G_LCD_W) s->lcd2.window_x0 = (uint8_t)(N1G_LCD_W - 1u);
    if (s->lcd2.window_x1 >= N1G_LCD_W) s->lcd2.window_x1 = (uint8_t)(N1G_LCD_W - 1u);
    if (s->lcd2.window_y0 >= N1G_LCD_GRAM_H) s->lcd2.window_y0 = (uint8_t)(N1G_LCD_GRAM_H - 1u);
    if (s->lcd2.window_y1 >= N1G_LCD_GRAM_H) s->lcd2.window_y1 = (uint8_t)(N1G_LCD_GRAM_H - 1u);
    if (s->lcd2.window_x1 < s->lcd2.window_x0) s->lcd2.window_x1 = s->lcd2.window_x0;
    if (s->lcd2.window_y1 < s->lcd2.window_y0) s->lcd2.window_y1 = s->lcd2.window_y0;
}

static void lcd2_set_cursor(n1g_state_t *s, uint8_t x, uint8_t y) {
    s->lcd2.cursor_x = x >= N1G_LCD_W ? (uint8_t)(N1G_LCD_W - 1u) : x;
    s->lcd2.cursor_y = y >= N1G_LCD_GRAM_H ? (uint8_t)(N1G_LCD_GRAM_H - 1u) : y;
    s->lcd2.cursor = (uint32_t)s->lcd2.cursor_y * N1G_LCD_W + s->lcd2.cursor_x;
}

static void lcd2_push_pixel(n1g_state_t *s, uint16_t pixel) {
    lcd2_init_window(s);
    if (s->lcd2.cursor_x < N1G_LCD_W && s->lcd2.cursor_y < N1G_LCD_GRAM_H) {
        s->lcd2.pixels[(uint32_t)s->lcd2.cursor_y * N1G_LCD_W + s->lcd2.cursor_x] = pixel;
    }

    s->lcd2.cursor_x++;
    if (s->lcd2.cursor_x > s->lcd2.window_x1 || s->lcd2.cursor_x >= N1G_LCD_W) {
        s->lcd2.cursor_x = s->lcd2.window_x0;
        s->lcd2.cursor_y++;
        if (s->lcd2.cursor_y > s->lcd2.window_y1 || s->lcd2.cursor_y >= N1G_LCD_GRAM_H) {
            s->lcd2.cursor_y = s->lcd2.window_y0;
        }
    }
    s->lcd2.cursor = (uint32_t)s->lcd2.cursor_y * N1G_LCD_W + s->lcd2.cursor_x;
    s->lcd2.dirty = true;
}

static void lcd2_count_word(n1g_state_t *s) {
    s->lcd2.words++;
    s->counters.lcd_words++;
}

static void lcd2_handle_command_data(n1g_state_t *s, uint16_t value) {
    lcd2_init_window(s);
    switch (s->lcd2.command) {
    case LCD_CNTL_HORIZ_RAM_ADDR_POS:
        n1g_log(s,
                "lcd window horizontal raw=0x%04x x0=%u x1=%u",
                value,
                (unsigned)(value & 0xffu),
                (unsigned)(value >> 8u));
        s->lcd2.window_x0 = (uint8_t)(value & 0xffu);
        s->lcd2.window_x1 = (uint8_t)(value >> 8u);
        s->lcd2.window_sets++;
        lcd2_clamp_window(s);
        break;
    case LCD_CNTL_VERT_RAM_ADDR_POS:
        n1g_log(s,
                "lcd window vertical raw=0x%04x y0=%u y1=%u",
                value,
                (unsigned)(value & 0xffu),
                (unsigned)(value >> 8u));
        s->lcd2.window_y0 = (uint8_t)(value & 0xffu);
        s->lcd2.window_y1 = (uint8_t)(value >> 8u);
        s->lcd2.window_sets++;
        lcd2_clamp_window(s);
        break;
    case LCD_CNTL_RAM_ADDR_SET:
        lcd2_set_cursor(s, (uint8_t)(value & 0xffu), (uint8_t)(value >> 8u));
        s->lcd2.cursor_sets++;
        break;
    case LCD_CNTL_WRITE_TO_GRAM:
        lcd2_push_pixel(s, swap16(value));
        s->lcd2.gram_pixels++;
        lcd2_count_word(s);
        break;
    default:
        break;
    }
}

static void lcd2_write_port(n1g_state_t *s, uint32_t value) {
    if (value == LCD2_CMD_MASK) {
        s->lcd2.pending_command = true;
        s->lcd2.pending_data = false;
        return;
    }

    if ((value & LCD2_DATA_MASK) == LCD2_DATA_MASK) {
        uint8_t byte = (uint8_t)value;
        if (s->lcd2.pending_data) {
            uint16_t data = (uint16_t)(((uint16_t)s->lcd2.pending_data_hi << 8u) | byte);
            s->lcd2.pending_data = false;
            lcd2_handle_command_data(s, data);
        } else {
            s->lcd2.pending_data_hi = byte;
            s->lcd2.pending_data = true;
        }
        return;
    }

    if ((value & LCD2_CMD_MASK) == LCD2_CMD_MASK) {
        s->lcd2.command = (uint16_t)value;
        s->lcd2.pending_command = false;
        s->lcd2.pending_data = false;
    }
}

static void lcd2_start_block(n1g_state_t *s) {
    lcd2_init_window(s);
    s->lcd2.block_starts++;
    s->lcd2.last_block_cursor_x = s->lcd2.cursor_x;
    s->lcd2.last_block_cursor_y = s->lcd2.cursor_y;
    s->lcd2.block_ctrl &= ~LCD2_BLOCK_READY;
    s->lcd2.block_ctrl |= LCD2_BLOCK_TXOK;
    s->lcd2.block_pixels_remaining = ((s->lcd2.block_config & 0xffffu) + 1u) / 2u;
    if (s->lcd2.block_starts <= 64u) {
        uint32_t window_pixels =
            ((uint32_t)s->lcd2.window_x1 - s->lcd2.window_x0 + 1u) *
            ((uint32_t)s->lcd2.window_y1 - s->lcd2.window_y0 + 1u);
        n1g_log(s,
                "lcd block start=%llu window=%u,%u-%u,%u cursor=%u,%u pixels=%u window_pixels=%u config=0x%08x",
                (unsigned long long)s->lcd2.block_starts,
                s->lcd2.window_x0,
                s->lcd2.window_y0,
                s->lcd2.window_x1,
                s->lcd2.window_y1,
                s->lcd2.cursor_x,
                s->lcd2.cursor_y,
                s->lcd2.block_pixels_remaining,
                window_pixels,
                s->lcd2.block_config);
    }
}

static void lcd2_finish_block_if_done(n1g_state_t *s) {
    if (s->lcd2.block_pixels_remaining == 0) {
        s->lcd2.block_ctrl |= LCD2_BLOCK_READY | LCD2_BLOCK_TXOK;
    }
}

static void lcd2_push_block_word(n1g_state_t *s, uint32_t value) {
    uint16_t first = (uint16_t)(value & 0xffffu);
    uint16_t second = (uint16_t)(value >> 16u);

    if (s->lcd2.block_pixels_remaining == 0) {
        lcd2_push_pixel(s, first);
        lcd2_push_pixel(s, second);
        s->lcd2.block_pixels += 2u;
        lcd2_count_word(s);
        return;
    }

    if (s->lcd2.block_pixels_remaining > 0) {
        lcd2_push_pixel(s, first);
        s->lcd2.block_pixels++;
        s->lcd2.block_pixels_remaining--;
    }
    if (s->lcd2.block_pixels_remaining > 0) {
        lcd2_push_pixel(s, second);
        s->lcd2.block_pixels++;
        s->lcd2.block_pixels_remaining--;
    }
    lcd2_count_word(s);
    lcd2_finish_block_if_done(s);
}

void n1g_dev_lcd2_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value) {
    (void)size;
    if (offset < sizeof(s->lcd2.regs)) {
        s->lcd2.regs[offset / 4u] = value;
    }

    switch (offset) {
    case 0x00:
    case 0x0c:
        lcd2_write_port(s, value);
        break;
    case 0x04:
        s->lcd2.block_ctrl = value;
        break;
    case 0x20:
        s->lcd2.block_ctrl = value;
        if ((value & 0x30000000u) == 0x30000000u) {
            lcd2_start_block(s);
        }
        break;
    case 0x24:
        s->lcd2.block_config = value;
        break;
    case 0x100:
        lcd2_push_block_word(s, value);
        break;
    default:
        break;
    }
}

static void rgb565(uint16_t p, unsigned *r, unsigned *g, unsigned *b) {
    uint16_t raw = swap16(p);
    *r = ((raw >> 11) & 0x1f) * 255u / 31u;
    *g = ((raw >> 5) & 0x3f) * 255u / 63u;
    *b = (raw & 0x1f) * 255u / 31u;
}

bool n1g_dev_lcd2_write_ppm(n1g_state_t *s, const char *path) {
    if (!path) {
        return true;
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        n1g_info(s, "failed to open ppm: %s", path);
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
    n1g_info(s, "wrote ppm %s lcd_words=%llu", path, (unsigned long long)s->lcd2.words);
    return true;
}
