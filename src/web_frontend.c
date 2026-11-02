#define _POSIX_C_SOURCE 200809L

#include "nano1g/web_frontend.h"

#include "nano1g/cpu_unicorn.h"
#include "nano1g/devices.h"
#include "nano1g/input_script.h"
#include "nano1g/map.h"
#include "nano1g/ram.h"
#include "nano1g/trace.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <stdatomic.h>

#define N1G_WEB_INVALID_FD ((intptr_t)-1)
#define N1G_WEB_MAX_CONN 16
#define N1G_WEB_CMD_POOL 48
#define N1G_WEB_SESSIONS 8
#define N1G_WEB_FRAME_MS 16u
#define N1G_WEB_LONGPOLL_MS 1000u
#define N1G_WEB_SESSION_MS 700u
#define N1G_WEB_PIXELS (N1G_LCD_W * N1G_LCD_H)
#define N1G_WEB_WHEEL_CLAMP 384

#ifdef _WIN32
#define N1G_SOCK(fd) ((SOCKET)(uintptr_t)(fd))
#else
#define N1G_SOCK(fd) ((int)(fd))
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#ifdef _WIN32
typedef WSAPOLLFD n1g_pfd;
#else
typedef struct pollfd n1g_pfd;
#endif

enum {
    ST_READ = 0,
    ST_WRITE,
    ST_WAIT_CMD,
    ST_WAIT_FRAME,
    ST_WATCH
};

enum {
    CMD_BUTTON = 1,
    CMD_WHEEL,
    CMD_RELEASE,
    CMD_HARDWARE,
    CMD_DISPLAY,
    CMD_AUDIO,
    CMD_DUMP,
    CMD_CONTROL
};

struct n1g_web_cmd {
    int type;
    bool in_use;
    bool done;
    bool abandoned;
    bool noreply;
    int http_status;
    char content_type[40];
    char inline_body[512];
    uint8_t *heap_body;
    uint8_t *body;
    size_t body_len;
    char button[16];
    bool pressed;
    bool tap;
    int wheel_delta;
    uint32_t sid;
    bool has_battery, has_main, has_usb, has_hold, has_rtc;
    uint32_t battery, main_charger, usb_charger, hold, rtc_usec;
    uint32_t force_backlight;
    uint64_t audio_cursor;
    uint32_t dump_addr, dump_count;
    struct n1g_web_cmd *next;
};

struct n1g_web_session {
    bool used;
    uint32_t id;
    uint32_t mask;
    uint64_t last_ms;
    uint32_t watch_gen;
    bool watch_open;
};

struct n1g_web_conn {
    intptr_t fd;
    int state;
    bool keep_alive;
    bool close_after;
    bool watching;
    uint32_t watch_sid;
    uint32_t watch_gen;
    char inbuf[8192];
    size_t in_len;
    char *out;
    size_t out_len;
    size_t out_off;
    struct n1g_web_cmd *cmd;
    uint64_t frame_since;
    uint64_t deadline_ms;
    bool frame_raw;
    bool frame_bmp;
    bool dead;
};

struct n1g_web_impl {
    n1g_web_server_t *web;
#ifdef _WIN32
    CRITICAL_SECTION mu;
    HANDLE thread;
#else
    pthread_mutex_t mu;
    pthread_t thread;
#endif
    int wake_r;
    int wake_w;
    atomic_int urgent;
    bool stop;
    bool thread_started;
    uint16_t pixels[N1G_WEB_PIXELS];
    uint16_t staging[N1G_WEB_PIXELS];
    uint32_t intensity;
    bool force_fullbright;
    uint64_t frame_seq;
    uint64_t last_publish_ms;
    uint64_t status_ms;
    bool frame_dirty;
    bool force_publish;
    char status[32768];
    size_t status_len;
    struct n1g_web_session sessions[N1G_WEB_SESSIONS];
    struct n1g_web_conn conns[N1G_WEB_MAX_CONN];
    int nconns;
    struct n1g_web_cmd cmds[N1G_WEB_CMD_POOL];
    struct n1g_web_cmd *q_head;
    struct n1g_web_cmd *q_tail;
    char *index_html;
    size_t index_len;
    char bind_text[64];
};

/* HTML page is emitted as separate literals so each stays under the C limit. */
static const char html_0[] = "<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n<meta name=\"viewport\" content=\"width=device-width,initial-scale=1,viewport-fit=cover\">\n<meta name=\"theme-color\" content=\"#c5c8ce\">\n<meta name=\"apple-mobile-web-app-capable\" content=\"yes\">\n<meta name=\"mobile-web-app-capable\" content=\"yes\">\n<title>iPod nano</title>\n<style>\n:root{color-scheme:light;--desk:#c5c8ce;--ink:#1c1f24;--muted:#5c6370}\n*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}\nhtml,body{height:100%;margin:0}\nbody{background:radial-gradient(1200px 700px at 50% -10%,#f7f8fa,var(--desk) 55%,#aeb3bb);color:var(--ink);font-family:\"Helvetica Neue\",Helvetica,Arial,sans-serif;overflow:hidden;overscroll-behavior:none;user-select:none;-webkit-user-select:none}\nbutton,input,select{font:inherit}\n.app{height:100%;height:100dvh;display:flex;flex-direction:column;padding:max(10px,env(safe-area-inset-top)) max(12px,env(safe-area-inset-right)) max(10px,env(safe-area-inset-bottom)) max(12px,env(safe-area-inset-left));gap:8px}\n.top{display:flex;flex-direction:column;align-items:center;gap:4px;flex:0 0 auto}\nh1{font-size:15px;font-weight:600;letter-spacing:.12em;text-transform:lowercase;margin:0;color:#2a2e34}\nh1 span{letter-spacing:0;font-weight:500}\n#status{font-size:12px;color:var(--muted);min-height:1.2em;text-align:center}\n#alert{display:none;max-width:440px;background:#fff4f2;color:#8a2118;border:1px solid #f0c2bb;border-radius:10px;padding:8px 12px;font-size:13px;text-align:center;line-height:1.35}\n#alert.show{display:block}\n.stage{flex:1 1 auto;min-height:0;display:grid;place-items:center;width:1";
static const char html_1[] = "00%}\n#ipod{height:min(100%,780px);width:auto;max-width:min(100%,360px);aspect-ratio:40/90;filter:drop-shadow(0 16px 28px rgba(30,36,48,.18))}\n#ipod-body{height:100%;border-radius:14%/6.2%;background:linear-gradient(180deg,#fff 0%,#f7f7f5 38%,#ecebe6 100%);box-shadow:inset 0 1px 0 #fff,inset 0 0 0 1px rgba(255,255,255,.85),inset 0 -12px 20px rgba(0,0,0,.03);position:relative;display:flex;flex-direction:column;align-items:center;padding:7.2% 0 6%}\n#screen-well{width:78%;background:#070809;border-radius:3px;padding:3.4% 3.2% 4%;box-shadow:inset 0 0 0 1px #2c2e32,0 1px 0 rgba(255,255,255,.65)}\n#ipod-screen{width:100%;height:auto;aspect-ratio:176/132;display:block;background:#000;image-rendering:pixelated;image-rendering:crisp-edges}\n#ipod-clickwheel{width:74%;aspect-ratio:1;margin-top:8.5%;position:relative;border-radius:50%;background:radial-gradient(circle at 50% 40%,#fbfbfa,#d9d9d4 64%,#c8c8c2);box-shadow:inset 0 0 0 1px #b9b9b3,inset 0 8px 14px rgba(255,255,255,.45),0 10px 16px rgba(0,0,0,.06);touch-action:none}\n#ipod.held #ipod-clickwheel{filter:grayscale(.35) brightness(.92)}\n#hold-badge{display:none;position:absolute;left:50%;top:50%;transform:translate(-50%,-62%);font-size:11px;letter-spacing:.18em;color:#6d6258;font-weight:700;pointer-events:none}\n#ipod.held #hold-badge{display:block}\n#ipod-clickwheel button{position:absolute;border:0;background:transparent;color:#6f6f6a;font-weight:700;padding:0;touch-action:none;cursor:pointer}\n#ipod-clickwheel button.active{color:#222}\n#ipod-btn-menu{left:50%;top:6%;transform:translateX(-50%);font-size:10px;letter-spacing:.14em;widt";
static const char html_2[] = "h:46%;height:22%}\n#ipod-btn-prev{left:4%;top:50%;transform:translateY(-50%);width:24%;height:34%;font-size:15px}\n#ipod-btn-next{right:4%;top:50%;transform:translateY(-50%);width:24%;height:34%;font-size:15px}\n#ipod-btn-play{left:50%;bottom:5%;transform:translateX(-50%);width:42%;height:22%;font-size:14px}\n#ipod-btn-select{left:31%;top:31%;width:38%;height:38%;border-radius:50%;background:radial-gradient(circle at 50% 38%,#fff,#f4f4f1 68%,#e6e6e1);box-shadow:0 1px 2px rgba(0,0,0,.2),inset 0 0 0 1px rgba(0,0,0,.05)}\n#ipod-btn-select.active{background:#ecece8}\n.panel{flex:0 1 auto;max-height:30dvh;overflow:auto;width:min(560px,100%);margin:0 auto;background:rgba(255,255,255,.72);border:1px solid rgba(255,255,255,.9);border-radius:14px;padding:8px 10px 10px;backdrop-filter:blur(8px)}\n.row{display:flex;flex-wrap:wrap;gap:8px 12px;align-items:center;justify-content:center;font-size:12px;color:#333}\n.row label{display:flex;align-items:center;gap:4px}\n#stats{margin-top:6px;color:#4d5560}\n#stats b{font-weight:600;color:#222}\n#firmware-select{max-width:46vw}\n#restart-btn,#panel button{border:1px solid #cfd3d8;background:#fff;border-radius:8px;padding:4px 10px;color:#222}\n#battery-level{width:92px}\n#debug{margin-top:4px;text-align:center;color:#667}\n#debug summary{cursor:pointer}\n@media (max-width:420px){\n  h1{font-size:13px}\n  .panel{padding:6px 8px 8px}\n  .row{font-size:11px}\n}\n@media (max-height:700px){\n  .top{gap:2px}\n  #status{font-size:11px}\n}\n</style>\n</head>\n<body>\n<div class=\"app\">\n  <div class=\"top\">\n    <h1>iPod <span>nano</span></h1>\n    <div id=\"status\">Loading...</div>\n ";
static const char html_3[] = "   <div id=\"alert\" role=\"alert\"></div>\n  </div>\n  <div class=\"stage\">\n    <div id=\"ipod\">\n      <div id=\"ipod-body\">\n        <div id=\"screen-well\"><canvas id=\"ipod-screen\" width=\"176\" height=\"132\"></canvas></div>\n        <div id=\"ipod-clickwheel\">\n          <button type=\"button\" id=\"ipod-btn-menu\" data-btn=\"menu\" aria-label=\"Menu\">MENU</button>\n          <button type=\"button\" id=\"ipod-btn-prev\" data-btn=\"left\" aria-label=\"Previous\">&#124;&#9664;&#9664;</button>\n          <button type=\"button\" id=\"ipod-btn-next\" data-btn=\"right\" aria-label=\"Next\">&#9654;&#9654;&#124;</button>\n          <button type=\"button\" id=\"ipod-btn-play\" data-btn=\"play\" aria-label=\"Play/Pause\">&#9654;&#10073;&#10073;</button>\n          <button type=\"button\" id=\"ipod-btn-select\" data-btn=\"select\" aria-label=\"Select\"></button>\n          <div id=\"hold-badge\">HOLD</div>\n        </div>\n      </div>\n    </div>\n  </div>\n  <div class=\"panel\">\n    <div class=\"row\" id=\"firmware-bar\">\n      <label>Image\n        <select id=\"firmware-select\">\n          <option value=\"apple-official\">Apple official boot</option>\n          <option value=\"apple-stage0\">Apple stage0 canary</option>\n          <option value=\"apple-direct\">Apple OS direct diagnostic</option>\n          <option value=\"rockbox\">Rockbox</option>\n          <option value=\"ipodlinux\">iPod Linux (experimental)</option>\n        </select>\n      </label>\n      <button id=\"restart-btn\" type=\"button\">Restart</button>\n      <label id=\"audio-control\"><input id=\"audio-enable\" type=\"checkbox\">Audio</label>\n    </div>\n    <div class=\"row\" id=\"hardware-bar\">\n      <label>Bat";
static const char html_4[] = "tery <input id=\"battery-level\" type=\"range\" min=\"0\" max=\"100\" value=\"100\"><output id=\"battery-value\">100%</output></label>\n      <label><input id=\"main-charger\" type=\"checkbox\">FireWire</label>\n      <label><input id=\"usb-charger\" type=\"checkbox\">USB power</label>\n      <label><input id=\"hold-switch\" type=\"checkbox\">Hold</label>\n      <label><input id=\"force-backlight\" type=\"checkbox\">Keep screen on</label>\n    </div>\n    <div class=\"row\" id=\"stats\">\n      <span><b>FPS</b> <span id=\"fps\">0</span></span>\n      <span><b>guest</b> <span id=\"guest\">0</span></span>\n      <span><b>audio</b> <span id=\"audio\">0/0</span></span>\n      <span><b>input</b> <span id=\"input\">none</span></span>\n    </div>\n    <details id=\"debug\">\n      <summary>Hardware counters</summary>\n      <div>state <span id=\"running\">...</span> · lcd <span id=\"lcd\">0</span> · light <span id=\"light\">default</span> · disk <span id=\"disk\">0</span> · pc <span id=\"pc\">0x00000000</span></div>\n    </details>\n  </div>\n</div>\n<script>\nconst W=176,H=132,COUNTS=96;\nconst fpsEl=document.getElementById('fps');\nconst canvas=document.getElementById('ipod-screen');\nconst ctx=canvas.getContext('2d',{alpha:false});\nconst statusEl=document.getElementById('status');\nconst alertEl=document.getElementById('alert');\nconst firmwareSelect=document.getElementById('firmware-select');\nconst restartBtn=document.getElementById('restart-btn');\nconst audioEnable=document.getElementById('audio-enable');\nconst batteryLevel=document.getElementById('battery-level');\nconst batteryValue=document.getElementById('battery-value');\nconst mainCharger=documen";
static const char html_5[] = "t.getElementById('main-charger');\nconst usbCharger=document.getElementById('usb-charger');\nconst holdSwitch=document.getElementById('hold-switch');\nconst forceBacklight=document.getElementById('force-backlight');\nconst ipod=document.getElementById('ipod');\nconst sid=Math.floor(Math.random()*1e9)+1;\nlet selectDirty=false,hardwareTimer=null,audioCtx=null,audioTimer=null,audioCursor=0,audioLatest=0,audioNext=0,audioRate=0,audioStream=-1,audioPolling=false,audioSources=new Set(),clickerSeen=false,clickerStops=0;\nconst held=new Set();\nlet watchCtl=null,watchGen=0;\nlet inputChain=Promise.resolve();\nfunction setStatus(message){statusEl.textContent=message;}\nfunction text(id,value){document.getElementById(id).textContent=value;}\nfunction sendInput(url){\n  const run=()=>fetch(url,{cache:'no-store'}).then(r=>{if(!r.ok)setStatus('Input was rejected');}).catch(e=>setStatus('Input failed: '+e.message));\n  inputChain=inputChain.then(run,run);\n}\nfunction ensureWatch(){\n  if(watchCtl)return;\n  watchCtl=new AbortController();\n  fetch('/input?sid='+sid+'&watch=1',{signal:watchCtl.signal,cache:'no-store'}).catch(()=>{});\n}\nfunction stopWatch(){\n  if(!watchCtl)return;\n  const ctl=watchCtl;watchCtl=null;ctl.abort();\n}\nfunction buttonDown(name){\n  if(held.has(name))return;\n  held.add(name);\n  watchGen++;\n  const el=document.querySelector('[data-btn=\"'+name+'\"]');\n  if(el)el.classList.add('active');\n  ensureWatch();\n  sendInput('/input?sid='+sid+'&button='+encodeURIComponent(name)+'&state=down');\n}\nfunction buttonUp(name){\n  if(!held.has(name))return;\n  held.delete(name);\n  const el=document.quer";
static const char html_6[] = "ySelector('[data-btn=\"'+name+'\"]');\n  if(el)el.classList.remove('active');\n  sendInput('/input?sid='+sid+'&button='+encodeURIComponent(name)+'&state=up');\n  if(held.size===0){\n    const gen=watchGen;\n    inputChain=inputChain.then(()=>{if(gen===watchGen&&held.size===0)stopWatch();});\n  }\n}\nfunction releaseAll(){\n  watchGen++;\n  Array.from(held).forEach(buttonUp);\n  document.querySelectorAll('[data-btn]').forEach(el=>el.classList.remove('active'));\n  fetch('/input?sid='+sid+'&release=1',{keepalive:true,cache:'no-store'}).catch(()=>{});\n  stopWatch();\n}\nfirmwareSelect.onchange=()=>{selectDirty=true;};\nfirmwareSelect.onkeydown=e=>{if(e.key==='Enter'){e.preventDefault();restartSelected();}};\nasync function restartSelected(){\n  setStatus('Restarting '+firmwareSelect.value+'...');\n  alertEl.classList.remove('show');\n  selectDirty=false;\n  try{\n    const r=await fetch('/control?restart='+encodeURIComponent(firmwareSelect.value),{cache:'no-store'});\n    if(!r.ok)setStatus('Restart failed');\n  }catch(e){setStatus('Restart failed: '+e.message);}\n}\nrestartBtn.onclick=e=>{e.preventDefault();restartSelected();};\nasync function updateHardware(){\n  const q=new URLSearchParams({battery:batteryLevel.value,main_charger:mainCharger.checked?'1':'0',usb_charger:usbCharger.checked?'1':'0',hold:holdSwitch.checked?'1':'0'});\n  try{const r=await fetch('/hardware?'+q,{cache:'no-store'});if(!r.ok)setStatus('Hardware update failed');}\n  catch(e){setStatus('Hardware update failed: '+e.message);}\n}\nfunction queueHardware(){if(hardwareTimer)clearTimeout(hardwareTimer);hardwareTimer=setTimeout(updateHardw";
static const char html_7[] = "are,80);}\nbatteryLevel.oninput=()=>{batteryValue.value=batteryLevel.value+'%';queueHardware();};\nmainCharger.onchange=updateHardware;usbCharger.onchange=updateHardware;holdSwitch.onchange=updateHardware;\nforceBacklight.onchange=async()=>{\n  try{const r=await fetch('/display?force_backlight='+(forceBacklight.checked?'1':'0'),{cache:'no-store'});if(!r.ok)setStatus('Display update failed');}\n  catch(e){setStatus('Display update failed: '+e.message);}\n};\ndocument.querySelectorAll('[data-btn]').forEach(el=>{\n  const name=el.dataset.btn;\n  el.addEventListener('pointerdown',e=>{\n    if(e.pointerType==='mouse'&&e.button!==0)return;\n    e.preventDefault();e.stopPropagation();\n    try{el.setPointerCapture(e.pointerId);}catch(err){}\n    buttonDown(name);\n  });\n  const up=e=>{e.preventDefault();buttonUp(name);};\n  el.addEventListener('pointerup',up);\n  el.addEventListener('pointercancel',up);\n  el.addEventListener('contextmenu',e=>e.preventDefault());\n});\nfunction keyButton(key){switch(key){case'ArrowUp':return'menu';case'ArrowLeft':return'left';case'ArrowRight':return'right';case'ArrowDown':return'play';case'Enter':return'select';default:return null;}}\nconst keysDown=new Set();\nwindow.addEventListener('keydown',e=>{\n  if(e.target&&(e.target.tagName==='INPUT'||e.target.tagName==='SELECT'||e.target.tagName==='TEXTAREA'))return;\n  const b=keyButton(e.key);if(!b||e.repeat||keysDown.has(b))return;\n  e.preventDefault();keysDown.add(b);buttonDown(b);\n});\nwindow.addEventListener('keyup',e=>{\n  const b=keyButton(e.key);if(!b)return;\n  e.preventDefault();keysDown.delete(b);buttonUp(b);\n});\nwind";
static const char html_8[] = "ow.addEventListener('blur',()=>{keysDown.clear();releaseAll();});\ndocument.addEventListener('visibilitychange',()=>{if(document.hidden){keysDown.clear();releaseAll();}});\nwindow.addEventListener('pagehide',()=>{keysDown.clear();releaseAll();});\nconst wheel=document.getElementById('ipod-clickwheel');\nlet dragging=false,dragId=0,lastAngle=0,wheelFrac=0,wheelQueued=0,wheelTimer=0;\nfunction angleOf(e){\n  const r=wheel.getBoundingClientRect();\n  return Math.atan2(e.clientX-(r.left+r.width/2),-(e.clientY-(r.top+r.height/2)))*(180/Math.PI)+180;\n}\nfunction radiusOf(e){\n  const r=wheel.getBoundingClientRect();\n  const x=e.clientX-(r.left+r.width/2),y=e.clientY-(r.top+r.height/2);\n  return Math.hypot(x,y)/(r.width/2);\n}\nfunction flushWheel(){\n  wheelTimer=0;\n  const steps=wheelQueued;wheelQueued=0;\n  if(steps)sendInput('/input?sid='+sid+'&wheel_delta='+steps);\n}\nfunction noteDegrees(delta){\n  wheelFrac+=delta*COUNTS/360;\n  const steps=Math.trunc(wheelFrac);\n  wheelFrac-=steps;\n  if(!steps)return;\n  wheelQueued+=steps;\n  if(!wheelTimer)wheelTimer=setTimeout(flushWheel,16);\n}\nwheel.addEventListener('pointerdown',e=>{\n  if(e.target.closest('[data-btn]'))return;\n  if(e.pointerType==='mouse'&&e.button!==0)return;\n  e.preventDefault();\n  dragging=true;dragId=e.pointerId;lastAngle=angleOf(e);\n  try{wheel.setPointerCapture(e.pointerId);}catch(err){}\n});\nwheel.addEventListener('pointermove',e=>{\n  if(!dragging||e.pointerId!==dragId)return;\n  e.preventDefault();\n  if(radiusOf(e)<0.22)return;\n  const a=angleOf(e);\n  let d=a-lastAngle;if(d>180)d-=360;if(d<-180)d+=360;\n  lastAngle=a;noteDegrees(d";
static const char html_9[] = ");\n});\nfunction endDrag(e){\n  if(!dragging||(e&&e.pointerId!==dragId&&e.type!=='pointercancel'))return;\n  dragging=false;flushWheel();\n}\nwheel.addEventListener('pointerup',endDrag);\nwheel.addEventListener('pointercancel',endDrag);\nwheel.addEventListener('contextmenu',e=>e.preventDefault());\nwheel.addEventListener('wheel',e=>{e.preventDefault();noteDegrees(e.deltaY<0?8:-8);flushWheel();},{passive:false});\nfunction readU64(v,o){return v.getUint32(o,true)+v.getUint32(o+4,true)*4294967296;}\nfunction resetAudioQueue(){for(const src of audioSources){try{src.stop();}catch(e){}}audioSources.clear();if(audioCtx)audioNext=audioCtx.currentTime+.04;}\nfunction playClicker(period,duty,durationTicks,rtcScale,count){\n  if(!audioEnable.checked||!audioCtx||!count)return;\n  const hz=93750/(period+1),duration=Math.min(1,Math.max(.001,durationTicks*rtcScale/1000000)),gain=Math.max(.01,.06*duty/255),limit=Math.min(count,8);\n  for(let i=0;i<limit;i++){\n    const when=audioCtx.currentTime+.01+i*(duration+.003),osc=audioCtx.createOscillator(),amp=audioCtx.createGain();\n    osc.type='square';osc.frequency.setValueAtTime(hz,when);amp.gain.setValueAtTime(gain,when);amp.gain.setValueAtTime(0,when+duration);\n    osc.connect(amp);amp.connect(audioCtx.destination);osc.onended=()=>{osc.disconnect();amp.disconnect();};osc.start(when);osc.stop(when+duration+.001);\n  }\n}\nasync function pumpAudio(){\n  if(!audioEnable.checked||!audioCtx||audioPolling||audioNext-audioCtx.currentTime>.2)return;\n  audioPolling=true;\n  try{\n    const requested=Math.floor(audioCursor),r=await fetch('/audio.pcm?cursor='+requested,{ca";
static const char html_10[] = "che:'no-store'}),b=await r.arrayBuffer();\n    if(b.byteLength<32)return;\n    const v=new DataView(b);if(v.getUint32(0,true)!==0x3141314e)return;\n    const rate=v.getUint32(4,true),start=readU64(v,8),next=readU64(v,16),count=v.getUint32(24,true),stream=v.getUint32(28,true)>>>1;\n    if(stream!==audioStream||rate!==audioRate||start!==requested){resetAudioQueue();audioStream=stream;audioRate=rate;}\n    audioCursor=next;if(count<2||b.byteLength<32+count*2)return;\n    const frames=Math.floor(count/2),ab=audioCtx.createBuffer(2,frames,rate),l=ab.getChannelData(0),rr=ab.getChannelData(1),pcm=new DataView(b,32);\n    for(let i=0;i<frames;i++){l[i]=pcm.getInt16(i*4,true)/32768;rr[i]=pcm.getInt16(i*4+2,true)/32768;}\n    const src=audioCtx.createBufferSource();src.buffer=ab;src.connect(audioCtx.destination);src.onended=()=>audioSources.delete(src);audioSources.add(src);\n    let when=Math.max(audioCtx.currentTime+.03,audioNext);if(when>audioCtx.currentTime+.25)when=audioCtx.currentTime+.03;src.start(when);audioNext=when+frames/rate;\n  }catch(e){setStatus('Audio failed: '+e.message);}finally{audioPolling=false;}\n}\naudioEnable.onchange=async()=>{\n  if(audioEnable.checked){\n    const AC=window.AudioContext||window.webkitAudioContext;if(!AC){audioEnable.checked=false;return;}\n    if(!audioCtx)audioCtx=new AC();await audioCtx.resume();audioCursor=audioLatest;resetAudioQueue();\n    if(audioTimer)clearInterval(audioTimer);audioTimer=setInterval(pumpAudio,40);pumpAudio();\n  }else{if(audioTimer)clearInterval(audioTimer);audioTimer=null;resetAudioQueue();if(audioCtx)await audioCtx.suspend();}\n};\nl";
static const char html_11[] = "et shown=0,fpsStamp=performance.now();\nasync function frameLoop(){\n  let since=0;\n  for(;;){\n    let r;\n    try{r=await fetch('/frame.rgba?since='+since,{cache:'no-store'});}\n    catch(e){setStatus('Offline');text('running','offline');await new Promise(res=>setTimeout(res,400));continue;}\n    const seq=Number(r.headers.get('X-Frame-Seq'));\n    if(Number.isFinite(seq))since=seq;\n    if(r.status===204)continue;\n    const buf=await r.arrayBuffer();\n    if(buf.byteLength!==W*H*4)continue;\n    ctx.putImageData(new ImageData(new Uint8ClampedArray(buf),W,H),0,0);\n    shown++;\n    const now=performance.now();\n    if(now-fpsStamp>=500){fpsEl.textContent=String(Math.round(shown*1000/(now-fpsStamp)));shown=0;fpsStamp=now;}\n  }\n}\nasync function statusLoop(){\n  for(;;){\n    try{\n      const r=await fetch('/status.json',{cache:'no-store'});\n      const s=await r.json();\n      if(s.restart_error){alertEl.textContent=s.restart_error;alertEl.classList.add('show');setStatus('Still running the previous image');}\n      else{alertEl.classList.remove('show');alertEl.textContent='';setStatus((s.running?'Running':'Stopped')+' · '+s.label);}\n      if(s.preset&&!selectDirty&&document.activeElement!==firmwareSelect)firmwareSelect.value=s.preset;\n      if(document.activeElement!==batteryLevel){batteryLevel.value=s.battery_percent;batteryValue.value=s.battery_percent+'%';}\n      mainCharger.checked=!!s.main_charger;usbCharger.checked=!!s.usb_charger;holdSwitch.checked=!!s.hold;forceBacklight.checked=!!s.force_backlight_on;\n      ipod.classList.toggle('held',!!s.hold);\n      text('running',s.running?'ru";
static const char html_12[] = "nning':'stopped');text('guest',s.guest_insns.toLocaleString());\n      text('audio',(s.audio_output?'on ':'idle ')+s.audio_rate.toLocaleString());\n      text('lcd',s.lcd_words.toLocaleString());\n      text('light',(s.backlight_on?'on ':'off ')+s.backlight_level+'/32'+(s.force_backlight_on?' forced':''));\n      text('disk',s.disk_reads.toLocaleString());text('input',s.input);text('pc',s.cpu_pc);\n      if(s.audio_stream!==audioStream||s.audio_rate!==audioRate||s.audio_cursor<audioLatest){audioCursor=s.audio_cursor;audioStream=s.audio_stream;audioRate=s.audio_rate;resetAudioQueue();}\n      audioLatest=s.audio_cursor;\n      if(!clickerSeen){clickerStops=s.clicker_stops;clickerSeen=true;}\n      else if(s.clicker_stops>clickerStops){playClicker(s.clicker_period,s.clicker_duty,s.clicker_last_ticks,s.rtc_usec_per_tick,s.clicker_stops-clickerStops);clickerStops=s.clicker_stops;}\n    }catch(e){setStatus('Offline');text('running','offline');}\n    await new Promise(res=>setTimeout(res,250));\n  }\n}\nsetInterval(()=>{if(held.size)sendInput('/input?sid='+sid+'&ping=1');},250);\nframeLoop();statusLoop();\n</script>\n</body>\n</html>\n";


static const char *const html_parts[] = {
html_0,
html_1,
html_2,
html_3,
html_4,
html_5,
html_6,
html_7,
html_8,
html_9,
html_10,
html_11,
html_12
};
static const size_t html_part_count = sizeof(html_parts) / sizeof(html_parts[0]);

static bool build_status(n1g_state_t *s, n1g_web_server_t *web, bool running,
                         char *body, size_t cap, size_t *out_len);

static struct n1g_web_impl *impl_of(n1g_web_server_t *web) {
    return web ? (struct n1g_web_impl *)web->impl : NULL;
}

static uint64_t mono_ms(void) {
#ifdef _WIN32
    return GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
#endif
}

static void mu_lock(struct n1g_web_impl *im) {
#ifdef _WIN32
    EnterCriticalSection(&im->mu);
#else
    pthread_mutex_lock(&im->mu);
#endif
}

static void mu_unlock(struct n1g_web_impl *im) {
#ifdef _WIN32
    LeaveCriticalSection(&im->mu);
#else
    pthread_mutex_unlock(&im->mu);
#endif
}

static void wake_http(struct n1g_web_impl *im) {
#ifndef _WIN32
    char b = 1;
    if (im->wake_w >= 0) {
        ssize_t n = write(im->wake_w, &b, 1);
        (void)n;
    }
#else
    (void)im;
#endif
}

static void close_fd(intptr_t fd) {
    if (fd == N1G_WEB_INVALID_FD) return;
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
    if (flags < 0) return false;
    return fcntl(N1G_SOCK(fd), F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

static bool would_block(void) {
#ifdef _WIN32
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

void n1g_web_sleep_ms(uint32_t ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}

static void set_nodelay(intptr_t fd) {
    int one = 1;
    (void)setsockopt(N1G_SOCK(fd), IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof(one));
}

static void json_escape(const char *src, char *dst, size_t dst_len) {
    size_t j = 0;
    if (!src) src = "";
    for (size_t i = 0; src[i] != '\0' && j + 2 < dst_len; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '"' || c == '\\') {
            if (j + 3 >= dst_len) break;
            dst[j++] = '\\';
            dst[j++] = (char)c;
        } else if (c < 0x20u) {
            dst[j++] = ' ';
        } else {
            dst[j++] = (char)c;
        }
    }
    dst[j] = '\0';
}

static const char *button_names[5] = {"menu", "left", "select", "right", "play"};

static const char *canonical_button(const char *name) {
    if (strcmp(name, "prev") == 0) return "left";
    if (strcmp(name, "next") == 0) return "right";
    if (strcmp(name, "down") == 0) return "play";
    return name;
}

static int button_index(const char *name) {
    const char *canon = canonical_button(name);
    for (int i = 0; i < 5; i++) {
        if (strcmp(canon, button_names[i]) == 0) return i;
    }
    return -1;
}

static struct n1g_web_session *session_locked(struct n1g_web_impl *im, uint32_t id, bool create) {
    struct n1g_web_session *free_slot = NULL;
    for (int i = 0; i < N1G_WEB_SESSIONS; i++) {
        if (im->sessions[i].used && im->sessions[i].id == id) return &im->sessions[i];
        if (!im->sessions[i].used && !free_slot) free_slot = &im->sessions[i];
    }
    if (!create || !free_slot) return NULL;
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->used = true;
    free_slot->id = id;
    free_slot->last_ms = mono_ms();
    return free_slot;
}

static struct n1g_web_cmd *cmd_alloc_locked(struct n1g_web_impl *im) {
    for (int i = 0; i < N1G_WEB_CMD_POOL; i++) {
        if (!im->cmds[i].in_use) {
            memset(&im->cmds[i], 0, sizeof(im->cmds[i]));
            im->cmds[i].in_use = true;
            im->cmds[i].http_status = 200;
            return &im->cmds[i];
        }
    }
    return NULL;
}

static void cmd_recycle_locked(struct n1g_web_impl *im, struct n1g_web_cmd *cmd) {
    (void)im;
    if (!cmd) return;
    free(cmd->heap_body);
    cmd->heap_body = NULL;
    cmd->body = NULL;
    cmd->in_use = false;
    cmd->next = NULL;
}

static void enqueue_locked(struct n1g_web_impl *im, struct n1g_web_cmd *cmd) {
    cmd->next = NULL;
    if (im->q_tail) im->q_tail->next = cmd;
    else im->q_head = cmd;
    im->q_tail = cmd;
    atomic_store_explicit(&im->urgent, 1, memory_order_release);
}

static struct n1g_web_cmd *dequeue_all_locked(struct n1g_web_impl *im) {
    struct n1g_web_cmd *head = im->q_head;
    im->q_head = NULL;
    im->q_tail = NULL;
    return head;
}

static int n1g_strncasecmp(const char *a, const char *b, size_t n) {
#ifdef _WIN32
    return _strnicmp(a, b, n);
#else
    return strncasecmp(a, b, n);
#endif
}

static void cmd_set_inline(struct n1g_web_cmd *cmd, int status, const char *type, const char *text) {
    size_t n = strlen(text);
    if (n < sizeof(cmd->inline_body)) {
        memcpy(cmd->inline_body, text, n);
        cmd->inline_body[n] = '\0';
        cmd->body = (uint8_t *)cmd->inline_body;
        cmd->body_len = n;
    } else {
        uint8_t *heap = (uint8_t *)malloc(n + 1u);
        if (!heap) {
            memcpy(cmd->inline_body, "{\"ok\":false}\n", 13);
            cmd->body = (uint8_t *)cmd->inline_body;
            cmd->body_len = 13;
            cmd->http_status = 500;
            snprintf(cmd->content_type, sizeof(cmd->content_type), "application/json");
            return;
        }
        memcpy(heap, text, n);
        heap[n] = 0;
        cmd->heap_body = heap;
        cmd->body = heap;
        cmd->body_len = n;
    }
    cmd->http_status = status;
    snprintf(cmd->content_type, sizeof(cmd->content_type), "%s", type);
}

static bool query_span(const char *query, const char *key, const char **val, const char **end) {
    size_t klen;
    const char *p;
    if (!query || !key) return false;
    klen = strlen(key);
    p = query;
    while (*p) {
        if ((p == query || p[-1] == '&') && strncmp(p, key, klen) == 0 && p[klen] == '=') {
            *val = p + klen + 1;
            *end = *val;
            while (**end && **end != '&') (*end)++;
            return true;
        }
        p++;
    }
    return false;
}

static bool query_has(const char *query, const char *needle) {
    return query && needle && strstr(query, needle) != NULL;
}

static bool query_u32(const char *query, const char *key, uint32_t *out) {
    const char *val;
    const char *end;
    char tmp[32];
    char *stop = NULL;
    unsigned long value;
    size_t n;
    if (!query_span(query, key, &val, &end) || val == end) return false;
    n = (size_t)(end - val);
    if (n >= sizeof(tmp)) return false;
    memcpy(tmp, val, n);
    tmp[n] = '\0';
    value = strtoul(tmp, &stop, 0);
    if (stop == tmp) return false;
    *out = (uint32_t)value;
    return true;
}

static bool query_u64(const char *query, const char *key, uint64_t *out) {
    const char *val;
    const char *end;
    char tmp[48];
    char *stop = NULL;
    unsigned long long value;
    size_t n;
    if (!query_span(query, key, &val, &end) || val == end) return false;
    n = (size_t)(end - val);
    if (n >= sizeof(tmp)) return false;
    memcpy(tmp, val, n);
    tmp[n] = '\0';
    value = strtoull(tmp, &stop, 0);
    if (stop == tmp) return false;
    *out = (uint64_t)value;
    return true;
}

static bool query_i32(const char *query, const char *key, int *out) {
    const char *val;
    const char *end;
    char tmp[32];
    char *stop = NULL;
    long value;
    size_t n;
    if (!query_span(query, key, &val, &end) || val == end) return false;
    n = (size_t)(end - val);
    if (n >= sizeof(tmp)) return false;
    memcpy(tmp, val, n);
    tmp[n] = '\0';
    value = strtol(tmp, &stop, 10);
    if (stop == tmp) return false;
    *out = (int)value;
    return true;
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

static void apply_backlight_px(uint32_t intensity, uint8_t *r, uint8_t *g, uint8_t *b) {
    *r = (uint8_t)((*r * intensity + 127u) / 255u);
    *g = (uint8_t)((*g * intensity + 127u) / 255u);
    *b = (uint8_t)((*b * intensity + 127u) / 255u);
}

static uint8_t *make_rgba_snap(const uint16_t *pixels, uint32_t intensity, bool apply_lighting, size_t *out_len) {
    uint8_t *rgba = (uint8_t *)malloc(N1G_WEB_PIXELS * 4u);
    if (!rgba) return NULL;
    for (uint32_t i = 0; i < N1G_WEB_PIXELS; i++) {
        uint8_t r = 0, g = 0, b = 0;
        rgb565(pixels[i], &r, &g, &b);
        if (apply_lighting) apply_backlight_px(intensity, &r, &g, &b);
        rgba[i * 4u + 0u] = r;
        rgba[i * 4u + 1u] = g;
        rgba[i * 4u + 2u] = b;
        rgba[i * 4u + 3u] = 0xffu;
    }
    *out_len = N1G_WEB_PIXELS * 4u;
    return rgba;
}

static uint8_t *make_bmp_snap(const uint16_t *pixels, uint32_t intensity, bool apply_lighting, size_t *out_len) {
    const uint32_t w = N1G_LCD_W;
    const uint32_t h = N1G_LCD_H;
    const uint32_t row = (w * 3u + 3u) & ~3u;
    const uint32_t pixel_bytes = row * h;
    const uint32_t total = 54u + pixel_bytes;
    uint8_t *bmp = (uint8_t *)calloc(1, total);
    if (!bmp) return NULL;
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
            rgb565(pixels[y * w + x], &r, &g, &b);
            if (apply_lighting) apply_backlight_px(intensity, &r, &g, &b);
            uint8_t *px = dst + y * row + x * 3u;
            px[0] = b;
            px[1] = g;
            px[2] = r;
        }
    }
    *out_len = total;
    return bmp;
}

static const char *reason_for(int code) {
    if (code == 200) return "OK";
    if (code == 204) return "No Content";
    if (code == 400) return "Bad Request";
    if (code == 404) return "Not Found";
    if (code == 405) return "Method Not Allowed";
    if (code == 503) return "Service Unavailable";
    return "Internal Server Error";
}

static bool begin_response(struct n1g_web_conn *c, int code, const char *type,
                           const void *body, size_t body_len, const char *extra_hdr) {
    char hdr[640];
    const char *conn = c->keep_alive ? "keep-alive" : "close";
    int n = snprintf(hdr, sizeof(hdr),
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %zu\r\n"
                     "Cache-Control: no-store\r\n"
                     "Connection: %s\r\n"
                     "%s"
                     "\r\n",
                     code, reason_for(code), type, body_len, conn,
                     extra_hdr ? extra_hdr : "");
    if (n <= 0 || (size_t)n >= sizeof(hdr)) return false;
    char *buf = (char *)malloc((size_t)n + body_len);
    if (!buf) return false;
    memcpy(buf, hdr, (size_t)n);
    if (body_len && body) memcpy(buf + n, body, body_len);
    free(c->out);
    c->out = buf;
    c->out_len = (size_t)n + body_len;
    c->out_off = 0;
    c->close_after = !c->keep_alive;
    c->state = ST_WRITE;
    return true;
}

static bool begin_frame(struct n1g_web_conn *c, int code, uint64_t seq,
                        const char *type, const void *body, size_t body_len) {
    char extra[64];
    snprintf(extra, sizeof(extra), "X-Frame-Seq: %llu\r\n", (unsigned long long)seq);
    return begin_response(c, code, type, body, body_len, extra);
}

static void copy_frame_locked(struct n1g_web_impl *im, uint16_t *dst, uint32_t *intensity,
                              bool *force, uint64_t *seq) {
    memcpy(dst, im->pixels, sizeof(im->pixels));
    *intensity = im->intensity;
    *force = im->force_fullbright;
    *seq = im->frame_seq;
}

static bool send_snapshot_frame(struct n1g_web_impl *im, struct n1g_web_conn *c) {
    uint16_t *pix = (uint16_t *)malloc(sizeof(im->pixels));
    uint32_t intensity = 0;
    bool force = false;
    uint64_t seq = 0;
    if (!pix) return begin_response(c, 500, "text/plain", "out of memory\n", 14, NULL);
    mu_lock(im);
    copy_frame_locked(im, pix, &intensity, &force, &seq);
    mu_unlock(im);
    size_t len = 0;
    uint8_t *body;
    const char *type;
    if (c->frame_bmp) {
        body = make_bmp_snap(pix, intensity, !force, &len);
        type = "image/bmp";
    } else {
        bool light = !c->frame_raw && !force;
        body = make_rgba_snap(pix, intensity, light, &len);
        type = "application/octet-stream";
    }
    free(pix);
    if (!body) return begin_response(c, 500, "text/plain", "out of memory\n", 14, NULL);
    bool ok = begin_frame(c, 200, seq, type, body, len);
    free(body);
    return ok;
}

static void note_button_locked(struct n1g_web_impl *im, uint32_t sid, const char *name, bool pressed) {
    int idx = button_index(name);
    struct n1g_web_session *se = session_locked(im, sid, true);
    if (!se || idx < 0) return;
    if (pressed) se->mask |= 1u << idx;
    else se->mask &= ~(1u << idx);
    se->last_ms = mono_ms();
}

static void release_mask(n1g_state_t *s, uint32_t mask) {
    for (int i = 0; i < 5; i++) {
        if ((mask & (1u << i)) == 0u) continue;
        (void)n1g_dev_opto_button(s, button_names[i], false);
    }
}

static bool valid_restart_preset(const char *preset) {
    return strcmp(preset, "rockbox") == 0 || strcmp(preset, "ipodlinux") == 0 ||
           strcmp(preset, "apple-direct") == 0 || strcmp(preset, "apple-official") == 0 ||
           strcmp(preset, "apple-flash") == 0 || strcmp(preset, "apple-stage0") == 0 ||
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

static void apply_one(n1g_state_t *s, n1g_web_server_t *web, struct n1g_web_cmd *cmd) {
    struct n1g_web_impl *im = impl_of(web);
    if (cmd->type == CMD_BUTTON) {
        const char *name = canonical_button(cmd->button);
        bool wake = cmd->pressed && strcmp(name, "menu") == 0 && s->i2c.pcf_standby;
        bool ok;
        if (cmd->tap) ok = n1g_dev_opto_tap(s, name, N1G_INPUT_HOLD_TICKS);
        else ok = n1g_dev_opto_button(s, name, cmd->pressed);
        if (ok && !cmd->tap && !wake && !s->opts.hold_switch_engaged) {
            mu_lock(im);
            note_button_locked(im, cmd->sid, name, cmd->pressed);
            mu_unlock(im);
        }
        cmd_set_inline(cmd, ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}\n" : "{\"ok\":false}\n");
    } else if (cmd->type == CMD_WHEEL) {
        int delta = cmd->wheel_delta;
        if (delta > N1G_WEB_WHEEL_CLAMP) delta = N1G_WEB_WHEEL_CLAMP;
        if (delta < -N1G_WEB_WHEEL_CLAMP) delta = -N1G_WEB_WHEEL_CLAMP;
        bool ok = delta == 0 ? true : n1g_dev_opto_wheel(s, delta);
        cmd_set_inline(cmd, ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}\n" : "{\"ok\":false}\n");
    } else if (cmd->type == CMD_RELEASE) {
        uint32_t mask = 0;
        mu_lock(im);
        struct n1g_web_session *se = session_locked(im, cmd->sid, false);
        if (se) {
            mask = se->mask;
            se->mask = 0;
            se->last_ms = mono_ms();
        }
        mu_unlock(im);
        release_mask(s, mask);
        cmd_set_inline(cmd, 200, "application/json", "{\"ok\":true}\n");
    } else if (cmd->type == CMD_HARDWARE) {
        uint32_t battery = s->opts.battery_percent;
        uint32_t main_charger = s->opts.main_charger_connected ? 1u : 0u;
        uint32_t usb_charger = s->opts.usb_charger_connected ? 1u : 0u;
        uint32_t hold = s->opts.hold_switch_engaged ? 1u : 0u;
        uint32_t rtc_usec = s->opts.rtc_usec_per_tick;
        bool valid = true;
        if (cmd->has_battery) {
            battery = cmd->battery;
            valid = battery <= 100u;
        }
        if (valid && cmd->has_main) {
            main_charger = cmd->main_charger;
            valid = main_charger <= 1u;
        }
        if (valid && cmd->has_usb) {
            usb_charger = cmd->usb_charger;
            valid = usb_charger <= 1u;
        }
        if (valid && cmd->has_hold) {
            hold = cmd->hold;
            valid = hold <= 1u;
        }
        if (valid && cmd->has_rtc) {
            rtc_usec = cmd->rtc_usec;
            valid = rtc_usec >= 1u && rtc_usec <= 4096u;
        }
        if (!valid || !(cmd->has_battery || cmd->has_main || cmd->has_usb || cmd->has_hold || cmd->has_rtc)) {
            cmd_set_inline(cmd, 400, "application/json", "{\"ok\":false}\n");
            return;
        }
        s->opts.battery_percent = battery;
        s->opts.rtc_usec_per_tick = rtc_usec;
        (void)n1g_dev_gpio_set_chargers(s, main_charger != 0u, usb_charger != 0u);
        (void)n1g_dev_gpio_set_hold(s, hold != 0u);
        char body[192];
        int n = snprintf(body, sizeof(body),
                         "{\"ok\":true,\"battery_percent\":%u,\"main_charger\":%s,\"usb_charger\":%s,\"hold\":%s,\"rtc_usec_per_tick\":%u}\n",
                         s->opts.battery_percent,
                         s->opts.main_charger_connected ? "true" : "false",
                         s->opts.usb_charger_connected ? "true" : "false",
                         s->opts.hold_switch_engaged ? "true" : "false",
                         s->opts.rtc_usec_per_tick);
        if (n <= 0 || (size_t)n >= sizeof(body)) {
            cmd_set_inline(cmd, 500, "application/json", "{\"ok\":false}\n");
            return;
        }
        cmd_set_inline(cmd, 200, "application/json", body);
    } else if (cmd->type == CMD_DISPLAY) {
        if (cmd->force_backlight > 1u) {
            cmd_set_inline(cmd, 400, "application/json", "{\"ok\":false}\n");
            return;
        }
        bool enabled = cmd->force_backlight != 0u;
        if (web->force_backlight_on != enabled) {
            web->force_backlight_on = enabled;
            im->force_publish = true;
        }
        char body[64];
        snprintf(body, sizeof(body), "{\"ok\":true,\"force_backlight_on\":%s}\n", enabled ? "true" : "false");
        cmd_set_inline(cmd, 200, "application/json", body);
    } else if (cmd->type == CMD_CONTROL) {
        if (!valid_restart_preset(cmd->button)) {
            cmd_set_inline(cmd, 400, "application/json", "{\"ok\":false}\n");
            return;
        }
        snprintf(web->restart_preset, sizeof(web->restart_preset), "%s", cmd->button);
        web->restart_requested = true;
        cmd_set_inline(cmd, 200, "application/json", "{\"ok\":true}\n");
    } else if (cmd->type == CMD_AUDIO) {
        uint64_t produced = s->i2s.pcm_produced_halfwords & ~1ull;
        uint64_t cursor = produced;
        if (cmd->has_rtc) cursor = cmd->audio_cursor;
        uint64_t oldest = produced > N1G_AUDIO_RING_HALFWORDS ? produced - N1G_AUDIO_RING_HALFWORDS : 0u;
        if (oldest < s->i2s.pcm_stream_start_halfword) oldest = s->i2s.pcm_stream_start_halfword;
        oldest = (oldest + 1u) & ~1ull;
        if (cursor < oldest) {
            s->i2s.host_dropped_halfwords += oldest - cursor;
            cursor = oldest;
        }
        if (cursor > produced) cursor = produced;
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
            cmd_set_inline(cmd, 500, "text/plain", "out of memory\n");
            return;
        }
        put32(body, 0x3141314eu);
        put32(body + 4, s->i2s.pcm_sample_rate != 0u ? s->i2s.pcm_sample_rate : 44100u);
        put64(body + 8, cursor);
        put64(body + 16, cursor + count);
        put32(body + 24, count);
        put32(body + 28, (s->i2s.pcm_stream_id << 1u) | (s->i2c.wm8975_output_enabled ? 1u : 0u));
        for (uint32_t i = 0; i < count; i++) {
            put16(body + 32u + i * 2u, (uint16_t)s->i2s.pcm_ring[(cursor + i) % N1G_AUDIO_RING_HALFWORDS]);
        }
        cmd->heap_body = body;
        cmd->body = body;
        cmd->body_len = body_len;
        cmd->http_status = 200;
        snprintf(cmd->content_type, sizeof(cmd->content_type), "application/octet-stream");
    } else if (cmd->type == CMD_DUMP) {
        uint32_t count = cmd->dump_count;
        if (count == 0 || count > 64u) count = 16;
        char body[2048];
        int n = snprintf(body, sizeof(body), "{\"addr\":\"0x%08x\",\"words\":[", cmd->dump_addr);
        for (uint32_t i = 0; i < count && n > 0 && (size_t)n < sizeof(body); i++) {
            uint32_t value = 0;
            (void)n1g_ram_read(s, cmd->dump_addr + i * 4u, 4, &value);
            n += snprintf(body + n, sizeof(body) - (size_t)n, "%s\"0x%08x\"", i ? "," : "", value);
        }
        if (n > 0 && (size_t)n < sizeof(body)) n += snprintf(body + n, sizeof(body) - (size_t)n, "]}\n");
        if (n <= 0 || (size_t)n >= sizeof(body)) {
            cmd_set_inline(cmd, 500, "application/json", "{\"ok\":false,\"error\":\"response too large\"}\n");
            return;
        }
        cmd_set_inline(cmd, 200, "application/json", body);
    }
}

static bool publish_frame(n1g_state_t *s, n1g_web_server_t *web) {
    struct n1g_web_impl *im = impl_of(web);
    if (!im) return false;
    bool changed = s->counters.lcd_words != web->last_lcd_words ||
                   s->backlight.generation != web->last_backlight_generation;
    if (changed) {
        web->last_lcd_words = s->counters.lcd_words;
        web->last_backlight_generation = s->backlight.generation;
        im->frame_dirty = true;
    }
    bool force = im->force_publish;
    im->force_publish = false;
    if (!im->frame_dirty && !force) return false;
    uint64_t now = mono_ms();
    if (!force && im->last_publish_ms != 0 && now - im->last_publish_ms < N1G_WEB_FRAME_MS) return false;
    memcpy(im->staging, s->lcd2.pixels, sizeof(im->staging));
    uint32_t intensity = n1g_dev_backlight_intensity(s);
    mu_lock(im);
    memcpy(im->pixels, im->staging, sizeof(im->pixels));
    im->intensity = intensity;
    im->force_fullbright = web->force_backlight_on;
    web->frame_seq++;
    im->frame_seq = web->frame_seq;
    im->last_publish_ms = now;
    im->frame_dirty = false;
    mu_unlock(im);
    wake_http(im);
    return true;
}

static void refresh_status(n1g_state_t *s, n1g_web_server_t *web, bool running) {
    struct n1g_web_impl *im = impl_of(web);
    char *buf = (char *)malloc(sizeof(im->status));
    size_t n = 0;
    if (!buf) return;
    if (!build_status(s, web, running, buf, sizeof(im->status), &n)) {
        free(buf);
        return;
    }
    mu_lock(im);
    memcpy(im->status, buf, n);
    im->status[n] = '\0';
    im->status_len = n;
    mu_unlock(im);
    free(buf);
}

static void release_stale_sessions(n1g_state_t *s, struct n1g_web_impl *im) {
    uint32_t masks[N1G_WEB_SESSIONS];
    int count = 0;
    uint64_t now = mono_ms();
    mu_lock(im);
    for (int i = 0; i < N1G_WEB_SESSIONS; i++) {
        struct n1g_web_session *se = &im->sessions[i];
        if (!se->used || se->mask == 0 || se->watch_open) continue;
        if (now - se->last_ms < N1G_WEB_SESSION_MS) continue;
        masks[count++] = se->mask;
        se->mask = 0;
    }
    mu_unlock(im);
    for (int i = 0; i < count; i++) release_mask(s, masks[i]);
}

static void finish_batch(struct n1g_web_impl *im, struct n1g_web_cmd *batch) {
    mu_lock(im);
    for (struct n1g_web_cmd *cmd = batch; cmd; cmd = cmd->next) {
        if (cmd->noreply || cmd->abandoned) cmd_recycle_locked(im, cmd);
        else cmd->done = true;
    }
    if (!im->q_head) atomic_store_explicit(&im->urgent, 0, memory_order_release);
    mu_unlock(im);
    wake_http(im);
}

void n1g_web_poll(n1g_state_t *s, n1g_web_server_t *web, bool running) {
    struct n1g_web_impl *im = impl_of(web);
    if (!web || !web->active || !im) return;
    struct n1g_web_cmd *batch = NULL;
    struct n1g_web_cmd **tail = &batch;
    for (;;) {
        mu_lock(im);
        struct n1g_web_cmd *got = dequeue_all_locked(im);
        mu_unlock(im);
        if (!got) break;
        struct n1g_web_cmd *c = got;
        while (c) {
            struct n1g_web_cmd *next = c->next;
            c->next = NULL;
            if (!c->abandoned) apply_one(s, web, c);
            *tail = c;
            tail = &c->next;
            c = next;
        }
    }
    release_stale_sessions(s, im);
    bool published = publish_frame(s, web);
    uint64_t now_ms = mono_ms();
    if (batch || im->last_publish_ms == 0 || now_ms - im->status_ms >= 50u) {
        refresh_status(s, web, running);
        im->status_ms = now_ms;
    }
    if (batch) finish_batch(im, batch);
    else if (!published) {
        mu_lock(im);
        if (!im->q_head) atomic_store_explicit(&im->urgent, 0, memory_order_release);
        mu_unlock(im);
    }
}

bool n1g_web_urgent(const n1g_web_server_t *web) {
    if (!web || !web->impl) return false;
    const struct n1g_web_impl *im = (const struct n1g_web_impl *)web->impl;
    return atomic_load_explicit(&im->urgent, memory_order_acquire) != 0;
}

void n1g_web_note_restart_failure(n1g_web_server_t *web, const char *message) {
    if (!web) return;
    if (!message) message = "Restart failed";
    snprintf(web->restart_error, sizeof(web->restart_error), "%s", message);
}

void n1g_web_clear_restart_error(n1g_web_server_t *web) {
    if (!web) return;
    web->restart_error[0] = '\0';
}

bool n1g_web_take_restart(n1g_web_server_t *web, char *preset, size_t preset_size) {
    if (!web || !web->restart_requested || preset_size == 0) return false;
    snprintf(preset, preset_size, "%s", web->restart_preset);
    web->restart_requested = false;
    web->restart_preset[0] = '\0';
    web->last_lcd_words = UINT64_MAX;
    web->last_backlight_generation = UINT64_MAX;
    web->frame_seq++;
    return true;
}

static bool queue_cmd(struct n1g_web_impl *im, struct n1g_web_conn *c, struct n1g_web_cmd *cmd) {
    if (!cmd) {
        begin_response(c, 503, "application/json", "{\"ok\":false}\n", 13, NULL);
        return false;
    }
    c->cmd = cmd;
    c->state = ST_WAIT_CMD;
    mu_lock(im);
    enqueue_locked(im, cmd);
    mu_unlock(im);
    wake_http(im);
    return true;
}

static bool header_has_token(const char *headers, const char *name, const char *token) {
    size_t nlen = strlen(name);
    const char *p = headers;
    while (p && *p) {
            if ((p == headers || p[-1] == '\n') && n1g_strncasecmp(p, name, nlen) == 0 && p[nlen] == ':') {
            const char *v = p + nlen + 1;
            while (*v == ' ' || *v == '\t') v++;
            size_t tlen = strlen(token);
            for (const char *s = v; *s && *s != '\r' && *s != '\n'; s++) {
                if (n1g_strncasecmp(s, token, tlen) == 0) {
                    char before = (s == v) ? ' ' : s[-1];
                    char after = s[tlen];
                    if ((before == ' ' || before == '\t' || before == ',') &&
                        (after == '\0' || after == '\r' || after == '\n' || after == ' ' || after == '\t' || after == ','))
                        return true;
                }
            }
            return false;
        }
        p = strchr(p, '\n');
        if (p) p++;
    }
    return false;
}

static void consume_request(struct n1g_web_conn *c, size_t header_end) {
    size_t rest = c->in_len - header_end;
    memmove(c->inbuf, c->inbuf + header_end, rest);
    c->in_len = rest;
}

static bool handle_get(struct n1g_web_impl *im, struct n1g_web_conn *c, const char *path, const char *query) {
    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
        return begin_response(c, 200, "text/html; charset=utf-8", im->index_html, im->index_len, NULL);
    }
    if (strcmp(path, "/favicon.ico") == 0) {
        return begin_response(c, 204, "text/plain", "", 0, NULL);
    }
    if (strcmp(path, "/status.json") == 0) {
        mu_lock(im);
        size_t n = im->status_len;
        char *copy = n ? (char *)malloc(n) : NULL;
        if (n && copy) memcpy(copy, im->status, n);
        mu_unlock(im);
        bool ok;
        if (!copy || n == 0) {
            ok = begin_response(c, 200, "application/json", "{\"running\":true}\n", 17, NULL);
        } else {
            ok = begin_response(c, 200, "application/json", copy, n, NULL);
        }
        free(copy);
        return ok;
    }
    if (strcmp(path, "/frame.rgba") == 0 || strcmp(path, "/frame") == 0 || strcmp(path, "/frame.bmp") == 0) {
        uint64_t since = 0;
        bool has_since = query_u64(query, "since", &since);
        c->frame_raw = query_has(query, "raw=1");
        c->frame_bmp = strcmp(path, "/frame.bmp") == 0;
        mu_lock(im);
        uint64_t seq = im->frame_seq;
        mu_unlock(im);
        if (has_since && seq <= since) {
            c->frame_since = since;
            c->deadline_ms = mono_ms() + N1G_WEB_LONGPOLL_MS;
            c->state = ST_WAIT_FRAME;
            return true;
        }
        return send_snapshot_frame(im, c);
    }
    if (strcmp(path, "/input") == 0) {
        uint32_t sid = 0;
        (void)query_u32(query, "sid", &sid);
        if (query_has(query, "watch=1")) {
            mu_lock(im);
            struct n1g_web_session *se = session_locked(im, sid, true);
            if (se) {
                se->watch_gen++;
                se->watch_open = true;
                se->last_ms = mono_ms();
                c->watching = true;
                c->watch_sid = sid;
                c->watch_gen = se->watch_gen;
            }
            mu_unlock(im);
            c->state = ST_WATCH;
            c->keep_alive = false;
            return true;
        }
        if (query_has(query, "ping=1")) {
            mu_lock(im);
            struct n1g_web_session *se = session_locked(im, sid, true);
            if (se) se->last_ms = mono_ms();
            mu_unlock(im);
            return begin_response(c, 200, "application/json", "{\"ok\":true}\n", 12, NULL);
        }
        mu_lock(im);
        struct n1g_web_cmd *cmd = cmd_alloc_locked(im);
        mu_unlock(im);
        if (!cmd) return begin_response(c, 503, "application/json", "{\"ok\":false}\n", 13, NULL);
        cmd->sid = sid;
        if (query_has(query, "release=1")) {
            cmd->type = CMD_RELEASE;
        } else if (query_i32(query, "wheel_delta", &cmd->wheel_delta)) {
            cmd->type = CMD_WHEEL;
        } else if (query_has(query, "wheel=down")) {
            cmd->type = CMD_WHEEL;
            cmd->wheel_delta = 4;
        } else if (query_has(query, "wheel=up")) {
            cmd->type = CMD_WHEEL;
            cmd->wheel_delta = -4;
        } else {
            const char *buttons[] = {"menu", "left", "select", "right", "play", "prev", "next", "down"};
            bool found = false;
            for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
                char key[32];
                snprintf(key, sizeof(key), "button=%s", buttons[i]);
                if (query_has(query, key)) {
                    snprintf(cmd->button, sizeof(cmd->button), "%s", buttons[i]);
                    cmd->type = CMD_BUTTON;
                    cmd->tap = query_has(query, "tap=1");
                    cmd->pressed = !query_has(query, "state=up");
                    found = true;
                    break;
                }
            }
            if (!found) {
                mu_lock(im);
                cmd_recycle_locked(im, cmd);
                mu_unlock(im);
                return begin_response(c, 400, "application/json", "{\"ok\":false}\n", 13, NULL);
            }
        }
        return queue_cmd(im, c, cmd);
    }
    if (strcmp(path, "/hardware") == 0) {
        mu_lock(im);
        struct n1g_web_cmd *cmd = cmd_alloc_locked(im);
        mu_unlock(im);
        if (!cmd) return begin_response(c, 503, "application/json", "{\"ok\":false}\n", 13, NULL);
        cmd->type = CMD_HARDWARE;
        if (query_has(query, "battery=")) {
            cmd->has_battery = true;
            if (!query_u32(query, "battery", &cmd->battery)) cmd->battery = 101u;
        }
        if (query_has(query, "main_charger=")) {
            cmd->has_main = true;
            if (!query_u32(query, "main_charger", &cmd->main_charger)) cmd->main_charger = 2u;
        }
        if (query_has(query, "usb_charger=")) {
            cmd->has_usb = true;
            if (!query_u32(query, "usb_charger", &cmd->usb_charger)) cmd->usb_charger = 2u;
        }
        if (query_has(query, "hold=")) {
            cmd->has_hold = true;
            if (!query_u32(query, "hold", &cmd->hold)) cmd->hold = 2u;
        }
        if (query_has(query, "rtc_usec_per_tick=")) {
            cmd->has_rtc = true;
            if (!query_u32(query, "rtc_usec_per_tick", &cmd->rtc_usec)) cmd->rtc_usec = 0u;
        }
        return queue_cmd(im, c, cmd);
    }
    if (strcmp(path, "/display") == 0) {
        uint32_t force = 2u;
        if (!query_u32(query, "force_backlight", &force)) force = 2u;
        mu_lock(im);
        struct n1g_web_cmd *cmd = cmd_alloc_locked(im);
        mu_unlock(im);
        if (!cmd) return begin_response(c, 503, "application/json", "{\"ok\":false}\n", 13, NULL);
        cmd->type = CMD_DISPLAY;
        cmd->force_backlight = force;
        return queue_cmd(im, c, cmd);
    }
    if (strcmp(path, "/control") == 0) {
        const char *preset = restart_preset_from_query(query);
        if (!preset || !valid_restart_preset(preset)) {
            return begin_response(c, 400, "application/json", "{\"ok\":false}\n", 13, NULL);
        }
        mu_lock(im);
        struct n1g_web_cmd *cmd = cmd_alloc_locked(im);
        mu_unlock(im);
        if (!cmd) return begin_response(c, 503, "application/json", "{\"ok\":false}\n", 13, NULL);
        cmd->type = CMD_CONTROL;
        snprintf(cmd->button, sizeof(cmd->button), "%s", preset);
        return queue_cmd(im, c, cmd);
    }
    if (strcmp(path, "/audio.pcm") == 0) {
        mu_lock(im);
        struct n1g_web_cmd *cmd = cmd_alloc_locked(im);
        mu_unlock(im);
        if (!cmd) return begin_response(c, 503, "text/plain", "busy\n", 5, NULL);
        cmd->type = CMD_AUDIO;
        cmd->has_rtc = query_u64(query, "cursor", &cmd->audio_cursor);
        return queue_cmd(im, c, cmd);
    }
    if (strcmp(path, "/dump32") == 0) {
        uint32_t addr = 0;
        if (!query_u32(query, "addr", &addr)) {
            return begin_response(c, 400, "application/json", "{\"ok\":false,\"error\":\"missing addr\"}\n", 34, NULL);
        }
        mu_lock(im);
        struct n1g_web_cmd *cmd = cmd_alloc_locked(im);
        mu_unlock(im);
        if (!cmd) return begin_response(c, 503, "application/json", "{\"ok\":false}\n", 13, NULL);
        cmd->type = CMD_DUMP;
        cmd->dump_addr = addr;
        cmd->dump_count = 16;
        (void)query_u32(query, "count", &cmd->dump_count);
        return queue_cmd(im, c, cmd);
    }
    return begin_response(c, 404, "text/plain", "not found\n", 10, NULL);
}

static bool process_buffer(struct n1g_web_impl *im, struct n1g_web_conn *c) {
    if (c->state != ST_READ) return true;
    char *end = NULL;
    if (c->in_len >= 4) {
        for (size_t i = 0; i + 3 < c->in_len; i++) {
            if (c->inbuf[i] == '\r' && c->inbuf[i + 1] == '\n' && c->inbuf[i + 2] == '\r' && c->inbuf[i + 3] == '\n') {
                end = c->inbuf + i + 4;
                break;
            }
        }
    }
    if (!end) {
        if (c->in_len >= sizeof(c->inbuf) - 1u) return false;
        return true;
    }
    size_t header_len = (size_t)(end - c->inbuf);
    c->inbuf[header_len - 1] = '\0';
    bool http11 = false;
    if (strncmp(c->inbuf, "GET ", 4) != 0) {
        consume_request(c, header_len);
        c->keep_alive = false;
        return begin_response(c, 405, "text/plain", "method not allowed\n", 19, NULL);
    }
    char *path = c->inbuf + 4;
    char *sp = strchr(path, ' ');
    if (!sp) return false;
    *sp = '\0';
    if (strncmp(sp + 1, "HTTP/1.1", 8) == 0) http11 = true;
    char *query = strchr(path, '?');
    if (query) *query++ = '\0';
    char path_copy[256];
    char query_copy[1024];
    snprintf(path_copy, sizeof(path_copy), "%s", path);
    snprintf(query_copy, sizeof(query_copy), "%s", query ? query : "");
    const char *hdrs = strchr(sp + 1, '\n');
    bool close_hdr = hdrs && header_has_token(hdrs, "connection", "close");
    bool keep_hdr = hdrs && header_has_token(hdrs, "connection", "keep-alive");
    c->keep_alive = http11 ? !close_hdr : keep_hdr;
    consume_request(c, header_len);
    return handle_get(im, c, path_copy, query_copy);
}

static void close_conn(struct n1g_web_impl *im, int index) {
    struct n1g_web_conn *c = &im->conns[index];
    if (c->dead) return;
    c->dead = true;
    if (c->watching) {
        bool release = false;
        uint32_t sid = c->watch_sid;
        c->watching = false;
        mu_lock(im);
        struct n1g_web_session *se = session_locked(im, sid, false);
        if (se && se->watch_gen == c->watch_gen) {
            se->watch_open = false;
            release = se->mask != 0;
        }
        struct n1g_web_cmd *cmd = NULL;
        if (release) {
            cmd = cmd_alloc_locked(im);
            if (cmd) {
                cmd->type = CMD_RELEASE;
                cmd->sid = sid;
                cmd->noreply = true;
                enqueue_locked(im, cmd);
            }
        }
        mu_unlock(im);
        if (cmd) wake_http(im);
    }
    if (c->cmd) {
        mu_lock(im);
        if (c->cmd->done) cmd_recycle_locked(im, c->cmd);
        else c->cmd->abandoned = true;
        mu_unlock(im);
        c->cmd = NULL;
    }
    close_fd(c->fd);
    c->fd = N1G_WEB_INVALID_FD;
    free(c->out);
    c->out = NULL;
}

static void compact_conns(struct n1g_web_impl *im) {
    int w = 0;
    for (int i = 0; i < im->nconns; i++) {
        if (im->conns[i].dead) continue;
        if (w != i) im->conns[w] = im->conns[i];
        w++;
    }
    im->nconns = w;
}

static bool pump_write(struct n1g_web_conn *c) {
    while (c->out_off < c->out_len) {
        int n = send(N1G_SOCK(c->fd), c->out + c->out_off, (int)(c->out_len - c->out_off > 16384u ? 16384u : c->out_len - c->out_off), MSG_NOSIGNAL);
        if (n < 0) {
            if (would_block()) return true;
            return false;
        }
        if (n == 0) return false;
        c->out_off += (size_t)n;
    }
    free(c->out);
    c->out = NULL;
    c->out_len = 0;
    c->out_off = 0;
    if (c->close_after) return false;
    c->state = ST_READ;
    return true;
}

static bool pump_read(struct n1g_web_conn *c) {
    if (c->in_len >= sizeof(c->inbuf) - 1u) return c->state != ST_READ;
    int n = recv(N1G_SOCK(c->fd), c->inbuf + c->in_len, (int)(sizeof(c->inbuf) - 1u - c->in_len), 0);
    if (n < 0) return would_block();
    if (n == 0) return false;
    c->in_len += (size_t)n;
    return true;
}

static void service_waits(struct n1g_web_impl *im) {
    uint64_t now = mono_ms();
    for (int i = 0; i < im->nconns; i++) {
        struct n1g_web_conn *c = &im->conns[i];
        if (c->dead) continue;
        struct n1g_web_cmd *ready = NULL;
        if (c->state == ST_WAIT_CMD && c->cmd) {
            mu_lock(im);
            if (c->cmd && c->cmd->done) {
                ready = c->cmd;
                c->cmd = NULL;
            }
            mu_unlock(im);
        }
        if (ready) {
            bool ok = begin_response(c, ready->http_status,
                                     ready->content_type[0] ? ready->content_type : "application/json",
                                     ready->body, ready->body_len, NULL);
            mu_lock(im);
            cmd_recycle_locked(im, ready);
            mu_unlock(im);
            if (!ok) close_conn(im, i);
        } else if (c->state == ST_WAIT_FRAME) {
            mu_lock(im);
            uint64_t seq = im->frame_seq;
            mu_unlock(im);
            if (seq > c->frame_since) {
                if (!send_snapshot_frame(im, c)) close_conn(im, i);
            } else if (now >= c->deadline_ms) {
                if (!begin_frame(c, 204, seq, "text/plain", "", 0)) close_conn(im, i);
            }
        }
    }
}

static int poll_timeout(struct n1g_web_impl *im) {
    uint64_t now = mono_ms();
    int timeout = 1000;
    for (int i = 0; i < im->nconns; i++) {
        struct n1g_web_conn *c = &im->conns[i];
        if (c->state == ST_WAIT_FRAME) {
            uint64_t left = c->deadline_ms > now ? c->deadline_ms - now : 0;
            if ((int)left < timeout) timeout = (int)left;
        } else if (c->state == ST_WAIT_CMD) {
            if (timeout > 20) timeout = 20;
        }
    }
#ifdef _WIN32
    if (timeout > 5) timeout = 5;
#endif
    return timeout;
}

#ifdef _WIN32
static DWORD WINAPI http_thread(LPVOID arg)
#else
static void *http_thread(void *arg)
#endif
{
    struct n1g_web_impl *im = (struct n1g_web_impl *)arg;
    while (!im->stop) {
        compact_conns(im);
        n1g_pfd pfds[N1G_WEB_MAX_CONN + 2];
        int np = 0;
        pfds[np].fd = N1G_SOCK(im->web->listen_fd);
        pfds[np].events = POLLIN;
        pfds[np].revents = 0;
        int listen_slot = np++;
#ifndef _WIN32
        int wake_slot = -1;
        if (im->wake_r >= 0) {
            pfds[np].fd = im->wake_r;
            pfds[np].events = POLLIN;
            pfds[np].revents = 0;
            wake_slot = np++;
        }
#endif
        int conn_slot[N1G_WEB_MAX_CONN];
        for (int i = 0; i < N1G_WEB_MAX_CONN; i++) conn_slot[i] = -1;
        int live = im->nconns;
        for (int i = 0; i < live; i++) {
            if (im->conns[i].dead) {
                conn_slot[i] = -1;
                continue;
            }
            pfds[np].fd = N1G_SOCK(im->conns[i].fd);
            short ev = POLLIN;
            if (im->conns[i].state == ST_WRITE) ev = POLLOUT;
            else if (im->conns[i].state == ST_WATCH || im->conns[i].state == ST_WAIT_CMD || im->conns[i].state == ST_WAIT_FRAME)
                ev = POLLIN;
            pfds[np].events = ev;
            pfds[np].revents = 0;
            conn_slot[i] = np++;
        }
#ifdef _WIN32
        int pr = WSAPoll(pfds, (ULONG)np, poll_timeout(im));
#else
        int pr = poll(pfds, (nfds_t)np, poll_timeout(im));
#endif
        if (im->stop) break;
        if (pr < 0) {
#ifdef _WIN32
            if (WSAGetLastError() == WSAEINTR) continue;
#else
            if (errno == EINTR) continue;
#endif
            break;
        }
#ifndef _WIN32
        if (wake_slot >= 0 && (pfds[wake_slot].revents & POLLIN)) {
            char buf[64];
            while (read(im->wake_r, buf, sizeof(buf)) > 0) {}
        }
#endif
        service_waits(im);
        if (pfds[listen_slot].revents & POLLIN) {
            for (;;) {
                intptr_t fd = (intptr_t)accept(N1G_SOCK(im->web->listen_fd), NULL, NULL);
                if (fd == N1G_WEB_INVALID_FD) break;
                set_nonblocking(fd);
                set_nodelay(fd);
                if (im->nconns >= N1G_WEB_MAX_CONN) {
                    close_fd(fd);
                    continue;
                }
                struct n1g_web_conn *c = &im->conns[im->nconns++];
                memset(c, 0, sizeof(*c));
                c->fd = fd;
                c->state = ST_READ;
            }
        }
        for (int i = live; i < im->nconns; i++) {
            if (!pump_read(&im->conns[i]) ||
                (im->conns[i].state == ST_READ && !process_buffer(im, &im->conns[i]))) {
                close_conn(im, i);
            }
        }
        for (int i = 0; i < live; i++) {
            struct n1g_web_conn *c = &im->conns[i];
            if (c->dead) continue;
            int slot = conn_slot[i];
            if (slot < 0 || slot >= np) continue;
            short rev = pfds[slot].revents;
            if (rev & (POLLERR | POLLHUP | POLLNVAL)) {
                if (c->state != ST_WRITE) {
                    close_conn(im, i);
                    continue;
                }
            }
            if ((rev & POLLIN) && c->state != ST_WRITE) {
                if (!pump_read(c)) {
                    close_conn(im, i);
                    continue;
                }
            }
            if (c->dead) continue;
            if (c->state == ST_READ) {
                if (!process_buffer(im, c)) {
                    close_conn(im, i);
                    continue;
                }
            }
            if (c->dead) continue;
            if (c->state == ST_WRITE && ((rev & POLLOUT) || c->out_off < c->out_len)) {
                if (!pump_write(c)) {
                    close_conn(im, i);
                    continue;
                }
                if (c->state == ST_READ && c->in_len && !process_buffer(im, c)) close_conn(im, i);
            }
        }
        service_waits(im);
        for (int i = 0; i < im->nconns; i++) {
            if (im->conns[i].dead) continue;
            if (im->conns[i].state != ST_WRITE) continue;
            if (!pump_write(&im->conns[i])) {
                close_conn(im, i);
                continue;
            }
            if (im->conns[i].state == ST_READ && im->conns[i].in_len &&
                !process_buffer(im, &im->conns[i])) {
                close_conn(im, i);
            }
        }
    }
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

static bool build_index(struct n1g_web_impl *im) {
    size_t n = 0;
    for (size_t i = 0; i < html_part_count; i++) n += strlen(html_parts[i]);
    char *buf = (char *)malloc(n + 1u);
    if (!buf) return false;
    size_t off = 0;
    for (size_t i = 0; i < html_part_count; i++) {
        size_t len = strlen(html_parts[i]);
        memcpy(buf + off, html_parts[i], len);
        off += len;
    }
    buf[off] = '\0';
    im->index_html = buf;
    im->index_len = off;
    return true;
}

bool n1g_web_start(n1g_state_t *s, n1g_web_server_t *web, uint16_t port) {
    const char *bind_host = (s && s->opts.web_bind && s->opts.web_bind[0]) ? s->opts.web_bind : "127.0.0.1";
    memset(web, 0, sizeof(*web));
    web->listen_fd = N1G_WEB_INVALID_FD;
    web->port = port;
    web->last_lcd_words = UINT64_MAX;
    web->last_backlight_generation = UINT64_MAX;
    struct n1g_web_impl *im = (struct n1g_web_impl *)calloc(1, sizeof(*im));
    if (!im) return false;
    im->web = web;
    im->wake_r = -1;
    im->wake_w = -1;
    im->frame_dirty = true;
    snprintf(im->bind_text, sizeof(im->bind_text), "%s", bind_host);
    web->impl = im;
#ifdef _WIN32
    InitializeCriticalSection(&im->mu);
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        n1g_info(s, "web frontend failed to initialize Winsock");
        free(im);
        web->impl = NULL;
        return false;
    }
#else
    pthread_mutex_init(&im->mu, NULL);
    int pipes[2];
    if (pipe(pipes) != 0) {
        n1g_info(s, "web frontend failed to create wake pipe");
        pthread_mutex_destroy(&im->mu);
        free(im);
        web->impl = NULL;
        return false;
    }
    im->wake_r = pipes[0];
    im->wake_w = pipes[1];
    set_nonblocking(im->wake_r);
    set_nonblocking(im->wake_w);
#endif
    if (!build_index(im)) {
        n1g_info(s, "web frontend failed to build the page");
        n1g_web_stop(web);
        return false;
    }
    intptr_t fd = (intptr_t)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == N1G_WEB_INVALID_FD) {
        n1g_info(s, "web frontend failed to create socket");
        n1g_web_stop(web);
        return false;
    }
    int yes = 1;
    (void)setsockopt(N1G_SOCK(fd), SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, bind_host, &addr.sin_addr) != 1) {
        n1g_info(s, "web frontend rejected bind address %s", bind_host);
        close_fd(fd);
        n1g_web_stop(web);
        return false;
    }
    if (bind(N1G_SOCK(fd), (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(N1G_SOCK(fd), 16) != 0 || !set_nonblocking(fd)) {
        n1g_info(s, "web frontend failed to listen on %s:%u", bind_host, (unsigned)port);
        close_fd(fd);
        n1g_web_stop(web);
        return false;
    }
    web->listen_fd = fd;
#ifdef _WIN32
    im->thread = CreateThread(NULL, 0, http_thread, im, 0, NULL);
    im->thread_started = im->thread != NULL;
#else
    im->thread_started = pthread_create(&im->thread, NULL, http_thread, im) == 0;
#endif
    if (!im->thread_started) {
        n1g_info(s, "web frontend failed to start the HTTP thread");
        n1g_web_stop(web);
        return false;
    }
    web->active = true;
    n1g_info(s, "web frontend listening on http://%s:%u/", bind_host, (unsigned)port);
    return true;
}

void n1g_web_stop(n1g_web_server_t *web) {
    if (!web || !web->impl) return;
    struct n1g_web_impl *im = impl_of(web);
    im->stop = true;
    wake_http(im);
    if (im->thread_started) {
#ifdef _WIN32
        WaitForSingleObject(im->thread, INFINITE);
        CloseHandle(im->thread);
#else
        pthread_join(im->thread, NULL);
#endif
        im->thread_started = false;
    }
    for (int i = 0; i < im->nconns; i++) close_conn(im, i);
    close_fd(web->listen_fd);
    web->listen_fd = N1G_WEB_INVALID_FD;
#ifndef _WIN32
    if (im->wake_r >= 0) close(im->wake_r);
    if (im->wake_w >= 0) close(im->wake_w);
#endif
    free(im->index_html);
#ifdef _WIN32
    DeleteCriticalSection(&im->mu);
    WSACleanup();
#else
    pthread_mutex_destroy(&im->mu);
#endif
    free(im);
    web->impl = NULL;
    web->active = false;
}

static bool build_status(n1g_state_t *s, n1g_web_server_t *web, bool running,
                         char *body, size_t cap, size_t *out_len) {
    char escaped_error[256];
    json_escape(web->restart_error, escaped_error, sizeof(escaped_error));
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
    int n = snprintf(body,
                     cap,
                     "{\"running\":%s,\"frame_seq\":%llu,\"guest_insns\":%llu,"
                     "\"scheduled_insns\":%llu,\"active_slices\":%llu,"
                     "\"cpu_slice_calls\":%llu,\"cop_slice_calls\":%llu,"
                     "\"halted_ticks\":%llu,\"fast_forwarded_ticks\":%llu,"
                     "\"timing_boundaries\":%llu,\"mmio_callbacks\":%llu,"
                     "\"device_ticks\":%llu,\"rtc_usec_per_tick\":%u,"
                     "\"rtc_bcd\":\"%02x-%02x-%02xT%02x:%02x:%02x\","
                     "\"rtc_second_events\":%llu,\"rtc_alarm_events\":%llu,"
                     "\"pcf_standby\":%s,\"pcf_standby_requests\":%llu,"
                     "\"pcf_standby_transitions\":%llu,\"pcf_wake_requests\":%llu,"
                     "\"pcf_last_wake\":%u,\"pcf_adc_ready\":%s,"
                     "\"pcf_adc_conversions\":%llu,\"pcf_low_battery_events\":%llu,"
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
                     "\"battery_percent\":%u,\"battery_mv\":%u,\"battery_low\":%s,"
                     "\"main_charger\":%s,\"usb_charger\":%s,\"hold\":%s,"
                     "\"force_backlight_on\":%s,"
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
                     "\"cpu_pc\":\"0x%08x\",\"restart_error\":\"%s\"}\n",
                     running ? "true" : "false",
                     (unsigned long long)web->frame_seq,
                     (unsigned long long)s->counters.guest_insns,
                     (unsigned long long)s->counters.scheduled_insns,
                     (unsigned long long)s->counters.active_slices,
                     (unsigned long long)s->counters.cpu_slice_calls,
                     (unsigned long long)s->counters.cop_slice_calls,
                     (unsigned long long)s->counters.halted_ticks,
                     (unsigned long long)s->counters.fast_forwarded_ticks,
                     (unsigned long long)s->counters.timing_boundaries,
                     (unsigned long long)s->counters.mmio_callbacks,
                     (unsigned long long)s->counters.device_ticks,
                     s->opts.rtc_usec_per_tick,
                     rtc[6], rtc[5], rtc[4], rtc[2], rtc[1], rtc[0],
                     (unsigned long long)s->i2c.rtc_second_interrupts,
                     (unsigned long long)s->i2c.rtc_alarm_interrupts,
                     s->i2c.pcf_standby ? "true" : "false",
                     (unsigned long long)s->i2c.pcf_standby_requests,
                     (unsigned long long)s->i2c.pcf_standby_transitions,
                     (unsigned long long)s->i2c.pcf_wake_requests,
                     (unsigned)s->i2c.pcf_last_wake,
                     s->i2c.pcf_adc_ready ? "true" : "false",
                     (unsigned long long)s->i2c.pcf_adc_conversions,
                     (unsigned long long)s->i2c.pcf_low_battery_events,
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
                     n1g_dev_i2c_battery_mv(s),
                     s->i2c.pcf_low_battery ? "true" : "false",
                     s->opts.main_charger_connected ? "true" : "false",
                     s->opts.usb_charger_connected ? "true" : "false",
                     s->opts.hold_switch_engaged ? "true" : "false",
                     web->force_backlight_on ? "true" : "false",
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
                     n1g_cpu_pc(s, N1G_CORE_CPU),
                     escaped_error);
    if (n <= 0 || (size_t)n >= cap) return false;
    *out_len = (size_t)n;
    return true;
}

