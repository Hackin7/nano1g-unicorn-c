#ifndef NANO1G_WEB_FRONTEND_H
#define NANO1G_WEB_FRONTEND_H

#include "nano1g/state.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct n1g_web_server {
    intptr_t listen_fd;
    uint16_t port;
    uint64_t frame_seq;
    uint64_t last_lcd_words;
    uint64_t last_backlight_generation;
    char restart_preset[32];
    char restart_error[256];
    bool restart_requested;
    bool force_backlight_on;
    bool active;
    void *impl;
} n1g_web_server_t;

bool n1g_web_start(n1g_state_t *s, n1g_web_server_t *web, uint16_t port);
void n1g_web_poll(n1g_state_t *s, n1g_web_server_t *web, bool running);
bool n1g_web_urgent(const n1g_web_server_t *web);
bool n1g_web_take_restart(n1g_web_server_t *web, char *preset, size_t preset_size);
void n1g_web_note_restart_failure(n1g_web_server_t *web, const char *message);
void n1g_web_clear_restart_error(n1g_web_server_t *web);
void n1g_web_stop(n1g_web_server_t *web);
void n1g_web_sleep_ms(uint32_t ms);

#endif
