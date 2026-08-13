#ifndef NANO1G_BUS_H
#define NANO1G_BUS_H

#include "nano1g/state.h"

uint32_t n1g_bus_read(n1g_state_t *s, n1g_core_t core, uint32_t addr, uint32_t size);
void n1g_bus_write(n1g_state_t *s, uint32_t addr, uint32_t size, uint32_t value);
void n1g_bus_write_core(n1g_state_t *s, n1g_core_t core, uint32_t addr, uint32_t size, uint32_t value);
void n1g_bus_tick(n1g_state_t *s);
uint64_t n1g_bus_idle_advance_limit(const n1g_state_t *s);
void n1g_bus_advance(n1g_state_t *s, uint64_t ticks);
void n1g_bus_host_profile_report(n1g_state_t *s);

#endif
