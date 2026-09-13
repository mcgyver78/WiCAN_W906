#!/usr/bin/env python3
"""Generate the Node-RED dashboard page "Fehlerspeicher" for the WiCAN W906 firmware.

  make_dtc_flow.py OUT.json                  with an own MQTT broker config node for localhost:1883
  make_dtc_flow.py OUT.json --broker-id ID   use an existing broker config node of your Node-RED instead

The firmware reads/clears the trouble codes on the MQTT commands {"cmd":"read_dtc"} and
{"cmd":"clear_dtc"} (topic wican/<device id>/cmd) and publishes the result on <topic>/dtc.
"""
import argparse
import json

parser = argparse.ArgumentParser()
parser.add_argument("output")
parser.add_argument("--broker-id", default="", help="id of an existing mqtt-broker config node")
parser.add_argument("--topic", default="wican/sprinter/engine", help="AutoPID group topic of the WiCAN")
parser.add_argument("--texts", default="", help="JSON file {\"CODE\": \"text\"} for the local plain text table")
args = parser.parse_args()
texts = json.load(open(args.texts, encoding="utf-8")) if args.texts else {}

BROKER = args.broker_id or "w906_mqtt_broker"
TAB, UI_TAB = "w906_dtc_flow_tab", "w906_dtc_ui_tab"
GROUP_ACTIONS, GROUP_RESULT = "w906_dtc_grp_actions", "w906_dtc_grp_result"
DTC_TOPIC = args.topic + "/dtc"

nodes = [
    {"id": TAB, "type": "tab", "label": "Sprinter Fehlerspeicher", "disabled": False,
     "info": "Fehlerspeicher lesen/löschen über die WiCAN-W906-Firmware, Ergebnis auf " + DTC_TOPIC},
    {"id": UI_TAB, "type": "ui_tab", "name": "Fehlerspeicher", "icon": "fa-exclamation-triangle", "order": 21,
     "disabled": False, "hidden": False},
    {"id": GROUP_ACTIONS, "type": "ui_group", "name": "Kurztest", "tab": UI_TAB, "order": 1, "disp": True,
     "width": "6", "collapse": False, "className": ""},
    {"id": GROUP_RESULT, "type": "ui_group", "name": "Ergebnis", "tab": UI_TAB, "order": 2, "disp": True,
     "width": "24", "collapse": False, "className": ""},
]
if not args.broker_id:
    nodes.append({"id": BROKER, "type": "mqtt-broker", "name": "Venus OS MQTT", "broker": "localhost", "port": "1883",
                  "clientid": "", "autoConnect": True, "usetls": False, "protocolVersion": "4", "keepalive": "60",
                  "cleansession": True, "autoUnsubscribe": True, "birthTopic": "", "birthQos": "0", "birthPayload": "",
                  "birthMsg": {}, "closeTopic": "", "closeQos": "0", "closePayload": "", "closeMsg": {}, "willTopic": "",
                  "willQos": "0", "willPayload": "", "willMsg": {}, "userProps": "", "sessionExpiry": ""})


def text(nid, label, order, x, y):
    return {"id": nid, "type": "ui_text", "z": TAB, "group": GROUP_ACTIONS, "order": order, "width": 6, "height": 1,
            "name": label, "label": label, "format": "{{msg.payload}}", "layout": "row-spread", "className": "",
            "style": False, "font": "", "fontSize": 16, "color": "#000000", "x": x, "y": y, "wires": []}


def function(nid, name, code, outputs, wires, x, y):
    return {"id": nid, "type": "function", "z": TAB, "name": name, "func": code, "outputs": outputs, "timeout": 0,
            "noerr": 0, "initialize": "", "finalize": "", "libs": [], "x": x, "y": y, "wires": wires}


# The device id is needed for the command topic, it comes from the retained status message
nodes.append({"id": "w906_dtc_status_in", "type": "mqtt in", "z": TAB, "name": "WiCAN Status", "topic": "wican/+/can/status",
              "qos": "0", "datatype": "json", "broker": BROKER, "nl": False, "rap": True, "rh": 0, "inputs": 0,
              "x": 140, "y": 60, "wires": [["w906_dtc_remember"]]})
nodes.append(function("w906_dtc_remember", "WiCAN merken",
                      "const id = msg.topic.split('/')[1];\n"
                      "flow.set('wican_id', id);\n"
                      "msg.payload = id + ' (' + ((msg.payload && msg.payload.status) || '?') + ')';\n"
                      "return msg;", 1, [["w906_dtc_ui_device"]], 360, 60))
nodes.append(text("w906_dtc_ui_device", "WiCAN", 1, 600, 60))

nodes.append({"id": "w906_dtc_btn_read", "type": "ui_button", "z": TAB, "name": "Lesen", "group": GROUP_ACTIONS, "order": 4,
              "width": 3, "height": 1, "passthru": False, "label": "Fehlerspeicher lesen", "tooltip": "", "color": "",
              "bgcolor": "", "className": "", "icon": "fa-search", "payload": "read_dtc", "payloadType": "str",
              "topic": "", "topicType": "str", "x": 150, "y": 140, "wires": [["w906_dtc_command"]]})
nodes.append({"id": "w906_dtc_btn_clear", "type": "ui_button", "z": TAB, "name": "Löschen", "group": GROUP_ACTIONS, "order": 5,
              "width": 3, "height": 1, "passthru": False, "label": "Fehlerspeicher löschen", "tooltip": "",
              "color": "", "bgcolor": "#ca3838", "className": "", "icon": "fa-trash", "payload": "clear", "payloadType": "str",
              "topic": "", "topicType": "str", "x": 150, "y": 200, "wires": [["w906_dtc_confirm"]]})
nodes.append({"id": "w906_dtc_confirm", "type": "ui_toast", "z": TAB, "position": "dialog", "displayTime": "3",
              "highlight": "", "sendall": False, "outputs": 1, "ok": "Löschen", "cancel": "Abbrechen", "raw": False,
              "className": "", "topic": "Fehlerspeicher löschen?", "name": "Bestätigung", "x": 370, "y": 200,
              "wires": [["w906_dtc_confirmed"]]})
nodes.append(function("w906_dtc_confirmed", "bestätigt?",
                      "if (msg.payload !== 'Löschen') return null;\nmsg.payload = 'clear_dtc';\nreturn msg;",
                      1, [["w906_dtc_command"]], 560, 200))
nodes.append(function("w906_dtc_command", "Befehl an WiCAN",
                      "const id = flow.get('wican_id');\n"
                      "if (!id) return [null, { payload: 'WiCAN nicht gefunden (MQTT?)' }];\n"
                      "return [{ topic: 'wican/' + id + '/cmd', payload: JSON.stringify({ cmd: msg.payload }) },\n"
                      "        { payload: msg.payload === 'clear_dtc' ? 'Löschen angefordert …' : 'Lesen angefordert …' }];",
                      2, [["w906_dtc_mqtt_out"], ["w906_dtc_ui_state"]], 800, 160))
nodes.append({"id": "w906_dtc_mqtt_out", "type": "mqtt out", "z": TAB, "name": "Befehl", "topic": "", "qos": "0",
              "retain": "false", "respTopic": "", "contentType": "", "userProps": "", "correl": "", "expiry": "",
              "broker": BROKER, "x": 1010, "y": 140, "wires": []})

nodes.append({"id": "w906_dtc_result_in", "type": "mqtt in", "z": TAB, "name": "Fehlerspeicher", "topic": DTC_TOPIC,
              "qos": "0", "datatype": "json", "broker": BROKER, "nl": False, "rap": True, "rh": 0, "inputs": 0,
              "x": 140, "y": 300, "wires": [["w906_dtc_texts"]]})
texts_code = '// Eigene Klartexte, z. B. aus der Xentry-Anzeige des eigenen Fahrzeugs abgeschrieben.\n// Schlüssel: Fehlercode wie im Dashboard angezeigt ("P242F-FA", KWP z. B. "9301").\n// Tipp: für alle Fehlertypen eines Codes reicht der Code ohne Endung, z. B. "P242F".\nmsg.texts = %s;\nreturn msg;'
evaluate = """// Einordnung nach SAE J2012 (Nummernbereich) und ISO 14229 / SAE J2012 (Fehlertyp, Statusbits)
const AREA = { P: 'Antrieb', C: 'Fahrwerk', B: 'Karosserie', U: 'Netzwerk' };
const FTB_GROUP = { 0: 'allgemein', 1: 'elektrischer Fehler', 2: 'Signalfehler', 3: 'FM/PWM-Signalfehler',
    4: 'interner Fehler', 5: 'Programmierfehler', 6: 'Algorithmusfehler', 7: 'mechanischer Fehler',
    8: 'Bus-/Botschaftsfehler', 9: 'Bauteilfehler' };
const FTB = {
    0x00: 'keine Zusatzinformation', 0x01: 'allgemeiner elektrischer Fehler', 0x02: 'allgemeiner Signalfehler',
    0x03: 'FM/PWM-Fehler', 0x04: 'interner Fehler', 0x05: 'Programmierfehler', 0x06: 'Algorithmusfehler',
    0x07: 'mechanischer Fehler', 0x08: 'Bus-/Botschaftsfehler', 0x09: 'Bauteilfehler',
    0x11: 'Kurzschluss nach Masse', 0x12: 'Kurzschluss nach Plus', 0x13: 'Leitungsunterbrechung',
    0x14: 'Kurzschluss nach Masse oder Unterbrechung', 0x15: 'Kurzschluss nach Plus oder Unterbrechung',
    0x16: 'Spannung unter Schwellwert', 0x17: 'Spannung über Schwellwert', 0x18: 'Strom unter Schwellwert',
    0x19: 'Strom über Schwellwert', 0x1A: 'Widerstand unter Schwellwert', 0x1B: 'Widerstand über Schwellwert',
    0x1C: 'Spannung außerhalb des Bereichs', 0x1D: 'Strom außerhalb des Bereichs',
    0x1E: 'Widerstand außerhalb des Bereichs', 0x1F: 'Leitung zeitweise unterbrochen',
    0x21: 'Signalamplitude zu klein', 0x22: 'Signalamplitude zu groß', 0x23: 'Signal hängt auf Low',
    0x24: 'Signal hängt auf High', 0x25: 'ungültige Signalform', 0x26: 'Änderungsrate zu klein',
    0x27: 'Änderungsrate zu groß', 0x28: 'Signal-Offset außerhalb des Bereichs', 0x29: 'Signal ungültig',
    0x2F: 'Signal sprunghaft',
    0x31: 'kein Signal', 0x32: 'Low-Zeit zu kurz', 0x33: 'Low-Zeit zu lang', 0x34: 'High-Zeit zu kurz',
    0x35: 'High-Zeit zu lang', 0x36: 'Frequenz zu niedrig', 0x37: 'Frequenz zu hoch', 0x38: 'falsche Frequenz',
    0x39: 'zu wenige Impulse', 0x3A: 'zu viele Impulse',
    0x41: 'Prüfsummenfehler', 0x42: 'Speicherfehler', 0x43: 'Spezialspeicherfehler', 0x44: 'Datenspeicherfehler',
    0x45: 'Programmspeicherfehler', 0x46: 'Kalibrier-/Parameterspeicherfehler', 0x47: 'Watchdog-/Sicherheitsrechnerfehler',
    0x48: 'Überwachungssoftwarefehler', 0x49: 'interner Elektronikfehler', 0x4A: 'falsches Bauteil verbaut',
    0x4B: 'Übertemperatur',
    0x51: 'nicht programmiert', 0x52: 'nicht aktiviert', 0x53: 'deaktiviert', 0x54: 'Kalibrierung fehlt',
    0x55: 'nicht konfiguriert',
    0x61: 'Signalberechnung fehlerhaft', 0x62: 'Signalvergleich fehlerhaft', 0x63: 'Schutzzeit überschritten',
    0x64: 'Signal unplausibel', 0x65: 'zu wenige Signalwechsel', 0x66: 'zu viele Signalwechsel',
    0x67: 'Signal nach Ereignis falsch', 0x68: 'Ereignisinformation',
    0x71: 'Stellglied blockiert', 0x72: 'Stellglied hängt offen', 0x73: 'Stellglied hängt geschlossen',
    0x74: 'Stellglied rutscht', 0x75: 'Notlaufposition nicht erreichbar', 0x76: 'falsche Einbaulage',
    0x77: 'Sollposition nicht erreichbar', 0x78: 'Einstellung falsch', 0x79: 'mechanische Verbindung defekt',
    0x7A: 'Undichtigkeit', 0x7B: 'Flüssigkeitsstand zu niedrig',
    0x81: 'ungültige Daten empfangen', 0x82: 'Botschaftszähler falsch', 0x83: 'Signalschutz-Prüfwert falsch',
    0x84: 'Signal unter zulässigem Bereich', 0x85: 'Signal über zulässigem Bereich', 0x86: 'Signal ungültig',
    0x87: 'Botschaft fehlt', 0x88: 'Bus-Off', 0x8F: 'Botschaft sprunghaft',
    0x91: 'Parameterfehler', 0x92: 'Funktion fehlerhaft', 0x93: 'keine Funktion', 0x94: 'unerwartete Funktion',
    0x95: 'falsch montiert', 0x96: 'Bauteil intern defekt', 0x97: 'Funktion blockiert', 0x98: 'Übertemperatur'
};
function origin(letter, digit, nibble) {
    if (letter === 'P') {
        if (digit === 1) return 'herstellerspezifisch';
        if (digit === 3) return nibble <= 3 ? 'herstellerspezifisch' : 'genormt (SAE)';
        return 'genormt (SAE)';
    }
    return (digit === 1 || digit === 2) ? 'herstellerspezifisch' : 'genormt (SAE)';
}
function classify(dtc, protocol) {
    const m = /^([PCBU])([0-3])([0-9A-F])([0-9A-F]{2})-([0-9A-F]{2})$/.exec(dtc.code || '');
    if (!m) return protocol === 'KWP' ? 'Mercedes KWP-Code (herstellerspezifisch)' : '';
    const ftb = parseInt(m[5], 16);
    let type = FTB[ftb];
    if (!type) type = ftb >= 0xF0 ? 'herstellerspezifischer Fehlertyp' : (FTB_GROUP[ftb >> 4] || 'reserviert') + ' (0x' + m[5] + ')';
    return AREA[m[1]] + ' · ' + origin(m[1], parseInt(m[2]), parseInt(m[3], 16)) + ' · ' + type;
}
function statusText(dtc) {
    const s = parseInt(dtc.status || '0', 16);
    if (dtc.active === undefined) return 'gespeichert (0x' + dtc.status + ')';
    const parts = [];
    parts.push(s & 0x01 ? 'aktiv' : (s & 0x08 ? 'gespeichert' : (s & 0x04 ? 'sporadisch' : 'eingetragen')));
    if (s & 0x80) parts.push('Warnleuchte');
    return parts.join(', ') + ' (0x' + dtc.status + ')';
}
function plainText(texts, code) {
    return texts[code] || texts[code.split('-')[0]] || '–';
}

const p = msg.payload || {};
const texts = msg.texts || {};
const action = p.action === 'clear' ? 'Löschen' : 'Lesen';
if (p.state === 'running') {
    return [{ payload: action + ' läuft … ' + (p.ecu || 0) + '/' + (p.total || '?') }, null, null];
}
if (p.state === 'error') {
    const reason = p.reason === 'ecu_offline' ? 'keine Antwort – Zündung einschalten' : (p.reason || 'Fehler');
    return [{ payload: action + ': ' + reason }, null, null];
}
if (p.state !== 'done' || !Array.isArray(p.ecus)) return null;
const rows = [];
let silent = 0;
for (const ecu of p.ecus) {
    for (const dtc of ecu.dtcs || []) {
        rows.push({ ecu: ecu.name, code: dtc.code, text: plainText(texts, dtc.code),
                    info: classify(dtc, ecu.protocol), status: statusText(dtc) });
    }
    if (ecu.status !== 'ok') {
        silent++;
        const status = ecu.status === 'no_response' ? 'keine Antwort'
            : (ecu.status === 'nrc_33' ? 'Zugriff verweigert (nrc_33)' : ecu.status);
        rows.push({ ecu: ecu.name, code: '–', text: '–', info: '', status: status });
    }
}
const count = p.dtc_count || 0;
const when = new Date().toLocaleString('de-DE');
const summary = count === 0 ? 'keine Fehler gespeichert' : count + ' Fehler gespeichert';
return [{ payload: action + ' fertig (' + Math.round((p.duration_ms || 0) / 1000) + ' s)' },
        { payload: summary + (silent ? ', ' + silent + ' Steuergerät(e) ohne Antwort' : '') + ' – ' + when },
        { payload: rows }];"""
nodes.append(function("w906_dtc_texts", "Klartexte (lokal)", texts_code % json.dumps(texts, indent=4, ensure_ascii=False), 1,
                      [["w906_dtc_evaluate"]], 330, 360))
nodes.append(function("w906_dtc_evaluate", "Ergebnis auswerten", evaluate, 3,
                      [["w906_dtc_ui_state"], ["w906_dtc_ui_summary"], ["w906_dtc_ui_table"]], 380, 300))
nodes.append(text("w906_dtc_ui_state", "Status", 2, 1010, 260))
nodes.append(text("w906_dtc_ui_summary", "Ergebnis", 3, 620, 320))
nodes.append({"id": "w906_dtc_ui_table", "type": "ui_table", "z": TAB, "group": GROUP_RESULT, "name": "Fehlerliste",
              "order": 1, "width": 24, "height": 10,
              "columns": [{"field": "ecu", "title": "Steuergerät", "width": "210", "align": "left", "formatter": "plaintext", "formatterParams": {"target": "_blank"}},
                          {"field": "code", "title": "Fehlercode", "width": "100", "align": "left", "formatter": "plaintext", "formatterParams": {"target": "_blank"}},
                          {"field": "text", "title": "Klartext (lokal)", "width": "", "align": "left", "formatter": "textarea", "formatterParams": {"target": "_blank"}},
                          {"field": "info", "title": "Einordnung", "width": "260", "align": "left", "formatter": "textarea", "formatterParams": {"target": "_blank"}},
                          {"field": "status", "title": "Status", "width": "160", "align": "left", "formatter": "plaintext", "formatterParams": {"target": "_blank"}}],
              "outputs": 0, "cts": False, "x": 620, "y": 360, "wires": []})

ids = [n["id"] for n in nodes]
assert len(ids) == len(set(ids))
with open(args.output, "w", encoding="utf-8") as handle:
    json.dump(nodes, handle, indent=2, ensure_ascii=False)
    handle.write("\n")
print("%d nodes written to %s" % (len(nodes), args.output))
