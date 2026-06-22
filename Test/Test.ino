// Test.ino — DONANIM TEST KODU (Sanaldo araba kartı için)
//
// Kart:   Deneyap Kart 1A (ESP32-WROVER-E) — Arduino core 2.0.x
// Sürücü: L298N (IN1-4 + ENA/ENB)
// Kütüphane: esp-ps5 >= 1.3.3 -> https://github.com/HamzaYslmn/esp-ps5
//
// Tools -> Partition Scheme -> Huge APP (3MB No OTA)   <-- BT yığını için ZORUNLU.
//
// ============================ KULLANIM KILAVUZU ==============================
// Kodu kart'a yükle, Serial Monitor'u 115200 baud ile aç. Açılışta kart üzeri
// RGB LED yanıp söner -> kodun yüklendiğini ve çalıştığını gösterir.
// Sonra menüden tek harf yazıp Enter'a bas:
//
//   m  ->  MOTOR TESTİ : iki motor ileri-geri gidip gelir (L298N kontrolü).
//   b  ->  BLUETOOTH TESTİ : PS5 koluna bağlanır, kolun MAC adresini yazar.
//          X (çarpı) tuşuna basılı tutulunca "X pressed" yazmaya devam eder,
//          bırakınca durur, tekrar basınca yine yazar.
//   r  ->  RGB TESTİ : kart üzeri RGB LED renk döngüsü (çalışıyor göstergesi).
//
// Başka bir teste geçmek için ilgili harfi yaz. Reset = kart üzeri RST tuşu.
// ============================================================================

#include <ps5Controller.h>

// ---- motor pinleri (L298N) --------------------------------------------------
// trim %: hızlı tarafı kısıp aracı düz gitmesi için (orijinal koddan).
struct Motor { uint8_t pwm, in1, in2, ch, trim; };
Motor motors[2] = {
  { 23, 22, 21, 0, 100 },   // SOL  : ENA, IN1, IN2
  { 19, 18, 13, 1, 100 },   // SAĞ  : ENB, IN3, IN4
};

const int PWM_FREQ = 20000;   // 20 kHz, duyulabilir aralığın üstünde
const int PWM_RES  = 8;       // 0..255 duty
const int TEST_PWM = 150;     // test hızı (motorlar ucu ucuna dönsün, tam gaz değil)

const uint8_t RGB_PARLAKLIK = 10;   // RGB LED parlaklığı (maksimum 255; 10 = kısık)

char mod = 'r';       // aktif test modu: 'm' motor, 'b' bluetooth, 'r' rgb
bool btBagli = false; // bluetooth testinde bağlantı bir kez kuruldu mu

// ---- motor sürme ------------------------------------------------------------

// tek motor: işaretli -255..255 -> yön pinleri + PWM duty (trim ile düzeltilir)
void drive(const Motor& m, int speed) {
  digitalWrite(m.in1, speed > 0);   // ileri
  digitalWrite(m.in2, speed < 0);   // geri
  ledcWrite(m.ch, constrain(abs(speed), 0, 255) * m.trim / 100);
}

void setWheels(int sol, int sag) { drive(motors[0], sol); drive(motors[1], sag); }
void dur() { setWheels(0, 0); }

// ---- TESTLER ----------------------------------------------------------------

// MOTOR TESTİ: iki motor ileri git, dur, geri git, dur (L298N'i doğrular).
void motortest() {
  Serial.println("[MOTOR] ileri");
  setWheels(TEST_PWM, TEST_PWM);
  delay(1500);

  Serial.println("[MOTOR] dur");
  dur();
  delay(500);

  Serial.println("[MOTOR] geri");
  setWheels(-TEST_PWM, -TEST_PWM);
  delay(1500);

  Serial.println("[MOTOR] dur");
  dur();
  delay(500);
}

// BLUETOOTH TESTİ: PS5 koluna bağlan, MAC adresini yaz, X tuşunu izle.
// Tarama sırasında bulunan ilk PS5 kolunun MAC'i buraya yazılır.
char bulunanMac[18] = {0};

// tarama geri-çağrısı: bulunan her cihazın MAC'ini yazdırır, ilkini saklar
void onTarama(const uint8_t mac[6], const char* isim, int8_t rssi) {
  char s[18];
  snprintf(s, sizeof(s), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.printf("[BT] bulundu: %s  rssi=%d  isim=\"%s\"\n", s, rssi, isim);
  if (bulunanMac[0] == 0) strcpy(bulunanMac, s);   // ilk bulunanı bağlanmak için sakla
}

void bluetoothtest() {
  if (!btBagli) {                                  // bağlantıyı bir kez kur
    Serial.println("[BT] taraniyor... (kolda PS + Create tuslarini basili tut)");
    bulunanMac[0] = 0;
    ps5.scanDevices(5, onTarama);                  // 5 sn tara, MAC'leri yazdır

    if (bulunanMac[0] == 0) {
      Serial.println("[BT] kol bulunamadi, tekrar deneniyor...");
      delay(500);
      return;
    }
    Serial.printf("[BT] baglaniliyor -> %s\n", bulunanMac);
    ps5.begin(bulunanMac);
    while (!ps5.isConnected()) delay(10);
    Serial.printf("[BT] BAGLANDI. Kol MAC: %s\n", bulunanMac);
    Serial.println("[BT] X (carpi) tusuna bas. Birakana kadar yazacak.");
    btBagli = true;
  }

  if (!ps5.isConnected()) { Serial.println("[BT] baglanti koptu"); btBagli = false; return; }

  if (ps5.cross) Serial.println("X pressed");       // basili oldukca yazmaya devam et
  delay(20);                                         // ps5 akisini ayakta tutar
}

// RGB TESTİ: kart üzeri RGB LED'i renk döngüsünde çevirir (kod çalışıyor göstergesi).
// Parlaklık RGB_PARLAKLIK ile kısık tutulur. neopixelWrite çekirdek fonksiyonudur.
void rgbtest() {
  static uint8_t renk = 0;
  uint8_t b = RGB_PARLAKLIK;
  switch (renk % 3) {                               // kırmızı -> yeşil -> mavi
    case 0: neopixelWrite(RGB_BUILTIN, b, 0, 0); break;
    case 1: neopixelWrite(RGB_BUILTIN, 0, b, 0); break;
    case 2: neopixelWrite(RGB_BUILTIN, 0, 0, b); break;
  }
  renk++;
  delay(300);
}

// ---- ana akış ---------------------------------------------------------------

void menu() {
  Serial.println("\n==== DONANIM TESTI ====");
  Serial.println("  m -> motor testi");
  Serial.println("  b -> bluetooth (PS5) testi");
  Serial.println("  r -> rgb led testi");
  Serial.printf ("  aktif mod: %c\n", mod);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  for (auto& m : motors) {                          // L298N pinleri + PWM kanalları
    pinMode(m.in1, OUTPUT);
    pinMode(m.in2, OUTPUT);
    ledcSetup(m.ch, PWM_FREQ, PWM_RES);
    ledcAttachPin(m.pwm, m.ch);
  }
  dur();

  // açılış göstergesi: RGB 3 kez yanıp sönsün -> kod yüklendi & çalışıyor
  for (int i = 0; i < 3; i++) {
    neopixelWrite(RGB_BUILTIN, RGB_PARLAKLIK, RGB_PARLAKLIK, RGB_PARLAKLIK);
    delay(150);
    neopixelWrite(RGB_BUILTIN, 0, 0, 0);
    delay(150);
  }

  menu();
}

void loop() {
  if (Serial.available()) {                         // menüden mod seç
    char c = Serial.read();
    if (c == 'm' || c == 'b' || c == 'r') {
      mod = c;
      btBagli = false;                              // mod değişince BT'yi sıfırla
      dur();
      Serial.printf("\n>>> mod: %c\n", mod);
    }
  }

  switch (mod) {
    case 'm': motortest();     break;
    case 'b': bluetoothtest(); break;
    default:  rgbtest();       break;               // 'r'
  }
}
