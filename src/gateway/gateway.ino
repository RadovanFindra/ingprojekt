/*
 * KÓD PRE: Gateway (BLE Scanner)
 * Doska: ESP32-C6 (zatial)
 */

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <map>
#include <string>

// ID, ktoré hľadáme
#define CUSTOM_MANUFACTURER_ID 0x1234

// Dátová štruktúra, ktorú očakávame (musí sa zhodovať so Senzorom)
struct SensorData {
  uint32_t sensorId;    // Jedinečné ID senzora
  uint16_t temperature; // Teplota * 100
  uint16_t humidity;    // Vlhkosť * 100
};

// Štruktúra pre ukladanie prijatých dát na Gateway
struct SensorRecord {
  float temperature;
  float humidity;
  unsigned long lastSeen; // Kedy boli dáta naposledy prijaté
};

std::map<uint32_t, SensorRecord> sensorDatabase;


void updateSensorData(uint32_t id, float temp, float hum) {
    if (sensorDatabase.count(id) == 0) {
        Serial.printf("\n[NOVY SENZOR PRIJATY] ID: 0x%08X\n", id);
    }
    SensorRecord record;
    record.temperature = temp;
    record.humidity = hum;
    record.lastSeen = millis();
    sensorDatabase[id] = record;
}


// Callback trieda: Spracovanie nájdených zariadení
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
      
      // DEBUG: Vypíšeme *každé* zariadenie, ktoré má Manufacturer Data
      if (advertisedDevice.haveManufacturerData()) {
        
        // OPRAVA PRE C6/C3: Konverzia z Arduino String na std::string
        std::string strManufacturerData = advertisedDevice.getManufacturerData().c_str();
        
        // DEBUG: Zobrazíme surové dáta
        // Serial.printf("DEBUG: Nasiel som Manuf. Data, Dlzka: %d\n", strManufacturerData.length());

        // 1. Kontrola DĹŽKY DÁT (Očakávame 10 bajtov)
        if (strManufacturerData.length() >= 10) { 
          
          // 2. Kontrola Manufacturer ID (Očakávame 0x1234)
          uint16_t manufacturerId = (uint16_t)(strManufacturerData[1] << 8) | (uint16_t)strManufacturerData[0];

          // DEBUG: Vypíšeme nájdené ID
          // Serial.printf("DEBUG: Manuf. ID: 0x%04X (Ocakavam: 0x%04X)\n", manufacturerId, CUSTOM_MANUFACTURER_ID);

          if (manufacturerId == CUSTOM_MANUFACTURER_ID) {
            
            // 3. Extrahujeme dáta (od 3. bajtu, index 2)
            SensorData rcvData;
            memcpy(&rcvData, strManufacturerData.data() + 2, sizeof(rcvData));

            float temp = rcvData.temperature / 100.0;
            float humid = rcvData.humidity / 100.0;
            uint32_t sensorId = rcvData.sensorId;

            // 4. Aktualizácia databázy
            updateSensorData(sensorId, temp, humid);
          
          } // Koniec if (ID sa zhoduje)
        } // Koniec if (Dĺžka je v poriadku)
      } // Koniec if (Má Manuf. Data)
    }
};

void printDatabase() {
    Serial.println("\n-------------------------------------------");
    Serial.println("| ID Senzora | Temp (*C) | Vlhkost (%) | Last Seen (s) |");
    Serial.println("-------------------------------------------");
    
    unsigned long now = millis();

    for (const auto& pair : sensorDatabase) {
        uint32_t id = pair.first;
        const SensorRecord& record = pair.second;
        float time_s = (now - record.lastSeen) / 1000.0;
        Serial.printf("| 0x%08X | %8.2f | %9.2f | %12.1f | \n", 
                      id, record.temperature, record.humidity, time_s);
    }
    Serial.println("-------------------------------------------");
}


void setup() {
  Serial.begin(115200);
  Serial.println("Startujem Gateway Uzol (Multi-Senzor s LADENIM)...");

  BLEDevice::init(""); 
  
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99); 
  
  Serial.println("Zacinam skenovat...");
}

void loop() {
  BLEScanResults* foundDevices = BLEDevice::getScan()->start(5, false);
  
  // Pridáme výpis, koľko zariadení celkovo našiel skener (pred našim filtrom)
  Serial.printf("Skenovanie dokoncene. Celkovo najdenych BLE zariadeni: %d\n", foundDevices->getCount());

  printDatabase();
  BLEDevice::getScan()->clearResults();
  delay(500); 
}