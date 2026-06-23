// esp32-ps5.ino — 2WD car driven by a PS5 DualSense (Bluetooth) + 128x64 OLED dashboard.
//
// SETUP
//   Board   : ESP32-WROVER-32D   (Arduino core 3.3.6)
//   Driver  : L298N motor driver
//   Display : SSD1306 128x64 I2C OLED
//   Libs    : esp-ps5 >= 1.3.3, Adafruit_SSD1306, Adafruit_GFX
//   Build   : Tools -> Partition Scheme -> Huge APP (3MB No OTA)   (required for BT)
//
// CONTROLS
//   Analog  : left stick steers, R2 = forward, L2 = reverse   (lightbar rainbow)
//   D-pad   : arrows + diagonals (e.g. up+right = forward-right)
//   Gears   : Triangle = up, Cross = down   (top speed 100 / 150 / 255)
//
// WIRING
//   Left motor  : ENA 32   IN1 33   IN2 25
//   Right motor : ENB 26   IN3 27   IN4 14
//   OLED        : SDA 21   SCL 22   (I2C addr 0x3C)
//   Batteries   : CAR_BAT 34   ESP_BAT 35   (ADC1 inputs)
//
//   Pins chosen to be safe — avoid: 6-11 (flash), 16/17 (PSRAM),
//   0/2/5/12/15 (strapping), 1/3 (UART). Share GND; motors on a separate battery.

#include <ps5Controller.h>
#include <Adafruit_SSD1306.h>

// ---- pins -------------------------------------------------------------------
struct Motor { uint8_t pwm, in1, in2, trim; };   // trim %: slow the faster side to roll straight
Motor motors[2] = {
  { 32, 33, 25, 100 },   // LEFT  ENA, IN1, IN2
  { 26, 27, 14, 100 },   // RIGHT ENB, IN3, IN4
};
const uint8_t CAR_BAT = 34;   // 2S 18650, 6.4-8.4V, R1=220 R2=100 -> pin 2.00-2.63V (ADC1, input-only)
const uint8_t ESP_BAT = 35;   // 1S 18650, 3.2-4.2V, R1=100 R2=100 -> pin 1.60-2.10V (ADC1, input-only)

// ---- state ------------------------------------------------------------------
Adafruit_SSD1306 oled(128, 64, &Wire, -1);
const char* PS5_MAC = "";                        // "" = scan+pair, or "AA:BB:CC:DD:EE:FF"
int gear = 0;                                    // 0..2
uint32_t rumbleUntil = 0;                        // millis() deadline for the gear-shift buzz
enum Mode { ModeIdle, ModeAnalog, ModeDpad };   // PascalCase dodges esp32 core ALL_CAPS macros
const char* MODE_NAME[] = { "IDLE", "ANALOG", "DPAD" };

// ---- gears ------------------------------------------------------------------

int gearMax() {                                  // DRY: one source for the speed cap
  static const int GEARS[3] = { 100, 180, 255 };
  return GEARS[gear];
}

void shiftGears(bool up, bool down) {            // KISS: Triangle up / Cross down
  if (up == down) return;
  int prev = gear;
  if (up   && gear < 2) gear++;
  if (down && gear > 0) gear--;
  if (gear == prev) return;                      // already at a limit -> no change, no buzz
  rumbleUntil = millis() + 150;                  // brief buzz only on a real shift
  Serial.printf("[GEAR] %d (max %d)\n", gear + 1, gearMax());
}

// ---- motors -----------------------------------------------------------------

void drive(const Motor& m, int speed) {          // DRY: one motor, -127..127
  digitalWrite(m.in1, speed > 0);
  digitalWrite(m.in2, speed < 0);
  ledcWrite(m.pwm, map(abs(speed), 0, 127, 0, gearMax()) * m.trim / 100);
}

void setWheels(int left, int right) {            // DRY: both wheels
  drive(motors[0], left);
  drive(motors[1], right);
}

void stop() { setWheels(0, 0); }                 // KISS

int wheelPct(const Motor& m, int speed) {        // KISS: signed duty -100..100 for OLED
  int duty = map(abs(speed), 0, 127, 0, gearMax()) * m.trim / 100;
  return (speed < 0 ? -1 : 1) * (duty * 100 / 255);
}

void mix(int throttle, int turn, int& left, int& right) {  // KISS: throttle+turn -> wheels
  if (throttle == 0 && turn != 0) { left = turn; right = -turn; return; }  // spin in place
  int inner = throttle * (127 - abs(turn)) / 127;          // ease inner wheel, never reverse
  if (turn > 0) { left = throttle; right = inner; }
  else          { left = inner; right = throttle; }
}

// ---- battery ----------------------------------------------------------------

float vbat(uint8_t pin, float r1, float r2) {    // DRY: divider -> pack volts (8-sample avg)
  uint32_t mv = 0;
  for (int i = 0; i < 8; i++) mv += analogReadMilliVolts(pin);
  return (mv / 8) / 1000.0f * (r1 + r2) / r2;
}

int battPct(float v, float lo, float hi) {       // DRY: volts -> 0..100 % (linear, clamped)
  return constrain((int)((v - lo) / (hi - lo) * 100.0f), 0, 100);
}

// KISS: car 2S 18650 — its own resistors + bounds
void readCarBattery(float& v, int& pct, float r1 = 220, float r2 = 100, float vmin = 6.4, float vmax = 8.4) {
  v   = vbat(CAR_BAT, r1, r2);
  pct = battPct(v, vmin, vmax);
}

// KISS: esp 1S 18650 — its own resistors + bounds
void readEspBattery(float& v, int& pct, float r1 = 100, float r2 = 100, float vmin = 3.2, float vmax = 4.2) {
  v   = vbat(ESP_BAT, r1, r2);
  pct = battPct(v, vmin, vmax);
}

// ---- input ------------------------------------------------------------------

int deadzone(int v) {                            // KISS: stick -> -127..127, 10% center dead
  const int DEAD = 127 * 10 / 100;
  if (abs(v) <= DEAD) return 0;
  int s = (v < 0 ? -1 : 1) * map(abs(v), DEAD, 127, 0, 127);
  return constrain(s, -127, 127);
}

Mode readInputs(int& throttle, int& turn) {      // KISS: analog wins, else d-pad
  int stick = deadzone(ps5.lx);
  int trig  = (ps5.r2 - ps5.l2) / 2;             // R2 fwd / L2 rev
  if (stick != 0 || trig != 0) { throttle = trig; turn = stick; return ModeAnalog; }

  int t = (ps5.up ? 1 : 0) - (ps5.down ? 1 : 0);
  int s = (ps5.right ? 1 : 0) - (ps5.left ? 1 : 0);
  if (t != 0 || s != 0) {
    throttle = 127 * t;
    turn     = (t != 0 ? 64 : 127) * s;          // gentle arc (128x64) while moving; full pivot when only steering
    return ModeDpad;
  }

  throttle = 0; turn = 0; return ModeIdle;
}

// ---- lightbar ---------------------------------------------------------------

void wheel(uint8_t pos, uint8_t& r, uint8_t& g, uint8_t& b) {  // DRY: hue -> RGB rainbow
  pos = 255 - pos;
  if (pos < 85)       { r = 255 - pos * 3; g = 0;            b = pos * 3; }
  else if (pos < 170) { pos -= 85;  r = 0;            g = pos * 3;       b = 255 - pos * 3; }
  else                { pos -= 170; r = pos * 3;      g = 255 - pos * 3; b = 0; }
}

void colorCycle(int bright) {                    // KISS: one rainbow step at bright 0..10
  static uint8_t hue = 0;
  uint8_t r, g, b;
  wheel(hue++, r, g, b);
  ps5.lightbar(r * bright / 10, g * bright / 10, b * bright / 10).send();
}

void gearColor(int bright) {                     // KISS: solid color by gear at bright 0..10
  static const uint8_t RGB[3][3] = {
    { 0, 255,   0 },   // gear 1 = green
    { 0,   0, 255 },   // gear 2 = blue
    { 255, 0,   0 },   // gear 3 = red
  };
  ps5.lightbar(RGB[gear][0] * bright / 10, RGB[gear][1] * bright / 10, RGB[gear][2] * bright / 10).send();
}

// ---- adaptive triggers ------------------------------------------------------

void setTriggers() {                             // KISS: stage R2/L2 feel + shift buzz (sent by next .send())
  int rs   = map(ps5.r2, 0, 255, 10, 20);        // R2 stiffness 10% rest -> 20% full (linear, 15% mid)
  int ls   = map(ps5.l2, 0, 255, 10, 20);        // L2 same linear curve
  int buzz = millis() < rumbleUntil ? 180 : 0;   // brief gear-shift rumble
  ps5.r2Rigid(0, rs).l2Rigid(0, ls).rumble(0, buzz);
}

// ---- OLED -------------------------------------------------------------------

void drawOled(Mode mode, int left, int right, bool connected,    // KISS: full repaint
              float cv, int cp, float ev, int ep) {
  oled.clearDisplay();
  oled.setCursor(0, 0);
  oled.printf("PS5:%3d%%   BT:%s\n", ps5.battery, connected ? "OK" : "--");
  oled.printf("Mode: %s\n", MODE_NAME[mode]);
  oled.printf("L:%4d%%   R:%4d%%\n", wheelPct(motors[0], left), wheelPct(motors[1], right));
  oled.printf("Car:%4.1fV  %3d%%\n", cv, cp);
  oled.printf("ESP:%4.1fV  %3d%%\n", ev, ep);
  oled.display();
}

// ---- main -------------------------------------------------------------------

void setup() {
  const int PWM_FREQ = 20000, PWM_RES = 8;       // 20 kHz, 0..255 duty
  Serial.begin(115200);
  for (auto& m : motors) {
    pinMode(m.in1, OUTPUT);
    pinMode(m.in2, OUTPUT);
    ledcAttach(m.pwm, PWM_FREQ, PWM_RES);        // core 3.x: pin-based
  }
  stop();
  analogSetAttenuation(ADC_11db);                // full ADC range for dividers

  Wire.begin();                                  // SDA21 / SCL22
  if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C))
    Serial.println(F("[OLED] not found at 0x3C"));
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);

  Serial.println(F("[BOOT] hold PS+Create to pair."));
  if (PS5_MAC[0]) ps5.begin(PS5_MAC);            // known MAC: no scan
  else            ps5.begin(20);                 // scan + pair
}

void loop() {
  static bool wasConnected = false;
  bool connected = ps5.isConnected();
  if (connected != wasConnected) {               // log link edges
    wasConnected = connected;
    Serial.println(connected ? F("[LINK] connected") : F("[LINK] disconnected"));
  }

  int throttle = 0, turn = 0, left = 0, right = 0;
  Mode mode = ModeIdle;
  if (connected) {
    shiftGears(ps5.triangle.pressed, ps5.cross.pressed);
    mode = readInputs(throttle, turn);
    mix(throttle, turn, left, right);
    setWheels(left, right);

    static uint32_t t = 0;                       // ~25 fps lightbar + trigger feel
    if (millis() - t >= 40) {
      t = millis();
      setTriggers();                             // stage R2 gas / L2 brake resistance
      static Mode lastActive = ModeAnalog;       // idle keeps the last mode's color, dimmed
      if (mode != ModeIdle) lastActive = mode;
      int bright = (mode == ModeIdle) ? 1 : 10;  // full when driving, brightness 1 when idle
      if (lastActive == ModeAnalog) colorCycle(bright);   // rainbow
      else                          gearColor(bright);    // solid gear color
    }
  } else {
    stop();
  }

  static uint32_t disp = 0;                       // OLED + telemetry, 5x/sec
  if (millis() - disp >= 200) {
    disp = millis();
    float cv, ev; int cp, ep;
    readCarBattery(cv, cp);
    readEspBattery(ev, ep);
    drawOled(mode, left, right, connected, cv, cp, ev, ep);
    Serial.printf("[%-6s] thr=%4d turn=%4d L=%4d%% R=%4d%% gear=%d | R2=%3d L2=%3d lx=%4d "
                  "dpad[U%d D%d L%d R%d] | ps5-batt=%d%% bt=%s "
                  "car=%.2fV %d%% esp=%.2fV %d%%\n",
                  MODE_NAME[mode], throttle, turn,
                  wheelPct(motors[0], left), wheelPct(motors[1], right), gear + 1,
                  ps5.r2, ps5.l2, ps5.lx,
                  (bool)ps5.up, (bool)ps5.down, (bool)ps5.left, (bool)ps5.right,
                  ps5.battery, connected ? "OK" : "--", cv, cp, ev, ep);
  }

  delay(connected ? 4 : 100);                    // ~250 Hz loop = matches the pad's BT packet rate
}
