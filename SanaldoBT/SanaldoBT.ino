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

struct Motor { uint8_t pwm, in1, in2, ch, trim; };
Motor motors[2] = {
  { 23, 22, 21, 0, 100 },   // LEFT : ENA, IN1, IN2  (trim %: drop the faster side to roll straight)
  { 19, 18, 13, 1, 100 },   // RIGHT: ENB, IN3, IN4
};

const int PWM_FREQ = 20000, PWM_RES = 8;
int speedPct = 100;                 // 0..100, set by the app's speed slider

// one motor: signed -127..127 -> direction pins + PWM duty (x speedPct x per-side trim)
void drive(const Motor& m, int speed) {
  digitalWrite(m.in1, speed > 0);
  digitalWrite(m.in2, speed < 0);
  ledcWrite(m.ch, map(abs(speed) * speedPct / 100, 0, 127, 0, 255) * m.trim / 100);
}

// both wheels at once
void setWheels(int left, int right) {
  drive(motors[0], left);
  drive(motors[1], right);
}

// spin in place: wheels run opposite ways (steering with no throttle)
void tankTurn(int turn) { setWheels(turn, -turn); }

// steering eases the inner wheel toward 0 (it slows, never reverses)
void arcadeDrive(int throttle, int turn) {
  int inner = throttle * (127 - abs(turn)) / 127;
  if (turn > 0) setWheels(throttle, inner);   // turn right -> slow right
  else          setWheels(inner, throttle);   // turn left  -> slow left (turn 0 -> straight)
}

// stopped + steering -> tank turn, else drive-and-steer
void move(int throttle, int turn) {
  if (throttle == 0 && turn != 0) tankTurn(turn);
  else                            arcadeDrive(throttle, turn);
}

// full fwd/back jog. If wheels move here but not from the app -> the phone link, not wiring.
void selftest() {
  Serial.println(F("[TEST] fwd / back / stop"));
  speedPct = 100;                   // ignore the app's slider — test at full power
  setWheels(127, 127); delay(500);
  setWheels(-127, -127); delay(500);
  setWheels(0, 0);
}

void handle(char c) {
  switch (c) {
    case 'F': move( 127,    0); break;  // forward
    case 'G': move(-127,    0); break;  // back
    case 'L': move(   0, -127); break;  // tank turn left  (throttle 0 -> spin in place)
    case 'R': move(   0,  127); break;  // tank turn right (throttle 0 -> spin in place)
    case 'Q': move( 127,  -64); break;  // forward-left
    case 'E': move( 127,   64); break;  // forward-right
    case 'Z': move(-127,  -64); break;  // back-left
    case 'C': move(-127,   64); break;  // back-right
    case 'S': move(   0,    0); break;  // stop
    case 'q': speedPct = 100; break;    // full speed
    default:  speedPct = (c - '0') * 100 / 9; break;  // '0'..'9' slider (pre-filtered in pump)
  }
  Serial.printf("cmd=%c  speed=%d%%\n", c, speedPct);
}

// one worker for USB + phone serial: known letters drive, the word "selftest" runs the test
void pump(Stream& in) {
  static String buf;   // ponytail: shared by both streams, fine for one user at a time
  while (in.available()) {
    char c = in.read();
    if (c == '\n' || c == '\r') { buf = ""; continue; }
    if (strchr("FGLRQEZCSq0123456789", c)) handle(c);   // drive command
    buf += c;
    if (buf.endsWith("selftest")) { selftest(); buf = ""; }
    else if (buf.length() > 16)     buf = "";           // cap the buffer
  }
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
  Serial.println(F("[BOOT] Type 'selftest' to jog the motors. Pair to 'SanaldoBT' in the app."));
}

void loop() {
  pump(Serial);                            // USB monitor: "selftest", or drive by hand
  pump(SerialBT);                          // phone app: drive commands + "selftest"
  if (!SerialBT.hasClient()) move(0, 0);   // safety: no phone -> no move
}
