#include "nano1g/devices.h"

#include <stdio.h>

#define N1G_GPIO_GROUP_STRIDE      0x80u
#define N1G_GPIO_INPUT_VAL_LOCAL   0x30u
#define N1G_GPIO_INT_STAT_LOCAL    0x40u
#define N1G_GPIO_INT_EN_LOCAL      0x50u
#define N1G_GPIO_INT_LEV_LOCAL     0x60u
#define N1G_GPIO_INT_CLR_LOCAL     0x70u
#define N1G_GPIO_HI_IRQ_BIT        (1u << 0u)

#define N1G_GPIOA_INPUT_VAL_OFFSET 0x030u
#define N1G_GPIOA_INT_STAT_OFFSET  0x040u
#define N1G_GPIOA_INT_EN_OFFSET    0x050u
#define N1G_GPIOA_INT_LEV_OFFSET   0x060u
#define N1G_GPIOA_HOLD_BIT         0x20u

static uint32_t mask_for_size(uint32_t size) {
    if (size == 1) return 0xffu;
    if (size == 2) return 0xffffu;
    return 0xffffffffu;
}

static bool is_input_value_reg(uint32_t aligned) {
    uint32_t local = aligned & 0x7fu;
    return local >= 0x30u && local <= 0x3cu;
}

static bool is_interrupt_status_reg(uint32_t aligned) {
    uint32_t local = aligned & 0x7fu;
    return local >= N1G_GPIO_INT_STAT_LOCAL && local <= N1G_GPIO_INT_STAT_LOCAL + 0x0cu;
}

static bool is_interrupt_enable_reg(uint32_t aligned) {
    uint32_t local = aligned & 0x7fu;
    return local >= N1G_GPIO_INT_EN_LOCAL && local <= N1G_GPIO_INT_EN_LOCAL + 0x0cu;
}

static bool is_interrupt_level_reg(uint32_t aligned) {
    uint32_t local = aligned & 0x7fu;
    return local >= N1G_GPIO_INT_LEV_LOCAL && local <= N1G_GPIO_INT_LEV_LOCAL + 0x0cu;
}

static bool is_interrupt_clear_reg(uint32_t aligned) {
    uint32_t local = aligned & 0x7fu;
    return local >= N1G_GPIO_INT_CLR_LOCAL && local <= N1G_GPIO_INT_CLR_LOCAL + 0x0cu;
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

static uint32_t gpio_input_value(const n1g_state_t *s, uint32_t aligned) {
    uint32_t value = 0xffffffffu;
    if (aligned == N1G_GPIOA_INPUT_VAL_OFFSET) {
        value = s->opts.hold_switch_engaged
                    ? (value & ~N1G_GPIOA_HOLD_BIT)
                    : (value | N1G_GPIOA_HOLD_BIT);
    }
    if (aligned == N1G_GPIOL_INPUT_VAL_OFFSET) {
        value = s->opts.main_charger_connected
                    ? (value & ~N1G_GPIOL_MAIN_CHARGER_BIT)
                    : (value | N1G_GPIOL_MAIN_CHARGER_BIT);
        value = s->opts.usb_charger_connected
                    ? (value | N1G_GPIOL_USB_CHARGER_BIT)
                    : (value & ~N1G_GPIOL_USB_CHARGER_BIT);
    }
    return value;
}

static void gpio_irq_sync(n1g_state_t *s) {
    bool pending = false;
    for (uint32_t group = 0; group <= 0x100u; group += N1G_GPIO_GROUP_STRIDE) {
        for (uint32_t port = 0; port < 4u; port++) {
            uint32_t reg_offset = port * 4u;
            uint32_t status = s->gpio.regs[(group + N1G_GPIO_INT_STAT_LOCAL + reg_offset) / 4u];
            uint32_t enable = s->gpio.regs[(group + N1G_GPIO_INT_EN_LOCAL + reg_offset) / 4u];
            pending = pending || (status & enable) != 0u;
        }
    }
    if (pending) {
        s->intc.hi_cpu_status |= N1G_GPIO_HI_IRQ_BIT;
        s->intc.hi_cop_status |= N1G_GPIO_HI_IRQ_BIT;
    } else {
        s->intc.hi_cpu_status &= ~N1G_GPIO_HI_IRQ_BIT;
        s->intc.hi_cop_status &= ~N1G_GPIO_HI_IRQ_BIT;
    }
}

static void gpio_latch_input_change(n1g_state_t *s,
                                    uint32_t input_offset,
                                    uint32_t old_input,
                                    uint32_t input) {
    uint32_t group = input_offset & ~0x7fu;
    uint32_t port = input_offset & 0x0cu;
    uint32_t status_offset = group + N1G_GPIO_INT_STAT_LOCAL + port;
    uint32_t level_offset = group + N1G_GPIO_INT_LEV_LOCAL + port;
    uint32_t changed = old_input ^ input;
    uint32_t level = s->gpio.regs[level_offset / 4u];
    s->gpio.regs[status_offset / 4u] |= changed & ~(input ^ level);
    gpio_irq_sync(s);
}

bool n1g_dev_gpio_set_hold(n1g_state_t *s, bool engaged) {
    if (s->opts.hold_switch_engaged == engaged) {
        return true;
    }

    uint32_t old_input = gpio_input_value(s, N1G_GPIOA_INPUT_VAL_OFFSET);
    if (engaged) {
        n1g_dev_opto_release_all(s);
    }
    s->opts.hold_switch_engaged = engaged;
    uint32_t input = gpio_input_value(s, N1G_GPIOA_INPUT_VAL_OFFSET);
    gpio_latch_input_change(s, N1G_GPIOA_INPUT_VAL_OFFSET, old_input, input);

    s->opto.input_events++;
    (void)snprintf(s->opto.last_input,
                   sizeof(s->opto.last_input),
                   "hold:%s",
                   engaged ? "on" : "off");
    return true;
}

bool n1g_dev_gpio_set_chargers(n1g_state_t *s, bool main_connected, bool usb_connected) {
    if (s->opts.main_charger_connected == main_connected &&
        s->opts.usb_charger_connected == usb_connected) {
        return true;
    }
    uint32_t old_input = gpio_input_value(s, N1G_GPIOL_INPUT_VAL_OFFSET);
    s->opts.main_charger_connected = main_connected;
    s->opts.usb_charger_connected = usb_connected;
    uint32_t input = gpio_input_value(s, N1G_GPIOL_INPUT_VAL_OFFSET);
    gpio_latch_input_change(s, N1G_GPIOL_INPUT_VAL_OFFSET, old_input, input);
    return true;
}

uint32_t n1g_dev_gpio_read(n1g_state_t *s, uint32_t offset, uint32_t size) {
    uint32_t aligned = offset & ~3u;
    if (offset < sizeof(s->gpio.regs)) {
        uint32_t shift = (offset & 3u) * 8u;
        uint32_t value = is_input_value_reg(aligned)
                             ? gpio_input_value(s, aligned)
                             : s->gpio.regs[aligned / 4u];
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
            uint32_t aligned = target & ~3u;
            if (is_input_value_reg(aligned) || is_interrupt_status_reg(aligned)) {
                return;
            }
            uint32_t *reg = &s->gpio.regs[aligned / 4u];
            uint32_t shift = (target & 3u) * 8u;
            uint32_t mask = ((value >> 8) & 0xffu) << shift;
            uint32_t bits = (value & 0xffu) << shift;
            if (is_interrupt_clear_reg(aligned)) {
                uint32_t status_offset = aligned - N1G_GPIO_INT_CLR_LOCAL + N1G_GPIO_INT_STAT_LOCAL;
                s->gpio.regs[status_offset / 4u] &= ~(bits & mask);
            } else {
                *reg = (*reg & ~mask) | (bits & mask);
                n1g_dev_backlight_gpio_write(s, aligned, mask);
            }
            if (is_interrupt_enable_reg(aligned) || is_interrupt_level_reg(aligned) ||
                is_interrupt_clear_reg(aligned)) {
                gpio_irq_sync(s);
            }
        }
        return;
    }

    uint32_t aligned = offset & ~3u;
    if (is_input_value_reg(aligned) || is_interrupt_status_reg(aligned)) {
        return;
    }

    uint32_t shift = (offset & 3u) * 8u;
    uint32_t mask = mask_for_size(size) << shift;
    uint32_t bits = (value & mask_for_size(size)) << shift;
    if (is_interrupt_clear_reg(aligned)) {
        uint32_t status_offset = aligned - N1G_GPIO_INT_CLR_LOCAL + N1G_GPIO_INT_STAT_LOCAL;
        s->gpio.regs[status_offset / 4u] &= ~(bits & mask);
    } else {
        s->gpio.regs[aligned / 4u] = (s->gpio.regs[aligned / 4u] & ~mask) | (bits & mask);
        n1g_dev_backlight_gpio_write(s, aligned, mask);
    }
    if (is_interrupt_enable_reg(aligned) || is_interrupt_level_reg(aligned) ||
        is_interrupt_clear_reg(aligned)) {
        gpio_irq_sync(s);
    }
}
