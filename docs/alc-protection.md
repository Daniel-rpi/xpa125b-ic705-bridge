# ALC-Schutzschaltung (ALC protection)

*(English summary at the bottom of this document.)*

## Warum

Die XPA125B ist laut Datenblatt für 13 dB Gain / ≤5 W Ansteuerung / ≥110 W Ausgang
ausgelegt. Community-Berichte (und unsere eigenen Messungen) zeigen aber, dass der
Gain bei manchen Exemplaren deutlich außerhalb der Spezifikation liegt – schon
deutlich unter 1 W Ansteuerung kann nahezu die volle Ausgangsleistung anliegen.
Die PA hat einen eigenen harten Schutzabschalter (SWR/Übersteuerung), aber keine
abgestufte Warnung davor.

Wir haben **live am echten IC-705** geprüft, ob das Funkgerät selbst über sein
ALC-IN reagiert, wenn man das originale 705↔XPA125B-Rückführkabel ansteckt
(direkte Kabelverbindung, TTGO währenddessen nicht an der PA – beides geht wegen
gemeinsamer Buchse nicht gleichzeitig). Ergebnis: **keine native Regelung.** Das
Po-Meter folgt exakt der eingestellten Sendeleistung, unabhängig vom ALC-Meter-Wert
– sowohl in FM (bis 48 % Leistung getestet) als auch in SSB (Sprache + Pfeifton, bis
25–30 %). Die ALC-Regelung des 705 ist offenbar auf sein eigenes PA-Verhalten
ausgelegt, nicht auf ein externes ALC-Signal von einer nachgeschalteten PA.

**Folge:** Die einzige abgestufte, softwareseitige Schutzstufe zwischen "PA
arbeitet normal" und "PA schaltet hart ab" ist die hier beschriebene Funktion auf
dem TTGO. Sie ist **langsamer und gröber als eine echte ALC-Regelschleife**
(Abtastung alle 250 ms, Reaktion in festen Schritten, kein automatisches
Wiederhochfahren) – aber besser als nichts.

## Hardware

- XPA125B ACC-Pin 4 (ALC-Ausgang) → **220 Ω** in Reihe → TTGO GPIO36 (ADC1_CH0,
  input-only, kein Lötpad auf der Platine nötig – GPIO36 ist am T-Display v1.1
  ohnehin frei zugänglich).
- Gemessen (Multimeter, direkt an Pin 4 gegen Masse, echtes AliExpress-705↔XPA125B-
  Kabel mit durchverbundenem Pin 4): **RX ≈ 3,99 V, TX ≈ 1–2 V, durchgehend
  positiv, nie negativ.** Die genaue Bedeutung/Skalierung ist von Xiegu nicht
  dokumentiert – das Verhalten (hoch im Ruhezustand, sinkt beim Senden, sinkt
  weiter bei zunehmender Übersteuerung bis fast 0) deutet auf eine invertierte
  Aussteuerungsreserve-Anzeige hin.
- 3,99 V liegen ca. 0,4 V über der ESP32-ADC-Grenze (3,6 V absolut). Der 220-Ω-
  Widerstand begrenzt den Strom durch die interne Schutzdiode des GPIO-Pins
  (Klemmung bei ca. 3,9 V) auf < 0,5 mA – unbedenklich, kein zusätzliches Bauteil
  nötig.
- `analogSetAttenuation(ADC_11db)` für den vollen 0–3,3 V-Messbereich.

## Firmware-Logik (`ttgo_xpa_bridge.ino`)

### Persistentes Logging (für die Kalibrierung)

Die TTGO-Platine kann **nicht gleichzeitig per USB und von der PA versorgt
werden** (kein Akku, kein zweiter Spannungsregler-Pfad) – jedes Anstecken von USB
zum Auslesen der Messwerte reißt die Stromversorgung kurz ab und startet das
Board neu. Ein reiner RAM-Puffer für die Messreihen ist deshalb **garantiert
verlustanfällig**: genau das ist bei der allerersten Testreihe passiert – alle
Daten waren beim Wiederanstecken weg.

Fix: SPIFFS (Flash-Dateisystem). Während einer Sendung wird `ALC-Wert;
705-Sendeleistung (14 0A)` alle 250 ms in einen kleinen RAM-Zwischenpuffer
geschrieben und **erst nach TX-Ende** (oder bei einem ungewöhnlich langen Burst
vorsorglich schon währenddessen) als CSV-Block ans Flash angehängt – bewusst
nicht während jeder einzelnen Sendung, damit kein Flash-Schreibvorgang das
PTT-kritische Timing beeinflusst. Serial-Kommandos: `alclog dump` (ausgeben),
`alclog clear` (löschen), `alclog status`.

Alle Kalibrierdaten in diesem Dokument stammen aus genau diesen Dumps.

### Zwei Regelzweige, je nach Modulationsart

Ein fester Spannungsschwellwert allein funktioniert nicht für alle
Betriebsarten – ein Träger-Modus (FM) hat eine praktisch konstante ALC-Spannung
während der ganzen Sendung, ein Hüllkurven-Modus (SSB/CW) schwankt mit jedem Wort/
Atemzug zwischen weit auseinanderliegenden Werten. Die Firmware unterscheidet
deshalb:

```cpp
bool isConstantCarrierMode(uint8_t mode) {
  switch (mode) {
    case 0x02: case 0x04: case 0x05: case 0x06: case 0x08: case 0x17:
      return true;   // AM, RTTY, FM, WFM, RTTY-R, DV
    default:
      return false;  // LSB, USB, CW, CW-R → Hüllkurven-Modi
  }
}
```

**Konstanter Träger (FM/AM/RTTY/…):** einfaches Debounce. Fällt die ALC-Spannung
unter die Schwelle und bleibt dort für `ALC_PROTECT_FM_DEBOUNCE_MS` (750 ms),
gilt das als Übersteuerung.

**Hüllkurven-Modi (SSB/CW):** Hier hat sich der naheliegende Ansatz – ein
exponentiell gewichteter gleitender Mittelwert (EMA) über die ALC-Spannung – in
der Praxis als **grundlegend ungeeignet** erwiesen (siehe unten). Stattdessen ein
**Leaky-Bucket-Akkumulator** (asymmetrisch: schnell rauf, langsam runter):

```cpp
if (alcMv < ALC_PROTECT_THRESHOLD_MV) {
  alcProtectSsbAcc += ALC_PROTECT_SSB_ACC_INC;   // +1.0 pro 250-ms-Tick unter der Schwelle
} else {
  alcProtectSsbAcc -= ALC_PROTECT_SSB_ACC_DEC;   // -0.3 pro Tick darüber
}
// geclampt auf [0, ALC_PROTECT_SSB_ACC_CAP=20], ausgelöst ab ALC_PROTECT_SSB_ACC_TRIGGER=6.0
```

Warum das funktioniert, wo EMA scheitert: Eine echte Sprach-/Pfeifton-Übersteuerung
zeigt sich als **wiederkehrende kurze Unterschreitungen**, unterbrochen von
normalen Atempausen (in denen die ALC-Spannung kurz auf den Ruhewert ~3,1 V
zurückspringt). Ein EMA mit einem für schnelle Reaktion nötigen α wird durch
jede Atempause sofort wieder nach oben gezogen und erreicht den Schwellwert nie
zuverlässig – simuliert gegen eine echte 18-Sekunden-Aufnahme (durchgehend
lautes Pfeifen mit natürlichen Atempausen) blieb der EMA-Minimalwert bei
1107,5 mV, weit über der 500-mV-Schwelle, *obwohl* die reale Übersteuerung
während der gesamten Aufnahme präsent war. Der Leaky-Bucket dagegen **zählt
Ereignisse statt zu glätten**: jede Unterschreitung erhöht den Zähler stärker,
als eine kurze Pause ihn absenkt – bei anhaltender (auch intermittierender)
Übersteuerung akkumuliert er zuverlässig bis zum Trigger, an derselben
Testaufnahme simuliert: Auslösung nach 2,25 s.

### Reaktion

```cpp
int step = constantCarrier ? ALC_PROTECT_FM_STEP_RAW : ALC_PROTECT_SSB_STEP_RAW;
int newRaw = currentPowerRaw - step;
sendPowerSetRaw(newRaw);   // CI-V 14 0A, reiner Fire-and-forget-Write
```

- **Ein Schritt pro Auslösung**, kein Sprung auf einen festen Zielwert. `raw` ist
  0–255 (`14 0A`-Rohwert, 705-intern), FM-Schritt = 10 (~2 %), SSB-Schritt = 13
  (~5 %) – unterschiedlich kalibriert, weil sich der tatsächliche Auslösepunkt in
  Roheinheiten je nach Modus stark unterscheidet.
- **Cooldown 4 s** zwischen zwei Aktionen, damit nicht mehrfach hintereinander
  nachgeregelt wird, bevor sich der neue Zustand eingestellt hat.
- **Kein automatisches Wiederhochfahren** (bewusste Entscheidung) – der Bediener
  muss die Leistung nach einer Auslösung selbst wieder erhöhen. Sicherste
  Variante: die Software regelt nur nach unten, nie eigenständig nach oben.
- **Fire-and-forget statt blockierendem FB/NG-Handshake** (anders als die
  Tune-Sequenz an anderer Stelle im selben Sketch) – ein automatischer
  Schutzeingriff darf den Hauptloop/die PTT-Failsafe-Prüfung während einer
  laufenden Sendung nicht aufhalten. Schlägt der Schreibvorgang unbemerkt fehl,
  ist man dadurch nicht schlechter gestellt als vorher; die Reduktion wird beim
  nächsten Auslösen einfach erneut versucht.
- Deaktivierbar zur Laufzeit über die USB-Serielle: `alcprotect off` / `alcprotect on`
  (Default: an) – falls sich während eines Tests etwas unerwartet verhält, ohne
  neu flashen zu müssen.
- Reine 705-Funktion – im (davon unabhängigen) PMR-171-BLE-Modus derselben
  Firmware bleibt sie inaktiv.

## Kalibrierdaten (reale Messungen, 2026-09-27)

### FM-Sweep, 1 % → 21 % (bei angeschlossener PA)

| Leistung (raw / %) | ALC (mV) | Beobachtung |
|---|---|---|
| 3 / 1,2 % … 13 / 5,1 % | 2700–3118 | normal, keine Übersteuerung |
| 16 / 6,3 % | fällt von 2009 → 142 innerhalb 1 s | **PA-eigener Kollaps beginnt hier** |
| 18 / 7,1 % | bleibt bei ~142–190 | PA bereits im Fehlerzustand |
| 21 / 8,2 % | ~147–150, dann PA schaltet ab | harter Schutzabschalter der PA |

→ Der reale PA-Kollaps liegt konstant bei **raw = 16** (unabhängig vom
Testdurchlauf, mehrfach reproduziert) – siehe auch die Test-CSV im Repo
(`alcprotect_test_2026-09-27.csv` als Referenz, nicht mit veröffentlicht, da reine
Rohmessdaten ohne zusätzlichen Erkenntniswert über diese Tabelle hinaus).

### SSB, sustained lautes Pfeifen bis 35 %

PA schaltet bei ca. **raw = 90 (35 %)** ab. Mit dem finalen Leaky-Bucket-Ansatz
live bestätigt: **vierstufige automatische Rückregelungs-Kaskade**
90 → 77 → 64 → 51 → 38 (raw), die sich auf einen stabilen, crash-freien
Arbeitspunkt einschwang, ohne dass die PA nochmal in den Fehlerzustand ging.

## Bekannte Grenzen (ehrlich)

- Kein automatisches Wiederhochfahren – reine Schutzfunktion, kein
  Leistungsoptimierer.
- Reaktionszeit ~250 ms–4 s, je nach Modus/Debounce/Cooldown – eine echte
  Hardware-ALC-Schleife ist um Größenordnungen schneller. Für einen harten,
  plötzlichen Fehlanpassungsfall (z. B. offene Antenne) ersetzt diese Funktion
  **nicht** die PA-eigene Schutzabschaltung, sie ergänzt sie nur um eine
  frühere, sanftere Warnstufe.
- Die SSB-Schrittgröße (13/~5 %) ist bislang **ohne eigene reale
  Auslöse-Rückmeldung** kalibriert (der 35 %-Test hat die Kaskade ausgelöst und
  bestätigt, dass sie konvergiert – aber der exakt optimale Schrittwert wurde
  nicht gegen mehrere unabhängige SSB-Übersteuerungs-Ereignisse durchgemessen).
- Kalibriert an **einem konkreten Exemplar** der XPA125B – der PA-Kollapspunkt
  (raw=16 im FM-Test) kann bei einem anderen Gerät abweichen.

## English summary

The XPA125B's own gain can be so far out of spec that near-full RF output
appears at well under 1 W of drive. We confirmed live on a real IC-705 that the
radio's own ALC input does **not** self-regulate transmit power when the direct
705↔XPA125B ALC feedback cable is used (Po-meter tracks the dial power setting
regardless of ALC-meter reading, both in FM and SSB) — so a software safety net
on the TTGO bridge is genuinely needed, not redundant.

We tap the PA's own ALC output (ACC pin 4) through a 220 Ω series resistor into
ESP32 GPIO36 (ADC1_CH0). For constant-carrier modes (FM/AM/RTTY/…) a simple
debounced threshold is sufficient. For envelope modes (SSB/CW) an exponential
moving average turned out to be fundamentally unsuited — verified by simulating
it against real recorded data, where sustained-but-intermittent overdrive (real
speech/whistle bursts separated by breathing pauses) never pulled the average
below threshold even though the danger was continuously present. We replaced it
with an asymmetric leaky-bucket accumulator (fast rise on overdrive, slow decay
on recovery), which correctly triggered a 4-step automatic power-reduction
cascade (90→77→64→51→38 raw units) that converged to a stable, crash-free
operating point in a live SSB test. Every threshold/step value here was derived
from actual serial-logged test data (persisted to SPIFFS flash, since the board
cannot be powered by USB and the PA at the same time to retrieve a RAM-only log),
never from theory alone.
