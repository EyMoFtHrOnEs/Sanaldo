// ArduinoNanoHC06.ino — 2WD car driven by the "Arduino Bluetooth RC Car" phone
// app over an HC-06 (Bluetooth Classic SPP). Arduino Nano / Uno (ATmega328P).
//
// Same control scheme as the ESP32 sketch, ported to AVR:
//   - no LEDC peripheral -> analogWrite() for PWM (ENA/ENB must be PWM pins)
//   - no built-in radio   -> HC-06 on SoftwareSerial (USB stays free for debug)
//
// App default 3x3 D-pad: Q F E / L S R / Z G C  (+ '0'..'9' speed, 'q' full).
//
// WIRING (Nano -> L298N):
//   D5 -> ENA   D2 -> IN1   D4 -> IN2   (LEFT)   <- D5,D6 are PWM pins
//   D6 -> ENB   D7 -> IN3   D8 -> IN4   (RIGHT)
//   GND -> L298N GND (shared). Motor power from a SEPARATE battery.
//
// WIRING (Nano -> HC-06):
//   HC-06 TX  -> D10 (Nano RX)
//   HC-06 RX  -> D11 (Nano TX) THROUGH a divider (5V->~3.3V: 1k in series, 2k to GND)
//   HC-06 STATE -> D12 (HIGH when a phone is connected; used for the safety stop)
//   VCC 5V, GND. Default pair code 1234.  Unplug HC-06 while uploading.

#include <SoftwareSerial.h>
SoftwareSerial bt(10, 11);          // RX, TX
const int BT_STATE = 12;            // HC-06 STATE pin (set USE_STATE 0 if unwired)
#define USE_STATE 1

struct Motor { uint8_t pwm, in1, in2, trim; };
Motor motors[2] = {
  { 5, 2, 4, 100 },   // LEFT : ENA, IN1, IN2  (trim %: drop the faster side to roll straight)
  { 6, 7, 8, 100 },   // RIGHT: ENB, IN3, IN4
};

int speedPct = 100;                 // 0..100, set by the app's speed slider

// one motor: signed -127..127 -> direction pins + PWM duty (x speedPct x per-side trim)
void drive(const Motor& m, int speed) {
  digitalWrite(m.in1, speed > 0);
  digitalWrite(m.in2, speed < 0);
  int duty = map(abs(speed) * speedPct / 100, 0, 127, 0, 255) * m.trim / 100;
  analogWrite(m.pwm, duty);
}

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
  Serial.print(F("cmd=")); Serial.print(c);
  Serial.print(F("  speed=")); Serial.println(speedPct);
}

// one worker for USB + phone serial: known letters drive, the word "selftest" runs the test
void pump(Stream& in) {
  static String buf;   // ponytail: shared by both streams, fine for one user at a time
  while (in.available()) {
    char c = in.read();
    if (c == '\n' || c == '\r') { buf = ""; continue; }
    if (strchr("FGLRQEZCSq0123456789", c)) { handle(c); buf = ""; continue; }  // drive command
    buf += c;                                           // otherwise build toward "selftest"
    if (buf.endsWith("selftest")) { selftest(); buf = ""; }
    else if (!String("selftest").startsWith(buf)) {     // not a drive cmd, not building selftest
      Serial.print(F("unknown: ")); Serial.println(c);
      buf = String("selftest").startsWith(String(c)) ? String(c) : "";
    }
  }
}

void setup() {
  Serial.begin(115200);
  bt.begin(9600);                   // HC-06 default baud
  pinMode(BT_STATE, INPUT);
  for (auto& m : motors) {
    pinMode(m.in1, OUTPUT);
    pinMode(m.in2, OUTPUT);
  }
  move(0, 0);
  Serial.println(F("[BOOT] Type 'selftest' to jog the motors. Pair to HC-06 (code 1234) in the app."));
}

void loop() {
  pump(Serial);                                  // USB monitor: "selftest", or drive by hand
  pump(bt);                                       // phone app: drive commands + "selftest"
#if USE_STATE
  if (digitalRead(BT_STATE) == LOW) move(0, 0);   // safety: phone disconnected -> stop
#endif
}
