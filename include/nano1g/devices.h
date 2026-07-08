#ifndef NANO1G_DEVICES_H
#define NANO1G_DEVICES_H

#include "nano1g/state.h"

uint32_t n1g_dev_flash_read(n1g_state_t *s, uint32_t offset, uint32_t size);
void n1g_dev_flash_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value);

uint32_t n1g_dev_intc_read(n1g_state_t *s, uint32_t offset, uint32_t size);
void n1g_dev_intc_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value);
void n1g_dev_intc_tick(n1g_state_t *s);

uint32_t n1g_dev_mailbox_read(n1g_state_t *s, n1g_core_t core, uint32_t offset, uint32_t size);
void n1g_dev_mailbox_write(n1g_state_t *s, n1g_core_t core, uint32_t offset, uint32_t size, uint32_t value);

uint32_t n1g_dev_timer_read(n1g_state_t *s, uint32_t offset, uint32_t size);
void n1g_dev_timer_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value);
void n1g_dev_timer_tick(n1g_state_t *s);

uint32_t n1g_dev_cpucon_read(n1g_state_t *s, uint32_t offset, uint32_t size);
void n1g_dev_cpucon_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value);
void n1g_dev_cpucon_tick(n1g_state_t *s);

uint32_t n1g_dev_devcon_read(n1g_state_t *s, uint32_t offset, uint32_t size);
void n1g_dev_devcon_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value);

uint32_t n1g_dev_cachecon_read(n1g_state_t *s, uint32_t offset, uint32_t size);
void n1g_dev_cachecon_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value);

uint32_t n1g_dev_memcon_read(n1g_state_t *s, uint32_t offset, uint32_t size);
void n1g_dev_memcon_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value);

uint32_t n1g_dev_dma_read(n1g_state_t *s, uint32_t offset, uint32_t size);
void n1g_dev_dma_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value);
void n1g_dev_dma_tick(n1g_state_t *s);

uint32_t n1g_dev_gpio_read(n1g_state_t *s, uint32_t offset, uint32_t size);
void n1g_dev_gpio_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value);

uint32_t n1g_dev_i2c_read(n1g_state_t *s, uint32_t offset, uint32_t size);
void n1g_dev_i2c_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value);

uint32_t n1g_dev_opto_read(n1g_state_t *s, uint32_t offset, uint32_t size);
void n1g_dev_opto_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value);

uint32_t n1g_dev_lcd2_read(n1g_state_t *s, uint32_t offset, uint32_t size);
void n1g_dev_lcd2_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value);
bool n1g_dev_lcd2_write_ppm(n1g_state_t *s, const char *path);

uint32_t n1g_dev_ppcon_read(n1g_state_t *s, uint32_t offset, uint32_t size);
void n1g_dev_ppcon_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value);

uint32_t n1g_dev_stub_read(n1g_state_t *s, const char *name, uint32_t base, uint32_t offset, uint32_t size);
void n1g_dev_stub_write(n1g_state_t *s, const char *name, uint32_t base, uint32_t offset, uint32_t size, uint32_t value);

#endif
