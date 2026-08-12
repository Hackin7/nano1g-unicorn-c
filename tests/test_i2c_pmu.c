#include "nano1g/devices.h"

#include <stdio.h>
#include <string.h>

#define I2C_CTRL 0x00u
#define I2C_ADDR 0x04u
#define I2C_DATA0 0x0cu

#define PCF_WRITE_ADDR 0x10u
#define PCF_READ_ADDR 0x11u
#define PCF_OOCS 0x01u
#define PCF_INT1 0x02u
#define PCF_INT2 0x03u
#define PCF_INT1M 0x05u
#define PCF_OOCC1 0x08u
#define PCF_RTCSC 0x0au
#define PCF_RTCMN 0x0bu
#define PCF_RTCHR 0x0cu
#define PCF_RTCWD 0x0du
#define PCF_RTCDT 0x0eu
#define PCF_RTCMT 0x0fu
#define PCF_RTCYR 0x10u
#define PCF_RTCSCA 0x11u
#define PCF_RTCMNA 0x12u

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
    s.opts.rtc_usec_per_tick = 1000u;

    int failed = expect_u32(pcf_read_byte(&s, PCF_OOCS), 0x58u,
                            "OOCS reset power-good state is wrong") |
                 expect_u32(pcf_read_byte(&s, PCF_OOCC1), 0x60u,
                            "OOCC1 reset wake configuration is wrong");
    s.opts.usb_charger_connected = true;
    failed |= expect_u32(pcf_read_byte(&s, PCF_OOCS), 0x78u,
                         "OOCS charger state did not follow hardware state");
    s.opts.usb_charger_connected = false;

    pcf_write_byte(&s, PCF_RTCSCA, 0x01u);
    pcf_write_byte(&s, PCF_OOCC1, 0x11u);
    s.counters.device_ticks++;
    n1g_dev_i2c_tick(&s);
    failed |= expect_u32((uint32_t)s.i2c.pcf_standby, 1u,
                         "GOSTDBY did not enter standby") |
              expect_u32((uint32_t)s.i2c.pcf_standby_transitions, 1u,
                         "standby transition count is wrong") |
              expect_u32((uint32_t)s.cpu[N1G_CORE_CPU].halted, 1u,
                         "standby did not halt the CPU") |
              expect_u32(pcf_read_byte(&s, PCF_OOCC1), 0x10u,
                         "GOSTDBY did not auto-clear");
    s.counters.device_ticks += 999u;
    n1g_dev_i2c_tick(&s);
    failed |= expect_u32((uint32_t)s.i2c.pcf_standby, 0u,
                         "RTC alarm did not leave standby") |
              expect_u32((uint32_t)s.i2c.pcf_wake_requests, 1u,
                         "RTC alarm wake request count is wrong") |
              expect_u32((uint32_t)s.i2c.pcf_last_wake,
                         N1G_PCF_WAKE_RTC_ALARM,
                         "RTC alarm wake reason is wrong") |
              expect_u32(pcf_read_byte(&s, PCF_INT1), 0xc0u,
                         "RTC second/alarm status was not latched during wake");

    pcf_write_byte(&s, PCF_OOCC1, 0x20u);
    s.i2c.pcf_standby = true;
    n1g_dev_i2c_charger_event(&s, true);
    failed |= expect_u32((uint32_t)s.i2c.pcf_last_wake, N1G_PCF_WAKE_CHARGER,
                         "charger wake reason is wrong") |
              expect_u32(pcf_read_byte(&s, PCF_INT2), 0x01u,
                         "charger insertion status was not latched");
    s.i2c.pcf_standby = true;
    n1g_dev_i2c_onkey(&s, true);
    failed |= expect_u32((uint32_t)s.i2c.pcf_last_wake, N1G_PCF_WAKE_ONKEY,
                         "ONKEY wake reason is wrong") |
              expect_u32(pcf_read_byte(&s, PCF_INT1), 0x02u,
                         "ONKEY status was not latched");

    memset(&s, 0, sizeof(s));
    s.opts.rtc_usec_per_tick = 1u;

    s.i2c.pcf_regs[PCF_INT1] = 0xa5u;
    failed |= expect_u32(pcf_read_byte(&s, PCF_INT1), 0xa5u,
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

    failed |= expect_u32(pcf_read_byte(&s, PCF_RTCSCA), 0x7fu,
                         "seconds alarm reset value is wrong") |
              expect_u32(pcf_read_byte(&s, PCF_RTCMNA), 0x7fu,
                         "minutes alarm reset value is wrong");

    s.opts.rtc_usec_per_tick = 1000000u;
    pcf_write_byte(&s, PCF_RTCSCA, 0x01u);
    s.counters.device_ticks++;
    n1g_dev_i2c_tick(&s);
    failed |= expect_u32(pcf_read_byte(&s, PCF_INT1), 0xc0u,
                         "RTC second/alarm status was not latched") |
              expect_u32((uint32_t)s.i2c.rtc_second_interrupts, 1u,
                         "RTC second event count is wrong") |
              expect_u32((uint32_t)s.i2c.rtc_alarm_interrupts, 1u,
                         "RTC alarm event count is wrong");

    n1g_dev_i2c_tick(&s);
    failed |= expect_u32(pcf_read_byte(&s, PCF_INT1), 0u,
                         "alarm retriggered within the same RTC second");

    s.counters.device_ticks += 59u;
    n1g_dev_i2c_tick(&s);
    failed |= expect_u32(pcf_read_byte(&s, PCF_RTCSC), 0x00u,
                         "RTC seconds did not roll over") |
              expect_u32(pcf_read_byte(&s, PCF_RTCMN), 0x01u,
                         "RTC minutes did not advance") |
              expect_u32(pcf_read_byte(&s, PCF_INT1), 0x40u,
                         "periodic second status was not latched");

    pcf_write_byte(&s, PCF_RTCSC, 0x58u);
    pcf_write_byte(&s, PCF_RTCMN, 0x59u);
    pcf_write_byte(&s, PCF_RTCHR, 0x23u);
    pcf_write_byte(&s, PCF_RTCWD, 0x01u);
    pcf_write_byte(&s, PCF_RTCDT, 0x28u);
    pcf_write_byte(&s, PCF_RTCMT, 0x02u);
    pcf_write_byte(&s, PCF_RTCYR, 0x28u);
    pcf_write_byte(&s, PCF_RTCMNA, 0x42u);
    s.counters.device_ticks += 2u;

    n1g_pcf_backup_t backup;
    n1g_dev_i2c_save_pcf(&s, &backup);
    memset(&s, 0, sizeof(s));
    s.opts.rtc_usec_per_tick = 1000000u;
    n1g_dev_i2c_restore_pcf(&s, &backup);

    failed |= expect_u32(pcf_read_byte(&s, PCF_RTCSC), 0x00u,
                         "RTC seconds were not materialized across restart") |
              expect_u32(pcf_read_byte(&s, PCF_RTCMN), 0x00u,
                         "RTC minutes were not preserved across restart") |
              expect_u32(pcf_read_byte(&s, PCF_RTCHR), 0x00u,
                         "RTC hour was not preserved across restart") |
              expect_u32(pcf_read_byte(&s, PCF_RTCDT), 0x29u,
                         "RTC leap-day rollover was not preserved") |
              expect_u32(pcf_read_byte(&s, PCF_RTCMNA), 0x42u,
                         "RTC alarm programming was not preserved");

    if (!failed) {
        puts("i2c pmu unit ok");
    }
    return failed ? 1 : 0;
}
