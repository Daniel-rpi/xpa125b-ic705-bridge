# IC-705 ↔ Xiegu XPA125B via TTGO Bluetooth-Bridge

Ein LilyGO **TTGO T-Display v1.1** (ESP32) verbindet sich per **Bluetooth
Classic (CI-V-over-SPP)** mit einem Icom **IC-705** und steuert darüber eine
nachgeschaltete Xiegu **XPA125B**-Endstufe (100 W HF-Verstärker) vollautomatisch:
Bandumschaltung, PTT und – seit Kurzem – eine softwareseitige ALC-basierte
Schutzschaltung, die eingreift, bevor die PA in ihre eigene harte
Fehlerabschaltung läuft.

Kein Kabelsalat zwischen 705 und PA, kein manuelles Bandwechseln an der PA, und
eine grobe, aber wirksame Warnstufe vor Übersteuerung – bei einem Funkgerät, das
selbst gar keine passende ACC-Buchse für so eine PA hat.

*(English: An ESP32 board bridges an Icom IC-705 (Bluetooth CI-V) to a Xiegu
XPA125B HF amplifier — automatic band switching, PTT, and a software ALC-based
overdrive protection layer. See [docs/alc-protection.md](docs/alc-protection.md)
for a full English write-up of the protection logic.)*

## ⚠️ Sicherheitshinweise (bitte vor dem Nachbau lesen)

- **≥ 3,2 V am PTT-Eingang der XPA125B zerstört deren internen Prozessor
  dauerhaft** (laut offiziellem Handbuch). Der PTT-Ausgang des TTGO läuft daher
  ausschließlich über eine galvanische Trennung (Optokoppler bzw. MOSFET-gegen-GND,
  siehe unten) – niemals GPIO26 direkt an die PA anschließen.
- Diese Firmware sendet zur reinen Anzeige (Frequenz/Modus/TX-Status/Uhrzeit)
  **ausschließlich CI-V-Lesebefehle**. Echte Sende-Kommandos (Leistung setzen,
  Modus setzen, PTT) gibt es nur in der Tune-Sequenz (Taster) und der
  ALC-Schutzfunktion – beide sind im Quellcode ausführlich kommentiert.
- Alle hier beschriebenen Werte (Bandspannungen, PTT-Pegel, ALC-Schwellen) sind
  **an einem konkreten IC-705 und einer konkreten XPA125B gemessen**. Vor dem
  Anschließen an eigene Hardware selbst nachmessen, nicht blind übernehmen.
- Kein Ersatz für die Schutzabschaltung der PA selbst – nur eine zusätzliche,
  frühere Warnstufe.
- Nachbau/Nutzung auf eigene Verantwortung.

## Basiert auf / Danke an

Dieses Projekt wäre ohne die Vorarbeit von **DL1BZ** nicht in dieser Form
entstanden:

- [dl1bz/ESP32-IC705BT-PA](https://github.com/dl1bz/ESP32-IC705BT-PA) – das
  ursprüngliche Konzept (ESP32 + Bluetooth-CI-V + Optokoppler-PTT + PWM/RC-Band-
  spannung für eine XPA125B), inklusive Forks von
  [PE1OFO](https://github.com/PE1OFO/IC-705-BlueTooth-Controller) und
  [DK8HC](https://github.com/dk8hc/IC-705-BlueTooth-Controller).

**Wichtig, damit klar ist, was wovon abstammt:** Die hier veröffentlichte
Firmware ist ein **komplett eigenständig geschriebener Sketch** (kein Codeauszug
aus den obigen Repos, keine `CIVmasterLib`-Abhängigkeit) – die Übernahme betrifft
das **Konzept und einzelne kalibrierte Werte**: die Verdrahtung
GPIO26→PTT/GPIO27→Bandspannung, die Bandspannungstabelle aus dem Xiegu-Handbuch
und die daraus abgeleitete Lastkorrektur (`corrMv` in `firmware/bands.h`) sind
1:1 aus `z_userprog.ino` von DL1BZ übernommen. Alles Weitere (TFT-Anzeige,
Uhrzeit/UTC, Tune-Sequenz mit FB/NG-Bestätigung, ALC-Auslesung samt
Schutzschaltung, das komplette PMR-171-BLE-Modul, die Daughterboard-Platine) ist
eigene Arbeit dieses Projekts.

## Hardware

### Übersicht

```
IC-705 ──(Bluetooth Classic, CI-V-over-SPP)──> TTGO T-Display v1.1 (ESP32)
                                                     │  GPIO26 (PTT, über Opto/MOSFET gegen GND)
                                                     │  GPIO27 (PWM → RC 470Ω/22µF → Bandspannung)
                                                     │  GPIO36 (ADC, über 220Ω ← ALC-Ausgang, Pin 4)
                                                     ▼
                                        Xiegu XPA125B – ACC-Buchse (6-pol. Mini-DIN)
                                        Pin 1 = +12…14,5V (Versorgung der Platine)
                                        Pin 2 = PTT (≥3,2V = Prozessorschaden!)
                                        Pin 3 = Bandspannung
                                        Pin 4 = ALC-Ausgang (~1–4V, invertiert)
                                        Pin 6 = GND
```

Das TTGO bezieht seine eigene 3,3-V-Versorgung direkt aus der ACC-Pin-1-Spannung
der PA (isolierter DC/DC-Wandler bzw. Buck-Regler auf dem Daughterboard, siehe
`hardware/`) – **kein separates USB-Netzteil nötig, aber USB und PA-Strom dürfen
nicht gleichzeitig anliegen** (kein Akku auf der Platine, zwei Spannungsquellen
würden sich gegenseitig stören).

### Eigene SMD-Platine (Daughterboard)

Statt der Verkabelung frei fliegend aufzubauen: ein fertig geroutetes,
2-lagiges Daughterboard, das exakt auf die Stiftleisten des TTGO T-Display v1.1
gesteckt wird (Pin-Raster 1:1 aus der offiziellen LilyGO-Eagle-Bibliothek
übernommen), inkl. isoliertem Step-Down-Wandler, PTT-Schaltung und
RC-Filter für die Bandspannung.

- **[hardware/DESIGN.md](hardware/DESIGN.md)** – vollständige Bauteilliste,
  Netzliste, Layout-Vorgaben, Inbetriebnahme-Checkliste
- **[hardware/BESTELLUNG.md](hardware/BESTELLUNG.md)** – Schritt-für-Schritt
  JLCPCB-Bestellanleitung
- **`hardware/kicad/`** – fertig geroutetes KiCad-Projekt (DRC-geprüft)
- **`hardware/gerber/`** – Gerber + BOM + CPL, fertig für den JLCPCB-Upload
- **`images/`** – Renderansichten des bestückten Boards

Die im finalen PCB-Entwurf verwendete PTT-Schaltung (N-MOSFET gegen GND) ist
eine bewusste, im Prototyp **nicht** getestete Vereinfachung gegenüber dem
ursprünglich verifizierten Optokoppler-Aufbau – Details und Begründung in
`hardware/DESIGN.md`. Der Optokoppler-Aufbau selbst ist am realen Gerät
vollständig getestet (siehe unten).

## Firmware

`firmware/ttgo_xpa_bridge.ino` (+ `bands.h`, `tft_setup.h`) – ein einziger
Arduino-Sketch für das TTGO T-Display v1.1 (Board-Definition:
`esp32:esp32:lilygo_t_display`, Arduino-ESP32-Core 3.x).

### Funktionen

- **Live-Anzeige** auf dem eingebauten TFT: Frequenz, Modulationsart, Band,
  RX/TX-Status, Uhrzeit (lokal + UTC, aus dem 705 ausgelesen), S-Meter.
- **Automatische Bandumschaltung** der XPA125B passend zur 705-Frequenz (PWM →
  RC-Tiefpass → Bandspannung, Tabelle inkl. Lastkorrektur in `bands.h`).
- **PTT-Durchschaltung** – der 705 meldet TX per CI-V-Broadcast/FastPTT-Push,
  das TTGO schaltet die PA-PTT-Leitung synchron mit.
- **Tune-Taster** (oberer Button, Mehrfachklick = längere Haltezeit): setzt
  kurzzeitig Leistung + Modus (FM statt CW – siehe Kommentar im Code, PTT-only
  erzeugt in CW keinen Träger), keyt den XPA125B-eigenen Antennentuner, stellt
  danach den Ausgangszustand wieder her. Jeder Schritt wartet auf eine
  FB(OK)/FA(NG)-Bestätigung; bei Fehlschlag Eskalation bis zur kompletten
  Geräteabschaltung (Cmd `18 00`).
- **ALC-Schutzschaltung** (neu, 2026-09-27) – siehe
  **[docs/alc-protection.md](docs/alc-protection.md)** für die volle
  Herleitung, Kalibrierdaten und Begründung, warum ein einfacher gleitender
  Mittelwert dafür nicht ausreicht.
- **Persistentes ALC/Leistungs-Logging** auf SPIFFS (überlebt den
  USB/PA-Stromwechsel, den ein reiner RAM-Puffer nicht überlebt hätte).
- **Zweiter Funkgeräte-Modus (PMR-171, Bluetooth LE)** – per langem Tastendruck
  umschaltbar (in NVS/`Preferences` persistiert). Eigenständiges Protokoll
  (BLE GATT statt Classic SPP), teilt sich aber Anzeige/GPIO-Grundgerüst mit dem
  705-Modus. Die ALC-Schutzschaltung ist **bewusst 705-spezifisch** und bleibt
  in diesem Modus inaktiv.
- **Automatischer Bluetooth-Reconnect**: das Board ist gleichzeitig
  BT-Server (nimmt Verbindungen vom 705 an) und -Master (verbindet sich aktiv
  zu bereits gekoppelten Geräten) – deckt sowohl "705 verbindet sich selbst"
  als auch "705 hat die Verbindung verloren, TTGO holt sie aktiv zurück" ab.

### Serial-Kommandos (USB, 115200 Baud, nur wenn USB angeschlossen – siehe
Sicherheitshinweis oben zur PA-Stromversorgung)

| Kommando | Wirkung |
|---|---|
| `dbg on` / `dbg off` | rohe CI-V-Frames auf der Konsole mitloggen |
| `fastptt on` | fastPTT-Push (`24 00 00 01`) aktivieren |
| `txpoll <ms>` | Poll-Intervall für den TX-Status |
| `band <name\|zero\|auto>` | Bandspannung manuell fixieren/automatisch |
| `alclog dump` / `clear` / `status` | ALC/Leistungs-Log (SPIFFS) auslesen/löschen/Status |
| `alcprotect on` / `off` | Schutzschaltung ohne Neuflashen de-/aktivieren |

### Flashen

```bash
arduino-cli core install esp32:esp32
arduino-cli compile --fqbn esp32:esp32:lilygo_t_display firmware/ttgo_xpa_bridge.ino
arduino-cli upload -p /dev/ttyXXX --fqbn esp32:esp32:lilygo_t_display firmware/ttgo_xpa_bridge.ino
```

Getestet mit Arduino-ESP32-Core 3.3.11.

## Status

- **Optokoppler-Prototyp (frei verdrahtet):** live an der echten XPA125B
  verifiziert – Bandumschaltung, PTT, Tune-Sequenz, ALC-Auslesung und die
  Schutzschaltung wurden alle mit echter Hardware getestet.
- **SMD-Daughterboard (MOSFET-Variante):** Design fertiggestellt und bei JLCPCB
  bestellt/gefertigt – die PTT-Schaltung dieser Variante selbst ist noch nicht
  am realen Gerät gegengetestet (siehe `hardware/DESIGN.md`).
- **ALC-Schutzschaltung:** live getestet in FM (Kollapspunkt sauber erkannt)
  und SSB (vierstufige automatische Rückregelungs-Kaskade beobachtet, hat einen
  drohenden PA-Fehlerabschaltung erfolgreich verhindert).

## Lizenz

Eigener Code (Firmware, PCB-Design) unter [MIT-Lizenz](LICENSE). Die in
`firmware/bands.h` übernommene Bandspannungs-/Lastkorrekturtabelle stammt aus
DL1BZs `z_userprog.ino` (siehe Danke-Abschnitt) – bitte bei Weiterverwendung
dieser konkreten Werte entsprechend referenzieren.
