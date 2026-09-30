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
| 16 | +12 V Dauerplus → Verpolschutz → TVS → DC-DC → 3,3 V |
| 4, 5 | Masse |
| 6 | CAN-H |
| 14 | CAN-L |

DC-DC: Eingang möglichst ≥ 36 V, geringer Ruhestrom (das Gerät hängt
dauerhaft an der Starterbatterie).

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
