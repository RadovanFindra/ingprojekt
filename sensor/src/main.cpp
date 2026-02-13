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

#define CUSTOM_MANUFACTURER_ID 0x1234

BLEAdvertising *pAdvertising;

// Dátová štruktúra
struct SensorData {
  uint32_t sensorId;    // ID (posledné 4 bajty MAC)
  uint16_t temperature; // Teplota * 100
  uint16_t humidity;    // Vlhkosť * 100
};

// AES 128-bit kľúč
const uint8_t AES_KEY[16] = { 0x10,0x20,0x30,0x40,0x50,0x60,0x70,0x80,
                              0x90,0xA0,0xB0,0xC0,0xD0,0xE0,0xF0,0x00 };

uint32_t mySensorId = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("Startujem Senzorovy Uzol s ID...");


  WiFi.mode(WIFI_STA);
  WiFi.disconnect();


  uint8_t mac_addr[6];


  WiFi.macAddress(mac_addr);

  // Použijeme posledné 4 bajty MAC pre ID
  mySensorId = (mac_addr[2] << 24) | (mac_addr[3] << 16) | (mac_addr[4] << 8) | mac_addr[5];

  Serial.printf("Unikatne ID Senzora: 0x%08X (Plna MAC: %02X:%02X:%02X:%02X:%02X:%02X)\n", 
                mySensorId, mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);

  // Kontrola, či ID nie je 0
  if (mySensorId == 0) {
    Serial.println("CHYBA: neviem precitat MAC adresu!");
  }

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
