#include "nano1g/devices.h"

#include "nano1g/trace.h"

#define MAILBOX_IRQ_BIT 4u
#define MAILBOX_QUEUE_NOTIFY 0x20000000u

static uint32_t mask_value(uint32_t value, uint32_t size) {
    if (size == 1) return value & 0xffu;
    if (size == 2) return value & 0xffffu;
    return value;
}

static n1g_core_t other_core(n1g_core_t core) {
    return core == N1G_CORE_CPU ? N1G_CORE_COP : N1G_CORE_CPU;
}

static void set_mailbox_irq(n1g_state_t *s, n1g_core_t core) {
    if (core == N1G_CORE_CPU) {
        s->intc.cpu_status |= (1u << MAILBOX_IRQ_BIT);
    } else {
        s->intc.cop_status |= (1u << MAILBOX_IRQ_BIT);
    }
}

static void clear_mailbox_irq(n1g_state_t *s, n1g_core_t core) {
    if (core == N1G_CORE_CPU) {
        s->intc.cpu_status &= ~(1u << MAILBOX_IRQ_BIT);
    } else {
        s->intc.cop_status &= ~(1u << MAILBOX_IRQ_BIT);
    }
}

static void mailbox_log(n1g_state_t *s,
                        const char *op,
                        n1g_core_t core,
                        uint32_t offset,
                        uint32_t value) {
    if (s->opts.profile != N1G_PROFILE_APPLE) {
        return;
    }
    static uint32_t logs;
    if (logs >= 128u) {
        return;
    }
    logs++;
    n1g_log(s,
            "apple mailbox %s core=%u offset=0x%02x value=0x%08x shared=0x%08x cpuq=0x%08x copq=0x%08x intc=0x%08x/0x%08x",
            op,
            (unsigned)core,
            offset,
            value,
            s->mailbox.shared_bits,
            s->mailbox.queue[N1G_CORE_CPU],
            s->mailbox.queue[N1G_CORE_COP],
            s->intc.cpu_status,
            s->intc.cop_status);
}

uint32_t n1g_dev_mailbox_read(n1g_state_t *s, n1g_core_t core, uint32_t offset, uint32_t size) {
    uint32_t value = 0;
    if (offset == 0x00u) {
        value = s->mailbox.shared_bits;
        clear_mailbox_irq(s, core);
    } else if (offset >= 0x10u && offset <= 0x1fu) {
        value = s->mailbox.queue[N1G_CORE_CPU];
        if (core == N1G_CORE_CPU) {
            s->mailbox.queue[N1G_CORE_CPU] = 0;
            clear_mailbox_irq(s, N1G_CORE_CPU);
        }
    } else if (offset >= 0x20u && offset <= 0x2fu) {
        value = s->mailbox.queue[N1G_CORE_COP];
        if (core == N1G_CORE_COP) {
            s->mailbox.queue[N1G_CORE_COP] = 0;
            clear_mailbox_irq(s, N1G_CORE_COP);
        }
    } else {
        value = 0;
    }
    mailbox_log(s, "read", core, offset, value);
    return mask_value(value, size);
}

void n1g_dev_mailbox_write(n1g_state_t *s,
                           n1g_core_t core,
                           uint32_t offset,
                           uint32_t size,
                           uint32_t value) {
    (void)size;
    if (offset == 0x04u) {
        s->mailbox.shared_bits |= value;
        set_mailbox_irq(s, other_core(core));
    } else if (offset == 0x08u) {
        s->mailbox.shared_bits &= ~value;
        set_mailbox_irq(s, other_core(core));
    } else if (offset >= 0x10u && offset <= 0x1fu) {
        s->mailbox.queue[N1G_CORE_CPU] |= value ? value : MAILBOX_QUEUE_NOTIFY;
        set_mailbox_irq(s, N1G_CORE_CPU);
    } else if (offset >= 0x20u && offset <= 0x2fu) {
        s->mailbox.queue[N1G_CORE_COP] |= value ? value : MAILBOX_QUEUE_NOTIFY;
        set_mailbox_irq(s, N1G_CORE_COP);
    }
    mailbox_log(s, "write", core, offset, value);
}
