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

void setup() {
  Serial.begin(115200);
  delay(500);
  bool ok = SerialBT.begin("SanaldoBT");
  Serial.printf("[BOOT] SerialBT.begin = %s\n", ok ? "OK" : "FAIL");
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_BT);
  Serial.printf("[BOOT] BT MAC = %02X:%02X:%02X:%02X:%02X:%02X\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.println(F("[BOOT] Pair phone to 'SanaldoBT', then send text from a BT terminal app."));
}

void loop() {
  while (SerialBT.available()) {                  // phone -> ESP32 -> USB + back to phone
    char c = SerialBT.read();
    Serial.printf("[BT] %c (0x%02X)\n", c, c);
    SerialBT.write(c);                            // echo back to the phone
  }
  while (Serial.available()) SerialBT.write(Serial.read());   // USB -> phone
}
