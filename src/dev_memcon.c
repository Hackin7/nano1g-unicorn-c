#include "nano1g/devices.h"

#include "nano1g/cpu_unicorn.h"

#include <stdbool.h>

static uint32_t mask_for_size(uint32_t size) {
    if (size == 1u) return 0xffu;
    if (size == 2u) return 0xffffu;
    return 0xffffffffu;
}

static uint32_t merge_write(uint32_t old_value, uint32_t offset, uint32_t size, uint32_t value) {
    uint32_t shift = (offset & 3u) * 8u;
    uint32_t mask = mask_for_size(size) << shift;
    return (old_value & ~mask) | ((value << shift) & mask);
}

static uint32_t read_part(uint32_t value, uint32_t offset, uint32_t size) {
    uint32_t shift = (offset & 3u) * 8u;
    return (value >> shift) & mask_for_size(size);
}

uint32_t n1g_dev_memcon_read(n1g_state_t *s, uint32_t offset, uint32_t size) {
    uint32_t aligned = offset & ~3u;
    if (aligned >= sizeof(s->memcon.regs)) {
        return 0;
    }
    return read_part(s->memcon.regs[aligned / 4u], offset, size);
}

void n1g_dev_memcon_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value) {
    uint32_t aligned = offset & ~3u;
    if (aligned >= sizeof(s->memcon.regs)) {
        return;
    }
    uint32_t *reg = &s->memcon.regs[aligned / 4u];
    *reg = merge_write(*reg, offset, size, value);
    if (aligned == 0xf044u) {
        /* CACHE_OPERATION ops complete immediately; hardware self-clears the
         * op bits, so guest |= sequences do not resubmit stale ops. */
        *reg &= ~0x6u;
    }
    if (aligned >= 0xf000u && aligned <= 0xf03cu) {
        (void)n1g_cpu_apply_memmap(s);
    }
    /* PP502x cache maintenance lives in this window, not at CACHE_CTL:
     * CACHE_OPERATION (0xf044) invalidate ops and direct cache status-line
     * writes (CACHE_STATUS_BASE_CPU/COP at 0x4000/0x6000) discard the
     * icache, so stale Unicorn translation blocks must be dropped or the
     * guest keeps executing old code after loading a codec/plugin over it.
     * Debounced to one flush per device tick - the status-line discard loop
     * writes 512 words back to back. */
    bool icache_discard =
        (aligned >= 0x4000u && aligned < 0x8000u) ||
        (aligned == 0xf044u && (value & 0x4u) != 0);
    if (icache_discard) {
        /* Flushing Unicorn TBs from inside an executing MMIO hook frees the
         * block being executed, so defer: the mmio callback stops emulation
         * and the main loop flushes between slices.
         *
         * Debounce to one flush per discard burst: the guest invalidate loop
         * writes 512 status words for one logical icache discard, and the
         * code bytes it covers were already written before the loop began,
         * so the first flush of a burst is sufficient. */
        static uint64_t last_flush_request_tick = ~0ull;
        if (!s->tb_flush_pending &&
            (last_flush_request_tick == ~0ull ||
             s->counters.device_ticks - last_flush_request_tick >= 16u)) {
            s->tb_flush_pending = true;
        }
        last_flush_request_tick = s->counters.device_ticks;
    }
}
