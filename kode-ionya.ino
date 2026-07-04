#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "MAX30105.h"
#include "heartRate.h"      // Untuk deteksi ritme instan
#include "spo2_algorithm.h" // Untuk hitung nilai SpO2 %

MAX30105 particleSensor;

// === 1. KONFIGURASI WI-FI & SERVER GCP ===
const char* ssid = "BIG FAMILY";         // <--- Ganti dengan Nama Wi-Fi Aa
const char* password = "modaldonk"; // <--- Ganti dengan Password Wi-Fi Aa
const char* serverUrl = "http://34.121.110.87:8000/api/kirim-data";

// === 2. PIN HARDWARE ===
const int pinLED = 25;     
const int pinBuzzer = 13;  
const int pinNTC = 34;     

// === 3. VARIABEL BUFFER SENSOR ===
#define BUFFER_SIZE 50 
uint32_t irBuffer[BUFFER_SIZE]; 
uint32_t redBuffer[BUFFER_SIZE]; 

int32_t bufferLength = BUFFER_SIZE; 
int32_t spo2;      
int8_t validSPO2;  
int32_t heartRate; 
int8_t validHeartRate; 

// Variabel waktu untuk membatasi pengiriman data ke server (setiap 5 detik)
unsigned long waktuTerakhirKirim = 0;
const unsigned long intervalKirim = 5000; // 5000 milidetik = 5 detik

// === 4. FUNGSI KIRIM DATA KE SERVER GCP ===
void kirimDataKeServer(int dataBpm, int dataSpo2, float dataSuhu) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(serverUrl);
    http.addHeader("Content-Type", "application/json");

    // Mengunci struktur JSON agar sesuai dengan backend FastAPI website
    String jsonPayload = "{\"bpm\":" + String(dataBpm) + 
                         ",\"spo2\":" + String(dataSpo2) + 
                         ",\"suhu\":" + String(dataSuhu) + "}";

    Serial.print("\n[GCP] Mengirim data ke GCP: ");
    Serial.println(jsonPayload);

    int httpResponseCode = http.POST(jsonPayload);

    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.println("[GCP] Respon Server: " + response);
      
      // LOGIKA AKTUATOR OTOMATIS BERDASARKAN RESPON SERVER
      if (response.indexOf("stres") > 0 || response.indexOf("demam") > 0) {
        Serial.println("[ALARM] Kondisi Bahaya Terdeteksi oleh Server! Nyalakan Alarm.");
        // Buzzer berbunyi panjang tanda peringatan dari server
        tone(pinBuzzer, 1500, 1000); 
      }
    } else {
      Serial.print("[GCP] Gagal Kirim, Kode Eror HTTP: ");
      Serial.println(httpResponseCode);
    }
    http.end();
  } else {
    Serial.println("\n[GCP] Wi-Fi Terputus, Gagal Mengirim.");
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== SISTEM MONITORING KESEHATAN IOT ACTIVE ===");

  pinMode(pinLED, OUTPUT); 
  pinMode(pinBuzzer, OUTPUT);
  pinMode(pinNTC, INPUT);    

  // Hubungkan ke Wi-Fi Rumah/Kosan
  Serial.print("Menghubungkan ke Wi-Fi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n[OK] Wi-Fi Terhubung!");
  Serial.print("IP Address ESP32: ");
  Serial.println(WiFi.localIP());

  Wire.begin(21, 22);

  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("[ERROR] Sensor MAX30102 tidak terdeteksi!");
    while (1);
  }

  particleSensor.setup(0x1F, 4, 2, 200, 411, 4096); 
  Serial.println("[OK] Semua Hardware & Jaringan Siap. Tempelkan Jari Anda...");
}

void loop() {
  // 1. Ambil 50 sampel data + Aktifkan Fitur Kedip LED & Buzzer Real-Time
  for (byte i = 0; i < bufferLength; i++) {
    while (particleSensor.available() == false) particleSensor.check(); 
    
    redBuffer[i] = particleSensor.getRed();
    irBuffer[i] = particleSensor.getIR();
    particleSensor.nextSample();

    // Logika Ritme Instan Mengikuti Denyut Nadi Asli
    if (irBuffer[i] > 50000 && checkForBeat(irBuffer[i]) == true) {
      digitalWrite(pinLED, HIGH);
      tone(pinBuzzer, 2800); 
      delay(40);             
      digitalWrite(pinLED, LOW);
      noTone(pinBuzzer);
    }
  }

  // 2. Kalkulasi Algoritma BPM dan SpO2 setelah sampel terkumpul
  maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, &spo2, &validSPO2, &heartRate, &validHeartRate);

  // 3. Baca Sensor Suhu NTC dan lakukan Kalibrasi
  int nilaiADC = analogRead(pinNTC); 
  float suhuCelcius = 27.0 + ((160.0 - nilaiADC) * 0.1);
  if (suhuCelcius < 0) suhuCelcius = 0;

  // Variabel penampung nilai akhir (jika tidak valid, beri nilai default/standby)
  int bpmFinal = (validHeartRate == 1 && heartRate > 40 && heartRate < 150) ? heartRate : 0;
  int spo2Final = (validSPO2 == 1 && spo2 >= 70 && spo2 <= 100) ? spo2 : 0;

  // 4. Tampilkan Hasil Analisis di Serial Monitor Lokal
  Serial.print("\n[LOKAL] Suhu: ");
  Serial.print(suhuCelcius, 1);
  Serial.print(" °C | ");

  if (irBuffer[bufferLength - 1] < 50000) {
    Serial.println("Status: Jari Lepas");
    bpmFinal = 0;
    spo2Final = 0;
  } else {
    Serial.print("BPM: "); Serial.print(bpmFinal);
    Serial.print(" | SpO2: "); Serial.print(spo2Final); Serial.println(" %");
  }

  // 5. MANAJEMEN WAKTU: Kirim data ke Server GCP setiap 5 detik sekali
  // Hanya mengirim jika jari terpasang dengan benar (BPM dan SpO2 tidak nol)
  if (millis() - waktuTerakhirKirim >= intervalKirim) {
    waktuTerakhirKirim = millis();
    
    if (bpmFinal > 0 && spo2Final > 0) {
      kirimDataKeServer(bpmFinal, spo2Final, suhuCelcius);
    } else {
      Serial.println("[GCP] Data belum valid/jari lepas, menunda pengiriman data ke server.");
    }
  }
}