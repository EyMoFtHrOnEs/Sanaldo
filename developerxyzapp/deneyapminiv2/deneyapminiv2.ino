// deneyapminiv2.ino — 2WD car driven by the "BT Arduino Car Controller" phone
// app (Devloper.xyz) over Wi-Fi. WiFi port of the HC-06 Nano sketch.
//
// Board:  Deneyap Mini v2 (ESP32-S2) — Arduino core 2.0.x.
//         The S2 has NO Bluetooth but DOES have Wi-Fi, so the app's WiFi mode
//         is the way to drive it with no extra module.
//         Tools -> USB CDC On Boot: Enabled  (so Serial = USB, for the log).
// Driver: L298N
//
// HOW: the board makes an open hotspot; join it on the phone, then in the app
// pick "WiFi" mode pointing at 192.168.4.1 : 80. Each button is an HTTP GET
//   /?State=F        (F=fwd B=back L=left R=right  I/G/J/H=diagonals  S=stop)
//   /?State=5        ('0'..'9' = speed slider).  Anything else -> "unknown".
//
// WIRING — Deneyap Mini v2 silk (D-label) -> L298N. Separate motor battery,
// shared GND. Safe broken-out pins (no strapping/USB/flash/UART0/onboard-RGB):
//   D2 (GPIO42) -> ENA   D3 (GPIO41) -> IN1   D4 (GPIO40) -> IN2   (LEFT)
//   D5 (GPIO39) -> ENB   D6 (GPIO38) -> IN3   D7 (GPIO37) -> IN4   (RIGHT)

#include <WiFi.h>

const char* AP_SSID = "SanaldoCar";   // join this network; app talks to 192.168.4.1:80
const char* AP_PASS = "";             // open network ("" ) — set 8+ chars to lock it

struct Motor { uint8_t pwm, in1, in2, ch, trim; };
Motor motors[2] = {            // use the board's D-label macros = safe broken-out pins by construction
  { D2, D3, D4, 0, 100 },   // LEFT : D2/ENA(GPIO42), D3/IN1(41), D4/IN2(40)  (trim %: drop faster side to roll straight)
  { D5, D6, D7, 1, 100 },   // RIGHT: D5/ENB(GPIO39), D6/IN3(38), D7/IN4(37)
};
const int PWM_FREQ = 20000, PWM_RES = 8;

int speedCar = 200;                 // 0..255, set by the app's '0'..'9' slider
const int speed_Coeff = 3;          // diagonal: inner wheel runs at speed/speed_Coeff
unsigned long lastCmd = 0;          // deadman timer (HTTP is stateless; stop if the link goes quiet)
const unsigned long TIMEOUT = 600;  // no command for this long -> stop

WiFiServer server(80);

// one wheel, signed -255..255: direction pins + PWM magnitude (x per-side trim)
void wheel(const Motor& m, int s) {
  digitalWrite(m.in1, s > 0);
  digitalWrite(m.in2, s < 0);
  ledcWrite(m.ch, abs(s) * m.trim / 100);
}
#define L(s) wheel(motors[0], (s))
#define R(s) wheel(motors[1], (s))

void rgb(uint8_t r, uint8_t g, uint8_t b) { neopixelWrite(RGB_BUILTIN, r, g, b); }

// full fwd/back jog — proves wiring independent of the phone link
void selftest() {
  Serial.println(F("[TEST] fwd / back / stop"));
  L(200); R(200); delay(500);
  L(-200); R(-200); delay(500);
  L(0); R(0);
}

void handle(char c) {
  lastCmd = millis();
  int v = speedCar, w = speedCar / speed_Coeff;
  switch (c) {
    case 'F': L( v); R( v); break;        // forward
    case 'B': L(-v); R(-v); break;        // back
    case 'L': L(-v); R( v); break;        // spin left
    case 'R': L( v); R(-v); break;        // spin right
    case 'I': L( v); R( w); break;        // forward-right
    case 'G': L( w); R( v); break;        // forward-left
    case 'J': L(-v); R(-w); break;        // back-right
    case 'H': L(-w); R(-v); break;        // back-left
    case 'S': L( 0); R( 0); break;        // stop
    case '0' ... '9': speedCar = map(c - '0', 0, 9, 100, 255); break;  // slider
    case 'T': selftest(); break;          // USB: type T to jog the motors
    default:  Serial.print(F("unknown: ")); Serial.println(c); return;
  }
  Serial.print(F("cmd=")); Serial.print(c);
  Serial.print(F("  speed=")); Serial.println(speedCar);
}

void setup() {
  Serial.begin(115200);
  for (auto& m : motors) {
    pinMode(m.in1, OUTPUT); pinMode(m.in2, OUTPUT);
    ledcSetup(m.ch, PWM_FREQ, PWM_RES);
    ledcAttachPin(m.pwm, m.ch);
  }
  L(0); R(0);
  for (int i = 0; i < 3; i++) { rgb(10, 10, 10); delay(120); rgb(0, 0, 0); delay(120); }  // boot proof

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  server.begin();
  Serial.print(F("[BOOT] hotspot \"")); Serial.print(AP_SSID);
  Serial.print(F("\" up at http://")); Serial.println(WiFi.softAPIP());  // 192.168.4.1
  Serial.println(F("[BOOT] Join it on the phone, point the app at that IP. Type T for selftest."));
}

void loop() {
  // USB monitor: T = selftest, or drive by hand
  while (Serial.available()) { char c = Serial.read(); if (c != '\n' && c != '\r') handle(c); }

  // phone app: one HTTP GET per button -> /?State=X
  WiFiClient client = server.available();
  if (client) {
    String line = client.readStringUntil('\r');     // "GET /?State=F HTTP/1.1"
    int i = line.indexOf("State=");
    if (i >= 0) handle(line[i + 6]);
    client.print(F("HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"));
    client.stop();
  }

  bool stale = millis() - lastCmd > TIMEOUT;
  if (stale) { L(0); R(0); }                         // link quiet -> don't run away
  rgb(0, stale ? 8 : 0, stale ? 0 : 12);             // green idle / blue while driving
}
