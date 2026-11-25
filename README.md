# Pengukur Arus Motor DC — ESP32 + ACS712 + OLED

Motormu tiba-tiba panas, tapi tak ada yang mengukur arusnya? Alat kecil ini
yang mengukurnya: sensor ACS712-05B dipasang seri di kabel daya motor DC 12 V,
dan arusnya tampil langsung di OLED — lengkap dengan buzzer dan LED yang
menyala begitu arusnya melewati 3 ampere.

Video penjelasan (60 detik):
https://www.youtube.com/shorts/P-CyGl_A2oQ

## Komponen

| Bagian | Keterangan |
|---|---|
| ESP32 DevKit | Otak alat |
| Sensor ACS712-05B | Sensor arus Hall, range ±5 A, 185 mV/A |
| Resistor 10k (2 buah) | Pembagi tegangan, membagi output sensor jadi setengah |
| OLED SSD1306 128x64 I2C (0x3C) | Tampil arus, bilah arus, dan status alarm |
| Buzzer aktif HIGH | Bunyi bila arus melebihi 3 A |
| LED merah + resistor 220 Ω | Menyala bersama buzzer saat alarm |
| Motor DC 12 V | Beban yang diukur arusnya |
| Adaptor 12 V 2 A | Catu motor, terpisah dari ESP32 |

## Wiring

| Dari | Ke |
|---|---|
| ACS712 IP+ | Adaptor 12 V+ |
| ACS712 IP- | Motor DC + (sensor seri di jalur daya motor) |
| Motor DC - | Adaptor GND |
| ACS712 OUT | Pembagi tegangan 2x 10k, lalu ke ESP32 GPIO34 (ADC1) |
| ACS712 VCC | ESP32 VIN 5 V |
| ACS712 GND | GND |
| OLED SDA | ESP32 GPIO21 |
| OLED SCL | ESP32 GPIO22 |
| OLED VCC | ESP32 3V3 |
| OLED GND | GND |
| Buzzer + | ESP32 GPIO25 |
| Buzzer - | GND |
| LED anoda | ESP32 GPIO26 (katoda lewat resistor 220 Ω ke GND) |
| Semua GND | Digabung |

Skema lengkap: [schematic.png](schematic.png)

## Cara kerja

ACS712 adalah sensor arus efek Hall: arus yang mengalir lewat terminal
IP+ ke IP- menimbulkan tegangan di pin OUT. Menurut datasheet Allegro,
ACS712-05B mengukur ±5 A dengan sensitivitas 185 mV/A, dan outputnya 2,5 V
saat arus nol (setengah dari supply 5 V).

Karena output sensor bisa mencapai 3,425 V pada arus +5 A — di atas batas
3,3 V ADC ESP32 — output dilewatkan dulu lewat pembagi tegangan dua
resistor 10k, yang membagi tegangan jadi setengah. ESP32 membaca tegangan
itu lewat ADC 12-bit (0–4095), lalu menghitung arus dengan rumus:

```
arus (A) = (tegangan ADC x 2 - 2,5) / 0,185
```

Angka ADC yang diharapkan (tegangan referensi 3,3 V):

| Arus motor | Output ACS712 | Setelah pembagi | Nilai ADC |
|---|---|---|---|
| 0 A | 2,500 V | 1,250 V | 1551 |
| +5 A | 3,425 V | 1,713 V | 2125 |
| -5 A | 1,575 V | 0,788 V | 977 |

Saat dinyalakan, sketch mengkalibrasi arus nol sendiri: motor harus dalam
keadaan mati, lalu 200 bacaan ADC pertama dirata-rata sebagai offset arus
nol, sehingga selisih kecil tegangan supply ikut terkoreksi. Setelah itu
arus diukur tiap 1 detik (rata-rata 20 bacaan), ditampilkan di OLED bersama
bilah arus dan arus tertinggi, dan dikirim juga ke Serial Monitor.

## Alarm beban berlebih

Satu mekanisme alarm dipakai di alat ini: buzzer dan LED sebagai penanda.
Begitu arus melewati ambang 3 A, buzzer (GPIO25) berbunyi dan LED merah
(GPIO26) menyala, dan OLED menampilkan "ALARM! > 3 A". Sketch tidak
memutus motor — matikan motor secara manual begitu alarm berbunyi.

## Kalibrasi

Ubah `AMBANG_ALARM_A` di sketch kalau motormu normalnya menarik arus
besar (mis. 4 A). Pastikan ambangnya tetap di bawah 5 A, batas ukur
sensor. Kalau angka arus nol meleset jauh dari 0,00 A padahal motor mati,
periksa tegangan 5 V ke sensor dan ulangi kalibrasi (nyalakan ulang alat
dengan motor mati).

## Konfigurasi di sketch

| Konstanta | Nilai | Keterangan |
|---|---|---|
| `PIN_SENSOR_ARUS` | GPIO34 | OUT ACS712 via pembagi tegangan |
| `PIN_BUZZER` | GPIO25 | Buzzer aktif HIGH |
| `PIN_LED_ALARM` | GPIO26 | LED merah alarm |
| `PIN_SDA_OLED` / `PIN_SCL_OLED` | GPIO21 / GPIO22 | Bus I2C OLED |
| `ALAMAT_OLED` | 0x3C | Alamat I2C SSD1306 |
| `SENSITIVITAS_V_PER_A` | 0,185 | 185 mV/A (ACS712-05B) |
| `TEGANGAN_NOL_V` | 2,5 | Output sensor saat arus 0 A |
| `FAKTOR_PEMBAGI` | 2,0 | Pembagi tegangan 2x 10k |
| `AMBANG_ALARM_A` | 3,0 | Alarm bila arus melebihi 3 A |
| `PERIODE_UKUR_MS` | 1000 | Ukur dan tampil tiap 1 detik |

## Angka compile (terverifikasi)

- Board: ESP32 DevKit (esp32:esp32:esp32doit-devkit-v1), core 4.0.0
- Arduino CLI 1.6.0, 25 Nov 2026
- Sketch: 336.053 byte (25%) dari program storage
- RAM: 24.888 byte (7%) dari dynamic memory
- Library: Adafruit_SSD1306 2.5.17, Adafruit_GFX_Library 1.13.0,
  Adafruit_BusIO (ada di folder `libraries/`)

## Peringatan

Motor DC 12 V dicatu dari adaptor sendiri. JANGAN catu motor dari pin 5V
ESP32 — arusnya bisa merusak board. ACS712-05B hanya mengukur sampai ±5 A;
motor yang menarik arus lebih besar butuh varian ACS712-20A atau ACS712-30A
(dengan sensitivitas berbeda: 100 mV/A dan 66 mV/A).

## Lisensi

MIT — lihat [LICENSE](LICENSE).
