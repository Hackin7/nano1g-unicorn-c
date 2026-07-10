#include "nano1g/input_script.h"

#include <stdlib.h>
#include <string.h>

#include "nano1g/devices.h"
#include "nano1g/trace.h"

static bool push_event(n1g_input_script_t *script, n1g_input_event_t e) {
    if (script->count >= N1G_INPUT_MAX_EVENTS) {
        return false;
    }
    script->events[script->count++] = e;
    return true;
}

bool n1g_input_script_load(n1g_input_script_t *script, const char *text) {
    char buf[2048];
    memset(script, 0, sizeof(*script));
    strncpy(buf, text, sizeof(buf) - 1u);
    buf[sizeof(buf) - 1u] = '\0';

    for (char *tok = strtok(buf, ","); tok != NULL; tok = strtok(NULL, ",")) {
        n1g_input_event_t e;
        memset(&e, 0, sizeof(e));
        size_t len = strlen(tok);

        if (strncmp(tok, "wait:", 5) == 0) {
            e.kind = N1G_INPUT_EV_WAIT;
            e.ticks = strtoull(tok + 5, NULL, 10);
            if (!push_event(script, e)) return false;
        } else if (strncmp(tok, "wheel:", 6) == 0) {
            e.kind = N1G_INPUT_EV_WHEEL;
            e.delta = (int32_t)strtol(tok + 6, NULL, 10);
            if (!push_event(script, e)) return false;
        } else if (len > 5 && strcmp(tok + len - 5, "-down") == 0) {
            e.kind = N1G_INPUT_EV_DOWN;
            tok[len - 5] = '\0';
            strncpy(e.name, tok, sizeof(e.name) - 1u);
            if (!push_event(script, e)) return false;
        } else if (len > 3 && strcmp(tok + len - 3, "-up") == 0) {
            e.kind = N1G_INPUT_EV_UP;
            tok[len - 3] = '\0';
            strncpy(e.name, tok, sizeof(e.name) - 1u);
            if (!push_event(script, e)) return false;
        } else {
            n1g_input_event_t down_e, wait_e, up_e;
            memset(&down_e, 0, sizeof(down_e));
            memset(&wait_e, 0, sizeof(wait_e));
            memset(&up_e, 0, sizeof(up_e));

            down_e.kind = N1G_INPUT_EV_DOWN;
            strncpy(down_e.name, tok, sizeof(down_e.name) - 1u);

            wait_e.kind = N1G_INPUT_EV_WAIT;
            wait_e.ticks = N1G_INPUT_HOLD_TICKS;

            up_e.kind = N1G_INPUT_EV_UP;
            strncpy(up_e.name, tok, sizeof(up_e.name) - 1u);

            if (!push_event(script, down_e) || !push_event(script, wait_e) || !push_event(script, up_e)) {
                return false;
            }
        }
    }
    return script->count > 0;
}

void n1g_input_script_tick(n1g_state_t *s) {
    n1g_input_script_t *script = &s->input_script_state;

    if (script->wait_left > 0) {
        script->wait_left--;
        return;
    }

    while (script->cursor < script->count) {
        n1g_input_event_t *e = &script->events[script->cursor];

        if (e->kind == N1G_INPUT_EV_WAIT) {
            script->cursor++;
            if (e->ticks > 0) {
                script->wait_left = e->ticks - 1u;
                return;
            }
            continue;
        }

        if (e->kind == N1G_INPUT_EV_WHEEL) {
            n1g_dev_opto_wheel(s, e->delta);
            n1g_info(s, "input inject wheel delta=%d", e->delta);
        } else {
            bool down = e->kind == N1G_INPUT_EV_DOWN;
            n1g_dev_opto_button(s, e->name, down);
            n1g_info(s, "input inject button=%s state=%s", e->name, down ? "down" : "up");
        }
        script->cursor++;
    }
}

bool n1g_input_script_done(const n1g_input_script_t *script) {
    return script->cursor >= script->count && script->wait_left == 0;
}
