# WiCAN-Display

Eigenständige Anzeige für den WiCAN im Sprinter W906: Live-Werte auf frei einteilbaren Seiten, Fehlerspeicher
lesen und löschen, alles ohne Cerbo, MQTT-Broker oder Node-RED. Das Display spricht per WLAN und HTTP direkt
mit dem WiCAN; der bisherige Weg WiCAN → MQTT → Node-RED bleibt daneben bestehen.

Hardware: Elecrow CrowPanel 2.1inch-HMI ESP32 Rotary Display (ESP32-S3R8, rundes IPS-Panel 480 × 480,
Drehknopf mit Taster, Touch).

## Stand (2026-10-05)

**Nichts davon ist je auf dem Board gelaufen. Das Board lag beim Schreiben nicht vor.**

| Teil | Stand |
|---|---|
| Logik (`components/core`, 27 Module) | auf dem PC getestet: 27 Testprogramme mit gut 11.000 Prüfungen, dazu über 8.000 Mutationen, von denen jede einen Test rot machen muss. Läuft in der CI unter gcc, lokal unter clang, jeweils auch mit `unsigned char` wie auf dem ESP32. |
| Zeichencode (`components/ui`, LVGL 9.5.0) | in der CI gebaut; ein Renderer ohne Display zeichnet 66 Bildschirme und prüft, dass nichts über den Kreis ragt, sich überlappt oder abgeschnitten wird. Die Bilder hängen als Artefakt `wican-display-screens` am CI-Lauf. |
| Plattform (`main`: Start, Bildschirm-Task, WLAN, HTTP, Flash) | übersetzt und linkt für den ESP32-S3. Mehr nicht. |
| Board-Schicht (`components/board`) | übersetzt. Alle Pins, Zeiten und die Panel-Initialisierung sind aus dem Schaltplan und den Beispielen des Herstellers **gelesen**, nichts ist gemessen. |
| Webseite (`main/web/index.html`) | in Chromium gegen einen Display-Mock auf dem PC ausprobiert. Nicht auf einem Telefon, nicht in Safari oder Firefox. |
| WiCAN-Firmware (HTTP-Weg für den Fehlerspeicher) | auf dem Adapter im Fahrzeug geflasht und gemessen (2026-10-04): Lesen über HTTP funktioniert, MQTT unverändert. Löschen über HTTP ist am Fahrzeug noch nicht getestet. Siehe `tools/w906/API.md`. |

## Aufbau

```
components/core    alles Verhalten in reinem C ohne ESP-IDF; app.h ist das ganze Gerät, app_web.h seine Weboberfläche
components/ui      zeichnet, was core als scene_t vorgibt, und entscheidet nichts
components/board   die Hardware des CrowPanel: Expander, Panel, Knopf, Touch, Hintergrundlicht
components/store   was einen Neustart überlebt (NVS)
main               die Plattform: Tasks, WLAN, HTTP in beide Richtungen, Ereignisse der App
main/web           die Webseite, eine Datei
layouts            das eingebaute Layout für den W906 (35 Werte auf 7 Seiten)
test               Tests der Logik, Mutationslisten, redproof.py
host               der Renderer ohne Display
tools              Prüfungen für Partitionstabelle, Layouts und Webseite; mock_display.py
API.md             die Weboberfläche des Displays
```

Die Regel dahinter: Was sich entscheiden lässt, steht in `components/core` und ist getestet. `main` und
`components/board` reichen nur weiter. Wer etwas am Verhalten ändert, ändert es in `core` und im Test dazu.

## Prüfen ohne Board

Logik-Tests und Rot-Nachweis (clang oder gcc, make, python3):

```bash
cd ~/projects/WiCAN/WiCAN_W906-display/display/test && make
```

```bash
cd ~/projects/WiCAN/WiCAN_W906-display/display/test && python3 redproof.py --module hold
```

Ohne `--module` laufen alle Mutationen; das dauert auf einem Laptop über eine Stunde, die CI teilt es in acht Teile.

Webseite gegen den Mock (danach im Browser `http://127.0.0.1:8907/`, die Steuerleiste für Freigabe und Knopf
liegt unter `/mock`):

```bash
cd ~/projects/WiCAN/WiCAN_W906-display/display/tools && python3 mock_display.py --port 8907
```

Die Firmware selbst wird nur in der CI gebaut (`.github/workflows/display.yml`, ESP-IDF v5.5.2). Das Ergebnis
ist das Artefakt `wican-display`.

## Flashen

Erst wenn die Prüfliste unten abgearbeitet wird. Das Board hängt über das mitgelieferte Kabel am USB des
ESP32-S3 selbst (kein USB-UART-Wandler); meldet es sich nicht, BOOT halten und RESET tippen.

Artefakt holen (legt die Dateien nach `~/Downloads/wican-display`):

```bash
gh run download -R mcgyver78/WiCAN_W906 --name wican-display --dir ~/Downloads/wican-display
```

Erstes Flashen über USB. Die Adressen gehören zur Partitionstabelle `partitions.csv` und dürfen nicht
geändert werden; `/dev/cu.usbmodemXXXX` durch den Port ersetzen, unter dem sich das Board meldet:

```bash
cd ~/Downloads/wican-display && python3 -m esptool --chip esp32s3 --port /dev/cu.usbmodemXXXX write_flash 0x0 bootloader/bootloader.bin 0x8000 partition_table/partition-table.bin 0x29000 ota_data_initial.bin 0x30000 wican-display.bin
```

Spätere Versionen kommen über die Webseite (Gerät → Firmware). Das Display startet eine hochgeladene Firmware
erst nach einem Knopfdruck und fragt nach ihrem ersten Start „Update in Ordnung?". Ohne Antwort binnen fünf
Minuten, oder nach jedem Neustart davor, läuft wieder die Version davor.

## Bedienung in Kürze

- Drehen: Seite vor und zurück, harte Enden. Kurz drücken: Menü. Lang drücken: eine Ebene zurück.
- Fehlerspeicher nur über das Menü. Löschen: auf „Löschen" drehen und den Knopf drei Sekunden halten.
  Touch kann nur abbrechen. Gelöscht wird nur die Liste, die das Display selbst gerade gelesen hat und zeigt,
  höchstens zehn Minuten lang, bei Zündung an und Motor aus.
- Web-Zugriff: Menü → Web-Zugriff schaltet Änderungen über die Webseite frei (10 Minuten nach der letzten
  Änderung, höchstens 30 Minuten). WLAN speichern, Firmware installieren und Werkseinstellungen brauchen
  zusätzlich einen Knopfdruck am Gerät. Die Webseite kann den Fehlerspeicher weder lesen noch löschen.
- Ohne gespeichertes WLAN öffnet das Display einen eigenen Hotspot; Name und Passwort stehen unter
  Menü → Web-Zugriff.
- Knopf beim Einschalten halten: sicherer Modus (eingebaute Ansichten, Hotspot an).

## Erster Tag am Board

Im Quelltext sind 46 Stellen mit `CHECK:` markiert (22 in `components/board/board.c`, 8 in `main/main.c`,
5 in `main/screen.c`, 8 in `main/net.c`, 3 in `main/web.c`). Jede sagt, was zu beobachten ist und was zu ändern
ist, wenn es anders kommt:

```bash
grep -rn "CHECK:" ~/projects/WiCAN/WiCAN_W906-display/display/components/board ~/projects/WiCAN/WiCAN_W906-display/display/main
```

Die Reihenfolge, in der sie sich stellen:

1. Vor dem ersten Flashen: Meldet sich das Board am Mac als USB-Gerät? Die Werksfirmware läuft und zeigt ein
   Bild? Das ist der Beleg, dass Board und Kabel in Ordnung sind.
2. Nach dem Flashen, serielle Ausgabe mitlesen: Antwortet der Expander an 0x21? Welche Kennung meldet der
   Touch-Controller?
3. Bild: steht, gerade und ganz, auch mit WLAN-Verkehr? Farben richtig (Rot, Grün, Blau), nichts gespiegelt?
   Der Timing-Satz ist der der Werksfirmware (12 MHz); der Satz aus den ESPHome-Beispielen des Herstellers
   steht in `board_pins.h` als Ausweichweg.
4. Hintergrundlicht: nach dem Start dunkel, dann stufenlos heller, kein Flackern bei 1 %.
5. Knopf: zehn Rasten vor und zurück ergeben null; eine Raste ist eine Seite (sonst `KNOB_COUNTS_PER_DETENT`);
   Drehrichtung (Einstellung „Drehrichtung", nicht der Quelltext). Taster: losgelassen und gedrückt werden
   richtig gelesen, auch nach Minuten.
6. Touch: Ein Tipp trifft die Zeile unter dem Finger, oben und unten in einer Liste.
7. Löschdialog: Der Ring füllt sich in drei Sekunden. Er darf neu beginnen, aber nie früher als nach drei
   Sekunden Halten vollenden – auch nicht, während im Browser ein Layout gespeichert wird.
8. Speicher und Wärme: kleinster freier interner Heap nach einer Stunde Betrieb mit Webseite; Chiptemperatur
   bei voller Helligkeit im geschlossenen Gehäuse.
9. Update: eine Firmware über die Webseite hochladen, bestätigen, „Update in Ordnung?" einmal bestätigen und
   einmal nicht (dann muss nach fünf Minuten die alte Version wieder laufen).

## Bekannte offene Punkte

- Die Plattform und die Board-Schicht sind gelesen und übersetzt, nie gelaufen.
- Löschen des Fehlerspeichers über HTTP ist am Fahrzeug noch nicht getestet, ebenso der Schlaf-Aufschub des
  WiCAN nach einem Scan.
- Startet der WiCAN neu, während das Display nicht im WLAN ist, behält das Display den alten Wertekatalog bis
  zum eigenen Neustart.
- Ein Wert, den das Fahrzeug nie beantwortet, hält den Ring seiner Seite dauerhaft gelb.
- Die Webseite erkennt einen Neustart des Displays nur daran, dass die Laufzeit rückwärts springt.
- Sprache der Bedienoberfläche ist Deutsch, fest eingebaut.
