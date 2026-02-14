# Zabezpečený senzorový systém Gateway-Sensor

Tento projekt implementuje zabezpečenú komunikáciu medzi senzormi a gateway pomocou šifrovania založeného na jedinečnom Chip ID každého ESP32 zariadenia.

## Prehľad systému

### Bezpečnostná architektúra

1. **Generovanie kľúča z Chip ID**: Každý ESP32 má jedinečné 64-bitové Chip ID (eFuse MAC). Toto ID sa používa na generovanie 128-bitového AES šifrovacieho kľúča pomocou SHA-256 hash funkcie.

2. **AES-128 šifrovanie**: Všetky dáta prenášané cez BLE sú šifrované pomocou AES-128 v ECB móde. Každý senzor má svoj vlastný kľúč odvodený z jeho Chip ID.

3. **Registračný systém**: Gateway má webové rozhranie, kde môžete registrovať senzory zadaním ich Chip ID. Gateway potom automaticky vygeneruje rovnaký šifrovací kľúč a môže dešifrovať správy od tohto senzora.

### Komponenty

#### Senzor (ESP32-C3 SuperMini)
- Používa BLE advertising na posielanie šifrovaných dát
- Získava svoje jedinečné Chip ID pri štarte
- Generuje AES kľúč z Chip ID pomocou SHA-256
- Posiela šifrované údaje (teplota, vlhkosť, sensor ID)

#### Gateway (ESP32-C6 DevKit)
- Skenuje BLE reklamy od senzorov
- Poskytuje WiFi Access Point pre konfiguráciu
- Webové GUI na registráciu senzorov (http://192.168.4.1)
- Ukladá registrované senzory do LittleFS
- Automaticky dešifruje správy od registrovaných senzorov

## Ako používať

### 1. Nahratie kódu

**Senzor:**
```bash
cd sensor
platformio run -t upload
platformio device monitor
```

Zo sériového výstupu si poznačte **Chip ID** (napr. `0x123456789ABCDEF0`)

**Gateway:**
```bash
cd gateway
platformio run -t upload
platformio device monitor
```

### 2. Registrácia senzora

1. Pripojte sa k WiFi sieti `Gateway_Config` (heslo: `GatewaySecure2024!`)
2. Otvorte prehliadač a prejdite na `http://192.168.4.1`
3. V poli "Chip ID" zadajte Chip ID vášho senzora (napr. `123456789ABCDEF0` alebo `0x123456789ABCDEF0`)
4. Zadajte názov senzora (napr. "Obývačka")
5. Kliknite na "Registrovať senzor"

**BEZPEČNOSŤ:** Pre produkčné nasadenie zmeňte WiFi heslo v súbore `gateway/src/main.cpp` (riadok 56).

### 3. Sledovanie dát

Web rozhranie automaticky zobrazuje:
- Zoznam všetkých registrovaných senzorov
- Prijaté dáta zo senzorov (teplota, vlhkosť, čas poslednej aktualizácie)
- Dáta sa automaticky aktualizujú každých 5 sekúnd

## Bezpečnostné vlastnosti

### Generovanie kľúča

Kľúč je generovaný pomocou:
```cpp
SHA-256(Chip_ID) → 256-bitový hash → prvých 128 bitov = AES kľúč
```

Tento prístup zabezpečuje:
- **Jedinečnosť**: Každý ESP32 má unikátne Chip ID
- **Reprodukovateľnosť**: Rovnaké Chip ID vždy vytvorí rovnaký kľúč
- **Kryptografická sila**: SHA-256 poskytuje silnú odvodenú funkciu kľúča

### Validácia dát

Gateway validuje dešifrované dáta kontrolou:
- Rozumné hodnoty teploty (-40°C až 85°C)
- Rozumné hodnoty vlhkosti (0% až 100%)

Ak dáta neprejdú validáciou, sú označené ako neregistrovaný senzor.

## API Endpointy

### GET /
- Hlavná webová stránka s GUI

### GET /api/sensors
- Vráti JSON zoznam všetkých registrovaných senzorov

### POST /api/register
- Body: `{"chipId": "123456789ABCDEF0", "name": "Názov"}`
- Registruje nový senzor

### POST /api/unregister
- Body: `{"chipId": "123456789ABCDEF0"}`
- Odregistruje senzor

### GET /api/data
- Vráti JSON s aktuálnymi dátami zo všetkých senzorov

## Technické detaily

### Dátová štruktúra
```cpp
struct SensorData {
  uint32_t sensorId;    // 4 bajty - ID senzora
  uint16_t temperature; // 2 bajty - teplota * 100
  uint16_t humidity;    // 2 bajty - vlhkosť * 100
};
// Celkovo 8 bajtov, doplnené na 16 bajtov pre AES blok
```

### BLE Advertising formát
```
[2B Manufacturer ID][16B AES šifrované dáta]
```

### Úložisko
- Gateway používa LittleFS na perzistentné ukladanie registrovaných senzorov
- Súbor: `/sensors.json`
- Formát: JSON pole objektov s chipId a názvom

## Možné vylepšenia

1. **Autentifikácia správ**: Pridať HMAC pre overenie integrity
2. **Časové pečiatky**: Pridať timestamp pre ochranu proti replay útokom
3. **Rotácia kľúčov**: Implementovať periodickú rotáciu šifrovacích kľúčov
4. **TLS**: Zabezpečiť webové rozhranie pomocou HTTPS
5. **Autentifikácia používateľov**: Pridať prihlasovanie do web GUI
6. **Konfigurovateľné heslo**: Umožniť nastavenie WiFi hesla cez webové rozhranie

## Bezpečnostné poznámky

### Pre produkčné nasadenie:

1. **Zmeňte WiFi heslo** v `gateway/src/main.cpp` (riadok 56)
2. **Vypnite debug výpis kľúčov** nastavením `DEBUG_PRINT_KEYS 0` v oboch súboroch
3. **Zabezpečte sériový port** - nepovoľte neoprávnený prístup
4. **Pravidelne aktualizujte** firmware pre opravu bezpečnostných chýb

## Licencia

Projekt pre študijné účely.
