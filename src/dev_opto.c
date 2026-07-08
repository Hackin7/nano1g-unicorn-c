#include "nano1g/devices.h"

#include <stdio.h>
#include <string.h>

#define OPTO_READY_BIT 0x04000000u
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

static void enqueue_status(n1g_state_t *s, uint32_t status) {
    s->opto.regs[0x40u / 4u] = status;
    s->opto.regs[0x04u / 4u] |= OPTO_READY_BIT;
    s->intc.hi_cpu_status |= OPTO_HI_IRQ_BIT;
    s->intc.hi_cop_status |= OPTO_HI_IRQ_BIT;
    s->opto.input_events++;
}

static void clear_status(n1g_state_t *s) {
    s->opto.regs[0x40u / 4u] = 0;
    s->opto.regs[0x04u / 4u] &= ~OPTO_READY_BIT;
    s->intc.hi_cpu_status &= ~OPTO_HI_IRQ_BIT;
    s->intc.hi_cop_status &= ~OPTO_HI_IRQ_BIT;
}

uint32_t n1g_dev_opto_read(n1g_state_t *s, uint32_t offset, uint32_t size) {
    uint32_t aligned = offset & ~3u;
    if (aligned < sizeof(s->opto.regs)) {
        return read_part(s->opto.regs[aligned / 4u], offset, size);
    }
    return 0;
}

void n1g_dev_opto_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value) {
    (void)size;
    uint32_t aligned = offset & ~3u;
    if (aligned >= sizeof(s->opto.regs)) {
        return;
    }

    if (aligned == 0x40u && value == 0) {
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

bool n1g_dev_opto_wheel(n1g_state_t *s, int delta) {
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
