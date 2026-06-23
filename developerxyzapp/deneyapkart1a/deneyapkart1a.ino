// deneyapkart1a.ino — 2WD car driven by the "BT Arduino Car Controller" phone
// app (Devloper.xyz) over Bluetooth Classic (SPP). BT port of the Nano sketch.
//
// Board:  Deneyap Kart 1A (ESP32-WROVER-E) — Arduino core 2.0.x.
//         The WROVER has Bluetooth Classic, so the app's Bluetooth mode drives
//         it with no extra module (unlike the ESP32-S2, which is Wi-Fi only).
//         Tools -> Partition Scheme -> Huge APP (3MB No OTA)  <-- REQUIRED for BT.
//         Pairs as "Sanaldo BT" (same name as the Nano version).
// Driver: L298N
//
// App protocol (one letter per button):
//   F=fwd  B=back  L=left  R=right   S=stop
//   I=fwd-right  G=fwd-left   J=back-right  H=back-left   (diagonals: inner wheel slowed)
//   '0'..'9' = speed slider.  Anything else -> printed as "unknown".
//
// WIRING — Deneyap Kart 1A silk (D-label) -> L298N. Separate motor battery,
// shared GND:
//   D0 (GPIO23) -> ENA   D1 (GPIO22) -> IN1   D4 (GPIO21) -> IN2   (LEFT)
//   D5 (GPIO19) -> ENB   D6 (GPIO18) -> IN3   D12(GPIO13) -> IN4   (RIGHT)

#include "BluetoothSerial.h"
BluetoothSerial SerialBT;

struct Motor { uint8_t pwm, in1, in2, ch, trim; };
Motor motors[2] = {            // trim %: drop the faster side to roll straight
  { 23, 22, 21, 0, 100 },   // LEFT : D0/ENA(GPIO23), D1/IN1(22), D4/IN2(21)
  { 19, 18, 13, 1, 100 },   // RIGHT: D5/ENB(GPIO19), D6/IN3(18), D12/IN4(13)
};
const int PWM_FREQ = 20000, PWM_RES = 8;

int speedCar = 200;                 // 0..255, set by the app's '0'..'9' slider
const int speed_Coeff = 3;          // diagonal: inner wheel runs at speed/speed_Coeff
unsigned long lastCmd = 0;          // deadman timer (stop if the link goes quiet)
const unsigned long TIMEOUT = 600;  // no command for this long -> stop

// one wheel, signed -255..255: direction pins + PWM magnitude (x per-side trim)
void wheel(const Motor& m, int s) {
  digitalWrite(m.in1, s > 0);
  digitalWrite(m.in2, s < 0);
  ledcWrite(m.ch, abs(s) * m.trim / 100);
}
#define L(s) wheel(motors[0], (s))
#define R(s) wheel(motors[1], (s))

// full fwd/back jog — if wheels move here but not from the app, it's the phone link, not wiring
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

void pump(Stream& in) {
  while (in.available()) {
    char c = in.read();
    if (c != '\n' && c != '\r') handle(c);
  }
}

void setup() {
  Serial.begin(115200);
  for (auto& m : motors) {
    pinMode(m.in1, OUTPUT); pinMode(m.in2, OUTPUT);
    ledcSetup(m.ch, PWM_FREQ, PWM_RES);
    ledcAttachPin(m.pwm, m.ch);
  }
  L(0); R(0);
  SerialBT.begin("Sanaldo BT");         // pair to "Sanaldo BT" in the app
  Serial.println(F("[BOOT] Pair to 'Sanaldo BT' in the app. Type T to test the motors."));
}

void loop() {
  pump(Serial);                                 // USB monitor: T = selftest, or drive by hand
  pump(SerialBT);                               // phone app: drive commands
  if (millis() - lastCmd > TIMEOUT) { L(0); R(0); }   // safety: stale link -> stop
}
