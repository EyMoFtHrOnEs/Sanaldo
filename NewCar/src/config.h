#pragma once
// Wiring, and the three numbers the board needs to act on it.
//
// Nothing about how the car *drives* is here. Ramps, turn limits, driving modes,
// what a key means - all of that lives in python/car.py, where changing it costs a
// rerun instead of a reflash. This end is an output stage with a radio.

// ---------------------------------------------------------------------------
// L298N wiring - ESP32 DevKit (classic WROOM-32)
//
//   ESP32          L298N
//   -----          -----
//   GPIO25  ----->  ENA        left motor enable  (PWM)
//   GPIO26  ----->  IN1        left  direction A
//   GPIO27  ----->  IN2        left  direction B
//   GPIO33  ----->  ENB        right motor enable (PWM)
//   GPIO32  ----->  IN3        right direction A
//   GPIO13  ----->  IN4        right direction B
//   GND     -----   GND        MUST be common with the battery ground
//
// All six avoid the strapping pins (0/2/12/15), the flash pins (6-11) and the
// input-only range (34-39), so nothing here fights the bootloader.
//
// Pull the ENA/ENB jumpers off the L298N or the PWM does nothing.
// Do not power the ESP32 from the L298N 5V regulator while driving motors; the
// brownouts look exactly like BLE bugs.
// ---------------------------------------------------------------------------
constexpr int PIN_ENA = 25, PIN_IN1 = 26, PIN_IN2 = 27;  // left
constexpr int PIN_ENB = 33, PIN_IN3 = 32, PIN_IN4 = 13;  // right

constexpr int PWM_FREQ_HZ = 10000;  // L298N likes 5-20 kHz; drop to 1000 if it stalls at low duty
constexpr int PWM_BITS    = 10;
constexpr int PWM_MAX     = (1 << PWM_BITS) - 1;

constexpr int CONTROL_HZ = 100;  // how often the duty on the pins is refreshed

// The one piece of policy that cannot live in the controller: a controller which
// has lost contact cannot stop a car. Silence this long, or a dropped connection,
// and the motors go off.
constexpr int CMD_TIMEOUT_MS = 400;
