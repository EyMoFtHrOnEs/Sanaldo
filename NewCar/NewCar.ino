// BLE RC car: ESP32 + L298N. Core 0 runs the radio, core 1 runs the wheels.
//
// The board does two things and no more: put the duty it was sent on the motors,
// and stop when nobody is talking to it. How the car drives - ramps, turn limits,
// cruise and turbo - is decided in python/car.py and arrives here already chewed.
//
//   src/config.h  wiring, PWM, failsafe timeout
//   src/motor.h   L298N output stage
//   src/ble.h     Nordic UART receive, "<left%> <right%>"
//
// Board: ESP32 Dev Module, arduino-esp32 3.3.10 (see sketch.yaml).
#include <Arduino.h>
#include "src/config.h"
#include "src/motor.h"
#include "src/ble.h"

#if ESP_ARDUINO_VERSION_MAJOR < 3
#error "Needs arduino-esp32 3.x: ledcAttach/ledcWrite take a pin, not a channel."
#endif

// Core 1, and nothing else on it. The refresh is steady so a packet that arrives
// mid-tick waits at most 10 ms rather than landing on the pins half-applied.
static void driveTask(void *) {
  const TickType_t period = pdMS_TO_TICKS(1000 / CONTROL_HZ);
  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    float left, right;
    ble::command(left, right);
    motor::drive(left, right);
    vTaskDelayUntil(&wake, period);
  }
}

void setup() {
  Serial.begin(115200);
  motor::begin();  // pins low and PWM attached before either task can touch them
  xTaskCreatePinnedToCore(ble::task, "ble",   8192, nullptr, 1, nullptr, 0);
  xTaskCreatePinnedToCore(driveTask, "drive", 2048, nullptr, 3, nullptr, 1);
}

void loop() {
  vTaskDelete(nullptr);  // both tasks are pinned above; the Arduino loop task is dead weight
}
