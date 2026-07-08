#ifndef NANO1G_TRACE_H
#define NANO1G_TRACE_H

#include "nano1g/state.h"

void n1g_log(n1g_state_t *s, const char *fmt, ...);
void n1g_info(n1g_state_t *s, const char *fmt, ...);
void n1g_die(const char *fmt, ...);
uint32_t n1g_parse_u32(const char *text, const char *label);
uint64_t n1g_parse_u64(const char *text, const char *label);

#endif
