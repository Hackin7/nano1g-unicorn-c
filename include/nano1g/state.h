#ifndef NANO1G_STATE_H
#define NANO1G_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "nano1g/map.h"
#include "nano1g/unicorn_compat.h"

typedef enum n1g_core {
    N1G_CORE_CPU = 0,
    N1G_CORE_COP = 1,
    N1G_CORE_COUNT = 2
} n1g_core_t;

typedef enum n1g_profile {
    N1G_PROFILE_ROCKBOX = 0,
    N1G_PROFILE_APPLE = 1
} n1g_profile_t;

typedef struct n1g_opts {
    const char *firmware_path;
    const char *disk_path;
    const char *ppm_path;
    const char *input_script;
    n1g_profile_t profile;
    uint64_t max_insns;
    uint32_t slice_insns;
    uint32_t load_addr;
    uint32_t entry;
    bool entry_set;
    bool trace_pc;
} n1g_opts_t;

typedef struct n1g_cpu {
    uc_engine *uc;
    bool running;
    bool halted;
    uint64_t guest_insns;
} n1g_cpu_t;

typedef struct n1g_ram {
    uint8_t *sdram;
    uint8_t *fastram;
} n1g_ram_t;

typedef struct n1g_disk {
    uint8_t *data;
    size_t size;
    uint32_t selected_lba;
    uint8_t sector_count;
    uint16_t data_index;
    uint8_t status;
} n1g_disk_t;

typedef struct n1g_intc {
    uint32_t cpu_status;
    uint32_t cop_status;
    uint32_t cpu_enable;
    uint32_t cop_enable;
    uint32_t hi_cpu_status;
    uint32_t hi_cop_status;
    uint32_t hi_cpu_enable;
    uint32_t hi_cop_enable;
} n1g_intc_t;

typedef struct n1g_timer {
    uint32_t cfg[2];
    uint32_t val[2];
    uint32_t usec;
} n1g_timer_t;

typedef struct n1g_cpucon {
    uint32_t ctl[2];
} n1g_cpucon_t;

typedef struct n1g_dma {
    uint32_t regs[0x2000 / 4];
} n1g_dma_t;

typedef struct n1g_gpio {
    uint32_t regs[0xa00 / 4];
} n1g_gpio_t;

typedef struct n1g_i2c {
    uint32_t regs[0x100 / 4];
} n1g_i2c_t;

typedef struct n1g_opto {
    uint32_t regs[0x100 / 4];
} n1g_opto_t;

typedef struct n1g_lcd2 {
    uint32_t regs[0x200 / 4];
    uint16_t pixels[N1G_LCD_W * N1G_LCD_H];
    uint32_t cursor;
    uint64_t words;
    bool dirty;
} n1g_lcd2_t;

typedef struct n1g_flash {
    uint8_t bytes[N1G_FLASH_SIZE];
} n1g_flash_t;

typedef struct n1g_counters {
    uint64_t guest_insns;
    uint64_t device_ticks;
    uint64_t mmio_reads;
    uint64_t mmio_writes;
    uint64_t lcd_words;
    uint64_t disk_reads;
    uint64_t irq_count;
} n1g_counters_t;

typedef struct n1g_state {
    n1g_opts_t opts;
    n1g_cpu_t cpu[N1G_CORE_COUNT];
    n1g_ram_t ram;
    n1g_disk_t disk;
    n1g_intc_t intc;
    n1g_timer_t timer;
    n1g_cpucon_t cpucon;
    n1g_dma_t dma;
    n1g_gpio_t gpio;
    n1g_i2c_t i2c;
    n1g_opto_t opto;
    n1g_lcd2_t lcd2;
    n1g_flash_t flash;
    n1g_counters_t counters;
    FILE *trace;
    void *mmio_contexts[32];
    size_t mmio_context_count;
} n1g_state_t;

#endif
