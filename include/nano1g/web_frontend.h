#ifndef NANO1G_WEB_FRONTEND_H
#define NANO1G_WEB_FRONTEND_H

#include "nano1g/state.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct n1g_web_server {
    intptr_t listen_fd;
    uint16_t port;
    uint64_t frame_seq;
    uint64_t last_lcd_words;
    bool active;
} n1g_web_server_t;

bool n1g_web_start(n1g_state_t *s, n1g_web_server_t *web, uint16_t port);
void n1g_web_poll(n1g_state_t *s, n1g_web_server_t *web, bool running);
void n1g_web_stop(n1g_web_server_t *web);
void n1g_web_sleep_ms(uint32_t ms);

#endif
