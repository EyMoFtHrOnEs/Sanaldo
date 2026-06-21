// ps5_2wd_car.ino — 2WD car driven by a PS5 DualSense over Bluetooth Classic.
//
// Board:   Deneyap Kart 1A  (plain ESP32-WROVER-E)
// Driver:  L298N  (IN1-4 + ENA/ENB)
// Library: esp-ps5  -> https://github.com/HamzaYslmn/esp-ps5
//
// Two control schemes, pick with `mode` below:
//   ANALOG : R2 = forward, L2 = back; steer with the left OR right stick (10% deadzone).
//   DIGITAL: drive with the D-pad (up/down + left/right combine, e.g. up+right = fwd-right).
// Gears (both modes): Triangle = up, Cross(X) = down. 3 gears cap top speed: 100 / 150 / 255.
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

enum Mode { MODE_ANALOG, MODE_DIGITAL };   // ANALOG/DIGITAL are core macros, don't reuse
Mode mode = MODE_DIGITAL;           // <-- pick your control scheme

const int GEARS[3] = { 100, 150, 255 };   // max PWM duty per gear (255 = full)
int gear = 0;                       // 0..2; Triangle = up, Cross = down

const int DEADZONE = 127 * 10 / 100;  // 10% stick deadzone
const int PWM_FREQ = 20000;         // 20 kHz, above audible range
const int PWM_RES  = 8;             // 0..255 duty

// one motor: signed -127..127 -> direction pins + PWM duty, capped by the current gear (x trim)
void drive(const Motor& m, int speed) {
  digitalWrite(m.in1, speed > 0);
  digitalWrite(m.in2, speed < 0);
  ledcWrite(m.ch, map(abs(speed), 0, 127, 0, GEARS[gear]) * m.trim / 100);
}

// both wheels at once (and print them)
void setWheels(int left, int right) {
  drive(motors[0], left);
  drive(motors[1], right);
  debug(left, right);
}

void stop() { setWheels(0, 0); }

// spin in place: wheels run opposite ways (steering with no throttle)
void tankTurn(int turn) { setWheels(turn, -turn); }

// steering eases the inner wheel toward 0 (it slows, never reverses)
void arcadeDrive(int throttle, int turn) {
  int inner = throttle * (127 - abs(turn)) / 127;
  if (turn > 0) setWheels(throttle, inner);   // turn right -> slow right
  else          setWheels(inner, throttle);   // turn left  -> slow left (turn 0 -> straight)
}

// full fwd/back jog. If wheels move here but not while driving -> the PS5 link, not wiring.
void selftest() {
  int g = gear; gear = 2;            // test at top gear regardless of current
  Serial.println(F("[TEST] fwd / back / stop"));
  setWheels(127, 127); delay(500);
  setWheels(-127, -127); delay(500);
  stop();
  gear = g;
}

// run selftest when "selftest" arrives over serial
void checkSerial() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  if (cmd == "selftest" || cmd == "/selftest") selftest();
}

int deadzone(int v) {
  if (abs(v) <= DEADZONE) return 0;
  int s = (v < 0 ? -1 : 1) * map(abs(v), DEADZONE, 127, 0, 127);
  return constrain(s, -127, 127);   // -128 stick reading would otherwise overshoot
}

// read throttle+turn (-127..127 each) for the active mode
void readInputs(int& throttle, int& turn) {
  if (mode == MODE_ANALOG) {
    throttle = (ps5.r2 - ps5.l2) / 2;                // R2 forward / L2 back
    turn = deadzone(ps5.lx);                         // left stick...
    if (turn == 0) turn = deadzone(ps5.rx);          // ...or right stick
  } else {                                           // DIGITAL: D-pad only
    throttle = 127 * (ps5.up - ps5.down);
    turn     = 127 * (ps5.right - ps5.left);
  }
}

// Triangle = up a gear, Cross = down (edge-triggered, both modes)
void shiftGears() {
  if (ps5.triangle.pressed && gear < 2) gear++;
  if (ps5.cross.pressed    && gear > 0) gear--;
  if (ps5.triangle.pressed || ps5.cross.pressed)
    Serial.printf("gear %d (max %d)\n", gear + 1, GEARS[gear]);
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
  Serial.printf("[BOOT] mode=%s  Type 'selftest' to jog motors. Hold PS + Create to pair.\n",
                mode == MODE_ANALOG ? "ANALOG" : "DIGITAL");
  ps5.begin(20);                    // scan up to 20s for first controller
}

void loop() {
  checkSerial();                                           // "selftest" works even with no pad
  if (!ps5.isConnected()) { stop(); delay(100); return; }  // safety: no pad -> no move

  shiftGears();
  int throttle, turn;
  readInputs(throttle, turn);

  if (throttle == 0 && turn != 0) tankTurn(turn);          // stopped -> spin in place
  else                            arcadeDrive(throttle, turn);  // moving -> slow the inner wheel

  delay(20);
}

// status print, only when the wheels change (verify logic with no L298N attached)
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
