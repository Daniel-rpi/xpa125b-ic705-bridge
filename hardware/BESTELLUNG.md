# Bestellung bei JLCPCB – Schritt für Schritt (Erstbesteller)

Stand 2026-09-18. Alle Fertigungsdaten liegen fertig in `fertigung/`:
- `ttgo_xpa_gerber.zip` – Platinendaten (Gerber + Bohrdaten), bei JLC bereits als **2 Lagen, 25,89 × 31,94 mm** erkannt
- `BOM_jlcpcb.csv` – Stückliste (LCSC-Nummern, 13 Bauteile in 11 Zeilen)
- `CPL_jlcpcb.csv` – Bestückungspositionen (Position + Drehung jedes Bauteils, alle „Top")

Der Bestell-Tab ist in deinem Browser bereits vorbereitet: Gerber hochgeladen, **0,8 mm, bleifreies HASL (Standard-Oberfläche), 5 Stück, Bestückung
Top-Seite, „Economic"**. Ich habe dort NICHTS bestätigt, kein Konto angelegt, nichts bezahlt und keine Bedingungen akzeptiert.

## Was du selbst tun musst
1. **Konto bei jlcpcb.com anlegen bzw. einloggen** (E-Mail + Passwort; Konto/Passwörter gibst du selbst ein).
2. Beim Bestücken: Häkchen **„I agree to the Terms and Conditions of JLCPCB assembly Service"** selbst setzen (Bedingungen kurz lesen).
3. **NEXT** klicken → Seite „Upload BOM & CPL": `BOM_jlcpcb.csv` und `CPL_jlcpcb.csv` hochladen
   (sag mir Bescheid, dann lade ich sie dir per Plugin hoch und lese das Ergebnis mit).
4. **Teile-Abgleich prüfen** (der wichtigste Schritt): Alle 13 Bauteile müssen „Matched" mit Lagerbestand sein. Erwartet:
   11 Positionen, davon **genau 2 Extended** (AP63203 = C780769, Drossel = C5832360) → dafür berechnet JLC eine Zusatzgebühr
   (meines Wissens ca. 3 USD je Extended-Teil, steht dort im Preis). Alles andere Basic/Preferred ohne Gebühr.
5. **Bestückungsvorschau prüfen** (2. Wichtigster Schritt; JLC dreht Bauteile manchmal anders als KiCad). Nur diese drei Teile sind
   drehungsempfindlich – so muss die **Draufsicht** aussehen (USB-C-Ende des TTGO = unten):
   | Teil | Richtig ist … |
   |---|---|
   | **U1** (AP63203, 6 Beine) | 3 Beine links, 3 rechts; **Pin 1 (Punkt/Marker) oben links** |
   | **Q1** (AO3400A, 3 Beine) | **2 Beine links, 1 Bein rechts** (das einzelne Bein rechts = Drain) |
   | **D1** (1N5819WS, Diode) | **Kathodenstrich links** |
   Alle anderen (Kondensatoren, Widerstände, Drossel, TVS-Diode D2 bidirektional) sind egal. Falsch gedreht → in JLCs Vorschau mit dem
   Drehknopf um 90° korrigieren (oder mir den Screenshot geben, ich sage dir den Winkel).
6. Zusammenfassung (Preis, Lieferadresse) prüfen, **Versandart wählen** (DHL ~28 USD ist die teuerste; günstigere Optionen wie
   „Global Standard Direct Line" dauern länger) und **bezahlen** (Zahlungsdaten gibst du selbst ein).

## Erwartete Kosten (grobe Einordnung, endgültig erst nach dem BOM-Upload)
PCB 5 Stück ≈ 4 USD + Oberfläche 1,1 USD, Bestückung/Bauteile/Extended-Gebühr kommen nach dem BOM-Upload dazu, Versand meist der größte Posten.
Mindestmenge sind 5 Platinen; du bekommst alle 5 bestückt (Ersatz für Fehlversuche).

## Wenn die Platinen da sind
1. TTGO-Stiftleisten (Kunststoffkörper > 2,5 mm) durch die Löcher des Daughter-Boards stecken, Board bündig auf die Kunststoffkörper
   drücken (Bauteile zeigen zum TTGO), auf der Außenseite verlöten. **USB nicht gleichzeitig, wenn das Board an der PA hängt.**
2. Inbetriebnahme wie in `DESIGN.md` („Inbetriebnahme"): erst Spannungen messen, dann TTGO, dann PA – mit der roten PTT-Ader
   vorher gegen GND messen (< 3,2 V!).
3. **PTT-Schalter ist jetzt ein MOSFET** (statt 4N25 im Prototyp) – erster Test an der PA mit wenig Leistung.
