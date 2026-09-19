#pragma once
// Every knob lives here. No Arduino headers: test/ compiles this with plain g++.

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

// ---- chassis geometry ----
constexpr float TRACK_M          = 0.15f;    // measured: wheel centre to wheel centre
constexpr float WHEEL_DIAMETER_M = 0.055f;   // measured
constexpr float MOTOR_RPM        = 500.0f;   // the motor's rated no-load speed

// Top speed is geometry, not a guess: one revolution carries the car pi*d, and the
// motor manages MOTOR_RPM of them a minute - minus whatever the real world takes.
// A loaded motor never reaches its no-load rating and the L298N drops ~2 V getting
// there, so LOAD_FACTOR is the one number here you have to measure: time the car
// over 2 m at full throttle and set it to (measured m/s) / 1.44.
constexpr float LOAD_FACTOR = 0.75f;  // CALIBRATE ME
constexpr float V_MAX_MPS = WHEEL_DIAMETER_M * 3.14159265f * MOTOR_RPM / 60.0f * LOAD_FACTOR;

// ---- CALIBRATE THESE TOO ----
constexpr float MOTOR_MIN_DUTY = 0.25f;  // lowest duty where the wheels actually turn
constexpr float TRIM_LEFT = 1.00f, TRIM_RIGHT = 1.00f;  // even out a chassis that veers

// ---- how fast it may gain and lose speed ----
constexpr float ACCEL_MPS2 = 1.20f;  // ~0.9 s from rest to V_MAX
constexpr float DECEL_MPS2 = 2.40f;  // braking may be brisker than launching

// ---- how it is allowed to turn ----
// Two limits shape every corner: below ~0.73 m/s the radius floor binds, so slow
// corners come out tight, and above it lateral grip binds, so fast ones open up.
// Full stick always asks for whichever of the two currently applies, which is what
// makes the steering feel the same at both ends of the speed range.
constexpr float MIN_TURN_RADIUS_M = 0.18f;  // ~1.2x track. Tighter than this scrubs.
constexpr float MAX_LATERAL_MPS2  = 3.00f;  // sideways grip before it slides or tips
constexpr float YAW_ACCEL_RPS2    = 9.00f;  // ~0.3 s to full lock: quick, still not a step
constexpr float PIVOT_YAW_RATE    = 3.00f;  // 170 deg/s on the spot
constexpr float PIVOT_FADE_MPS    = 0.30f;  // pivot help fades out by this speed

// ---- loop / link ----
constexpr int CONTROL_HZ     = 100;
constexpr int CMD_TIMEOUT_MS = 400;  // silence longer than this -> coast to a stop
