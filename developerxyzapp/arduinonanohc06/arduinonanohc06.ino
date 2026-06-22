// arduinonanohc06.ino — 2WD car driven by the "BT Arduino Car Controller" phone
// app (Devloper.xyz) over an HC-06 Bluetooth module. Arduino Nano.
//
// Board:  Arduino Nano (ATmega328P)
// BT:     HC-06 (Bluetooth Classic SPP) on SoftwareSerial — USB Serial stays free.
// Driver: L298N
//
// App protocol (one letter per button):
//   F=fwd  B=back  L=left  R=right   S=stop
//   I=fwd-right  G=fwd-left   J=back-right  H=back-left   (diagonals: inner wheel slowed)
//   '0'..'9' = speed slider.  Anything else -> printed as "unknown".
//
// WIRING (Nano -> HC-06):  D2 <- HC-06 TX,  D3 -> HC-06 RX (3.3V! use a divider),  5V, GND
// WIRING (Nano -> L298N):  D5->ENA D7->IN1 D8->IN2 (LEFT)   D6->ENB D4->IN3 D9->IN4 (RIGHT)

#include <SoftwareSerial.h>
SoftwareSerial BT(2, 3);            // RX (<-HC-06 TX), TX (->HC-06 RX)

#define ENA 5
#define IN1 7
#define IN2 8
#define ENB 6
#define IN3 4
#define IN4 9

int speedCar = 200;                 // 0..255, set by the app's '0'..'9' slider
const int speed_Coeff = 3;          // diagonal: inner wheel runs at speed/speed_Coeff
unsigned long lastCmd = 0;          // deadman timer (HC-06 has no link status)
const unsigned long TIMEOUT = 600;  // no command for this long -> stop
#define BT_NAME "Sanaldo BT"        // HC-06 name, set automatically at boot (see setup)

// one wheel, signed -255..255: direction pins + PWM magnitude
void wheel(int pwm, int a, int b, int s) {
  digitalWrite(a, s > 0);
  digitalWrite(b, s < 0);
  analogWrite(pwm, abs(s));
}
#define L(s) wheel(ENA, IN1, IN2, (s))
#define R(s) wheel(ENB, IN3, IN4, (s))

// send one HC-06 AT command (no CR/LF — HC-06 wants the whole token within ~1s) and echo the reply
void atWait(const char* cmd) {
  BT.print(cmd);
  unsigned long t = millis();
  while (millis() - t < 1100) while (BT.available()) Serial.write(BT.read());
  Serial.println();
}

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
  Serial.begin(9600);
  BT.begin(9600);                   // HC-06 default baud
  pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);
  pinMode(ENA, OUTPUT); pinMode(ENB, OUTPUT);
  L(0); R(0);

  // one-time rename: at power-on the HC-06 isn't paired yet, so it still takes AT commands
  Serial.println(F("[HC06] setting name to '" BT_NAME "'..."));
  atWait("AT");                  // -> OK
  atWait("AT+NAME" BT_NAME);     // -> OKsetname

  Serial.println(F("[BOOT] Pair to '" BT_NAME "' in the app. Type T to test the motors."));
}

void loop() {
  pump(Serial);                                 // USB monitor: T = selftest, or drive by hand
  pump(BT);                                      // phone app: drive commands
  if (millis() - lastCmd > TIMEOUT) { L(0); R(0); }   // safety: stale link -> stop
}
