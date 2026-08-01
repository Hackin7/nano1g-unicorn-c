#include "nano1g/devices.h"

#include <stdio.h>
#include <string.h>

#define I2C_CTRL_START 0x80u
#define I2C_CTRL_TWO_BYTES 0x02u
#define WM8975_ADDR_WRITE (0x1au << 1u)
#define WM8975_RESET_REG 0x0fu

static int expect_true(int condition, const char *message) {
    if (!condition) {
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

    write_codec(&s, WM8975_RESET_REG, 0u);
    write_codec(&s, 0x06u, 0x067u);
    write_codec(&s, 0x02u, 0x180u);

    if (expect_true(s.i2c.transactions == 3u, "wrong I2C transaction count") ||
        expect_true(s.i2c.addr_writes[0x1au] == 3u, "wrong WM8975 write count") ||
        expect_true(s.i2c.wm8975_resets == 1u, "WM8975 reset was not recorded") ||
        expect_true(s.i2c.wm8975_regs[0x06u] == 0x067u,
                    "WM8975 low eight-bit value was decoded incorrectly") ||
        expect_true(s.i2c.wm8975_regs[0x02u] == 0x180u,
                    "WM8975 ninth data bit was decoded incorrectly") ||
        expect_true((s.i2c.wm8975_written & (1ull << 0x06u)) != 0u,
                    "WM8975 written-register mask missed register 6") ||
        expect_true((s.i2c.wm8975_written & (1ull << 0x02u)) != 0u,
                    "WM8975 written-register mask missed register 2")) {
        return 1;
    }

    write_codec(&s, WM8975_RESET_REG, 0u);
    if (expect_true(s.i2c.wm8975_resets == 2u, "second WM8975 reset was not recorded") ||
        expect_true(s.i2c.wm8975_regs[0x06u] == 0u,
                    "WM8975 reset did not clear register 6") ||
        expect_true(s.i2c.wm8975_regs[0x02u] == 0u,
                    "WM8975 reset did not clear register 2") ||
        expect_true(s.i2c.wm8975_written == 0u,
                    "WM8975 reset did not clear the written-register mask")) {
        return 1;
    }

    puts("i2c codec unit ok");
    return 0;
}
