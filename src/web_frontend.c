#include "nano1g/web_frontend.h"

#include "nano1g/cpu_unicorn.h"
#include "nano1g/devices.h"
#include "nano1g/map.h"
#include "nano1g/trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#define N1G_WEB_INVALID_FD ((intptr_t)-1)

#ifdef _WIN32
#define N1G_SOCK(fd) ((SOCKET)(uintptr_t)(fd))
#else
#define N1G_SOCK(fd) ((int)(fd))
#endif

static const char index_html[] =
"<!doctype html><html><head><meta charset=\"utf-8\">"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<title>iPod Nano 1G Emulator</title><style>"
"body{background-color:#e8eaed;color:#1f2328;font-family:sans-serif;margin:0}"
"#container{margin:auto;max-width:560px;width:min(100%,560px);padding:20px}"
"h1{font-size:24px;margin:12px 0;text-align:center}"
"#status,#stats{font-size:14px;margin:12px 0;text-align:center}"
"#firmware-bar{align-items:center;display:flex;gap:8px;justify-content:center;margin:12px 0;font-size:14px}"
"#firmware-select{margin-left:6px}"
"#restart-btn{border:1px solid #c6cbd1;background:#fff;border-radius:4px;padding:4px 9px;color:#24292f}"
"#stats{align-items:center;display:flex;gap:14px;justify-content:center;flex-wrap:wrap}"
"#stats span{white-space:nowrap}"
"#ipod-container{transform:scale(1.8);transform-origin:top center;height:720px}"
"#ipod-container:focus{outline:none}"
"#ipod-body{background-color:white;border-radius:22px;box-shadow:0 18px 44px rgba(24,31,42,.18);height:392px;margin:auto;padding-top:28px;width:236px}"
"#ipod-screen{background:black;border-radius:4px;border:1px solid #d0d7de;display:block;image-rendering:pixelated;margin:auto;position:relative;width:176px;height:132px}"
"#ipod-clickwheel{position:relative;background-color:#ddd;width:156px;height:156px;margin:auto;margin-top:40px;border-radius:100%;box-shadow:inset #9c9c9c 0 0 2px 0}"
"#ipod-btn-select{background-color:white;width:58px;height:58px;border-radius:100%;left:49px;position:absolute;top:49px;border:0;box-shadow:#9c9c9c 0 0 2px 0}"
"#ipod-clickwheel button{background:transparent;border:0;height:36px;position:absolute;width:44px;color:#777;font-weight:700;font-size:11px;padding:0}"
"#ipod-btn-menu{left:56px;top:8px}"
"#ipod-btn-prev{left:8px;top:60px;font-size:18px}"
"#ipod-btn-next{right:8px;top:60px;font-size:18px}"
"#ipod-btn-play{bottom:8px;left:56px;font-size:15px}"
"#ipod-btn-select:active,#ipod-btn-select.active,#ipod-clickwheel button:active,#ipod-clickwheel button.active{background-color:rgba(0,0,0,.08)}"
"#debug{font-size:12px;text-align:center;color:#57606a;line-height:1.6}"
"@media(max-width:520px){#ipod-container{transform:scale(1.35);height:540px}#stats{flex-direction:column;gap:8px}}"
"@media(max-width:380px){#ipod-container{transform:scale(1.15);height:460px}}"
"</style></head><body><div id=\"container\">"
"<h1>iPod Nano 1G</h1>"
"<div id=\"status\">Loading...</div>"
"<div id=\"firmware-bar\"><label>Image <select id=\"firmware-select\"><option value=\"apple-stage0\">Apple stage0 OS</option><option value=\"apple-direct\">Apple official direct</option><option value=\"rockbox\">Rockbox</option></select></label><button id=\"restart-btn\" type=\"button\">Restart</button></div>"
"<div id=\"stats\"><span><b>FPS</b> <span id=\"fps\">0</span></span><span><b>guest</b> <span id=\"guest\">0</span></span><span><b>input</b> <span id=\"input\">none</span></span></div>"
"<div id=\"ipod-container\" tabindex=\"0\">"
"<div id=\"ipod-body\">"
"<canvas id=\"ipod-screen\" width=\"176\" height=\"132\"></canvas>"
"<div id=\"ipod-clickwheel\">"
"<button id=\"ipod-btn-menu\" aria-label=\"Menu\">MENU</button>"
"<button id=\"ipod-btn-prev\" aria-label=\"Previous\">&#9664;</button>"
"<button id=\"ipod-btn-next\" aria-label=\"Next\">&#9654;</button>"
"<button id=\"ipod-btn-play\" aria-label=\"Play/Pause\">&#9654;&#10073;&#10073;</button>"
"<div id=\"ipod-btn-select\" role=\"button\" aria-label=\"Select\"></div>"
"</div></div></div>"
"<div id=\"debug\">state <span id=\"running\">...</span> &middot; lcd <span id=\"lcd\">0</span> &middot; disk <span id=\"disk\">0</span> &middot; pc <span id=\"pc\">0x00000000</span></div>"
"</div><script>"
"const fps_counter=document.getElementById('fps');"
"const canvas=document.getElementById('ipod-screen');"
"const ctx=canvas.getContext('2d');"
"const status_el=document.getElementById('status');"
"const firmware_select=document.getElementById('firmware-select');"
"const restart_btn=document.getElementById('restart-btn');"
"const ipod=document.getElementById('ipod-container');"
"let seq=-1,last_frame=0,wheel_down=false,last_angle=0;"
"function set_status(message){status_el.textContent=message;}"
"function text(id,value){document.getElementById(id).textContent=value;}"
"async function send_input(url){try{await fetch(url,{cache:'no-store'});}catch(e){set_status('Input failed: '+e.message);}}"
"async function restart_selected(){set_status('Restarting '+firmware_select.value+'...');seq=-1;try{const r=await fetch('/control?restart='+encodeURIComponent(firmware_select.value),{cache:'no-store'});if(!r.ok)set_status('Restart failed');}catch(e){set_status('Restart failed: '+e.message);}}"
"restart_btn.onclick=e=>{e.preventDefault();restart_selected();};"
"function button_url(name,state){return '/input?button='+encodeURIComponent(name)+'&state='+state;}"
"function bind_button(selector,name){const el=document.querySelector(selector);let release_timer=null;"
"const down=e=>{e.preventDefault();clearTimeout(release_timer);el.classList.add('active');send_input(button_url(name,'down'));};"
"const up=e=>{e.preventDefault();clearTimeout(release_timer);el.classList.remove('active');release_timer=setTimeout(()=>send_input(button_url(name,'up')),80);};"
"el.onmousedown=down;el.onmouseup=up;el.onmouseleave=up;el.onpointerdown=down;el.onpointerup=up;el.onpointercancel=up;el.ontouchstart=down;el.ontouchend=up;el.ontouchcancel=up;el.onclick=e=>{down(e);up(e);};}"
"bind_button('#ipod-btn-menu','menu');bind_button('#ipod-btn-prev','left');bind_button('#ipod-btn-next','right');bind_button('#ipod-btn-play','play');bind_button('#ipod-btn-select','select');"
"function key_button(key){switch(key){case'ArrowUp':return'menu';case'ArrowLeft':return'left';case'ArrowRight':return'right';case'ArrowDown':return'play';case'Enter':return'select';default:return null;}}"
"ipod.onkeydown=e=>{const b=key_button(e.key);if(b){e.preventDefault();send_input(button_url(b,'down'));}};"
"ipod.onkeyup=e=>{const b=key_button(e.key);if(b){e.preventDefault();send_input(button_url(b,'up'));}};"
"const wheel=document.getElementById('ipod-clickwheel');"
"function wheel_step(delta){send_input('/input?wheel='+(delta<0?'up':'down'));}"
"wheel.onmousedown=e=>{e.preventDefault();wheel_down=true;};"
"wheel.onmouseup=e=>{e.preventDefault();wheel_down=false;};"
"wheel.onmouseleave=e=>{e.preventDefault();wheel_down=false;};"
"wheel.onmousemove=e=>{e.preventDefault();if(!wheel_down)return;const r=e.currentTarget.getBoundingClientRect();"
"const a=Math.atan2(e.clientX-(r.left+r.width/2),-(e.clientY-(r.top+r.height/2)))*(180/Math.PI)+180;"
"let d=a-last_angle;if(d>180)d-=360;if(d<-180)d+=360;if(Math.abs(d)>10){wheel_step(d);last_angle=a;}};"
"wheel.onwheel=e=>{e.preventDefault();wheel_step(e.deltaY);};"
"document.body.addEventListener('mousewheel',e=>{e.preventDefault();wheel_step(e.deltaY);},{passive:false});"
"async function draw_frame(frame_seq){const r=await fetch('/frame.rgba?'+frame_seq,{cache:'no-store'});const buf=await r.arrayBuffer();ctx.putImageData(new ImageData(new Uint8ClampedArray(buf),176,132),0,0);const now=performance.now();fps_counter.textContent=last_frame?Math.floor(1000/(now-last_frame)):'0';last_frame=now;}"
"async function tick(){try{const r=await fetch('/status.json',{cache:'no-store'});const s=await r.json();"
"set_status((s.running?'Running':'Stopped')+' - '+s.label);if(s.preset)firmware_select.value=s.preset;text('running',s.running?'running':'stopped');text('guest',s.guest_insns.toLocaleString());text('lcd',s.lcd_words.toLocaleString());text('disk',s.disk_reads.toLocaleString());text('input',s.input);text('pc',s.cpu_pc);"
"if(s.frame_seq!==seq){seq=s.frame_seq;draw_frame(seq);}}catch(e){set_status('Offline');text('running','offline');}}"
"setInterval(tick,120);tick();ipod.focus();"
"</script></body></html>";

#ifdef _WIN32
static bool winsock_started;
#endif

static void close_fd(intptr_t fd) {
    if (fd == N1G_WEB_INVALID_FD) {
        return;
    }
#ifdef _WIN32
    closesocket(N1G_SOCK(fd));
#else
    close(N1G_SOCK(fd));
#endif
}

static bool set_nonblocking(intptr_t fd) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(N1G_SOCK(fd), FIONBIO, &mode) == 0;
#else
    int flags = fcntl(N1G_SOCK(fd), F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    return fcntl(N1G_SOCK(fd), F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

static bool would_block(void) {
#ifdef _WIN32
    int err = WSAGetLastError();
    return err == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

void n1g_web_sleep_ms(uint32_t ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000u);
#endif
}

static bool send_all(intptr_t fd, const void *data, size_t len) {
    const char *p = (const char *)data;
    while (len > 0) {
        int n = send(N1G_SOCK(fd), p, len > 16384u ? 16384 : (int)len, 0);
        if (n <= 0) {
            if (would_block()) {
                n1g_web_sleep_ms(1);
                continue;
            }
            return false;
        }
        p += n;
        len -= (size_t)n;
    }
    return true;
}

static bool send_response(intptr_t fd,
                          const char *status,
                          const char *type,
                          const void *body,
                          size_t body_len) {
    char hdr[384];
    int n = snprintf(hdr,
                     sizeof(hdr),
                     "HTTP/1.1 %s\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %llu\r\n"
                     "Cache-Control: no-store\r\n"
                     "Connection: close\r\n"
                     "\r\n",
                     status,
                     type,
                     (unsigned long long)body_len);
    if (n <= 0 || (size_t)n >= sizeof(hdr)) {
        return false;
    }
    return send_all(fd, hdr, (size_t)n) && send_all(fd, body, body_len);
}

static void put16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
    p[2] = (uint8_t)((v >> 16) & 0xffu);
    p[3] = (uint8_t)(v >> 24);
}

static void rgb565(uint16_t p, uint8_t *r, uint8_t *g, uint8_t *b) {
    uint16_t raw = (uint16_t)((p >> 8u) | (p << 8u));
    *r = (uint8_t)(((raw >> 11) & 0x1fu) * 255u / 31u);
    *g = (uint8_t)(((raw >> 5) & 0x3fu) * 255u / 63u);
    *b = (uint8_t)((raw & 0x1fu) * 255u / 31u);
}

static uint8_t *make_bmp(n1g_state_t *s, size_t *out_len) {
    const uint32_t w = N1G_LCD_W;
    const uint32_t h = N1G_LCD_H;
    const uint32_t row = (w * 3u + 3u) & ~3u;
    const uint32_t pixel_bytes = row * h;
    const uint32_t total = 54u + pixel_bytes;
    uint8_t *bmp = (uint8_t *)calloc(1, total);
    if (!bmp) {
        return NULL;
    }

    bmp[0] = 'B';
    bmp[1] = 'M';
    put32(bmp + 2, total);
    put32(bmp + 10, 54u);
    put32(bmp + 14, 40u);
    put32(bmp + 18, w);
    put32(bmp + 22, (uint32_t)(0u - h));
    put16(bmp + 26, 1u);
    put16(bmp + 28, 24u);
    put32(bmp + 34, pixel_bytes);

    uint8_t *dst = bmp + 54u;
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            uint8_t r = 0, g = 0, b = 0;
            rgb565(s->lcd2.pixels[y * w + x], &r, &g, &b);
            uint8_t *px = dst + y * row + x * 3u;
            px[0] = b;
            px[1] = g;
            px[2] = r;
        }
    }

    *out_len = total;
    return bmp;
}

static uint8_t *make_rgba(n1g_state_t *s, size_t *out_len) {
    const uint32_t pixels = N1G_LCD_W * N1G_LCD_H;
    uint8_t *rgba = (uint8_t *)malloc(pixels * 4u);
    if (!rgba) {
        return NULL;
    }

    for (uint32_t i = 0; i < pixels; i++) {
        uint8_t r = 0, g = 0, b = 0;
        rgb565(s->lcd2.pixels[i], &r, &g, &b);
        rgba[i * 4u + 0u] = r;
        rgba[i * 4u + 1u] = g;
        rgba[i * 4u + 2u] = b;
        rgba[i * 4u + 3u] = 0xffu;
    }

    *out_len = pixels * 4u;
    return rgba;
}

static bool send_status(n1g_state_t *s, n1g_web_server_t *web, intptr_t fd, bool running) {
    const char *label = s->opts.run_label ? s->opts.run_label : "custom";
    const char *preset = "custom";
    if (strcmp(label, "Apple stage0 OS") == 0) {
        preset = "apple-stage0";
    } else if (strcmp(label, "Apple official direct") == 0) {
        preset = "apple-direct";
    } else if (strcmp(label, "Rockbox") == 0) {
        preset = "rockbox";
    }

    char body[768];
    int n = snprintf(body,
                     sizeof(body),
                     "{\"running\":%s,\"frame_seq\":%llu,\"guest_insns\":%llu,"
                     "\"device_ticks\":%llu,\"lcd_words\":%llu,\"disk_reads\":%llu,"
                     "\"irq_count\":%llu,\"input_events\":%llu,\"input\":\"%s\","
                     "\"label\":\"%s\",\"preset\":\"%s\","
                     "\"cpu_pc\":\"0x%08x\"}\n",
                     running ? "true" : "false",
                     (unsigned long long)web->frame_seq,
                     (unsigned long long)s->counters.guest_insns,
                     (unsigned long long)s->counters.device_ticks,
                     (unsigned long long)s->counters.lcd_words,
                     (unsigned long long)s->counters.disk_reads,
                     (unsigned long long)s->counters.irq_count,
                     (unsigned long long)s->opto.input_events,
                     s->opto.last_input[0] ? s->opto.last_input : "none",
                     label,
                     preset,
                     n1g_cpu_pc(s, N1G_CORE_CPU));
    if (n <= 0 || (size_t)n >= sizeof(body)) {
        return false;
    }
    return send_response(fd, "200 OK", "application/json", body, (size_t)n);
}

static bool query_has(const char *query, const char *needle) {
    return query && strstr(query, needle) != NULL;
}

static bool send_input(n1g_state_t *s, intptr_t fd, const char *query) {
    bool ok = false;
    if (query_has(query, "wheel=down")) {
        ok = n1g_dev_opto_wheel(s, 4);
    } else if (query_has(query, "wheel=up")) {
        ok = n1g_dev_opto_wheel(s, -4);
    } else {
        const char *buttons[] = {"menu", "left", "select", "right", "play"};
        for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
            char key[32];
            (void)snprintf(key, sizeof(key), "button=%s", buttons[i]);
            if (query_has(query, key)) {
                ok = n1g_dev_opto_button(s, buttons[i], !query_has(query, "state=up"));
                break;
            }
        }
    }

    if (!ok) {
        const char msg[] = "{\"ok\":false}\n";
        return send_response(fd, "400 Bad Request", "application/json", msg, sizeof(msg) - 1u);
    }
    const char msg[] = "{\"ok\":true}\n";
    return send_response(fd, "200 OK", "application/json", msg, sizeof(msg) - 1u);
}

static bool valid_restart_preset(const char *preset) {
    return strcmp(preset, "rockbox") == 0 ||
           strcmp(preset, "apple-direct") == 0 ||
           strcmp(preset, "apple-stage0") == 0;
}

static const char *restart_preset_from_query(const char *query) {
    if (query_has(query, "restart=rockbox")) return "rockbox";
    if (query_has(query, "restart=apple-direct")) return "apple-direct";
    if (query_has(query, "restart=apple-stage0")) return "apple-stage0";
    return NULL;
}

static bool send_control(n1g_web_server_t *web, intptr_t fd, const char *query) {
    const char *preset = restart_preset_from_query(query);
    if (!preset || !valid_restart_preset(preset)) {
        const char msg[] = "{\"ok\":false}\n";
        return send_response(fd, "400 Bad Request", "application/json", msg, sizeof(msg) - 1u);
    }

    strncpy(web->restart_preset, preset, sizeof(web->restart_preset) - 1u);
    web->restart_preset[sizeof(web->restart_preset) - 1u] = '\0';
    web->restart_requested = true;
    const char msg[] = "{\"ok\":true}\n";
    return send_response(fd, "200 OK", "application/json", msg, sizeof(msg) - 1u);
}

static void handle_client(n1g_state_t *s, n1g_web_server_t *web, intptr_t fd, bool running) {
    char req[2048];
    int n = recv(N1G_SOCK(fd), req, sizeof(req) - 1u, 0);
    if (n <= 0 && would_block()) {
        n1g_web_sleep_ms(1);
        n = recv(N1G_SOCK(fd), req, sizeof(req) - 1u, 0);
    }
    if (n <= 0) {
        return;
    }
    req[n] = '\0';
    if (strncmp(req, "GET ", 4) != 0) {
        const char msg[] = "method not allowed\n";
        (void)send_response(fd, "405 Method Not Allowed", "text/plain", msg, sizeof(msg) - 1u);
        return;
    }

    char *path = req + 4;
    char *end = strchr(path, ' ');
    if (!end) {
        return;
    }
    *end = '\0';
    char *query = strchr(path, '?');
    if (query) {
        *query++ = '\0';
    }

    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
        (void)send_response(fd, "200 OK", "text/html; charset=utf-8", index_html, sizeof(index_html) - 1u);
    } else if (strcmp(path, "/status.json") == 0) {
        (void)send_status(s, web, fd, running);
    } else if (strcmp(path, "/input") == 0) {
        (void)send_input(s, fd, query ? query : "");
    } else if (strcmp(path, "/control") == 0) {
        (void)send_control(web, fd, query ? query : "");
    } else if (strcmp(path, "/frame.rgba") == 0) {
        size_t len = 0;
        uint8_t *rgba = make_rgba(s, &len);
        if (!rgba) {
            const char msg[] = "out of memory\n";
            (void)send_response(fd, "500 Internal Server Error", "text/plain", msg, sizeof(msg) - 1u);
        } else {
            (void)send_response(fd, "200 OK", "application/octet-stream", rgba, len);
            free(rgba);
        }
    } else if (strcmp(path, "/frame.bmp") == 0) {
        size_t len = 0;
        uint8_t *bmp = make_bmp(s, &len);
        if (!bmp) {
            const char msg[] = "out of memory\n";
            (void)send_response(fd, "500 Internal Server Error", "text/plain", msg, sizeof(msg) - 1u);
        } else {
            (void)send_response(fd, "200 OK", "image/bmp", bmp, len);
            free(bmp);
        }
    } else if (strcmp(path, "/favicon.ico") == 0) {
        (void)send_response(fd, "204 No Content", "text/plain", "", 0);
    } else {
        const char msg[] = "not found\n";
        (void)send_response(fd, "404 Not Found", "text/plain", msg, sizeof(msg) - 1u);
    }
}

bool n1g_web_start(n1g_state_t *s, n1g_web_server_t *web, uint16_t port) {
    memset(web, 0, sizeof(*web));
    web->listen_fd = N1G_WEB_INVALID_FD;
    web->port = port;
    web->last_lcd_words = UINT64_MAX;

#ifdef _WIN32
    if (!winsock_started) {
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            n1g_info(s, "web frontend failed to initialize Winsock");
            return false;
        }
        winsock_started = true;
    }
#endif

    intptr_t fd = (intptr_t)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == N1G_WEB_INVALID_FD) {
        n1g_info(s, "web frontend failed to create socket");
        return false;
    }

    int yes = 1;
    (void)setsockopt(N1G_SOCK(fd), SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (bind(N1G_SOCK(fd), (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(N1G_SOCK(fd), 16) != 0 ||
        !set_nonblocking(fd)) {
        n1g_info(s, "web frontend failed to listen on 127.0.0.1:%u", (unsigned)port);
        close_fd(fd);
        return false;
    }

    web->listen_fd = fd;
    web->active = true;
    n1g_info(s, "web frontend listening on http://127.0.0.1:%u/", (unsigned)port);
    return true;
}

void n1g_web_poll(n1g_state_t *s, n1g_web_server_t *web, bool running) {
    if (!web || !web->active) {
        return;
    }
    if (s->counters.lcd_words != web->last_lcd_words) {
        web->last_lcd_words = s->counters.lcd_words;
        web->frame_seq++;
    }

    for (unsigned i = 0; i < 8u; i++) {
        intptr_t c = (intptr_t)accept(N1G_SOCK(web->listen_fd), NULL, NULL);
        if (c == N1G_WEB_INVALID_FD) {
            if (!would_block()) {
                n1g_info(s, "web frontend accept failed");
            }
            return;
        }
        (void)set_nonblocking(c);
        handle_client(s, web, c, running);
        close_fd(c);
    }
}

bool n1g_web_take_restart(n1g_web_server_t *web, char *preset, size_t preset_size) {
    if (!web || !web->restart_requested || preset_size == 0) {
        return false;
    }
    strncpy(preset, web->restart_preset, preset_size - 1u);
    preset[preset_size - 1u] = '\0';
    web->restart_requested = false;
    web->restart_preset[0] = '\0';
    web->last_lcd_words = UINT64_MAX;
    web->frame_seq++;
    return true;
}

void n1g_web_stop(n1g_web_server_t *web) {
    if (!web || !web->active) {
        return;
    }
    close_fd(web->listen_fd);
    web->listen_fd = N1G_WEB_INVALID_FD;
    web->active = false;
#ifdef _WIN32
    if (winsock_started) {
        WSACleanup();
        winsock_started = false;
    }
#endif
}
