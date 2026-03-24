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
#include <IRremoteESP8266.h>
#include <IRsend.h>

// BEZPEČNOSTNÉ UPOZORNENIE: Nastavte na 0 pre produkčné nasadenie!
#define DEBUG_PRINT_KEYS 1


// Konštanty pre validáciu senzorov
#define MIN_TEMPERATURE -40.0
#define MAX_TEMPERATURE 85.0
#define MIN_HUMIDITY 0.0
#define MAX_HUMIDITY 100.0

#define CUSTOM_MANUFACTURER_ID 0x1234
#define IR_LED_PIN 2
#define SENSOR_ACTIVE_WINDOW_MS 120000UL
#define AC_COMMAND_MIN_INTERVAL_MS 7000UL

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
  String sensorId;
};

// Databáza všetkých známych senzorov (kľúč = ID senzora)
std::map<uint32_t, SensorRecord> sensorDatabase;

// Databáza registrovaných senzorov (kľúč = chipId)
std::map<uint64_t, RegisteredSensor> registeredSensors;

// Web server na porte 80
AsyncWebServer server(80);
IRsend irsend(IR_LED_PIN);

// WiFi údaje
const char* ssid = "Gateway_Config";
const char* password = "GatewaySecure2024!";  // Zmeňte toto heslo po prvom nasadení!

struct ClimateControlState {
  bool enabled;
  float targetTemp;
  float hysteresis;
  uint32_t selectedSensorId;  // 0 = automaticky podla najcerstvejsieho senzora
  bool acPowerOn;
  unsigned long lastIrCommandAt;
  uint64_t irCodeOn;
  uint64_t irCodeOff;
  uint64_t irCodeCool;
  uint16_t irBits;
};

ClimateControlState climate = {
  false,
  24.0f,
  0.5f,
  0,
  false,
  0,
  0x20DF10EF,
  0x20DF906F,
  0x20DF40BF,
  32
};

String formatHex64(uint64_t value) {
  char buf[19];
  sprintf(buf, "0x%016llX", value);
  return String(buf);
}

uint64_t parseHexToU64(const String& rawInput) {
  String input = rawInput;
  input.trim();
  if (input.startsWith("0x") || input.startsWith("0X")) {
    input = input.substring(2);
  }
  if (input.length() == 0) return 0;
  char* endPtr;
  uint64_t value = strtoull(input.c_str(), &endPtr, 16);
  if (*endPtr != '\0') return 0;
  return value;
}

bool sendAcIrCommand(uint64_t code, const char* label) {
  unsigned long now = millis();
  if (now - climate.lastIrCommandAt < AC_COMMAND_MIN_INTERVAL_MS) {
    Serial.printf("AC IR skip (%s): prilis skoro od posledneho prikazu\n", label);
    return false;
  }
  irsend.sendNEC(code, climate.irBits);
  climate.lastIrCommandAt = now;
  Serial.printf("AC IR odoslany (%s): code=0x%016llX, bits=%u\n", label, code, climate.irBits);
  return true;
}

String formatSensorIdHex(uint32_t sensorId) {
  char sensorIdStr[9];
  sprintf(sensorIdStr, "%08X", sensorId);
  return String(sensorIdStr);
}

String getSensorNameBySensorId(uint32_t sensorId) {
  String targetSensorId = formatSensorIdHex(sensorId);
  for (const auto &entry : registeredSensors) {
    if (entry.second.sensorId.length() > 0 && entry.second.sensorId.equalsIgnoreCase(targetSensorId)) {
      return entry.second.name;
    }
  }
  return "Neznamy";
}

bool getControlTemperature(float* outTemp, uint32_t* outSensorId, String* outName) {
  unsigned long now = millis();
  bool found = false;
  unsigned long newestSeen = 0;
  float selectedTemp = 0.0f;
  uint32_t selectedId = 0;

  for (const auto &pair : sensorDatabase) {
    unsigned long age = now - pair.second.lastSeen;
    if (age > SENSOR_ACTIVE_WINDOW_MS) continue;
    if (climate.selectedSensorId != 0 && pair.first != climate.selectedSensorId) continue;

    if (!found || pair.second.lastSeen > newestSeen) {
      found = true;
      newestSeen = pair.second.lastSeen;
      selectedTemp = pair.second.temperature;
      selectedId = pair.first;
    }
  }

  if (!found) return false;
  *outTemp = selectedTemp;
  *outSensorId = selectedId;
  *outName = getSensorNameBySensorId(selectedId);
  return true;
}

void runAutoClimateControl() {
  if (!climate.enabled) return;

  float controlTemp;
  uint32_t controlSensorId;
  String controlSensorName;
  if (!getControlTemperature(&controlTemp, &controlSensorId, &controlSensorName)) {
    return;
  }

  float onThreshold = climate.targetTemp + climate.hysteresis;
  float offThreshold = climate.targetTemp - climate.hysteresis;

  if (!climate.acPowerOn && controlTemp >= onThreshold) {
    uint64_t startCode = climate.irCodeCool != 0 ? climate.irCodeCool : climate.irCodeOn;
    bool sent = sendAcIrCommand(startCode, "AUTO START");
    if (sent) {
      climate.acPowerOn = true;
      Serial.printf("AUTO AC: Zapnutie, zdroj=%s (0x%08X), temp=%.2f\n",
                    controlSensorName.c_str(), controlSensorId, controlTemp);
    }
  } else if (climate.acPowerOn && controlTemp <= offThreshold) {
    bool sent = sendAcIrCommand(climate.irCodeOff, "OFF");
    if (sent) {
      climate.acPowerOn = false;
      Serial.printf("AUTO AC: Vypnutie, zdroj=%s (0x%08X), temp=%.2f\n",
                    controlSensorName.c_str(), controlSensorId, controlTemp);
    }
  }
}



bool parseSensorIdHex(const char* input, String& normalizedSensorId) {
  if (!input) return false;

  String raw = String(input);
  raw.trim();
  if (raw.length() == 0) return false;

  if (raw.startsWith("0x") || raw.startsWith("0X")) {
    raw = raw.substring(2);
  }

  if (raw.length() == 0 || raw.length() > 8) return false;

  char* endPtr;
  uint32_t sensorId = (uint32_t)strtoul(raw.c_str(), &endPtr, 16);
  if (*endPtr != '\0') return false;

  normalizedSensorId = formatSensorIdHex(sensorId);
  return true;
}

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
    sensor["sensorId"] = pair.second.sensorId;
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
    regSensor.sensorId = sensor["sensorId"] | "";
    generateKeyFromChipId(chipId, regSensor.aesKey);
    
    registeredSensors[chipId] = regSensor;
    Serial.printf("Nacitany senzor: ChipID=0x%016llX, Name=%s, SensorID=%s\n",
                  chipId, regSensor.name.c_str(), regSensor.sensorId.c_str());
  }
}

// Funkcia na registráciu senzora (sensorId sa nastaví pri prvom príjmutí dát)
void registerSensor(uint64_t chipId, const String& name) {
  RegisteredSensor sensor;
  sensor.chipId = chipId;
  sensor.name = name;
  sensor.sensorId = "";
  generateKeyFromChipId(chipId, sensor.aesKey);
  
  registeredSensors[chipId] = sensor;
  saveSensorsToFile();
  
  Serial.printf("Registrovany novy senzor: ChipID=0x%016llX, Name=%s (SensorID sa nadstaví pri prvom príjmutí dát)\n",
                chipId, name.c_str());
  
  #if DEBUG_PRINT_KEYS
  Serial.print("AES kluc: ");
  for (int i = 0; i < 16; i++) {
    Serial.printf("%02X ", sensor.aesKey[i]);
  }
  Serial.println();
  Serial.println("UPOZORNENIE: Pre produkciu nastavte DEBUG_PRINT_KEYS na 0!");
  #endif
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
      
      // Očakávame teplotu a vlhkosť v definovaných rozsahoch
      if (t >= MIN_TEMPERATURE && t <= MAX_TEMPERATURE && h >= MIN_HUMIDITY && h <= MAX_HUMIDITY) {
        String incomingSensorId = formatSensorIdHex(rcv.sensorId);
        
        // Ak je sensorId prázdny, nastav ho z prvej prijatej správy
        if (pair.second.sensorId.length() == 0) {
          registeredSensors[pair.first].sensorId = incomingSensorId;
          saveSensorsToFile();
          Serial.printf("SensorID automaticky nastavený: ChipID=0x%016llX, SensorID=%s\n",
                        pair.first, incomingSensorId.c_str());
        }
        // Ak sensorId už máme, skontroluj, či sa zhoduje
        else if (!pair.second.sensorId.equalsIgnoreCase(incomingSensorId)) {
          continue;
        }

        Serial.printf("Uspesne desifrovane: ChipID=0x%016llX (Name: %s), SensorID=%s\n",
                      pair.first, pair.second.name.c_str(), incomingSensorId.c_str());
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
  irsend.begin();
  
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
    :root {
      --bg: #f2f5f8;
      --card: #ffffff;
      --text: #16212b;
      --muted: #5b6b7a;
      --accent: #0b8f6d;
      --accent-dark: #0a775b;
      --danger: #d64045;
      --danger-dark: #b52f34;
      --line: #dde4ea;
      --chip: #e9f7f2;
      --chip-text: #0b7559;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      font-family: "Segoe UI", Tahoma, sans-serif;
      color: var(--text);
      background: radial-gradient(circle at 15% 15%, #dceff0 0%, #f2f5f8 40%, #ecf2f6 100%);
      padding: 20px;
    }
    .container {
      max-width: 1040px;
      margin: 0 auto;
      background: var(--card);
      border: 1px solid var(--line);
      border-radius: 18px;
      box-shadow: 0 12px 28px rgba(18, 35, 52, 0.08);
      padding: 22px;
    }
    h1 {
      margin: 0 0 10px 0;
      font-size: 30px;
      line-height: 1.2;
    }
    h2 { margin: 28px 0 12px; }
    .subtle { color: var(--muted); margin-bottom: 16px; }
    .info {
      background: #edf8ff;
      border: 1px solid #c8e5fb;
      color: #1a5075;
      border-radius: 10px;
      padding: 12px;
      margin-bottom: 16px;
    }
    .grid {
      display: grid;
      grid-template-columns: repeat(3, minmax(0, 1fr));
      gap: 12px;
      margin: 14px 0 20px;
    }
    .stat {
      background: #f8fbfc;
      border: 1px solid var(--line);
      border-radius: 12px;
      padding: 10px 12px;
    }
    .stat .label { color: var(--muted); font-size: 12px; }
    .stat .value { font-size: 22px; font-weight: 700; margin-top: 4px; }
    .form-wrap {
      background: #f9fbfd;
      border: 1px solid var(--line);
      border-radius: 12px;
      padding: 14px;
    }
    .form-group { margin-bottom: 12px; }
    label { display: block; margin-bottom: 6px; font-weight: 600; }
    input[type="text"] {
      width: 100%;
      padding: 10px 12px;
      border: 1px solid #cfd8e3;
      border-radius: 8px;
      font-size: 14px;
      background: #fff;
    }
    button {
      background: var(--accent);
      color: #fff;
      padding: 10px 16px;
      border: none;
      border-radius: 8px;
      cursor: pointer;
      font-size: 14px;
      font-weight: 600;
      transition: 0.2s ease;
    }
    button:hover { background: var(--accent-dark); }
    button.delete { background: var(--danger); }
    button.delete:hover { background: var(--danger-dark); }
    .table-wrap {
      overflow-x: auto;
      border: 1px solid var(--line);
      border-radius: 12px;
      background: #fff;
    }
    table { width: 100%; border-collapse: collapse; min-width: 740px; }
    th, td {
      padding: 11px 12px;
      text-align: left;
      border-bottom: 1px solid var(--line);
      font-size: 14px;
      vertical-align: middle;
    }
    th {
      background: #f0f6fa;
      color: #274055;
      font-size: 12px;
      letter-spacing: 0.04em;
      text-transform: uppercase;
    }
    tr:hover { background: #fafdfd; }
    .chip {
      display: inline-block;
      padding: 3px 8px;
      border-radius: 999px;
      background: var(--chip);
      color: var(--chip-text);
      font-size: 12px;
      font-weight: 600;
    }
    .status-ok { color: #0a7b57; font-weight: 600; }
    .status-stale { color: #b85a00; font-weight: 600; }
    .sensor-data { margin-top: 32px; }
    .footer-note { color: var(--muted); font-size: 13px; margin-top: 10px; }
    .ac-card {
      margin-top: 26px;
      border: 1px solid var(--line);
      border-radius: 12px;
      padding: 14px;
      background: #f8fbfd;
    }
    .ac-grid {
      display: grid;
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: 12px;
    }
    .ac-input { margin-bottom: 10px; }
    .ac-input label { margin-bottom: 5px; }
    .ac-input input, .ac-input select {
      width: 100%;
      padding: 10px 12px;
      border: 1px solid #cfd8e3;
      border-radius: 8px;
      background: #fff;
    }
    .ac-actions {
      display: flex;
      gap: 8px;
      flex-wrap: wrap;
      margin-top: 8px;
    }
    .btn-secondary { background: #2f6b9a; }
    .btn-secondary:hover { background: #25577e; }
    .ac-status-box {
      margin-top: 10px;
      border: 1px dashed #b9c9d7;
      border-radius: 10px;
      padding: 10px;
      color: #37556c;
      font-size: 13px;
      background: #fff;
    }
    @media (max-width: 760px) {
      body { padding: 12px; }
      .container { padding: 14px; border-radius: 12px; }
      h1 { font-size: 24px; }
      .grid { grid-template-columns: 1fr; }
      .ac-grid { grid-template-columns: 1fr; }
    }
  </style>
</head>
<body>
  <div class="container">
    <h1>Gateway Dashboard</h1>
    <div class="subtle">Registracia BLE senzorov a live prehlad prijatych merani.</div>
    <div class="grid">
      <div class="stat"><div class="label">Registrovane senzory</div><div class="value" id="registeredCount">0</div></div>
      <div class="stat"><div class="label">Aktivne data streamy</div><div class="value" id="activeCount">0</div></div>
      <div class="stat"><div class="label">Posledny refresh</div><div class="value" id="lastRefresh">-</div></div>
    </div>
    
    <div class="info">
      <strong>Inštrukcie:</strong> Zadajte Chip ID senzora (64-bit hex hodnota, napr. 0x123456789ABCDEF0) a meno pre identifikáciu.
      Systém automaticky vygeneruje šifrovací kľúč pomocou SHA-256.
    </div>
    
    <h2>Registrovať nový senzor</h2>
    <div class="form-wrap">
      <div class="form-group">
        <label for="chipId">Chip ID (hex):</label>
        <input type="text" id="chipId" placeholder="0x123456789ABCDEF0 alebo 123456789ABCDEF0">
      </div>
      <div class="form-group">
        <label for="name">Názov senzora:</label>
        <input type="text" id="name" placeholder="Napr. Obyvacka, Kupelna">
      </div>
      <button onclick="registerSensor()">Registrovat senzor</button>
    </div>
    
    <h2>Registrované senzory</h2>
    <div id="sensors"></div>
    
    <h2 class="sensor-data">Prijaté dáta zo senzorov</h2>
    <div id="sensorData"></div>

    <div class="ac-card">
      <h2>Klimatizacia (IR)</h2>
      <div class="subtle">Automaticke ovladanie podla teploty zo zvoleneho senzora.</div>
      <div class="ac-grid">
        <div>
          <div class="ac-input">
            <label for="acEnabled">Auto rezim</label>
            <select id="acEnabled">
              <option value="false">Vypnute</option>
              <option value="true">Zapnute</option>
            </select>
          </div>
          <div class="ac-input">
            <label for="acSourceSensor">Zdrojovy senzor</label>
            <select id="acSourceSensor">
              <option value="00000000">Auto (najcerstvejsi)</option>
            </select>
          </div>
          <div class="ac-input">
            <label for="acTarget">Cielova teplota (C)</label>
            <input type="text" id="acTarget" value="24.0">
          </div>
          <div class="ac-input">
            <label for="acHyst">Hysteresis (C)</label>
            <input type="text" id="acHyst" value="0.5">
          </div>
        </div>
        <div>
          <div class="ac-input">
            <label for="irBits">IR bity</label>
            <input type="text" id="irBits" value="32">
          </div>
          <div class="ac-input">
            <label for="irCodeOn">IR kod ON (hex)</label>
            <input type="text" id="irCodeOn" value="0x20DF10EF">
          </div>
          <div class="ac-input">
            <label for="irCodeOff">IR kod OFF (hex)</label>
            <input type="text" id="irCodeOff" value="0x20DF906F">
          </div>
          <div class="ac-input">
            <label for="irCodeCool">IR kod COOL preset (hex)</label>
            <input type="text" id="irCodeCool" value="0x20DF40BF">
          </div>
        </div>
      </div>
      <div class="ac-actions">
        <button onclick="saveAcConfig()">Ulozit AC konfiguraciu</button>
        <button class="btn-secondary" onclick="sendAcManual('cool')">Poslat COOL</button>
        <button class="btn-secondary" onclick="sendAcManual('on')">Poslat ON</button>
        <button class="delete" onclick="sendAcManual('off')">Poslat OFF</button>
      </div>
      <div class="ac-status-box" id="acStatus">AC status: nacitavam...</div>
    </div>
  </div>
  
  <script>
    function loadSensors() {
      fetch('/api/sensors')
        .then(response => response.json())
        .then(data => {
          document.getElementById('registeredCount').textContent = data.length;
          renderAcSensorOptions(data);
          let html = '<div class="table-wrap"><table><tr><th>Chip ID</th><th>Nazov</th><th>Sensor ID</th><th>Akcia</th></tr>';
          data.forEach(sensor => {
            const sensorId = sensor.sensorId ? `0x${sensor.sensorId}` : '<span class="chip">caka na prve data</span>';
            html += `<tr>
              <td>${sensor.chipId}</td>
              <td>${sensor.name}</td>
              <td>${sensorId}</td>
              <td><button class="delete" onclick="deleteSensor('${sensor.chipId}')">Odstranit</button></td>
            </tr>`;
          });
          html += '</table></div>';
          document.getElementById('sensors').innerHTML = html;
        });
    }

    function renderAcSensorOptions(sensors) {
      const select = document.getElementById('acSourceSensor');
      if (!select) return;

      const current = select.value;
      let html = '<option value="00000000">Auto (najcerstvejsi)</option>';
      sensors.forEach(sensor => {
        if (!sensor.sensorId) return;
        html += `<option value="${sensor.sensorId}">${sensor.name} (0x${sensor.sensorId})</option>`;
      });
      select.innerHTML = html;
      if ([...select.options].some(o => o.value === current)) {
        select.value = current;
      }
    }
    
    function loadSensorData() {
      fetch('/api/data')
        .then(response => response.json())
        .then(data => {
          data.sort((a, b) => Number(a.lastSeen) - Number(b.lastSeen));
          let activeCount = 0;
          let html = '<div class="table-wrap"><table><tr><th>Nazov</th><th>Sensor ID</th><th>Teplota (C)</th><th>Vlhkost (%)</th><th>Aktualizovane pred (s)</th><th>Stav</th></tr>';
          data.forEach(sensor => {
            const age = Number(sensor.lastSeen);
            const isActive = age <= 300;
            if (isActive) activeCount += 1;
            html += `<tr>
              <td>${sensor.name || 'Neznamy'}</td>
              <td>0x${sensor.id}</td>
              <td>${sensor.temperature}</td>
              <td>${sensor.humidity}</td>
              <td>${sensor.lastSeen}</td>
              <td class="${isActive ? 'status-ok' : 'status-stale'}">${isActive ? 'Aktivny' : 'Stary zaznam'}</td>
            </tr>`;
          });
          html += '</table></div>';
          document.getElementById('sensorData').innerHTML = html;
          document.getElementById('activeCount').textContent = activeCount;
          document.getElementById('lastRefresh').textContent = new Date().toLocaleTimeString();
        });
    }
    
    function registerSensor() {
      let chipId = document.getElementById('chipId').value.trim();
      let name = document.getElementById('name').value.trim();
      
      if (!chipId || !name) {
        alert('Prosím vyplňte všetky polia (Chip ID, Názov)');
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
          alert('Senzor uspesne registrovany. Sensor ID sa nastavi po prijati prvej spravy.');
          document.getElementById('chipId').value = '';
          document.getElementById('name').value = '';
          loadSensors();
        } else {
          alert('Chyba: ' + data.message);
        }
      });
    }
    
    function deleteSensor(chipId) {
      if (!confirm('Naozaj chcete odstranit tento senzor?')) return;
      
      fetch('/api/unregister', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ chipId: chipId })
      })
      .then(response => response.json())
      .then(data => {
        if (data.success) {
          alert('Senzor odstraneny.');
          loadSensors();
        }
      });
    }

    function loadAcStatus() {
      fetch('/api/ac/status')
        .then(response => response.json())
        .then(data => {
          document.getElementById('acEnabled').value = data.enabled ? 'true' : 'false';
          document.getElementById('acTarget').value = data.targetTemp;
          document.getElementById('acHyst').value = data.hysteresis;
          document.getElementById('irBits').value = data.irBits;
          document.getElementById('irCodeOn').value = data.irCodeOn;
          document.getElementById('irCodeOff').value = data.irCodeOff;
          document.getElementById('irCodeCool').value = data.irCodeCool;
          if (data.selectedSensorId) {
            const id = data.selectedSensorId.toUpperCase();
            const select = document.getElementById('acSourceSensor');
            if ([...select.options].some(o => o.value === id)) {
              select.value = id;
            }
          }

          let controlInfo = 'n/a';
          if (data.controlTemp) {
            controlInfo = `${data.controlTemp} C (${data.controlSensorName || 'Neznamy'} / 0x${data.controlSensorId})`;
          }
          const power = data.acPowerOn ? 'ON' : 'OFF';
          const mode = data.enabled ? 'AUTO' : 'MANUAL';
          document.getElementById('acStatus').textContent =
            `AC status: ${power}, rezim=${mode}, kontrolna teplota=${controlInfo}`;
        });
    }

    function saveAcConfig() {
      const payload = {
        enabled: document.getElementById('acEnabled').value === 'true',
        selectedSensorId: document.getElementById('acSourceSensor').value,
        targetTemp: Number(document.getElementById('acTarget').value),
        hysteresis: Number(document.getElementById('acHyst').value),
        irBits: Number(document.getElementById('irBits').value),
        irCodeOn: document.getElementById('irCodeOn').value.trim(),
        irCodeOff: document.getElementById('irCodeOff').value.trim(),
        irCodeCool: document.getElementById('irCodeCool').value.trim()
      };

      fetch('/api/ac/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload)
      })
      .then(response => response.json())
      .then(data => {
        if (data.success) {
          alert('AC konfiguracia ulozena.');
          loadAcStatus();
        } else {
          alert('Chyba konfiguracie: ' + (data.message || 'neznamy problem'));
        }
      });
    }

    function sendAcManual(action) {
      fetch('/api/ac/manual', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ action: action })
      })
      .then(response => response.json())
      .then(data => {
        if (data.success) {
          loadAcStatus();
        } else {
          alert('IR prikaz neodoslany: ' + (data.message || 'skus znova o par sekund'));
        }
      });
    }
    
    // Načítať dáta pri načítaní stránky
    loadSensors();
    loadSensorData();
    loadAcStatus();
    
    // Automaticky aktualizovať dáta každých 5 sekúnd
    setInterval(loadSensorData, 5000);
    setInterval(loadAcStatus, 5000);
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
      sensor["sensorId"] = pair.second.sensorId;
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
      
      if (!chipIdStr || strlen(chipIdStr) == 0) {
        request->send(200, "application/json", "{\"success\":false,\"message\":\"Chip ID je povinné\"}");
        return;
      }

      if (!name || strlen(name) == 0) {
        request->send(200, "application/json", "{\"success\":false,\"message\":\"Názov je povinný\"}");
        return;
      }
      
      char* endPtr;
      uint64_t chipId = strtoull(chipIdStr, &endPtr, 16);
      
      // Kontrola, či sa konverzia podarila (endPtr by mal ukazovať na koniec reťazca)
      if (*endPtr != '\0' || chipId == 0) {
        request->send(200, "application/json", "{\"success\":false,\"message\":\"Neplatné Chip ID - musí byť hex číslo\"}");
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
      
      if (!chipIdStr || strlen(chipIdStr) == 0) {
        request->send(200, "application/json", "{\"success\":false,\"message\":\"Chip ID je povinné\"}");
        return;
      }
      
      char* endPtr;
      uint64_t chipId = strtoull(chipIdStr, &endPtr, 16);
      
      if (*endPtr != '\0' || chipId == 0) {
        request->send(200, "application/json", "{\"success\":false,\"message\":\"Neplatné Chip ID\"}");
        return;
      }
      
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
      sensor["name"] = getSensorNameBySensorId(pair.first);
      sensor["temperature"] = String(pair.second.temperature, 2);
      sensor["humidity"] = String(pair.second.humidity, 2);
      sensor["lastSeen"] = String((now - pair.second.lastSeen) / 1000.0, 1);
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  server.on("/api/ac/status", HTTP_GET, [](AsyncWebServerRequest *request){
    JsonDocument doc;
    doc["enabled"] = climate.enabled;
    doc["targetTemp"] = climate.targetTemp;
    doc["hysteresis"] = climate.hysteresis;

    char selectedSensorIdStr[9];
    sprintf(selectedSensorIdStr, "%08X", climate.selectedSensorId);
    doc["selectedSensorId"] = selectedSensorIdStr;

    doc["acPowerOn"] = climate.acPowerOn;
    doc["irBits"] = climate.irBits;
    doc["irCodeOn"] = formatHex64(climate.irCodeOn);
    doc["irCodeOff"] = formatHex64(climate.irCodeOff);
    doc["irCodeCool"] = formatHex64(climate.irCodeCool);

    float controlTemp;
    uint32_t controlSensorId;
    String controlSensorName;
    if (getControlTemperature(&controlTemp, &controlSensorId, &controlSensorName)) {
      doc["controlTemp"] = String(controlTemp, 2);
      char controlIdStr[9];
      sprintf(controlIdStr, "%08X", controlSensorId);
      doc["controlSensorId"] = controlIdStr;
      doc["controlSensorName"] = controlSensorName;
    }

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  server.on("/api/ac/config", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, (const char*)data);
      if (err) {
        request->send(200, "application/json", "{\"success\":false,\"message\":\"Neplatny JSON\"}");
        return;
      }

      climate.enabled = doc["enabled"] | climate.enabled;
      climate.targetTemp = doc["targetTemp"] | climate.targetTemp;
      climate.hysteresis = doc["hysteresis"] | climate.hysteresis;
      climate.irBits = doc["irBits"] | climate.irBits;

      if (climate.hysteresis < 0.1f) climate.hysteresis = 0.1f;
      if (climate.hysteresis > 5.0f) climate.hysteresis = 5.0f;
      if (climate.targetTemp < 16.0f) climate.targetTemp = 16.0f;
      if (climate.targetTemp > 30.0f) climate.targetTemp = 30.0f;
      if (climate.irBits < 8 || climate.irBits > 64) climate.irBits = 32;

      const char* selectedSensorIdStr = doc["selectedSensorId"];
      if (selectedSensorIdStr) {
        String normalized;
        if (parseSensorIdHex(selectedSensorIdStr, normalized)) {
          climate.selectedSensorId = strtoul(normalized.c_str(), NULL, 16);
        } else {
          climate.selectedSensorId = 0;
        }
      }

      const char* onCode = doc["irCodeOn"];
      const char* offCode = doc["irCodeOff"];
      const char* coolCode = doc["irCodeCool"];
      if (onCode) climate.irCodeOn = parseHexToU64(String(onCode));
      if (offCode) climate.irCodeOff = parseHexToU64(String(offCode));
      if (coolCode) climate.irCodeCool = parseHexToU64(String(coolCode));

      request->send(200, "application/json", "{\"success\":true}");
    });

  server.on("/api/ac/manual", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, (const char*)data);
      if (err) {
        request->send(200, "application/json", "{\"success\":false,\"message\":\"Neplatny JSON\"}");
        return;
      }

      const char* action = doc["action"];
      if (!action) {
        request->send(200, "application/json", "{\"success\":false,\"message\":\"Chyba action\"}");
        return;
      }

      bool sent = false;
      String a = String(action);
      if (a.equalsIgnoreCase("on")) {
        sent = sendAcIrCommand(climate.irCodeOn, "MANUAL ON");
        if (sent) climate.acPowerOn = true;
      } else if (a.equalsIgnoreCase("off")) {
        sent = sendAcIrCommand(climate.irCodeOff, "MANUAL OFF");
        if (sent) climate.acPowerOn = false;
      } else if (a.equalsIgnoreCase("cool")) {
        sent = sendAcIrCommand(climate.irCodeCool, "MANUAL COOL");
      }

      if (sent) {
        request->send(200, "application/json", "{\"success\":true}");
      } else {
        request->send(200, "application/json", "{\"success\":false,\"message\":\"Prikaz neodoslany\"}");
      }
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
  runAutoClimateControl();
  printDatabase();
  BLEDevice::getScan()->clearResults();
  delay(1000);
}
