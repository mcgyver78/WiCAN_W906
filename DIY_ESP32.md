# WiCAN selbst gebaut: ESP32-WROOM + CAN-Transceiver

Build-Variante der WiCAN-OBD-Firmware für einen klassischen ESP32
(ESP32-WROOM-32, 4 MB Flash) statt des ESP32-C3 im WiCAN OBD V300. Zur
Laufzeit verhält sich das Board wie ein V300: gleiche Weboberfläche,
AutoPID, MQTT, Sleep-Modus.

## Pinbelegung

| Funktion | ESP32-GPIO | Anschluss |
|---|---|---|
| CAN TX | GPIO21 | Transceiver TXD (D) |
| CAN RX | GPIO22 | Transceiver RXD (R) |
| CAN Standby | GPIO23 | SN65HVD230 Rs (Pin 8) bzw. TJA1051T S (Pin 8); 10 kΩ nach GND |
| Batteriespannung | GPIO34 (ADC1_CH6) | Teiler 100 kΩ / 16 kΩ von OBD-Pin 16, 100 nF nach GND |
| LED „verbunden“ | GPIO25 | LED + 1 kΩ nach GND (optional) |
| LED „aktiv“ | GPIO26 | LED + 1 kΩ nach GND (optional) |
| LED „Power“ | GPIO27 | LED + 1 kΩ nach GND (optional) |
| Log (UART0) | GPIO1 / GPIO3 | USB-Seriell des Devboards |

Gemieden: Strapping-Pins (0, 2, 5, 12, 15), Flash-Pins (6–11), UART0.

Die Firmware setzt Standby beim Start auf High und beim Öffnen des CAN auf
Low. Der Pulldown hält den Transceiver aktiv, solange der ESP32 bootet.

**Spannungsteiler:** Er muss genau 100 k / 16 k sein. Die Firmware rechnet
mit dem V300-Faktor 116/16 (`sleep_mode.c`), sonst stimmen Anzeige und
Sleep-Schwelle nicht.

## Transceiver

- **SN65HVD230 (VP230):** 3,3 V, direkt am ESP32. Erste Wahl.
- **TJA1051T:** braucht 5 V an VCC; RXD liefert dann 5 V → Teiler (z. B.
  10 k / 20 k) vor GPIO22. Ob 3,3 V an TXD sicher als High erkannt werden,
  im Datenblatt (V_IH an TXD) prüfen.
- **MCP2515:** wird nicht gebraucht, die Firmware nutzt den eingebauten
  TWAI-Controller.

Den 120-Ω-Abschlusswiderstand auf dem Transceiver-Modul **entfernen** – der
Fahrzeugbus ist bereits abgeschlossen.

## OBD-Stecker

| OBD-Pin | Signal |
|---|---|
| 16 | +12 V Dauerplus (V_BAT hinter F1 und D1) |
| 4, 5 | Masse |
| 6 | CAN-H |
| 14 | CAN-L |

Die Aderfarben fertiger OBD-Kabel sind nicht genormt – vor dem Anlöten
durchmessen.

## Spannungsversorgung

```
OBD 16 ── F1 ── D1 ── V_BAT

V_BAT ──┬── D2 ── GND
        ├── C1 ── GND        (Plus an V_BAT)
        ├── C2 ── GND
        ├── R1 ──┬── GPIO34
        │        ├── R2 ── GND
        │        └── C3 ── GND
        └── MP1584 IN+

MP1584 IN−  ── GND
MP1584 OUT+ ──┬── 3V3 ESP32-DevKit
              └── VCC SN65HVD230
MP1584 OUT− ── GND

OBD 4, 5 ── GND
```

- **D1** (Schottky, Verpolschutz): Ring zum MP1584.
- **D2** (TVS, unidirektional): Ring zur 12-V-Seite. Falsch herum schließt
  sie 12 V kurz, und F1 löst aus.
- **C1**: Plus an V_BAT, Spannungsfestigkeit ≥ 35 V.
- **MP1584EN**: verträgt höchstens 28 V am Eingang, deshalb D2 davor. Erst
  **ohne** ESP32 auf 3,3 V einstellen, Poti danach sichern (Lack),
  Power-LED des Moduls auslöten (Ruhestrom).
- **DevKit**: 3,3 V an den Pin **3V3**, nicht an 5V/VIN. Die Power-LED des
  DevKits ebenfalls auslöten. USB am DevKit und 12 V am MP1584 nicht
  gleichzeitig anschließen – zum Flashen auf dem Tisch reicht USB allein.
- **Teiler R1/R2** hinter D1: Die Firmware schlägt 0,2 V auf den Messwert
  auf, vermutlich als Ausgleich für den Diodenabfall beim V300 (nicht am
  V300-Schaltplan geprüft). Bei 12,6 V an V_BAT liegen etwa 1,74 V an
  GPIO34.
- Ohne Teiler darf der Sleep-Modus nicht eingeschaltet werden: GPIO34 misst
  dann ~0 V, der WiCAN schläft ein und wacht nicht mehr auf.

## Stückliste

Bedrahtete Bauteile, Bestellnummern von Reichelt (Stand 30.09.2026, alle ab
Lager).

| Pos. | Bauteil | Wert / Typ | Reichelt | Menge |
|---|---|---|---|---|
| 1 | ESP32-DevKit | ESP32-DevKitC (WROOM-32) | – | 1 |
| 2 | CAN-Transceiver-Modul | SN65HVD230 (VP230) | – | 1 |
| 3 | DC-DC-Modul | MP1584EN | – | 1 |
| 4 | OBD-Stecker mit Kabel | | – | 1 |
| 5 | F1 Polyfuse | 0,5 A Haltestrom, 72 V | `LITT RXEF050` | 1 |
| 6 | D1 Schottky | 1N5819, 40 V / 1 A | `1N 5819 TSC` | 1 |
| 7 | D2 TVS | P6KE20A, unidirektional | `P6KE 20A` | 1 |
| 8 | C1 Elko | 100 µF / 50 V, 105 °C, AEC-Q200 | `FC-A 100U 50` | 1 |
| 9 | C2, C3 Keramik | 100 nF / 50 V, X7R, RM 2,5 | `X7R-2,5 100N MUR` | 2 |
| 10 | R1 | 100 kΩ, 1 % | `METALL 100K` | 1 |
| 11 | R2 | 16 kΩ, 1 % | `METALL 16,0K` | 1 |
| 12 | R3 | 10 kΩ, 1 % (Pulldown GPIO23 → GND) | `METALL 10,0K` | 1 |
| 13 | Lochrasterplatine, Buchsenleisten 2,54 mm, Gehäuse, Draht, Zugentlastung | | – | |

- **TVS-Falle:** Bei P6KE nennt die Zahl die Durchbruchspannung, bei SMBJ die
  Sperrspannung. Das Gegenstück zur SMBJ18A ist die **P6KE20A**
  (Sperrspannung 17,1 V, Klemmung 27,7 V), nicht die P6KE18A (Sperrspannung
  nur 15,3 V).
- **16 kΩ nicht da:** 15 kΩ + 1 kΩ in Reihe. Das Verhältnis 100k/16k muss
  stimmen.
- Einen 10-µF-Kondensator am 3,3-V-Ausgang braucht es nicht, das DevKit hat
  ihn schon.
- LEDs an GPIO25–27 sind optional; die Pins können offen bleiben.

## Bauen

Ohne lokale ESP-IDF über GitHub Actions: Workflow
*Build Firmware (DIY ESP32-WROOM)*, Artefakt `wican-fw-obd-diy-esp32`.

Lokal mit ESP-IDF v5.5.2:

```bash
sh tools/diy_esp32/build.sh
```

Das Skript leitet die Einstellungen aus der `sdkconfig` des C3 ab,
entfernt die zielspezifischen Einträge, legt
`sdkconfig.diy_esp32.defaults` darüber und baut nach `build_diy/`. Die
`sdkconfig` im Repo bleibt unberührt. Am Ende prüft es, ob die wichtigen
Einstellungen (Target, BLE-only-Controller, Dual-Core, Partitionstabelle)
angekommen sind.

Eigene Partitionstabelle `wican_partitions_diy_esp32.csv` mit den vollen
4 MB (2 × 1856 K + 320 K Speicher), als Reserve. Angenommen war zuerst, der
Xtensa-Build sei größer als der C3-Build, der die 1740-K-Slots fast füllt.
Gemessen (CI, 30.09.2026) ist er kleiner: 1.633.264 statt 1.737.840 Bytes,
14 % Slot frei.

Gegenüber der C3-Konfiguration liegen WLAN-RX-Pfad und Ringbuffer im Flash
statt im IRAM – mit den C3-Einstellungen lief das IRAM beim Linken um
664 Bytes über.

## Erstes Flashen

Einmal per USB am Devboard, danach per OTA über die Weboberfläche:

```bash
cd build_diy
esptool.py --chip esp32 -b 460800 write_flash @flash_args
```

Vorher die Chip-Revision ansehen – vor Revision 3 hat der TWAI-Controller
Fehler, die der Treiber nur teilweise umgeht:

```bash
esptool.py --chip esp32 chip_id
```

## Stand

Übersetzt (CI grün, 30.09.2026), nicht an Hardware getestet. LED-Polung vom V300 übernommen, ungeprüft.
