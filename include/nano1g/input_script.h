#ifndef NANO1G_INPUT_SCRIPT_H
#define NANO1G_INPUT_SCRIPT_H

#include "nano1g/state.h"

/* Grammar (comma-separated tokens):
 *   wait:N        pause N device ticks
 *   NAME-down     press button (select|left|right|play|menu)
 *   NAME-up       release button
 *   NAME          press, hold N1G_INPUT_HOLD_TICKS, release
 *   wheel:+D      wheel movement of D steps (D may be negative)
 */
#define N1G_INPUT_HOLD_TICKS 2000u

bool n1g_input_script_load(n1g_input_script_t *script, const char *text);
void n1g_input_script_tick(n1g_state_t *s);
bool n1g_input_script_done(const n1g_input_script_t *script);

#endif
