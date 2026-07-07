#ifndef NANO1G_BUS_H
#define NANO1G_BUS_H

#include "nano1g/state.h"

uint32_t n1g_bus_read(n1g_state_t *s, uint32_t addr, uint32_t size);
void n1g_bus_write(n1g_state_t *s, uint32_t addr, uint32_t size, uint32_t value);
void n1g_bus_tick(n1g_state_t *s);

#endif
