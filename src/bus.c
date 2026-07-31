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

/* First-access log for MMIO addresses that fall through every device route.
 * One line per 64-byte block keeps verbose runs readable while still
 * surfacing each unmodeled register bank native firmware touches. */
#define N1G_UNROUTED_SLOTS 128u
static uint32_t unrouted_blocks[N1G_UNROUTED_SLOTS];
static size_t unrouted_count;

static void log_unrouted(n1g_state_t *s, const char *kind, uint32_t addr, uint32_t size, uint32_t value) {
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
    if (addr >= N1G_LCD2_BASE && addr <= N1G_LCD2_BASE + 0x1ff) {
        return n1g_dev_lcd2_read(s, addr - N1G_LCD2_BASE, size);
    }
    if (addr >= N1G_I2S_BASE && addr <= N1G_I2S_BASE + 0xff) {
        return n1g_dev_i2s_read(s, addr - N1G_I2S_BASE, size);
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
    if (n1g_ram_write(s, addr, size, value)) {
        return;
    }
    if (addr < N1G_FLASH_BASE + N1G_FLASH_SIZE) {
        n1g_dev_flash_write(s, addr - N1G_FLASH_BASE, size, value);
    } else if (addr >= N1G_FLASH_ALIAS_BASE && addr < N1G_FLASH_ALIAS_BASE + N1G_FLASH_SIZE) {
        n1g_dev_flash_write(s, addr - N1G_FLASH_ALIAS_BASE, size, value);
    } else if (addr >= N1G_MAILBOX_BASE && addr <= N1G_MAILBOX_BASE + 0x2f) {
        n1g_dev_mailbox_write(s, core, addr - N1G_MAILBOX_BASE, size, value);
    } else if (addr >= N1G_INTC_BASE && addr <= N1G_INTC_BASE + 0x1ff) {
        n1g_dev_intc_write(s, addr - N1G_INTC_BASE, size, value);
    } else if (addr >= N1G_TIMER_BASE && addr <= N1G_TIMER_BASE + 0x17) {
        n1g_dev_timer_write(s, addr - N1G_TIMER_BASE, size, value);
    } else if (addr >= N1G_DEVCON_BASE && addr <= N1G_DEVCON_BASE + 0xfff) {
        n1g_dev_devcon_write(s, addr - N1G_DEVCON_BASE, size, value);
    } else if (addr >= N1G_CPUCON_BASE && addr <= N1G_CPUCON_BASE + 0xfff) {
        n1g_dev_cpucon_write(s, addr - N1G_CPUCON_BASE, size, value);
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
    } else if (addr >= N1G_LCD2_BASE && addr <= N1G_LCD2_BASE + 0x1ff) {
        n1g_dev_lcd2_write(s, addr - N1G_LCD2_BASE, size, value);
    } else if (addr >= N1G_I2S_BASE && addr <= N1G_I2S_BASE + 0xff) {
        n1g_dev_i2s_write(s, addr - N1G_I2S_BASE, size, value);
    } else if (addr >= N1G_I2C_BASE && addr <= N1G_I2C_BASE + 0xff) {
        n1g_dev_i2c_write(s, addr - N1G_I2C_BASE, size, value);
    } else if (addr >= N1G_SERIAL0_BASE && addr < N1G_SERIAL0_BASE + 0x40) {
        n1g_dev_serial_write(s, 0, addr - N1G_SERIAL0_BASE, size, value);
    } else if (addr >= N1G_SERIAL1_BASE && addr < N1G_SERIAL1_BASE + 0x40) {
        n1g_dev_serial_write(s, 1, addr - N1G_SERIAL1_BASE, size, value);
    } else if (addr >= N1G_OPTO_BASE && addr <= N1G_OPTO_BASE + 0xff) {
        n1g_dev_opto_write(s, addr - N1G_OPTO_BASE, size, value);
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
    n1g_dev_i2s_tick(s);
    n1g_dev_dma_tick(s);
    n1g_dev_serial_tick(s);
    n1g_dev_cpucon_tick(s);
    n1g_disk_tick(s);
    n1g_dev_intc_tick(s);
}
