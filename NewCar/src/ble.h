#pragma once
// The radio half: a Nordic UART server and the shared command block the drive
// loop reads. It moves bytes and times them out; motor::fromKeys decides what
// they mean.
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "config.h"
#include "motor.h"

// Nordic UART Service. Nothing custom to document: the Python client in python/,
// nRF Connect and every generic "BLE terminal" app all speak it already.
#define NUS_SERVICE "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_RX      "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  // controller -> car
#define NUS_TX      "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  // car -> controller

namespace ble {

static portMUX_TYPE  s_mux   = portMUX_INITIALIZER_UNLOCKED;
static motor::Command s_cmd;
static uint32_t      s_stamp = 0;
static volatile bool s_connected  = false;
static volatile bool s_restartAdv = false;
static BLECharacteristic *s_tx = nullptr;

// Read by the drive task on core 1, and the only place the failsafe lives. A
// disconnect reads as neutral at once; silence is given CMD_TIMEOUT_MS first, so a
// single dropped packet is not a lurch. Neutral is a coast, not a cut: the ramp in
// motor::step still has to bring it down.
static motor::Command command() {
  motor::Command c;
  uint32_t stamp;
  portENTER_CRITICAL(&s_mux);
  c = s_cmd;
  stamp = s_stamp;
  portEXIT_CRITICAL(&s_mux);
  if (!s_connected || millis() - stamp > (uint32_t)CMD_TIMEOUT_MS) return {};
  return c;
}

// Commanded state, not measured: there are no encoders, so this is what the
// controller believes - still the number you want while tuning the ramps.
static void telemetry(const motor::State &s, float dutyLeft, float dutyRight) {
  static uint32_t last = 0;
  uint32_t now = millis();
  if (!s_connected || s_tx == nullptr || now - last < 200) return;  // 5 Hz, off the hot path
  last = now;
  char buf[24];
  int n = snprintf(buf, sizeof buf, "%.2f %.2f %d %d",
                   s.v, s.w, (int)(dutyLeft * 100), (int)(dutyRight * 100));
  s_tx->setValue((uint8_t *)buf, n);
  s_tx->notify();
}

class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    portENTER_CRITICAL(&s_mux);
    motor::fromKeys((const char *)c->getData(), c->getLength(), s_cmd);
    s_stamp = millis();
    portEXIT_CRITICAL(&s_mux);
  }
};

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *) override { s_connected = true; }
  void onDisconnect(BLEServer *) override {
    s_connected = false;
    portENTER_CRITICAL(&s_mux);
    s_cmd.throttle = 0.0f;
    s_cmd.steer = 0.0f;
    portEXIT_CRITICAL(&s_mux);
    s_restartAdv = true;  // re-advertising inside the callback is flaky; the task does it
  }
};

// Core 0. The Bluedroid host task is already pinned here, so init and the
// advertising housekeeping sit beside it and never share a core with the control
// loop. Writes arrive on the host task and land in motor::fromKeys.
static void task(void *) {
  BLEDevice::init("NewCar");
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  BLEService *svc = server->createService(NUS_SERVICE);
  BLECharacteristic *rx = svc->createCharacteristic(
      NUS_RX, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  rx->setCallbacks(new RxCallbacks());

  s_tx = svc->createCharacteristic(NUS_TX, BLECharacteristic::PROPERTY_NOTIFY);
  s_tx->addDescriptor(new BLE2902());

  svc->start();
  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(NUS_SERVICE);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.println("BLE up: NewCar");

  for (;;) {
    if (s_restartAdv) {
      s_restartAdv = false;
      vTaskDelay(pdMS_TO_TICKS(200));
      BLEDevice::startAdvertising();
      Serial.println("re-advertising");
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

}  // namespace ble
