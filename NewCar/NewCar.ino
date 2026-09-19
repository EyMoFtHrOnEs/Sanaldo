// BLE RC car: ESP32 + L298N, two motors, differential drive under an Ackermann
// curvature limit. Core 0 runs the radio, core 1 runs the wheels.
//
//   src/config.h  every tunable and the wiring map
//   src/motor.h   packet meaning, motion model, kinematics, L298N output
//   src/ble.h     Nordic UART server and the shared command block
//   python/       keyboard controller, plus a simulator that links src/motor.h
//
// Board: ESP32 Dev Module, arduino-esp32 3.3.10 (see sketch.yaml).
#include <Arduino.h>
#include "src/config.h"
#include "src/motor.h"
#include "src/ble.h"

#if ESP_ARDUINO_VERSION_MAJOR < 3
#error "Needs arduino-esp32 3.x: ledcAttach/ledcWrite take a pin, not a channel."
#endif

// Core 1, and nothing else on it. Fixed 100 Hz, so the slew limits in motor::step
// are real rates and not "however fast the loop got round this time".
static void driveTask(void *) {
  const TickType_t period = pdMS_TO_TICKS(1000 / CONTROL_HZ);
  const float dt = 1.0f / CONTROL_HZ;
  TickType_t wake = xTaskGetTickCount();
  motor::State state;

  for (;;) {
    float dutyLeft, dutyRight;
    motor::tick(state, ble::command(), dt, dutyLeft, dutyRight);
    motor::drive(dutyLeft, dutyRight);
    ble::telemetry(state, dutyLeft, dutyRight);
    vTaskDelayUntil(&wake, period);
  }
}

void setup() {
  Serial.begin(115200);
  motor::begin();  // pins low and PWM attached before either task can touch them
  xTaskCreatePinnedToCore(ble::task, "ble",   8192, nullptr, 1, nullptr, 0);
  xTaskCreatePinnedToCore(driveTask, "drive", 4096, nullptr, 3, nullptr, 1);
}

void loop() {
  vTaskDelete(nullptr);  // both tasks are pinned above; the Arduino loop task is dead weight
}
