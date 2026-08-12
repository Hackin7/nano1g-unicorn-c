#include "nano1g/devices.h"

#include <stdio.h>
#include <string.h>

#include "nano1g/trace.h"

#define OPTO_READY_BIT 0x04000000u
#define OPTO_PENDING_BITS 0x0c000000u
#define OPTO_PACKET_BASE 0x8000001au
#define OPTO_WHEEL_ACTIVE 0x40000000u
#define OPTO_HI_IRQ_BIT (1u << 8u)

static uint32_t mask_for_size(uint32_t size) {
    if (size == 1u) return 0xffu;
    if (size == 2u) return 0xffffu;
    return 0xffffffffu;
}

static uint32_t read_part(uint32_t value, uint32_t offset, uint32_t size) {
    uint32_t shift = (offset & 3u) * 8u;
    return (value >> shift) & mask_for_size(size);
}

static uint32_t button_bit(const char *button) {
    if (strcmp(button, "select") == 0) return 0x0100u;
    if (strcmp(button, "right") == 0 || strcmp(button, "next") == 0) return 0x0200u;
    if (strcmp(button, "left") == 0 || strcmp(button, "prev") == 0) return 0x0400u;
    if (strcmp(button, "play") == 0 || strcmp(button, "down") == 0) return 0x0800u;
    if (strcmp(button, "menu") == 0) return 0x1000u;
    return 0;
}

static void set_last_input(n1g_state_t *s, const char *kind, const char *name) {
    (void)snprintf(s->opto.last_input,
                   sizeof(s->opto.last_input),
                   "%s:%s",
                   kind,
                   name);
}

static bool suppress_for_hold(n1g_state_t *s, const char *name) {
    if (!s->opts.hold_switch_engaged) {
        return false;
    }
    s->opto.suppressed_events++;
    set_last_input(s, "held", name);
    return true;
}

static void enqueue_status(n1g_state_t *s, uint32_t status) {
    if (s->opto.queue_len > 0u) {
        uint8_t last = (uint8_t)((s->opto.queue_head + s->opto.queue_len - 1u) & 7u);
        if (s->opto.queue[last] == status) {
            return;
        }
    }
    if (s->opto.queue_len >= 8u) {
        s->opto.queue_head = (uint8_t)((s->opto.queue_head + 1u) & 7u);
        s->opto.queue_len--;
    }
    s->opto.queue[(s->opto.queue_head + s->opto.queue_len) & 7u] = status;
    s->opto.queue_len++;
    s->intc.hi_cpu_status |= OPTO_HI_IRQ_BIT;
    s->intc.hi_cop_status |= OPTO_HI_IRQ_BIT;
    s->opto.input_events++;
    if (s->opts.verbose && s->opto.input_events <= 128u) {
        n1g_log(s,
                "apple opto enqueue status=0x%08x pending=%u hi_cpu=0x%08x hi_en=0x%08x",
                status,
                s->opto.queue_len,
                s->intc.hi_cpu_status,
                s->intc.hi_cpu_enable);
    }
}

static void clear_status(n1g_state_t *s) {
    if (s->opto.queue_len > 0u) {
        s->opto.queue_head = (uint8_t)((s->opto.queue_head + 1u) & 7u);
        s->opto.queue_len--;
    }
    if (s->opto.queue_len == 0u) {
        s->intc.hi_cpu_status &= ~OPTO_HI_IRQ_BIT;
        s->intc.hi_cop_status &= ~OPTO_HI_IRQ_BIT;
    } else {
        s->intc.hi_cpu_status |= OPTO_HI_IRQ_BIT;
        s->intc.hi_cop_status |= OPTO_HI_IRQ_BIT;
    }
    if (s->opts.verbose && s->opto.input_events <= 128u) {
        n1g_log(s,
                "apple opto clear pending=%u hi_cpu=0x%08x hi_en=0x%08x",
                s->opto.queue_len,
                s->intc.hi_cpu_status,
                s->intc.hi_cpu_enable);
    }
}

uint32_t n1g_dev_opto_read(n1g_state_t *s, uint32_t offset, uint32_t size) {
    uint32_t aligned = offset & ~3u;
    if (aligned < sizeof(s->opto.regs)) {
        uint32_t value = s->opto.regs[aligned / 4u];
        if (aligned == 0x04u) {
            value |= s->opto.queue_len ? OPTO_PENDING_BITS : 0u;
        } else if (aligned == 0x40u) {
            value = s->opto.queue_len
                        ? s->opto.queue[s->opto.queue_head]
                        : (OPTO_PACKET_BASE | s->opto.button_bits);
        }
        if (s->opts.verbose && (aligned == 0x04u || aligned == 0x40u) &&
            s->opto.input_events <= 128u) {
            n1g_log(s,
                    "apple opto read offset=0x%02x value=0x%08x pending=%u",
                    aligned,
                    value,
                    s->opto.queue_len);
        }
        return read_part(value, offset, size);
    }
    return 0;
}

void n1g_dev_opto_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value) {
    (void)size;
    uint32_t aligned = offset & ~3u;
    if (aligned >= sizeof(s->opto.regs)) {
        return;
    }

    if (aligned == 0x00u || aligned == 0x40u) {
        clear_status(s);
        return;
    }

    s->opto.regs[aligned / 4u] = value;
}

bool n1g_dev_opto_button(n1g_state_t *s, const char *button, bool pressed) {
    uint32_t bit = button_bit(button);
    if (bit == 0) {
        return false;
    }
    if (pressed && strcmp(button, "menu") == 0 && s->i2c.pcf_standby) {
        n1g_dev_i2c_onkey(s, true);
        set_last_input(s, "wake", button);
        return true;
    }
    if (suppress_for_hold(s, button)) {
        return true;
    }

    if (pressed) {
        s->opto.button_bits |= bit;
        set_last_input(s, "down", button);
    } else {
        s->opto.button_bits &= ~bit;
        set_last_input(s, "up", button);
    }
    enqueue_status(s, OPTO_PACKET_BASE | s->opto.button_bits);
    return true;
}

bool n1g_dev_opto_tap(n1g_state_t *s, const char *button, uint64_t hold_ticks) {
    uint32_t bit = button_bit(button);
    if (bit == 0) {
        return false;
    }
    if (strcmp(button, "menu") == 0 && s->i2c.pcf_standby) {
        n1g_dev_i2c_onkey(s, true);
        set_last_input(s, "wake", button);
        return true;
    }
    if (suppress_for_hold(s, button)) {
        return true;
    }
    if (s->opto.pending_release_bits != 0u) {
        s->opto.button_bits &= ~s->opto.pending_release_bits;
        enqueue_status(s, OPTO_PACKET_BASE | s->opto.button_bits);
    }
    s->opto.button_bits |= bit;
    s->opto.pending_release_bits = bit;
    s->opto.pending_release_tick = s->counters.device_ticks + hold_ticks;
    set_last_input(s, "tap", button);
    enqueue_status(s, OPTO_PACKET_BASE | s->opto.button_bits);
    return true;
}

bool n1g_dev_opto_wheel(n1g_state_t *s, int delta) {
    if (suppress_for_hold(s, delta >= 0 ? "wheel-down" : "wheel-up")) {
        return true;
    }
    int pos = (int)s->opto.wheel_pos + delta;
    while (pos < 0) {
        pos += 96;
    }
    s->opto.wheel_pos = (uint8_t)(pos % 96);
    set_last_input(s, delta >= 0 ? "wheel" : "wheel", delta >= 0 ? "down" : "up");
    enqueue_status(s,
                   OPTO_PACKET_BASE |
                   OPTO_WHEEL_ACTIVE |
                   ((uint32_t)s->opto.wheel_pos << 16u) |
                   s->opto.button_bits);
    return true;
}

void n1g_dev_opto_release_all(n1g_state_t *s) {
    if (s->opto.button_bits == 0u && s->opto.pending_release_bits == 0u) {
        return;
    }
    s->opto.button_bits = 0u;
    s->opto.pending_release_bits = 0u;
    s->opto.pending_release_tick = 0u;
    set_last_input(s, "up", "hold");
    enqueue_status(s, OPTO_PACKET_BASE);
}

void n1g_dev_opto_tick(n1g_state_t *s) {
    if (s->opto.pending_release_bits == 0u ||
        s->counters.device_ticks < s->opto.pending_release_tick) {
        return;
    }
    s->opto.button_bits &= ~s->opto.pending_release_bits;
    s->opto.pending_release_bits = 0;
    s->opto.pending_release_tick = 0;
    set_last_input(s, "up", "tap");
    enqueue_status(s, OPTO_PACKET_BASE | s->opto.button_bits);
}
