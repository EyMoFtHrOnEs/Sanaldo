// ps5_2wd_car.ino — 2WD car driven by a PS5 DualSense over Bluetooth Classic.
//
// Board:   Deneyap Kart 1A  (plain ESP32-WROVER-E)
// Driver:  L298N  (IN1-4 + ENA/ENB)
// Library: esp-ps5  -> https://github.com/HamzaYslmn/esp-ps5
//
// Arcade drive: left stick Y = throttle, X = turn (mixed into both wheels).
//
// WIRING (Deneyap 1A silk label -> L298N).  Pins below are written as raw
// GPIO numbers so the sketch compiles under either "ESP32 Dev Module" or
// the Deneyap board package. Share GND with the L298N; motor power comes
// from a SEPARATE battery, never from the board's 5V/3V3.
//   D0 (GPIO23) -> ENA    D1 (GPIO22) -> IN1    D4 (GPIO21) -> IN2   (LEFT)
//   D5 (GPIO19) -> ENB    D6 (GPIO18) -> IN3    D12(GPIO13) -> IN4   (RIGHT)
//
// All six are "safe" outputs: no strapping pins (0,2,5,12,15), no UART0
// (1,3), no WROVER PSRAM pins (16,17).
//
// Tools -> Partition Scheme -> Huge APP (3MB No OTA)   <-- REQUIRED for BT stack.

#include <ps5Controller.h>

struct Motor { uint8_t pwm, in1, in2, ch; };
Motor motors[2] = {
  { 23, 22, 21, 0 },   // LEFT : ENA, IN1, IN2
  { 19, 18, 13, 1 },   // RIGHT: ENB, IN3, IN4
};

const int DEADZONE = 16;            // ignore stick drift near center (0..127)
const int PWM_FREQ = 20000;         // 20 kHz, above audible range
const int PWM_RES  = 8;             // 0..255 duty

// signed -127..127 -> direction pins + PWM duty
void drive(const Motor& m, int speed) {
  digitalWrite(m.in1, speed > 0);
  digitalWrite(m.in2, speed < 0);
  ledcWrite(m.ch, map(abs(speed), 0, 127, 0, 255));
}

void stop() { for (auto& m : motors) drive(m, 0); }

int deadzone(int v) {
  if (abs(v) <= DEADZONE) return 0;
  return (v < 0 ? -1 : 1) * map(abs(v), DEADZONE, 127, 0, 127);
}

void setup() {
  Serial.begin(115200);
  for (auto& m : motors) {
    pinMode(m.in1, OUTPUT);
    pinMode(m.in2, OUTPUT);
    ledcSetup(m.ch, PWM_FREQ, PWM_RES);
    ledcAttachPin(m.pwm, m.ch);
  }
  stop();
  Serial.println(F("[BOOT] Hold PS + Create on the DualSense to pair."));
  ps5.begin(20);                    // scan up to 20s for first controller
}

void loop() {
  if (!ps5.isConnected()) { stop(); delay(100); return; }  // safety: no pad -> no move

  // Throttle: R2 forward / L2 back (analog). D-pad up/down fills in when idle.
  int throttle = (ps5.r2 - ps5.l2) / 2;                     // 0..255 each -> -127..127
  if (throttle == 0) throttle = 127 * (ps5.up - ps5.down);

  // Steer: left stick X. D-pad left/right fills in when idle.
  int turn = deadzone(ps5.lx);
  if (turn == 0) turn = 127 * (ps5.right - ps5.left);

  int left  = constrain(throttle + turn, -127, 127);
  int right = constrain(throttle - turn, -127, 127);
  drive(motors[0], left);
  drive(motors[1], right);

  debug(throttle, turn, left, right);
  delay(20);
}

// Plain-language status, printed only when it changes.
// Lets you verify the drive logic with no L298N connected.
void debug(int throttle, int turn, int left, int right) {
  static int pl = INT_MIN, pr = INT_MIN;
  if (left == pl && right == pr) return;
  pl = left; pr = right;

  const char* move = throttle > 0 ? "FWD " : throttle < 0 ? "BACK" : "STOP";
  const char* dir  = turn > 0 ? ">>" : turn < 0 ? "<<" : "--";
  int spd = max(abs(left), abs(right)) * 100 / 127;     // 0..100 %

  Serial.printf("%s  steer %s  %3d%%   (L%-4d R%-4d)\n", move, dir, spd, left, right);
}
