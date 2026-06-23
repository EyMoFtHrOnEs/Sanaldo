// SanaldoWifi.ino — 2WD car controlled from a phone over Wi-Fi (WebSocket).
// This one JOINS an existing Wi-Fi network (no hotspot). For the standalone
// hotspot version see ../SanaldoHotspot.
//
// Board:   Deneyap Mini v2 (ESP32-S2) — Arduino core 2.0.x.
//          The S2 has NO Bluetooth, but it DOES have Wi-Fi, so this is the way
//          to drive this board from a phone with no extra module.
//          Tools -> USB CDC On Boot: Enabled  (so Serial = USB, for the log).
// Driver:  L298N
// Libs:    ESPAsyncWebServer + AsyncTCP   (Library Manager: "ESPAsyncWebServer"
//          by ESP32Async, pulls in AsyncTCP). WiFi is built in.
//
// HOW IT WORKS:
//   1. ESP32-S2 joins your Wi-Fi network (set WIFI_SSID / WIFI_PASS below).
//   2. The Serial Monitor prints the IP it got (e.g. 192.168.1.42).
//   3. Open http://<that-ip>/ on a phone/PC ON THE SAME NETWORK -> joystick UI.
//   4. Two on-screen joysticks: LEFT = forward/back, RIGHT = left/right.
//   5. Every message from the web is logged to USB Serial for debugging.
//
// WIRING — Deneyap Mini v2 silk (D-label) -> L298N. SEPARATE motor battery,
// shared GND, never the S2's pins. These are safe broken-out header pins
// (no strapping 0/45/46, no USB 19/20, no flash/PSRAM 26-32, no UART0 43/44,
//  not the onboard RGB on 33):
//   D2 (GPIO42) -> ENA   D3 (GPIO41) -> IN1   D4 (GPIO40) -> IN2   (LEFT)
//   D5 (GPIO39) -> ENB   D6 (GPIO38) -> IN3   D7 (GPIO37) -> IN4   (RIGHT)

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

const char* WIFI_SSID = "YOUR_WIFI";       // <-- your network name
const char* WIFI_PASS = "YOUR_PASSWORD";   // <-- your network password
const int   FAILSAFE_MS = 500;   // no message for this long -> stop (link dropped)

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
uint32_t lastMsg = 0;

// ---- motors (same mix as the other Sanaldo sketches) ------------------------

struct Motor { uint8_t pwm, in1, in2, ch, trim; };
Motor motors[2] = {
  { 42, 41, 40, 0, 100 },   // LEFT : D2/ENA, D3/IN1, D4/IN2  (trim %: drop the faster side to roll straight)
  { 39, 38, 37, 1, 100 },   // RIGHT: D5/ENB, D6/IN3, D7/IN4
};
const int PWM_FREQ = 20000, PWM_RES = 8;

void drive(const Motor& m, int speed) {
  digitalWrite(m.in1, speed > 0);
  digitalWrite(m.in2, speed < 0);
  ledcWrite(m.ch, map(abs(speed), 0, 127, 0, 255) * m.trim / 100);
}
void setWheels(int left, int right) { drive(motors[0], left); drive(motors[1], right); }
void stop() { setWheels(0, 0); }

// onboard RGB (Deneyap Mini v2): boot proof + live status
void rgb(uint8_t r, uint8_t g, uint8_t b) { neopixelWrite(RGB_BUILTIN, r, g, b); }

// throttle/turn (-127..127) -> wheels. No throttle + steering = spin in place.
void move(int throttle, int turn) {
  int left, right;
  if (throttle == 0 && turn != 0) { left = turn; right = -turn; }
  else {
    int inner = throttle * (127 - abs(turn)) / 127;
    if (turn > 0) { left = throttle; right = inner; }   // turn right -> slow right
    else          { left = inner; right = throttle; }   // turn left / straight
  }
  setWheels(left, right);
}

// ---- web UI (inline; served straight off the board) -------------------------

const char PAGE[] PROGMEM = R"HTML(<!doctype html><html><head>
<meta name=viewport content="width=device-width,initial-scale=1,user-scalable=no">
<title>Sanaldo</title><style>
*{margin:0;-webkit-user-select:none;user-select:none;touch-action:none}
body{background:#111;height:100vh;display:flex;align-items:center;justify-content:space-around;font-family:sans-serif}
.pad{width:40vw;height:40vw;max-width:300px;max-height:300px;border-radius:50%;background:#222;border:2px solid #444;position:relative}
.knob{width:40%;height:40%;border-radius:50%;background:#0af;position:absolute;left:30%;top:30%}
#s{position:fixed;top:8px;left:8px;color:#888;font-size:14px}
</style></head><body>
<div id=s>baglaniyor...</div>
<div class=pad id=L><div class=knob></div></div>
<div class=pad id=R><div class=knob></div></div>
<script>
function stick(el,axis){
  const knob=el.querySelector('.knob'); let val=0,id=null;
  function set(cx,cy){
    const r=el.getBoundingClientRect(), R=r.width/2;
    let dx=cx-(r.left+R), dy=cy-(r.top+R), d=Math.hypot(dx,dy);
    if(d>R){dx*=R/d;dy*=R/d;}
    knob.style.transform=`translate(${dx}px,${dy}px)`;
    val = axis=='y' ? -dy/R : dx/R;                 // up = forward, right = +
  }
  function reset(){val=0;knob.style.transform='translate(0,0)';id=null;}
  el.onpointerdown=e=>{id=e.pointerId;el.setPointerCapture(id);set(e.clientX,e.clientY);};
  el.onpointermove=e=>{if(e.pointerId==id)set(e.clientX,e.clientY);};
  el.onpointerup=el.onpointercancel=reset;
  return ()=>Math.round(val*127);
}
const thr=stick(L,'y'), turn=stick(R,'x'), s=document.getElementById('s');
let ws;
function conn(){
  ws=new WebSocket('ws://'+location.host+'/ws');
  ws.onopen=()=>s.textContent='bagli';
  ws.onclose=()=>{s.textContent='koptu, tekrar...';setTimeout(conn,500);};
}
conn();
setInterval(()=>{ if(ws&&ws.readyState==1) ws.send(thr()+','+turn()); },60);  // 60ms heartbeat
</script></body></html>)HTML";

// ---- websocket --------------------------------------------------------------

void onWs(AsyncWebSocket*, AsyncWebSocketClient* c, AwsEventType type, void* arg, uint8_t* data, size_t len) {
  if (type == WS_EVT_CONNECT)         { Serial.printf("[WS] client %u connected\n", c->id()); }
  else if (type == WS_EVT_DISCONNECT) { Serial.printf("[WS] client %u gone\n", c->id()); stop(); }
  else if (type == WS_EVT_DATA) {
    AwsFrameInfo* info = (AwsFrameInfo*)arg;
    if (!(info->final && info->index == 0 && info->len == len)) return;  // ignore split frames
    data[len] = 0;
    int throttle = 0, turn = 0;
    sscanf((char*)data, "%d,%d", &throttle, &turn);
    throttle = constrain(throttle, -127, 127);
    turn     = constrain(turn,     -127, 127);
    Serial.printf("[WS] \"%s\" -> thr=%4d turn=%4d\n", (char*)data, throttle, turn);  // log everything
    move(throttle, turn);
    lastMsg = millis();
  }
}

// ---- setup / loop -----------------------------------------------------------

void setup() {
  Serial.begin(115200);
  for (auto& m : motors) {
    pinMode(m.in1, OUTPUT); pinMode(m.in2, OUTPUT);
    ledcSetup(m.ch, PWM_FREQ, PWM_RES);
    ledcAttachPin(m.pwm, m.ch);
  }
  stop();
  for (int i = 0; i < 3; i++) { rgb(10, 10, 10); delay(120); rgb(0, 0, 0); delay(120); }  // boot proof

  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);             // lower radio current -> avoid brownout reset loop
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[BOOT] joining \"%s\"", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {         // blink amber until joined
    rgb(12, 6, 0); delay(150); rgb(0, 0, 0); delay(150);
    Serial.print('.');
  }
  Serial.printf("\n[BOOT] connected. Drive from http://%s/ (same network).\n",
                WiFi.localIP().toString().c_str());

  ws.onEvent(onWs);
  server.addHandler(&ws);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* r){ r->send_P(200, "text/html", PAGE); });
  server.begin();
}

void loop() {
  ws.cleanupClients();
  if (millis() - lastMsg > FAILSAFE_MS) stop();   // link quiet -> don't run away
  bool linked = ws.count() > 0;                   // a phone/browser is connected
  rgb(0, linked ? 0 : 8, linked ? 12 : 0);        // green = no client, blue = client connected
  delay(2);
}
