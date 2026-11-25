/*
  Pengukur Arus Motor DC dengan ACS712 + OLED — ESP32
  ===================================================
  Cara kerja:
  Sensor ACS712-05B dipasang SERI di jalur daya motor DC 12 V.
  Arus motor yang mengalir menimbulkan tegangan di output sensor
  (efek Hall), lalu ESP32 membacanya lewat ADC dan menampilkannya
  di OLED SSD1306 128x64, lengkap dengan status aman/alarm.

  Angka datasheet ACS712-05B (Allegro, terverifikasi 25 Nov 2026):
    - Range ukur            : +/-5 A
    - Sensitivitas          : 185 mV/A
    - Output arus nol       : 2,5 V (VCC/2 pada supply 5 V)
    - Supply                : 4,5-5,5 V (pakai 5 V)
  Karena output sensor bisa sampai 3,425 V pada +5 A (2,5 + 0,925),
  sedangkan ADC ESP32 hanya tahan 0-3,3 V, output sensor dilewatkan
  dulu lewat pembagi tegangan 2x resistor 10k (bagi 2):
    - Arus nol  : 2,5 V  -> 1,25 V  -> ADC sekitar 1551
    - Arus +5 A : 3,425 V -> 1,71 V -> ADC sekitar 2125
    - Arus -5 A : 1,575 V -> 0,79 V -> ADC sekitar 977
  Rumus arus (ampere):
    arus = (teganganADC x 2 - 2,5) / 0,185

  Kalibrasi arus nol: saat dinyalakan, motor HARUS dalam keadaan mati.
  Sketch merata-rata 200 bacaan ADC pertama sebagai offset arus nol,
  jadi selisih kecil tegangan supply ikut terkoreksi otomatis.

  Alarm: bila arus melewati ambang 3 A, buzzer (GPIO25) berbunyi dan
  LED merah (GPIO26) menyala. Mekanisme alarm yang dipakai SATU saja:
  buzzer + LED sebagai penanda — sketch TIDAK memutus motor.

  PERINGATAN: motor DC 12 V dicatu dari adaptor sendiri. JANGAN catu
  motor dari pin 5V ESP32 — arusnya bisa merusak board.

  Video penjelasan (60 detik): link menyusul setelah upload
  Repo: https://github.com/oerban70-cloud/pengukur-arus-motor-dc-acs712
*/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------- Konfigurasi pin ----------
const uint8_t PIN_SENSOR_ARUS   = 34;  // OUT ACS712 (via pembagi tegangan) -> GPIO34 (ADC1)
const uint8_t PIN_BUZZER        = 25;  // buzzer aktif HIGH
const uint8_t PIN_LED_ALARM     = 26;  // LED merah alarm
const uint8_t PIN_SDA_OLED      = 21;  // SDA bus I2C
const uint8_t PIN_SCL_OLED      = 22;  // SCL bus I2C

// ---------- Konfigurasi OLED ----------
const uint8_t ALAMAT_OLED       = 0x3C;  // alamat I2C SSD1306
const uint8_t LEBAR_OLED        = 128;   // lebar layar (piksel)
const uint8_t TINGGI_OLED       = 64;    // tinggi layar (piksel)
const int8_t  RESET_OLED        = -1;    // pin reset tidak dipakai

// ---------- Konfigurasi ACS712 + ADC ----------
// ACS712-05B: sensitivitas 185 mV/A, output arus nol = VCC/2 = 2,5 V.
const float SENSITIVITAS_V_PER_A = 0.185;  // 185 mV/A
const float TEGANGAN_NOL_V        = 2.5;    // output sensor saat arus 0 A
// Pembagi tegangan 2x resistor 10k: faktor 2 (tegangan sensor = 2x tegangan ADC).
const float FAKTOR_PEMBAGI        = 2.0;
// ADC ESP32 12-bit: 0-4095 pada tegangan referensi 3,3 V.
const float TEGANGAN_ADC_MAX_V    = 3.3;
const uint16_t NILAI_ADC_MAX      = 4095;

// ---------- Konfigurasi alarm dan tampilan ----------
const float AMBANG_ALARM_A        = 3.0;    // alarm bila arus melebihi 3 A
const uint8_t JUMLAH_SAMPEL       = 20;     // rata-rata 20 bacaan per ukur
const uint16_t JUMLAH_SAMPEL_KALIBRASI = 200;  // bacaan saat kalibrasi arus nol
const unsigned long PERIODE_UKUR_MS = 1000; // tampilkan hasil tiap 1 detik
const unsigned long BAUD_SERIAL     = 115200;

// ---------- State ----------
float offsetNolAdc      = 1551.0;  // nilai ADC saat arus nol (hasil kalibrasi)
float arusMotorA         = 0.0;     // arus motor saat ini (ampere)
float arusMaksA          = 0.0;     // arus tertinggi sejak dinyalakan
bool  alarmAktif         = false;
unsigned long waktuUkurTerakhirMs = 0;

Adafruit_SSD1306 oled(LEBAR_OLED, TINGGI_OLED, &Wire, RESET_OLED);

// Baca ADC beberapa kali lalu rata-rata, supaya angka stabil.
float bacaAdcRataRata(uint8_t jumlahSampel) {
  unsigned long jumlah = 0;
  for (uint8_t i = 0; i < jumlahSampel; i++) {
    jumlah += analogRead(PIN_SENSOR_ARUS);
    delay(2);
  }
  return (float)jumlah / jumlahSampel;
}

// Ubah nilai ADC rata-rata menjadi arus (ampere).
// Langkah: ADC -> tegangan di pin -> tegangan sensor (x faktor pembagi)
// -> kurangi offset nol -> bagi sensitivitas.
float adcKeArus(float nilaiAdc) {
  float teganganAdcV = nilaiAdc * TEGANGAN_ADC_MAX_V / NILAI_ADC_MAX;
  float teganganSensorV = teganganAdcV * FAKTOR_PEMBAGI;
  float teganganNolAdcV = offsetNolAdc * TEGANGAN_ADC_MAX_V / NILAI_ADC_MAX * FAKTOR_PEMBAGI;
  return (teganganSensorV - teganganNolAdcV) / SENSITIVITAS_V_PER_A;
}

// Ukur arus sekali: baca ADC, hitung arus, perbarui maksimum dan alarm.
void ukurArus() {
  float nilaiAdc = bacaAdcRataRata(JUMLAH_SAMPEL);
  arusMotorA = adcKeArus(nilaiAdc);
  if (arusMotorA < 0.0) {
    arusMotorA = 0.0;  // motor DC satu arah: arus negatif dibaca 0
  }
  if (arusMotorA > arusMaksA) {
    arusMaksA = arusMotorA;
  }
  alarmAktif = arusMotorA > AMBANG_ALARM_A;
  digitalWrite(PIN_BUZZER, alarmAktif ? HIGH : LOW);
  digitalWrite(PIN_LED_ALARM, alarmAktif ? HIGH : LOW);
}

// Tampilkan arus, status, dan arus tertinggi di OLED.
void tampilkanOled() {
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0);
  oled.println("ARUS MOTOR DC");
  oled.setTextSize(2);
  oled.setCursor(0, 14);
  oled.print(arusMotorA, 2);
  oled.print(" A");
  // Bilah arus: penuh saat 5 A (range sensor).
  uint8_t lebarBilah = (uint8_t)(arusMotorA / 5.0 * 120.0);
  if (lebarBilah > 120) {
    lebarBilah = 120;
  }
  oled.drawRect(0, 36, 122, 10, SSD1306_WHITE);
  oled.fillRect(1, 37, lebarBilah, 8, SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(0, 50);
  if (alarmAktif) {
    oled.print("ALARM! > 3 A");
  } else {
    oled.print("AMAN  maks ");
    oled.print(arusMaksA, 2);
    oled.print(" A");
  }
  oled.display();
}

void setup() {
  Serial.begin(BAUD_SERIAL);
  pinMode(PIN_SENSOR_ARUS, INPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_ALARM, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_LED_ALARM, LOW);
  analogReadResolution(12);  // ADC 12-bit: 0-4095

  Wire.begin(PIN_SDA_OLED, PIN_SCL_OLED);
  if (!oled.begin(SSD1306_SWITCHCAPVCC, ALAMAT_OLED)) {
    Serial.println("OLED 0x3C tidak ditemukan, cek wiring I2C!");
    while (true) {
      delay(1000);
    }
  }

  // Kalibrasi arus nol (motor harus mati): rata-rata 200 bacaan ADC.
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0);
  oled.println("Kalibrasi arus nol");
  oled.println("Motor harus MATI...");
  oled.display();
  offsetNolAdc = bacaAdcRataRata((uint8_t)JUMLAH_SAMPEL_KALIBRASI);
  Serial.print("Offset arus nol (ADC): ");
  Serial.println(offsetNolAdc, 1);
  Serial.println("Pengukur arus motor DC siap.");
}

void loop() {
  unsigned long sekarang = millis();
  if (sekarang - waktuUkurTerakhirMs >= PERIODE_UKUR_MS) {
    waktuUkurTerakhirMs = sekarang;
    ukurArus();
    tampilkanOled();
    Serial.print("Arus: ");
    Serial.print(arusMotorA, 2);
    Serial.print(" A  maks: ");
    Serial.print(arusMaksA, 2);
    Serial.println(alarmAktif ? " A  ALARM" : " A");
  }
}
