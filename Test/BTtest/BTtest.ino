// SanaldoBTtest.ino — minimal Bluetooth test: print whatever the phone sends.
//
// Board: Deneyap Kart 1A (ESP32-WROVER-E) / any ESP32 with BT Classic.
// No motors. Pair your phone to "SanaldoBT", open any BT serial terminal app,
// send text — it shows up in the USB Serial Monitor. Whatever you type back in
// the Serial Monitor is sent to the phone (echo both ways).
//
// Tools -> Partition Scheme -> Huge APP (3MB No OTA)   <-- REQUIRED for BT.

#include "BluetoothSerial.h"
BluetoothSerial SerialBT;

// Deneyap Kart 1A: onboard LED is an addressable RGB (NeoPixel) on GPIO13.
#ifndef RGB_BUILTIN
#define RGB_BUILTIN 13
#endif
static inline void rgb(uint8_t r, uint8_t g, uint8_t b) { neopixelWrite(RGB_BUILTIN, r, g, b); }

void setup() {
  // Heartbeat FIRST — proves code runs without trusting Serial at all.
  for (int i = 0; i < 5; i++) { rgb(0, 40, 0); delay(150);   // green flash
                                rgb(0, 0, 0);  delay(150); }

  Serial.begin(115200);
  delay(500);
  Serial.println(F("\n[BOOT] sketch running, calling SerialBT.begin..."));
  Serial.flush();
  bool ok = SerialBT.begin("SanaldoBT");
  Serial.printf("[BOOT] SerialBT.begin = %s\n", ok ? "OK" : "FAIL");
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_BT);
  Serial.printf("[BOOT] BT MAC = %02X:%02X:%02X:%02X:%02X:%02X\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.println(F("[BOOT] Pair phone to 'SanaldoBT', then send text from a BT terminal app."));
}

void loop() {
  rgb(0, ((millis() / 500) & 1) ? 20 : 0, 0);        // slow green blink = loop is alive

  while (SerialBT.available()) {                  // phone -> ESP32 -> USB + back to phone
    char c = SerialBT.read();
    Serial.printf("[BT] %c (0x%02X)\n", c, c);
    SerialBT.write(c);                            // echo back to the phone
  }
  while (Serial.available()) SerialBT.write(Serial.read());   // USB -> phone
}
