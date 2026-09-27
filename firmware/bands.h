// In eine eigene Header-Datei ausgelagert, da Arduinos automatische
// Funktions-Prototyp-Generierung sonst eine Vorwaertsdeklaration von
// findBand() VOR der struct-Definition einfuegt (Compile-Fehler
// "BandDef does not name a type") - ein bekanntes Arduino-IDE-Verhalten.
#pragma once

// bandMv  = Soll-Bandspannung laut offiziellem Xiegu-XPA125B-Handbuch
//           (ACC-Pin 3, "Band voltage input"), in mV.
// corrMv  = Lastkorrektur in mV, aus dl1bz/ESP32-IC705BT-PA (z_userprog.ino)
//           uebernommen: gleicher RC-Tiefpass (470 Ohm + 22 uF) am
//           GPIO27, die PA belastet den Ausgang leicht (~2,8 %) - der
//           PWM-Sollwert wird um corrMv angehoben, damit die Spannung
//           MIT angeschlossener PA auf dem Handbuchwert liegt. OHNE PA
//           (Bench-Test) liegt die gemessene Spannung entsprechend
//           ca. corrMv hoeher als bandMv.
struct BandDef {
  const char* name;
  long lowHz;
  long highHz;
  int bandMv;
  int corrMv;
};

const BandDef BANDS[] = {
  {"160m", 1800000L,  2000000L,  230,  6},
  {"80m",  3500000L,  4000000L,  460, 14},
  {"60m",  5250000L,  5450000L,  690, 19},
  {"40m",  7000000L,  7300000L,  920, 26},
  {"30m",  10100000L, 10150000L, 1150, 30},
  {"20m",  14000000L, 14350000L, 1380, 35},
  {"17m",  18068000L, 18168000L, 1610, 44},
  {"15m",  21000000L, 21450000L, 1840, 58},
  {"12m",  24890000L, 24990000L, 2070, 55},
  {"10m",  28000000L, 29700000L, 2300, 63},
  {"6m",   50000000L, 54000000L, 2530, 71},
};
const int NUM_BANDS = sizeof(BANDS) / sizeof(BANDS[0]);

const BandDef* findBand(long hz) {
  for (int i = 0; i < NUM_BANDS; i++) {
    if (hz >= BANDS[i].lowHz && hz <= BANDS[i].highHz) return &BANDS[i];
  }
  return nullptr;
}
