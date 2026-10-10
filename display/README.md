# WiCAN-Display

Eigenständige Anzeige für den WiCAN im Sprinter W906: Live-Werte auf frei einteilbaren Seiten, Fehlerspeicher
lesen und löschen, alles ohne Cerbo, MQTT-Broker oder Node-RED. Das Display spricht per WLAN und HTTP direkt
mit dem WiCAN; der bisherige Weg WiCAN → MQTT → Node-RED bleibt daneben bestehen.

Hardware: Elecrow CrowPanel 2.1inch-HMI ESP32 Rotary Display (ESP32-S3R8, rundes IPS-Panel 480 × 480,
Drehknopf mit Taster, Touch).

## Stand (2026-10-10)

Die Firmware läuft seit dem 2026-10-09 auf dem Board, seit dem 2026-10-10 im Fahrzeug. Gesehen oder gemessen:

- Start ohne Fehler: Expander antwortet, Touch-Controller meldet sich als CST826 (Kennung 0x11), PSRAM 8 MB,
  WLAN-Treiber startet ohne eigenen NVS-Bereich, „safe mode 0".
- Das Bild steht ruhig, Ring rot, Text richtig; Touch geht; Standby schaltet das Licht nach 60 s ab.
- Knopf: Dieser Encoder liefert zwei Zählschritte je Raste (`KNOB_COUNTS_PER_DETENT`); damit ist eine Raste
  eine Seite.
- Hotspot, Eintragen eines Netzes über die Webseite, Beitritt, echte Werte vom WiCAN.
- Update über die Webseite: hochladen, am Knopf installieren, „Update in Ordnung?" bestätigen.
- Fehlerspeicher lesen vom Display aus (2026-10-10): 35 s, 18 von 18 Steuergeräten, die Liste steht auf dem
  Display.

**Offen: die Funkstrecke des Boards ist schwach.** Zwei Meter vom Router empfängt das Display ihn mit −56 bis
−69 dBm, rund 25 dB unter dem, was der Platz hergibt; der WiCAN in gleicher Entfernung verliert nichts. In guten
Phasen gehen 2 % der Pings verloren, in schlechten 50 bis 100 %, dann scheitert schon der Beitritt. Auch in
einer guten Phase ist die Strecke in beide Richtungen langsam (60 KB vom Display: 1,9 s im Mittel, bis 10 s).
Das Gehäuse ist aus PETG, der Platz ist besser als der des WiCAN. Zwei Erklärungen haben sich als falsch
erwiesen und stehen hier, damit niemand sie wiederholt:

- „Der Energiesparmodus des WLAN-Treibers ist schuld": Er ist abgeschaltet (das senkt die Antwortzeit von 154
  auf 14 ms), die Verluste blieben.
- „Mit eingeschaltetem Hotspot ist die Strecke gut": In zehn Minuten einer Mitschrift stimmte das (kein
  einziger Fehlschlag gegen zwei bis sieben je Minute davor und danach), in einer späteren Messung nicht.

Versuch dagegen, noch ohne Beleg am Gerät: Das Display spricht nur 802.11b/g (`main/net.c`).

Damit ein Aussetzer kein Lesen mehr kostet: Ein Lesen des Fehlerspeichers, das der WiCAN angenommen hat,
übersteht eine Verbindungspause; das Display holt die Liste, wenn er wieder antwortet, und gibt erst nach
180 s Stille auf. Das Löschen bleibt dabei, wie es war: Geht die Verbindung währenddessen verloren, ist der
Ausgang unbekannt, und es muss neu gelesen werden.

Ebenfalls offen: Nach jedem gewollten Neustart nennt die Info-Seite `wdt` als Grund (zweimal gesehen, Ursache
nicht geklärt). Nicht geprüft am Board: Fehlerspeicher löschen, Zurücknehmen eines Updates, Wärme im Gehäuse.

| Teil | Stand |
|---|---|
| Logik (`components/core`, 27 Module) | auf dem PC getestet: 27 Testprogramme mit gut 12.000 Prüfungen, dazu über 8.000 Mutationen, von denen jede einen Test rot machen muss. Läuft in der CI unter gcc, lokal unter clang, jeweils auch mit `unsigned char` wie auf dem ESP32. |
| Zeichencode (`components/ui`, LVGL 9.5.0) | in der CI gebaut; ein Renderer ohne Display zeichnet 79 von Hand gebaute Bildschirme und gut 200 Szenen aus den Tests der Logik und prüft, dass nichts über den Kreis ragt, sich überlappt oder abgeschnitten wird. Die Bilder hängen als Artefakt `wican-display-screens` am CI-Lauf. |
| Plattform (`main`: Start, Bildschirm-Task, WLAN, HTTP, Flash) | übersetzt und linkt für den ESP32-S3. Gegen ESP-IDF v5.5.2 und LVGL 9.5.0 gegengelesen. Dazu laufen `main.c`, `screen.c`, `net.c` und `web.c` unverändert auf dem PC gegen die echte Logik, mit Platzhaltern für ESP-IDF, FreeRTOS und LVGL (`host/platform`, in der CI). Die Platzhalter sind ein nach den Quellen geschriebenes Modell: Sie zeigen, dass die Plattform ihre eigenen Regeln hält, nicht, dass Treiber, Scheduler und Flash sich so verhalten wie gelesen. |
| Board-Schicht (`components/board`) | Pins, Zeiten und die Panel-Initialisierung sind aus dem Schaltplan und den Beispielen des Herstellers gelesen. Am Board bestätigt (2026-10-09): Bild, Farben, Hintergrundlicht, Expander, Touch, Taster, Encoder. Gemessen ist nur die Zahl der Zählschritte je Raste. |
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
host/platform      die Plattform auf dem PC: Simulationen von main.c, screen.c, net.c und web.c, ihre Mutationen
tools              Prüfungen für Partitionstabelle, Layouts, Webseite und Build; mock_display.py
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

Die Plattform auf dem PC und ihr Rot-Nachweis (dieser läuft nicht in der CI, etwa eine Minute):

```bash
cd ~/projects/WiCAN/WiCAN_W906-display/display/host/platform && make
```

```bash
cd ~/projects/WiCAN/WiCAN_W906-display/display/host/platform && python3 redproof.py
```

Webseite gegen den Mock (danach im Browser `http://127.0.0.1:8907/`, die Steuerleiste für Freigabe und Knopf
liegt unter `/mock`):

```bash
cd ~/projects/WiCAN/WiCAN_W906-display/display/tools && python3 mock_display.py --port 8907
```

Die Firmware selbst wird nur in der CI gebaut (`.github/workflows/display.yml`, ESP-IDF v5.5.2). Das Ergebnis
ist das Artefakt `wican-display`. Derselbe Job prüft, dass das Image ein Fünftel seines Slots frei lässt und
dass LVGL mit dem Assert-Handler aus `components/ui` übersetzt wird (ein fehlgeschlagenes Assert endet in
`abort()` und damit in einem Neustart, nicht in einer Endlosschleife).

## Flashen

Erst wenn die Prüfliste unten abgearbeitet wird. Das Board hängt über das mitgelieferte Kabel am USB des
ESP32-S3 selbst (kein USB-UART-Wandler); meldet es sich nicht, BOOT halten und RESET tippen.

Artefakt holen (legt die Dateien nach `~/Downloads/wican-display`):

```bash
gh run download -R mcgyver78/WiCAN_W906 --name wican-display --dir ~/Downloads/wican-display
```

Geflasht wird mit `esptool` (auf dem Mac über Homebrew; die Befehle unten sind gegen die Hilfe von
esptool 5.4.0 geprüft, geflasht wurde damit noch nichts):

```bash
brew install esptool
```

In allen Befehlen `/dev/cu.usbmodemXXXX` durch den Port ersetzen, unter dem sich das Board meldet:

```bash
ls /dev/cu.usbmodem*
```

Vor dem ersten Flashen die Werksfirmware sichern (die ganzen 16 MB, das dauert einige Minuten). Sie ist der
einzige Beleg, dass Board und Panel in Ordnung sind, und der Vergleich, wenn unser Bild nicht kommt:

```bash
esptool --chip esp32s3 --port /dev/cu.usbmodemXXXX read-flash 0 0x1000000 ~/Downloads/crowpanel_werksfirmware.bin
```

Zurück zur Werksfirmware geht es mit derselben Datei:

```bash
esptool --chip esp32s3 --port /dev/cu.usbmodemXXXX write-flash 0 ~/Downloads/crowpanel_werksfirmware.bin
```

Erstes Flashen über USB. Zuerst den ganzen Flash löschen (etwa eine Minute): Die Partitionen der
Werksfirmware liegen anders, und ihre Reste lägen sonst in unseren.

```bash
esptool --chip esp32s3 --port /dev/cu.usbmodemXXXX erase-flash
```

Die Adressen gehören zur Partitionstabelle `partitions.csv` und dürfen nicht geändert werden:

```bash
cd ~/Downloads/wican-display && esptool --chip esp32s3 --port /dev/cu.usbmodemXXXX write-flash 0x0 bootloader/bootloader.bin 0x8000 partition_table/partition-table.bin 0x29000 ota_data_initial.bin 0x30000 wican-display.bin
```

So ist es am 2026-10-09 gelaufen (löschen 61 s, schreiben 14 s, alle Prüfsummen bestätigt).

Die serielle Ausgabe (Start, Fehlermeldungen, die Zeilen zu den `CHECK:`-Stellen) kommt über dasselbe
USB-Kabel; beenden mit Ctrl-A, dann K. Läuft sie nicht mit, geht die Meldung eines Absturzes verloren:

```bash
screen /dev/cu.usbmodemXXXX 115200
```

Spätere Versionen kommen über die Webseite (Gerät → Firmware). Das Display startet eine hochgeladene Firmware
erst nach einem Knopfdruck und fragt nach ihrem ersten Start „Update in Ordnung?". Ohne Antwort binnen fünf
Minuten, oder nach jedem Neustart davor, läuft wieder die Version davor. Eine so zurückgenommene Version
bietet das Display danach nicht als „Vorherige Version" an.

Achtung nach erneutem Flashen über USB: Das schreibt die Buchführung des Bootloaders neu. Liegt im anderen
Slot noch ein zurückgenommenes Update, steht es danach wieder unter „Vorherige Version".

## Bedienung in Kürze

- Drehen: Seite vor und zurück, harte Enden. Kurz drücken: Menü. Lang drücken: eine Ebene zurück.
- Fehlerspeicher nur über das Menü. Löschen: auf „Löschen" drehen und den Knopf drei Sekunden halten.
  Touch kann nur abbrechen. Gelöscht wird nur die Liste, die das Display selbst gerade gelesen hat und zeigt,
  höchstens zehn Minuten lang, bei Zündung an und Motor aus.
- Fragen des Displays beantwortet nur der Knopf. Ein Tipp auf den Bildschirm bestätigt nichts, auch nicht
  „Update in Ordnung?" – eine Firmware, mit der der Knopf nicht geht, wird so nach fünf Minuten
  zurückgenommen.
- Solange das Display liest oder löscht, sind Neustart, Vorherige Version und Werkseinstellungen gesperrt
  (die Zeilen sind grau), und die Webseite kann kein Netz speichern oder vergessen (Antwort „busy").
- Wird das Display zu heiß (85 °C am Chip), schaltet es das Licht ab. Offene Fragen und Dialoge sind dann
  beendet, ein gerade gehaltener Knopfdruck zählt nicht, und die Webseite bekommt für alles, was eine
  Bestätigung am Gerät braucht, die Antwort „hot".
- Web-Zugriff: Menü → Web-Zugriff schaltet Änderungen über die Webseite frei (10 Minuten nach der letzten
  Änderung, höchstens 30 Minuten). WLAN speichern, Firmware installieren und Werkseinstellungen brauchen
  zusätzlich einen Knopfdruck am Gerät. Die Webseite kann den Fehlerspeicher weder lesen noch löschen.
- Ohne gespeichertes WLAN öffnet das Display einen eigenen Hotspot; Name und Passwort stehen unter
  Menü → Web-Zugriff. Solange kein Netz gespeichert ist und im sicheren Modus lässt er sich nicht abschalten
  (die Zeile „Hotspot" in den Einstellungen ist dann grau).
- Knopf beim Einschalten halten: sicherer Modus (eingebaute Ansichten, Hotspot an).

## Erster Tag am Board

Im Quelltext sind 51 Stellen mit `CHECK:` markiert (21 in `components/board/board.c`, 9 in `main/main.c`,
5 in `main/screen.c`, 10 in `main/net.c`, 3 in `main/web.c`, 2 in `main/web/index.html`, 1 in
`sdkconfig.defaults`). Jede sagt, was zu beobachten ist und was zu ändern ist, wenn es anders kommt:

```bash
grep -rn "CHECK:" ~/projects/WiCAN/WiCAN_W906-display/display/components/board ~/projects/WiCAN/WiCAN_W906-display/display/main ~/projects/WiCAN/WiCAN_W906-display/display/sdkconfig.defaults
```

Die Reihenfolge, in der sie sich stellen:

1. Vor dem ersten Flashen: Meldet sich das Board am Mac als USB-Gerät? Die Werksfirmware läuft und zeigt ein
   Bild? Das ist der Beleg, dass Board und Kabel in Ordnung sind. Dann die Werksfirmware sichern (siehe
   „Flashen").
2. Nach dem Flashen, serielle Ausgabe mitlesen: Antwortet der Expander an 0x21? Welche Kennung meldet der
   Touch-Controller? Startet der WLAN-Treiber (er läuft ohne eigenen NVS-Bereich)? Steht in der Startzeile
   „safe mode 0", ohne dass eine Hand am Knopf war? Unter Menü → Info darf „Letzter Neustart" nie `task_wdt`
   zeigen: Der Watchdog beendet jetzt einen Hänger von Bildschirm- oder Zeichen-Task mit einem Neustart.
3. Bild: steht, gerade und ganz, auch mit WLAN-Verkehr? Farben richtig (Rot, Grün, Blau), nichts gespiegelt?
   Der Timing-Satz ist der der Werksfirmware (12 MHz); der Satz aus den ESPHome-Beispielen des Herstellers
   steht in `board_pins.h` als Ausweichweg.
4. Hintergrundlicht: nach dem Start dunkel, dann stufenlos heller, kein Flackern bei 1 %.
5. Knopf: Gemessen am 2026-10-09: Solange `KNOB_COUNTS_PER_DETENT` 4 war, brauchte eine Seite zwei Rasten
   der Hand. Der Knopf gibt also zwei Zählschritte je Raste, und die Konstante in `components/core/knob.h`
   steht seitdem auf 2. Noch zu sehen: Mit der 2 ist eine Raste eine Seite; zehn Rasten vor und zurück
   ergeben null; Drehrichtung (Einstellung „Drehrichtung", nicht der Quelltext). Taster: losgelassen und
   gedrückt werden richtig gelesen, auch nach Minuten. Gehen Tastendrücke verloren, ohne dass gerade etwas
   gespeichert wird, dauert eine Lesung länger als die angenommenen 10 ms (`READ_MS` in `main/screen.c`).
6. Touch: Ein Tipp trifft die Zeile unter dem Finger, oben und unten in einer Liste.
7. Löschdialog: Der Ring füllt sich in drei Sekunden. Er darf neu beginnen, aber nie früher als nach drei
   Sekunden Halten vollenden – auch nicht, während im Browser ein Layout gespeichert wird.
8. Speicher und Wärme: kleinster freier interner Heap nach einer Stunde Betrieb mit Webseite; Chiptemperatur
   bei voller Helligkeit im geschlossenen Gehäuse.
9. Update: eine Firmware über die Webseite hochladen, bestätigen, „Update in Ordnung?" einmal bestätigen und
   einmal nicht (dann muss nach fünf Minuten die alte Version wieder laufen).

## Bekannte offene Punkte

- Die Plattform und die Board-Schicht sind gelesen, übersetzt und (die Plattform) auf dem PC simuliert, nie
  auf dem Board gelaufen.
- Bleibt der Netz-Task 60 s stehen (eine Gegenstelle, die ihre Antwort nie beendet), startet das Display neu.
  Das kann einen laufenden Firmware-Upload abbrechen.
- Während eines Firmware-Uploads fragt das Display den WiCAN weiter ab. Ob der Upload von 2 MB dabei bei
  laufendem Motor zügig durchläuft, ist am Board zu messen.
- „Update in Ordnung?" bleibt stehen, wenn die Hitze das Licht abschaltet; ihre fünf Minuten laufen weiter.
  Ein Update kann also zurückgenommen werden, nur weil das Display in dieser Zeit zu heiß war.
- Eine Seite, auf der seit dem Verbinden noch kein Wert geliefert wurde, zeigt Striche ohne gelben Ring.
- Löschen des Fehlerspeichers über HTTP ist am Fahrzeug noch nicht getestet, ebenso der Schlaf-Aufschub des
  WiCAN nach einem Scan.
- Die Webseite erkennt einen Neustart des Displays nur daran, dass die Laufzeit rückwärts springt.
- Sprache der Bedienoberfläche ist Deutsch, fest eingebaut.
