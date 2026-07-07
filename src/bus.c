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

uint32_t n1g_bus_read(n1g_state_t *s, uint32_t addr, uint32_t size) {
    uint32_t out = 0;
    s->counters.mmio_reads++;
    if (n1g_ram_read(s, addr, size, &out)) {
        return out;
    }
    if (addr < N1G_FLASH_BASE + N1G_FLASH_SIZE) {
        return n1g_dev_flash_read(s, addr - N1G_FLASH_BASE, size);
    }
    if (addr >= N1G_CPUID_BASE && addr <= N1G_CPUID_BASE + 0xfff) {
        return 0x55504f44u;
    }
    if (addr >= N1G_MAILBOX_BASE && addr <= N1G_MAILBOX_BASE + 0x2f) {
        return 0;
    }
    if (addr >= N1G_INTC_BASE && addr <= N1G_INTC_BASE + 0x1ff) {
        return n1g_dev_intc_read(s, addr - N1G_INTC_BASE, size);
    }
    if (addr >= N1G_TIMER_BASE && addr <= N1G_TIMER_BASE + 0x17) {
        return n1g_dev_timer_read(s, addr - N1G_TIMER_BASE, size);
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
    if (addr >= N1G_PPCON_BASE && addr <= N1G_PPCON_BASE + 0x1fff) {
        if (addr == N1G_PPCON_BASE) return 0x00000022u;
        if (addr == N1G_PPCON_BASE + 4u) return 0x00005022u;
        return 0;
    }
    if (addr >= N1G_LCD2_BASE && addr <= N1G_LCD2_BASE + 0x1ff) {
        return n1g_dev_lcd2_read(s, addr - N1G_LCD2_BASE, size);
    }
    if (addr >= N1G_I2C_BASE && addr <= N1G_I2C_BASE + 0xff) {
        return n1g_dev_i2c_read(s, addr - N1G_I2C_BASE, size);
    }
    if (addr >= N1G_OPTO_BASE && addr <= N1G_OPTO_BASE + 0xff) {
        return n1g_dev_opto_read(s, addr - N1G_OPTO_BASE, size);
    }
    if (addr >= N1G_EIDE_BASE && addr <= N1G_EIDE_BASE + 0xfff) {
        return n1g_disk_read(s, addr - N1G_EIDE_BASE, size);
    }
    return mask_value(0, size);
}

void n1g_bus_write(n1g_state_t *s, uint32_t addr, uint32_t size, uint32_t value) {
    s->counters.mmio_writes++;
    if (n1g_ram_write(s, addr, size, value)) {
        return;
    }
    if (addr < N1G_FLASH_BASE + N1G_FLASH_SIZE) {
        n1g_dev_flash_write(s, addr - N1G_FLASH_BASE, size, value);
    } else if (addr >= N1G_INTC_BASE && addr <= N1G_INTC_BASE + 0x1ff) {
        n1g_dev_intc_write(s, addr - N1G_INTC_BASE, size, value);
    } else if (addr >= N1G_TIMER_BASE && addr <= N1G_TIMER_BASE + 0x17) {
        n1g_dev_timer_write(s, addr - N1G_TIMER_BASE, size, value);
    } else if (addr >= N1G_CPUCON_BASE && addr <= N1G_CPUCON_BASE + 0xfff) {
        n1g_dev_cpucon_write(s, addr - N1G_CPUCON_BASE, size, value);
    } else if (addr >= N1G_DMA_BASE && addr <= N1G_DMA_BASE + 0x1fff) {
        n1g_dev_dma_write(s, addr - N1G_DMA_BASE, size, value);
    } else if (addr >= N1G_GPIO_BASE && addr <= N1G_GPIO_BASE + 0x9ff) {
        n1g_dev_gpio_write(s, addr - N1G_GPIO_BASE, size, value);
    } else if (addr >= N1G_LCD2_BASE && addr <= N1G_LCD2_BASE + 0x1ff) {
        n1g_dev_lcd2_write(s, addr - N1G_LCD2_BASE, size, value);
    } else if (addr >= N1G_I2C_BASE && addr <= N1G_I2C_BASE + 0xff) {
        n1g_dev_i2c_write(s, addr - N1G_I2C_BASE, size, value);
    } else if (addr >= N1G_OPTO_BASE && addr <= N1G_OPTO_BASE + 0xff) {
        n1g_dev_opto_write(s, addr - N1G_OPTO_BASE, size, value);
    } else if (addr >= N1G_EIDE_BASE && addr <= N1G_EIDE_BASE + 0xfff) {
        n1g_disk_write(s, addr - N1G_EIDE_BASE, size, value);
    }
}

void n1g_bus_tick(n1g_state_t *s) {
    s->counters.device_ticks++;
    n1g_dev_timer_tick(s);
    n1g_dev_dma_tick(s);
    n1g_dev_intc_tick(s);
}
