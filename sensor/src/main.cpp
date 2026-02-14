/*
 * KÓD PRE: Senzor (BLE Advertiser)
 * Doska: ESP32-C6 (zatial)
 */
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLEAdvertising.h>
#include <WiFi.h>
#include <aes/esp_aes.h>
#include <mbedtls/sha256.h>

// BEZPEČNOSTNÉ UPOZORNENIE: Nastavte na 0 pre produkčné nasadenie!
#define DEBUG_PRINT_KEYS 1

#define CUSTOM_MANUFACTURER_ID 0x1234

BLEAdvertising *pAdvertising;

// Dátová štruktúra
struct SensorData {
  uint32_t sensorId;    // ID (posledné 4 bajty MAC)
  uint16_t temperature; // Teplota * 100
  uint16_t humidity;    // Vlhkosť * 100
};

// AES 128-bit kľúč - bude vygenerovaný z Chip ID
uint8_t AES_KEY[16];

uint32_t mySensorId = 0;
uint64_t myChipId = 0;

// Funkcia na generovanie AES kľúča z Chip ID pomocou SHA-256
void generateKeyFromChipId(uint64_t chipId, uint8_t* key) {
  uint8_t hash[32];
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0); // 0 = SHA-256 (nie SHA-224)
  mbedtls_sha256_update(&ctx, (uint8_t*)&chipId, sizeof(chipId));
  mbedtls_sha256_finish(&ctx, hash);
  mbedtls_sha256_free(&ctx);
  
  // Použijeme prvých 16 bajtov z hash ako AES-128 kľúč
  memcpy(key, hash, 16);
}

void setup() {
  Serial.begin(115200);
  Serial.println("Startujem Senzorovy Uzol s ID...");

  // Získanie jedinečného Chip ID z ESP32
  myChipId = ESP.getEfuseMac();
  
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  uint8_t mac_addr[6];
  WiFi.macAddress(mac_addr);

  // Použijeme posledné 4 bajty MAC pre ID
  mySensorId = (mac_addr[2] << 24) | (mac_addr[3] << 16) | (mac_addr[4] << 8) | mac_addr[5];

  Serial.printf("Unikatne ID Senzora: 0x%08X (Plna MAC: %02X:%02X:%02X:%02X:%02X:%02X)\n", 
                mySensorId, mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  Serial.printf("Chip ID: 0x%016llX\n", myChipId);

  // Kontrola, či ID nie je 0
  if (mySensorId == 0) {
    Serial.println("CHYBA: neviem precitat MAC adresu!");
  }

  // Generovanie AES kľúča z Chip ID
  generateKeyFromChipId(myChipId, AES_KEY);
  
  #if DEBUG_PRINT_KEYS
  Serial.print("Vygenerovany AES kluc z Chip ID: ");
  for (int i = 0; i < 16; i++) {
    Serial.printf("%02X ", AES_KEY[i]);
  }
  Serial.println();
  Serial.println("UPOZORNENIE: Pre produkciu nastavte DEBUG_PRINT_KEYS na 0!");
  #endif

  BLEDevice::init("Senzor_ID_01");
  pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x06);

  Serial.println("Zacinam vysielat (Advertising)...");
}

void loop() {
  float temp_f = 25.0 + (rand() % 100)/100.0;
  float humid_f = 45.0 + (rand() % 100)/100.0;

  SensorData dataToSend;
  dataToSend.sensorId = mySensorId;
  dataToSend.temperature = (uint16_t)(temp_f * 100);
  dataToSend.humidity = (uint16_t)(humid_f * 100);

  // ŠIFROVANIE
  uint8_t encrypted[16];
  esp_aes_context ctx;
  esp_aes_init(&ctx);
  esp_aes_setkey(&ctx, AES_KEY, 128);
  esp_aes_encrypt(&ctx, (uint8_t*)&dataToSend, encrypted);
  esp_aes_free(&ctx);

  // HEX dump pre overenie
  Serial.print("Šifrovaný payload: ");
  for (int i=0; i<16; i++) Serial.printf("%02X ", encrypted[i]);
  Serial.println();

  // Rekonštrukcia BLE reklamného paketu
  String payload = "";
  payload += (char)(CUSTOM_MANUFACTURER_ID & 0xFF);
  payload += (char)((CUSTOM_MANUFACTURER_ID >> 8) & 0xFF);
  payload += String((char*)encrypted, 16);

  BLEAdvertisementData adv;
  adv.setFlags(0x04);
  adv.setManufacturerData(payload.c_str());
  pAdvertising->setAdvertisementData(adv);
  pAdvertising->start();

  Serial.printf("Odosielam šifrované dáta ID=0x%08X | T=%.2f | H=%.2f\n", mySensorId, temp_f, humid_f);

  delay(5000);
  pAdvertising->stop();
  delay(100);
  delay(100);
}
