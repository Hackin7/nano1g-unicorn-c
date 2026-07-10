#include "nano1g/devices.h"

static uint32_t mask_for_size(uint32_t size) {
    if (size == 1) return 0xffu;
    if (size == 2) return 0xffffu;
    return 0xffffffffu;
}

static bool is_input_value_reg(uint32_t aligned) {
    uint32_t local = aligned & 0x7fu;
    return local >= 0x30u && local <= 0x3cu;
}

/* GPIOL_INPUT_VAL (port group I-L, port L): real iPod Nano 1g hardware wires
 * charger presence here (Rockbox firmware/target/arm/ipod/power-ipod.c
 * power_input_status()). Bit 3 (0x08) is the main/FireWire charger, active
 * low; bit 4 (0x10) is the USB charger, active high. Every other input-value
 * register keeps the blanket idle-high stub below (real pull-ups on unused
 * inputs), since only this one pin pair is externally observable/mockable
 * today via --main-charger / --usb-charger.
 */
#define N1G_GPIOL_INPUT_VAL_OFFSET 0x13cu
#define N1G_GPIOL_MAIN_CHARGER_BIT 0x08u
#define N1G_GPIOL_USB_CHARGER_BIT 0x10u

uint32_t n1g_dev_gpio_read(n1g_state_t *s, uint32_t offset, uint32_t size) {
    uint32_t aligned = offset & ~3u;
    if (offset < sizeof(s->gpio.regs)) {
        uint32_t shift = (offset & 3u) * 8u;
        uint32_t value = is_input_value_reg(aligned) ? 0xffffffffu : s->gpio.regs[offset / 4u];
        if (aligned == N1G_GPIOL_INPUT_VAL_OFFSET) {
            value = s->opts.main_charger_connected
                        ? (value & ~N1G_GPIOL_MAIN_CHARGER_BIT)
                        : (value | N1G_GPIOL_MAIN_CHARGER_BIT);
            value = s->opts.usb_charger_connected
                        ? (value | N1G_GPIOL_USB_CHARGER_BIT)
                        : (value & ~N1G_GPIOL_USB_CHARGER_BIT);
        }
        return (value >> shift) & mask_for_size(size);
    }
    return 0;
}

void n1g_dev_gpio_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value) {
    if (offset >= sizeof(s->gpio.regs)) {
        return;
    }

    if (offset >= 0x800u) {
        uint32_t target = offset - 0x800u;
        if (target < sizeof(s->gpio.regs)) {
            if (is_input_value_reg(target & ~3u)) {
                return;
            }
            uint32_t *reg = &s->gpio.regs[target / 4u];
            uint32_t shift = (target & 3u) * 8u;
            uint32_t mask = ((value >> 8) & 0xffu) << shift;
            uint32_t bits = (value & 0xffu) << shift;
            *reg = (*reg & ~mask) | (bits & mask);
        }
        return;
    }

    if (is_input_value_reg(offset & ~3u)) {
        return;
    }

    uint32_t shift = (offset & 3u) * 8u;
    uint32_t mask = mask_for_size(size) << shift;
    uint32_t bits = (value & mask_for_size(size)) << shift;
    s->gpio.regs[offset / 4u] = (s->gpio.regs[offset / 4u] & ~mask) | (bits & mask);
}
