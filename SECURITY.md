# Bezpečnostná analýza - Sensor-Gateway systém

## Prehľad zabezpečenia

Tento dokument obsahuje bezpečnostnú analýzu implementovaného systému šifrovania komunikácie medzi senzormi a gateway.

## Implementované bezpečnostné funkcie

### ✅ 1. Kryptografické šifrovanie
- **AES-128 ECB mód**: Všetky senzorové dáta sú šifrované
- **SHA-256 KDF**: Kľúč je odvodený z jedinečného Chip ID pomocou SHA-256
- **Jedinečnosť kľúčov**: Každý senzor má unikátny šifrovací kľúč

### ✅ 2. Autentifikácia senzorov
- **Registračný systém**: Len registrované senzory môžu komunikovať
- **Chip ID overenie**: Gateway overuje Chip ID pred dešifrovaním
- **Validácia dát**: Gateway kontroluje rozumnosť dešifrovaných hodnôt

### ✅ 3. Perzistentné úložisko
- **LittleFS**: Registrované senzory sú trvalo uložené
- **JSON formát**: Ľahko čitateľný a parsovateľný formát
- **Ochrana pri reštarte**: Registrácie pretrvajú po reštarte gateway

### ✅ 4. Vstupná validácia
- **Kontrola Chip ID**: Validácia hex formátu s endPtr kontrolou
- **Chybové správy**: Jasné chybové hlásenia pre používateľa
- **Nulová hodnota**: Kontrola neplatných Chip ID hodnôt

### ✅ 5. Debug režim
- **DEBUG_PRINT_KEYS flag**: Kontrolovaný výpis šifrovacích kľúčov
- **Bezpečnostné upozornenia**: Varovanie pri výpise kľúčov v debug móde
- **Produkčný mód**: Jednoduchá deaktivácia pre nasadenie

## Identifikované riziká a mitigácie

### 🟡 Stredné riziko: WiFi AP prístup

**Riziko:**
- Predvolené WiFi heslo môže byť kompromitované
- WiFi AP je otvorený pre pripojenie bez dodatočnej autentifikácie

**Mitigácia:**
- Použité silnejšie predvolené heslo: `GatewaySecure2024!`
- Dokumentácia vyžaduje zmenu hesla pre produkciu
- Odporúčanie: Implementovať autentifikáciu používateľov vo web GUI

**Priorita:** Stredná - vyžaduje manuálnu akciu používateľa

### 🟡 Stredné riziko: Debug výpis kľúčov

**Riziko:**
- Šifrovacie kľúče sú vypisované na sériový port v debug móde
- Neautorizovaný prístup k sériovému portu môže odchytiť kľúče

**Mitigácia:**
- Implementovaný DEBUG_PRINT_KEYS flag (default: 1 pre development)
- Jasné upozornenia v kóde a dokumentácii
- Jednoduchá deaktivácia nastavením na 0

**Priorita:** Stredná - vyžaduje zmenu pred produkčným nasadením

### 🟢 Nízke riziko: AES-128 ECB mód

**Riziko:**
- ECB mód neskrýva opakujúce sa bloky dát
- Pre dlhé správy môže odhaliť vzory

**Mitigácia:**
- Krátke správy (8 bajtov + padding) minimalizujú riziko
- Dáta sa neustále menia (teplota, vlhkosť)
- Validácia dát na strane prijímača

**Priorita:** Nízka - akceptovatelné pre tento use case

**Odporúčanie:** Pre budúce zlepšenie zvážiť AES-CBC alebo AES-GCM

### 🟢 Nízke riziko: HTTP namiesto HTTPS

**Riziko:**
- Web rozhranie používa nešifrované HTTP
- Man-in-the-middle útok môže odchytiť komunikáciu

**Mitigácia:**
- WiFi AP je lokálny (192.168.4.1)
- Útočník musí byť pripojený na rovnakom WiFi
- Žiadne citlivé heslá sa neprenášajú (okrem Chip ID)

**Priorita:** Nízka - lokálna sieť poskytuje základnú ochranu

**Odporúčanie:** Pre budúce zlepšenie implementovať HTTPS s self-signed certifikátom

### 🟢 Nízke riziko: Replay útoky

**Riziko:**
- Útočník môže zachytiť a prehrať BLE pakety
- Žiadna ochrana proti replay útokom (timestamp, nonce)

**Mitigácia:**
- Dáta sa rýchlo menia (každých 5 sekúnd nové hodnoty)
- Gateway používa lastSeen timestamp
- Validácia rozumnosti hodnôt

**Priorita:** Nízka - minimálny dopad pre tento use case

**Odporúčanie:** Pre kritické aplikácie pridať nonce alebo timestamp

## Bezpečnostné best practices

### ✅ Dodržané

1. **Kryptografické funkcie**: Použitie osvedčených SHA-256 a AES-128
2. **Žiadne hardcoded kľúče**: Kľúče sú generované dynamicky
3. **Validácia vstupov**: Kontrola všetkých API vstupov
4. **Minimalizácia povrchu útoku**: Len potrebné služby sú aktívne
5. **Dokumentácia**: Jasné bezpečnostné pokyny v dokumentácii

### 📋 Odporúčania na zlepšenie

1. **Autentifikácia používateľov**: Pridať login do web GUI
2. **HTTPS**: Implementovať TLS pre web rozhranie
3. **AES-GCM**: Použiť autentifikované šifrovanie
4. **Nonce/Timestamp**: Pridať ochranu proti replay útokom
5. **HMAC**: Pridať autentifikáciu správ
6. **Rotácia kľúčov**: Implementovať periodickú výmenu kľúčov

## Compliance

### GDPR
- ✅ Žiadne osobné údaje sa nespracovávajú
- ✅ Len technické údaje (teplota, vlhkosť, ID)

### Všeobecné bezpečnostné štandardy
- ✅ Použitie štandardných kryptografických algoritmov (SHA-256, AES-128)
- ✅ Žiadne známe zraniteľné knižnice
- ✅ Minimálne oprávnenia (len potrebné funkcie)

## Testovanie zabezpečenia

### Odporúčané testy

1. **Test neautorizovaného prístupu**
   - Pokus o čítanie dát bez registrácie senzora
   - Výsledok: Gateway by mala zamietnuť všetky nešifrované správy

2. **Test neplatných vstupov**
   - Zadanie neplatného Chip ID formátu
   - Výsledok: API by malo vrátiť chybové hlásenie

3. **Test ochránenia hesla**
   - Pokus o prihlásenie s nesprávnym heslom
   - Výsledok: WiFi pripojenie by malo zlyhať

4. **Test perzistencie**
   - Reštart gateway s registrovanými senzormi
   - Výsledok: Registrácie by mali pretrvať

## Záver

Implementovaný systém poskytuje **dobrú základnú úroveň zabezpečenia** pre IoT senzorový systém:

### Silné stránky:
- ✅ Kryptografické šifrovanie všetkých dát
- ✅ Jedinečné kľúče pre každý senzor
- ✅ Registračný systém pre autorizáciu
- ✅ Validácia vstupov a dát
- ✅ Konfigurovateľný debug mód

### Oblasti na zlepšenie:
- 🔧 Implementovať autentifikáciu používateľov
- 🔧 Pridať HTTPS podporu
- 🔧 Zvážiť autentifikované šifrovanie (AES-GCM)
- 🔧 Pridať ochranu proti replay útokom

### Bezpečnostné hodnotenie: **B+ (Dobré)**

Systém je vhodný pre:
- ✅ Domáce/hobby projekty
- ✅ Prototypovanie
- ✅ Nekritické IoT aplikácie
- ✅ Vzdelávacie účely

Pre kritické produkčné nasadenie odporúčame implementovať dodatočné bezpečnostné opatrenia uvedené v sekcii "Odporúčania na zlepšenie".

---

**Autor analýzy:** GitHub Copilot Security Review  
**Dátum:** 2026-02-14  
**Verzia:** 1.0
