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

void n1g_dev_evp_init(n1g_state_t *s);
uint32_t n1g_dev_evp_read(n1g_state_t *s, uint32_t offset, uint32_t size);
void n1g_dev_evp_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value);

uint32_t n1g_dev_memcon_read(n1g_state_t *s, uint32_t offset, uint32_t size);
void n1g_dev_memcon_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value);

uint32_t n1g_dev_dma_read(n1g_state_t *s, uint32_t offset, uint32_t size);
void n1g_dev_dma_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value);
uint32_t n1g_dev_dma_secondary_read(n1g_state_t *s, uint32_t offset, uint32_t size);
void n1g_dev_dma_secondary_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value);
void n1g_dev_dma_tick(n1g_state_t *s);

uint32_t n1g_dev_gpio_read(n1g_state_t *s, uint32_t offset, uint32_t size);
void n1g_dev_gpio_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value);
bool n1g_dev_gpio_set_hold(n1g_state_t *s, bool engaged);
bool n1g_dev_gpio_set_chargers(n1g_state_t *s, bool main_connected, bool usb_connected);

uint32_t n1g_dev_i2c_read(n1g_state_t *s, uint32_t offset, uint32_t size);
void n1g_dev_i2c_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value);
void n1g_dev_i2c_tick(n1g_state_t *s);
void n1g_dev_i2c_get_rtc(const n1g_state_t *s, uint8_t fields[7]);
void n1g_dev_i2c_advance_rtc(n1g_state_t *s, uint64_t seconds);
void n1g_dev_i2c_save_pcf(const n1g_state_t *s, n1g_pcf_backup_t *backup);
void n1g_dev_i2c_restore_pcf(n1g_state_t *s, const n1g_pcf_backup_t *backup);

uint32_t n1g_dev_i2s_read(n1g_state_t *s, uint32_t offset, uint32_t size);
void n1g_dev_i2s_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value);
void n1g_dev_i2s_tick(n1g_state_t *s);
uint32_t n1g_dev_i2s_tx_free(const n1g_state_t *s);
void n1g_dev_i2s_push_tx(n1g_state_t *s, uint32_t size, uint32_t value);

uint32_t n1g_dev_serial_read(n1g_state_t *s, unsigned channel, uint32_t offset, uint32_t size);
void n1g_dev_serial_write(n1g_state_t *s, unsigned channel, uint32_t offset, uint32_t size, uint32_t value);
void n1g_dev_serial_tick(n1g_state_t *s);

uint32_t n1g_dev_opto_read(n1g_state_t *s, uint32_t offset, uint32_t size);
void n1g_dev_opto_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value);
bool n1g_dev_opto_button(n1g_state_t *s, const char *button, bool pressed);
bool n1g_dev_opto_tap(n1g_state_t *s, const char *button, uint64_t hold_ticks);
bool n1g_dev_opto_wheel(n1g_state_t *s, int delta);
void n1g_dev_opto_release_all(n1g_state_t *s);
void n1g_dev_opto_tick(n1g_state_t *s);

uint32_t n1g_dev_usb_read(n1g_state_t *s, uint32_t offset, uint32_t size);
void n1g_dev_usb_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value);

uint32_t n1g_dev_pwm_read(n1g_state_t *s, uint32_t offset, uint32_t size);
void n1g_dev_pwm_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value);
uint32_t n1g_dev_dimmer_read(n1g_state_t *s, uint32_t offset, uint32_t size);
void n1g_dev_dimmer_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value);
void n1g_dev_backlight_gpio_write(n1g_state_t *s, uint32_t gpio_offset, uint32_t write_mask);
bool n1g_dev_backlight_powered(const n1g_state_t *s);
uint32_t n1g_dev_backlight_level(const n1g_state_t *s);
uint32_t n1g_dev_backlight_intensity(const n1g_state_t *s);
const char *n1g_dev_backlight_mode(const n1g_state_t *s);

uint32_t n1g_dev_lcd2_read(n1g_state_t *s, uint32_t offset, uint32_t size);
void n1g_dev_lcd2_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value);
bool n1g_dev_lcd2_write_ppm(n1g_state_t *s, const char *path);

uint32_t n1g_dev_ppcon_read(n1g_state_t *s, uint32_t offset, uint32_t size);
void n1g_dev_ppcon_write(n1g_state_t *s, uint32_t offset, uint32_t size, uint32_t value);

uint32_t n1g_dev_stub_read(n1g_state_t *s, const char *name, uint32_t base, uint32_t offset, uint32_t size);
void n1g_dev_stub_write(n1g_state_t *s, const char *name, uint32_t base, uint32_t offset, uint32_t size, uint32_t value);

#endif
