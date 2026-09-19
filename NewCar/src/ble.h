#pragma once
// The radio. Receives two motor duties and times them out; that is the whole job.
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include "config.h"

// The write half of the Nordic UART Service, so the Python controller, nRF Connect
// and any generic "BLE terminal" app can all drive this with no custom tooling.
// Packets are two whole percentages, left then right: "-35 72". Nothing streams
// back - without encoders the board has nothing to report that the controller did
// not already tell it.
#define NUS_SERVICE "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_RX      "6e400002-b5a3-f393-e0a9-e50e24dcca9e"

namespace ble {

static portMUX_TYPE  s_mux   = portMUX_INITIALIZER_UNLOCKED;
static float         s_left  = 0.0f;
static float         s_right = 0.0f;
static uint32_t      s_stamp = 0;
static volatile bool s_connected  = false;
static volatile bool s_restartAdv = false;

// Read by the drive task on core 1, and the only place the failsafe lives. A
// disconnect reads as stopped at once; silence is given CMD_TIMEOUT_MS first, so a
// single dropped packet is not a stutter.
static void command(float &left, float &right) {
  uint32_t stamp;
  portENTER_CRITICAL(&s_mux);
  left = s_left;
  right = s_right;
  stamp = s_stamp;
  portEXIT_CRITICAL(&s_mux);
  if (!s_connected || millis() - stamp > (uint32_t)CMD_TIMEOUT_MS) left = right = 0.0f;
}

class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    char buf[16];
    size_t n = c->getLength();
    if (n == 0 || n >= sizeof buf) return;
    memcpy(buf, c->getData(), n);
    buf[n] = '\0';

    int left, right;
    if (sscanf(buf, "%d %d", &left, &right) != 2) return;  // junk: hold the last good
    portENTER_CRITICAL(&s_mux);                            // command and let it time out
    s_left = constrain(left, -100, 100) / 100.0f;
    s_right = constrain(right, -100, 100) / 100.0f;
    s_stamp = millis();
    portEXIT_CRITICAL(&s_mux);
  }
};

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *) override { s_connected = true; }
  void onDisconnect(BLEServer *) override {
    s_connected = false;
    s_restartAdv = true;  // re-advertising inside the callback is flaky; the task does it
  }
};

// Core 0. The Bluedroid host task is already pinned here, so init and the
// advertising housekeeping sit beside it and never share a core with the control
// loop. Writes arrive on the host task and land in RxCallbacks.
static void task(void *) {
  BLEDevice::init("NewCar");
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  BLEService *svc = server->createService(NUS_SERVICE);
  BLECharacteristic *rx = svc->createCharacteristic(
      NUS_RX, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  rx->setCallbacks(new RxCallbacks());
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
