#include "nano1g/web_frontend.h"

#include "nano1g/cpu_unicorn.h"
#include "nano1g/devices.h"
#include "nano1g/input_script.h"
#include "nano1g/map.h"
#include "nano1g/ram.h"
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
"#hardware-bar{align-items:center;display:flex;gap:12px;justify-content:center;margin:10px 0;font-size:12px;flex-wrap:wrap}"
"#hardware-bar label{align-items:center;display:flex;gap:4px;white-space:nowrap}"
"#battery-level{width:96px}"
"#audio-control{align-items:center;display:flex;gap:4px}"
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
"#ipod-container.held #ipod-clickwheel{opacity:.55}"
"#debug{font-size:12px;text-align:center;color:#57606a;line-height:1.6}"
"@media(max-width:520px){#ipod-container{transform:scale(1.35);height:540px}#stats{flex-direction:column;gap:8px}}"
"@media(max-width:380px){#ipod-container{transform:scale(1.15);height:460px}}"
"</style></head><body><div id=\"container\">"
"<h1>iPod Nano 1G</h1>"
"<div id=\"status\">Loading...</div>"
"<div id=\"firmware-bar\"><label>Image <select id=\"firmware-select\"><option value=\"apple-official\">Apple official boot</option><option value=\"apple-stage0\">Apple stage0 canary</option><option value=\"apple-direct\">Apple OS direct diagnostic</option><option value=\"rockbox\">Rockbox</option><option value=\"ipodlinux\">iPod Linux (experimental)</option></select></label><button id=\"restart-btn\" type=\"button\">Restart</button><label id=\"audio-control\"><input id=\"audio-enable\" type=\"checkbox\">Audio</label></div>"
"<div id=\"hardware-bar\"><label>Battery <input id=\"battery-level\" type=\"range\" min=\"0\" max=\"100\" value=\"100\"><output id=\"battery-value\">100%</output></label><label><input id=\"main-charger\" type=\"checkbox\">FireWire</label><label><input id=\"usb-charger\" type=\"checkbox\">USB power</label><label><input id=\"hold-switch\" type=\"checkbox\">Hold</label></div>"
"<div id=\"stats\"><span><b>FPS</b> <span id=\"fps\">0</span></span><span><b>guest</b> <span id=\"guest\">0</span></span><span><b>audio</b> <span id=\"audio\">0/0</span></span><span><b>input</b> <span id=\"input\">none</span></span></div>"
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
"<div id=\"debug\">state <span id=\"running\">...</span> &middot; lcd <span id=\"lcd\">0</span> &middot; light <span id=\"light\">default</span> &middot; disk <span id=\"disk\">0</span> &middot; pc <span id=\"pc\">0x00000000</span></div>"
"</div><script>"
"const fps_counter=document.getElementById('fps');"
"const canvas=document.getElementById('ipod-screen');"
"const ctx=canvas.getContext('2d');"
"const status_el=document.getElementById('status');"
"const firmware_select=document.getElementById('firmware-select');"
"const restart_btn=document.getElementById('restart-btn');"
"const audio_enable=document.getElementById('audio-enable');"
"const battery_level=document.getElementById('battery-level'),battery_value=document.getElementById('battery-value'),main_charger=document.getElementById('main-charger'),usb_charger=document.getElementById('usb-charger'),hold_switch=document.getElementById('hold-switch');"
"const ipod=document.getElementById('ipod-container');"
"let seq=-1,last_frame=0,wheel_down=false,last_angle=0,select_dirty=false,tick_inflight=false,hardware_timer=null,audio_ctx=null,audio_timer=null,audio_cursor=0,audio_latest=0,audio_next=0,audio_rate=0,audio_stream=-1,audio_polling=false,audio_sources=new Set(),clicker_seen=false,clicker_stops=0;"
"function set_status(message){status_el.textContent=message;}"
"function text(id,value){document.getElementById(id).textContent=value;}"
"async function send_input(url){try{await fetch(url,{cache:'no-store'});}catch(e){set_status('Input failed: '+e.message);}}"
"firmware_select.onchange=()=>{select_dirty=true;};"
"firmware_select.onkeydown=e=>{if(e.key==='Enter'){e.preventDefault();restart_selected();}};"
"async function restart_selected(){set_status('Restarting '+firmware_select.value+'...');seq=-1;select_dirty=false;try{const r=await fetch('/control?restart='+encodeURIComponent(firmware_select.value),{cache:'no-store'});if(!r.ok)set_status('Restart failed');}catch(e){set_status('Restart failed: '+e.message);}}"
"restart_btn.onclick=e=>{e.preventDefault();restart_selected();};"
"async function update_hardware(){const q=new URLSearchParams({battery:battery_level.value,main_charger:main_charger.checked?'1':'0',usb_charger:usb_charger.checked?'1':'0',hold:hold_switch.checked?'1':'0'});try{const r=await fetch('/hardware?'+q,{cache:'no-store'});if(!r.ok)set_status('Hardware update failed');}catch(e){set_status('Hardware update failed: '+e.message);}}"
"function queue_hardware(){if(hardware_timer)clearTimeout(hardware_timer);hardware_timer=setTimeout(update_hardware,80);}"
"battery_level.oninput=()=>{battery_value.value=battery_level.value+'%';queue_hardware();};main_charger.onchange=update_hardware;usb_charger.onchange=update_hardware;hold_switch.onchange=update_hardware;"
"function tap_url(name){return '/input?button='+encodeURIComponent(name)+'&tap=1';}"
"function bind_button(selector,name){const el=document.querySelector(selector);let pressed=false;"
"const down=e=>{e.preventDefault();pressed=true;el.classList.add('active');};"
"const up=e=>{e.preventDefault();if(!pressed)return;pressed=false;el.classList.remove('active');send_input(tap_url(name));};"
"el.onpointerdown=down;el.onpointerup=up;el.onpointercancel=up;el.onmouseleave=e=>{if(pressed)up(e);};el.onclick=e=>e.preventDefault();}"
"bind_button('#ipod-btn-menu','menu');bind_button('#ipod-btn-prev','left');bind_button('#ipod-btn-next','right');bind_button('#ipod-btn-play','play');bind_button('#ipod-btn-select','select');"
"function key_button(key){switch(key){case'ArrowUp':return'menu';case'ArrowLeft':return'left';case'ArrowRight':return'right';case'ArrowDown':return'play';case'Enter':return'select';default:return null;}}"
"let key_pressed=null;"
"ipod.onkeydown=e=>{const b=key_button(e.key);if(b&&key_pressed!==b){e.preventDefault();key_pressed=b;}};"
"ipod.onkeyup=e=>{const b=key_button(e.key);if(b&&key_pressed===b){e.preventDefault();key_pressed=null;send_input(tap_url(b));}};"
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
"function read_u64(v,o){return v.getUint32(o,true)+v.getUint32(o+4,true)*4294967296;}"
"function reset_audio_queue(){for(const src of audio_sources){try{src.stop();}catch(e){}}audio_sources.clear();if(audio_ctx)audio_next=audio_ctx.currentTime+.04;}"
"function play_clicker(period,duty,duration_ticks,rtc_scale,count){if(!audio_enable.checked||!audio_ctx||!count)return;const hz=93750/(period+1),duration=Math.min(1,Math.max(.001,duration_ticks*rtc_scale/1000000)),gain=Math.max(.01,.06*duty/255),limit=Math.min(count,8);for(let i=0;i<limit;i++){const when=audio_ctx.currentTime+.01+i*(duration+.003),osc=audio_ctx.createOscillator(),amp=audio_ctx.createGain();osc.type='square';osc.frequency.setValueAtTime(hz,when);amp.gain.setValueAtTime(gain,when);amp.gain.setValueAtTime(0,when+duration);osc.connect(amp);amp.connect(audio_ctx.destination);osc.onended=()=>{osc.disconnect();amp.disconnect();};osc.start(when);osc.stop(when+duration+.001);}}"
"async function pump_audio(){if(!audio_enable.checked||!audio_ctx||audio_polling||audio_next-audio_ctx.currentTime>.2)return;audio_polling=true;try{const requested=Math.floor(audio_cursor),r=await fetch('/audio.pcm?cursor='+requested,{cache:'no-store'}),b=await r.arrayBuffer();if(b.byteLength<32)return;const v=new DataView(b);if(v.getUint32(0,true)!==0x3141314e)return;const rate=v.getUint32(4,true),start=read_u64(v,8),next=read_u64(v,16),count=v.getUint32(24,true),stream=v.getUint32(28,true)>>>1;if(stream!==audio_stream||rate!==audio_rate||start!==requested){reset_audio_queue();audio_stream=stream;audio_rate=rate;}audio_cursor=next;if(count<2||b.byteLength<32+count*2)return;const frames=Math.floor(count/2),ab=audio_ctx.createBuffer(2,frames,rate),l=ab.getChannelData(0),rr=ab.getChannelData(1),pcm=new DataView(b,32);for(let i=0;i<frames;i++){l[i]=pcm.getInt16(i*4,true)/32768;rr[i]=pcm.getInt16(i*4+2,true)/32768;}const src=audio_ctx.createBufferSource();src.buffer=ab;src.connect(audio_ctx.destination);src.onended=()=>audio_sources.delete(src);audio_sources.add(src);let when=Math.max(audio_ctx.currentTime+.03,audio_next);if(when>audio_ctx.currentTime+.25)when=audio_ctx.currentTime+.03;src.start(when);audio_next=when+frames/rate;}catch(e){set_status('Audio failed: '+e.message);}finally{audio_polling=false;}}"
"audio_enable.onchange=async()=>{if(audio_enable.checked){const AC=window.AudioContext||window.webkitAudioContext;if(!AC){audio_enable.checked=false;return;}if(!audio_ctx)audio_ctx=new AC();await audio_ctx.resume();audio_cursor=audio_latest;reset_audio_queue();if(audio_timer)clearInterval(audio_timer);audio_timer=setInterval(pump_audio,40);pump_audio();}else{if(audio_timer)clearInterval(audio_timer);audio_timer=null;reset_audio_queue();if(audio_ctx)await audio_ctx.suspend();}};"
"async function draw_frame(frame_seq){const r=await fetch('/frame.rgba?'+frame_seq,{cache:'no-store'});const buf=await r.arrayBuffer();ctx.putImageData(new ImageData(new Uint8ClampedArray(buf),176,132),0,0);const now=performance.now();fps_counter.textContent=last_frame?Math.floor(1000/(now-last_frame)):'0';last_frame=now;}"
"async function tick(){if(tick_inflight)return;tick_inflight=true;try{const r=await fetch('/status.json',{cache:'no-store'});const s=await r.json();"
"set_status((s.running?'Running':'Stopped')+' - '+s.label);if(s.preset&&!select_dirty&&document.activeElement!==firmware_select)firmware_select.value=s.preset;if(document.activeElement!==battery_level){battery_level.value=s.battery_percent;battery_value.value=s.battery_percent+'%';}main_charger.checked=s.main_charger;usb_charger.checked=s.usb_charger;hold_switch.checked=s.hold;ipod.classList.toggle('held',s.hold);text('running',s.running?'running':'stopped');text('guest',s.guest_insns.toLocaleString());text('audio',(s.audio_output?'on ':'idle ')+s.audio_rate.toLocaleString());text('lcd',s.lcd_words.toLocaleString());text('light',(s.backlight_on?'on ':'off ')+s.backlight_level+'/32');text('disk',s.disk_reads.toLocaleString());text('input',s.input);text('pc',s.cpu_pc);if(s.audio_stream!==audio_stream||s.audio_rate!==audio_rate||s.audio_cursor<audio_latest){audio_cursor=s.audio_cursor;audio_stream=s.audio_stream;audio_rate=s.audio_rate;reset_audio_queue();}audio_latest=s.audio_cursor;if(!clicker_seen){clicker_stops=s.clicker_stops;clicker_seen=true;}else if(s.clicker_stops>clicker_stops){play_clicker(s.clicker_period,s.clicker_duty,s.clicker_last_ticks,s.rtc_usec_per_tick,s.clicker_stops-clicker_stops);clicker_stops=s.clicker_stops;}"
"if(s.frame_seq!==seq){await draw_frame(s.frame_seq);seq=s.frame_seq;}}catch(e){set_status('Offline');text('running','offline');}finally{tick_inflight=false;}}"
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

static void put64(uint8_t *p, uint64_t v) {
    put32(p, (uint32_t)v);
    put32(p + 4, (uint32_t)(v >> 32u));
}

static void rgb565(uint16_t p, uint8_t *r, uint8_t *g, uint8_t *b) {
    uint16_t raw = (uint16_t)((p >> 8u) | (p << 8u));
    *r = (uint8_t)(((raw >> 11) & 0x1fu) * 255u / 31u);
    *g = (uint8_t)(((raw >> 5) & 0x3fu) * 255u / 63u);
    *b = (uint8_t)((raw & 0x1fu) * 255u / 31u);
}

static void apply_backlight(n1g_state_t *s, uint8_t *r, uint8_t *g, uint8_t *b) {
    uint32_t intensity = n1g_dev_backlight_intensity(s);
    *r = (uint8_t)((*r * intensity + 127u) / 255u);
    *g = (uint8_t)((*g * intensity + 127u) / 255u);
    *b = (uint8_t)((*b * intensity + 127u) / 255u);
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
            apply_backlight(s, &r, &g, &b);
            uint8_t *px = dst + y * row + x * 3u;
            px[0] = b;
            px[1] = g;
            px[2] = r;
        }
    }

    *out_len = total;
    return bmp;
}

static uint8_t *make_rgba(n1g_state_t *s, size_t *out_len, bool apply_lighting) {
    const uint32_t pixels = N1G_LCD_W * N1G_LCD_H;
    uint8_t *rgba = (uint8_t *)malloc(pixels * 4u);
    if (!rgba) {
        return NULL;
    }

    for (uint32_t i = 0; i < pixels; i++) {
        uint8_t r = 0, g = 0, b = 0;
        rgb565(s->lcd2.pixels[i], &r, &g, &b);
        if (apply_lighting) {
            apply_backlight(s, &r, &g, &b);
        }
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
    if (strcmp(label, "Apple stage0 canary") == 0 ||
        strcmp(label, "Apple native boot") == 0 ||
        strcmp(label, "Apple stage0 OS") == 0) {
        preset = "apple-stage0";
    } else if (strcmp(label, "Apple OS direct diagnostic") == 0 ||
               strcmp(label, "Apple OS direct (blocked)") == 0 ||
               strcmp(label, "Apple official direct") == 0) {
        preset = "apple-direct";
    } else if (strcmp(label, "Apple official boot") == 0 ||
               strcmp(label, "Apple flash boot") == 0) {
        preset = "apple-official";
    } else if (strcmp(label, "Rockbox") == 0) {
        preset = "rockbox";
    } else if (strcmp(label, "iPod Linux (experimental)") == 0) {
        preset = "ipodlinux";
    }

    uint32_t opto_front = s->opto.queue_len ? s->opto.queue[s->opto.queue_head] : 0u;
    uint8_t rtc[7];
    n1g_dev_i2c_get_rtc(s, rtc);
    char body[16384];
    int n = snprintf(body,
                     sizeof(body),
                     "{\"running\":%s,\"frame_seq\":%llu,\"guest_insns\":%llu,"
                     "\"device_ticks\":%llu,\"rtc_usec_per_tick\":%u,"
                     "\"rtc_bcd\":\"%02x-%02x-%02xT%02x:%02x:%02x\","
                     "\"rtc_second_events\":%llu,\"rtc_alarm_events\":%llu,"
                     "\"lcd_words\":%llu,\"lcd_gram\":%llu,"
                     "\"lcd_block\":%llu,\"lcd_overruns\":%llu,\"lcd_blocks\":%llu,"
                     "\"dma_lcd_transfers\":%llu,\"lcd_dma_accepts\":%llu,"
                     "\"lcd_dma_mismatches\":%llu,\"lcd_dma_descriptor_pixels\":%llu,"
                     "\"lcd_dma_block_pixels\":%llu,\"disk_reads\":%llu,\"disk_writes\":%llu,"
                     "\"irq_count\":%llu,\"i2s_tx\":%llu,\"i2s_drained\":%llu,"
                     "\"dma_audio_starts\":%llu,\"dma_audio_done\":%llu,\"dma_audio_bytes\":%llu,"
                     "\"audio_cursor\":%llu,\"audio_rate\":%u,\"audio_stream\":%u,\"audio_stream_start\":%llu,\"audio_output\":%s,"
                     "\"audio_nonzero\":%llu,\"audio_silenced\":%llu,\"audio_peak\":%u,\"audio_underruns\":%llu,\"audio_underrun_samples\":%llu,\"audio_overruns\":%llu,\"audio_dropped\":%llu,"
                     "\"i2c_txns\":%llu,\"i2c_last\":\"addr=0x%02x op=%s count=%u data=0x%08x\","
                     "\"wm8975\":\"writes=%llu resets=%llu mode=%s output=%u muted=%u rate=%u control=0x%03x power=0x%03x out1=0x%03x/0x%03x\","
                     "\"input_events\":%llu,\"input_suppressed\":%llu,\"input\":\"%s\","
                     "\"battery_percent\":%u,\"main_charger\":%s,\"usb_charger\":%s,\"hold\":%s,"
                     "\"backlight_on\":%s,\"backlight_level\":%u,\"backlight_mode\":\"%s\","
                     "\"backlight_pwm\":\"0x%08x\",\"backlight_pulses\":%llu,"
                     "\"clicker_on\":%s,\"clicker_period\":%u,\"clicker_duty\":%u,"
                     "\"clicker_writes\":%llu,\"clicker_starts\":%llu,\"clicker_stops\":%llu,\"clicker_last_ticks\":%llu,"
                     "\"opto_queue\":%u,\"opto_front\":\"0x%08x\","
                     "\"opto_buttons\":\"0x%08x\",\"opto_regs04\":\"0x%08x\","
                     "\"intc_cpu\":\"0x%08x/0x%08x\",\"intc_hi_cpu\":\"0x%08x/0x%08x\","
                     "\"apple_input_hits\":\"%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\","
                     "\"apple_input_last\":\"raw=0x%08x buttons=0x%08x wheel=0x%08x lang_kind=0x%08x lang_w30=0x%08x accept=%llu select=%llu\","
                     "\"apple_input_task_hits\":\"%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\","
                     "\"apple_input_task_last\":\"wait_id=0x%08x woke_id=0x%08x keypost=0x%08x/0x%08x queue=0x%08x payload=0x%08x ui_kind=0x%08x ui_code=0x%08x\","
                     "\"apple_key_gate\":\"writes=%llu pc=0x%08x addr=0x%08x size=%u value=0x%08x bytes68=0x%08x bytes6c=0x%08x\","
                     "\"apple_ui_ready\":\"h=%llu,%llu,%llu,%llu,%llu bytes68=0x%08x bytes6c=0x%08x lr=0x%08x\","
                     "\"apple_work_pool\":\"h=%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu head=0x%08x words=0x%08x,0x%08x,0x%08x,0x%08x branch_obj=0x%08x stale_r1=0x%08x sub=0x%08x handler=0x%08x lr=0x%08x\","
                     "\"apple_ui_branch\":\"h=%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu obj=0x%08x objw=0x%08x,0x%08x,0x%08x,0x%08x dispatch=0x%08x/0x%08x select=0x%08x/0x%08x accept=0x%08x/0x%08x\","
                     "\"apple_ui_dispatch\":\"h=%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu obj=0x%08x objw=0x%08x,0x%08x,0x%08x,0x%08x vt=0x%08x,0x%08x,0x%08x,0x%08x objtab=0x%08x/0x%08x lr=0x%08x\","
                     "\"apple_preferences_hits\":[%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu],"
                     "\"apple_power_hits\":[%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu],"
                     "\"apple_shutdown_guard\":[%u,%u,%u],"
                     "\"apple_shutdown_gate\":[%u,%u,%u],"
                     "\"apple_pwrp\":[%u,%u,%u,%u,%u],"
                     "\"apple_power_event_types\":[%u,%u,%u],"
                     "\"apple_power_timer\":[%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u],"
                     "\"apple_power_state\":[%u,%u,%u,%u,%u,%u],"
                     "\"label\":\"%s\",\"preset\":\"%s\","
                     "\"cpu_pc\":\"0x%08x\"}\n",
                     running ? "true" : "false",
                     (unsigned long long)web->frame_seq,
                     (unsigned long long)s->counters.guest_insns,
                     (unsigned long long)s->counters.device_ticks,
                     s->opts.rtc_usec_per_tick,
                     rtc[6], rtc[5], rtc[4], rtc[2], rtc[1], rtc[0],
                     (unsigned long long)s->i2c.rtc_second_interrupts,
                     (unsigned long long)s->i2c.rtc_alarm_interrupts,
                     (unsigned long long)s->counters.lcd_words,
                     (unsigned long long)s->lcd2.gram_pixels,
                     (unsigned long long)s->lcd2.block_pixels,
                     (unsigned long long)s->lcd2.block_overrun_words,
                     (unsigned long long)s->lcd2.block_starts,
                     (unsigned long long)(s->dma.lcd_transfers[0] + s->dma.lcd_transfers[1] +
                                          s->dma.lcd_transfers[2] + s->dma.lcd_transfers[3]),
                     (unsigned long long)s->dma.lcd_geometry_accepts,
                     (unsigned long long)s->dma.lcd_geometry_mismatches,
                     (unsigned long long)s->dma.lcd_descriptor_pixels,
                     (unsigned long long)s->dma.lcd_block_pixels,
                     (unsigned long long)s->counters.disk_reads,
                     (unsigned long long)s->counters.disk_writes,
                     (unsigned long long)s->counters.irq_count,
                     (unsigned long long)s->i2s.tx_halfwords,
                     (unsigned long long)s->i2s.tx_drained_halfwords,
                     (unsigned long long)(s->dma.ch[0].starts + s->dma.ch[1].starts + s->dma.ch[2].starts + s->dma.ch[3].starts),
                     (unsigned long long)(s->dma.ch[0].completions + s->dma.ch[1].completions + s->dma.ch[2].completions + s->dma.ch[3].completions),
                     (unsigned long long)(s->dma.ch[0].bytes_pushed + s->dma.ch[1].bytes_pushed + s->dma.ch[2].bytes_pushed + s->dma.ch[3].bytes_pushed),
                     (unsigned long long)s->i2s.pcm_produced_halfwords,
                     s->i2s.pcm_sample_rate != 0u ? s->i2s.pcm_sample_rate : 44100u,
                     s->i2s.pcm_stream_id,
                     (unsigned long long)s->i2s.pcm_stream_start_halfword,
                     s->i2c.wm8975_output_enabled ? "true" : "false",
                     (unsigned long long)s->i2s.pcm_nonzero_halfwords,
                     (unsigned long long)s->i2s.pcm_silenced_halfwords,
                     s->i2s.pcm_peak,
                     (unsigned long long)s->i2s.underruns,
                     (unsigned long long)s->i2s.underrun_halfwords,
                     (unsigned long long)s->i2s.tx_overruns,
                     (unsigned long long)s->i2s.host_dropped_halfwords,
                     (unsigned long long)s->i2c.transactions,
                     s->i2c.last_addr,
                     s->i2c.last_read ? "read" : "write",
                     s->i2c.last_count,
                     s->i2c.last_data,
                     (unsigned long long)s->i2c.addr_writes[0x1au],
                     (unsigned long long)s->i2c.wm8975_resets,
                     s->i2c.wm8975_legacy_mode ? "legacy" : "native",
                     s->i2c.wm8975_output_enabled ? 1u : 0u,
                     (s->i2c.wm8975_regs[0x05u] & (1u << 3u)) != 0u ? 1u : 0u,
                     s->i2c.wm8975_sample_rate,
                     (unsigned)s->i2c.wm8975_regs[0x08u],
                     (unsigned)s->i2c.wm8975_regs[s->i2c.wm8975_legacy_mode ? 0x06u : 0x1au],
                     (unsigned)s->i2c.wm8975_regs[0x02u],
                     (unsigned)s->i2c.wm8975_regs[0x03u],
                     (unsigned long long)s->opto.input_events,
                     (unsigned long long)s->opto.suppressed_events,
                     s->opto.last_input[0] ? s->opto.last_input : "none",
                     s->opts.battery_percent,
                     s->opts.main_charger_connected ? "true" : "false",
                     s->opts.usb_charger_connected ? "true" : "false",
                     s->opts.hold_switch_engaged ? "true" : "false",
                     n1g_dev_backlight_powered(s) ? "true" : "false",
                     n1g_dev_backlight_level(s),
                     n1g_dev_backlight_mode(s),
                     s->backlight.pwm_regs[0x10u / 4u],
                     (unsigned long long)s->backlight.dimmer_pulses,
                     s->backlight.clicker_enabled ? "true" : "false",
                     (unsigned)s->backlight.clicker_period,
                     (unsigned)s->backlight.clicker_duty,
                     (unsigned long long)s->backlight.clicker_writes,
                     (unsigned long long)s->backlight.clicker_starts,
                     (unsigned long long)s->backlight.clicker_stops,
                     (unsigned long long)s->backlight.clicker_last_duration_ticks,
                     (unsigned)s->opto.queue_len,
                     opto_front,
                     s->opto.button_bits,
                     s->opto.regs[0x04u / 4u],
                     s->intc.cpu_status,
                     s->intc.cpu_enable,
                     s->intc.hi_cpu_status,
                     s->intc.hi_cpu_enable,
                     (unsigned long long)s->counters.apple_input_hits[0],
                     (unsigned long long)s->counters.apple_input_hits[1],
                     (unsigned long long)s->counters.apple_input_hits[2],
                     (unsigned long long)s->counters.apple_input_hits[3],
                     (unsigned long long)s->counters.apple_input_hits[4],
                     (unsigned long long)s->counters.apple_input_hits[5],
                     (unsigned long long)s->counters.apple_input_hits[6],
                     (unsigned long long)s->counters.apple_input_hits[7],
                     (unsigned long long)s->counters.apple_input_hits[8],
                     (unsigned long long)s->counters.apple_input_hits[9],
                     (unsigned long long)s->counters.apple_input_hits[10],
                     (unsigned long long)s->counters.apple_input_hits[11],
                     s->counters.apple_input_last[0][7],
                     s->counters.apple_input_last[1][5],
                     s->counters.apple_input_last[1][6],
                     s->counters.apple_input_last[7][5],
                     s->counters.apple_input_last[7][7],
                     (unsigned long long)s->counters.apple_input_hits[9],
                     (unsigned long long)s->counters.apple_input_hits[10],
                     (unsigned long long)s->counters.apple_input_task_hits[0],
                     (unsigned long long)s->counters.apple_input_task_hits[1],
                     (unsigned long long)s->counters.apple_input_task_hits[2],
                     (unsigned long long)s->counters.apple_input_task_hits[3],
                     (unsigned long long)s->counters.apple_input_task_hits[4],
                     (unsigned long long)s->counters.apple_input_task_hits[5],
                     (unsigned long long)s->counters.apple_input_task_hits[6],
                     (unsigned long long)s->counters.apple_input_task_hits[7],
                     (unsigned long long)s->counters.apple_input_task_hits[8],
                     (unsigned long long)s->counters.apple_input_task_hits[9],
                     (unsigned long long)s->counters.apple_input_task_hits[10],
                     (unsigned long long)s->counters.apple_input_task_hits[11],
                     (unsigned long long)s->counters.apple_input_task_hits[12],
                     (unsigned long long)s->counters.apple_input_task_hits[13],
                     (unsigned long long)s->counters.apple_input_task_hits[14],
                     (unsigned long long)s->counters.apple_input_task_hits[15],
                     s->counters.apple_input_task_last[1][1],
                     s->counters.apple_input_task_last[2][1],
                     s->counters.apple_input_task_last[3][0],
                     s->counters.apple_input_task_last[3][1],
                     s->counters.apple_input_task_last[14][0],
                     s->counters.apple_input_task_last[14][1],
                     s->counters.apple_input_task_last[15][6],
                     s->counters.apple_input_task_last[15][7],
                     (unsigned long long)s->counters.apple_key_gate_writes,
                     s->counters.apple_key_gate_last[0],
                     s->counters.apple_key_gate_last[1],
                     s->counters.apple_key_gate_last[2],
                     s->counters.apple_key_gate_last[3],
                     s->counters.apple_key_gate_last[7],
                     s->counters.apple_key_gate_bytes,
                     (unsigned long long)s->counters.apple_ui_ready_hits[0],
                     (unsigned long long)s->counters.apple_ui_ready_hits[1],
                     (unsigned long long)s->counters.apple_ui_ready_hits[2],
                     (unsigned long long)s->counters.apple_ui_ready_hits[3],
                     (unsigned long long)s->counters.apple_ui_ready_hits[4],
                     s->counters.apple_ui_ready_bytes68,
                     s->counters.apple_ui_ready_bytes6c,
                     s->counters.apple_ui_ready_last[3][7],
                     (unsigned long long)s->counters.apple_work_pool_hits[0],
                     (unsigned long long)s->counters.apple_work_pool_hits[1],
                     (unsigned long long)s->counters.apple_work_pool_hits[2],
                     (unsigned long long)s->counters.apple_work_pool_hits[3],
                     (unsigned long long)s->counters.apple_work_pool_hits[4],
                     (unsigned long long)s->counters.apple_work_pool_hits[5],
                     (unsigned long long)s->counters.apple_work_pool_hits[6],
                     (unsigned long long)s->counters.apple_work_pool_hits[7],
                     s->counters.apple_work_pool_head,
                     s->counters.apple_work_pool_words[0],
                     s->counters.apple_work_pool_words[1],
                     s->counters.apple_work_pool_words[2],
                     s->counters.apple_work_pool_words[3],
                     s->counters.apple_work_pool_last[3][0],
                     s->counters.apple_work_pool_last[3][1],
                     s->counters.apple_work_pool_last[3][4],
                     s->counters.apple_work_pool_last[7][3],
                     s->counters.apple_work_pool_last[3][7],
                     (unsigned long long)s->counters.apple_ui_branch_hits[0],
                     (unsigned long long)s->counters.apple_ui_branch_hits[1],
                     (unsigned long long)s->counters.apple_ui_branch_hits[2],
                     (unsigned long long)s->counters.apple_ui_branch_hits[3],
                     (unsigned long long)s->counters.apple_ui_branch_hits[4],
                     (unsigned long long)s->counters.apple_ui_branch_hits[5],
                     (unsigned long long)s->counters.apple_ui_branch_hits[6],
                     (unsigned long long)s->counters.apple_ui_branch_hits[7],
                     s->counters.apple_ui_branch_last[0][0],
                     s->counters.apple_ui_branch_words[0][0],
                     s->counters.apple_ui_branch_words[0][1],
                     s->counters.apple_ui_branch_words[0][2],
                     s->counters.apple_ui_branch_words[0][3],
                     s->counters.apple_ui_branch_last[2][0],
                     s->counters.apple_ui_branch_last[2][4],
                     s->counters.apple_ui_branch_words[3][3],
                     s->counters.apple_ui_branch_last[3][3],
                     s->counters.apple_ui_branch_words[4][3],
                     s->counters.apple_ui_branch_last[4][3],
                     (unsigned long long)s->counters.apple_ui_dispatch_hits[0],
                     (unsigned long long)s->counters.apple_ui_dispatch_hits[1],
                     (unsigned long long)s->counters.apple_ui_dispatch_hits[2],
                     (unsigned long long)s->counters.apple_ui_dispatch_hits[3],
                     (unsigned long long)s->counters.apple_ui_dispatch_hits[4],
                     (unsigned long long)s->counters.apple_ui_dispatch_hits[5],
                     (unsigned long long)s->counters.apple_ui_dispatch_hits[6],
                     (unsigned long long)s->counters.apple_ui_dispatch_hits[7],
                     s->counters.apple_ui_dispatch_last[2][0],
                     s->counters.apple_ui_dispatch_words[2][0],
                     s->counters.apple_ui_dispatch_words[2][1],
                     s->counters.apple_ui_dispatch_words[2][2],
                     s->counters.apple_ui_dispatch_words[2][3],
                     s->counters.apple_ui_dispatch_words[2][4],
                     s->counters.apple_ui_dispatch_words[2][5],
                     s->counters.apple_ui_dispatch_words[2][6],
                     s->counters.apple_ui_dispatch_words[2][7],
                     s->counters.apple_ui_dispatch_last[3][0],
                     s->counters.apple_ui_dispatch_last[3][1],
                     s->counters.apple_ui_dispatch_last[3][7],
                     (unsigned long long)s->counters.apple_preferences_hits[0],
                     (unsigned long long)s->counters.apple_preferences_hits[1],
                     (unsigned long long)s->counters.apple_preferences_hits[2],
                     (unsigned long long)s->counters.apple_preferences_hits[3],
                     (unsigned long long)s->counters.apple_preferences_hits[4],
                     (unsigned long long)s->counters.apple_preferences_hits[5],
                     (unsigned long long)s->counters.apple_preferences_hits[6],
                     (unsigned long long)s->counters.apple_preferences_hits[7],
                     (unsigned long long)s->counters.apple_preferences_hits[8],
                     (unsigned long long)s->counters.apple_power_hits[0],
                     (unsigned long long)s->counters.apple_power_hits[1],
                     (unsigned long long)s->counters.apple_power_hits[2],
                     (unsigned long long)s->counters.apple_power_hits[3],
                     (unsigned long long)s->counters.apple_power_hits[4],
                     (unsigned long long)s->counters.apple_power_hits[5],
                     (unsigned long long)s->counters.apple_power_hits[6],
                     (unsigned long long)s->counters.apple_power_hits[7],
                     (unsigned long long)s->counters.apple_power_hits[8],
                     (unsigned long long)s->counters.apple_power_hits[9],
                     (unsigned long long)s->counters.apple_power_hits[10],
                     (unsigned long long)s->counters.apple_power_hits[11],
                     (unsigned long long)s->counters.apple_power_hits[12],
                     (unsigned long long)s->counters.apple_power_hits[13],
                     (unsigned long long)s->counters.apple_power_hits[14],
                     (unsigned long long)s->counters.apple_power_hits[15],
                     (unsigned long long)s->counters.apple_power_hits[16],
                     (unsigned long long)s->counters.apple_power_hits[17],
                     (unsigned long long)s->counters.apple_power_hits[18],
                     (unsigned long long)s->counters.apple_power_hits[19],
                     (unsigned long long)s->counters.apple_power_hits[20],
                     (unsigned long long)s->counters.apple_power_hits[21],
                     (unsigned long long)s->counters.apple_power_hits[22],
                     (unsigned long long)s->counters.apple_power_hits[23],
                     (unsigned long long)s->counters.apple_power_hits[24],
                     (unsigned long long)s->counters.apple_power_hits[25],
                     (unsigned long long)s->counters.apple_power_hits[26],
                     (unsigned long long)s->counters.apple_power_hits[27],
                     (unsigned long long)s->counters.apple_power_hits[28],
                     (unsigned long long)s->counters.apple_power_hits[29],
                     (unsigned long long)s->counters.apple_power_hits[30],
                     (unsigned long long)s->counters.apple_power_hits[31],
                     (unsigned long long)s->counters.apple_power_hits[32],
                     (unsigned long long)s->counters.apple_power_hits[33],
                     s->counters.apple_power_last[23][0],
                     s->counters.apple_power_last[24][0],
                     s->counters.apple_power_last[25][0],
                     s->counters.apple_power_last[27][1],
                     s->counters.apple_power_last[28][1],
                     s->counters.apple_power_last[29][1],
                     s->counters.apple_power_last[30][0],
                     s->counters.apple_power_last[31][0],
                     s->counters.apple_power_last[32][2],
                     s->counters.apple_power_last[32][3],
                     s->counters.apple_power_last[33][5],
                     s->counters.apple_power_last[0][4],
                     s->counters.apple_power_last[1][4],
                     s->counters.apple_power_last[2][4],
                     s->counters.apple_power_last[11][0],
                     s->counters.apple_power_last[11][1],
                     s->counters.apple_power_last[12][0],
                     s->counters.apple_power_last[13][0],
                     s->counters.apple_power_last[13][1],
                     s->counters.apple_power_last[13][6],
                     s->counters.apple_power_last[14][0],
                     s->counters.apple_power_last[16][4],
                     s->counters.apple_power_last[16][5],
                     s->counters.apple_power_last[17][0],
                     s->counters.apple_power_last[18][0],
                     s->counters.apple_power_last[21][2],
                     s->counters.apple_power_last[21][4],
                     s->counters.apple_power_last[21][5],
                     s->counters.apple_power_last[22][0],
                     s->counters.apple_power_last[22][5],
                     s->counters.apple_power_last[22][6],
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

static bool query_u32(const char *query, const char *key, uint32_t *out) {
    if (!query || !key || !out) {
        return false;
    }
    const char *p = strstr(query, key);
    if (!p) {
        return false;
    }
    p += strlen(key);
    if (*p != '=') {
        return false;
    }
    char *end = NULL;
    unsigned long value = strtoul(p + 1, &end, 0);
    if (end == p + 1) {
        return false;
    }
    *out = (uint32_t)value;
    return true;
}

static bool query_u64(const char *query, const char *key, uint64_t *out) {
    if (!query || !key || !out) {
        return false;
    }
    const char *p = strstr(query, key);
    if (!p) {
        return false;
    }
    p += strlen(key);
    if (*p != '=') {
        return false;
    }
    char *end = NULL;
    unsigned long long value = strtoull(p + 1, &end, 0);
    if (end == p + 1) {
        return false;
    }
    *out = (uint64_t)value;
    return true;
}

static bool send_audio_pcm(n1g_state_t *s, intptr_t fd, const char *query) {
    uint64_t produced = s->i2s.pcm_produced_halfwords & ~1ull;
    uint64_t cursor = produced;
    (void)query_u64(query, "cursor", &cursor);
    uint64_t oldest = produced > N1G_AUDIO_RING_HALFWORDS ?
                      produced - N1G_AUDIO_RING_HALFWORDS : 0u;
    if (oldest < s->i2s.pcm_stream_start_halfword) {
        oldest = s->i2s.pcm_stream_start_halfword;
    }
    oldest = (oldest + 1u) & ~1ull;
    if (cursor < oldest) {
        s->i2s.host_dropped_halfwords += oldest - cursor;
        cursor = oldest;
    }
    if (cursor > produced) {
        cursor = produced;
    }
    cursor = (cursor + 1u) & ~1ull;
    uint64_t available = produced - cursor;
    if (available > 16384u) {
        s->i2s.host_dropped_halfwords += available - 16384u;
        cursor = produced - 16384u;
        available = 16384u;
    }

    uint32_t count = (uint32_t)available;
    size_t body_len = 32u + (size_t)count * 2u;
    uint8_t *body = (uint8_t *)malloc(body_len);
    if (!body) {
        const char msg[] = "out of memory\n";
        return send_response(fd, "500 Internal Server Error", "text/plain", msg, sizeof(msg) - 1u);
    }
    put32(body, 0x3141314eu); /* N1A1 */
    put32(body + 4, s->i2s.pcm_sample_rate != 0u ? s->i2s.pcm_sample_rate : 44100u);
    put64(body + 8, cursor);
    put64(body + 16, cursor + count);
    put32(body + 24, count);
    put32(body + 28, (s->i2s.pcm_stream_id << 1u) |
                     (s->i2c.wm8975_output_enabled ? 1u : 0u));
    for (uint32_t i = 0; i < count; i++) {
        put16(body + 32u + i * 2u,
              (uint16_t)s->i2s.pcm_ring[(cursor + i) % N1G_AUDIO_RING_HALFWORDS]);
    }
    bool ok = send_response(fd, "200 OK", "application/octet-stream", body, body_len);
    free(body);
    return ok;
}

static bool send_dump32(n1g_state_t *s, intptr_t fd, const char *query) {
    uint32_t addr = 0;
    uint32_t count = 16;
    if (!query_u32(query, "addr", &addr)) {
        const char msg[] = "{\"ok\":false,\"error\":\"missing addr\"}\n";
        return send_response(fd, "400 Bad Request", "application/json", msg, sizeof(msg) - 1u);
    }
    (void)query_u32(query, "count", &count);
    if (count == 0 || count > 64u) {
        count = 16;
    }

    char body[2048];
    int n = snprintf(body, sizeof(body), "{\"addr\":\"0x%08x\",\"words\":[", addr);
    for (uint32_t i = 0; i < count && n > 0 && (size_t)n < sizeof(body); i++) {
        uint32_t value = 0;
        (void)n1g_ram_read(s, addr + i * 4u, 4, &value);
        n += snprintf(body + n,
                      sizeof(body) - (size_t)n,
                      "%s\"0x%08x\"",
                      i ? "," : "",
                      value);
    }
    if (n > 0 && (size_t)n < sizeof(body)) {
        n += snprintf(body + n, sizeof(body) - (size_t)n, "]}\n");
    }
    if (n <= 0 || (size_t)n >= sizeof(body)) {
        const char msg[] = "{\"ok\":false,\"error\":\"response too large\"}\n";
        return send_response(fd, "500 Internal Server Error", "application/json", msg, sizeof(msg) - 1u);
    }
    return send_response(fd, "200 OK", "application/json", body, (size_t)n);
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
                if (query_has(query, "tap=1")) {
                    ok = n1g_dev_opto_tap(s, buttons[i], N1G_INPUT_HOLD_TICKS);
                } else {
                    ok = n1g_dev_opto_button(s, buttons[i], !query_has(query, "state=up"));
                }
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

static bool send_hardware(n1g_state_t *s, intptr_t fd, const char *query) {
    uint32_t battery = s->opts.battery_percent;
    uint32_t main_charger = s->opts.main_charger_connected ? 1u : 0u;
    uint32_t usb_charger = s->opts.usb_charger_connected ? 1u : 0u;
    uint32_t hold = s->opts.hold_switch_engaged ? 1u : 0u;
    uint32_t rtc_usec_per_tick = s->opts.rtc_usec_per_tick;
    bool touched = false;
    bool valid = true;

    if (query_has(query, "battery=")) {
        touched = true;
        valid = query_u32(query, "battery", &battery) && battery <= 100u;
    }
    if (valid && query_has(query, "main_charger=")) {
        touched = true;
        valid = query_u32(query, "main_charger", &main_charger) && main_charger <= 1u;
    }
    if (valid && query_has(query, "usb_charger=")) {
        touched = true;
        valid = query_u32(query, "usb_charger", &usb_charger) && usb_charger <= 1u;
    }
    if (valid && query_has(query, "hold=")) {
        touched = true;
        valid = query_u32(query, "hold", &hold) && hold <= 1u;
    }
    if (valid && query_has(query, "rtc_usec_per_tick=")) {
        touched = true;
        valid = query_u32(query, "rtc_usec_per_tick", &rtc_usec_per_tick) &&
                rtc_usec_per_tick >= 1u && rtc_usec_per_tick <= 4096u;
    }
    if (!touched || !valid) {
        const char msg[] = "{\"ok\":false}\n";
        return send_response(fd, "400 Bad Request", "application/json", msg, sizeof(msg) - 1u);
    }

    s->opts.battery_percent = battery;
    s->opts.rtc_usec_per_tick = rtc_usec_per_tick;
    (void)n1g_dev_gpio_set_chargers(s, main_charger != 0u, usb_charger != 0u);
    (void)n1g_dev_gpio_set_hold(s, hold != 0u);

    char body[192];
    int n = snprintf(body,
                     sizeof(body),
                     "{\"ok\":true,\"battery_percent\":%u,\"main_charger\":%s,\"usb_charger\":%s,\"hold\":%s,\"rtc_usec_per_tick\":%u}\n",
                     s->opts.battery_percent,
                     s->opts.main_charger_connected ? "true" : "false",
                     s->opts.usb_charger_connected ? "true" : "false",
                     s->opts.hold_switch_engaged ? "true" : "false",
                     s->opts.rtc_usec_per_tick);
    if (n <= 0 || (size_t)n >= sizeof(body)) {
        return false;
    }
    return send_response(fd, "200 OK", "application/json", body, (size_t)n);
}

static bool valid_restart_preset(const char *preset) {
    return strcmp(preset, "rockbox") == 0 ||
           strcmp(preset, "ipodlinux") == 0 ||
           strcmp(preset, "apple-direct") == 0 ||
           strcmp(preset, "apple-official") == 0 ||
           strcmp(preset, "apple-flash") == 0 ||
           strcmp(preset, "apple-stage0") == 0 ||
           strcmp(preset, "apple-native") == 0;
}

static const char *restart_preset_from_query(const char *query) {
    if (query_has(query, "restart=rockbox")) return "rockbox";
    if (query_has(query, "restart=ipodlinux")) return "ipodlinux";
    if (query_has(query, "restart=apple-direct")) return "apple-direct";
    if (query_has(query, "restart=apple-official")) return "apple-official";
    if (query_has(query, "restart=apple-flash")) return "apple-flash";
    if (query_has(query, "restart=apple-stage0")) return "apple-stage0";
    if (query_has(query, "restart=apple-native")) return "apple-native";
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
    } else if (strcmp(path, "/dump32") == 0) {
        (void)send_dump32(s, fd, query ? query : "");
    } else if (strcmp(path, "/input") == 0) {
        (void)send_input(s, fd, query ? query : "");
    } else if (strcmp(path, "/hardware") == 0) {
        (void)send_hardware(s, fd, query ? query : "");
    } else if (strcmp(path, "/control") == 0) {
        (void)send_control(web, fd, query ? query : "");
    } else if (strcmp(path, "/audio.pcm") == 0) {
        (void)send_audio_pcm(s, fd, query ? query : "");
    } else if (strcmp(path, "/frame.rgba") == 0) {
        size_t len = 0;
        uint8_t *rgba = make_rgba(s, &len, !query_has(query, "raw=1"));
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
    web->last_backlight_generation = UINT64_MAX;

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
    if (s->counters.lcd_words != web->last_lcd_words ||
        s->backlight.generation != web->last_backlight_generation) {
        web->last_lcd_words = s->counters.lcd_words;
        web->last_backlight_generation = s->backlight.generation;
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
    web->last_backlight_generation = UINT64_MAX;
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
