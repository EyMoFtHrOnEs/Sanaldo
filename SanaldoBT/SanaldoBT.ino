// SanaldoBT.ino — 2WD car driven by the "Arduino Bluetooth RC Car" phone app
// over Bluetooth Classic (SPP). Same board and wiring as the PS5 sketch.
//
// Board:   Deneyap Kart 1A  (ESP32-WROVER-E)
// Driver:  L298N
// App:     "Arduino Bluetooth RC Car" — pair to "SanaldoBT", then connect.
//
// The app sends one letter per button. Default 3x3 D-pad layout:
//   Q F E      forward-left / FORWARD / forward-right
//   L S R      LEFT / stop / RIGHT
//   Z G C      back-left  / BACK    / back-right
// Speed slider sends '0'..'9' (and 'q' = full). Lights/sensors (M,N,T,D...) ignored.
//
// WIRING (Deneyap 1A silk label -> L298N), all safe GPIOs:
//   D0 (GPIO23) -> ENA   D1 (GPIO22) -> IN1   D4 (GPIO21) -> IN2   (LEFT)
//   D5 (GPIO19) -> ENB   D6 (GPIO18) -> IN3   D12(GPIO13) -> IN4   (RIGHT)
//
// Tools -> Partition Scheme -> Huge APP (3MB No OTA)   <-- REQUIRED for BT.

#include "BluetoothSerial.h"
BluetoothSerial SerialBT;

struct Motor { uint8_t pwm, in1, in2, ch; };
Motor motors[2] = {
  { 23, 22, 21, 0 },   // LEFT : ENA, IN1, IN2
  { 19, 18, 13, 1 },   // RIGHT: ENB, IN3, IN4
};

const int PWM_FREQ = 20000, PWM_RES = 8;
int speedPct = 100;                 // 0..100, set by the app's speed slider

// signed -127..127 -> direction pins + PWM duty (scaled by speedPct)
void drive(const Motor& m, int speed) {
  digitalWrite(m.in1, speed > 0);
  digitalWrite(m.in2, speed < 0);
  ledcWrite(m.ch, map(abs(speed) * speedPct / 100, 0, 127, 0, 255));
}

// Mix throttle+turn (-127..127) into the two wheels.
void move(int throttle, int turn) {
  drive(motors[0], constrain(throttle + turn, -127, 127));
  drive(motors[1], constrain(throttle - turn, -127, 127));
}

void handle(char c) {
  switch (c) {
    case 'F': move( 127,    0); break;  // forward
    case 'G': move(-127,    0); break;  // back
    case 'L': move(   0, -127); break;  // spin left
    case 'R': move(   0,  127); break;  // spin right
    case 'Q': move( 127,  -64); break;  // forward-left
    case 'E': move( 127,   64); break;  // forward-right
    case 'Z': move(-127,  -64); break;  // back-left
    case 'C': move(-127,   64); break;  // back-right
    case 'S': move(   0,    0); break;  // stop
    case 'q': speedPct = 100; break;    // full speed
    default:
      if (c >= '0' && c <= '9') speedPct = (c - '0') * 100 / 9;
      else return;                      // ignore lights/sensors (M,N,T,D,...)
  }
  Serial.printf("cmd=%c  speed=%d%%\n", c, speedPct);
}

void setup() {
  Serial.begin(115200);
  for (auto& m : motors) {
    pinMode(m.in1, OUTPUT);
    pinMode(m.in2, OUTPUT);
    ledcSetup(m.ch, PWM_FREQ, PWM_RES);
    ledcAttachPin(m.pwm, m.ch);
  }
  move(0, 0);
  SerialBT.begin("SanaldoBT");
  Serial.println(F("[BOOT] Pair to 'SanaldoBT' in the app."));
}

void loop() {
  if (SerialBT.available()) handle(SerialBT.read());
  if (!SerialBT.hasClient()) move(0, 0);   // safety: no phone -> no move
}
