#include "nano1g/devices.h"

#include <string.h>

static uint32_t data32(const n1g_state_t *s) {
    return (uint32_t)s->i2c.data[0] |
           ((uint32_t)s->i2c.data[1] << 8u) |
           ((uint32_t)s->i2c.data[2] << 16u) |
           ((uint32_t)s->i2c.data[3] << 24u);
}

static uint32_t mask_for_size(uint32_t size) {
    if (size == 1u) return 0xffu;
    if (size == 2u) return 0xffffu;
    return 0xffffffffu;
}

static uint32_t read_part(uint32_t value, uint32_t offset, uint32_t size) {
    uint32_t shift = (offset & 3u) * 8u;
    return (value >> shift) & mask_for_size(size);
}

static void write_data_bytes(n1g_state_t *s, uint32_t first, uint32_t size, uint32_t value) {
    for (uint32_t i = 0; i < size && first + i < sizeof(s->i2c.data); i++) {
        s->i2c.data[first + i] = (uint8_t)(value >> (i * 8u));
    }
}

/* Rockbox's IPOD_NANO percent_to_volt_discharge table (millivolt at 0%, 10%,
 * ..., 100%; firmware/target/arm/ipod/powermgmt-ipod-pcf.c). 0% is 3230 mV,
 * not 0 mV, and the shutoff/"battery empty" screen fires at or below that
 * same 3230 mV floor, so any mock must interpolate from this table rather
 * than scale linearly from a zero-volt baseline.
 */
static uint32_t battery_percent_to_adc_raw(uint32_t percent) {
    static const uint16_t table_mv[11] = {
        3230, 3620, 3700, 3730, 3750, 3780, 3830, 3890, 3950, 4030, 4160
    };
    if (percent > 100u) {
        percent = 100u;
    }
    uint32_t idx = percent / 10u;
    uint32_t rem = percent % 10u;
    uint32_t lo = table_mv[idx];
    uint32_t hi = table_mv[idx < 10u ? idx + 1u : 10u];
    uint32_t mv = lo + ((hi - lo) * rem) / 10u;
    /* Inverse of battery_adc_voltage(): mv = (raw * 6000) >> 10. */
    return (mv * 1024u + 3000u) / 6000u;
}

static uint8_t dec2bcd(uint32_t v) {
    return (uint8_t)(((v / 10u) << 4) | (v % 10u));
}

static uint32_t bcd2dec(uint8_t v) {
    return ((uint32_t)(v >> 4) * 10u) + (v & 0x0fu);
}

/* PCF50605 RTC (regs 0x0a..0x10, BCD): a base date/time plus elapsed guest
 * seconds, so the Rockbox status-bar clock actually ticks. The default base
 * is 2026-01-01 12:00:00 (Thursday); a guest rtc_write_datetime replaces the
 * base and elapsed time restarts from that write. */
static uint8_t pcf_rtc_read(n1g_state_t *s, uint8_t reg) {
    static const uint8_t days_in_month[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    uint32_t sec = 0, min = 0, hour = 12, wday = 4, mday = 1, mon = 1, year = 26;

    bool guest_set = (s->i2c.pcf_written & (1ull << 0x0a)) != 0;
    uint64_t base_ticks = 0;
    if (guest_set) {
        sec = bcd2dec(s->i2c.pcf_regs[0x0a]);
        min = bcd2dec(s->i2c.pcf_regs[0x0b]);
        hour = bcd2dec(s->i2c.pcf_regs[0x0c]);
        wday = bcd2dec(s->i2c.pcf_regs[0x0d]);
        mday = bcd2dec(s->i2c.pcf_regs[0x0e]);
        mon = bcd2dec(s->i2c.pcf_regs[0x0f]);
        year = bcd2dec(s->i2c.pcf_regs[0x10]);
        base_ticks = s->i2c.rtc_base_ticks;
    }
    if (mon == 0) {
        mon = 1;
    }
    if (mday == 0) {
        mday = 1;
    }

    uint64_t elapsed =
        (s->counters.device_ticks - base_ticks) * s->opts.rtc_usec_per_tick / 1000000ull;
    sec += (uint32_t)(elapsed % 60u);
    min += sec / 60u;
    sec %= 60u;
    min += (uint32_t)((elapsed / 60u) % 60u);
    hour += min / 60u;
    min %= 60u;
    hour += (uint32_t)((elapsed / 3600u) % 24u);
    uint32_t days = (uint32_t)(elapsed / 86400u) + hour / 24u;
    hour %= 24u;
    wday = (wday + days) % 7u;
    for (uint32_t i = 0; i < days; i++) {
        uint32_t dim = days_in_month[(mon - 1u) % 12u];
        if (mon == 2u && ((2000u + year) % 4u) == 0u) {
            dim = 29u;
        }
        if (++mday > dim) {
            mday = 1u;
            if (++mon > 12u) {
                mon = 1u;
                year = (year + 1u) % 100u;
            }
        }
    }

    switch (reg) {
    case 0x0a: return dec2bcd(sec);
    case 0x0b: return dec2bcd(min);
    case 0x0c: return dec2bcd(hour);
    case 0x0d: return dec2bcd(wday % 7u);
    case 0x0e: return dec2bcd(mday);
    case 0x0f: return dec2bcd(mon);
    default:   return dec2bcd(year);
    }
}

static uint8_t pcf_read(n1g_state_t *s) {
    uint8_t reg = s->i2c.pcf_reg_set ? s->i2c.pcf_reg++ : 0;
    s->i2c.pcf_reg_set = true;

    if (reg >= 0x0au && reg <= 0x10u) {
        return pcf_rtc_read(s, reg);
    }

    if (reg < sizeof(s->i2c.pcf_regs) && (s->i2c.pcf_written & (1ull << reg)) != 0) {
        return s->i2c.pcf_regs[reg];
    }

    switch (reg) {
    case 0x00: /* ID */
    case 0x02: /* INT1 */
    case 0x03: /* INT2 */
    case 0x04: /* INT3 */
        return 0;
    case 0x30: { /* ADCS1: upper 8 bits of the 10-bit battery ADC reading. */
        uint32_t raw = battery_percent_to_adc_raw(s->opts.battery_percent);
        return (uint8_t)((raw >> 2) & 0xffu);
    }
    case 0x31: { /* ADCS2: ready flag plus the low 2 bits of the battery ADC reading. */
        uint32_t raw = battery_percent_to_adc_raw(s->opts.battery_percent);
        return (uint8_t)(0x80u | (raw & 0x3u));
    }
    case 0x38: /* GPOC1 default used by Clicky for this PMU family. */
        return 0x04;
    default:
        if (reg < sizeof(s->i2c.pcf_regs)) {
            return s->i2c.pcf_regs[reg];
        }
        return 0;
    }
}

static void pcf_write(n1g_state_t *s, uint8_t value, bool first_byte) {
    if (first_byte) {
        s->i2c.pcf_reg = value;
        s->i2c.pcf_reg_set = true;
        return;
    }
    if (s->i2c.pcf_reg < sizeof(s->i2c.pcf_regs)) {
        s->i2c.pcf_regs[s->i2c.pcf_reg] = value;
        s->i2c.pcf_written |= 1ull << s->i2c.pcf_reg;
        if (s->i2c.pcf_reg >= 0x0au && s->i2c.pcf_reg <= 0x10u) {
            s->i2c.rtc_base_ticks = s->counters.device_ticks;
        }
    }
    s->i2c.pcf_reg++;
}

static void start_txn(n1g_state_t *s) {
    uint8_t addr = (uint8_t)((s->i2c.addr_op >> 1u) & 0x7fu);
    bool read = (s->i2c.addr_op & 1u) != 0u;
    bool ctrl_read = (s->i2c.control & 0x20u) != 0u;
    uint8_t len = (uint8_t)((s->i2c.control >> 1u) & 0x03u);
    uint8_t count = (uint8_t)(len + 1u);

    if (read != ctrl_read) {
        read = ctrl_read;
    }

    if (addr == 0x08u) {
        for (uint8_t i = 0; i < count && i < sizeof(s->i2c.data); i++) {
            if (read) {
                s->i2c.data[i] = pcf_read(s);
            } else {
                pcf_write(s, s->i2c.data[i], i == 0u);
            }
        }
    } else if (read) {
        memset(s->i2c.data, 0, sizeof(s->i2c.data));
    }

    s->i2c.busy_reads = 1;
}

uint32_t n1g_dev_i2c_read(n1g_state_t *s, uint32_t offset, uint32_t size) {
    switch (offset) {
    case 0x00:
        return read_part(s->i2c.control & 0x26u, offset, size);
    case 0x04:
        return read_part(s->i2c.addr_op, offset, size);
    case 0x0c:
        return read_part(data32(s), offset, size);
    case 0x10:
        return read_part(s->i2c.data[1], offset, size);
    case 0x14:
        return read_part(s->i2c.data[2], offset, size);
    case 0x18:
        return read_part(s->i2c.data[3], offset, size);
    case 0x1c:
        if (s->i2c.busy_reads) {
            s->i2c.busy_reads--;
            return 0x40u;
        }
        return 0;
    default:
        if (offset < sizeof(s->i2c.regs)) {
            return s->i2c.regs[offset / 4u];
        }
        return 0;
    }
}

void n1g_dev_i2c_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value) {
    uint8_t v = (uint8_t)value;
    switch (offset) {
    case 0x00:
        s->i2c.control = v;
        if (v & 0x80u) {
            start_txn(s);
        }
        break;
    case 0x04:
        s->i2c.addr_op = v;
        break;
    case 0x0c:
        write_data_bytes(s, 0, size, value);
        break;
    case 0x10:
        write_data_bytes(s, 1, size, value);
        break;
    case 0x14:
        write_data_bytes(s, 2, size, value);
        break;
    case 0x18:
        write_data_bytes(s, 3, size, value);
        break;
    default:
        if (offset < sizeof(s->i2c.regs)) {
            s->i2c.regs[offset / 4u] = value;
        }
        break;
    }
}
