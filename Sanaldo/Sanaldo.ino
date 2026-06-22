// Sanaldo.ino — 2WD car driven by a PS5 DualSense over Bluetooth Classic.
//
// Board:   Deneyap Kart 1A (ESP32-WROVER-E) — Arduino core 2.0.x (ledcSetup API)
// Driver:  L298N  (IN1-4 + ENA/ENB)
// Library: esp-ps5 >= 1.3.3  -> https://github.com/HamzaYslmn/esp-ps5
//
// Drive with EITHER scheme, both live at once:
//   analog  : R2 = forward, L2 = back; steer with the left or right stick.
//   digital : D-pad up/down + left/right (combine, e.g. up+right = forward-right).
// Steering eases the inner wheel; with no throttle, left/right spin in place.
// Gears: Triangle = up, Cross(X) = down. 3 gears cap top speed: 100 / 150 / 255.
// Lightbar smoothly cycles through the rainbow while connected.
//
// WIRING (Deneyap 1A silk -> L298N). Share GND with the L298N; motor power from
// a SEPARATE battery, never the board's 5V/3V3.
//   D0 (GPIO23) -> ENA   D1 (GPIO22) -> IN1   D4 (GPIO21) -> IN2   (LEFT)
//   D5 (GPIO19) -> ENB   D6 (GPIO18) -> IN3   D12(GPIO13) -> IN4   (RIGHT)
//
// Tools -> Partition Scheme -> Huge APP (3MB No OTA)   <-- REQUIRED for BT stack.

#include <ps5Controller.h>

struct Motor { uint8_t pwm, in1, in2, ch, trim; };
Motor motors[2] = {
  { 23, 22, 21, 0, 100 },   // LEFT : ENA, IN1, IN2  (trim %: drop the faster side to roll straight)
  { 19, 18, 13, 1, 100 },   // RIGHT: ENB, IN3, IN4
};

const int GEARS[3] = { 100, 150, 255 };   // max PWM duty per gear (255 = full)
int gear           = 0;                   // 0..2; Triangle = up, Cross = down
const int PWM_FREQ = 20000;               // 20 kHz, above audible range
const int PWM_RES  = 8;                   // 0..255 duty
const int DEADZONE = 127 * 10 / 100;      // 10% stick deadzone

// ---- MAC ADDRESS -----------------------------------------------------------------
const char* PS5_MAC = "";   // e.g. "AA:BB:CC:DD:EE:FF"

// ---- motors -----------------------------------------------------------------

// one motor: signed -127..127 -> direction pins + PWM duty, capped by gear (x trim)
void drive(const Motor& m, int speed) {
  digitalWrite(m.in1, speed > 0);
  digitalWrite(m.in2, speed < 0);
  ledcWrite(m.ch, map(abs(speed), 0, 127, 0, GEARS[gear]) * m.trim / 100);
}

void setWheels(int left, int right) {
  drive(motors[0], left);
  drive(motors[1], right);
}

void stop() { setWheels(0, 0); }

// throttle/turn (-127..127) -> wheels. No throttle + steering = spin in place;
// otherwise ease the inner wheel toward 0 (it slows, never reverses).
void mix(int throttle, int turn, int& left, int& right) {
  if (throttle == 0 && turn != 0) { left = turn; right = -turn; return; }
  int inner = throttle * (127 - abs(turn)) / 127;
  if (turn > 0) { left = throttle; right = inner; }   // turn right -> slow right
  else          { left = inner; right = throttle; }   // turn left / straight
}

// ---- input ------------------------------------------------------------------

// stick value -> -127..127 with a 10% center deadzone
int deadzone(int v) {
  if (abs(v) <= DEADZONE) return 0;
  int s = (v < 0 ? -1 : 1) * map(abs(v), DEADZONE, 127, 0, 127);
  return constrain(s, -127, 127);         // an int8 stick can read -128
}

// throttle/turn (-127..127). Analog (triggers/sticks) wins; D-pad fills in when idle.
void readInputs(int& throttle, int& turn) {
  throttle = (ps5.r2 - ps5.l2) / 2;                                   // R2 fwd / L2 back
  if (throttle == 0) throttle = 127 * ((ps5.up ? 1 : 0) - (ps5.down ? 1 : 0));

  turn = deadzone(ps5.lx);                                            // left stick...
  if (turn == 0) turn = deadzone(ps5.rx);                             // ...or right stick...
  if (turn == 0) turn = 127 * ((ps5.right ? 1 : 0) - (ps5.left ? 1 : 0));  // ...or D-pad
}

// Triangle = up, Cross = down. Ignore when both/neither edge fired.
void shiftGears(bool up, bool down) {
  if (up == down) return;
  if (up   && gear < 2) gear++;
  if (down && gear > 0) gear--;
  Serial.printf("[GEAR] %d (max %d)\n", gear + 1, GEARS[gear]);
}

// ---- lightbar ---------------------------------------------------------------

// hue 0..255 -> RGB rainbow (Adafruit-style color wheel)
void wheel(uint8_t pos, uint8_t& r, uint8_t& g, uint8_t& b) {
  pos = 255 - pos;
  if (pos < 85)       { r = 255 - pos * 3; g = 0;            b = pos * 3; }
  else if (pos < 170) { pos -= 85;  r = 0;            g = pos * 3;       b = 255 - pos * 3; }
  else                { pos -= 170; r = pos * 3;      g = 255 - pos * 3; b = 0; }
}

// smooth color transition; one step per call. Also keeps the controller streaming.
void colorCycle() {
  static uint8_t hue = 0;
  uint8_t r, g, b;
  wheel(hue++, r, g, b);
  ps5.lightbar(r, g, b).send();
}

// ---- main -------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  for (auto& m : motors) {
    pinMode(m.in1, OUTPUT);
    pinMode(m.in2, OUTPUT);
    ledcSetup(m.ch, PWM_FREQ, PWM_RES);
    ledcAttachPin(m.pwm, m.ch);
  }
  stop();
  Serial.println(F("[BOOT] D-pad drives, Triangle/Cross shift gears. Hold PS + Create to pair."));
  if (PS5_MAC[0]) ps5.begin(PS5_MAC);   // static MAC: fast-connect, no scan
  else            ps5.begin(20);        // empty: scan + pair
}

void loop() {
  if (!ps5.isConnected()) { stop(); delay(100); return; }   // no pad -> no move

  shiftGears(ps5.triangle.pressed, ps5.cross.pressed);

  int throttle, turn, left, right;
  readInputs(throttle, turn);
  mix(throttle, turn, left, right);
  setWheels(left, right);

  static uint32_t t = 0;                                    // ~25 fps color transition
  if (millis() - t >= 40) { t = millis(); colorCycle(); }

  static uint32_t dbg = 0;                                  // once/sec input dump (no car needed)
  if (millis() - dbg >= 1000) {
    dbg = millis();
    Serial.printf("[IN] thr=%4d turn=%4d L=%4d R=%4d | R2=%3d L2=%3d lx=%4d rx=%4d "
                  "dpad[U%d D%d L%d R%d] gear=%d\n",
                  throttle, turn, left, right, ps5.r2, ps5.l2, ps5.lx, ps5.rx,
                  ps5.up, ps5.down, ps5.left, ps5.right, gear + 1);
  }

  delay(20);
}
