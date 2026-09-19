#pragma once
// The L298N output stage, and nothing else. Duty arrives already decided.
#include <Arduino.h>
#include <math.h>
#include "config.h"

namespace motor {

inline void begin() {
  const int dirPins[] = {PIN_IN1, PIN_IN2, PIN_IN3, PIN_IN4};
  for (int p : dirPins) pinMode(p, OUTPUT);
  ledcAttach(PIN_ENA, PWM_FREQ_HZ, PWM_BITS);  // core 3.x LEDC is addressed by pin
  ledcAttach(PIN_ENB, PWM_FREQ_HZ, PWM_BITS);
}

// duty is -1..1, sign is direction.
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
  ledcWrite(en, (uint32_t)(fminf(fabsf(duty), 1.0f) * PWM_MAX));
}

inline void drive(float left, float right) {
  writeOne(PIN_ENA, PIN_IN1, PIN_IN2, left);
  writeOne(PIN_ENB, PIN_IN3, PIN_IN4, right);
}

}  // namespace motor
