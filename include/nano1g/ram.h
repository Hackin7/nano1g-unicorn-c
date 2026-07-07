#ifndef NANO1G_RAM_H
#define NANO1G_RAM_H

#include "nano1g/state.h"

bool n1g_ram_init(n1g_state_t *s);
void n1g_ram_destroy(n1g_state_t *s);
bool n1g_ram_read(n1g_state_t *s, uint32_t addr, uint32_t size, uint32_t *out);
bool n1g_ram_write(n1g_state_t *s, uint32_t addr, uint32_t size, uint32_t value);
uint8_t *n1g_ram_ptr(n1g_state_t *s, uint32_t addr, size_t size);

#endif
