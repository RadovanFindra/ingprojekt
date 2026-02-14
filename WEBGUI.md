# Web GUI Náhľad

Tento súbor obsahuje náhľad webového rozhrania gateway.

## Hlavná stránka

Po pripojení na `http://192.168.4.1` sa zobrazí:

```
╔════════════════════════════════════════════════════════════════════════╗
║  🌡️ Gateway - Registrácia senzorov                                    ║
╠════════════════════════════════════════════════════════════════════════╣
║                                                                        ║
║  ℹ️ Inštrukcie: Zadajte Chip ID senzora (64-bit hex hodnota, napr.   ║
║     0x123456789ABCDEF0) a meno pre identifikáciu. Systém automaticky  ║
║     vygeneruje šifrovací kľúč pomocou SHA-256.                        ║
║                                                                        ║
║  ┌──────────────────────────────────────────────────────────────────┐ ║
║  │ Registrovať nový senzor                                          │ ║
║  ├──────────────────────────────────────────────────────────────────┤ ║
║  │                                                                  │ ║
║  │ Chip ID (hex):                                                   │ ║
║  │ [0x123456789ABCDEF0 alebo 123456789ABCDEF0________________]     │ ║
║  │                                                                  │ ║
║  │ Názov senzora:                                                   │ ║
║  │ [Napr. Obývačka, Kúpeľňa_____________________________]          │ ║
║  │                                                                  │ ║
║  │ [ Registrovať senzor ]                                           │ ║
║  └──────────────────────────────────────────────────────────────────┘ ║
║                                                                        ║
║  ┌──────────────────────────────────────────────────────────────────┐ ║
║  │ Registrované senzory                                             │ ║
║  ├────────────────────┬────────────────┬──────────────────────────┤ ║
║  │ Chip ID            │ Názov          │ Akcia                    │ ║
║  ├────────────────────┼────────────────┼──────────────────────────┤ ║
║  │ 123456789ABCDEF0   │ Obývačka       │ [ Odstrániť ]            │ ║
║  │ FEDCBA9876543210   │ Kúpeľňa        │ [ Odstrániť ]            │ ║
║  │ AABBCCDDEEFF0011   │ Garáž          │ [ Odstrániť ]            │ ║
║  └────────────────────┴────────────────┴──────────────────────────┘ ║
║                                                                        ║
║  ┌──────────────────────────────────────────────────────────────────┐ ║
║  │ Prijaté dáta zo senzorov                                         │ ║
║  ├──────────┬──────────────┬──────────────┬────────────────────────┤ ║
║  │ Sensor ID│ Teplota (°C) │ Vlhkosť (%)  │ Posledná aktualizácia │ ║
║  ├──────────┼──────────────┼──────────────┼────────────────────────┤ ║
║  │ 12345678 │ 25.43        │ 45.67        │ 2.1s                   │ ║
║  │ ABCDEF01 │ 22.18        │ 52.34        │ 1.3s                   │ ║
║  │ 9876FEDC │ 28.92        │ 38.21        │ 4.7s                   │ ║
║  └──────────┴──────────────┴──────────────┴────────────────────────┘ ║
║                                                                        ║
║  Dáta sa automaticky aktualizujú každých 5 sekúnd                    ║
║                                                                        ║
╚════════════════════════════════════════════════════════════════════════╝
```

## Vzorový workflow

### 1. Prvé spustenie (žiadne senzory)

```
╔════════════════════════════════════════════════════════════════════════╗
║  Registrované senzory                                                  ║
╠════════════════════════════════════════════════════════════════════════╣
║  (žiadne registrované senzory)                                         ║
╚════════════════════════════════════════════════════════════════════════╝

╔════════════════════════════════════════════════════════════════════════╗
║  Prijaté dáta zo senzorov                                              ║
╠════════════════════════════════════════════════════════════════════════╣
║  (žiadne dáta)                                                         ║
╚════════════════════════════════════════════════════════════════════════╝
```

### 2. Registrácia senzora

Používateľ zadá:
- Chip ID: `123456789ABCDEF0`
- Názov: `Obývačka`

Klikne na "Registrovať senzor"

### 3. Úspešná registrácia

```
╔════════════════════════════════════════════════════════════════════════╗
║  ✅ Senzor úspešne registrovaný!                                       ║
╚════════════════════════════════════════════════════════════════════════╝
```

Tabuľka sa aktualizuje:

```
╔════════════════════════════════════════════════════════════════════════╗
║  Registrované senzory                                                  ║
╠────────────────────┬────────────────┬──────────────────────────────────╣
║ Chip ID            │ Názov          │ Akcia                            ║
╠────────────────────┼────────────────┼──────────────────────────────────╣
║ 123456789ABCDEF0   │ Obývačka       │ [ Odstrániť ]                    ║
╚════════════════════╧════════════════╧══════════════════════════════════╝
```

### 4. Príjem dát zo senzora

Po pár sekundách (keď senzor začne vysielať):

```
╔════════════════════════════════════════════════════════════════════════╗
║  Prijaté dáta zo senzorov                                              ║
╠══════════╦══════════════╦══════════════╦═══════════════════════════════╣
║ Sensor ID║ Teplota (°C) ║ Vlhkosť (%)  ║ Posledná aktualizácia         ║
╠══════════╬══════════════╬══════════════╬═══════════════════════════════╣
║ 12345678 ║ 25.43        ║ 45.67        ║ 2.1s                          ║
╚══════════╩══════════════╩══════════════╩═══════════════════════════════╝
```

Hodnoty sa aktualizujú v reálnom čase každých 5 sekúnd.

### 5. Chybová validácia

Pri zadaní neplatného Chip ID (napr. "xyz"):

```
╔════════════════════════════════════════════════════════════════════════╗
║  ❌ Chyba: Neplatné Chip ID - musí byť hex číslo                       ║
╚════════════════════════════════════════════════════════════════════════╝
```

Pri prázdnych poliach:

```
╔════════════════════════════════════════════════════════════════════════╗
║  ❌ Prosím vyplňte všetky polia                                        ║
╚════════════════════════════════════════════════════════════════════════╝
```

### 6. Odregistrácia senzora

Kliknutím na "Odstrániť":

```
╔════════════════════════════════════════════════════════════════════════╗
║  ⚠️ Naozaj chcete odstrániť tento senzor?                              ║
║                                                                        ║
║  [ Zrušiť ]  [ OK ]                                                    ║
╚════════════════════════════════════════════════════════════════════════╝
```

Po potvrdení:

```
╔════════════════════════════════════════════════════════════════════════╗
║  ✅ Senzor odstránený!                                                 ║
╚════════════════════════════════════════════════════════════════════════╝
```

## Responzívny dizajn

GUI je responzívne a funguje na:
- 💻 Desktop prehliadačoch (Chrome, Firefox, Safari, Edge)
- 📱 Mobilných telefónoch (Android, iOS)
- 📲 Tabletoch

## Farebná schéma

- **Pozadie**: Svetlo sivá (#f0f0f0)
- **Karty**: Biela (#ffffff)
- **Primárna farba**: Zelená (#4CAF50) - tlačidlá, hlavičky tabuliek
- **Sekundárna farba**: Červená (#f44336) - tlačidlá na odstránenie
- **Info box**: Svetlo modrá (#e7f3fe)
- **Text**: Tmavo sivá (#333333)
- **Ohraničenie**: Svetlá sivá (#dddddd)

## JavaScript funkcionality

- ✅ Automatická aktualizácia dát (každých 5 sekúnd)
- ✅ Real-time validácia vstupov
- ✅ AJAX požiadavky bez obnovenia stránky
- ✅ Responzívne chybové hlásenia
- ✅ Potvrdzovacie dialógy pre kritické akcie

## API volania

### Načítanie registrovaných senzorov
```
GET /api/sensors
→ [{chipId: "123456789ABCDEF0", name: "Obývačka"}, ...]
```

### Registrácia senzora
```
POST /api/register
Body: {chipId: "123456789ABCDEF0", name: "Obývačka"}
→ {success: true} | {success: false, message: "..."}
```

### Odregistrácia senzora
```
POST /api/unregister
Body: {chipId: "123456789ABCDEF0"}
→ {success: true}
```

### Načítanie dát zo senzorov
```
GET /api/data
→ [{id: "12345678", temperature: "25.43", humidity: "45.67", lastSeen: "2.1"}, ...]
```

## Prístup

**URL:** http://192.168.4.1  
**WiFi SSID:** Gateway_Config  
**WiFi Heslo:** GatewaySecure2024!  

(Heslo zmeňte v produkčnom nasadení!)
