# Architektúra systému - Sensor-Gateway Šifrovanie

## Diagram komunikácie

```
┌─────────────────────────────────────────────────────────────────────┐
│                         SENZOR (ESP32-C3)                            │
├─────────────────────────────────────────────────────────────────────┤
│  1. Získanie Chip ID (eFuse MAC)                                    │
│     myChipId = ESP.getEfuseMac()  →  0x123456789ABCDEF0             │
│                                                                       │
│  2. Generovanie AES kľúča                                            │
│     SHA-256(Chip ID) → prvých 128 bitov = AES Key                   │
│     [DE AD BE EF CA FE BA BE ...]                                   │
│                                                                       │
│  3. Príprava dát                                                     │
│     ┌────────────────────────────┐                                  │
│     │ SensorData (8 bajtov)      │                                  │
│     ├────────────────────────────┤                                  │
│     │ sensorId:    0x12345678    │                                  │
│     │ temperature: 2500 (25.00°C)│                                  │
│     │ humidity:    4500 (45.00%) │                                  │
│     └────────────────────────────┘                                  │
│     (doplnené na 16 bajtov pre AES blok)                            │
│                                                                       │
│  4. Šifrovanie AES-128                                               │
│     plaintext → [XX XX XX XX XX XX XX XX XX XX XX XX XX XX XX XX]  │
│                                                                       │
│  5. BLE Advertising                                                  │
│     ┌────────────┬──────────────────────────────────────┐           │
│     │ Mfr ID (2B)│   Encrypted Data (16B)               │           │
│     ├────────────┼──────────────────────────────────────┤           │
│     │   0x1234   │  XX XX XX XX XX XX XX XX ...         │           │
│     └────────────┴──────────────────────────────────────┘           │
└─────────────────────────────────────────────────────────────────────┘
                              │
                              │ BLE Advertising
                              │ (wireless)
                              ↓
┌─────────────────────────────────────────────────────────────────────┐
│                        GATEWAY (ESP32-C6)                            │
├─────────────────────────────────────────────────────────────────────┤
│  1. BLE Skenovanie                                                   │
│     Príjem: [0x34 0x12][XX XX XX XX XX XX XX XX XX XX XX XX ...]   │
│                                                                       │
│  2. Kontrola Manufacturer ID                                         │
│     if (manufacturerId == 0x1234) → pokračuj                        │
│                                                                       │
│  3. Načítanie registrovaných senzorov z LittleFS                    │
│     ┌──────────────────────────────────────────────┐                │
│     │ sensors.json                                  │                │
│     ├──────────────────────────────────────────────┤                │
│     │ [{                                            │                │
│     │   "chipId": "123456789ABCDEF0",              │                │
│     │   "name": "Obývačka"                         │                │
│     │ }]                                            │                │
│     └──────────────────────────────────────────────┘                │
│                                                                       │
│  4. Pre každý registrovaný senzor:                                   │
│     a) Generovanie AES kľúča z Chip ID                              │
│        SHA-256(0x123456789ABCDEF0) → AES Key                        │
│                                                                       │
│     b) Pokus o dešifrovanie                                          │
│        encrypted → AES-128 Decrypt → plaintext                      │
│                                                                       │
│     c) Validácia dešifrovaných dát                                  │
│        ✓ Teplota: -40°C až 85°C                                     │
│        ✓ Vlhkosť: 0% až 100%                                        │
│                                                                       │
│  5. Ak validácia prejde:                                            │
│     ┌────────────────────────────┐                                  │
│     │ SensorData                 │                                  │
│     ├────────────────────────────┤                                  │
│     │ sensorId:    0x12345678    │ ← Úspešne dešifrované!          │
│     │ temperature: 25.00°C       │                                  │
│     │ humidity:    45.00%        │                                  │
│     └────────────────────────────┘                                  │
│                                                                       │
│  6. Uloženie do databázy senzorov                                   │
│     sensorDatabase[sensorId] = {temp, humidity, lastSeen}           │
│                                                                       │
│  7. Zobrazenie cez Web GUI                                          │
└─────────────────────────────────────────────────────────────────────┘
                              │
                              │ WiFi AP
                              │ (192.168.4.1)
                              ↓
┌─────────────────────────────────────────────────────────────────────┐
│                      WEB BROWSER (Používateľ)                        │
├─────────────────────────────────────────────────────────────────────┤
│                                                                       │
│  ┌──────────────────────────────────────────────────────────┐       │
│  │  Gateway - Registrácia senzorov                          │       │
│  ├──────────────────────────────────────────────────────────┤       │
│  │                                                            │       │
│  │  Registrovať nový senzor:                                │       │
│  │  Chip ID: [0x123456789ABCDEF0_______________]            │       │
│  │  Názov:   [Obývačka_________________________]            │       │
│  │           [Registrovať senzor]                           │       │
│  │                                                            │       │
│  │  Registrované senzory:                                    │       │
│  │  ┌───────────────────┬──────────┬──────────┐             │       │
│  │  │ Chip ID           │ Názov    │ Akcia    │             │       │
│  │  ├───────────────────┼──────────┼──────────┤             │       │
│  │  │ 123456789ABCDEF0  │ Obývačka │ [Odstrániť]            │       │
│  │  └───────────────────┴──────────┴──────────┘             │       │
│  │                                                            │       │
│  │  Prijaté dáta zo senzorov:                                │       │
│  │  ┌──────────┬──────────┬──────────┬──────────┐            │       │
│  │  │ Sensor ID│ Temp (°C)│ Vlhk (%) │ Aktualizácia         │       │
│  │  ├──────────┼──────────┼──────────┼──────────┤            │       │
│  │  │ 12345678 │  25.43   │  45.67   │ 2.1s     │            │       │
│  │  └──────────┴──────────┴──────────┴──────────┘            │       │
│  │                                                            │       │
│  └──────────────────────────────────────────────────────────┘       │
│                                                                       │
└─────────────────────────────────────────────────────────────────────┘
```

## Tok registrácie senzora

```
[Používateľ]                [Web GUI]           [Gateway]         [LittleFS]
     │                          │                    │                 │
     │ 1. Zadá Chip ID          │                    │                 │
     │ ─────────────────────────>                    │                 │
     │                          │                    │                 │
     │ 2. POST /api/register    │                    │                 │
     │ ─────────────────────────────────────────────>                 │
     │                          │                    │                 │
     │                          │  3. generateKeyFromChipId()          │
     │                          │    SHA-256(chipId) → AES Key         │
     │                          │                    │                 │
     │                          │  4. Uložiť do mapy │                 │
     │                          │    registeredSensors[chipId] = {...} │
     │                          │                    │                 │
     │                          │  5. saveSensorsToFile()              │
     │                          │    ──────────────────────────────────>
     │                          │                    │   Uložiť JSON   │
     │                          │                    │                 │
     │ 6. {"success": true}     │                    │                 │
     │ <─────────────────────────────────────────────                 │
     │                          │                    │                 │
     │ 7. Obnoviť zoznam        │                    │                 │
     │ ─────────────────────────>                    │                 │
     │                          │ GET /api/sensors   │                 │
     │                          │ ───────────────────>                 │
     │                          │                    │                 │
     │ 8. JSON so zoznamom      │                    │                 │
     │ <─────────────────────────────────────────────                 │
     │                          │                    │                 │
```

## Tok dešifrovania správy

```
[Senzor]              [BLE]              [Gateway]           [Registrovaný DB]
    │                   │                    │                       │
    │ 1. Šifrovaná      │                    │                       │
    │    správa         │                    │                       │
    │ ──────────────────>                    │                       │
    │                   │                    │                       │
    │                   │ 2. BLE Advertising │                       │
    │                   │ ───────────────────>                       │
    │                   │                    │                       │
    │                   │  3. onResult()     │                       │
    │                   │    callback        │                       │
    │                   │                    │                       │
    │                   │  4. Pre každý registrovaný senzor:         │
    │                   │    ───────────────────────────────────────>│
    │                   │    Získať AES kľúč │                       │
    │                   │    <───────────────────────────────────────│
    │                   │                    │                       │
    │                   │  5. Pokus o dešifrovanie                   │
    │                   │    AES-128 Decrypt │                       │
    │                   │                    │                       │
    │                   │  6. Validácia      │                       │
    │                   │    (teplota, vlhkosť)                      │
    │                   │                    │                       │
    │                   │  7. Ak OK:         │                       │
    │                   │    updateSensorData()                      │
    │                   │    Uložiť do sensorDatabase                │
    │                   │                    │                       │
```

## Bezpečnostná schéma

```
┌────────────────────────────────────────────────────────────┐
│                    Chip ID (64-bit)                         │
│                 0x123456789ABCDEF0                          │
└──────────────────────┬─────────────────────────────────────┘
                       │
                       │ SHA-256 Hash Function
                       ↓
┌────────────────────────────────────────────────────────────┐
│              SHA-256 Hash (256-bit)                         │
│  DE AD BE EF CA FE BA BE 01 23 45 67 89 AB CD EF ...       │
└──────────────────────┬─────────────────────────────────────┘
                       │
                       │ Prvých 128 bitov
                       ↓
┌────────────────────────────────────────────────────────────┐
│              AES-128 Key (128-bit)                          │
│        DE AD BE EF CA FE BA BE 01 23 45 67 89 AB CD EF     │
└──────────────────────┬─────────────────────────────────────┘
                       │
                       │ AES-128 ECB Mode
                       ↓
┌────────────────────────────────────────────────────────────┐
│                 Encrypted Data (128-bit)                    │
│        XX XX XX XX XX XX XX XX XX XX XX XX XX XX XX XX     │
└────────────────────────────────────────────────────────────┘
```

## Vlastnosti zabezpečenia

1. **Jedinečnosť**: Každý ESP32 má unikátne Chip ID z eFuse
2. **Reprodukovateľnosť**: Rovnaké Chip ID → rovnaký kľúč
3. **Jednosmernosť**: Z AES kľúča sa nedá vypočítať Chip ID
4. **Odolnosť**: SHA-256 je kryptograficky bezpečná hash funkcia
5. **Validácia**: Gateway kontroluje rozumnosť dešifrovaných hodnôt
6. **Autorizácia**: Len registrované senzory môžu komunikovať

## Úložné štruktúry

### LittleFS: /sensors.json
```json
[
  {
    "chipId": "123456789ABCDEF0",
    "name": "Obývačka"
  },
  {
    "chipId": "FEDCBA9876543210",
    "name": "Kúpeľňa"
  }
]
```

### RAM: registeredSensors (map)
```cpp
std::map<uint64_t, RegisteredSensor> registeredSensors;

// Kľúč: chipId (0x123456789ABCDEF0)
// Hodnota: {
//   chipId: 0x123456789ABCDEF0,
//   aesKey: [DE AD BE EF ...],
//   name: "Obývačka"
// }
```

### RAM: sensorDatabase (map)
```cpp
std::map<uint32_t, SensorRecord> sensorDatabase;

// Kľúč: sensorId (0x12345678)
// Hodnota: {
//   temperature: 25.43,
//   humidity: 45.67,
//   lastSeen: 123456 (millis)
// }
```
