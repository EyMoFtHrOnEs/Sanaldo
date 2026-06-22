# BT Arduino Car Controller

Telefon uygulamasıyla kontrol edilen 2 motorlu (2WD) araba için Arduino kodları.

- **Uygulama:** [BT Arduino Car Controller — Devloper.xyz](https://play.google.com/store/apps/details?id=com.bt.arduino.car.control.roboattic)
- **Komut protokolü:** uygulama her butonda tek bir harf gönderir
  `F` ileri · `B` geri · `L` sol · `R` sağ · `S` dur ·
  `I/G/J/H` çaprazlar · `0`–`9` hız ayarı

Bu klasörde aynı arabanın **iki donanım sürümü** var:

| Klasör | Kart | Bağlantı | Modül gerekir mi? |
|--------|------|----------|-------------------|
| `arduinonanohc06` | Arduino Nano | **Bluetooth** (HC-06) | Evet, HC-06 |
| `deneyapminiv2`   | [Deneyap Mini v2](https://magaza.deneyapkart.org/tr/product/detail/deneyap-mini-v2-type-c) (ESP32-S2) | **Wi-Fi** | Hayır (dahili Wi-Fi) |

> ESP32-S2'de Bluetooth **yoktur**, o yüzden Deneyap kart Wi-Fi ile çalışır.
> Nano'da Wi-Fi yoktur, o yüzden Nano harici HC-06 Bluetooth modülüyle çalışır.

Her iki kodda da motorlar bir **L298N** sürücü üzerinden bağlıdır.

---

## Ortak: yükleme ve test

1. Arduino IDE'de klasörün içindeki `.ino` dosyasını aç.
2. Kartını ve portunu seç, **Yükle (Upload)**.
3. **Seri Ekran (Serial Monitor)** aç — baud: Nano için **9600**, Deneyap için **115200**.
4. Motorları test etmek için Seri Ekran'a **`T`** yaz → araba ileri/geri zıplar.
   (Burada motor dönüyor ama uygulamadan dönmüyorsa sorun bağlantıda, kabloda değil.)

**Güvenlik:** ~600 ms boyunca komut gelmezse motorlar otomatik durur (bağlantı koparsa araba kaçmaz).

---

## 1) Arduino Nano + HC-06 (`arduinonanohc06`)

**Kablolama — Nano → HC-06:**

| Nano | HC-06 |
|------|-------|
| D2   | TX |
| D3   | RX *(3.3V! gerilim bölücü kullan: seri 1k, GND'ye 2k)* |
| 5V   | VCC |
| GND  | GND |

**Kablolama — Nano → L298N:**

| Nano | L298N | Motor |
|------|-------|-------|
| D5 | ENA | Sol |
| D7 | IN1 | Sol |
| D8 | IN2 | Sol |
| D6 | ENB | Sağ |
| D4 | IN3 | Sağ |
| D9 | IN4 | Sağ |

> Nano, L298N ve pil **ortak GND** olmalı.

**Kullanım:**
1. Kodu yükle.
2. Kod her açılışta modülün adını otomatik **Sanaldo BT** yapar. Adı değiştirmek için koddaki `BT_NAME` satırını düzenle.
3. Telefonun Bluetooth ayarlarından **Sanaldo BT**'yi eşleştir (varsayılan PIN `1234`).
4. Uygulamayı aç → **Bluetooth** modunu seç → modüle bağlan.
5. Butonlarla sür.

---

## 2) Deneyap Mini v2 + Wi-Fi (`deneyapminiv2`)

ESP32-S2 kendi Wi-Fi ağını (hotspot) açar, ekstra modül gerekmez.

**Kablolama — Deneyap Mini v2 → L298N** (kartın silk etiketleri, güvenli pinler):

| Deneyap | L298N | Motor |
|---------|-------|-------|
| D2 | ENA | Sol |
| D3 | IN1 | Sol |
| D4 | IN2 | Sol |
| D5 | ENB | Sağ |
| D6 | IN3 | Sağ |
| D7 | IN4 | Sağ |

> Motor için **ayrı pil** kullan, GND ortak olsun. Bu pinler strapping/USB/UART/flash/RGB pinlerine değmez.

**Arduino IDE ayarı:** `Tools → USB CDC On Boot: Enabled` (Seri Ekran USB'den çalışsın).

**Kullanım:**
1. Kodu yükle.
2. Telefonun Wi-Fi ayarlarından **`SanaldoCar`** ağına bağlan (şifresiz).
3. Uygulamayı aç → **Wi-Fi** modunu seç → IP **`192.168.4.1`**, port **`80`**.
4. Butonlarla sür.

> Ağın adını/şifresini değiştirmek için kodun başındaki `AP_SSID` / `AP_PASS` satırlarını düzenle.
