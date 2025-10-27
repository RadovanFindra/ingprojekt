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


#define CUSTOM_MANUFACTURER_ID 0x1234

// Dátová štruktúra,
struct SensorData {
  uint32_t sensorId;     // ID
  uint16_t temperature;  // Teplota * 100
  uint16_t humidity;     // Vlhkosť * 100
};

struct SensorRecord {
  float temperature;
  float humidity;
  unsigned long lastSeen;
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


  // Callback trieda
  class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {


      if (advertisedDevice.haveManufacturerData()) {


        std::string strManufacturerData = advertisedDevice.getManufacturerData().c_str();


        //  Kontrola DĹŽKY DÁT
        if (strManufacturerData.length() >= 10) {

          // Kontrola Manufacturer ID
          uint16_t manufacturerId = (uint16_t)(strManufacturerData[1] << 8) | (uint16_t)strManufacturerData[0];


          if (manufacturerId == CUSTOM_MANUFACTURER_ID) {

            // dáta
            SensorData rcvData;
            memcpy(&rcvData, strManufacturerData.data() + 2, sizeof(rcvData));

            float temp = rcvData.temperature / 100.0;
            float humid = rcvData.humidity / 100.0;
            uint32_t sensorId = rcvData.sensorId;


            updateSensorData(sensorId, temp, humid);
          }
        }
      }
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
  Serial.println("Startujem Gateway...");

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
