#ifndef NANO1G_CPU_UNICORN_H
#define NANO1G_CPU_UNICORN_H

#include "nano1g/state.h"

bool n1g_cpu_init(n1g_state_t *s);
void n1g_cpu_destroy(n1g_state_t *s);
bool n1g_cpu_map_memory(n1g_state_t *s);
bool n1g_cpu_apply_memmap(n1g_state_t *s);
bool n1g_cpu_step_slice(n1g_state_t *s, n1g_core_t core, uint32_t max_insns);
void n1g_cpu_raise_irq(n1g_state_t *s, n1g_core_t core);
void n1g_cpu_raise_fiq(n1g_state_t *s, n1g_core_t core);
void n1g_cpu_flush_tb(n1g_state_t *s);
uint32_t n1g_cpu_pc(n1g_state_t *s, n1g_core_t core);
void n1g_cpu_set_reg(n1g_state_t *s, n1g_core_t core, int reg, uint32_t value);
uint32_t n1g_cpu_get_reg(n1g_state_t *s, n1g_core_t core, int reg);
void n1g_cpu_set_gpr(n1g_state_t *s, n1g_core_t core, unsigned reg, uint32_t value);
uint32_t n1g_cpu_get_gpr(n1g_state_t *s, n1g_core_t core, unsigned reg);

#endif
