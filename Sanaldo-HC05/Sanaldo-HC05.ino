// Sanaldo-HC05.ino — 2WD car driven from a phone via an HC-05 Bluetooth module.
//
// Board:   ESP32-S2 module — Arduino core 2.0.x (ledcSetup API).
//          The S2 has NO Bluetooth radio (no BLE, no BT Classic), so the HC-05
//          (a Classic SPP module) does the wireless; the S2 just talks UART to it.
// Driver:  L298N (IN1-4 + ENA/ENB)
//
// HOW TO DRIVE: pair your phone with the HC-05 (default PIN 1234/0000), open the
// "Arduino Bluetooth RC Car" app. Its buttons send single letters (same map as
// SanaldoBT). Default 3x3 D-pad layout:
//   Q F E      forward-left / FORWARD / forward-right
//   L S R      LEFT / stop / RIGHT
//   Z G C      back-left  / BACK    / back-right
//   speed     : digits 0..9 and q set top speed (0=stop ... 9,q=full)
// No app? Any BT serial terminal works — just send the letters above.
//
// WIRING — HC-05 to ESP32-S2 (3V3 logic; share GND):
//   HC-05 VCC -> 5V    HC-05 GND -> GND
//   HC-05 TXD -> GPIO18 (S2 RX)    HC-05 RXD -> GPIO17 (S2 TX)
// WIRING — L298N (motor power from a SEPARATE battery, shared GND, never the S2):
//   GPIO4 -> ENA   GPIO5 -> IN1   GPIO6  -> IN2   (LEFT)
//   GPIO7 -> ENB   GPIO8 -> IN3   GPIO9  -> IN4   (RIGHT)
// Pins are ESP32-S2-safe (avoid USB 19/20, strapping 0/45/46, flash 26-32).
// Adjust to match your build.

// ---- HC-05 UART -------------------------------------------------------------
const int HC05_RX  = 18;     // S2 RX  <- HC-05 TXD
const int HC05_TX  = 17;     // S2 TX  -> HC-05 RXD
const int HC05_BAUD = 9600;  // HC-05 default data-mode baud
const int FAILSAFE_MS = 0;   // 0 = off (hold last command). Set e.g. 600 to stop
                             // if no byte arrives for that long (needs an app that
                             // repeats while held, not just press/release).

struct Motor { uint8_t pwm, in1, in2, ch, trim; };
Motor motors[2] = {
  { 4, 5, 6, 0, 100 },   // LEFT : ENA, IN1, IN2  (trim %: drop the faster side to roll straight)
  { 7, 8, 9, 1, 100 },   // RIGHT: ENB, IN3, IN4
};

int speedCap        = 200;                // 0..255 duty cap; digits 0..9/q set it
const int SPEED_TBL[10] = {0,28,56,85,113,141,170,198,226,255};  // digit -> duty
const int PWM_FREQ  = 20000;              // 20 kHz, above audible range
const int PWM_RES   = 8;                  // 0..255 duty

// ---- motors -----------------------------------------------------------------

// one motor: signed -127..127 -> direction pins + PWM duty, capped by speedCap (x trim)
void drive(const Motor& m, int speed) {
  digitalWrite(m.in1, speed > 0);
  digitalWrite(m.in2, speed < 0);
  ledcWrite(m.ch, map(abs(speed), 0, 127, 0, speedCap) * m.trim / 100);
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

// one command letter -> throttle/turn (-127..127). Returns false for unknown.
bool command(char c, int& throttle, int& turn) {       // same map as SanaldoBT
  throttle = 0; turn = 0;
  switch (c) {
    case 'F': throttle =  127;             break;  // forward
    case 'G': throttle = -127;             break;  // back
    case 'L': turn     = -127;             break;  // spin left
    case 'R': turn     =  127;             break;  // spin right
    case 'Q': throttle =  127; turn =  -64; break; // fwd-left
    case 'E': throttle =  127; turn =   64; break; // fwd-right
    case 'Z': throttle = -127; turn =  -64; break; // back-left
    case 'C': throttle = -127; turn =   64; break; // back-right
    case 'S': /* stop: both 0 */           break;  // stop
    case 'q': speedCap = 255;             return false;   // full speed
    default:
      if (c >= '0' && c <= '9') { speedCap = SPEED_TBL[c - '0']; return false; }
      return false;                                 // unknown -> ignore, don't move
  }
  return true;
}

// ---- main -------------------------------------------------------------------

void setup() {
  Serial.begin(115200);                              // USB debug
  Serial1.begin(HC05_BAUD, SERIAL_8N1, HC05_RX, HC05_TX);
  for (auto& m : motors) {
    pinMode(m.in1, OUTPUT);
    pinMode(m.in2, OUTPUT);
    ledcSetup(m.ch, PWM_FREQ, PWM_RES);
    ledcAttachPin(m.pwm, m.ch);
  }
  stop();
  Serial.println(F("[BOOT] HC-05 ready. Pair phone, send F/B/L/R/S to drive."));
}

void loop() {
  static uint32_t lastCmd = 0;
  int throttle = 0, turn = 0, left, right;

  while (Serial1.available()) {                       // drain everything that arrived
    char c = Serial1.read();
    if (c == '\n' || c == '\r') continue;             // ignore line endings
    if (command(c, throttle, turn)) {                 // a movement letter -> apply
      mix(throttle, turn, left, right);
      setWheels(left, right);
      lastCmd = millis();
      Serial.printf("[CMD] %c  L=%4d R=%4d cap=%d\n", c, left, right, speedCap);
    } else if ((c >= '0' && c <= '9') || c == 'q') {  // speed change (no movement)
      Serial.printf("[SPD] %c  cap=%d\n", c, speedCap);
    } else {                                          // anything else -> show it, ignore
      Serial.printf("[??]  %c (0x%02X) unknown\n", c, c);
    }
  }

  // optional failsafe: stop if the link goes quiet (see FAILSAFE_MS note above)
  if (FAILSAFE_MS && millis() - lastCmd > (uint32_t)FAILSAFE_MS) stop();

  delay(10);
}
