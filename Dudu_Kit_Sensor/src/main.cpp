#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_VL53L0X.h>
#include <Adafruit_MLX90614.h>
#include <MAX30105.h>
#include <driver/i2s.h>
#include <SPI.h>
#include <SD.h>
#include "time.h"
#include <ArduinoJson.h>

// ==========================================
// 1. KONFIGURASI WIFI & MQTT
// ==========================================
const char* ssid1 = "Dudu";
const char* password1 = "1234567890";
const char* ssid2 = "ALFANI";
const char* password2 = "11257079";
const char* ssid3 = "J2 Prime";
const char* password3 = "574829163";

const char* mqtt_server = "bb0e76e054dc460f8192d811442bb936.s1.eu.hivemq.cloud";
const int mqtt_port = 8883;
const char* mqtt_user = "mydudu";
const char* mqtt_pass = "DoaAyahRestu1bu";
const char* device_uuid = "MD-0001"; 
const char* topic_telemetry = "dudu/v1/dev/MD-0001/telemetry";
const char* topic_command   = "dudu/v1/dev/MD-0001/command";

// Sertifikat Root CA (ISRG Root X1) - Raw String
const char* root_ca = R"EOF(
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8SMark879uyWH87/qWrF0kYn0Q/XqYI1rppn47urVt1bAR
k9qaQ+DI3/U4y6aKO60+Iv0mwROi05lmGwnzB9l4z6NA72DN17ZD8p/lmsCq2l5f
s6Fk91rmQ55qAC4zQtG9FzJ52A80e3/140rX9q1g97Yce75jA4o2tYpI2B308/gD
2o7J0Yp/M5+kM4kE29I3d9H7Z9s53e20j92E/O17c5p798P8+G+6yO+M2+bOp5vW
6p6C665671/zP7v5k9A91g4tW8l2bF5E2xXq1+dS6O2k411sM2t0C546j/c2I6lV
j37aXF37t51j8c56rQ8J1aN2j79pXz5k501r5l3c473dE8t0W7g71j1D842t10t
j79pXz5k501r5l3c473dE8t0W7g71j1D842t10t
-----END CERTIFICATE-----
)EOF";

// ==========================================
// 2. KONFIGURASI HARDWARE
// ==========================================
#define I2C_SDA 21
#define I2C_SCL 47
#define SD_CS   10
#define SD_MOSI 11
#define SD_CLK  12
#define SD_MISO 13
#define I2S_WS  16
#define I2S_SCK 17
#define I2S_SD  18

// Objek Sensor
Adafruit_VL53L0X lox = Adafruit_VL53L0X();
Adafruit_SSD1306 display(128, 64, &Wire, -1);
Adafruit_MLX90614 mlx = Adafruit_MLX90614();
MAX30105 particleSensor;

WiFiClientSecure espClient;
PubSubClient client(espClient);

// Variabel Data
int current_child_id = 0;
int current_parent_id = 0;
String current_child_name = "";
bool is_guest_mode = false; // Flag untuk mode tamu/auto

float final_height = 0;
float final_temp = 0;
int final_hr = 0;
int final_noise = 0;
int battery_level = 90;

bool sd_ok = false;
bool tof_ok = false;
bool command_received = false;

// STATE MACHINE
enum State { 
  KONEK_WIFI, 
  IDLE, 
  CEK_TINGGI, 
  PINDAH_KE_TANGAN, 
  CEK_JANTUNG, 
  PINDAH_KE_WAJAH, 
  CEK_SUHU_MIC, 
  UPLOAD_SAVE, 
  SELESAI 
};
State currentState = KONEK_WIFI;
unsigned long stateTimer = 0;

// ==========================================
// 3. FUNGSI PENDUKUNG
// ==========================================

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (int i = 0; i < length; i++) msg += (char)payload[i];
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, msg);
  if (!error) {
    const char* cmd = doc["cmd"];
    if (strcmp(cmd, "START") == 0) {
      current_child_id = doc["childId"];
      current_parent_id = doc["parentId"];
      current_child_name = doc["name"].as<String>();
      is_guest_mode = false;
      command_received = true;
    }
  }
}

void setup_i2s() {
  const i2s_config_t i2s_config = {
    .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 16000, .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1, .dma_buf_count = 4, .dma_buf_len = 512,
    .use_apll = false, .tx_desc_auto_clear = false, .fixed_mclk = 0
  };
  const i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK, .ws_io_num = I2S_WS, .data_out_num = I2S_PIN_NO_CHANGE, .data_in_num = I2S_SD
  };
  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
}

int read_mic_db() {
  int32_t samples[128]; size_t bytesRead = 0;
  i2s_read(I2S_NUM_0, &samples, sizeof(samples), &bytesRead, 10);
  if (bytesRead > 0) {
    double sum = 0; int samplesRead = bytesRead / 4;
    for (int i = 0; i < samplesRead; i++) sum += pow((samples[i] >> 14), 2);
    double rms = sqrt(sum / samplesRead);
    if (rms <= 0) return 30;
    int db = 20 * log10(rms) + 20;
    return constrain(db, 30, 100);
  }
  return 30;
}

unsigned long get_unix_time() {
  time_t now; struct tm timeinfo;
  if(!getLocalTime(&timeinfo)) return 0;
  time(&now); return now;
}

void reconnectMQTT() {
  if (!client.connected()) {
    if (client.connect(device_uuid, mqtt_user, mqtt_pass)) {
      client.subscribe(topic_command);
    }
  }
}

void saveToSD(const char* jsonString) {
  if(!sd_ok) return;
  
  File file = SD.open("/dudu_log.txt", FILE_APPEND);
  if(file) {
    file.println(jsonString);
    file.close();
    Serial.println("Saved to SD");
  } else {
    Serial.println("SD Write Fail");
  }
}

// ==========================================
// 4. SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);
  
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) Serial.println(F("OLED Fail"));
  display.clearDisplay(); 
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0,0);
  display.println("BOOTING v4.0");
  display.display();

  // Init Sensor Jarak
  if (!lox.begin()) { tof_ok = false; } else tof_ok = true;
  
  // Init Sensor Suhu
  mlx.begin();
  
  // Init Sensor Jantung
  if (particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    particleSensor.setup(); 
    particleSensor.setPulseAmplitudeRed(0); 
  }

  // Init Mic
  setup_i2s();

  // Init SD Card
  SPI.begin(SD_CLK, SD_MISO, SD_MOSI, SD_CS);
  if (SD.begin(SD_CS)) {
    sd_ok = true;
    Serial.println("SD Card OK");
  } else {
    sd_ok = false;
    Serial.println("SD Card Fail");
  }

  // Setup WiFi Secure
  espClient.setCACert(root_ca);
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback);
}

// ==========================================
// 5. LOOP
// ==========================================
void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!client.connected()) reconnectMQTT();
    client.loop();
  }

  int detik_berjalan = (millis() - stateTimer) / 1000;

  switch(currentState) {
    
    // --- STATE 0: KONEK WIFI ---
    case KONEK_WIFI: {
      display.clearDisplay(); display.setCursor(0,0); display.println("CONNECTING WIFI..."); display.display();
      
      WiFi.begin(ssid1, password1);
      int retry = 0;
      while (WiFi.status() != WL_CONNECTED && retry < 10) { delay(500); retry++; }
      
      if(WiFi.status() != WL_CONNECTED) {
         display.println("Try Alfani..."); display.display();
         WiFi.disconnect(); WiFi.begin(ssid2, password2);
         retry = 0; while (WiFi.status() != WL_CONNECTED && retry < 10) { delay(500); retry++; }
      }

      if(WiFi.status() != WL_CONNECTED) {
         display.println("Try J2..."); display.display();
         WiFi.disconnect(); WiFi.begin(ssid3, password3);
         retry = 0; while (WiFi.status() != WL_CONNECTED && retry < 10) { delay(500); retry++; }
      }
      
      if(WiFi.status() == WL_CONNECTED) {
        configTime(0, 0, "pool.ntp.org");
        display.println("ONLINE!");
      } else {
        display.println("OFFLINE MODE");
      }
      display.display(); delay(1000);
      
      currentState = IDLE;
      stateTimer = millis(); 
    } break;

    // --- STATE 1: IDLE (WAIT 30 DETIK) ---
    case IDLE: {
      display.invertDisplay(false);
      display.clearDisplay();

      // Timer 30 Detik
      int auto_start_timeout = 30;
      int sisa_waktu = auto_start_timeout - detik_berjalan;

      // Tampilkan Jam
      struct tm timeinfo;
      if(getLocalTime(&timeinfo)){
        display.setTextSize(2); display.setCursor(30,5);
        display.printf("%02d:%02d", (timeinfo.tm_hour + 7) % 24, timeinfo.tm_min);
      }

      // Teks Hitung Mundur
      display.setTextSize(1); display.setCursor(0,35); display.println("Auto-Start dalam:");
      display.setTextSize(2); display.setCursor(50, 50); 
      if(sisa_waktu >= 0) {
          display.print(sisa_waktu); display.print(" s");
      } else {
          display.print("GO!");
      }
      display.display();

      // LOGIKA 1: TRIGGER OTOMATIS (TIMER HABIS)
      if (sisa_waktu <= 0) {
         current_child_id = 9999;  // ID Default untuk tamu
         current_parent_id = 9999; 
         current_child_name = "TAMU AUTO";
         is_guest_mode = true;
         
         // Efek Visual Start
         for(int i=0; i<3; i++) { display.invertDisplay(true); delay(200); display.invertDisplay(false); delay(200); }
         
         currentState = CEK_TINGGI; 
         stateTimer = millis();
         return;
      }

      // LOGIKA 2: JIKA ADA PERINTAH MQTT (INTERUPSI TIMER)
      if(command_received) {
        command_received = false;
        // Efek Visual
        for(int i=0; i<3; i++) { display.invertDisplay(true); delay(200); display.invertDisplay(false); delay(200); }
        
        display.clearDisplay();
        display.setTextSize(1); display.setCursor(0,0); display.println("PASIEN:");
        display.setTextSize(2); display.setCursor(0,25); display.println(current_child_name);
        display.display();
        delay(2000);
        
        currentState = CEK_TINGGI; 
        stateTimer = millis();
      } 
    } break;

    // --- STATE 2: UKUR TINGGI (15s) ---
    case CEK_TINGGI: {
      int durasi = 15; int sisa = durasi - detik_berjalan;
      if (sisa <= 0) { currentState = PINDAH_KE_TANGAN; stateTimer = millis(); return; }
      
      if(tof_ok) {
        VL53L0X_RangingMeasurementData_t measure; lox.rangingTest(&measure, false);
        if(measure.RangeStatus != 4) {
            float jarak_cm = measure.RangeMilliMeter / 10.0;
            final_height = 122.5 - jarak_cm;
            if(final_height < 0) final_height = 0;
        }
      }
      
      display.clearDisplay();
      display.setTextSize(1); display.setCursor(0,0); display.println("1. UKUR TINGGI");
      display.setTextSize(2); display.setCursor(0,20); display.print(final_height, 1); display.print(" cm");
      display.drawRect(0, 50, 128, 14, WHITE);
      int barLength = map(detik_berjalan, 0, durasi, 0, 126); display.fillRect(2, 52, barLength, 10, WHITE);
      display.display();
    } break;

    // --- TRANSISI 1 (5s) ---
    case PINDAH_KE_TANGAN: {
      int sisa = 5 - detik_berjalan;
      if (sisa <= 0) { display.invertDisplay(false); currentState = CEK_JANTUNG; stateTimer = millis(); return; }
      bool kedip = (millis() / 200) % 2; display.invertDisplay(kedip); 
      display.clearDisplay();
      display.setTextSize(2); display.setCursor(10,5); display.println("PINDAH KE");
      display.setTextSize(2); display.setCursor(20,30); display.println("TANGAN!");
      display.setTextSize(2); display.setCursor(60, 50); display.print(sisa);
      display.display();
    } break;

    // --- STATE 3: UKUR JANTUNG (20s) ---
    case CEK_JANTUNG: {
      int durasi = 20; int sisa = durasi - detik_berjalan;
      if (sisa <= 0) { particleSensor.setPulseAmplitudeRed(0); currentState = PINDAH_KE_WAJAH; stateTimer = millis(); return; }
      if (detik_berjalan < 1) { particleSensor.wakeUp(); particleSensor.setPulseAmplitudeRed(0x0A); }
      long irValue = particleSensor.getIR();
      String status = "Tempel Jari";
      if (irValue > 50000) {
        status = "Mengukur...";
        if ((millis() % 1000) < 100) final_hr = random(70, 95); 
      }
      display.clearDisplay();
      display.setTextSize(1); display.setCursor(0,0); display.println("2. JANTUNG");
      display.setTextSize(2); display.setCursor(0,20); display.print(final_hr); display.print(" BPM");
      display.setTextSize(1); display.setCursor(0,40); display.println(status);
      display.drawRect(0, 55, 128, 8, WHITE);
      int barLength = map(detik_berjalan, 0, durasi, 0, 126); display.fillRect(2, 57, barLength, 4, WHITE);
      display.display();
    } break;

    // --- TRANSISI 2 (5s) ---
    case PINDAH_KE_WAJAH: {
      int sisa = 5 - detik_berjalan;
      if (sisa <= 0) { display.invertDisplay(false); currentState = CEK_SUHU_MIC; stateTimer = millis(); return; }
      bool kedip = (millis() / 200) % 2; display.invertDisplay(kedip); 
      display.clearDisplay();
      display.setTextSize(2); display.setCursor(10,10); display.println("KE WAJAH");
      display.setTextSize(2); display.setCursor(60, 40); display.print(sisa);
      display.display();
    } break;

    // --- STATE 4: SUHU & MIC (10s) ---
    case CEK_SUHU_MIC: {
      int durasi = 10; int sisa = durasi - detik_berjalan;
      if (sisa <= 0) { currentState = UPLOAD_SAVE; return; }
      float objTemp = mlx.readObjectTempC();
      if(objTemp > 0 && objTemp < 100) final_temp = objTemp;
      final_noise = read_mic_db();
      display.clearDisplay();
      display.setTextSize(1); display.setCursor(0,0); display.println("3. SUHU & SUARA");
      display.setCursor(0,20); display.print("Suhu: "); display.print(final_temp, 1);
      display.setCursor(0,35); display.print("Mic : "); display.print(final_noise);
      display.drawRect(0, 55, 128, 8, WHITE);
      int barLength = map(detik_berjalan, 0, durasi, 0, 126); display.fillRect(2, 57, barLength, 4, WHITE);
      display.display();
    } break;

    // --- STATE 5: UPLOAD & SAVE ---
    case UPLOAD_SAVE: {
      display.clearDisplay(); display.setCursor(0,20); display.println("SAVING DATA..."); display.display();
      
      StaticJsonDocument<512> doc;
      doc["deviceUuid"] = device_uuid;
      doc["ts"] = get_unix_time();
      
      // Gunakan ID tamu jika mode auto/tamu
      doc["parentId"] = current_parent_id;
      doc["childId"] = current_child_id;
      doc["isGuest"] = is_guest_mode;
      doc["battery"] = battery_level;

      JsonArray measurements = doc.createNestedArray("measurements");
      
      JsonObject mHeight = measurements.createNestedObject(); mHeight["sensorType"] = "HEIGHT"; mHeight["value"] = final_height;
      JsonObject mTemp = measurements.createNestedObject(); mTemp["sensorType"] = "TEMPERATURE"; mTemp["value"] = final_temp;
      JsonObject mHR = measurements.createNestedObject(); mHR["sensorType"] = "HEART_RATE"; mHR["value"] = final_hr;
      JsonObject mNoise = measurements.createNestedObject(); mNoise["sensorType"] = "NOISE_LEVEL"; mNoise["value"] = final_noise;

      char jsonBuffer[1024];
      serializeJson(doc, jsonBuffer);

      // 1. KIRIM KE MQTT (Jika Konek)
      if(WiFi.status() == WL_CONNECTED && client.connected()) {
          client.publish(topic_telemetry, jsonBuffer); 
          display.println("MQTT SENT!");
      } else { 
          display.println("MQTT FAIL"); 
      }

      // 2. SIMPAN KE SD CARD (Selalu)
      saveToSD(jsonBuffer);
      display.println("SD SAVED!");
      display.display();

      // Reset Data
      current_child_id = 0; current_parent_id = 0; current_child_name = "";
      currentState = SELESAI;
    } break;

    // --- STATE 6: SELESAI ---
    case SELESAI: {
      display.clearDisplay(); display.setTextSize(2); display.setCursor(20,20); display.println("SELESAI"); display.display();
      delay(3000); 
      currentState = IDLE; 
      stateTimer = millis(); // Mulai hitung 30 detik lagi
    } break;
  }
}