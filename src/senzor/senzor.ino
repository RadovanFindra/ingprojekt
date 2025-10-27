/*
 * KÓD PRE: Senzor (BLE Advertiser)
 * Doska: ESP32-C6 (zatial)
 */

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLEAdvertising.h>
#include <WiFi.h> 

#define CUSTOM_MANUFACTURER_ID 0x1234

BLEAdvertising *pAdvertising;

// Dátová štruktúra s ID uzla
struct SensorData {
  uint32_t sensorId;    // Jedinečné ID senzora (posledné 4 bajty MAC)
  uint16_t temperature; // Teplota * 100
  uint16_t humidity;    // Vlhkosť * 100
};

// Globálna premenná pre naše ID
uint32_t mySensorId = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("Startujem Senzorovy Uzol s ID...");

  // -----------------------------------------------------------------
  // OPRAVA CHYBY (ID=0): Vynútime inicializáciu Wi-Fi hardvéru
  // -----------------------------------------------------------------
  WiFi.mode(WIFI_STA); // Inicializuje Wi-Fi v režime Station
  WiFi.disconnect();   // Hneď sa odpojíme, nechceme sa pripájať

  // Získanie unikátneho ID (posledné 4 bajty MAC)
  uint8_t mac_addr[6];
  
  // Teraz by čítanie MAC malo fungovať
  WiFi.macAddress(mac_addr);
  // -----------------------------------------------------------------

  // Použijeme posledné 4 bajty MAC pre ID
  mySensorId = (mac_addr[2] << 24) | (mac_addr[3] << 16) | (mac_addr[4] << 8) | mac_addr[5];

  Serial.printf("Unikatne ID Senzora: 0x%08X (Plna MAC: %02X:%02X:%02X:%02X:%02X:%02X)\n", 
                mySensorId, mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);

  // Kontrola, či ID stále nie je 0
  if (mySensorId == 0) {
    Serial.println("CHYBA: Stale neviem precitat MAC adresu!");
    // Tu by sme mohli zastaviť alebo použiť náhodné ID
  }

  BLEDevice::init("Senzor_ID_01");
  pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x06); 
  
  Serial.println("Zacinam vysielat (Advertising)...");
}

void loop() {
  // 1. SIMULÁCIA DÁT
  float temp_f = 25.0 + (rand() % 100) / 100.0;
  float humid_f = 45.0 + (rand() % 100) / 100.0;

  SensorData dataToSend;
  dataToSend.sensorId = mySensorId; 
  dataToSend.temperature = (uint16_t)(temp_f * 100);
  dataToSend.humidity = (uint16_t)(humid_f * 100);


  // 2. PRÍPRAVA DÁTOVÉHO BALÍČKA
  std::string strManufacturerData = "";
  strManufacturerData += (char)(CUSTOM_MANUFACTURER_ID & 0xFF);
  strManufacturerData += (char)((CUSTOM_MANUFACTURER_ID >> 8) & 0xFF);
  strManufacturerData.append((char*)&dataToSend, sizeof(dataToSend));

  // 3. NASTAVENIE A SPUSTENIE VYSIELANIA
  BLEAdvertisementData oAdvertisementData = BLEAdvertisementData();
  oAdvertisementData.setFlags(0x04); 
  oAdvertisementData.setManufacturerData(String(strManufacturerData.c_str())); 
  pAdvertising->setAdvertisementData(oAdvertisementData);
  pAdvertising->start(); 

  Serial.printf("ID: 0x%08X, Temp=%.2f, Hum=%.2f\n", dataToSend.sensorId, temp_f, humid_f);
  
  delay(5000);
  pAdvertising->stop();
  delay(100); 
}