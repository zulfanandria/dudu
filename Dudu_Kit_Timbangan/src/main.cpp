#include <Arduino.h>
#include <Wire.h>
#include "SH1106Wire.h" 
#include <HX711_ADC.h>

// ==========================================
// KONFIGURASI PIN (HARDWARE SETUP)
// ==========================================

// 1. Layar OLED 2.42" (I2C-2)
#define OLED_SDA 8
#define OLED_SCL 9
#define OLED_ADDR 0x3C

// 2. Load Cell HX711
const int HX711_dout = 4;
const int HX711_sck = 5;

// ==========================================
// OBJECTS & VARIABLES
// ==========================================

// Driver SH1106 untuk layar 1.3" / 2.42"
SH1106Wire display(OLED_ADDR, OLED_SDA, OLED_SCL); 
HX711_ADC LoadCell(HX711_dout, HX711_sck);

// --- SETTING KALIBRASI ---
// Ganti nilai ini nanti dengan hasil tuning final via Serial Monitor.
// Saya set angka besar negatif karena tadi pembacaan Anda 4000 (positif) saat ditekan.
float calibrationValue = -25000.0; 

// Timer untuk update layar agar tidak kedip
unsigned long t = 0;

// ==========================================
// FUNGSI DISPLAY (UI)
// ==========================================

void drawBigText(int x, int y, const String &text) {
  // Efek Bold Manual (Geser 1 pixel)
  display.setFont(ArialMT_Plain_24);
  display.drawString(x, y, text);
  display.drawString(x + 1, y, text);
  display.drawString(x, y + 1, text);
}

void drawWeightUI(float valueKg) {
  char intPart[8];
  char fracPart[8];

  // Logic: Ambil 2 desimal saja agar stabil
  // Contoh: 65.4567 -> 65 .45
  int integer = (int)valueKg;
  int fraction = abs((int)((valueKg - integer) * 100)); // Ambil 2 digit

  sprintf(intPart, "%d", integer);
  sprintf(fracPart, ".%02d", fraction); 

  display.clear();

  // === HEADER ===
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(0, 0, "HEALTH BOX");
  display.drawLine(0, 12, 128, 12);

  // === CENTER ALIGNMENT CALCULATION ===
  display.setFont(ArialMT_Plain_24);
  int wInt = display.getStringWidth(intPart);
  
  display.setFont(ArialMT_Plain_16);
  int wFrac = display.getStringWidth(fracPart);
  
  display.setFont(ArialMT_Plain_10);
  int wUnit = display.getStringWidth("kg");

  // Hitung posisi X awal agar teks tepat di tengah layar
  int totalWidth = wInt + wFrac + wUnit + 6; 
  int startX = (128 - totalWidth) / 2;
  int baseY = 25; 

  // === DRAW TEXT ===
  // 1. Angka Utama (Besar)
  drawBigText(startX, baseY, intPart);

  // 2. Desimal (Sedang)
  display.setFont(ArialMT_Plain_16);
  display.drawString(startX + wInt + 2, baseY + 8, fracPart);

  // 3. Satuan (Kecil)
  display.setFont(ArialMT_Plain_10);
  display.drawString(startX + wInt + wFrac + 6, baseY + 14, "kg");

  display.display();
}

// ==========================================
// MAIN SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(100);

  // 1. INIT OLED
  if(!display.init()) {
    Serial.println("OLED Error!");
  }
  display.flipScreenVertically();
  display.setContrast(255); // Maksimal terang
  
  // Tampilan Awal
  display.clear();
  display.setFont(ArialMT_Plain_16);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 20, "Booting...");
  display.setFont(ArialMT_Plain_10);
  display.drawString(64, 45, "Jangan Injak!");
  display.display();

  // 2. INIT LOAD CELL
  Serial.println("Starting Load Cell...");
  LoadCell.begin();
  
  // -- FITUR STABILISASI --
  // Meningkatkan jumlah sampel rata-rata.
  // Default 1. Ubah ke 16 agar angka "kalem" (tidak loncat-loncat).
  LoadCell.setSamplesInUse(16); 

  // Waktu stabilisasi 2 detik (Tare otomatis)
  LoadCell.start(2000, true); 
  
  if (LoadCell.getTareTimeoutFlag()) {
    Serial.println("TIMEOUT: Cek kabel HX711!");
    display.clear();
    display.drawString(64, 20, "ERROR SENSOR");
    display.display();
    while (1); // Stop loop jika hardware error
  } else {
    LoadCell.setCalFactor(calibrationValue); 
    Serial.println("Startup OK");
  }
}

// ==========================================
// MAIN LOOP
// ==========================================
void loop() {
  // Wajib dipanggil di setiap loop untuk sampling data
  LoadCell.update();

  // Refresh data setiap 200ms (5 FPS) - Cukup responsif tapi stabil
  if (millis() > t + 200) {
    float weight = LoadCell.getData();

    // -- FITUR ZERO THRESHOLD --
    // Jika berat cuma noise kecil (< 0.1 kg), anggap 0.00
    // Ini menghilangkan angka -0.02 atau 0.05 saat kosong
    if (abs(weight) < 0.1) {
      weight = 0.00;
    }

    // Debugging Serial
    Serial.print("Weight: ");
    Serial.print(weight);
    Serial.print(" | CalFactor: ");
    Serial.println(calibrationValue);

    // Update OLED UI
    drawWeightUI(weight);
    
    t = millis();
  }

  // -- FITUR TUNING KALIBRASI LIVE --
  // Ketik karakter ini di Serial Monitor untuk ubah kalibrasi realtime
  if (Serial.available() > 0) {
    char inByte = Serial.read();
    if (inByte == 't') {
      LoadCell.tareNoDelay(); 
      Serial.println("Tare (Nol) Berhasil.");
    }
    // Tuning Halus (Koreksi kecil)
    else if (inByte == '+') { calibrationValue += 10.0; LoadCell.setCalFactor(calibrationValue); }
    else if (inByte == '-') { calibrationValue -= 10.0; LoadCell.setCalFactor(calibrationValue); }
    // Tuning Kasar (Koreksi cepat)
    else if (inByte == 'A') { calibrationValue += 1000.0; LoadCell.setCalFactor(calibrationValue); }
    else if (inByte == 'Z') { calibrationValue -= 1000.0; LoadCell.setCalFactor(calibrationValue); }
  }
}
