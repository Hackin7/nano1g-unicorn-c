#include "nano1g/devices.h"

#include <stdio.h>
#include <string.h>

#define I2C_CTRL 0x00u
#define I2C_ADDR 0x04u
#define I2C_DATA0 0x0cu

#define PCF_WRITE_ADDR 0x10u
#define PCF_READ_ADDR 0x11u
#define PCF_INT1 0x02u
#define PCF_INT1M 0x05u
#define PCF_OOCC1 0x08u

static int expect_u32(uint32_t actual, uint32_t expected, const char *message) {
    if (actual != expected) {
        fprintf(stderr, "%s: got 0x%08x, expected 0x%08x\n",
                message, actual, expected);
        return 1;
    }
    return 0;
}

static void pcf_set_pointer(n1g_state_t *s, uint8_t reg) {
    n1g_dev_i2c_write(s, I2C_ADDR, 4u, PCF_WRITE_ADDR);
    n1g_dev_i2c_write(s, I2C_DATA0, 1u, reg);
    n1g_dev_i2c_write(s, I2C_CTRL, 4u, 0x80u);
}

static uint8_t pcf_read_byte(n1g_state_t *s, uint8_t reg) {
    pcf_set_pointer(s, reg);
    n1g_dev_i2c_write(s, I2C_ADDR, 4u, PCF_READ_ADDR);
    n1g_dev_i2c_write(s, I2C_CTRL, 4u, 0xa0u);
    return (uint8_t)n1g_dev_i2c_read(s, I2C_DATA0, 1u);
}

static void pcf_write_byte(n1g_state_t *s, uint8_t reg, uint8_t value) {
    n1g_dev_i2c_write(s, I2C_ADDR, 4u, PCF_WRITE_ADDR);
    n1g_dev_i2c_write(s, I2C_DATA0, 2u, (uint32_t)reg | ((uint32_t)value << 8u));
    n1g_dev_i2c_write(s, I2C_CTRL, 4u, 0x82u);
}

int main(void) {
    n1g_state_t s;
    memset(&s, 0, sizeof(s));
    s.opts.rtc_usec_per_tick = 1u;

    s.i2c.pcf_regs[PCF_INT1] = 0xa5u;
    int failed = expect_u32(pcf_read_byte(&s, PCF_INT1), 0xa5u,
                            "first interrupt-status read lost pending bits") |
                 expect_u32(pcf_read_byte(&s, PCF_INT1), 0u,
                            "interrupt-status read did not clear pending bits");

    pcf_write_byte(&s, PCF_INT1, 0x5au);
    failed |= expect_u32(pcf_read_byte(&s, PCF_INT1), 0u,
                         "guest write fabricated interrupt-status bits") |
              expect_u32((uint32_t)s.i2c.pcf_reg_writes[PCF_INT1], 1u,
                         "interrupt-status write attempt was not counted");

    pcf_write_byte(&s, PCF_INT1M, 0x96u);
    failed |= expect_u32(pcf_read_byte(&s, PCF_INT1M), 0x96u,
                         "interrupt mask did not retain a guest write");

    pcf_write_byte(&s, PCF_OOCC1, 0x61u);
    failed |= expect_u32(pcf_read_byte(&s, PCF_OOCC1), 0x61u,
                         "OOCC1 did not retain wake and standby bits") |
              expect_u32((uint32_t)s.i2c.pcf_standby_requests, 1u,
                         "GOSTDBY did not record one standby request");
    pcf_write_byte(&s, PCF_OOCC1, 0x60u);
    failed |= expect_u32((uint32_t)s.i2c.pcf_standby_requests, 1u,
                         "non-standby OOCC1 write changed request count");

    if (!failed) {
        puts("i2c pmu unit ok");
    }
    return failed ? 1 : 0;
}
