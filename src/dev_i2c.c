#include "nano1g/devices.h"

#include "nano1g/trace.h"

#include <string.h>

#define WM8975_I2C_ADDR 0x1au
#define WM8975_REG_COUNT 0x2bu
#define WM8975_RESET_REG 0x0fu

#define WM8975_DAPCTRL 0x05u
#define WM8975_DACMU (1u << 3u)
#define WM8975_LEGACY_APATH 0x04u
#define WM8975_LEGACY_DACSEL (1u << 4u)
#define WM8975_LEGACY_PWR 0x06u
#define WM8975_LEGACY_DACPD (1u << 3u)
#define WM8975_LEGACY_OUTPD (1u << 4u)
#define WM8975_LEGACY_POWEROFF (1u << 7u)
#define WM8975_LEGACY_ACTIVE 0x09u
#define WM8975_PWRMGMT2 0x1au
#define WM8975_PWRMGMT2_OUT1 (3u << 5u)
#define WM8975_PWRMGMT2_DACS (3u << 7u)
#define WM8975_LOUTMIX1 0x22u
#define WM8975_LOUTMIX1_LD2LO (1u << 8u)
#define WM8975_ROUTMIX2 0x25u
#define WM8975_ROUTMIX2_RD2RO (1u << 8u)

#define PCF50605_OOCC1 0x08u
#define PCF50605_OOCC1_GOSTDBY 0x01u
#define PCF50605_OOCC1_RTCWAK 0x10u
#define PCF50605_OOCC1_CHGWAK 0x20u
#define PCF50605_OOCS 0x01u
#define PCF50605_INT1 0x02u
#define PCF50605_INT2 0x03u
#define PCF50605_INT3 0x04u
#define PCF50605_INT1M 0x05u
#define PCF50605_INT2M 0x06u
#define PCF50605_INT1_SECOND 0x40u
#define PCF50605_INT1_ALARM 0x80u
#define PCF50605_INT1_ONKEYF 0x02u
#define PCF50605_INT2_CHGINS 0x01u
#define PCF50605_INT2_CHGRM 0x02u
#define PCF50605_INT3_ADCRDY 0x01u
#define PCF50605_INT3_LOWBAT 0x40u
#define PCF50605_RTCSC 0x0au
#define PCF50605_RTCYR 0x10u
#define PCF50605_RTCSCA 0x11u
#define PCF50605_RTCYRA 0x17u
#define PCF50605_ADCC1 0x2eu
#define PCF50605_ADCC2 0x2fu
#define PCF50605_ADCC2_START 0x01u
#define PCF50605_ADCC2_SYNC 0x60u
#define PCF50605_ADCC2_RES8 0x80u
#define PCF50605_ADCS1 0x30u
#define PCF50605_ADCS2 0x31u
#define PCF50605_ADCS3 0x32u
#define PCF50605_BVMC 0x34u
#define PCF50605_BVMC_LOWBAT 0x01u
#define PCF50605_BVMC_THRESHOLD 0x0eu
#define PCF50605_BVMC_DISABLE_DEBOUNCE 0x10u
#define PCF50605_RTC_WRITTEN_MASK (0x7full << PCF50605_RTCSC)

static const uint8_t pcf_alarm_reset[7] = {
    0x7fu, 0x7fu, 0x3fu, 0x07u, 0x3fu, 0x1fu, 0xffu
};

static const uint8_t pcf_alarm_mask[7] = {
    0x7fu, 0x7fu, 0x3fu, 0x07u, 0x3fu, 0x1fu, 0xffu
};

static uint8_t pcf_oocc1(const n1g_state_t *s) {
    return (s->i2c.pcf_written & (1ull << PCF50605_OOCC1)) != 0u
               ? s->i2c.pcf_regs[PCF50605_OOCC1]
               : 0x60u;
}

static void pcf_wake(n1g_state_t *s, n1g_pcf_wake_reason_t reason) {
    if (!s->i2c.pcf_standby) {
        return;
    }
    s->i2c.pcf_standby = false;
    s->i2c.pcf_standby_deadline = 0u;
    s->i2c.pcf_wake_requests++;
    s->i2c.pcf_last_wake = reason;
}

static uint64_t pcf_ticks_for_usec(const n1g_state_t *s, uint64_t usec) {
    uint64_t scale = s->opts.rtc_usec_per_tick != 0u
                         ? s->opts.rtc_usec_per_tick
                         : 1u;
    uint64_t ticks = (usec + scale - 1u) / scale;
    return ticks != 0u ? ticks : 1u;
}

static void pcf_enter_standby(n1g_state_t *s) {
    if (s->i2c.pcf_standby) {
        return;
    }
    s->i2c.pcf_standby = true;
    s->i2c.pcf_standby_deadline = 0u;
    s->i2c.pcf_standby_transitions++;
    s->i2c.pcf_regs[PCF50605_OOCC1] &= (uint8_t)~PCF50605_OOCC1_GOSTDBY;
    s->i2c.pcf_regs[PCF50605_ADCC1] = 0u;
    s->i2c.pcf_regs[PCF50605_ADCC2] = 0u;
    s->i2c.pcf_written &= ~((1ull << PCF50605_ADCC1) |
                            (1ull << PCF50605_ADCC2));
    s->i2c.pcf_adc_deadline = 0u;
    s->i2c.pcf_adc_ready = false;
    s->cpu[N1G_CORE_CPU].halted = true;
    s->cpu[N1G_CORE_COP].halted = true;
}

static int32_t wm8975_volume_q15(uint16_t value) {
    uint32_t level = value & 0x7fu;
    if (level < 0x30u) {
        return 0;
    }
    int db = (int)level - 0x79;
    int64_t gain = 32768;
    while (db < 0) {
        gain = (gain * 29205 + 16384) >> 15;
        db++;
    }
    while (db > 0) {
        gain = (gain * 36766 + 16384) >> 15;
        db--;
    }
    return (int32_t)gain;
}

static uint32_t wm8975_sample_rate(uint16_t value, bool legacy) {
    uint8_t setting = (uint8_t)value;
    if (legacy && setting == 0x23u) return 44100u;
    switch (setting) {
    case 0x4d: return 8000u;
    case 0x61: return 12000u;
    case 0x55: return 16000u;
    case 0x77: return 22050u;
    case 0x79: return 24000u;
    case 0x59: return 32000u;
    case 0x63: return 44100u;
    case 0x41: return 48000u;
    case 0x7f: return 88200u;
    case 0x5d: return 96000u;
    default: return 44100u;
    }
}

static void wm8975_refresh(n1g_state_t *s) {
    bool legacy = (s->i2c.wm8975_written & (1ull << WM8975_LEGACY_ACTIVE)) != 0u;
    uint16_t left = s->i2c.wm8975_regs[0x02u];
    uint16_t right = s->i2c.wm8975_regs[0x03u];
    if ((s->i2c.wm8975_written & (1ull << 0x03u)) == 0u && (left & (1u << 8u)) != 0u) {
        right = left;
    }
    s->i2c.wm8975_legacy_mode = legacy;
    s->i2c.wm8975_gain_q15[0] = wm8975_volume_q15(left);
    s->i2c.wm8975_gain_q15[1] = wm8975_volume_q15(right);
    s->i2c.wm8975_sample_rate = wm8975_sample_rate(s->i2c.wm8975_regs[0x08u], legacy);

    bool muted = (s->i2c.wm8975_regs[WM8975_DAPCTRL] & WM8975_DACMU) != 0u;
    if (legacy) {
        uint16_t power = s->i2c.wm8975_regs[WM8975_LEGACY_PWR];
        bool active = (s->i2c.wm8975_regs[WM8975_LEGACY_ACTIVE] & 1u) != 0u;
        bool powered = (power & (WM8975_LEGACY_DACPD | WM8975_LEGACY_OUTPD |
                                WM8975_LEGACY_POWEROFF)) == 0u;
        bool routed = (s->i2c.wm8975_regs[WM8975_LEGACY_APATH] &
                       WM8975_LEGACY_DACSEL) != 0u;
        s->i2c.wm8975_output_enabled = active && powered && routed && !muted;
    } else {
        uint16_t power = s->i2c.wm8975_regs[WM8975_PWRMGMT2];
        bool powered = (power & WM8975_PWRMGMT2_DACS) == WM8975_PWRMGMT2_DACS &&
                       (power & WM8975_PWRMGMT2_OUT1) == WM8975_PWRMGMT2_OUT1;
        bool routed = (s->i2c.wm8975_regs[WM8975_LOUTMIX1] &
                       WM8975_LOUTMIX1_LD2LO) != 0u &&
                      (s->i2c.wm8975_regs[WM8975_ROUTMIX2] &
                       WM8975_ROUTMIX2_RD2RO) != 0u;
        s->i2c.wm8975_output_enabled = powered && routed && !muted;
    }
}

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
static uint32_t battery_percent_to_mv(uint32_t percent) {
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
    return lo + ((hi - lo) * rem) / 10u;
}

uint32_t n1g_dev_i2c_battery_mv(const n1g_state_t *s) {
    return battery_percent_to_mv(s->opts.battery_percent);
}

static uint32_t battery_percent_to_adc_raw(uint32_t percent) {
    uint32_t mv = battery_percent_to_mv(percent);
    /* Inverse of battery_adc_voltage(): mv = (raw * 6000) >> 10. */
    return (mv * 1024u + 3000u) / 6000u;
}

static uint8_t pcf_bvm_control(const n1g_state_t *s) {
    /* The Nano's 3.23 V empty point sits below its 3.33 V disk-safe point;
     * use the 3.3 V variant threshold until a board-specific PMU ID dump is
     * available. Guest programming still overrides every writable bit.
     */
    return (s->i2c.pcf_written & (1ull << PCF50605_BVMC)) != 0u
               ? s->i2c.pcf_regs[PCF50605_BVMC] & 0x1eu
               : 0x0cu;
}

static uint32_t pcf_bvm_threshold_mv(const n1g_state_t *s) {
    uint8_t control = pcf_bvm_control(s);
    uint32_t code = (control & PCF50605_BVMC_THRESHOLD) >> 1u;
    return code == 0u ? 2700u : 2700u + code * 100u;
}

bool n1g_dev_i2c_idle_quiescent(const n1g_state_t *s) {
    uint32_t battery_mv = n1g_dev_i2c_battery_mv(s);
    uint32_t threshold_mv = pcf_bvm_threshold_mv(s);
    bool below = battery_mv < threshold_mv;

    if (!s->i2c.pcf_low_battery && below &&
        s->i2c.pcf_low_battery_deadline == 0u) {
        return false;
    }
    if (s->i2c.pcf_low_battery &&
        battery_mv >= threshold_mv + threshold_mv * 4u / 100u) {
        return false;
    }
    if (!below && s->i2c.pcf_low_battery_deadline != 0u) {
        return false;
    }
    return true;
}

static uint16_t pcf_adc_input(const n1g_state_t *s, uint8_t mux) {
    uint32_t mv = n1g_dev_i2c_battery_mv(s);
    uint32_t raw;
    switch (mux) {
    case 0u:
    case 2u:
        raw = battery_percent_to_adc_raw(s->opts.battery_percent);
        break;
    case 1u:
    case 3u:
    case 12u:
        raw = mv > 3000u ? ((mv - 3000u) * 1024u + 1200u) / 2400u : 0u;
        break;
    case 4u:
        raw = 512u;
        break;
    default:
        raw = 0u;
        break;
    }
    return (uint16_t)(raw < 1024u ? raw : 1023u);
}

static void pcf_complete_adc(n1g_state_t *s) {
    uint8_t control = s->i2c.pcf_regs[PCF50605_ADCC2];
    uint8_t mux = (control >> 1u) & 0x0fu;
    s->i2c.pcf_adc_result1 = pcf_adc_input(s, mux);
    s->i2c.pcf_adc_result2 = mux == 12u ? pcf_adc_input(s, 3u) : 0u;
    if ((control & PCF50605_ADCC2_RES8) != 0u) {
        s->i2c.pcf_adc_result1 &= 0x03fcu;
        s->i2c.pcf_adc_result2 &= 0x03fcu;
    }
    s->i2c.pcf_adc_ready = true;
    s->i2c.pcf_adc_deadline = 0u;
    s->i2c.pcf_adc_conversions++;
    s->i2c.pcf_regs[PCF50605_INT3] |= PCF50605_INT3_ADCRDY;
}

static uint8_t dec2bcd(uint32_t v) {
    return (uint8_t)(((v / 10u) << 4) | (v % 10u));
}

static uint32_t bcd2dec(uint8_t v) {
    return ((uint32_t)(v >> 4) * 10u) + (v & 0x0fu);
}

static void pcf_rtc_advance_fields(uint8_t fields[7], uint64_t elapsed) {
    static const uint8_t days_in_month[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    uint64_t sec = bcd2dec(fields[0]);
    uint64_t min = bcd2dec(fields[1]);
    uint64_t hour = bcd2dec(fields[2]);
    uint32_t wday = bcd2dec(fields[3]);
    uint32_t mday = bcd2dec(fields[4]);
    uint32_t mon = bcd2dec(fields[5]);
    uint32_t year = bcd2dec(fields[6]);

    if (mon == 0) {
        mon = 1;
    }
    if (mday == 0) {
        mday = 1;
    }

    sec += elapsed % 60u;
    min += sec / 60u;
    sec %= 60u;
    min += (elapsed / 60u) % 60u;
    hour += min / 60u;
    min %= 60u;
    hour += (elapsed / 3600u) % 24u;
    uint64_t days = elapsed / 86400u + hour / 24u;
    hour %= 24u;
    wday = (uint32_t)((wday + days) % 7u);
    for (uint64_t i = 0; i < days; i++) {
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

    fields[0] = dec2bcd(sec);
    fields[1] = dec2bcd(min);
    fields[2] = dec2bcd(hour);
    fields[3] = dec2bcd(wday % 7u);
    fields[4] = dec2bcd(mday);
    fields[5] = dec2bcd(mon);
    fields[6] = dec2bcd(year);
}

/* PCF50605 RTC (regs 0x0a..0x10, BCD): a base date/time plus elapsed guest
 * seconds, so the Rockbox status-bar clock actually ticks. The default base
 * is 2026-01-01 12:00:00 (Thursday); a guest rtc_write_datetime replaces the
 * base and elapsed time restarts from that write. */
static void pcf_rtc_fields(const n1g_state_t *s, uint8_t fields[7]) {
    static const uint8_t default_rtc[7] = {
        0x00u, 0x00u, 0x12u, 0x04u, 0x01u, 0x01u, 0x26u
    };
    bool guest_set = (s->i2c.pcf_written & (1ull << PCF50605_RTCSC)) != 0u;
    uint64_t base_ticks = 0u;
    if (guest_set) {
        memcpy(fields, &s->i2c.pcf_regs[PCF50605_RTCSC], 7u);
        base_ticks = s->i2c.rtc_base_ticks;
    } else {
        memcpy(fields, default_rtc, sizeof(default_rtc));
    }
    uint64_t elapsed =
        (s->counters.device_ticks - base_ticks) * s->opts.rtc_usec_per_tick / 1000000ull;
    pcf_rtc_advance_fields(fields, elapsed);
}

static uint8_t pcf_rtc_read(const n1g_state_t *s, uint8_t reg) {
    uint8_t fields[7];
    pcf_rtc_fields(s, fields);
    return fields[reg - PCF50605_RTCSC];
}

void n1g_dev_i2c_get_rtc(const n1g_state_t *s, uint8_t fields[7]) {
    pcf_rtc_fields(s, fields);
}

void n1g_dev_i2c_advance_rtc(n1g_state_t *s, uint64_t seconds) {
    uint8_t fields[7];
    pcf_rtc_fields(s, fields);
    pcf_rtc_advance_fields(fields, seconds);
    memcpy(&s->i2c.pcf_regs[PCF50605_RTCSC], fields, sizeof(fields));
    s->i2c.pcf_written |= PCF50605_RTC_WRITTEN_MASK;
    s->i2c.rtc_base_ticks = s->counters.device_ticks;
    s->i2c.rtc_last_second = 0u;
    s->i2c.rtc_alarm_match_active = false;
}

void n1g_dev_i2c_save_pcf(const n1g_state_t *s, n1g_pcf_backup_t *backup) {
    memset(backup, 0, sizeof(*backup));
    memcpy(backup->regs, s->i2c.pcf_regs, sizeof(backup->regs));
    pcf_rtc_fields(s, &backup->regs[PCF50605_RTCSC]);
    backup->written = s->i2c.pcf_written | PCF50605_RTC_WRITTEN_MASK;
    backup->alarm_match_active = s->i2c.rtc_alarm_match_active;
    backup->valid = true;
}

void n1g_dev_i2c_restore_pcf(n1g_state_t *s, const n1g_pcf_backup_t *backup) {
    if (!backup || !backup->valid) {
        return;
    }
    memcpy(s->i2c.pcf_regs, backup->regs, sizeof(s->i2c.pcf_regs));
    s->i2c.pcf_written = backup->written;
    s->i2c.rtc_base_ticks = s->counters.device_ticks;
    s->i2c.rtc_last_second = 0u;
    s->i2c.rtc_alarm_match_active = backup->alarm_match_active;
}

static uint8_t pcf_read(n1g_state_t *s) {
    uint8_t reg = s->i2c.pcf_reg_set ? s->i2c.pcf_reg++ : 0;
    s->i2c.pcf_reg_set = true;
    if (reg < sizeof(s->i2c.pcf_reg_reads) / sizeof(s->i2c.pcf_reg_reads[0])) {
        s->i2c.pcf_reg_reads[reg]++;
    }

    if (reg >= PCF50605_RTCSC && reg <= PCF50605_RTCYR) {
        return pcf_rtc_read(s, reg);
    }

    if (reg >= PCF50605_INT1 && reg <= PCF50605_INT3) {
        uint8_t value = s->i2c.pcf_regs[reg];
        s->i2c.pcf_regs[reg] = 0u;
        return value;
    }

    if (reg == PCF50605_OOCS) {
        uint8_t value = s->i2c.pcf_regs[PCF50605_OOCS] & 0x84u;
        value |= 0x50u;
        if (!s->i2c.pcf_low_battery) {
            value |= 0x08u;
        }
        if (s->opts.main_charger_connected || s->opts.usb_charger_connected) {
            value |= 0x20u;
        }
        return value;
    }

    if (reg == PCF50605_OOCC1 &&
        (s->i2c.pcf_written & (1ull << PCF50605_OOCC1)) == 0u) {
        return 0x60u;
    }

    if (reg == PCF50605_BVMC) {
        uint8_t value = pcf_bvm_control(s);
        return value | (s->i2c.pcf_low_battery ? PCF50605_BVMC_LOWBAT : 0u);
    }

    if (reg == PCF50605_ADCS1) {
        return (uint8_t)(s->i2c.pcf_adc_result1 >> 2u);
    }
    if (reg == PCF50605_ADCS2) {
        return (uint8_t)((s->i2c.pcf_adc_ready ? 0x80u : 0u) |
                         ((s->i2c.pcf_adc_result2 & 0x03u) << 2u) |
                         (s->i2c.pcf_adc_result1 & 0x03u));
    }
    if (reg == PCF50605_ADCS3) {
        return (uint8_t)(s->i2c.pcf_adc_result2 >> 2u);
    }

    if (reg < sizeof(s->i2c.pcf_regs) && (s->i2c.pcf_written & (1ull << reg)) != 0) {
        return s->i2c.pcf_regs[reg];
    }

    if (reg >= PCF50605_RTCSCA && reg <= PCF50605_RTCYRA) {
        return pcf_alarm_reset[reg - PCF50605_RTCSCA];
    }

    switch (reg) {
    case 0x00: /* ID */
        return 0;
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
        uint8_t reg = s->i2c.pcf_reg;
        s->i2c.pcf_reg_writes[reg]++;
        if (reg < PCF50605_INT1 || reg > PCF50605_INT3) {
            if (reg == PCF50605_OOCS) {
                s->i2c.pcf_regs[reg] = value & 0x04u;
            } else if (reg == PCF50605_BVMC) {
                s->i2c.pcf_regs[reg] = value & 0x1eu;
            } else {
                s->i2c.pcf_regs[reg] = value;
            }
            s->i2c.pcf_written |= 1ull << reg;
            if (reg == PCF50605_OOCC1 && (value & PCF50605_OOCC1_GOSTDBY) != 0u) {
                s->i2c.pcf_standby_requests++;
                if (!s->i2c.pcf_standby && s->i2c.pcf_standby_deadline == 0u) {
                    s->i2c.pcf_standby_deadline =
                        s->counters.device_ticks + pcf_ticks_for_usec(s, 1000u);
                }
            }
            if (reg == PCF50605_ADCC2 &&
                (value & PCF50605_ADCC2_START) != 0u) {
                s->i2c.pcf_regs[reg] &= (uint8_t)~PCF50605_ADCC2_START;
                s->i2c.pcf_adc_ready = false;
                s->i2c.pcf_adc_deadline = 0u;
                if ((value & PCF50605_ADCC2_SYNC) == 0u) {
                    s->i2c.pcf_adc_deadline =
                        s->counters.device_ticks + pcf_ticks_for_usec(s, 25u);
                }
            }
            if (reg >= PCF50605_RTCSC && reg <= PCF50605_RTCYR) {
                s->i2c.rtc_base_ticks = s->counters.device_ticks;
                s->i2c.rtc_last_second = 0u;
            }
            if (reg >= PCF50605_RTCSCA && reg <= PCF50605_RTCYRA) {
                s->i2c.rtc_alarm_match_active = false;
            }
        }
    }
    s->i2c.pcf_reg++;
}

void n1g_dev_i2c_tick(n1g_state_t *s) {
    if (!s->i2c.pcf_standby && s->i2c.pcf_standby_deadline != 0u &&
        s->counters.device_ticks >= s->i2c.pcf_standby_deadline) {
        pcf_enter_standby(s);
    }

    if (s->i2c.pcf_adc_deadline != 0u &&
        s->counters.device_ticks >= s->i2c.pcf_adc_deadline) {
        pcf_complete_adc(s);
    }

    uint32_t battery_mv = n1g_dev_i2c_battery_mv(s);
    uint32_t threshold_mv = pcf_bvm_threshold_mv(s);
    bool below = battery_mv < threshold_mv;
    if (!s->i2c.pcf_low_battery && below) {
        if (s->i2c.pcf_low_battery_deadline == 0u) {
            uint64_t debounce =
                (pcf_bvm_control(s) & PCF50605_BVMC_DISABLE_DEBOUNCE) != 0u
                    ? 1u
                    : pcf_ticks_for_usec(s, 62000u);
            s->i2c.pcf_low_battery_deadline =
                s->counters.device_ticks + debounce;
        } else if (s->counters.device_ticks >= s->i2c.pcf_low_battery_deadline) {
            s->i2c.pcf_low_battery = true;
            s->i2c.pcf_low_battery_deadline = 0u;
            s->i2c.pcf_low_battery_events++;
            s->i2c.pcf_regs[PCF50605_INT3] |= PCF50605_INT3_LOWBAT;
            s->i2c.pcf_low_battery_standby_deadline =
                s->counters.device_ticks + pcf_ticks_for_usec(s, 8000000u);
        }
    } else if (s->i2c.pcf_low_battery &&
               battery_mv >= threshold_mv + threshold_mv * 4u / 100u) {
        s->i2c.pcf_low_battery = false;
        s->i2c.pcf_low_battery_deadline = 0u;
        s->i2c.pcf_low_battery_standby_deadline = 0u;
    } else if (!below) {
        s->i2c.pcf_low_battery_deadline = 0u;
    }

    if (!s->i2c.pcf_standby &&
        s->i2c.pcf_low_battery_standby_deadline != 0u &&
        s->counters.device_ticks >= s->i2c.pcf_low_battery_standby_deadline) {
        s->i2c.pcf_low_battery_standby_deadline = 0u;
        pcf_enter_standby(s);
    }

    uint64_t elapsed_ticks = s->counters.device_ticks - s->i2c.rtc_base_ticks;
    uint64_t elapsed_seconds =
        elapsed_ticks * s->opts.rtc_usec_per_tick / 1000000ull;
    if (elapsed_seconds == s->i2c.rtc_last_second) {
        return;
    }

    s->i2c.rtc_last_second = elapsed_seconds;
    s->i2c.pcf_regs[PCF50605_INT1] |= PCF50605_INT1_SECOND;
    s->i2c.rtc_second_interrupts++;

    uint8_t current[7];
    pcf_rtc_fields(s, current);
    bool any_enabled = false;
    bool all_match = true;
    for (uint8_t i = 0; i < 7u; i++) {
        uint8_t reg = (uint8_t)(PCF50605_RTCSCA + i);
        uint8_t alarm = (s->i2c.pcf_written & (1ull << reg)) != 0u
                            ? s->i2c.pcf_regs[reg]
                            : pcf_alarm_reset[i];
        alarm &= pcf_alarm_mask[i];
        if (alarm == pcf_alarm_reset[i]) {
            continue;
        }
        any_enabled = true;
        if (alarm != (current[i] & pcf_alarm_mask[i])) {
            all_match = false;
        }
    }

    bool match = any_enabled && all_match;
    if (match && !s->i2c.rtc_alarm_match_active) {
        s->i2c.pcf_regs[PCF50605_INT1] |= PCF50605_INT1_ALARM;
        s->i2c.rtc_alarm_interrupts++;
        if ((pcf_oocc1(s) & PCF50605_OOCC1_RTCWAK) != 0u &&
            (s->i2c.pcf_regs[PCF50605_INT1M] & PCF50605_INT1_ALARM) == 0u) {
            pcf_wake(s, N1G_PCF_WAKE_RTC_ALARM);
        }
    }
    s->i2c.rtc_alarm_match_active = match;
}

void n1g_dev_i2c_onkey(n1g_state_t *s, bool pressed) {
    if (!pressed) {
        return;
    }
    s->i2c.pcf_regs[PCF50605_INT1] |= PCF50605_INT1_ONKEYF;
    pcf_wake(s, N1G_PCF_WAKE_ONKEY);
}

void n1g_dev_i2c_charger_event(n1g_state_t *s, bool inserted) {
    uint8_t event = inserted ? PCF50605_INT2_CHGINS : PCF50605_INT2_CHGRM;
    s->i2c.pcf_regs[PCF50605_INT2] |= event;
    if (inserted && (pcf_oocc1(s) & PCF50605_OOCC1_CHGWAK) != 0u &&
        (s->i2c.pcf_regs[PCF50605_INT2M] & PCF50605_INT2_CHGINS) == 0u) {
        pcf_wake(s, N1G_PCF_WAKE_CHARGER);
    }
}

static void wm8975_write(n1g_state_t *s, uint8_t count) {
    if (count < 2u) {
        return;
    }
    uint8_t first = s->i2c.data[0];
    uint8_t reg = first >> 1u;
    uint16_t value = (uint16_t)(((uint16_t)(first & 1u) << 8u) | s->i2c.data[1]);
    if (reg >= WM8975_REG_COUNT) {
        return;
    }
    if (reg == WM8975_RESET_REG) {
        memset(s->i2c.wm8975_regs, 0, sizeof(s->i2c.wm8975_regs));
        s->i2c.wm8975_written = 0;
        s->i2c.wm8975_resets++;
        wm8975_refresh(s);
        return;
    }
    s->i2c.wm8975_regs[reg] = value;
    s->i2c.wm8975_written |= 1ull << reg;
    wm8975_refresh(s);
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
    } else if (addr == WM8975_I2C_ADDR && !read) {
        wm8975_write(s, count);
    } else if (read) {
        memset(s->i2c.data, 0, sizeof(s->i2c.data));
    }

    s->i2c.transactions++;
    if (read) {
        s->i2c.addr_reads[addr]++;
    } else {
        s->i2c.addr_writes[addr]++;
    }
    s->i2c.last_addr = addr;
    s->i2c.last_count = count;
    s->i2c.last_read = read;
    s->i2c.last_data = data32(s);
    uint64_t addr_transactions = s->i2c.addr_reads[addr] + s->i2c.addr_writes[addr];
    if (s->opts.verbose && addr_transactions <= 32u) {
        n1g_log(s,
                "i2c txn addr=0x%02x op=%s count=%u data=0x%08x addr_txns=%llu",
                addr,
                read ? "read" : "write",
                count,
                s->i2c.last_data,
                (unsigned long long)addr_transactions);
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
