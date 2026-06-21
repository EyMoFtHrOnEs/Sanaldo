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

struct Motor { uint8_t pwm, in1, in2, ch, trim; };
Motor motors[2] = {
  { 23, 22, 21, 0, 100 },   // LEFT : ENA, IN1, IN2  (trim %: drop the faster side to roll straight)
  { 19, 18, 13, 1, 100 },   // RIGHT: ENB, IN3, IN4
};

const int DEADZONE = 16;            // ignore stick drift near center (0..127)
const int PWM_FREQ = 20000;         // 20 kHz, above audible range
const int PWM_RES  = 8;             // 0..255 duty

// --- low level: one motor, signed -127..127 -> direction pins + PWM duty (scaled by per-side trim)
void drive(const Motor& m, int speed) {
  digitalWrite(m.in1, speed > 0);
  digitalWrite(m.in2, speed < 0);
  ledcWrite(m.ch, map(abs(speed), 0, 127, 0, 255) * m.trim / 100);
}

// --- low level: set both wheels at once (and print what they're doing)
void setWheels(int left, int right) {
  drive(motors[0], left);
  drive(motors[1], right);
  debug(left, right);
}

void stop() { setWheels(0, 0); }

// Spin in place: wheels run opposite ways. Used when steering with no throttle.
void tankTurn(int turn) { setWheels(turn, -turn); }

// Drive while steering: keep the outer wheel at full throttle, ease the INNER
// wheel down toward 0 as the turn sharpens (it slows, it never reverses).
void arcadeDrive(int throttle, int turn) {
  int inner = throttle * (127 - abs(turn)) / 127;   // 0..throttle
  if (turn > 0) setWheels(throttle, inner);         // turning right -> slow right wheel
  else          setWheels(inner, throttle);         // turning left  -> slow left  (turn==0 -> straight)
}

int deadzone(int v) {
  if (abs(v) <= DEADZONE) return 0;
  return (v < 0 ? -1 : 1) * map(abs(v), DEADZONE, 127, 0, 127);
}

// --- read the controller into a throttle/turn pair (-127..127 each)
int readThrottle() {
  int t = (ps5.r2 - ps5.l2) / 2;                    // R2 forward / L2 back (analog)
  if (t == 0) t = 127 * (ps5.up - ps5.down);        // D-pad fills in when triggers idle
  return t;
}
int readTurn() {
  int t = deadzone(ps5.lx);                          // left stick X
  if (t == 0) t = 127 * (ps5.right - ps5.left);      // D-pad fills in when stick idle
  return t;
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

  int throttle = readThrottle();
  int turn     = readTurn();

  if (throttle == 0 && turn != 0) tankTurn(turn);          // stopped -> spin in place
  else                            arcadeDrive(throttle, turn);  // moving -> slow the inner wheel

  delay(20);
}

// Plain-language status, printed only when the wheels change.
// Lets you verify the drive logic with no L298N connected.
void debug(int left, int right) {
  static int pl = INT_MIN, pr = INT_MIN;
  if (left == pl && right == pr) return;
  pl = left; pr = right;

  const char* move = (left > 0 && right > 0) ? "FWD " :
                     (left < 0 && right < 0) ? "BACK" :
                     (left || right)         ? "TURN" : "STOP";
  int spd = max(abs(left), abs(right)) * 100 / 127;        // 0..100 %

  Serial.printf("%s  %3d%%   (L%-4d R%-4d)\n", move, spd, left, right);
}
