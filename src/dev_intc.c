#include "nano1g/devices.h"

#include "nano1g/cpu_unicorn.h"

uint32_t n1g_dev_intc_read(n1g_state_t *s, uint32_t offset, uint32_t size) {
    (void)size;
    switch (offset) {
    case 0x000: return s->intc.cpu_status;
    case 0x004: return s->intc.cop_status;
    case 0x020: return s->intc.cpu_enable;
    case 0x030: return s->intc.cop_enable;
    case 0x100: return s->intc.hi_cpu_status;
    case 0x104: return s->intc.hi_cop_status;
    case 0x120: return s->intc.hi_cpu_enable;
    case 0x130: return s->intc.hi_cop_enable;
    default: return 0;
    }
}

void n1g_dev_intc_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value) {
    (void)size;
    switch (offset) {
    case 0x024: s->intc.cpu_enable |= value; break;
    case 0x028: s->intc.cpu_enable &= ~value; break;
    case 0x034: s->intc.cop_enable |= value; break;
    case 0x038: s->intc.cop_enable &= ~value; break;
    case 0x124: s->intc.hi_cpu_enable |= value; break;
    case 0x128: s->intc.hi_cpu_enable &= ~value; break;
    case 0x134: s->intc.hi_cop_enable |= value; break;
    case 0x138: s->intc.hi_cop_enable &= ~value; break;
    default: break;
    }
}

void n1g_dev_intc_tick(n1g_state_t *s) {
    if (s->intc.cpu_status & s->intc.cpu_enable) {
        n1g_cpu_raise_irq(s, N1G_CORE_CPU);
    }
    if (s->intc.cop_status & s->intc.cop_enable) {
        n1g_cpu_raise_irq(s, N1G_CORE_COP);
    }
}
