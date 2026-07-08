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

typedef enum n1g_boot_mode {
    N1G_BOOT_DIRECT = 0,
    N1G_BOOT_FLASH = 1
} n1g_boot_mode_t;

typedef enum n1g_low0_map {
    N1G_LOW0_NONE = 0,
    N1G_LOW0_RAM = 1,
    N1G_LOW0_FLASH = 2
} n1g_low0_map_t;

typedef struct n1g_opts {
    const char *firmware_path;
    const char *flash_path;
    const char *disk_path;
    const char *ppm_path;
    const char *input_script;
    n1g_profile_t profile;
    n1g_boot_mode_t boot_mode;
    uint64_t max_insns;
    uint32_t slice_insns;
    uint32_t timer_divider;
    uint32_t rtc_usec_per_tick;
    uint32_t load_addr;
    uint32_t entry;
    uint32_t dump_addr;
    uint32_t dump_count;
    uint16_t web_port;
    bool entry_set;
    bool dump_set;
    bool firmware_from_disk;
    bool map_flash_zero;
    bool virtual_memmap;
    bool ram_fill_zero;
    bool web_enabled;
    bool web_no_hold;
    bool run_forever;
    bool trace_pc;
    bool trace_mmio;
    bool verbose;
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
    uint32_t transfer_lba;
    uint16_t sectors_remaining;
    uint8_t sector_count;
    uint8_t device;
    uint8_t command;
    uint8_t error;
    uint8_t status;
    uint32_t ide0_cfg;
    uint32_t ide1_cfg;
    uint8_t transfer_kind;
    uint16_t data_index;
    uint8_t identify[512];
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

typedef struct n1g_mailbox {
    uint32_t shared_bits;
    uint32_t queue[2];
} n1g_mailbox_t;

typedef struct n1g_timer {
    uint32_t cfg[2];
    uint32_t val[2];
    uint32_t usec;
    uint32_t cfg_tick_phase;
} n1g_timer_t;

typedef struct n1g_cpucon {
    uint32_t ctl[2];
    uint32_t wait_ticks[2];
} n1g_cpucon_t;

typedef struct n1g_devcon {
    uint32_t regs[0x1000 / 4];
} n1g_devcon_t;

typedef struct n1g_cachecon {
    uint32_t regs[0x1000 / 4];
} n1g_cachecon_t;

typedef struct n1g_evp {
    uint32_t regs[8];
} n1g_evp_t;

typedef struct n1g_memcon {
    uint32_t regs[0x10000 / 4];
} n1g_memcon_t;

typedef struct n1g_dma {
    uint32_t regs[0x2000 / 4];
} n1g_dma_t;

typedef struct n1g_gpio {
    uint32_t regs[0xa00 / 4];
} n1g_gpio_t;

typedef struct n1g_i2c {
    uint32_t regs[0x100 / 4];
    uint8_t data[4];
    uint8_t control;
    uint8_t addr_op;
    uint8_t busy_reads;
    uint8_t pcf_reg;
    uint8_t pcf_regs[0x40];
    uint64_t pcf_written;
    bool pcf_reg_set;
} n1g_i2c_t;

typedef struct n1g_opto {
    uint32_t regs[0x100 / 4];
    uint32_t button_bits;
    uint8_t wheel_pos;
    uint64_t input_events;
    char last_input[32];
} n1g_opto_t;

typedef struct n1g_usb {
    uint32_t regs[0x1000 / 4];
} n1g_usb_t;

typedef struct n1g_lcd2 {
    uint32_t regs[0x200 / 4];
    uint16_t pixels[N1G_LCD_W * N1G_LCD_H];
    uint32_t cursor;
    uint32_t block_ctrl;
    uint32_t block_config;
    uint32_t block_pixels_remaining;
    uint16_t command;
    uint8_t window_x0;
    uint8_t window_y0;
    uint8_t window_x1;
    uint8_t window_y1;
    uint8_t cursor_x;
    uint8_t cursor_y;
    uint8_t pending_data_hi;
    bool pending_command;
    bool pending_data;
    uint64_t words;
    bool dirty;
} n1g_lcd2_t;

typedef struct n1g_flash {
    uint8_t bytes[N1G_FLASH_SIZE];
    uint8_t saved_window[0x100];
    uint8_t mode;
    uint8_t status;
    bool saved_window_valid;
    bool program_pending;
    bool erase_pending;
} n1g_flash_t;

typedef struct n1g_ppcon {
    uint32_t regs[0x2000 / 4];
} n1g_ppcon_t;

typedef struct n1g_counters {
    uint64_t guest_insns;
    uint64_t device_ticks;
    uint64_t mmio_reads;
    uint64_t mmio_writes;
    uint64_t lcd_words;
    uint64_t disk_reads;
    uint64_t irq_count;
    uint64_t apple_pc_hits[10];
    uint64_t apple_ui_hits[16];
    uint64_t apple_lcd_task_hits[18];
    uint64_t apple_lcd_path_hits[12];
    uint32_t apple_lcd_path_last[12][6];
    uint64_t apple_lcd_producer_hits[12];
    uint32_t apple_lcd_producer_last[12][8];
} n1g_counters_t;

typedef struct n1g_state {
    n1g_opts_t opts;
    n1g_cpu_t cpu[N1G_CORE_COUNT];
    n1g_ram_t ram;
    n1g_disk_t disk;
    n1g_intc_t intc;
    n1g_mailbox_t mailbox;
    n1g_timer_t timer;
    n1g_cpucon_t cpucon;
    n1g_devcon_t devcon;
    n1g_cachecon_t cachecon;
    n1g_evp_t evp;
    n1g_memcon_t memcon;
    n1g_dma_t dma;
    n1g_gpio_t gpio;
    n1g_i2c_t i2c;
    n1g_opto_t opto;
    n1g_usb_t usb;
    n1g_lcd2_t lcd2;
    n1g_flash_t flash;
    n1g_ppcon_t ppcon;
    n1g_counters_t counters;
    FILE *trace;
    void *mmio_contexts[32];
    size_t mmio_context_count;
    n1g_low0_map_t low0_map;
    bool flash_alias_mapped;
} n1g_state_t;

#endif
