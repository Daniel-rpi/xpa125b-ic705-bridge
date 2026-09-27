// TTGO T-Display v1.1 - Live-Anzeige fuer den IC-705 per Bluetooth CI-V,
// PLUS (seit 2026-09-11) ein Taster-ausgeloester Tune-Trigger fuer eine
// nachgeschaltete Xiegu XPA125B.
//
// SICHERHEIT: Die reine Anzeige (Frequenz/Modus/TX-Status/Uhrzeit/UTC-
// Offset) sendet weiterhin ausschliesslich reine CI-V-Lesebefehle (kein
// Datenbyte nach dem [Sub-]Kommando -> "Read", kein "Write").
//
// EINZIGE AUSNAHME: runTuneSequence() (ausgeloest durch den oberen Taster,
// GPIO35) sendet ECHTE Sende-Kommandos (Leistung setzen, Modus setzen,
// PTT ein/aus) - das ist die erste tatsaechliche TX-Aktion dieses Boards.
// Jeder einzelne Schritt wartet auf eine bestaetigte FB(OK)/FA(NG)-Antwort
// des Radios, bevor der naechste Schritt erfolgt (exakt dasselbe Muster
// wie band_sync.py auf raspberryhf fuer PTT nutzt). Schlaegt IRGENDEIN
// Schritt fehl/bleibt unbestaetigt, wird sofort auf PTT-AUS + Wieder-
// herstellung von Modus/Leistung eskaliert; bleibt selbst das erfolglos,
// wird als letzte Notbremse Cmd 18 00 (komplette Geraete-Abschaltung)
// gesendet - dieselbe staerkste verfuegbare Absicherung, die auch die
// bestehende 705-Notfall-Abschaltung auf dem homeassistant-Host nutzt.
// Ein fest verdrahteter Gesamt-Timeout (TUNE_SEQ_HARD_TIMEOUT_MS) schuetzt
// zusaetzlich gegen ein Haengenbleiben mitten in der Sequenz.
#include "tft_setup.h"
#include <TFT_eSPI.h>
#include "BluetoothSerial.h"
#include <BLEDevice.h>
#include <Preferences.h>
#include "bands.h"

TFT_eSPI tft = TFT_eSPI();
BluetoothSerial SerialBT;

// CI-V-Adressen (identisch zu band_sync.py auf raspberryhf)
const uint8_t RADIO_ADDR = 0xA4;   // feste CI-V-Werksadresse des IC-705
const uint8_t CTRL_ADDR  = 0xE0;   // Controller-/PC-Adresse

// Nur diese Byte-Folgen werden je gesendet - alle reine Lesebefehle
// (kein Datenbyte nach dem [Sub-]Kommando -> "Read", kein "Write").
// Uhrzeit/UTC-Offset laut offiziellem IC-705-CI-V-Referenzhandbuch:
// Cmd 1A 05, Sub-Sub-Kommando 0166 (Uhrzeit, lokal!) bzw. 0170 (UTC-
// Offset: Versatz-BCD + Richtung 00=+/01=-).
const uint8_t QUERY_FREQ[]       = {0xFE, 0xFE, RADIO_ADDR, CTRL_ADDR, 0x03, 0xFD};
const uint8_t QUERY_MODE[]       = {0xFE, 0xFE, RADIO_ADDR, CTRL_ADDR, 0x04, 0xFD};
const uint8_t QUERY_TX_STATUS[]  = {0xFE, 0xFE, RADIO_ADDR, CTRL_ADDR, 0x1C, 0x00, 0xFD};
const uint8_t QUERY_TIME[]       = {0xFE, 0xFE, RADIO_ADDR, CTRL_ADDR, 0x1A, 0x05, 0x01, 0x66, 0xFD};
const uint8_t QUERY_UTC_OFFSET[] = {0xFE, 0xFE, RADIO_ADDR, CTRL_ADDR, 0x1A, 0x05, 0x01, 0x70, 0xFD};
const uint8_t QUERY_POWER[]      = {0xFE, 0xFE, RADIO_ADDR, CTRL_ADDR, 0x14, 0x0A, 0xFD};
const uint8_t QUERY_SMETER[]     = {0xFE, 0xFE, RADIO_ADDR, CTRL_ADDR, 0x15, 0x02, 0xFD};
const uint8_t CMD_POWER_OFF[]    = {0xFE, 0xFE, RADIO_ADDR, CTRL_ADDR, 0x18, 0x00, 0xFD};

// --- Tune-Sequenz-Konstanten (2026-09-11, Nutzerwunsch + XPA125B-Handbuch
// Abschnitt 3.2.3 "Using the automatic antenna tuner unit"). ---
// WICHTIGE ANNAHME: 50% wird bewusst als externe-DC-Versorgung (705-Max
// 10W) angenommen -> 50% von 10W = 5W, exakt der vom Handbuch geforderte
// Wert. Laeuft der 705 stattdessen auf Akku (Max 5W), waere 50% nur 2,5W
// zu wenig fuers Tunen - der Sketch kann den aktuellen Stromversorgungs-
// Modus nicht per CI-V abfragen (keine etablierte/verifizierte Abfrage
// dafuer bekannt), daher bewusst als Annahme dokumentiert statt geraten.
const int TUNE_POWER_PERCENT = 50;
// FM statt CW (Nutzerfund 2026-09-11, live getestet): reines CI-V-PTT
// (Cmd 1C 00) erzeugt in CW-Betriebsart KEINEN Traeger - CW braucht ein
// echtes Keying-Signal, das PTT allein nicht liefert. FM erzeugt bei PTT
// dagegen automatisch einen vollen Traeger ohne weiteres Kommando - exakt
// dasselbe bereits bewaehrte Prinzip, das combo_tune_sequence() in
// band_sync.py auf raspberryhf fuer die SPE-Expert-PA-Tune-Sequenz nutzt
// ("705 kurz auf FM keyen"). Das XPA125B-Handbuch nennt zwar "CW" als
// Vorgabe, aber der eigentliche Bedarf ist nur ein stabiler Traeger zum
// Tunen - den liefert FM genauso zuverlaessig, ohne zusaetzliche Komplexitaet.
const uint8_t TUNE_MODE = 0x05;              // FM
const unsigned long TUNE_HOLD_MS = 3000;     // Basiswert PRO Klick (Nutzerwunsch: 1x=3s, 2x=6s, 3x=9s)
const unsigned long TUNE_ACK_TIMEOUT_MS = 2000;
const unsigned long TUNE_SEQ_HARD_TIMEOUT_MS = 20000;  // Notbremse fuer die GESAMTE Sequenz
const int TUNE_PTT_OFF_MAX_RETRIES = 5;

// ============================================================
// XPA125B-Ansteuerung (2026-09-18): Bandspannung (GPIO27) + PTT (GPIO26).
// Schaltplan 1:1 nach dl1bz/ESP32-IC705BT-PA, Pinbelegung der 4N25-Seite
// gegen das Vishay-Datenblatt korrigiert (siehe CLAUDE.md):
//   GPIO26 --[220R]--> 4N25 Pin1(Anode), Pin2(Kathode)=GND,
//          Pin5(Kollektor) -> ACC Pin2 (PTT, rot), Pin4(Emitter) -> ACC Pin6 (GND)
//   GPIO27 -> PWM 5 kHz/10 Bit -> RC 470R + 22uF -> ACC Pin3 (Bandspannung)
// ACHTUNG (XPA125B-Handbuch): >= 3,2 V am PTT-Pin zerstoert den Prozessor
// der PA dauerhaft - GPIO26 darf NIE direkt an die PA, nur ueber den Opto.
// ============================================================
const int PIN_PTT_OUT  = 26;   // HIGH = Optokoppler an = PTT der PA gegen GND (TX)
const int PIN_BAND_OUT = 27;   // PWM -> RC-Tiefpass -> Bandspannung

// ------------------------------------------------------------
// ALC-Einlesen (2026-09-27): PA-Pin4 (ALC-Ausgang der XPA125B) ueber einen
// 220-Ohm-Vorwiderstand an GPIO36 (ADC1_CH0, input-only, kein Loeten auf
// der Platine noetig, das Loetauge war schon frei). Gemessen (Multimeter,
// direkt an Pin4 gegen Masse): RX ~3,994V, TX ~1,x..2,x V, durchgehend
// positiv. 3,994V liegen ~0,4V ueber der ESP32-Grenze (3,6V absolut) -
// der 220-Ohm-Widerstand begrenzt den Strom durch die interne Schutzdiode
// (Klemmung bei ca. 3,9V) auf unter 0,5 mA, unbedenklich. Reiner Lesewert,
// KEINE Rueckwirkung auf PTT/Band/705-Sendeleistung in dieser Stufe.
// Vorwaertsdeklarationen fuer den ALC-Logger weiter unten - die echten
// Definitionen liegen erst deutlich spaeter im File (Anzeigezustand bzw.
// Tune-Sequenz-Tracking), werden hier aber schon gebraucht.
extern int txState;
extern int currentPowerRaw;
extern bool havePower;
extern uint8_t modeCode;
extern bool tuneRunning;
extern bool mode171;

const int PIN_ALC_ADC = 36;
const unsigned long ALC_READ_INTERVAL_MS = 250;
int alcMv = -1;              // -1 = noch keine Messung
unsigned long lastAlcReadMs = 0;
unsigned long lastAlcLogMs = 0;

// ------------------------------------------------------------
// ALC-Datenlogger (2026-09-27, Nutzerwunsch: fuer eine Kalibrierkurve
// "705-Sendeleistung -> ALC" nicht mehr vom Display abschreiben muessen).
// Puffert ALC (Pin4, GPIO36) + die per Cmd 14 0A abgefragte 705-Sende-
// leistung waehrend eines TX-Bursts zunaechst im RAM, schreibt den Burst
// aber IMMER erst NACH dessen Ende (TX 1->0, siehe handleFrame()) als
// Block in eine SPIFFS-Datei - bewusst NICHT waehrend des Sendens selbst,
// damit kein Flash-Schreibvorgang das PTT-kritische Timing stoeren kann.
//
// KORREKTUR (2026-09-27, echter eigener Fehler): die erste Version dieses
// Loggers hielt die Daten AUSSCHLIESSLICH im RAM - das ueberlebt aber
// KEINEN Stromausfall, und genau ein solcher tritt bei JEDEM Wechsel
// PA-Strom -> USB auf (die Platine hat keinen Akku, der die Luecke
// ueberbruecken koennte - siehe Kopfkommentar zum ALC-Einlesen oben).
// Ergebnis: die allererste Testreihe des Nutzers war komplett verloren,
// weil das Board beim Wiederanstecken an USB (zum Auslesen!) neu
// gestartet ist. Deshalb jetzt SPIFFS statt reinem RAM - der Flash
// uebersteht den Stromausfall, den RAM-Puffer NIE.
//
// Bewusst KEIN Live-Bluetooth-Relay zu raspberryhf (bräuchte eine zweite,
// parallele BT-Verbindung neben der PTT-kritischen 705-Verbindung,
// unerprobt, plus einen neuen BT-Server auf raspberryhf) - stattdessen:
// PA-Strom trennen, USB an raspberryhf anschliessen, per Serial-Kommando
// "alclog dump" komplett auslesen (uebersteht beliebig viele Bootzyklen
// dazwischen, da die Datei erst per "alclog clear" geloescht wird).
// Leistung ist ein reiner CI-V-LESEBEFEHL (14 0A ohne Datenbytes danach) -
// aendert nichts am Radio, passt zum bestehenden Sicherheitsmodell dieses
// Sketches.
// ------------------------------------------------------------
#include "FS.h"
#include "SPIFFS.h"

struct AlcLogEntry {
  unsigned long ms;
  int alcMv;
  int powerRaw;   // -1 = zum Zeitpunkt dieser Messung noch kein gueltiger Leistungswert bekannt
};
// Reicht als RAM-Zwischenpuffer fuer EINEN Burst (bei 250ms-Takt 500
// Eintraege = 125s) - deutlich mehr als ein typischer Testsendeknopfdruck
// braucht. Wird nahe der Kapazitaetsgrenze (ALC_LOG_FLUSH_THRESHOLD) auch
// schon WAEHREND einer unerwartet langen Sendung zwischengespeichert,
// damit nichts verloren geht - das ist der einzige Fall, in dem doch
// waehrend TX auf Flash geschrieben wird.
const int ALC_LOG_CAPACITY = 500;
const int ALC_LOG_FLUSH_THRESHOLD = 480;
AlcLogEntry alcLog[ALC_LOG_CAPACITY];
int alcLogCount = 0;

const char* ALC_LOG_PATH = "/alclog.csv";
bool spiffsOk = false;

// Nur waehrend TX abgefragt (siehe loop()) - haelt currentPowerRaw/havePower
// (bereits fuer die Tune-Sequenz vorhanden) fuer den Logger aktuell, ohne
// den PTT-kritischen Loop bei RX unnoetig mit zusaetzlichem BT-Traffic zu
// belasten.
const unsigned long POWER_QUERY_INTERVAL_MS = 500;
unsigned long lastPowerQueryMs = 0;

// Haengt den aktuellen RAM-Zwischenpuffer an die persistente Datei an und
// leert ihn danach. Wird am TX-Ende aufgerufen (normalfall, deutlich
// ausserhalb jeder Zeitkritik) sowie vorsorglich kurz vor dem Ueberlaufen
// eines ungewoehnlich langen Bursts (Ausnahmefall, siehe logAlcSample()).
void flushAlcLogToFile() {
  if (alcLogCount == 0) return;
  if (!spiffsOk) { alcLogCount = 0; return; }  // nichts zu retten, verwerfen statt haengenzubleiben
  fs::File f = SPIFFS.open(ALC_LOG_PATH, FILE_APPEND);
  if (!f) {
    Serial.println("ALCLOG: Datei zum Anhaengen oeffnen fehlgeschlagen - Burst verworfen");
    alcLogCount = 0;
    return;
  }
  for (int i = 0; i < alcLogCount; i++) {
    const AlcLogEntry& e = alcLog[i];
    if (e.powerRaw >= 0) {
      float pct = e.powerRaw * 100.0 / 255.0;
      float watts = e.powerRaw / 255.0 * 10.0;
      f.printf("%lu;%d;%d;%.1f;%.2f\n", e.ms, e.alcMv, e.powerRaw, pct, watts);
    } else {
      f.printf("%lu;%d;-1;-;-\n", e.ms, e.alcMv);
    }
  }
  f.close();
  alcLogCount = 0;
}

// Zusaetzliches periodisches Sicherheitsnetz (2026-09-27): faellt waehrend
// einer laufenden Sendung die Versorgung der Platine kurz aus (z.B. wenn die
// PA-eigene Schutzschaltung bei einem gezielten Grenztest die eigene
// Spannung mitbeeinflusst - genau das, was wir mit dem naechsten FM-Sweep-
// Test bewusst provozieren), wuerde ein reiner "nur am TX-Ende flushen"-
// Ansatz ausgerechnet den interessantesten, noch nicht geflushten letzten
// Sekundenbruchteil verlieren. Deshalb zusaetzlich alle ALC_LOG_TIME_FLUSH_MS
// ein Zwischen-Flush, unabhaengig vom eigentlichen Burst-Ende - begrenzt den
// maximalen Datenverlust bei einem unerwarteten Reset auf wenige Sekunden
// statt des gesamten Bursts. Reine SPIFFS-Schreiblast, beruehrt PTT/GPIO26
// nicht.
const unsigned long ALC_LOG_TIME_FLUSH_MS = 2000;
unsigned long lastAlcTimeFlushMs = 0;

void logAlcSample() {
  if (alcLogCount >= ALC_LOG_FLUSH_THRESHOLD) {
    flushAlcLogToFile();
  }
  if (alcLogCount >= ALC_LOG_CAPACITY) return;  // sollte durch den Threshold-Flush nie erreicht werden
  alcLog[alcLogCount].ms = millis();
  alcLog[alcLogCount].alcMv = alcMv;
  alcLog[alcLogCount].powerRaw = havePower ? currentPowerRaw : -1;
  alcLogCount++;
  if (millis() - lastAlcTimeFlushMs >= ALC_LOG_TIME_FLUSH_MS) {
    lastAlcTimeFlushMs = millis();
    flushAlcLogToFile();
  }
}

// Watt-Umrechnung nur eine Annahme (externe DC-Versorgung, 705-Max 10W -
// dieselbe bereits an anderer Stelle in diesem Sketch dokumentierte
// Annahme wie bei TUNE_POWER_PERCENT). Bei Akkubetrieb waeren es nur 5W.
// Steht bereits in jeder Zeile der Datei selbst (siehe flushAlcLogToFile()),
// dumpAlcLog() gibt die Datei nur unveraendert ueber Serial aus.
void dumpAlcLog() {
  flushAlcLogToFile();  // ein gerade laufender Burst soll im Dump miterscheinen
  if (!spiffsOk) { Serial.println("ALCLOG: SPIFFS nicht verfuegbar"); return; }
  if (!SPIFFS.exists(ALC_LOG_PATH)) {
    Serial.println("ALCLOG: keine Datei vorhanden (noch nichts geloggt)");
    return;
  }
  fs::File f = SPIFFS.open(ALC_LOG_PATH, FILE_READ);
  Serial.printf("ALCLOG: %u Byte in %s (ms;alc_mV;leistung_raw;leistung_pct;leistung_W_bei_10W_extern)\n",
                (unsigned)f.size(), ALC_LOG_PATH);
  uint8_t buf[256];
  while (f.available()) {
    size_t n = f.read(buf, sizeof(buf));
    Serial.write(buf, n);
  }
  f.close();
  Serial.println("ALCLOG: Ende");
}

// ============================================================
// Automatischer ALC-Schutz (2026-09-27, BETA - erste Testversion, Schwellwerte
// noch nicht endgueltig kalibriert, siehe Chatverlauf/CLAUDE.md).
//
// Grundlage: FM-Sweep-Test zeigte einen scharfen Kollaps der PA-ALC-Spannung
// (GPIO36/Pin4) zwischen 5% und 6% Reglerstand, die PA-eigene Schutzschaltung
// loeste bei diesem Test bei ~8% aus. Bei SSB/Sprache ist ein kurzer tiefer
// Einbruch dagegen NORMAL (Sprachhuelle) und nicht gefaehrlich - erst eine
// ANHALTENDE Uebersteuerung ist dort aussagekraeftig. Deshalb zwei
// verschiedene Bewertungen je nach Modus:
//   - Konstanter Traeger (FM/AM/RTTY/RTTY-R/WFM/DV): Rohwert direkt, kurzer
//     Debounce (ALC_PROTECT_FM_DEBOUNCE_MS).
//   - Huellkurven-Modi (LSB/USB/CW/CW-R): "Leaky-Bucket"-Gefahren-Akkumulator
//     statt gleitendem Mittelwert.
//
// KORREKTUR (2026-09-27, echter Testbefund): Ein einfacher exponentiell
// gleitender Mittelwert ueber die Rohspannung hat sich als untauglich
// erwiesen - im echten Test (18s durchgehendes lautes Pfeifen bei 35%
// Leistung, deutlich ueber der FM-Fehlergrenze von 8%) ist der Mittelwert
// NIE unter die Schwelle gefallen, weil die kurzen Atempausen (Ruecksprung
// auf ~3118mV) ihn immer wieder hochgezogen haben, bevor er tief genug
// einschwingen konnte. Stattdessen jetzt ein Akkumulator nach dem Vorbild
// eines simplen thermischen Modells: steigt bei jedem 250ms-Tick UNTER der
// Schwelle schnell, faellt bei jedem Tick DARUEBER nur LANGSAM - dadurch
// summiert sich wiederholte, auch durch kurze Pausen unterbrochene Uebersteuerung
// auf, statt von den Pausen weggemittelt zu werden. Parameter empirisch gegen
// die echten Testdaten (Sweep bis 35% + 18s Dauerpfeifen bei 35%) kalibriert -
// loest dort nach 2,25s aus. Weiterhin ein erster Startwert, noch nicht mit
// normaler (leiserer/kuerzerer) Sprache auf Fehlalarme gegengeprueft.
//
// Reagiert mit GENAU EINEM Leistungsschritt nach unten pro Ausloesung (per
// 14 0A, reine SCHREIB-Aktion, bewusst NICHT ueber den blockierenden
// FB/NG-Bestaetigungsmechanismus der Tune-Sequenz - ein Fire-and-Forget-
// Write haelt den Hauptloop/PTT-Failsafe waehrend TX nicht auf; schlaegt der
// Schreibvorgang stillschweigend fehl, sind wir dadurch nicht schlechter
// gestellt als vorher, nur die Reduktion bleibt in diesem Zyklus aus und
// wird beim naechsten Ausloesen erneut versucht). KEIN automatisches
// Wiederhochfahren (sicherste Variante, Nutzerentscheidung) - der Bediener
// muss selbst wieder hochdrehen. Ein Cooldown verhindert mehrfaches
// Nachregeln innerhalb kurzer Zeit. FM/SSB haben bewusst EIGENE Schrittgroessen,
// da sich der Ausloesepunkt in raw-Einheiten stark unterscheiden kann.
//
// "alcprotect off"/"alcprotect on" per USB-Serial schaltet die gesamte
// Funktion um (Default: an) - fuer den Fall, dass sich waehrend eines Tests
// etwas unerwartet verhaelt und man das schnell abschalten will, ohne neu
// zu flashen.
// ============================================================
bool alcProtectEnabled = true;
const int ALC_PROTECT_THRESHOLD_MV = 500;
const unsigned long ALC_PROTECT_FM_DEBOUNCE_MS = 750;
const unsigned long ALC_PROTECT_COOLDOWN_MS = 4000;
const int ALC_PROTECT_FM_STEP_RAW  = 10;  // ~2% (Nutzertest 2026-09-27: Kollaps konstant bei
                                           // raw=16/6,3% -> 16-10=6/~2,4%)
const int ALC_PROTECT_SSB_STEP_RAW = 13;  // ~5% - noch ohne reale Ausloese-Rueckmeldung kalibriert,
                                           // SSB kann bei einem deutlich hoeheren Reglerstand
                                           // ausloesen als FM (im Test bis 90/35% erreicht)

// Leaky-Bucket-Parameter (SSB/CW) - siehe Kommentar oben. Bei 250ms-Takt:
// steigt um ACC_INC pro Tick unter der Schwelle, faellt um ACC_DEC pro Tick
// darueber, ausgeloest ab ACC_TRIGGER (z.B. 6 = grob 1,5s ununterbrochene
// Uebersteuerung, laenger falls durch Pausen unterbrochen).
const float ALC_PROTECT_SSB_ACC_INC     = 1.0;
const float ALC_PROTECT_SSB_ACC_DEC     = 0.3;
const float ALC_PROTECT_SSB_ACC_CAP     = 20.0;
const float ALC_PROTECT_SSB_ACC_TRIGGER = 6.0;

bool alcProtectFmLow = false;
unsigned long alcProtectFmLowSinceMs = 0;
float alcProtectSsbAcc = 0;
unsigned long alcProtectLastActionMs = 0;

bool isConstantCarrierMode(uint8_t mode) {
  switch (mode) {
    case 0x02: case 0x04: case 0x05: case 0x06: case 0x08: case 0x17:
      return true;   // AM, RTTY, FM, WFM, RTTY-R, DV
    default:
      return false;  // LSB, USB, CW, CW-R -> Huellkurven-Modi
  }
}

// Wird bei jedem TX-Beginn aufgerufen (siehe handleFrame()) - jede neue
// Sendung startet die Bewertung frisch, unabhaengig vom Verlauf der letzten.
void resetAlcProtectState() {
  alcProtectFmLow = false;
  alcProtectSsbAcc = 0;
}

void checkAlcProtect() {
  if (mode171 || !alcProtectEnabled) return;      // reine 705-Funktion
  if (txState != 1 || alcMv < 0) return;
  if (tuneRunning) return;                        // Tune-Sequenz regelt Leistung selbst
  if (!havePower || currentPowerRaw <= 0) return; // nichts mehr zu reduzieren

  bool triggered = false;
  unsigned long now = millis();
  bool constantCarrier = isConstantCarrierMode(modeCode);

  if (constantCarrier) {
    if (alcMv < ALC_PROTECT_THRESHOLD_MV) {
      if (!alcProtectFmLow) { alcProtectFmLow = true; alcProtectFmLowSinceMs = now; }
      if (now - alcProtectFmLowSinceMs >= ALC_PROTECT_FM_DEBOUNCE_MS) triggered = true;
    } else {
      alcProtectFmLow = false;
    }
  } else {
    if (alcMv < ALC_PROTECT_THRESHOLD_MV) {
      alcProtectSsbAcc += ALC_PROTECT_SSB_ACC_INC;
    } else {
      alcProtectSsbAcc -= ALC_PROTECT_SSB_ACC_DEC;
    }
    if (alcProtectSsbAcc < 0) alcProtectSsbAcc = 0;
    if (alcProtectSsbAcc > ALC_PROTECT_SSB_ACC_CAP) alcProtectSsbAcc = ALC_PROTECT_SSB_ACC_CAP;
    if (alcProtectSsbAcc >= ALC_PROTECT_SSB_ACC_TRIGGER) triggered = true;
  }

  if (triggered && (now - alcProtectLastActionMs >= ALC_PROTECT_COOLDOWN_MS)) {
    int step = constantCarrier ? ALC_PROTECT_FM_STEP_RAW : ALC_PROTECT_SSB_STEP_RAW;
    int newRaw = currentPowerRaw - step;
    if (newRaw < 0) newRaw = 0;
    Serial.printf("[%lums] ALC-SCHUTZ: Leistung %d -> %d (raw), Modus=%s, ALC=%dmV%s\n",
                  now, currentPowerRaw, newRaw, modeName(modeCode), alcMv,
                  constantCarrier ? "" : " (Akkumulator-Ausloesung)");
    sendPowerSetRaw(newRaw);
    currentPowerRaw = newRaw;  // optimistisch uebernehmen, naechste QUERY_POWER-Antwort bestaetigt/korrigiert
    alcProtectLastActionMs = now;
    // Nach einer Aktion frisch bewerten statt sofort erneut durch denselben
    // (jetzt schon reagierten) Zustand auszuloesen.
    alcProtectFmLow = false;
    alcProtectSsbAcc = 0;
  }
}

void updateAlc() {
  unsigned long now = millis();
  if (now - lastAlcReadMs < ALC_READ_INTERVAL_MS) return;
  lastAlcReadMs = now;
  alcMv = analogReadMilliVolts(PIN_ALC_ADC);
  if (txState == 1) {
    logAlcSample();
    checkAlcProtect();
  }
  if (now - lastAlcLogMs >= 2000) {
    lastAlcLogMs = now;
    Serial.printf("[%lums] ALC (GPIO36) = %d mV\n", now, alcMv);
  }
}

const int BAND_PWM_FREQ = 5000;
const int BAND_PWM_RES  = 10;
const int BAND_PWM_MAX  = (1 << BAND_PWM_RES) - 1;   // 1023 = volle 3,3 V

extern bool mode171;
extern long freqHz;            // weiter unten definiert (Anzeigezustand)
extern bool haveFreq;
extern int txState;            // weiter unten definiert - fuer den ALC-Logger oben gebraucht
extern int currentPowerRaw;
extern bool havePower;

int appliedBandIdx = -2;       // -2 = noch nie gesetzt, -1 = kein PA-Band (0 mV)

// Bench-Test-Ueberschreibung per USB-Serial ("band 20m"): haelt die
// Bandspannung unabhaengig von der 705-Frequenz fest, damit man sie ohne
// Funkgeraet-Bandwechsel nachmessen kann. Laeuft nach 5 Minuten automatisch
// ab (RAM-only, ueberlebt keinen Reboot) - eine vergessene Ueberschreibung
// wuerde sonst mit angeschlossener PA das falsche Filterband schalten.
bool bandTestOverride = false;
unsigned long bandTestUntil = 0;
const unsigned long BAND_TEST_MAX_MS = 300000;

void writeBandMillivolts(int mv) {
  if (mv < 0) mv = 0;
  long duty = ((long)mv * BAND_PWM_MAX) / 3300;
  if (duty > BAND_PWM_MAX) duty = BAND_PWM_MAX;
  ledcWrite(PIN_BAND_OUT, (uint32_t)duty);
  Serial.printf("  -> PWM-Duty %ld/%d (%d mV Ziel am Pin vor RC-Last)\n", duty, BAND_PWM_MAX, mv);
}

// idx = Index in BANDS[], oder -1 = ausserhalb der PA-Baender -> 0 mV.
void applyBandIndex(int idx, long hzForLog) {
  if (idx == appliedBandIdx) return;
  appliedBandIdx = idx;
  if (idx >= 0) {
    const BandDef& b = BANDS[idx];
    Serial.printf("BAND: %s (%ld Hz) Soll %d mV + Korr %d mV = %d mV\n",
                  b.name, hzForLog, b.bandMv, b.corrMv, b.bandMv + b.corrMv);
    writeBandMillivolts(b.bandMv + b.corrMv);
  } else {
    Serial.printf("BAND: kein PA-Band (%ld Hz) -> 0 mV\n", hzForLog);
    writeBandMillivolts(0);
  }
}

void updateBandVoltage(long hz) {
  if (bandTestOverride) return;
  const BandDef* b = findBand(hz);
  applyBandIndex(b ? (int)(b - BANDS) : -1, hz);
}

// ------------------------------------------------------------
// PTT-Ausgang (GPIO26 -> Optokoppler -> PA-PTT). Folgt dem TX-Zustand des
// 705. Quellen (Messung 2026-09-18, siehe CLAUDE.md): nach "24 00 00 01"
// (dl1bz "fastPTT") meldet das 705 selbst bei Sendebeginn "24 00 01 01"
// und bei Ende "24 00 01 00" (spaetestens gleichzeitig mit dem Polling,
// meist 30-80 ms frueher). Zusaetzlich fragen wir "1C 00" alle 100 ms ab
// (Rueckfallebene, falls fastPTT nach einem Radio-Neustart aus ist).
// Wer zuletzt meldet, gewinnt.
// FAIL-SAFE: PTT ist nur erlaubt, wenn die Frequenz bekannt und in einem
// PA-Band ist und KEIN Bandspannungs-Test laeuft. Bei BT-Abbruch oder
// Funkstille des Radios waehrend TX faellt PTT sofort auf LOW.
// ------------------------------------------------------------
bool pttOut = false;
unsigned long lastRadioFrameMs = 0;
const unsigned long PTT_SILENCE_LIMIT_MS = 600;   // Poll laeuft alle 100 ms

bool pttAllowed() {
  return haveFreq && !bandTestOverride && findBand(freqHz) != nullptr;
}

void setPtt(bool on, const char* why) {
  if (on == pttOut) return;
  pttOut = on;
  digitalWrite(PIN_PTT_OUT, on ? HIGH : LOW);
  Serial.printf("[%lums] PTT GPIO26 -> %s (%s)\n", millis(), on ? "HIGH/TX" : "LOW/RX", why);
}

void applyPttForTx(bool tx, const char* src) {
  if (tx && !pttAllowed()) {
    if (!pttOut) Serial.printf("[%lums] PTT verweigert (%s): kein PA-Band/Frequenz unbekannt/Band-Test\n", millis(), src);
    setPtt(false, "nicht erlaubt");
    return;
  }
  setPtt(tx, src);
}

// requireRecentFrame=false in blockierenden Warteschleifen (Tune-Haltezeit),
// in denen das Radio absichtlich keine Frames liefert.
void pttFailsafeCheck(bool requireRecentFrame) {
  if (!pttOut) return;
  if (!SerialBT.hasClient()) { setPtt(false, "Failsafe: BT getrennt"); return; }
  if (requireRecentFrame && millis() - lastRadioFrameMs > PTT_SILENCE_LIMIT_MS) {
    setPtt(false, "Failsafe: Funkstille");
  }
}

// USB-Serial-Kommandos (115200, Zeilenende \n):
//   band <160m|80m|60m|40m|30m|20m|17m|15m|12m|10m|6m>  Bandspannung festhalten
//   band zero                                           0 mV festhalten
//   band auto                                           zurueck zur 705-Frequenz
String serialLine;

// PTT-Diagnose (2026-09-18, reine Messung - veraendert GPIO26 NICHT):
//   dbg on|off       jeden eingehenden 705-Frame mit Zeitstempel loggen
//   txpoll <ms>      TX-Status (1C 00) zusaetzlich alle <ms> abfragen (50..2000)
//   fastptt on       dl1bz-"fastPTT": Cmd 24 00 00 01 (TX-Output-Power-Setting
//                    ON, laut Handbuch S. "24 00 00"). Wird vom Radio beim
//                    Ausschalten wieder auf OFF gesetzt (Fussnote *7).
//   alclog dump      komplette ALC+Leistungs-Datei ausgeben (siehe oben,
//                    ueberlebt Stromausfaelle/Reboots zwischen Tests)
//   alclog clear     Datei + laufenden Burst leeren (fuer eine neue Testreihe)
//   alclog status    Dateigroesse + noch nicht geflushten Burst anzeigen
//   alcprotect off   automatischen ALC-Schutz (siehe dort) abschalten
//   alcprotect on    automatischen ALC-Schutz wieder einschalten (Default: an)
bool dbgFrames = false;
unsigned long txPollMs = 100;        // 0 = nur der normale 2-s-Takt
unsigned long lastFastTxPoll = 0;
const uint8_t CMD_FASTPTT_ON[] = {0xFE, 0xFE, RADIO_ADDR, CTRL_ADDR, 0x24, 0x00, 0x00, 0x01, 0xFD};

void handleSerialCommand(String cmd) {
  cmd.trim();
  cmd.toLowerCase();
  if (cmd == "dbg on")  { dbgFrames = true;  Serial.println("DBG: Frame-Log an");  return; }
  if (cmd == "dbg off") { dbgFrames = false; Serial.println("DBG: Frame-Log aus"); return; }
  if (cmd == "fastptt on") {
    SerialBT.write(CMD_FASTPTT_ON, sizeof(CMD_FASTPTT_ON));
    Serial.printf("[%lums] FASTPTT: 24 00 00 01 gesendet\n", millis());
    return;
  }
  if (cmd == "alclog dump") { dumpAlcLog(); return; }
  if (cmd == "alcprotect off") { alcProtectEnabled = false; Serial.println("ALC-SCHUTZ: aus"); return; }
  if (cmd == "alcprotect on")  { alcProtectEnabled = true;  Serial.println("ALC-SCHUTZ: an");  return; }
  if (cmd == "alclog clear") {
    alcLogCount = 0;   // laufenden Burst verwerfen statt noch anzuhaengen
    bool removed = spiffsOk ? SPIFFS.remove(ALC_LOG_PATH) : false;
    Serial.printf("ALCLOG: geleert (RAM + Datei%s)\n", removed ? "" : ", Datei war bereits leer/nicht vorhanden");
    return;
  }
  if (cmd == "alclog status") {
    size_t fsize = 0;
    if (spiffsOk && SPIFFS.exists(ALC_LOG_PATH)) {
      fs::File f = SPIFFS.open(ALC_LOG_PATH, FILE_READ);
      fsize = f.size();
      f.close();
    }
    Serial.printf("ALCLOG: %u Byte in Datei, %d Eintraege im aktuellen Burst noch nicht geflusht%s\n",
                  (unsigned)fsize, alcLogCount, spiffsOk ? "" : " [SPIFFS NICHT VERFUEGBAR]");
    return;
  }
  if (cmd.startsWith("txpoll ")) {
    long v = cmd.substring(7).toInt();
    if (v == 0) { txPollMs = 0; Serial.println("TXPOLL: aus (nur 2-s-Takt)"); }
    else {
      if (v < 50) v = 50;
      if (v > 2000) v = 2000;
      txPollMs = v;
      Serial.printf("TXPOLL: alle %lu ms\n", txPollMs);
    }
    return;
  }
  if (!cmd.startsWith("band ")) return;
  String arg = cmd.substring(5);
  arg.trim();
  if (arg == "auto") {
    bandTestOverride = false;
    appliedBandIdx = -2;   // erzwingt Neuberechnung
    Serial.println("BAND-TEST: aus, folge wieder der 705-Frequenz");
    if (haveFreq) updateBandVoltage(freqHz);
    return;
  }
  int idx = -3;
  if (arg == "zero") {
    idx = -1;
  } else {
    for (int i = 0; i < NUM_BANDS; i++) {
      if (arg == String(BANDS[i].name)) { idx = i; break; }
    }
  }
  if (idx == -3) {
    Serial.println("BAND-TEST: unbekanntes Band");
    return;
  }
  setPtt(false, "Band-Test gestartet");
  bandTestOverride = true;
  bandTestUntil = millis() + BAND_TEST_MAX_MS;
  appliedBandIdx = -2;     // erzwingt Ausgabe
  Serial.println("BAND-TEST: aktiv (max. 5 min)");
  applyBandIndex(idx, idx >= 0 ? BANDS[idx].lowHz : 0);
}

// --- CI-V-Frame-Parser (einfacher Bytestrom-Automat) ---
uint8_t frameBuf[32];
uint8_t frameLen = 0;
bool inFrame = false;
uint8_t feCount = 0;

// --- Anzeigezustand ---
long freqHz = -1;
uint8_t modeCode = 0xFF;      // 0xFF = unbekannt
bool haveFreq = false;
bool haveMode = false;
int txState = -1;             // -1=unbekannt, 0=RX, 1=TX (nur ueber Lesabfragen/Broadcasts, nie gesetzt!)

// Uhr: lokale Zeit + UTC-Offset getrennt gehalten, UTC wird daraus
// berechnet (das Radio liefert nur die lokale, vom Nutzer eingestellte
// Zeit direkt - Nutzerwunsch war ausdruecklich UTC).
int localMinutes = -1;        // Minuten seit Mitternacht, lokale 705-Zeit
int utcOffsetMinutes = -1;    // Betrag des Offsets (immer positiv)
int utcOffsetSign = 0;        // +1 oder -1 (Richtung: 00=+ radio-lokal ist UTC+Offset)
bool haveUtcOffset = false;

bool wasConnected = false;
unsigned long lastQueryToggle = 0;
unsigned long bootMillis = 0;
bool didBootReconnectAttempt = false;
// Nutzerbeobachtung (2026-09-11): nach dem Booten verbindet sich das 705
// oft kurz von selbst, meldet dann aber Disconnect (bisher musste dann
// jedesmal manuell per langem Tastendruck nachgeholfen werden). Ein
// einziger automatischer Versuch innerhalb dieses Zeitfensters spart das.
const unsigned long BOOT_RECONNECT_WINDOW_MS = 30000;
bool queryFreqNext = true;    // wechselt sich mit Modus ab
bool screenDirty = true;

// --- Bestaetigungs-/Leistungs-Tracking fuer die Tune-Sequenz ---
volatile bool ackReceived = false;
volatile bool ackOk = false;
int currentPowerRaw = -1;     // 0-255, aus Cmd 14 0A
bool havePower = false;
bool modeReadFresh = false;   // frisch (innerhalb der Tune-Sequenz) gelesener Modus
bool tuneRunning = false;     // verhindert doppelte/verschachtelte Ausloesung

int smeterRaw = -1;
bool haveSMeter = false;
bool showSMeterView = false;  // per langem Druck auf den unteren Taster umschaltbar
int freqViewTextSize = 1;     // per fitTextSize() in setup() berechnet
int sMeterViewTextSize = 1;

// --- Taster (TEST-Phase: physische Oben/Unten-Zuordnung zu den GPIOs noch
// nicht bestaetigt, siehe Kommentar in setup()). Beide GPIOs sind laut
// LilyGO-Doku aktiv-LOW (gedrueckt = auf GND gezogen).
const int BTN_GPIO0  = 0;
const int BTN_GPIO35 = 35;
bool displayOn = true;
bool lastBtn0State = HIGH;
bool lastBtn35State = HIGH;
unsigned long btn0PressStart = 0;
const unsigned long LONG_PRESS_MS = 700;  // ab hier gilt ein Druck als "lang"
unsigned long lastButtonMillis = 0;
const unsigned long BUTTON_DEBOUNCE_MS = 80;   // reine Elektrik-Entprellung, bewusst kurz
                                                // gehalten, damit ein absichtliches schnelles
                                                // Mehrfachklicken (siehe unten) nicht verschluckt wird

// Mehrfachklick-Erkennung NUR fuer den oberen Taster (Tune-Trigger,
// GPIO35) - Nutzerwunsch 2026-09-11: 1x=3s, 2x=6s, 3x(oder mehr, gedeckelt)=9s
// Sendedauer. Klicks werden gesammelt, bis MULTI_CLICK_WINDOW_MS ohne
// weiteren Klick vergangen ist, erst dann wird die Sequenz ausgeloest.
int tuneClickCount = 0;
unsigned long lastTuneClickTime = 0;
const unsigned long MULTI_CLICK_WINDOW_MS = 500;
const int MAX_TUNE_CLICKS = 3;

void toggleDisplay() {
  displayOn = !displayOn;
  digitalWrite(TFT_BL, displayOn ? HIGH : LOW);
}

// Langer Druck auf den unteren Taster (2026-09-11, Nutzerwunsch): aktiv
// eine Bluetooth-Verbindung zum 705 aufbauen, falls Auto-Connect (radio-
// seitig unzuverlaessig, siehe CLAUDE.md) gerade nicht gegriffen hat.
// Nutzt die bereits gebondete MAC-Adresse direkt statt einer namens-
// basierten Suche - letztere wuerde eine frische Sichtbarkeit des 705
// voraussetzen (Inquiry-Scan), die im Normalbetrieb nicht gegeben ist.
void reconnectToRadio() {
  Serial.printf("[%lums] reconnectToRadio() aufgerufen\n", millis());
  esp_bd_addr_t bondedDevices[5];
  int count = SerialBT.getBondedDevices(5, bondedDevices);
  if (count <= 0) {
    Serial.println("Reconnect: keine gebondeten Geraete gefunden.");
    return;
  }
  Serial.printf("Reconnect: %d gebondete(s) Geraet(e), verbinde mit dem ersten...\n", count);
  bool ok = SerialBT.connect(bondedDevices[0]);
  Serial.printf("[%lums] connect() zurueck, Ergebnis: %s\n", millis(), ok ? "true" : "false");
}

const char* modeName(uint8_t code) {
  switch (code) {
    case 0x00: return "LSB";
    case 0x01: return "USB";
    case 0x02: return "AM";
    case 0x03: return "CW";
    case 0x04: return "RTTY";
    case 0x05: return "FM";
    case 0x06: return "WFM";
    case 0x07: return "CW-R";
    case 0x08: return "RTTY-R";
    case 0x17: return "DV";
    default:   return "?";
  }
}

// Kalibrierung laut offiziellem CI-V-Referenzhandbuch (bereits etabliert
// in band_sync.py auf raspberryhf): 0000=S0, 0120=S9, 0241=S9+60dB.
// Oberhalb S9 hier bewusst in 5dB-Schritten (Nutzerwunsch 2026-09-11),
// feiner als die 10dB-Stufen, die anderswo im Projekt genutzt werden -
// gilt nur fuer DIESE Anzeige.
String sMeterString(int raw) {
  if (raw < 120) {
    int level = (int)round(raw / 120.0 * 9.0);
    if (level < 1) level = 1;
    if (level > 9) level = 9;
    return "S" + String(level);
  }
  float dbOverS9 = (raw - 120) / 121.0 * 60.0;
  int rounded5 = ((int)round(dbOverS9 / 5.0)) * 5;
  if (rounded5 < 0) rounded5 = 0;
  if (rounded5 > 30) rounded5 = 30;
  if (rounded5 == 0) return "S9";
  return "S9+" + String(rounded5);
}

// Ermittelt die groesste setTextSize-Stufe (Standardfont), bei der der
// angegebene Text noch in die Displaybreite (abzueglich kleinem Rand)
// passt - per echter Breitenmessung (tft.textWidth()), nicht geschaetzt.
int fitTextSize(const char* text, int maxSize) {
  // Rand bewusst knapp gehalten (2px) - bei der Standardschrift springt
  // die Breite in relativ grossen Stufen (6px/Zeichen UND Groessenstufe),
  // ein zu grosszuegiger Rand kann daher leicht eine ganze nutzbare
  // Groessenstufe verschenken (2026-09-11, Nutzerfund).
  int best = 1;
  for (int s = 1; s <= maxSize; s++) {
    tft.setTextSize(s);
    if (tft.textWidth(text) <= tft.width() - 2) {
      best = s;
    } else {
      break;
    }
  }
  return best;
}

// 5-Byte-BCD (MSB zuerst, je Byte High-Nibble dann Low-Nibble) -> Hz.
// Exakt dasselbe Schema wie ueberall sonst in diesem Projekt (band_sync.py).
long decodeFreqBcd(const uint8_t* d) {
  long hz = 0;
  for (int i = 4; i >= 0; i--) {
    hz = hz * 100 + ((d[i] >> 4) * 10) + (d[i] & 0x0F);
  }
  return hz;
}

// Ein Byte, das zwei BCD-Ziffern traegt (z.B. 0x14 -> 14).
int bcdByteToInt(uint8_t b) {
  return (b >> 4) * 10 + (b & 0x0F);
}

// Zwei Bytes, die zusammen eine 4-stellige BCD-Zahl (0000-0255) tragen -
// fuer Cmd 14 0A (Leistung). hi traegt die ersten zwei, lo die letzten
// zwei Dezimalstellen.
int bcd2BytesToInt(uint8_t hi, uint8_t lo) {
  return (hi >> 4) * 1000 + (hi & 0x0F) * 100 + (lo >> 4) * 10 + (lo & 0x0F);
}
void intToBcd2Bytes(int value, uint8_t* hi, uint8_t* lo) {
  int d3 = (value / 1000) % 10, d2 = (value / 100) % 10;
  int d1 = (value / 10) % 10,   d0 = value % 10;
  *hi = (d3 << 4) | d2;
  *lo = (d1 << 4) | d0;
}

void sendQuery(const uint8_t* q, size_t len) {
  SerialBT.write(q, len);
}

void handleFrame() {
  // frameBuf[0]=to, [1]=from, [2]=cmd, [3..]=data
  if (frameLen < 3) return;
  uint8_t from = frameBuf[1];
  uint8_t cmd  = frameBuf[2];
  uint8_t dataLen = frameLen - 3;

  if (from != RADIO_ADDR) return;  // nur Antworten/Broadcasts vom 705 selbst
  lastRadioFrameMs = millis();

  if (dbgFrames) {
    Serial.printf("[%lums] RX to=%02X cmd=%02X data=", millis(), frameBuf[0], cmd);
    for (int i = 3; i < frameLen; i++) Serial.printf("%02X ", frameBuf[i]);
    Serial.println();
  }

  if (cmd == 0x03 && dataLen >= 5) {
    freqHz = decodeFreqBcd(&frameBuf[3]);
    haveFreq = true;
    screenDirty = true;
    updateBandVoltage(freqHz);
  } else if (cmd == 0x00 && dataLen >= 5) {
    // Unaufgefordertes Transceive-Broadcast bei Frequenzaenderung
    freqHz = decodeFreqBcd(&frameBuf[3]);
    haveFreq = true;
    screenDirty = true;
    updateBandVoltage(freqHz);
  } else if (cmd == 0x04 && dataLen >= 1) {
    modeCode = frameBuf[3];
    haveMode = true;
    modeReadFresh = true;
    screenDirty = true;
  } else if (cmd == 0x1C && dataLen >= 2 && frameBuf[3] == 0x00) {
    // Antwort auf unsere Lesabfrage ODER unaufgefordertes Broadcast -
    // wir SENDEN dieses Kommando selbst nie mit einem dritten Datenbyte.
    int newTx = frameBuf[4];
    if (newTx != txState) {
      if (dbgFrames) Serial.printf("[%lums] TXSTATE %d -> %d\n", millis(), txState, newTx);
      txState = newTx;
      screenDirty = true;
      if (newTx == 1) {
        // TX-Beginn: sofortige Leistungsabfrage erzwingen statt bis zu
        // POWER_QUERY_INTERVAL_MS im naechsten loop() zu warten (siehe dort).
        lastPowerQueryMs = 0;
        resetAlcProtectState();  // jede neue Sendung frisch bewerten, siehe dort
      } else {
        // TX-Ende: den waehrend dieses Bursts im RAM gesammelten ALC-Log
        // jetzt dauerhaft sichern (siehe Kommentar am ALC-Datenlogger oben) -
        // bewusst NICHT waehrend TX selbst, um das PTT-Timing nicht zu stoeren.
        flushAlcLogToFile();
      }
    }
    applyPttForTx(newTx == 1, "poll");
  } else if (cmd == 0x24 && dataLen >= 3 && frameBuf[3] == 0x00 && frameBuf[4] == 0x01) {
    // fastPTT-Push des 705: 24 00 01 01 = TX-Beginn, 24 00 01 00 = TX-Ende.
    int newTx = (frameBuf[5] == 0x01) ? 1 : 0;
    if (newTx != txState) {
      txState = newTx;
      screenDirty = true;
      if (newTx == 1) { lastPowerQueryMs = 0; resetAlcProtectState(); }
      else flushAlcLogToFile();
    }
    applyPttForTx(newTx == 1, "push");
  } else if (cmd == 0x1A && dataLen >= 3 && frameBuf[3] == 0x05) {
    // frameBuf[4..5] = Sub-Sub-Kommando (2-Byte-BCD), frameBuf[6..] = Daten
    uint8_t subHi = frameBuf[4], subLo = frameBuf[5];
    uint8_t subDataLen = frameLen - 6;
    if (subHi == 0x01 && subLo == 0x66 && subDataLen >= 2) {
      // Uhrzeit (lokal!): 2 Byte BCD = HH, MM
      localMinutes = bcdByteToInt(frameBuf[6]) * 60 + bcdByteToInt(frameBuf[7]);
      screenDirty = true;
    } else if (subHi == 0x01 && subLo == 0x70 && subDataLen >= 3) {
      // UTC-Offset: 2 Byte BCD (HH,MM) + 1 Byte Richtung (00=+, 01=-)
      utcOffsetMinutes = bcdByteToInt(frameBuf[6]) * 60 + bcdByteToInt(frameBuf[7]);
      utcOffsetSign = (frameBuf[8] == 0x00) ? 1 : -1;
      haveUtcOffset = true;
      screenDirty = true;
    }
  } else if (cmd == 0x15 && dataLen >= 3 && frameBuf[3] == 0x02) {
    // Antwort auf S-Meter-Lesabfrage (Cmd 15 02): frameBuf[4..5] = 2-Byte-BCD (0-255)
    smeterRaw = bcd2BytesToInt(frameBuf[4], frameBuf[5]);
    haveSMeter = true;
    screenDirty = true;
  } else if (cmd == 0x14 && dataLen >= 3 && frameBuf[3] == 0x0A) {
    // Antwort auf Leistungs-Lesabfrage (Cmd 14 0A): frameBuf[4..5] = 2-Byte-BCD (0-255).
    // Bewusst NICHT ueber den ackReceived/ackOk-Mechanismus geroutet (der
    // ist ausschliesslich fuer echte FB/FA-SET-Bestaetigungen reserviert,
    // sonst koennte eine ganz normale Hintergrund-Leseantwort faelschlich
    // als Bestaetigung eines WAERHEND der Tune-Sequenz gesendeten SET-
    // Kommandos interpretiert werden) - eigene havePower-Flagge stattdessen.
    currentPowerRaw = bcd2BytesToInt(frameBuf[4], frameBuf[5]);
    havePower = true;
  } else if (cmd == 0xFB) {
    // Icom-Standard-Bestaetigung: Kommando erfolgreich ausgefuehrt. Kommt
    // laut Protokoll AUSSCHLIESSLICH als Antwort auf ein von uns gesendetes
    // SET-Kommando zurueck, nie auf eine reine Lesabfrage - daher hier
    // sicher als generische Bestaetigung fuer die Tune-Sequenz nutzbar.
    ackReceived = true;
    ackOk = true;
  } else if (cmd == 0xFA) {
    // Icom-Standard-Bestaetigung: Kommando ABGELEHNT.
    ackReceived = true;
    ackOk = false;
  }
}

void feedByte(uint8_t b) {
  if (b == 0xFE) {
    feCount++;
    if (feCount >= 2) {
      inFrame = true;
      frameLen = 0;
    }
    return;
  }
  feCount = 0;
  if (!inFrame) return;

  if (b == 0xFD) {
    handleFrame();
    inFrame = false;
    frameLen = 0;
    return;
  }
  if (frameLen < sizeof(frameBuf)) {
    frameBuf[frameLen++] = b;
  }
}

void drawStatic() {
  tft.fillScreen(TFT_BLACK);
  tft.fillRoundRect(0, 0, tft.width(), 26, 5, TFT_MAROON);
  tft.drawRoundRect(0, 0, tft.width(), 26, 5, TFT_WHITE);
  tft.setTextColor(TFT_YELLOW, TFT_MAROON);
  tft.setTextSize(2);
  tft.setCursor(8, 6);
  tft.print(mode171 ? "PMR-171" : "IC-705");
}

// Rechtes, farbiges Quadrat mit "RX"/"TX" - Layout wie beim keimoase.de-
// Projekt beschrieben (Nutzerwunsch 2026-09-11).
void drawTxIndicator() {
  const int x = 180, y = 78, w = 55, h = 55;
  uint16_t bg;
  const char* label;
  uint16_t fg = TFT_WHITE;
  if (txState == 1) {
    bg = TFT_RED;
    label = "TX";
  } else if (txState == 0) {
    bg = TFT_GREEN;
    label = "RX";
    fg = TFT_BLACK;
  } else {
    bg = TFT_DARKGREY;
    label = "?";
  }
  tft.fillRoundRect(x, y, w, h, 6, bg);
  tft.drawRoundRect(x, y, w, h, 6, TFT_WHITE);
  tft.setTextColor(fg, bg);

  // Waehrend TX zusaetzlich den ALC-Wert der PA anzeigen (Pin4, GPIO36,
  // siehe updateAlc() oben) - nur in diesem Zustand interessant, RX/?
  // behalten das urspruengliche einzeilige, vertikal zentrierte Layout,
  // damit an diesen (haeufigeren) Zustaenden nichts veraendert wird.
  if (txState == 1 && alcMv >= 0) {
    tft.setTextSize(2);
    int textW = strlen(label) * 12;
    tft.setCursor(x + (w - textW) / 2, y + 8);
    tft.print(label);

    char alcStr[12];
    snprintf(alcStr, sizeof(alcStr), "%d mV", alcMv);
    tft.setTextSize(1);
    int alcW = strlen(alcStr) * 6;  // Size-1-GLCD-Font ~6px/Zeichen
    tft.setCursor(x + (w - alcW) / 2, y + 36);
    tft.print(alcStr);
  } else {
    tft.setTextSize(2);
    int textW = strlen(label) * 12;  // Size-2-GLCD-Font ~12px/Zeichen
    tft.setCursor(x + (w - textW) / 2, y + (h - 16) / 2);
    tft.print(label);
  }
}

void drawClock() {
  // Mittig in der Titelzeile, gleiche Groesse/Font wie "IC-705"
  // (Nutzerwunsch: "so gross wie das 705 in der ersten Zeile"). Zeigt
  // ausschliesslich UTC, aus 705-lokaler Zeit minus/plus Offset berechnet.
  tft.setTextSize(2);
  tft.fillRect(82, 4, 64, 18, TFT_MAROON);
  tft.setTextColor(TFT_WHITE, TFT_MAROON);
  tft.setCursor(84, 6);
  if (localMinutes >= 0 && haveUtcOffset) {
    int utcMinutes = localMinutes - (utcOffsetSign * utcOffsetMinutes);
    utcMinutes = ((utcMinutes % 1440) + 1440) % 1440;
    tft.printf("%02d:%02d", utcMinutes / 60, utcMinutes % 60);
  } else {
    tft.print("--:--");
  }
}

// Alternative Ansicht (2026-09-11, Nutzerwunsch): grosse Frequenz +
// digitales S-Meter, umschaltbar per langem Druck auf den unteren
// Taster. Schriftgroessen werden einmalig in setup() gegen den jeweils
// laengsten realistischen Fall passend berechnet (fitTextSize()), damit
// die Zeilen bei jeder Frequenz/jedem S-Wert gleich gross bleiben.
void renderSMeterView() {
  tft.fillRect(0, 30, tft.width(), 105, TFT_BLACK);

  char freqStr[20];
  if (haveFreq) {
    snprintf(freqStr, sizeof(freqStr), "%.5f MHz", freqHz / 1000000.0);
  } else {
    snprintf(freqStr, sizeof(freqStr), "---.----- MHz");
  }
  tft.setTextSize(freqViewTextSize);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  int w1 = tft.textWidth(freqStr);
  tft.setCursor((tft.width() - w1) / 2, 42);
  tft.print(freqStr);

  char sStr[24];
  snprintf(sStr, sizeof(sStr), "Signal: %s", haveSMeter ? sMeterString(smeterRaw).c_str() : "-");
  tft.setTextSize(sMeterViewTextSize);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  int w2 = tft.textWidth(sStr);
  tft.setCursor((tft.width() - w2) / 2, 100);
  tft.print(sStr);
}

void render() {
  drawClock();

  // BT-Status oben rechts
  tft.setTextSize(1);
  tft.fillRect(150, 4, 85, 18, TFT_MAROON);
  if (SerialBT.hasClient()) {
    tft.setTextColor(TFT_GREEN, TFT_MAROON);
    tft.setCursor(150, 9);
    tft.print("BT CI-V OK");
  } else {
    tft.setTextColor(TFT_WHITE, TFT_MAROON);
    tft.setCursor(150, 9);
    tft.print("BT wartet..");
  }

  if (showSMeterView) {
    renderSMeterView();
    return;
  }

  // Frequenz gross, MHz direkt dahinter
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.fillRect(0, 30, tft.width(), 45, TFT_BLUE);
  tft.setTextColor(TFT_WHITE, TFT_BLUE);
  tft.setCursor(6, 62);
  if (haveFreq) {
    tft.printf("%.5f MHz", freqHz / 1000000.0);
  } else {
    tft.print("---.----- MHz");
  }

  // Band + Modus, UNTEREINANDER, aber Band wieder auf normale Groesse
  // zurueckgesetzt (die vorherige Verdopplung liess den Text nach oben in
  // die Frequenzbox hineinragen, da die Cursor-Y-Position bei GFX-Fonts
  // die TEXT-BASISLINIE ist, nicht die obere Kante - bei doppelter
  // Schriftgroesse reicht der Zeichenanfang dadurch entsprechend weiter
  // nach oben als erwartet). Bleibt trotzdem prominenter als zuvor, da es
  // jetzt eine eigene Zeile bekommt statt sich eine mit Modus zu teilen.
  tft.fillRect(0, 78, 176, 55, TFT_BLACK);
  const BandDef* band = haveFreq ? findBand(freqHz) : nullptr;

  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(4, 108);
  tft.print(band ? band->name : "-");
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setCursor(78, 108);
  tft.print(haveMode ? modeName(modeCode) : "-");
  tft.setFreeFont(NULL);

  drawTxIndicator();
}


// ============================================================
// Tune-Sequenz (2026-09-11) - siehe Sicherheits-Kopfkommentar.
// ============================================================

bool blockingWaitForAck(unsigned long timeoutMs) {
  ackReceived = false;
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    while (SerialBT.available()) feedByte((uint8_t)SerialBT.read());
    if (ackReceived) return ackOk;
    delay(5);
  }
  return false;  // Timeout = keine Bestaetigung = im Zweifel als Fehlschlag werten
}

bool blockingWaitForPowerRead(unsigned long timeoutMs) {
  havePower = false;
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    while (SerialBT.available()) feedByte((uint8_t)SerialBT.read());
    if (havePower) return true;
    delay(5);
  }
  return false;
}

bool blockingWaitForModeRead(unsigned long timeoutMs) {
  modeReadFresh = false;
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    while (SerialBT.available()) feedByte((uint8_t)SerialBT.read());
    if (modeReadFresh) return true;
    delay(5);
  }
  return false;
}

void sendPttSet(bool on) {
  uint8_t cmd[] = {0xFE, 0xFE, RADIO_ADDR, CTRL_ADDR, 0x1C, 0x00, (uint8_t)(on ? 0x01 : 0x00), 0xFD};
  SerialBT.write(cmd, sizeof(cmd));
}

void sendPowerSetRaw(int raw) {
  uint8_t hi, lo;
  intToBcd2Bytes(raw, &hi, &lo);
  uint8_t cmd[] = {0xFE, 0xFE, RADIO_ADDR, CTRL_ADDR, 0x14, 0x0A, hi, lo, 0xFD};
  SerialBT.write(cmd, sizeof(cmd));
}

void sendPowerSetPercent(int percent) {
  sendPowerSetRaw((percent * 255) / 100);
}

void sendModeSet(uint8_t mode) {
  uint8_t cmd[] = {0xFE, 0xFE, RADIO_ADDR, CTRL_ADDR, 0x06, mode, 0xFD};
  SerialBT.write(cmd, sizeof(cmd));
}

// Notbremse: volle Geraete-Abschaltung (Cmd 18 00) - staerkste verfuegbare
// Absicherung, wirkt unabhaengig vom Software-PTT-Zustand. Exakt dasselbe
// Kommando, das die bestehende 705-Notfall-Abschaltung auf dem
// homeassistant-Host nutzt.
void emergencyRadioOff() {
  SerialBT.write(CMD_POWER_OFF, sizeof(CMD_POWER_OFF));
}

// Versucht PTT mehrfach bestaetigt auszuschalten, eskaliert bei
// anhaltendem Misserfolg auf die volle Geraete-Abschaltung.
void safePttOff() {
  for (int i = 0; i < TUNE_PTT_OFF_MAX_RETRIES; i++) {
    sendPttSet(false);
    if (blockingWaitForAck(TUNE_ACK_TIMEOUT_MS)) return;  // bestaetigt aus
  }
  emergencyRadioOff();
}

// Kompletter Tune-Ablauf, ausgeloest durch den oberen Taster (GPIO35).
// Blockierend (haelt den Hauptloop fuer die Dauer der Sequenz an) - bei
// einer seltenen, bewusst ausgeloesten Aktion von wenigen Sekunden Dauer
// ein akzeptabler, einfacher und gut nachvollziehbarer Kompromiss
// gegenueber einer komplexeren nebenlaeufigen Zustandsmaschine.
void runTuneSequence(unsigned long holdMs) {
  if (tuneRunning) return;  // keine verschachtelte Ausloesung
  tuneRunning = true;
  unsigned long seqStart = millis();
  bool restorePower = false, restoreMode = false;
  int savedPowerRaw = -1;
  uint8_t savedMode = 0xFF;

  // Schritt 1: aktuelle Leistung merken.
  sendQuery(QUERY_POWER, sizeof(QUERY_POWER));
  if (!blockingWaitForPowerRead(TUNE_ACK_TIMEOUT_MS)) { tuneRunning = false; return; }
  savedPowerRaw = currentPowerRaw;

  // Schritt 2: aktuellen Modus merken.
  sendQuery(QUERY_MODE, sizeof(QUERY_MODE));
  if (!blockingWaitForModeRead(TUNE_ACK_TIMEOUT_MS)) { tuneRunning = false; return; }
  savedMode = modeCode;

  // Schritt 3: Leistung auf Tune-Wert setzen (50% - siehe Konstanten-
  // Kommentar oben zur Annahme "externe Stromversorgung").
  sendPowerSetPercent(TUNE_POWER_PERCENT);
  if (!blockingWaitForAck(TUNE_ACK_TIMEOUT_MS)) { tuneRunning = false; return; }  // nichts veraendert -> nichts wiederherzustellen
  restorePower = true;

  // Schritt 4: Modus auf CW setzen.
  sendModeSet(TUNE_MODE);
  if (!blockingWaitForAck(TUNE_ACK_TIMEOUT_MS)) {
    if (restorePower) { sendPowerSetRaw(savedPowerRaw); blockingWaitForAck(TUNE_ACK_TIMEOUT_MS); }
    tuneRunning = false;
    return;
  }
  restoreMode = true;

  // Schritt 5: PTT einschalten.
  sendPttSet(true);
  if (!blockingWaitForAck(TUNE_ACK_TIMEOUT_MS)) {
    // Nicht bestaetigt - sicherheitshalber TROTZDEM ein PTT-Aus hinter-
    // herschicken (schadet nie, auch falls PTT nie wirklich anging),
    // dann Modus/Leistung wiederherstellen.
    safePttOff();
    if (restoreMode)  { sendModeSet(savedMode);      blockingWaitForAck(TUNE_ACK_TIMEOUT_MS); }
    if (restorePower) { sendPowerSetRaw(savedPowerRaw); blockingWaitForAck(TUNE_ACK_TIMEOUT_MS); }
    tuneRunning = false;
    return;
  }

  // Schritt 6: halten (Nutzerwunsch: 3s) - nur ausgefuehrt, wenn wir bis
  // hierher nicht schon unerwartet lange gebraucht haben (Sicherheitsnetz
  // gegen den unwahrscheinlichen Fall verzoegerter/wiederholter Antworten).
  if (millis() - seqStart < TUNE_SEQ_HARD_TIMEOUT_MS) {
    unsigned long holdStart = millis();
    while (millis() - holdStart < holdMs) {
      while (SerialBT.available()) feedByte((uint8_t)SerialBT.read());
      pttFailsafeCheck(false);   // hier liefert das Radio absichtlich keine Frames
      delay(5);
    }
  }

  // Schritt 7: PTT ausschalten - mit Retry+Eskalation (siehe safePttOff()).
  safePttOff();

  // Schritt 8+9: Modus/Leistung wiederherstellen.
  sendModeSet(savedMode);
  blockingWaitForAck(TUNE_ACK_TIMEOUT_MS);
  sendPowerSetRaw(savedPowerRaw);
  blockingWaitForAck(TUNE_ACK_TIMEOUT_MS);

  tuneRunning = false;
  screenDirty = true;
}

// ============================================================
// PMR-171 per BLE (Service ffe0/ffe1), 2026-09-25. Nur Lesen (Status 0x0B).
// Modus wird per langem Tastendruck (>=2,5 s, unterer Taster) umgeschaltet
// und im Flash gemerkt; es laeuft immer nur EIN Bluetooth-Stack.
// Messungen: 200 ms Abfrage stabil (~91 % Antworten, Luecken bis 0,8 s
// beim TX-Ende); 100 ms fuehrt zu Hangern des 171-BLE-Moduls.
// PTT-Sicherheit: nur mit BLE-Verbindung, frischer Antwort (<1,5 s) und
// PA-Band; bei stummer Verbindung >3 s wird neu verbunden.
// ============================================================
Preferences prefs;
bool mode171 = false;
static const char* DEV171_NAME = "PMR-171-BT";
static BLEUUID SVC171("0000ffe0-0000-1000-8000-00805f9b34fb");
static BLEUUID CHR171("0000ffe1-0000-1000-8000-00805f9b34fb");
static const uint8_t QUERY171[] = {0xA5,0xA5,0xA5,0xA5,0x03,0x0B,0xF9,0x37};
const unsigned long POLL171_MS = 200;
const unsigned long PTT171_SILENCE_MS = 1500;
const unsigned long SWITCH_HOLD_MS = 2500;

BLEAdvertisedDevice* foundDev171 = nullptr;
int advSeen171 = 0;
BLEClient* client171 = nullptr;
BLERemoteCharacteristic* chr171 = nullptr;
volatile bool conn171 = false;
uint8_t rxbuf171[64]; int rxlen171 = 0;
uint8_t modeA171 = 255;
unsigned long lastResp171 = 0, lastQuery171 = 0;
bool screen171Dirty = true;

const char* modeName171(uint8_t m) {
  switch (m) { case 0: return "USB"; case 1: return "LSB"; case 2: return "CW-R"; case 3: return "CW";
    case 4: return "AM"; case 5: return "WFM"; case 6: return "NFM"; case 7: return "DIGI"; case 8: return "PKT"; }
  return "-";
}

class AdvCb171 : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice d) override {
    advSeen171++;
    Serial.printf("  adv171 %s rssi=%d name=%s\n", d.getAddress().toString().c_str(), d.getRSSI(), d.haveName() ? d.getName().c_str() : "-");
    bool svc = d.haveServiceUUID() && d.isAdvertisingService(SVC171);
    String nm = d.haveName() ? String(d.getName().c_str()) : String("-");
    if (svc || nm == DEV171_NAME) { BLEDevice::getScan()->stop(); foundDev171 = new BLEAdvertisedDevice(d); }
  }
};
class ClientCb171 : public BLEClientCallbacks {
  void onConnect(BLEClient*) override {}
  void onDisconnect(BLEClient*) override { conn171 = false; Serial.printf("[%lums] 171 BLE getrennt\n", millis()); }
};

uint16_t crc16_171(const uint8_t* b, int n) {
  uint16_t r = 0xFFFF;
  for (int i = 0; i < n; i++) { r ^= (uint16_t)b[i] << 8; for (int k = 0; k < 8; k++) r = (r & 0x8000) ? (uint16_t)((r << 1) ^ 0x1021) : (uint16_t)(r << 1); }
  return r;
}
unsigned long crcBad171 = 0;

void onNotify171(BLERemoteCharacteristic*, uint8_t* d, size_t n, bool) {
  if (rxlen171 + (int)n > (int)sizeof(rxbuf171)) rxlen171 = 0;
  memcpy(rxbuf171 + rxlen171, d, n); rxlen171 += n;
  while (rxlen171 >= 5) {
    int i = 0;
    while (i + 3 < rxlen171 && !(rxbuf171[i]==0xA5 && rxbuf171[i+1]==0xA5 && rxbuf171[i+2]==0xA5 && rxbuf171[i+3]==0xA5)) i++;
    if (i > 0) { memmove(rxbuf171, rxbuf171 + i, rxlen171 - i); rxlen171 -= i; if (rxlen171 < 5) return; }
    int total = rxbuf171[4] + 5;
    if (rxlen171 < total) return;
    uint16_t crcRx = ((uint16_t)rxbuf171[total-2] << 8) | rxbuf171[total-1];
    bool crcOk = crc16_171(rxbuf171 + 4, total - 6) == crcRx;
    if (!crcOk) { crcBad171++; Serial.printf("[%lums] 171 Rahmen mit falscher CRC verworfen (cmd %02X, len %d)\n", millis(), rxbuf171[5], total); }
    if (crcOk && rxbuf171[5] == 0x0B && total >= 32) {
      long fchk = ((long)rxbuf171[9] << 24) | ((long)rxbuf171[10] << 16) | ((long)rxbuf171[11] << 8) | rxbuf171[12];
      if (fchk < 100000L || fchk > 2000000000L) { Serial.printf("[%lums] 171 unplausible Frequenz %ld verworfen\n", millis(), fchk); memmove(rxbuf171, rxbuf171 + total, rxlen171 - total); rxlen171 -= total; continue; }
      const uint8_t* p = rxbuf171 + 6;
      uint8_t tx = p[0];
      long f = ((long)p[3] << 24) | ((long)p[4] << 16) | ((long)p[5] << 8) | p[6];
      modeA171 = p[1];
      lastResp171 = millis(); lastRadioFrameMs = lastResp171;
      if (f != freqHz || !haveFreq) { freqHz = f; haveFreq = true; }
      updateBandVoltage(freqHz);
      if ((int)tx != txState) { txState = tx; Serial.printf("[%lums] 171 TX=%u f=%ld\n", millis(), tx, f); }
      applyPttForTx(tx != 0, "171-BLE");
      screen171Dirty = true;
    }
    memmove(rxbuf171, rxbuf171 + total, rxlen171 - total); rxlen171 -= total;
  }
}

bool connect171() {
  if (!client171) { client171 = BLEDevice::createClient(); client171->setClientCallbacks(new ClientCb171()); }
  if (!client171->connect(foundDev171)) return false;
  BLERemoteService* s = client171->getService(SVC171);
  if (!s) { client171->disconnect(); return false; }
  chr171 = s->getCharacteristic(CHR171);
  if (!chr171 || !chr171->canNotify()) { client171->disconnect(); return false; }
  chr171->registerForNotify(onNotify171);
  rxlen171 = 0; lastResp171 = millis(); lastRadioFrameMs = lastResp171; conn171 = true;
  Serial.printf("[%lums] 171 BLE verbunden\n", millis());
  return true;
}

void render171() {
  tft.setTextSize(1);
  tft.fillRect(140, 4, 95, 18, TFT_MAROON);
  tft.setTextColor(conn171 ? TFT_GREEN : TFT_WHITE, TFT_MAROON);
  tft.setCursor(140, 9);
  tft.print(conn171 ? "BLE OK" : "BLE suche..");
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.fillRect(0, 30, tft.width(), 45, TFT_BLUE);
  tft.setTextColor(TFT_WHITE, TFT_BLUE);
  tft.setCursor(6, 62);
  if (haveFreq) tft.printf("%.5f MHz", freqHz / 1000000.0); else tft.print("---.----- MHz");
  tft.fillRect(0, 78, 176, 55, TFT_BLACK);
  const BandDef* band = haveFreq ? findBand(freqHz) : nullptr;
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK); tft.setCursor(4, 108); tft.print(band ? band->name : "-");
  tft.setTextColor(TFT_CYAN, TFT_BLACK);   tft.setCursor(78, 108); tft.print(modeA171 != 255 ? modeName171(modeA171) : "-");
  tft.setFreeFont(NULL);
  drawTxIndicator();
}

void switchRadio() {
  setPtt(false, "Radiowechsel");
  prefs.putUChar("radio", mode171 ? 0 : 1);
  tft.fillScreen(TFT_BLACK); tft.setTextColor(TFT_YELLOW, TFT_BLACK); tft.setTextSize(2);
  tft.setCursor(10, 50); tft.print(mode171 ? "-> IC-705" : "-> PMR-171");
  Serial.println("Radiowechsel - Neustart");
  delay(1200);
  ESP.restart();
}

void loop171() {
  unsigned long now = millis();
  static bool lastB0 = HIGH; static unsigned long b0Start = 0; static bool swDone = false, lastPress = false;
  bool b0 = digitalRead(BTN_GPIO0);
  if (lastB0 == HIGH && b0 == LOW) { b0Start = now; swDone = false; }
  if (b0 == LOW && !swDone && now - b0Start >= SWITCH_HOLD_MS) { swDone = true; switchRadio(); }
  if (lastB0 == LOW && b0 == HIGH && !swDone && now - b0Start > BUTTON_DEBOUNCE_MS) toggleDisplay();
  lastB0 = b0;

  if (!conn171) {
    setPtt(false, "171 nicht verbunden");
    haveFreq = false; txState = -1; modeA171 = 255;
    render171();
    foundDev171 = nullptr;
    BLEScan* sc = BLEDevice::getScan();
    sc->setAdvertisedDeviceCallbacks(new AdvCb171());
    sc->setActiveScan(true); sc->setInterval(100); sc->setWindow(99);
    sc->start(6, false);
    Serial.printf("[%lums] 171 Scan fertig, gefunden=%d gesehen=%d\n", millis(), foundDev171 != nullptr, advSeen171); advSeen171 = 0;
    if (foundDev171) { bool ok = connect171(); Serial.printf("[%lums] 171 connect=%d\n", millis(), ok); }
    delay(300);
    return;
  }
  if (now - lastQuery171 >= POLL171_MS) {
    lastQuery171 = now;
    chr171->writeValue((uint8_t*)QUERY171, sizeof(QUERY171), false);
  }
  if (pttOut && now - lastResp171 > PTT171_SILENCE_MS) setPtt(false, "Failsafe: 171 stumm");
  if (now - lastResp171 > 3000) {
    Serial.printf("[%lums] 171 3 s ohne Antwort - trenne\n", now);
    setPtt(false, "171 stumm"); client171->disconnect(); conn171 = false;
    delay(2000);   // kein Verbindungssturm: das BLE-Modul des 171 haengt sich sonst auf
    return;
  }
  if (screen171Dirty) { screen171Dirty = false; render171(); }
  delay(5);
}

void setup() {
  // ALLERERSTE Aktion: PTT-Ausgang definiert auf LOW (Optokoppler aus =
  // PA im RX). Noch VOR Serial/Display/Bluetooth, damit der Pin so kurz
  // wie moeglich unbestimmt ist.
  pinMode(PIN_PTT_OUT, OUTPUT);
  digitalWrite(PIN_PTT_OUT, LOW);

  Serial.begin(115200);
  delay(300);
  prefs.begin("xpa", false);
  mode171 = prefs.getUChar("radio", 0) == 1;
  Serial.printf("TTGO XPA-Bridge startet, Funkgeraet: %s\n", mode171 ? "PMR-171 (BLE)" : "IC-705 (BT Classic)");

  // Persistenter ALC-Log (siehe Kommentar dort) - true = beim ersten Mount
  // ohne gueltiges Dateisystem automatisch formatieren, danach bleibt es
  // formatiert. Ein Fehlschlag ist nicht fatal fuer den Rest des Sketches
  // (Anzeige/PTT/Band funktionieren unabhaengig davon weiter) - der Logger
  // faellt dann nur auf "verwerfen statt schreiben" zurueck.
  spiffsOk = SPIFFS.begin(true);
  Serial.printf("SPIFFS-Mount: %s\n", spiffsOk ? "OK" : "FEHLGESCHLAGEN");

  // Bandspannung: PWM auf GPIO27, startet bei 0 mV (Arduino-ESP32 3.x-API).
  analogSetAttenuation(ADC_11db);  // volle Skala bis ~3,3V (Signal geht real bis ~3,99V - clippt dort, siehe Kommentar oben)
  pinMode(PIN_ALC_ADC, INPUT);

  if (!ledcAttach(PIN_BAND_OUT, BAND_PWM_FREQ, BAND_PWM_RES)) {
    Serial.println("FEHLER: ledcAttach fuer GPIO27 fehlgeschlagen");
  }
  ledcWrite(PIN_BAND_OUT, 0);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  tft.init();
  tft.setRotation(1);  // Querformat, 240x135

  // Schriftgroessen fuer die S-Meter-Ansicht einmalig gegen die jeweils
  // laengsten realistischen Faelle berechnen (Nutzerwunsch: Frequenz "so
  // gross wie es von der Breite bei der laengsten Frequenz [6m/10m-Band]
  // geht"). "54.00000 MHz" deckt den breitesten Fall ab (2-stelliger
  // MHz-Anteil, wie bei allen Baendern ab 10m aufwaerts inkl. 6m).
  freqViewTextSize = fitTextSize("54.00000 MHz", 10);
  sMeterViewTextSize = fitTextSize("Signal: S9+30", 10);

  drawStatic();

  // Physische Zuordnung live bestaetigt (2026-09-11): unten=GPIO0,
  // oben=GPIO35 (siehe Taster-Verdrahtung in loop()).
  pinMode(BTN_GPIO0, INPUT_PULLUP);
  pinMode(BTN_GPIO35, INPUT_PULLUP);  // GPIO35 hat KEINE interne Pullup-
                                       // Faehigkeit (Input-only-Pin) - Board
                                       // bringt dafuer laut Doku einen
                                       // eigenen externen Pullup mit.

  // Master-Modus aktiv (zweiter Parameter true) - noetig, damit wir
  // NEBEN dem passiven Server-Betrieb (705 verbindet sich zu uns) auch
  // aktiv selbst eine Verbindung ausloesen koennen (langer Druck auf den
  // unteren Taster, siehe loop()). Laut BluetoothSerial-Quellcode
  // erfordert SerialBT.connect() zwingend diesen Modus.
  if (mode171) { BLEDevice::init("ttgo-xpa-bridge"); BLEDevice::setMTU(247); }
  else SerialBT.begin("TTGO-705-Bridge-Test", true);
  bootMillis = millis();
  Serial.printf("[%lums] SerialBT.begin() fertig\n", bootMillis);
}

void loop() {
  updateAlc();
  if (!mode171) while (SerialBT.available()) {
    feedByte((uint8_t)SerialBT.read());
  }

  // USB-Serial-Kommandos fuer den Bandspannungs-Bench-Test.
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n') {
      handleSerialCommand(serialLine);
      serialLine = "";
    } else if (c != '\r' && serialLine.length() < 40) {
      serialLine += c;
    }
  }
  if (bandTestOverride && (long)(millis() - bandTestUntil) >= 0) {
    Serial.println("BAND-TEST: 5 min abgelaufen, folge wieder der 705-Frequenz");
    bandTestOverride = false;
    appliedBandIdx = -2;
    if (haveFreq) updateBandVoltage(freqHz);
  }

  if (mode171) { loop171(); return; }
  if (digitalRead(BTN_GPIO0) == LOW && btn0PressStart && millis() - btn0PressStart >= SWITCH_HOLD_MS && lastBtn0State == LOW) { switchRadio(); }

  // Unterer Taster (GPIO0, per Nutzertest bestaetigt) = Display an/aus.
  // Oberer Taster (GPIO35) = Tune-Sequenz fuer die XPA125B.
  bool btn0 = digitalRead(BTN_GPIO0);
  bool btn35 = digitalRead(BTN_GPIO35);
  unsigned long nowMs = millis();
  if (nowMs - lastButtonMillis > BUTTON_DEBOUNCE_MS) {
    if (lastBtn0State == HIGH && btn0 == LOW) {
      // Druck-Beginn merken - Aktion erfolgt erst beim Loslassen, damit
      // zwischen kurz (Display-Toggle) und lang (Reconnect) unterschieden
      // werden kann.
      btn0PressStart = nowMs;
      lastButtonMillis = nowMs;
    } else if (lastBtn0State == LOW && btn0 == HIGH) {
      unsigned long heldMs = nowMs - btn0PressStart;
      lastButtonMillis = nowMs;
      if (heldMs >= LONG_PRESS_MS) {
        showSMeterView = !showSMeterView;
        // Kompletter Inhaltsbereich-Clear beim Umschalten (2026-09-11,
        // Nutzerfund): die Normalansicht raeumt nur ihre eigenen Teil-
        // bereiche frei (Frequenzbox + Band/Modus-Zeile bis x=176), deckt
        // aber nicht die volle Breite ab, die die S-Meter-Zeile bei ihrer
        // Schriftgroesse einnehmen kann - ohne diesen Clear blieben beim
        // Zurueckschalten Textreste am rechten Rand stehen.
        tft.fillRect(0, 30, tft.width(), tft.height() - 30, TFT_BLACK);
        screenDirty = true;
      } else {
        toggleDisplay();
      }
    } else if (lastBtn35State == HIGH && btn35 == LOW) {
      lastButtonMillis = nowMs;
      // Klick nur ZAEHLEN, nicht sofort ausloesen - siehe Auswertung
      // weiter unten nach Ablauf von MULTI_CLICK_WINDOW_MS.
      tuneClickCount++;
      lastTuneClickTime = nowMs;
    }
  }
  lastBtn0State = btn0;
  lastBtn35State = btn35;

  // Mehrfachklick-Auswertung: erst ausloesen, wenn eine Weile kein
  // weiterer Klick mehr kam (Nutzerwunsch: 1x=3s, 2x=6s, 3x(+)=9s).
  if (tuneClickCount > 0 && nowMs - lastTuneClickTime > MULTI_CLICK_WINDOW_MS) {
    int clicks = tuneClickCount;
    if (clicks > MAX_TUNE_CLICKS) clicks = MAX_TUNE_CLICKS;
    tuneClickCount = 0;
    if (SerialBT.hasClient()) {
      runTuneSequence((unsigned long)clicks * TUNE_HOLD_MS);
    }
  }

  bool nowConnected = SerialBT.hasClient();
  if (nowConnected != wasConnected) {
    Serial.printf("[%lums] Verbindungsstatus-Wechsel: %s\n", millis(), nowConnected ? "VERBUNDEN" : "GETRENNT");
    screenDirty = true;
    if (nowConnected) {
      // Frisch verbunden: fastPTT einschalten (das Radio setzt es beim
      // Ausschalten zurueck, Fussnote *7 im CI-V-Handbuch) und sofort
      // Frequenz/Modus holen, damit Band + PTT-Freigabe nicht 2 s warten.
      // Bewusst NUR hier einmal, nicht periodisch: die FB-Antwort geht
      // durch den generischen Ack-Mechanismus der Tune-Sequenz.
      SerialBT.write(CMD_FASTPTT_ON, sizeof(CMD_FASTPTT_ON));
      sendQuery(QUERY_FREQ, sizeof(QUERY_FREQ));
      sendQuery(QUERY_MODE, sizeof(QUERY_MODE));
      Serial.printf("[%lums] fastPTT (24 00 00 01) gesendet\n", millis());
    }
    if (!nowConnected) {
      setPtt(false, "BT getrennt");
      haveFreq = false;
      haveMode = false;
      txState = -1;
      haveUtcOffset = false;
      localMinutes = -1;
    }
  }

  // Einmaliger automatischer Reconnect-Versuch kurz nach dem Booten -
  // deckt sowohl "war kurz verbunden, dann Disconnect" (Nutzerbeobachtung)
  // als auch "nie verbunden gewesen" ab.
  // Per Zeitstempel-Log gemessen (2026-09-11): connect() selbst braucht
  // nur ~900ms - die urspruengliche 8s-Wartezeit war unnoetig vorsichtig
  // und sorgte fuer eine spuerbare, aber sinnlose Verzoegerung. 1,5s reicht
  // als Sicherheitsabstand, damit der BT-Stack nach begin() sicher bereit
  // ist, bevor wir selbst aktiv verbinden.
  if (!didBootReconnectAttempt && millis() - bootMillis < BOOT_RECONNECT_WINDOW_MS) {
    bool shouldTry = (!nowConnected && wasConnected) ||
                      (!nowConnected && millis() - bootMillis > 1500);
    if (shouldTry) {
      didBootReconnectAttempt = true;
      reconnectToRadio();
    }
  }

  wasConnected = nowConnected;

  // TX-Status und Uhrzeit werden JEDEN Takt (2s) abgefragt - der 705
  // meldet PTT-Wechsel ueber Bluetooth CI-V offenbar nicht zuverlaessig
  // von selbst per Broadcast, daher hier bewusst haeufig aktiv nachgefragt.
  // UTC-Offset aendert sich praktisch nie - nur einmalig pro Verbindung
  // nachfragen, bis eine Antwort da ist.
  if (nowConnected && !tuneRunning && millis() - lastQueryToggle > 2000) {
    lastQueryToggle = millis();
    sendQuery(QUERY_TX_STATUS, sizeof(QUERY_TX_STATUS));
    sendQuery(QUERY_TIME, sizeof(QUERY_TIME));
    if (showSMeterView) {
      sendQuery(QUERY_SMETER, sizeof(QUERY_SMETER));
    }
    if (!haveUtcOffset) {
      sendQuery(QUERY_UTC_OFFSET, sizeof(QUERY_UTC_OFFSET));
    }
    if (queryFreqNext) {
      sendQuery(QUERY_FREQ, sizeof(QUERY_FREQ));
    } else {
      sendQuery(QUERY_MODE, sizeof(QUERY_MODE));
    }
    queryFreqNext = !queryFreqNext;
  }

  pttFailsafeCheck(true);

  // Schnelles TX-Polling (Standard 100 ms, Rueckfallebene fuer PTT; per
  // "txpoll <ms>" aenderbar, "txpoll 0" = nur der 2-s-Takt).
  if (txPollMs > 0 && nowConnected && !tuneRunning && millis() - lastFastTxPoll >= txPollMs) {
    lastFastTxPoll = millis();
    sendQuery(QUERY_TX_STATUS, sizeof(QUERY_TX_STATUS));
  }

  // Fuer den ALC-Datenlogger (siehe oben): waehrend TX periodisch die
  // Sendeleistung nachfragen, damit jeder gepufferte ALC-Wert einen
  // moeglichst frischen Leistungswert dabei hat. Bewusst NUR waehrend TX -
  // bei RX interessiert der Wert fuer die Kalibrierkurve nicht, und es soll
  // kein unnoetiger BT-Traffic neben dem PTT-kritischen Polling entstehen.
  if (nowConnected && !tuneRunning && txState == 1 &&
      millis() - lastPowerQueryMs >= POWER_QUERY_INTERVAL_MS) {
    lastPowerQueryMs = millis();
    sendQuery(QUERY_POWER, sizeof(QUERY_POWER));
  }

  if (screenDirty) {
    render();
    screenDirty = false;
  }

  delay(txPollMs > 0 ? 1 : 10);
}
