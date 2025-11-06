/*
 * KÓD PRE: Gateway (BLE Scanner)
 * Doska: ESP32-C6 (zatial)
 */

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <aes/esp_aes.h>
#include <map>


#define CUSTOM_MANUFACTURER_ID 0x1234

// Dátová štruktúra,
struct SensorData {
  uint32_t sensorId;    // Unikátne ID senzora (posledné 4 bajty MAC)
  uint16_t temperature; // Teplota * 100
  uint16_t humidity;    // Vlhkosť * 100
};

struct SensorRecord {
  float temperature;
  float humidity;
  unsigned long lastSeen;
};

// Databáza všetkých známych senzorov (kľúč = ID senzora)
std::map<uint32_t, SensorRecord> sensorDatabase;

// AES kľúč musí byť rovnaký ako na senzore
const uint8_t AES_KEY[16] = { 0x10,0x20,0x30,0x40,0x50,0x60,0x70,0x80,
                              0x90,0xA0,0xB0,0xC0,0xD0,0xE0,0xF0,0x00 };


// Funkcia, ktorá uloží (alebo aktualizuje) údaje o senzore
void updateSensorData(uint32_t id, float temp, float hum) {
  if (sensorDatabase.count(id) == 0)
    Serial.printf("\n[NOVÝ SENZOR] ID: 0x%08X\n", id);

  SensorRecord r;
  r.temperature = temp;
  r.humidity = hum;
  r.lastSeen = millis();
  sensorDatabase[id] = r;
}


// Callback trieda – volá sa vždy, keď sa zachytí BLE reklama
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {


    // Preskoč, ak zariadenie neposiela manufacturer data
    if (!advertisedDevice.haveManufacturerData()) return;


    std::string data = advertisedDevice.getManufacturerData().c_str();


    if (data.length() < 18) return; // 2B ID + 16B AES


    // Rozkóduj Manufacturer ID (2 bajty, little-endian)
    uint16_t mid = (uint16_t)(data[1]<<8) | (uint16_t)data[0];
    if (mid != CUSTOM_MANUFACTURER_ID) return;


    // Skopíruj 16 bajtov šifrovaných dát (za ID)
    uint8_t encrypted[16];
    memcpy(encrypted, data.data() + 2, 16);


    // HEX dump priamo pred dešifrovaním (výpis prijatého šifrovaného bloku) - len pre debug
    Serial.print("Prijatý šifrovaný blok: ");
    for (int i=0; i<16; i++) Serial.printf("%02X ", encrypted[i]);
    Serial.println();


    // Dešifrovanie 
    uint8_t decrypted[16];
    esp_aes_context ctx;
    esp_aes_init(&ctx);
    esp_aes_setkey(&ctx, AES_KEY, 128);
    esp_aes_decrypt(&ctx, encrypted, decrypted);
    esp_aes_free(&ctx);


    // Interpretácia dešifrovaných bajtov ako SensorData
    SensorData rcv;
    memcpy(&rcv, decrypted, sizeof(SensorData));

    float t = rcv.temperature/100.0;
    float h = rcv.humidity/100.0;
    updateSensorData(rcv.sensorId, t, h);
  }
};


void printDatabase() {
  Serial.println("\n-------------------------------------------");
  Serial.println("| ID Senzora | Temp (*C) | Vlhkost (%) | Last Seen (s) |");
  Serial.println("-------------------------------------------");

  unsigned long now = millis();
  for (const auto &p : sensorDatabase) {
    float age = (now - p.second.lastSeen)/1000.0;
    Serial.printf("| 0x%08X | %8.2f | %9.2f | %12.1f | \n",
                  p.first, p.second.temperature, p.second.humidity, age);
  }
  Serial.println("-------------------------------------------");
}


void setup() {
  Serial.begin(115200);
  Serial.println("Startujem Gateway...");
  BLEDevice::init("");

  BLEScan* scan = BLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(99);

  Serial.println("Zacinam skenovat...");
}

void loop() {
  BLEScanResults* r = BLEDevice::getScan()->start(5, false);

  // Pridáme výpis, koľko zariadení celkovo našiel skener (pred našim filtrom)
  Serial.printf("Skenovanie dokončené, %d zariadení\n", r->getCount());
  printDatabase();
  BLEDevice::getScan()->clearResults();
  delay(1000);
}
