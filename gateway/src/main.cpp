/*
 * KÓD PRE: Gateway (BLE Scanner)
 * Doska: ESP32-C6 (zatial)
 */
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <aes/esp_aes.h>
#include <mbedtls/sha256.h>
#include <map>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>


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

// Štruktúra pre registrovaný senzor
struct RegisteredSensor {
  uint64_t chipId;
  uint8_t aesKey[16];
  String name;
};

// Databáza všetkých známych senzorov (kľúč = ID senzora)
std::map<uint32_t, SensorRecord> sensorDatabase;

// Databáza registrovaných senzorov (kľúč = chipId)
std::map<uint64_t, RegisteredSensor> registeredSensors;

// Web server na porte 80
AsyncWebServer server(80);

// WiFi údaje
const char* ssid = "Gateway_Config";
const char* password = "12345678";

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

// Funkcia na uloženie registrovaných senzorov do súboru
void saveSensorsToFile() {
  File file = LittleFS.open("/sensors.json", "w");
  if (!file) {
    Serial.println("Nepodarilo sa otvorit subor na zapis");
    return;
  }
  
  JsonDocument doc;
  JsonArray sensorsArray = doc.to<JsonArray>();
  
  for (const auto &pair : registeredSensors) {
    JsonObject sensor = sensorsArray.add<JsonObject>();
    char chipIdStr[17];
    sprintf(chipIdStr, "%016llX", pair.first);
    sensor["chipId"] = chipIdStr;
    sensor["name"] = pair.second.name;
  }
  
  serializeJson(doc, file);
  file.close();
  Serial.println("Registrovane senzory ulozene");
}

// Funkcia na načítanie registrovaných senzorov zo súboru
void loadSensorsFromFile() {
  if (!LittleFS.exists("/sensors.json")) {
    Serial.println("Subor sensors.json neexistuje");
    return;
  }
  
  File file = LittleFS.open("/sensors.json", "r");
  if (!file) {
    Serial.println("Nepodarilo sa otvorit subor na citanie");
    return;
  }
  
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  
  if (error) {
    Serial.println("Chyba pri parsovani JSON");
    return;
  }
  
  JsonArray sensorsArray = doc.as<JsonArray>();
  for (JsonObject sensor : sensorsArray) {
    const char* chipIdStr = sensor["chipId"];
    uint64_t chipId = strtoull(chipIdStr, NULL, 16);
    
    RegisteredSensor regSensor;
    regSensor.chipId = chipId;
    regSensor.name = sensor["name"].as<String>();
    generateKeyFromChipId(chipId, regSensor.aesKey);
    
    registeredSensors[chipId] = regSensor;
    Serial.printf("Nacitany senzor: ChipID=0x%016llX, Name=%s\n", chipId, regSensor.name.c_str());
  }
}

// Funkcia na registráciu senzora
void registerSensor(uint64_t chipId, const String& name) {
  RegisteredSensor sensor;
  sensor.chipId = chipId;
  sensor.name = name;
  generateKeyFromChipId(chipId, sensor.aesKey);
  
  registeredSensors[chipId] = sensor;
  saveSensorsToFile();
  
  Serial.printf("Registrovany novy senzor: ChipID=0x%016llX, Name=%s\n", chipId, name.c_str());
  Serial.print("AES kluc: ");
  for (int i = 0; i < 16; i++) {
    Serial.printf("%02X ", sensor.aesKey[i]);
  }
  Serial.println();
}

// Funkcia na odregistráciu senzora
void unregisterSensor(uint64_t chipId) {
  if (registeredSensors.erase(chipId) > 0) {
    saveSensorsToFile();
    Serial.printf("Senzor ChipID=0x%016llX bol odstraneny\n", chipId);
  }
}


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
    Serial.print("Prijaty sifrovaný blok: ");
    for (int i=0; i<16; i++) Serial.printf("%02X ", encrypted[i]);
    Serial.println();

    // Pokúsime sa dešifrovať pomocou všetkých registrovaných kľúčov
    bool decrypted = false;
    for (const auto &pair : registeredSensors) {
      uint8_t decryptedData[16];
      esp_aes_context ctx;
      esp_aes_init(&ctx);
      esp_aes_setkey(&ctx, pair.second.aesKey, 128);
      esp_aes_decrypt(&ctx, encrypted, decryptedData);
      esp_aes_free(&ctx);

      // Interpretácia dešifrovaných bajtov ako SensorData
      SensorData rcv;
      memcpy(&rcv, decryptedData, sizeof(SensorData));

      // Validácia: rozumné hodnoty teploty a vlhkosti
      float t = rcv.temperature/100.0;
      float h = rcv.humidity/100.0;
      
      // Očakávame teplotu medzi -40 a 85°C a vlhkosť 0-100%
      if (t >= -40.0 && t <= 85.0 && h >= 0.0 && h <= 100.0) {
        Serial.printf("Uspesne desifrovane pomocou ChipID=0x%016llX (Name: %s)\n", 
                      pair.first, pair.second.name.c_str());
        updateSensorData(rcv.sensorId, t, h);
        decrypted = true;
        break;
      }
    }
    
    if (!decrypted) {
      Serial.println("Ziadny kluc nepasuje - neregistrovany senzor alebo chybne data");
    }
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
  
  // Inicializácia LittleFS pre úložisko
  if (!LittleFS.begin(true)) {
    Serial.println("Chyba pri montovani LittleFS");
    return;
  }
  Serial.println("LittleFS namontovany");
  
  // Načítanie registrovaných senzorov
  loadSensorsFromFile();
  
  // Nastavenie WiFi AP
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP adresa: ");
  Serial.println(IP);
  
  // Web server routes
  
  // Hlavná stránka s GUI
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Gateway - Registrácia senzorov</title>
  <style>
    body { font-family: Arial, sans-serif; margin: 20px; background: #f0f0f0; }
    .container { max-width: 800px; margin: auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 0 10px rgba(0,0,0,0.1); }
    h1 { color: #333; }
    .form-group { margin-bottom: 15px; }
    label { display: block; margin-bottom: 5px; font-weight: bold; }
    input[type="text"] { width: 100%; padding: 8px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; }
    button { background: #4CAF50; color: white; padding: 10px 20px; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; }
    button:hover { background: #45a049; }
    button.delete { background: #f44336; }
    button.delete:hover { background: #da190b; }
    table { width: 100%; border-collapse: collapse; margin-top: 20px; }
    th, td { padding: 12px; text-align: left; border-bottom: 1px solid #ddd; }
    th { background-color: #4CAF50; color: white; }
    tr:hover { background-color: #f5f5f5; }
    .sensor-data { margin-top: 30px; }
    .info { background: #e7f3fe; padding: 10px; border-left: 4px solid #2196F3; margin-bottom: 20px; }
  </style>
</head>
<body>
  <div class="container">
    <h1>🌡️ Gateway - Registrácia senzorov</h1>
    
    <div class="info">
      <strong>Inštrukcie:</strong> Zadajte Chip ID senzora (64-bit hex hodnota, napr. 0x123456789ABCDEF0) a meno pre identifikáciu.
      Systém automaticky vygeneruje šifrovací kľúč pomocou SHA-256.
    </div>
    
    <h2>Registrovať nový senzor</h2>
    <div class="form-group">
      <label for="chipId">Chip ID (hex):</label>
      <input type="text" id="chipId" placeholder="0x123456789ABCDEF0 alebo 123456789ABCDEF0">
    </div>
    <div class="form-group">
      <label for="name">Názov senzora:</label>
      <input type="text" id="name" placeholder="Napr. Obývačka, Kúpeľňa">
    </div>
    <button onclick="registerSensor()">Registrovať senzor</button>
    
    <h2>Registrované senzory</h2>
    <div id="sensors"></div>
    
    <h2 class="sensor-data">Prijaté dáta zo senzorov</h2>
    <div id="sensorData"></div>
  </div>
  
  <script>
    function loadSensors() {
      fetch('/api/sensors')
        .then(response => response.json())
        .then(data => {
          let html = '<table><tr><th>Chip ID</th><th>Názov</th><th>Akcia</th></tr>';
          data.forEach(sensor => {
            html += `<tr>
              <td>${sensor.chipId}</td>
              <td>${sensor.name}</td>
              <td><button class="delete" onclick="deleteSensor('${sensor.chipId}')">Odstrániť</button></td>
            </tr>`;
          });
          html += '</table>';
          document.getElementById('sensors').innerHTML = html;
        });
    }
    
    function loadSensorData() {
      fetch('/api/data')
        .then(response => response.json())
        .then(data => {
          let html = '<table><tr><th>Sensor ID</th><th>Teplota (°C)</th><th>Vlhkosť (%)</th><th>Posledná aktualizácia (s)</th></tr>';
          data.forEach(sensor => {
            html += `<tr>
              <td>0x${sensor.id}</td>
              <td>${sensor.temperature}</td>
              <td>${sensor.humidity}</td>
              <td>${sensor.lastSeen}</td>
            </tr>`;
          });
          html += '</table>';
          document.getElementById('sensorData').innerHTML = html;
        });
    }
    
    function registerSensor() {
      let chipId = document.getElementById('chipId').value.trim();
      let name = document.getElementById('name').value.trim();
      
      if (!chipId || !name) {
        alert('Prosím vyplňte všetky polia');
        return;
      }
      
      // Odstránenie 0x prefixu ak existuje
      chipId = chipId.replace(/^0x/i, '');
      
      fetch('/api/register', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ chipId: chipId, name: name })
      })
      .then(response => response.json())
      .then(data => {
        if (data.success) {
          alert('Senzor úspešne registrovaný!');
          document.getElementById('chipId').value = '';
          document.getElementById('name').value = '';
          loadSensors();
        } else {
          alert('Chyba: ' + data.message);
        }
      });
    }
    
    function deleteSensor(chipId) {
      if (!confirm('Naozaj chcete odstrániť tento senzor?')) return;
      
      fetch('/api/unregister', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ chipId: chipId })
      })
      .then(response => response.json())
      .then(data => {
        if (data.success) {
          alert('Senzor odstránený!');
          loadSensors();
        }
      });
    }
    
    // Načítať dáta pri načítaní stránky
    loadSensors();
    loadSensorData();
    
    // Automaticky aktualizovať dáta každých 5 sekúnd
    setInterval(loadSensorData, 5000);
  </script>
</body>
</html>
)rawliteral";
    request->send(200, "text/html", html);
  });
  
  // API endpoint - zoznam registrovaných senzorov
  server.on("/api/sensors", HTTP_GET, [](AsyncWebServerRequest *request){
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    
    for (const auto &pair : registeredSensors) {
      JsonObject sensor = arr.add<JsonObject>();
      char chipIdStr[17];
      sprintf(chipIdStr, "%016llX", pair.first);
      sensor["chipId"] = chipIdStr;
      sensor["name"] = pair.second.name;
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // API endpoint - registrácia senzora
  server.on("/api/register", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      JsonDocument doc;
      deserializeJson(doc, (const char*)data);
      
      const char* chipIdStr = doc["chipId"];
      const char* name = doc["name"];
      
      uint64_t chipId = strtoull(chipIdStr, NULL, 16);
      
      if (chipId == 0) {
        request->send(200, "application/json", "{\"success\":false,\"message\":\"Neplatné Chip ID\"}");
        return;
      }
      
      registerSensor(chipId, String(name));
      request->send(200, "application/json", "{\"success\":true}");
    });
  
  // API endpoint - odregistrácia senzora
  server.on("/api/unregister", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      JsonDocument doc;
      deserializeJson(doc, (const char*)data);
      
      const char* chipIdStr = doc["chipId"];
      uint64_t chipId = strtoull(chipIdStr, NULL, 16);
      
      unregisterSensor(chipId);
      request->send(200, "application/json", "{\"success\":true}");
    });
  
  // API endpoint - dáta zo senzorov
  server.on("/api/data", HTTP_GET, [](AsyncWebServerRequest *request){
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    
    unsigned long now = millis();
    for (const auto &pair : sensorDatabase) {
      JsonObject sensor = arr.add<JsonObject>();
      char idStr[9];
      sprintf(idStr, "%08X", pair.first);
      sensor["id"] = idStr;
      sensor["temperature"] = String(pair.second.temperature, 2);
      sensor["humidity"] = String(pair.second.humidity, 2);
      sensor["lastSeen"] = String((now - pair.second.lastSeen) / 1000.0, 1);
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  server.begin();
  Serial.println("Web server spusteny");
  
  // BLE Inicializácia
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
