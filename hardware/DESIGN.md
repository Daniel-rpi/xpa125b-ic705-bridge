# TTGO ↔ XPA125B – SMD-Platine (Entwurfspaket für EasyEDA / JLCPCB)

Stand 2026-09-18. Grundlage: die **am 2026-09-18 an der echten XPA125B verifizierte** Prototyp-Schaltung
(4N25 + RC + isolierter DC/DC), ergänzt um zwei bewusste Änderungen (siehe „Abweichungen vom Prototyp").
Alle Teilenummern/Bestände/Gebührenklassen wurden am 2026-09-18 live gegen die JLC-Teiledatenbank
(jlcsearch.tscircuit.com, Felder `is_basic`/`is_preferred`/`stock`) geprüft – vor der Bestellung im JLC-Warenkorb
nochmal gegenchecken.

## Bauform (Nutzervorgabe 2026-09-18): Daughter-Board direkt auf den TTGO-Stiftleisten
Kleine Platine mit **genau dem Pin-Raster des TTGO T-Display v1.1** (2 Reihen à 12 Pins, 2,54 mm Raster, Reihenabstand
22,86 mm). Sie wird von hinten auf die Stiftleisten des TTGO gesteckt und auf der Außenseite verlötet - optisch ein eng
anliegendes Huckepack-Board. Maße/Koordinaten stammen aus der **offiziellen LilyGO-Eagle-Bibliothek**
(`TTGO-TDisplay_LilyGO_official.lbr`, Package `TTGO-T-DISPLAY-OL`), nicht aus Schätzungen:
- **Platine 25,89 × 31,94 mm** (0,4 mm je Seite breiter als der TTGO, damit die Pads ≥ 0,7 mm Randabstand haben - JLC trennt bei der
  Bestückung Randstreifen per V-Ritz und verlangt dafür ca. 0,4 mm; Länge = die 12 Pins + je 2,0 mm Rand), 0,8 mm, 2 Lagen.
- Pads: Bohrung 1,0 mm, **Pad-Ø 1,6 mm** (bei 1,78 mm läge der Kupferrand nur 0,22 mm von der Platinenkante, JLC will ≥ 0,3 mm;
  mit 1,6 mm sind es 0,31 mm).
- **Alle 24 Pads** liegen an den TTGO-Positionen (Tabelle in `ttgo_daughterboard_pads.csv`, Vorschau `board_preview.svg`).
  **Nur vier Pads sind mit der Schaltung verbunden**, alle anderen sind reine Lötaugen ohne Netz:

| TTGO-Pin | Reihe | Position (x / y in mm ab Platinen-Ecke unten links, USB-C-Ende unten) | Netz |
|---|---|---|---|
| 3V3 (`3V3_0`) | links | 1,11 / 2,00 | `3V3` (Ausgang des Bucks) |
| GND (`GND_4`) | rechts | 23,97 / 4,54 | `GND` |
| GPIO27 | rechts | 23,97 / 7,08 | `G27` (Bandspannung) |
| GPIO26 | rechts | 23,97 / 9,62 | `G26` (PTT) |
| **5V** | rechts | 23,97 / 2,00 | **NICHT verbinden** (direkt neben GND_4 - Kupfer-Abstand beachten) |

- **Alle vier Signalpads liegen in der rechten Reihe** dicht beieinander (y = 2,0 … 9,6), 3V3 gegenüber links unten.
- **Freie Zone für hohe Bauteile:** Mitte zwischen den Reihen, **y = 7,1 … 14,7 mm** (zwischen den Pads GPIO27 und GPIO33),
  x ≈ 4,2 … 20,9 mm. Auf dem Rückseitenfoto des TTGO ist genau dieser Bereich weitgehend unbestückt (Flash-IC ≈ 1,8 mm und
  die Tantal-Elkos sitzen an den Rändern/im USB-Bereich). Dorthin C1 (1206) und L1; niedrige Teile dürfen auch woanders hin.
- **J1 (ACC-Kabel)**: 4 Lötpads im 2,54-mm-Raster am Displayende der Platine, Vorschlag x = 8,74 / 11,28 / 13,82 / 16,36 bei
  y = 29,5 (Reihenfolge `+12V`, `PTT`, `BAND`, `GND` = ACC-Pin 1, 2, 3, 6). Die Kabel gehen so **nach oben weg, nicht am
  USB-C-Anschluss vorbei**.
- **Bestückungsseite = Richtung TTGO (Nutzerentscheidung 2026-09-18):** Stiftleisten-Kunststoffkörper sind „etwas mehr als 2,5 mm"
  hoch, das ist der Spalt zwischen TTGO-Rückseite und Board. Höhenbudget der Bauteile (Spalt 2,5 mm): C1 1206 ≈ 1,6 mm, U1/L1/D/Q1
  ≈ 1,0-1,1 mm, 0805 ≈ 1,25 mm, 0603/0402 < 1 mm. Auf der TTGO-Rückseite sind laut Produktfoto (mm-Raster über das Bild gelegt)
  **nur der Flash-IC (≈ 1,75 mm, Frontansicht x = 1,9-7,3 / y = 4,6-14,6 mm) und die Tantal-Elkos am USB-Ende (y < 2,2 mm)** hoch;
  dort sitzt kein Bauteil des Daughter-Boards. Der QFN (≈ 0,9 mm, x = 8,9-15,5 / y = 18,5-25 mm) und die Kleinteile bleiben unter
  1,25 + 0,9 < 2,5 mm. **Beim ersten Aufstecken trotzdem Probe-Sitz prüfen** (Foto-Ablesung ist eine Näherung).
- Aus den KiCad-Daten: Board wird auf der **Außenseite (B.Cu)** nur verlötet - dort GND-Fläche, PTT/BAND-Langstrecken, Beschriftung
  der TTGO-Pins (gespiegelt, damit sie von außen lesbar ist).

## ⚠ Der eine Punkt, der gegen „keine Aufpreis-Teile" läuft
Die 3,3-V-Versorgung aus den ~12,85 V der ACC-Pin-1 braucht einen **Schaltregler** (Linearregler würde ~1,2 W verheizen).
JLC führt **keine einzige Basic/Preferred-Leistungsdrossel** (nur mA-Signalinduktivitäten), und die freien Buck-Regler
(XL1509, TPS5430, TPS54331) sind Altbau-Typen mit Diode + Elektrolytkondensatoren – also weder flach noch ohne Extended-Drossel.
Deshalb: **2 Extended-Teile** (AP63203 + Drossel). Meines Wissens berechnet JLC dafür je eindeutigem Extended-Teil eine
einmalige Ladegebühr (ca. 3 USD pro Bestückungsauftrag) – im Warenkorb prüfen. Beide Teile sind stark lagernd
(11.392 bzw. 548.730 Stk.), also keine Sonderbestellware im Sinne von „nicht lieferbar".
Kostenfreie Alternative: keine gefunden, die auch flach ist (LDO = Hitzeproblem, siehe oben).

## ⚠️ ACC-Pin 1 ist im Handbuch als N/C dokumentiert, führt aber real Spannung
Das offizielle Xiegu-Handbuch listet ACC-Pin 1 der XPA125B als nicht belegt
(N/C). An unserem werksseitigen, unmodifizierten Exemplar liegt dort trotzdem
**real die Versorgungsspannung der PA an** (gemessen ~12,85 V bei
angeschlossenem Netzteil, skaliert mit dessen 12–14,5 V) - genau darauf baut
die gesamte Stromversorgung dieser Platine auf (`+12V_RAW` unten). Das war
kein Zufallsfund, sondern das Ergebnis einer längeren Diskussion und
letztlich einer direkten Multimeter-Messung. **Vor dem Nachbau am eigenen
Gerät selbst nachmessen** - eine andere Fertigungscharge/Hardwarerevision
könnte das jederzeit anders handhaben, da diese Belegung von Xiegu nie
zugesichert wurde.

## Schaltplan (Netzliste)
Netze: `+12V_RAW` (ACC-Pin 1, laut Handbuch N/C - siehe Warnhinweis oben), `+12V` (hinter D1), `3V3`, `GND`,
`SW`, `BST`, `PTT` (ACC-Pin 2), `BAND` (ACC-Pin 3), `G26`, `G27`, `GATE`.

| Bauteil | Anschlüsse |
|---|---|
| **J1** (4 Lötpads, ACC-Kabel) | 1=`+12V_RAW` (ACC-Pin 1), 2=`PTT` (Pin 2), 3=`BAND` (Pin 3), 4=`GND` (Pin 6) |
| **TTGO-Pads** (siehe oben) | `3V3` (links unten), `GND`, `G27`, `G26` (rechts) - kein separater Stecker J2 |
| D2 TVS SMF18CA | `+12V_RAW` ↔ `GND` |
| D1 1N5819WS | Anode `+12V_RAW`, Kathode `+12V` (Verpolschutz) |
| C1 10 µF/50 V | `+12V` ↔ `GND` (direkt an U1 Pin 3/4) |
| U1 AP63203WU-7 | Pin1 FB=`3V3`, Pin2 EN=`+12V`, Pin3 VIN=`+12V`, Pin4 GND=`GND`, Pin5 SW=`SW`, Pin6 BST=`BST` |
| C4 100 nF | `BST` ↔ `SW` |
| L1 4,7 µH | `SW` ↔ `3V3` |
| C2, C3 22 µF | `3V3` ↔ `GND` (je einer) |
| Q1 AO3400A | G=`GATE`, D=`PTT`, S=`GND` |
| R2 100 Ω | `G26` → `GATE` |
| R3 100 kΩ | `GATE` ↔ `GND` (hält PTT beim ESP32-Boot sicher aus) |
| R1 470 Ω | `G27` → `BAND` |
| C5 22 µF | `BAND` ↔ `GND` (direkt an J1.3 platzieren) |
| TP1–TP4 (Prüfpads, ohne BOM) | `3V3`, `+12V`, `BAND`, `PTT` |

Werte U1-Umfeld laut Diodes-Datenblatt DS41326, Tabelle 2 (AP63203, 3,3 V): L 3,9 µH (4,7 µH liegt im empfohlenen
Bereich 2,2–10 µH), C1 10 µF, C2 2×22 µF, C3 (BST) 100 nF, FB direkt an VOUT, EN darf an VIN (Abs.max 35 V, VIN 3,8–32 V).
RC-Filter 470 Ω/22 µF unverändert vom verifizierten Prototyp.

## Abweichungen vom verifizierten Prototyp (bewusst, bitte beachten)
1. **PTT-Schalter: N-MOSFET statt 4N25-Optokoppler.** Die Massen von TTGO und PA sind im Design ohnehin verbunden
   (für das Bandspannungssignal), der Optokoppler bringt also keine echte galvanische Trennung, ist aber das höchste Bauteil.
   AO3400A (48 mΩ @ 2,5 V Gate, SOT-23 ~1,1 mm) zieht PTT sauberer nach GND als die Opto-Sättigungsspannung.
   Sicherheit gleichwertig: der MOSFET kann die PTT-Leitung nur nach GND ziehen, nie Spannung einspeisen (Handbuch:
   ≥3,2 V am PTT-Pin zerstören die PA). **Auf dem Prototyp nicht getestet** – Firmware-Logik (GPIO26 HIGH = TX) bleibt identisch.
   Wer die verifizierte Topologie behalten will: LTV-357T (SOP-4 mini-flat) mit 220 Ω statt Q1/R2/R3 – sag Bescheid.
2. **Nicht isolierter Buck statt AM1G-1203SZ** – so bereits am 2026-09-13 entschieden (Isolation wegen der GND-Brücke
   sowieso nur „weich").

## Stückliste (JLCPCB-Format: `bom_jlcpcb.csv`)
| Ref | Wert | Package | LCSC | Klasse | Bestand |
|---|---|---|---|---|---|
| U1 | AP63203WU-7 (3,3 V, 2 A Buck) | TSOT-23-6 | C780769 | **Extended** | 11.392 |
| L1 | 4,7 µH, 2 A, 160 mΩ (FTC252010S4R7MBCA) | 1008 (2,5×2,0 mm) | C5832360 | **Extended** | 548.730 |
| Q1 | AO3400A N-MOSFET | SOT-23 | C20917 | Basic | 1.535.925 |
| D1 | 1N5819WS Schottky 40 V/1 A | SOD-323 | C191023 | Basic | 5.015.719 |
| D2 | SMF18CA TVS (bidirektional) | SOD-123FL | C19077513 | Preferred (gebührenfrei) | 398.632 |
| C1 | 10 µF/50 V X5R | 1206 | C13585 | Basic | 1.877.266 |
| C2, C3, C5 | 22 µF/25 V X5R | 0805 | C45783 | Basic | 1.734.789 |
| C4 | 100 nF/16 V X7R | 0402 | C1525 | Basic | 16.407.331 |
| R1 | 470 Ω 1 % | 0603 | C23179 | Basic | 1.899.614 |
| R2 | 100 Ω 1 % | 0603 | C22775 | Basic | 7.325.193 |
| R3 | 100 kΩ 1 % | 0603 | C25803 | Basic | 7.990.119 |

Einbauhöhe ca. 0,8 mm Platine + max. ~1,6 mm (C1) ≈ **2,4 mm gesamt**, sonst ≤ ~1,45 mm. Wer noch 0,3 mm will: C1 auf
10 µF/25 V 0805 (C15850) – 25 V liegt allerdings unter der TVS-Klemmspannung (29 V, nur µs-Impulse), daher Standard 50 V.

## Layout-Vorgaben (EasyEDA)
- **Zuerst die 24 TTGO-Pads exakt nach `ttgo_daughterboard_pads.csv` setzen** und die Platinenkontur (25,09 × 31,94 mm) zeichnen.
  Erst danach bestücken. Wer in EasyEDA lieber importiert: die mitgelieferte LilyGO-Eagle-Bibliothek enthält dieselben Pads.
- Nur die vier Netz-Pads (3V3, GND, G26, G27) verdrahten; die übrigen 20 Pads ohne Netz lassen (DRC-Warnung „unverbundenes
  Pad" ignorieren). Silkscreen an jedes Pad den TTGO-Pinnamen (Gegenprobe beim Aufstecken!). **5V-Pad: nicht verbinden**,
  Massefläche mit Abstand um dieses Pad herum freistellen.
- Unterseite (bzw. die Seite ohne Bauteile) als GND-Fläche, GND-Pad `GND_4` rechts unten anbinden.
- **Bauteilgruppen:** MOSFET Q1 + R1/R2/R3 + C5 direkt an den rechten Pads G26/G27/GND (y ≈ 4,5 … 10). Buck-Gruppe
  (U1, L1, C1–C4, D1, D2) in der Mitte bzw. darüber; **C1 und L1 in die freie Zone y = 7,1 … 14,7.**
- **U1-Umfeld eng:** C1 direkt an VIN/GND (Pin 3/4), L1 direkt an SW (Pin 5), C4 zwischen BST/SW, C2/C3 direkt hinter L1,
  SW-Fläche so klein wie möglich, GND-Vias neben C1/C2/C3. **BAND-Netz** (PWM-gefiltert) weit weg vom SW-Knoten,
  C5 unmittelbar am Pad G27-/J1-Zweig.
- J1-Beschriftung groß (`+12V`, `PTT`, `BAND`, `GND`) - ein Vertauschen an PTT ist der einzige teure Fehler.
- Prüfpads TP1–TP4 (1,0 mm) an den Rand, keine Montagebohrungen nötig (die Stiftleisten halten die Platine).
- Bauteile in EasyEDA über die **LCSC-Nummer** aus der BOM laden.

## JLCPCB-Bestelloptionen
PCB: 2 Lagen, 0,8 mm, HASL lead-free, Standard-Farbe. Bestückung: **Top only**, Standard-Bestückung; im Assembly-Schritt
prüfen, dass bei den Basic/Preferred-Teilen keine Extended-Gebühr angezeigt wird und bei U1/L1 genau zwei Gebühren stehen.
Drehwinkel/Polarität von U1 (Pin 1 Marker), D1, D2 und Q1 im Bestückungs-Viewer kontrollieren (häufigster Fehler).

## Inbetriebnahme (bevor die PA angeschlossen wird)
1. Noch ohne TTGO: nur J1.1 (+12 V) und GND mit dem Labornetzteil (12–14 V, Strombegrenzung 200 mA): TP `3V3` = 3,3 V ±3 %, Ruhestrom < 20 mA.
2. Platine auf die TTGO-Stiftleisten stecken (USB **nicht** gleichzeitig – der TTGO-LDO würde gegen den Buck arbeiten): bootet, verbindet sich.
3. Am 705 das Band wechseln: TP `BAND` = Handbuch-Sollwert +~3 % (ohne PA), z. B. 10m ≈ 2363 mV, 20m ≈ 1415 mV.
4. PTT drücken: `PTT`-Pad gegen GND im RX hochohmig, bei PTT nahezu 0 Ω (durchgeschalteter MOSFET), Rest wie im bisherigen PTT-Test.
5. Erst dann an die XPA125B; rote PTT-Ader vorher gegen GND messen (< 3,2 V!).

## Was ich nicht liefern kann (ehrlich)
Ich kann den EasyEDA-Editor nicht bedienen und habe **keine Gerber-/EasyEDA-Dateien erzeugt**. Der Entwurf ist die vollständige
Vorlage zum Nachzeichnen (Schaltplan ≈ 15 Min, Layout ≈ 30 Min); ich prüfe gern dein Ergebnis (Netzliste/BOM/Layout-Screenshot).
