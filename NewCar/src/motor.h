#pragma once
// Everything between a keypress and the L298N: what a packet means, the motion
// model, the differential kinematics and the output stage. One concern - nothing
// else in the firmware has an opinion about wheels.
//
// The model half pulls in no Arduino headers and the hardware half sits behind
// #ifdef ARDUINO, so python/test/ links this exact file and drives the real
// firmware maths instead of a re-implementation that would drift.
#include <math.h>
#include <stddef.h>
#include "config.h"
#ifdef ARDUINO
#include <Arduino.h>
#endif

namespace motor {

// v forward positive, w yaw positive = turning LEFT (CCW).
struct State { float v = 0.0f; float w = 0.0f; };

struct Command {
  float throttle = 0.0f;  // -1..1
  float steer    = 0.0f;  // -1..1, positive = right
};

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

inline float slew(float cur, float target, float maxDelta) {
  float d = target - cur;
  if (d >  maxDelta) d =  maxDelta;
  if (d < -maxDelta) d = -maxDelta;
  return cur + d;
}

// The controller sends the keys currently held, e.g. "wa"; "x" or an empty packet
// is neutral. This lives here rather than in ble.h because the radio only moves
// bytes - deciding what they mean is a motion concern, and keeping it Arduino-free
// is what lets the simulator speak the same protocol as the car.
inline void fromKeys(const char *keys, size_t n, Command &c) {
  float th = 0.0f, st = 0.0f;
  for (size_t i = 0; i < n; i++) {
    char k = keys[i] | 0x20;  // ASCII lower-case
    if      (k == 'w') th += 1.0f;
    else if (k == 's') th -= 1.0f;
    else if (k == 'a') st -= 1.0f;
    else if (k == 'd') st += 1.0f;
    else if (k == 'x') { th = 0.0f; st = 0.0f; }
  }
  c.throttle = clampf(th, -1.0f, 1.0f);
  c.steer    = clampf(st, -1.0f, 1.0f);
}

// How much yaw this speed may have. The Ackermann part is w = v / R: a steered
// axle cannot yaw freely, its rate is tied to forward speed by the turn radius, and
// holding R >= MIN_TURN_RADIUS_M makes a turn an arc instead of a scrub. Grip caps
// the other end, since lateral acceleration is v * w. Standing still there is no arc
// to follow, so a pivot is allowed, faded out by PIVOT_FADE_MPS so the allowance
// does not fall off a cliff as the car pulls away.
inline float maxYaw(float v) {
  float speed = fabsf(v);
  float pivot = PIVOT_YAW_RATE * clampf(1.0f - speed / PIVOT_FADE_MPS, 0.0f, 1.0f);
  if (speed < 1e-3f) return pivot;
  float arc = fminf(speed / MIN_TURN_RADIUS_M, MAX_LATERAL_MPS2 / speed);
  return fmaxf(arc, pivot);
}

// One control tick. throttle/steer are -1..1, steer positive = right. Steer scales
// the yaw actually available at this speed, so half stick is half a turn whether
// crawling or flat out - a fixed rate that got clipped afterwards would saturate
// long before the stick did.
inline State step(State s, float throttle, float steer, float dt) {
  float vTarget = clampf(throttle, -1.0f, 1.0f) * V_MAX_MPS;
  float wTarget = -clampf(steer, -1.0f, 1.0f) * maxYaw(s.v);  // right turn is CW, so negative

  // Slowing down, including a commanded reversal, gets the brake rate not the accel rate.
  bool braking = s.v * (vTarget - s.v) < 0.0f;
  s.v = slew(s.v, vTarget, (braking ? DECEL_MPS2 : ACCEL_MPS2) * dt);
  s.w = slew(s.w, wTarget, YAW_ACCEL_RPS2 * dt);
  return s;
}

// Brushed motors do nothing below their stiction duty, which leaves the bottom of
// the range dead. Squash -1..1 into [MOTOR_MIN_DUTY, 1] so small commands move.
inline float deadband(float u) {
  if (fabsf(u) < 0.01f) return 0.0f;
  float mag = MOTOR_MIN_DUTY + (1.0f - MOTOR_MIN_DUTY) * clampf(fabsf(u), 0.0f, 1.0f);
  return u < 0.0f ? -mag : mag;
}

// Command in, PWM duties out. The simulator calls exactly this, so what it draws is
// what the L298N would be handed.
//
// The middle is differential kinematics: v +- w*track/2. When a wheel saturates both
// scale by the SAME factor, which costs speed but preserves the ratio, so the car
// holds the radius it was asked for instead of straightening out under clipping.
inline void tick(State &s, const Command &c, float dt, float &dutyLeft, float &dutyRight) {
  s = step(s, c.throttle, c.steer, dt);
  float l = s.v - s.w * TRACK_M * 0.5f;
  float r = s.v + s.w * TRACK_M * 0.5f;
  float peak = fmaxf(fabsf(l), fabsf(r));
  if (peak > V_MAX_MPS) {
    l *= V_MAX_MPS / peak;
    r *= V_MAX_MPS / peak;
  }
  dutyLeft  = deadband(l / V_MAX_MPS) * TRIM_LEFT;
  dutyRight = deadband(r / V_MAX_MPS) * TRIM_RIGHT;
}

#ifdef ARDUINO

inline void begin() {
  const int dirPins[] = {PIN_IN1, PIN_IN2, PIN_IN3, PIN_IN4};
  for (int p : dirPins) pinMode(p, OUTPUT);
  ledcAttach(PIN_ENA, PWM_FREQ_HZ, PWM_BITS);  // core 3.x LEDC is addressed by pin
  ledcAttach(PIN_ENB, PWM_FREQ_HZ, PWM_BITS);
}

inline void writeOne(int en, int inA, int inB, float duty) {
  if (duty == 0.0f) {  // both inputs low = coast, rather than a direction held at zero duty
    digitalWrite(inA, LOW);
    digitalWrite(inB, LOW);
    ledcWrite(en, 0);
    return;
  }
  bool forward = duty > 0.0f;
  digitalWrite(inA, forward ? HIGH : LOW);
  digitalWrite(inB, forward ? LOW : HIGH);
  ledcWrite(en, (uint32_t)(clampf(fabsf(duty), 0.0f, 1.0f) * PWM_MAX));
}

inline void drive(float dutyLeft, float dutyRight) {
  writeOne(PIN_ENA, PIN_IN1, PIN_IN2, dutyLeft);
  writeOne(PIN_ENB, PIN_IN3, PIN_IN4, dutyRight);
}

#endif  // ARDUINO

}  // namespace motor
