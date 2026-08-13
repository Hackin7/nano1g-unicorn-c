#include "nano1g/bus.h"

#include "nano1g/devices.h"
#include "nano1g/disk_ata.h"
#include "nano1g/ram.h"
#include "nano1g/trace.h"

static uint32_t mask_value(uint32_t value, uint32_t size) {
    if (size == 1) return value & 0xffu;
    if (size == 2) return value & 0xffffu;
    return value;
}

static void host_profile_mmio(n1g_state_t *s, uint32_t addr, bool write) {
    if (!s->opts.host_profile) {
        return;
    }
    uint32_t slot = ((addr >> 2u) * 2654435761u) & (N1G_HOST_MMIO_PROFILE_SLOTS - 1u);
    for (uint32_t probe = 0; probe < N1G_HOST_MMIO_PROFILE_SLOTS; probe++) {
        n1g_host_mmio_profile_entry_t *entry = &s->host_mmio_profile[slot];
        if ((entry->reads == 0u && entry->writes == 0u) || entry->addr == addr) {
            entry->addr = addr;
            if (write) {
                entry->writes++;
            } else {
                entry->reads++;
            }
            return;
        }
        slot = (slot + 1u) & (N1G_HOST_MMIO_PROFILE_SLOTS - 1u);
    }
    s->host_mmio_profile_overflow++;
}

static const char *host_profile_mmio_device(uint32_t addr) {
    if (addr < N1G_FLASH_BASE + N1G_FLASH_SIZE) return "flash";
    if (addr >= N1G_FLASH_ALIAS_BASE && addr < N1G_FLASH_ALIAS_BASE + N1G_FLASH_SIZE) return "flash_alias";
    if (addr >= N1G_CPUID_BASE && addr <= N1G_CPUID_BASE + 0xfff) return "cpuid";
    if (addr >= N1G_MAILBOX_BASE && addr <= N1G_MAILBOX_BASE + 0x2f) return "mailbox";
    if (addr >= N1G_SYSREG_60003000_BASE && addr < N1G_SYSREG_60003000_BASE + N1G_SYSREG_60003000_SIZE) return "sysreg_60003000";
    if (addr >= N1G_INTC_BASE && addr <= N1G_INTC_BASE + 0x1ff) return "intc";
    if (addr >= N1G_TIMER_BASE && addr <= N1G_TIMER_BASE + 0x17) return "timer";
    if (addr >= N1G_DEVCON_BASE && addr <= N1G_DEVCON_BASE + 0xfff) return "devcon";
    if (addr >= N1G_CPUCON_BASE && addr <= N1G_CPUCON_BASE + 0xfff) return "cpucon";
    if (addr >= N1G_DMA_SECONDARY_BASE && addr <= N1G_DMA_SECONDARY_BASE + 0x1fff) return "dma_secondary";
    if (addr >= N1G_DMA_BASE && addr <= N1G_DMA_BASE + 0x1fff) return "dma";
    if (addr >= N1G_GPIO_BASE && addr <= N1G_GPIO_BASE + 0x9ff) return "gpio";
    if (addr >= N1G_CACHECON_BASE && addr <= N1G_CACHECON_BASE + 0xfff) return "cachecon";
    if (addr >= N1G_EVP_BASE && addr <= N1G_EVP_BASE + 0x1f) return "evp";
    if (addr >= N1G_PPCON_BASE && addr <= N1G_PPCON_BASE + 0x1fff) return "ppcon";
    if (addr >= N1G_SYSREG_70003800_BASE && addr < N1G_SYSREG_70003800_BASE + N1G_SYSREG_70003800_SIZE) return "sysreg_70003800";
    if (addr >= N1G_LCD2_BASE && addr <= N1G_LCD2_BASE + 0x1ff) return "lcd2";
    if (addr >= N1G_I2S_BASE && addr <= N1G_I2S_BASE + 0xff) return "i2s";
    if (addr >= N1G_PWM_BASE && addr <= N1G_PWM_BASE + 0x3f) return "pwm";
    if (addr >= N1G_I2C_BASE && addr <= N1G_I2C_BASE + 0xff) return "i2c";
    if (addr >= N1G_SERIAL0_BASE && addr < N1G_SERIAL0_BASE + 0x40) return "serial0";
    if (addr >= N1G_SERIAL1_BASE && addr < N1G_SERIAL1_BASE + 0x40) return "serial1";
    if (addr >= N1G_OPTO_BASE && addr <= N1G_OPTO_BASE + 0xff) return "opto";
    if (addr >= N1G_DIMMER_BASE && addr <= N1G_DIMMER_BASE + 0xff) return "dimmer";
    if (addr >= N1G_EIDE_BASE && addr <= N1G_EIDE_BASE + 0xfff) return "eide";
    if (addr >= N1G_USB_BASE && addr <= N1G_USB_BASE + 0xfff) return "usb";
    if (addr >= N1G_MEMCON_BASE && addr <= N1G_MEMCON_BASE + 0xffff) return "memcon";
    return "unrouted";
}

void n1g_bus_host_profile_report(n1g_state_t *s) {
    n1g_host_mmio_profile_entry_t top[16] = {{0}};
    for (uint32_t i = 0; i < N1G_HOST_MMIO_PROFILE_SLOTS; i++) {
        n1g_host_mmio_profile_entry_t candidate = s->host_mmio_profile[i];
        uint64_t candidate_total = candidate.reads + candidate.writes;
        if (candidate_total == 0u) {
            continue;
        }
        for (uint32_t rank = 0; rank < 16u; rank++) {
            uint64_t rank_total = top[rank].reads + top[rank].writes;
            if (candidate_total > rank_total ||
                (candidate_total == rank_total && candidate.addr < top[rank].addr)) {
                for (uint32_t move = 15u; move > rank; move--) {
                    top[move] = top[move - 1u];
                }
                top[rank] = candidate;
                break;
            }
        }
    }
    for (uint32_t rank = 0; rank < 16u; rank++) {
        uint64_t total = top[rank].reads + top[rank].writes;
        if (total == 0u) {
            break;
        }
        n1g_info(s,
                 "host_mmio rank=%u addr=0x%08x device=%s reads=%llu writes=%llu total=%llu",
                 rank + 1u,
                 top[rank].addr,
                 host_profile_mmio_device(top[rank].addr),
                 (unsigned long long)top[rank].reads,
                 (unsigned long long)top[rank].writes,
                 (unsigned long long)total);
    }
    if (s->host_mmio_profile_overflow != 0u) {
        n1g_info(s,
                 "host_mmio overflow=%llu slots=%u",
                 (unsigned long long)s->host_mmio_profile_overflow,
                 N1G_HOST_MMIO_PROFILE_SLOTS);
    }
}

/* First-access log for MMIO addresses that fall through every device route.
 * One line per 64-byte block keeps verbose runs readable while still
 * surfacing each unmodeled register bank native firmware touches. */
#define N1G_UNROUTED_SLOTS 128u
static uint32_t unrouted_blocks[N1G_UNROUTED_SLOTS];
static size_t unrouted_count;

static void log_unrouted(n1g_state_t *s, const char *kind, uint32_t addr, uint32_t size, uint32_t value) {
    if (kind[0] == 'w') {
        s->counters.unrouted_mmio_writes++;
    } else {
        s->counters.unrouted_mmio_reads++;
    }
    uint32_t block = (addr & ~0x3fu) | (kind[0] == 'w' ? 1u : 0u);
    for (size_t i = 0; i < unrouted_count; i++) {
        if (unrouted_blocks[i] == block) {
            return;
        }
    }
    if (unrouted_count < N1G_UNROUTED_SLOTS) {
        unrouted_blocks[unrouted_count++] = block;
    }
    n1g_log(s, "unrouted mmio %s addr=0x%08x size=%u value=0x%08x", kind, addr, size, value);
}

uint32_t n1g_dev_stub_read(n1g_state_t *s, const char *name, uint32_t base, uint32_t offset, uint32_t size) {
    (void)name;
    (void)base;
    (void)offset;
    (void)size;
    (void)s;
    return 0;
}

void n1g_dev_stub_write(n1g_state_t *s, const char *name, uint32_t base, uint32_t offset, uint32_t size, uint32_t value) {
    (void)s;
    (void)name;
    (void)base;
    (void)offset;
    (void)size;
    (void)value;
}

uint32_t n1g_bus_read(n1g_state_t *s, n1g_core_t core, uint32_t addr, uint32_t size) {
    uint32_t out = 0;
    s->counters.mmio_reads++;
    host_profile_mmio(s, addr, false);
    if (n1g_ram_read(s, addr, size, &out)) {
        return out;
    }
    if (addr < N1G_FLASH_BASE + N1G_FLASH_SIZE) {
        return n1g_dev_flash_read(s, addr - N1G_FLASH_BASE, size);
    }
    if (addr >= N1G_FLASH_ALIAS_BASE && addr < N1G_FLASH_ALIAS_BASE + N1G_FLASH_SIZE) {
        return n1g_dev_flash_read(s, addr - N1G_FLASH_ALIAS_BASE, size);
    }
    if (addr >= N1G_CPUID_BASE && addr <= N1G_CPUID_BASE + 0xfff) {
        return mask_value(core == N1G_CORE_COP ? 0xaaaaaaaau : 0x55555555u, size);
    }
    if (addr >= N1G_MAILBOX_BASE && addr <= N1G_MAILBOX_BASE + 0x2f) {
        return n1g_dev_mailbox_read(s, core, addr - N1G_MAILBOX_BASE, size);
    }
    if (addr >= N1G_SYSREG_60003000_BASE && addr < N1G_SYSREG_60003000_BASE + N1G_SYSREG_60003000_SIZE) {
        return n1g_dev_sysreg_60003000_read(s, addr - N1G_SYSREG_60003000_BASE, size);
    }
    if (addr >= N1G_INTC_BASE && addr <= N1G_INTC_BASE + 0x1ff) {
        return n1g_dev_intc_read(s, addr - N1G_INTC_BASE, size);
    }
    if (addr >= N1G_TIMER_BASE && addr <= N1G_TIMER_BASE + 0x17) {
        return n1g_dev_timer_read(s, addr - N1G_TIMER_BASE, size);
    }
    if (addr >= N1G_DEVCON_BASE && addr <= N1G_DEVCON_BASE + 0xfff) {
        return n1g_dev_devcon_read(s, addr - N1G_DEVCON_BASE, size);
    }
    if (addr >= N1G_CPUCON_BASE && addr <= N1G_CPUCON_BASE + 0xfff) {
        return n1g_dev_cpucon_read(s, addr - N1G_CPUCON_BASE, size);
    }
    if (addr >= N1G_DMA_SECONDARY_BASE && addr <= N1G_DMA_SECONDARY_BASE + 0x1fff) {
        return n1g_dev_dma_secondary_read(s, addr - N1G_DMA_SECONDARY_BASE, size);
    }
    if (addr >= N1G_DMA_BASE && addr <= N1G_DMA_BASE + 0x1fff) {
        return n1g_dev_dma_read(s, addr - N1G_DMA_BASE, size);
    }
    if (addr >= N1G_GPIO_BASE && addr <= N1G_GPIO_BASE + 0x9ff) {
        return n1g_dev_gpio_read(s, addr - N1G_GPIO_BASE, size);
    }
    if (addr >= N1G_CACHECON_BASE && addr <= N1G_CACHECON_BASE + 0xfff) {
        return n1g_dev_cachecon_read(s, addr - N1G_CACHECON_BASE, size);
    }
    if (addr >= N1G_EVP_BASE && addr <= N1G_EVP_BASE + 0x1f) {
        return n1g_dev_evp_read(s, addr - N1G_EVP_BASE, size);
    }
    if (addr >= N1G_PPCON_BASE && addr <= N1G_PPCON_BASE + 0x1fff) {
        return n1g_dev_ppcon_read(s, addr - N1G_PPCON_BASE, size);
    }
    if (addr >= N1G_SYSREG_70003800_BASE && addr < N1G_SYSREG_70003800_BASE + N1G_SYSREG_70003800_SIZE) {
        return n1g_dev_sysreg_70003800_read(s, addr - N1G_SYSREG_70003800_BASE, size);
    }
    if (addr >= N1G_LCD2_BASE && addr <= N1G_LCD2_BASE + 0x1ff) {
        return n1g_dev_lcd2_read(s, addr - N1G_LCD2_BASE, size);
    }
    if (addr >= N1G_I2S_BASE && addr <= N1G_I2S_BASE + 0xff) {
        return n1g_dev_i2s_read(s, addr - N1G_I2S_BASE, size);
    }
    if (addr >= N1G_PWM_BASE && addr <= N1G_PWM_BASE + 0x3f) {
        return n1g_dev_pwm_read(s, addr - N1G_PWM_BASE, size);
    }
    if (addr >= N1G_I2C_BASE && addr <= N1G_I2C_BASE + 0xff) {
        return n1g_dev_i2c_read(s, addr - N1G_I2C_BASE, size);
    }
    if (addr >= N1G_SERIAL0_BASE && addr < N1G_SERIAL0_BASE + 0x40) {
        return n1g_dev_serial_read(s, 0, addr - N1G_SERIAL0_BASE, size);
    }
    if (addr >= N1G_SERIAL1_BASE && addr < N1G_SERIAL1_BASE + 0x40) {
        return n1g_dev_serial_read(s, 1, addr - N1G_SERIAL1_BASE, size);
    }
    if (addr >= N1G_OPTO_BASE && addr <= N1G_OPTO_BASE + 0xff) {
        return n1g_dev_opto_read(s, addr - N1G_OPTO_BASE, size);
    }
    if (addr >= N1G_DIMMER_BASE && addr <= N1G_DIMMER_BASE + 0xff) {
        return n1g_dev_dimmer_read(s, addr - N1G_DIMMER_BASE, size);
    }
    if (addr >= N1G_EIDE_BASE && addr <= N1G_EIDE_BASE + 0xfff) {
        return n1g_disk_read(s, addr - N1G_EIDE_BASE, size);
    }
    if (addr >= N1G_USB_BASE && addr <= N1G_USB_BASE + 0xfff) {
        return n1g_dev_usb_read(s, addr - N1G_USB_BASE, size);
    }
    if (addr >= N1G_MEMCON_BASE && addr <= N1G_MEMCON_BASE + 0xffff) {
        return n1g_dev_memcon_read(s, addr - N1G_MEMCON_BASE, size);
    }
    log_unrouted(s, "read", addr, size, 0);
    return mask_value(0, size);
}

void n1g_bus_write_core(n1g_state_t *s, n1g_core_t core, uint32_t addr, uint32_t size, uint32_t value) {
    s->counters.mmio_writes++;
    host_profile_mmio(s, addr, true);
    if (n1g_ram_write(s, addr, size, value)) {
        return;
    }
    if (addr < N1G_FLASH_BASE + N1G_FLASH_SIZE) {
        n1g_dev_flash_write(s, addr - N1G_FLASH_BASE, size, value);
    } else if (addr >= N1G_FLASH_ALIAS_BASE && addr < N1G_FLASH_ALIAS_BASE + N1G_FLASH_SIZE) {
        n1g_dev_flash_write(s, addr - N1G_FLASH_ALIAS_BASE, size, value);
    } else if (addr >= N1G_MAILBOX_BASE && addr <= N1G_MAILBOX_BASE + 0x2f) {
        n1g_dev_mailbox_write(s, core, addr - N1G_MAILBOX_BASE, size, value);
    } else if (addr >= N1G_SYSREG_60003000_BASE && addr < N1G_SYSREG_60003000_BASE + N1G_SYSREG_60003000_SIZE) {
        n1g_dev_sysreg_60003000_write(s, addr - N1G_SYSREG_60003000_BASE, size, value);
    } else if (addr >= N1G_INTC_BASE && addr <= N1G_INTC_BASE + 0x1ff) {
        n1g_dev_intc_write(s, addr - N1G_INTC_BASE, size, value);
    } else if (addr >= N1G_TIMER_BASE && addr <= N1G_TIMER_BASE + 0x17) {
        n1g_dev_timer_write(s, addr - N1G_TIMER_BASE, size, value);
    } else if (addr >= N1G_DEVCON_BASE && addr <= N1G_DEVCON_BASE + 0xfff) {
        n1g_dev_devcon_write(s, addr - N1G_DEVCON_BASE, size, value);
    } else if (addr >= N1G_CPUCON_BASE && addr <= N1G_CPUCON_BASE + 0xfff) {
        n1g_dev_cpucon_write(s, addr - N1G_CPUCON_BASE, size, value);
    } else if (addr >= N1G_DMA_SECONDARY_BASE && addr <= N1G_DMA_SECONDARY_BASE + 0x1fff) {
        n1g_dev_dma_secondary_write(s, addr - N1G_DMA_SECONDARY_BASE, size, value);
    } else if (addr >= N1G_DMA_BASE && addr <= N1G_DMA_BASE + 0x1fff) {
        n1g_dev_dma_write(s, addr - N1G_DMA_BASE, size, value);
    } else if (addr >= N1G_GPIO_BASE && addr <= N1G_GPIO_BASE + 0x9ff) {
        n1g_dev_gpio_write(s, addr - N1G_GPIO_BASE, size, value);
    } else if (addr >= N1G_CACHECON_BASE && addr <= N1G_CACHECON_BASE + 0xfff) {
        n1g_dev_cachecon_write(s, addr - N1G_CACHECON_BASE, size, value);
    } else if (addr >= N1G_EVP_BASE && addr <= N1G_EVP_BASE + 0x1f) {
        n1g_dev_evp_write(s, addr - N1G_EVP_BASE, size, value);
    } else if (addr >= N1G_PPCON_BASE && addr <= N1G_PPCON_BASE + 0x1fff) {
        n1g_dev_ppcon_write(s, addr - N1G_PPCON_BASE, size, value);
    } else if (addr >= N1G_SYSREG_70003800_BASE && addr < N1G_SYSREG_70003800_BASE + N1G_SYSREG_70003800_SIZE) {
        n1g_dev_sysreg_70003800_write(s, addr - N1G_SYSREG_70003800_BASE, size, value);
    } else if (addr >= N1G_LCD2_BASE && addr <= N1G_LCD2_BASE + 0x1ff) {
        n1g_dev_lcd2_write(s, addr - N1G_LCD2_BASE, size, value);
    } else if (addr >= N1G_I2S_BASE && addr <= N1G_I2S_BASE + 0xff) {
        n1g_dev_i2s_write(s, addr - N1G_I2S_BASE, size, value);
    } else if (addr >= N1G_PWM_BASE && addr <= N1G_PWM_BASE + 0x3f) {
        n1g_dev_pwm_write(s, addr - N1G_PWM_BASE, size, value);
    } else if (addr >= N1G_I2C_BASE && addr <= N1G_I2C_BASE + 0xff) {
        n1g_dev_i2c_write(s, addr - N1G_I2C_BASE, size, value);
    } else if (addr >= N1G_SERIAL0_BASE && addr < N1G_SERIAL0_BASE + 0x40) {
        n1g_dev_serial_write(s, 0, addr - N1G_SERIAL0_BASE, size, value);
    } else if (addr >= N1G_SERIAL1_BASE && addr < N1G_SERIAL1_BASE + 0x40) {
        n1g_dev_serial_write(s, 1, addr - N1G_SERIAL1_BASE, size, value);
    } else if (addr >= N1G_OPTO_BASE && addr <= N1G_OPTO_BASE + 0xff) {
        n1g_dev_opto_write(s, addr - N1G_OPTO_BASE, size, value);
    } else if (addr >= N1G_DIMMER_BASE && addr <= N1G_DIMMER_BASE + 0xff) {
        n1g_dev_dimmer_write(s, addr - N1G_DIMMER_BASE, size, value);
    } else if (addr >= N1G_EIDE_BASE && addr <= N1G_EIDE_BASE + 0xfff) {
        n1g_disk_write(s, addr - N1G_EIDE_BASE, size, value);
    } else if (addr >= N1G_USB_BASE && addr <= N1G_USB_BASE + 0xfff) {
        n1g_dev_usb_write(s, addr - N1G_USB_BASE, size, value);
    } else if (addr >= N1G_MEMCON_BASE && addr <= N1G_MEMCON_BASE + 0xffff) {
        n1g_dev_memcon_write(s, addr - N1G_MEMCON_BASE, size, value);
    } else {
        log_unrouted(s, "write", addr, size, value);
    }
}

void n1g_bus_write(n1g_state_t *s, uint32_t addr, uint32_t size, uint32_t value) {
    n1g_bus_write_core(s, N1G_CORE_CPU, addr, size, value);
}

void n1g_bus_tick(n1g_state_t *s) {
    s->counters.device_ticks++;
    n1g_dev_opto_tick(s);
    n1g_dev_timer_tick(s);
    n1g_dev_i2c_tick(s);
    n1g_dev_i2s_tick(s);
    n1g_dev_dma_tick(s);
    n1g_dev_serial_tick(s);
    n1g_dev_cpucon_tick(s);
    n1g_disk_tick(s);
    n1g_dev_intc_tick(s);
}

static bool idle_devices_quiescent(const n1g_state_t *s) {
    if ((s->timer.cfg[0] & 0x80000000u) != 0u ||
        (s->timer.cfg[1] & 0x80000000u) != 0u ||
        s->cpucon.wait_ticks[0] != 0u || s->cpucon.wait_ticks[1] != 0u ||
        s->i2s.config != 0u || s->opto.pending_release_bits != 0u ||
        s->opto.queue_len != 0u || s->i2c.pcf_wake_requests != 0u) {
        return false;
    }
    if (s->intc.cpu_status != 0u || s->intc.cop_status != 0u ||
        s->intc.forced_status != 0u || s->intc.hi_cpu_status != 0u ||
        s->intc.hi_cop_status != 0u || s->intc.hi_forced_status != 0u) {
        return false;
    }
    if (s->disk.dma_pending || s->disk.pio_data_pending ||
        s->disk.pio_completion_pending || s->disk.nondata_pending ||
        s->disk.soft_reset_pending) {
        return false;
    }
    for (uint32_t channel = 0; channel < 4u; channel++) {
        if (s->dma.lcd_request_armed[channel] || s->dma.ch[channel].active) {
            return false;
        }
    }
    for (uint32_t channel = 0; channel < 2u; channel++) {
        if (s->serial.channel[channel].thre_irq_pending) {
            return false;
        }
    }
    if (!n1g_dev_i2c_idle_quiescent(s)) {
        return false;
    }
    return true;
}

static uint64_t ticks_until(uint64_t now, uint64_t deadline) {
    return deadline > now ? deadline - now : 1u;
}

uint64_t n1g_bus_idle_advance_limit(const n1g_state_t *s) {
    if (!s->cpu[N1G_CORE_CPU].halted || !s->cpu[N1G_CORE_COP].halted ||
        !idle_devices_quiescent(s)) {
        return 1u;
    }

    uint64_t now = s->counters.device_ticks;
    uint64_t limit = 65536u;
    const uint64_t deadlines[] = {
        s->i2c.pcf_standby_deadline,
        s->i2c.pcf_adc_deadline,
        s->i2c.pcf_low_battery_deadline,
        s->i2c.pcf_low_battery_standby_deadline,
    };
    for (size_t i = 0; i < sizeof(deadlines) / sizeof(deadlines[0]); i++) {
        if (deadlines[i] != 0u) {
            uint64_t until = ticks_until(now, deadlines[i]);
            if (until < limit) limit = until;
        }
    }

    uint64_t usec = s->opts.rtc_usec_per_tick;
    if (usec != 0u) {
        uint64_t elapsed_ticks = now - s->i2c.rtc_base_ticks;
        uint64_t target_usec = (s->i2c.rtc_last_second + 1u) * 1000000ull;
        uint64_t elapsed_usec = elapsed_ticks * usec;
        uint64_t until_second = target_usec > elapsed_usec
                                    ? (target_usec - elapsed_usec + usec - 1u) / usec
                                    : 1u;
        if (until_second < limit) limit = until_second;
    }
    return limit == 0u ? 1u : limit;
}

void n1g_bus_advance(n1g_state_t *s, uint64_t ticks) {
    if (ticks <= 1u) {
        n1g_bus_tick(s);
        return;
    }
    s->counters.device_ticks += ticks;
    s->timer.usec += (uint32_t)(ticks * s->opts.rtc_usec_per_tick);
    n1g_dev_i2c_tick(s);
    n1g_dev_intc_tick(s);
    s->counters.fast_forwarded_ticks += ticks - 1u;
}
