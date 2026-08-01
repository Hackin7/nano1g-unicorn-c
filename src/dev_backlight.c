#include "nano1g/devices.h"

#define PWM_BACKLIGHT_OFFSET 0x10u
#define PWM_ENABLE           0x80000000u

#define DIMMER_CONFIG        0x00u
#define DIMMER_STATUS        0x04u
#define DIMMER_VALUE_BASE    0x20u
#define DIMMER_START         0x40000000u
#define DIMMER_BUSY          0x80000000u
#define DIMMER_CHANNEL       8u
#define DIMMER_UP_WIDTH      127u
#define DIMMER_DOWN_WIDTH    1u

#define GPIOL_OUTPUT_VAL     0x12cu
#define GPIOL_BACKLIGHT      0x80u

static uint32_t mask_for_size(uint32_t size) {
    if (size == 1u) return 0xffu;
    if (size == 2u) return 0xffffu;
    return 0xffffffffu;
}

static uint32_t read_part(uint32_t value, uint32_t offset, uint32_t size) {
    return (value >> ((offset & 3u) * 8u)) & mask_for_size(size);
}

static uint32_t merge_write(uint32_t old_value,
                            uint32_t offset,
                            uint32_t size,
                            uint32_t value) {
    uint32_t shift = (offset & 3u) * 8u;
    uint32_t mask = mask_for_size(size) << shift;
    return (old_value & ~mask) | ((value << shift) & mask);
}

uint32_t n1g_dev_pwm_read(n1g_state_t *s, uint32_t offset, uint32_t size) {
    uint32_t aligned = offset & ~3u;
    if (aligned >= sizeof(s->backlight.pwm_regs)) {
        return 0u;
    }
    return read_part(s->backlight.pwm_regs[aligned / 4u], offset, size);
}

void n1g_dev_pwm_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value) {
    uint32_t aligned = offset & ~3u;
    if (aligned >= sizeof(s->backlight.pwm_regs)) {
        return;
    }
    uint32_t *reg = &s->backlight.pwm_regs[aligned / 4u];
    uint32_t old_value = *reg;
    *reg = merge_write(*reg, offset, size, value);
    if (aligned == PWM_BACKLIGHT_OFFSET) {
        s->backlight.pwm_seen = true;
        if (old_value != *reg) {
            s->backlight.generation++;
        }
    }
}

static void dimmer_start(n1g_state_t *s, uint32_t config) {
    uint32_t channel = config & 0xffu;
    uint32_t value_offset = DIMMER_VALUE_BASE + 4u * (channel / 8u);
    uint32_t width = value_offset < sizeof(s->backlight.dimmer_regs)
                         ? s->backlight.dimmer_regs[value_offset / 4u]
                         : 0u;

    if (!s->backlight.dimmer_seen) {
        s->backlight.nano_level = 16u;
    }
    s->backlight.dimmer_seen = true;
    s->backlight.dimmer_pulses++;
    if (channel == DIMMER_CHANNEL && width == DIMMER_UP_WIDTH) {
        if (s->backlight.nano_level < 32u) {
            s->backlight.nano_level++;
        }
        s->backlight.dimmer_up_pulses++;
    } else if (channel == DIMMER_CHANNEL && width == DIMMER_DOWN_WIDTH) {
        if (s->backlight.nano_level > 0u) {
            s->backlight.nano_level--;
        }
        s->backlight.dimmer_down_pulses++;
    }
    s->backlight.dimmer_regs[DIMMER_STATUS / 4u] |= DIMMER_BUSY;
    s->backlight.dimmer_busy_reads = 1u;
    s->backlight.generation++;
}

uint32_t n1g_dev_dimmer_read(n1g_state_t *s, uint32_t offset, uint32_t size) {
    uint32_t aligned = offset & ~3u;
    if (aligned >= sizeof(s->backlight.dimmer_regs)) {
        return 0u;
    }
    uint32_t value = s->backlight.dimmer_regs[aligned / 4u];
    if (aligned == DIMMER_STATUS && s->backlight.dimmer_busy_reads != 0u) {
        s->backlight.dimmer_busy_reads--;
        if (s->backlight.dimmer_busy_reads == 0u) {
            s->backlight.dimmer_regs[DIMMER_STATUS / 4u] &= ~DIMMER_BUSY;
        }
    }
    return read_part(value, offset, size);
}

void n1g_dev_dimmer_write(n1g_state_t *s,
                          uint32_t offset,
                          uint32_t size,
                          uint32_t value) {
    uint32_t aligned = offset & ~3u;
    if (aligned >= sizeof(s->backlight.dimmer_regs)) {
        return;
    }
    if (aligned == DIMMER_STATUS) {
        s->backlight.dimmer_regs[DIMMER_STATUS / 4u] &= ~DIMMER_BUSY;
        s->backlight.dimmer_busy_reads = 0u;
        return;
    }

    uint32_t *reg = &s->backlight.dimmer_regs[aligned / 4u];
    uint32_t old_value = *reg;
    *reg = merge_write(*reg, offset, size, value);
    if (aligned == DIMMER_CONFIG &&
        (old_value & DIMMER_START) == 0u && (*reg & DIMMER_START) != 0u) {
        dimmer_start(s, *reg);
    }
}

void n1g_dev_backlight_gpio_write(n1g_state_t *s,
                                  uint32_t gpio_offset,
                                  uint32_t write_mask) {
    if (gpio_offset != GPIOL_OUTPUT_VAL || (write_mask & GPIOL_BACKLIGHT) == 0u) {
        return;
    }
    bool powered = (s->gpio.regs[GPIOL_OUTPUT_VAL / 4u] & GPIOL_BACKLIGHT) != 0u;
    if (!s->backlight.nano_power_seen || powered != s->backlight.nano_powered) {
        s->backlight.generation++;
    }
    s->backlight.nano_power_seen = true;
    s->backlight.nano_powered = powered;
}

const char *n1g_dev_backlight_mode(const n1g_state_t *s) {
    if (s->backlight.dimmer_seen || s->backlight.nano_power_seen) {
        return "nano";
    }
    if (s->backlight.pwm_seen) {
        return "pwm";
    }
    return "default";
}

bool n1g_dev_backlight_powered(const n1g_state_t *s) {
    if (s->backlight.dimmer_seen || s->backlight.nano_power_seen) {
        return s->backlight.nano_power_seen && s->backlight.nano_powered;
    }
    if (s->backlight.pwm_seen) {
        uint32_t value = s->backlight.pwm_regs[PWM_BACKLIGHT_OFFSET / 4u];
        return (value & PWM_ENABLE) != 0u && ((value >> 16u) & 0xffu) != 0u;
    }
    return true;
}

uint32_t n1g_dev_backlight_level(const n1g_state_t *s) {
    if (s->backlight.dimmer_seen) {
        return s->backlight.nano_level;
    }
    if (s->backlight.pwm_seen) {
        uint32_t duty = (s->backlight.pwm_regs[PWM_BACKLIGHT_OFFSET / 4u] >> 16u) & 0xffu;
        return duty <= 32u ? duty : (duty * 32u + 127u) / 255u;
    }
    return 32u;
}

uint32_t n1g_dev_backlight_intensity(const n1g_state_t *s) {
    if (!n1g_dev_backlight_powered(s)) {
        return 0u;
    }
    uint32_t level = n1g_dev_backlight_level(s);
    if (level > 32u) level = 32u;
    return level == 0u ? 0u : 48u + (level * 207u) / 32u;
}
