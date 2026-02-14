# Testovací návod pre Sensor-Gateway systém

## Predpoklady
- PlatformIO nainštalované (Visual Studio Code + PlatformIO extension alebo PlatformIO CLI)
- ESP32-C3 SuperMini (pre senzor)
- ESP32-C6 DevKit (pre gateway)
- USB káble pre oba zariadenia

## Testovací scenár

### Krok 1: Kompilácia a nahratie kódu senzora

```bash
cd sensor
platformio run -t upload
platformio device monitor
```

**Očakávaný výstup:**
```
Startujem Senzorovy Uzol s ID...
Unikatne ID Senzora: 0xXXXXXXXX (Plna MAC: XX:XX:XX:XX:XX:XX)
Chip ID: 0x123456789ABCDEF0
Vygenerovany AES kluc z Chip ID: XX XX XX XX XX XX XX XX XX XX XX XX XX XX XX XX
Zacinam vysielat (Advertising)...
```

**Poznačte si Chip ID** - budete ho potrebovať na registráciu!

### Krok 2: Kompilácia a nahratie kódu gateway

```bash
cd gateway
platformio run -t upload
platformio device monitor
```

**Očakávaný výstup:**
```
Startujem Gateway...
LittleFS namontovany
AP IP adresa: 192.168.4.1
Web server spusteny
Zacinam skenovat...
```

### Krok 3: Pripojenie k Gateway WiFi

1. Na počítači alebo telefóne nájdite WiFi sieť: **Gateway_Config**
2. Heslo: **GatewaySecure2024!**
3. Pripojte sa k tejto sieti

**BEZPEČNOSŤ:** Pre produkčné nasadenie zmeňte heslo v `gateway/src/main.cpp`.

### Krok 4: Otvorenie webového rozhrania

1. Otvorte prehliadač
2. Navigujte na: **http://192.168.4.1**
3. Mala by sa zobraziť stránka "Gateway - Registrácia senzorov"

### Krok 5: Registrácia senzora

1. Do poľa "Chip ID" zadajte Chip ID vášho senzora (z Kroku 1)
   - Môžete zadať s alebo bez 0x prefixu
   - Príklad: `123456789ABCDEF0` alebo `0x123456789ABCDEF0`
2. Do poľa "Názov senzora" zadajte ľubovoľný názov (napr. "Test Senzor")
3. Kliknite na "Registrovať senzor"

**Očakávaný výsledok:**
- Zobrazí sa hlásenie "Senzor úspešne registrovaný!"
- V sekcii "Registrované senzory" by sa mal objaviť nový senzor

### Krok 6: Overenie prijímania dát

Po registrácii by ste mali vidieť:

1. **V sériovom monitore gateway:**
```
Prijaty sifrovaný blok: XX XX XX XX XX XX XX XX XX XX XX XX XX XX XX XX
Uspesne desifrovane pomocou ChipID=0x123456789ABCDEF0 (Name: Test Senzor)
[NOVÝ SENZOR] ID: 0xXXXXXXXX
```

2. **Vo webovom rozhraní (sekcia "Prijaté dáta zo senzorov"):**
   - Sensor ID: 0xXXXXXXXX
   - Teplota: ~25.XX °C
   - Vlhkosť: ~45.XX %
   - Posledná aktualizácia: < 5 sekúnd

### Krok 7: Test bez registrácie

Pre overenie, že systém skutočne funguje so šifrovaním:

1. Reštartujte gateway (stlačte RESET tlačidlo)
2. Gateway začne skenovať
3. **Bez registrácie** by ste mali vidieť v sériovom monitore:
```
Prijaty sifrovaný blok: XX XX XX XX XX XX XX XX XX XX XX XX XX XX XX XX
Ziadny kluc nepasuje - neregistrovany senzor alebo chybne data
```

4. Zaregistrujte senzor znova cez webové rozhranie
5. Gateway by teraz mala úspešne dešifrovať správy

## Automatické testovanie

### Test 1: Validácia Chip ID

```cpp
// Testovať, či sa Chip ID správne získava
assert(ESP.getEfuseMac() != 0);
```

### Test 2: Generovanie kľúča

```cpp
// Testovať, či SHA-256 generuje konzistentný kľúč
uint8_t key1[16], key2[16];
uint64_t testChipId = 0x123456789ABCDEF0;
generateKeyFromChipId(testChipId, key1);
generateKeyFromChipId(testChipId, key2);
assert(memcmp(key1, key2, 16) == 0);
```

### Test 3: Šifrovanie/Dešifrovanie

```cpp
// Testovať, či sa dáta správne šifrujú a dešifrujú
SensorData original, decrypted;
original.sensorId = 0x12345678;
original.temperature = 2500; // 25.00°C
original.humidity = 4500; // 45.00%

uint8_t encrypted[16];
// Šifrovanie...
// Dešifrovanie...
assert(memcmp(&original, &decrypted, sizeof(SensorData)) == 0);
```

## Možné problémy a riešenia

### 1. Gateway nenachádza senzor
- **Riešenie**: Skontrolujte, či je senzor zapnutý a vysiela
- Pozrite sa na sériový monitor senzora - mal by vypisovať "Odosielam šifrované dáta..."

### 2. "Ziadny kluc nepasuje"
- **Riešenie**: Uistite sa, že ste senzor zaregistrovali v gateway
- Skontrolujte, či ste zadali správne Chip ID

### 3. Nemôžem sa pripojiť na WiFi
- **Riešenie**: Reštartujte gateway
- Skontrolujte, či sa WiFi AP správne naštartoval (IP: 192.168.4.1 by malo byť vo výpise)

### 4. Webová stránka sa nenačíta
- **Riešenie**: Skontrolujte, či ste pripojení na Gateway_Config WiFi
- Skúste odpojiť a znova pripojiť
- Skúste http://192.168.4.1 (nie https)

### 5. Kompilačné chyby
- **Riešenie**: Uistite sa, že máte nainštalované všetky závislosti
- Pre gateway: ArduinoJson, ESPAsyncWebServer, AsyncTCP
- Skúste vymazať `.pio` priečinok a znova skompilovať

## Bezpečnostné testy

### Test 1: Unikátnosť kľúčov
Otestujte dva rôzne senzory - mali by mať rôzne Chip ID a teda rôzne kľúče.

### Test 2: Nemožnosť čítania bez registrácie
Overujte, že gateway nedokáže čítať dáta zo senzora, ktorý nie je zaregistrovaný.

### Test 3: Perzistencia registrácie
Reštartujte gateway - registrované senzory by mali zostať v pamäti (LittleFS).

## Výsledky testovania

Po úspešnom absolvovaní všetkých testov by ste mali mať:

✅ Senzor vysiela šifrované BLE správy pomocou kľúča odvodeného z Chip ID  
✅ Gateway poskytuje webové GUI na 192.168.4.1  
✅ Senzory sa dajú registrovať pomocou Chip ID  
✅ Gateway dešifruje len správy od registrovaných senzorov  
✅ Dáta sa zobrazujú vo webovom rozhraní  
✅ Registrácie pretrvajú po reštarte gateway  
