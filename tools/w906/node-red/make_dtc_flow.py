#!/usr/bin/env python3
"""Generate the Node-RED dashboard page "Fehlerspeicher" for the WiCAN W906 firmware.

  make_dtc_flow.py OUT.json                  with an own MQTT broker config node for localhost:1883
  make_dtc_flow.py OUT.json --broker-id ID   use an existing broker config node of your Node-RED instead
  make_dtc_flow.py OUT.json --device-id ID   always use this WiCAN (needed with several WiCANs on one broker)

Needs the Node-RED palettes node-red-dashboard and node-red-node-ui-table.

The firmware reads/clears the trouble codes on the MQTT commands {"cmd":"read_dtc"} and
{"cmd":"clear_dtc"} (topic wican/<device id>/cmd) and publishes the result on <topic>/dtc.
"""
import argparse
import json

parser = argparse.ArgumentParser()
parser.add_argument("output")
parser.add_argument("--broker-id", default="", help="id of an existing mqtt-broker config node")
parser.add_argument("--topic", default="wican/sprinter/engine", help="AutoPID group topic of the WiCAN")
parser.add_argument("--device-id", default="", help="device id of the WiCAN, otherwise the only one online is used")
parser.add_argument("--texts", default="", help="JSON file {\"CODE\": \"text\"} for the local plain text table")
args = parser.parse_args()
texts = json.load(open(args.texts, encoding="utf-8")) if args.texts else {}

BROKER = args.broker_id or "w906_mqtt_broker"
CSS = """<style>
.w906-small, .w906-small p, .w906-small span, .w906-small .label, .w906-small .value,
.w906-small button, .w906-small .md-button, .w906-small .tabulator, .w906-small .tabulator * {
    font-size: 14px !important; line-height: 1.3 !important; text-transform: none !important;
}
.w906-small .value { white-space: normal !important; }
</style>"""
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
            "name": label, "label": label, "format": "{{msg.payload}}", "layout": "row-left", "className": "w906-small",
            "style": False, "font": "", "fontSize": 16, "color": "#000000", "x": x, "y": y, "wires": []}


def function(nid, name, code, outputs, wires, x, y):
    return {"id": nid, "type": "function", "z": TAB, "name": name, "func": code, "outputs": outputs, "timeout": 0,
            "noerr": 0, "initialize": "", "finalize": "", "libs": [], "x": x, "y": y, "wires": wires}


# The device id is needed for the command topic, it comes from the retained status messages
nodes.append({"id": "w906_dtc_status_in", "type": "mqtt in", "z": TAB, "name": "WiCAN Status", "topic": "wican/+/can/status",
              "qos": "0", "datatype": "json", "broker": BROKER, "nl": False, "rap": True, "rh": 0, "inputs": 0,
              "x": 140, "y": 60, "wires": [["w906_dtc_remember"]]})
nodes.append(function("w906_dtc_remember", "WiCAN merken",
                      "// Commands go to the WiCAN given with --device-id, otherwise to the only one online\n"
                      "const FIXED_ID = %s;\n"
                      "const devices = flow.get('wican_devices') || {};\n"
                      "devices[msg.topic.split('/')[1]] = (msg.payload && msg.payload.status) || '?';\n"
                      "flow.set('wican_devices', devices);\n"
                      "const online = Object.keys(devices).filter(id => devices[id] === 'online');\n"
                      "if (FIXED_ID) {\n"
                      "    flow.set('wican_id', FIXED_ID);\n"
                      "    msg.payload = FIXED_ID + ' (' + (devices[FIXED_ID] || '?') + ')';\n"
                      "} else if (online.length === 1) {\n"
                      "    flow.set('wican_id', online[0]);\n"
                      "    msg.payload = online[0] + ' (online)';\n"
                      "} else {\n"
                      "    flow.set('wican_id', null);\n"
                      "    msg.payload = online.length ? online.length + ' WiCANs online, Flow mit --device-id erzeugen'\n"
                      "                                : 'kein WiCAN online';\n"
                      "}\n"
                      "return msg;" % json.dumps(args.device_id or None), 1, [["w906_dtc_ui_device"]], 360, 60))
nodes.append(text("w906_dtc_ui_device", "WiCAN", 1, 600, 60))

nodes.append({"id": "w906_dtc_btn_read", "type": "ui_button", "z": TAB, "name": "Lesen", "group": GROUP_ACTIONS, "order": 4,
              "width": 3, "height": 1, "passthru": False, "label": "Lesen", "tooltip": "Fehlerspeicher aller Steuergeräte lesen", "color": "",
              "bgcolor": "", "className": "w906-small", "icon": "fa-search", "payload": "read_dtc", "payloadType": "str",
              "topic": "", "topicType": "str", "x": 150, "y": 140, "wires": [["w906_dtc_command"]]})
nodes.append({"id": "w906_dtc_btn_clear", "type": "ui_button", "z": TAB, "name": "Löschen", "group": GROUP_ACTIONS, "order": 5,
              "width": 3, "height": 1, "passthru": False, "label": "Löschen", "tooltip": "Fehlerspeicher löschen (mit Sicherheitsabfrage)",
              "color": "", "bgcolor": "#ca3838", "className": "w906-small", "icon": "fa-trash", "payload": "clear", "payloadType": "str",
              "topic": "", "topicType": "str", "x": 150, "y": 200, "wires": [["w906_dtc_confirm_text"]]})
nodes.append(function("w906_dtc_confirm_text", "Sicherheitsabfrage",
                      "msg.payload = 'Löscht die Fehlerspeicher aller Steuergeräte mit Einträgen, auch Motor, Getriebe, ESP '\n"
                      "    + 'und Airbag (WiCAN ' + (flow.get('wican_id') || '–') + '). Freeze-Frame-Daten und Readiness '\n"
                      "    + 'gehen verloren, die Codes deshalb vorher lesen und notieren. Nur bei Zündung an, Motor aus '\n"
                      "    + 'und Fahrzeug im Stand.';\n"
                      "return msg;", 1, [["w906_dtc_confirm"]], 360, 200))
nodes.append({"id": "w906_dtc_confirm", "type": "ui_toast", "z": TAB, "position": "dialog", "displayTime": "3",
              "highlight": "", "sendall": False, "outputs": 1, "ok": "Löschen", "cancel": "Abbrechen", "raw": False,
              "className": "", "topic": "Fehlerspeicher löschen?", "name": "Bestätigung", "x": 570, "y": 200,
              "wires": [["w906_dtc_confirmed"]]})
nodes.append(function("w906_dtc_confirmed", "bestätigt?",
                      "if (msg.payload !== 'Löschen') return null;\nmsg.payload = 'clear_dtc';\nreturn msg;",
                      1, [["w906_dtc_command"]], 760, 200))
nodes.append(function("w906_dtc_command", "Befehl an WiCAN",
                      "const action = msg.payload === 'clear_dtc' ? 'Löschen' : 'Lesen';\n"
                      "const id = flow.get('wican_id');\n"
                      "if (!id) return [null, { payload: action + ': kein WiCAN ausgewählt' }, null];\n"
                      "flow.set('dtc_action', action);\n"
                      "return [{ topic: 'wican/' + id + '/cmd', payload: JSON.stringify({ cmd: msg.payload }) },\n"
                      "        { payload: action + ' …' },\n"
                      "        { payload: 'start' }];",
                      3, [["w906_dtc_mqtt_out"], ["w906_dtc_ui_state"], ["w906_dtc_watchdog"]], 980, 160))
nodes.append({"id": "w906_dtc_mqtt_out", "type": "mqtt out", "z": TAB, "name": "Befehl", "topic": "", "qos": "0",
              "retain": "false", "respTopic": "", "contentType": "", "userProps": "", "correl": "", "expiry": "",
              "broker": BROKER, "x": 1190, "y": 120, "wires": []})
# Progress messages restart the timer, the result or an error stops it
nodes.append({"id": "w906_dtc_watchdog", "type": "trigger", "z": TAB, "name": "keine Antwort?", "op1": "",
              "op2": "keine Antwort vom WiCAN", "op1type": "nul", "op2type": "str", "duration": "60", "extend": True,
              "overrideDelay": False, "units": "s", "reset": "", "bytopic": "all", "topic": "topic", "outputs": 1,
              "x": 1000, "y": 300, "wires": [["w906_dtc_ui_state"]]})

nodes.append({"id": "w906_dtc_result_in", "type": "mqtt in", "z": TAB, "name": "Fehlerspeicher", "topic": DTC_TOPIC,
              "qos": "0", "datatype": "json", "broker": BROKER, "nl": False, "rap": False, "rh": 0, "inputs": 0,
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
const NRC = { '11': 'Dienst nicht unterstützt', '12': 'Unterfunktion nicht unterstützt', '21': 'beschäftigt',
    '22': 'Bedingungen nicht erfüllt', '31': 'außerhalb des Bereichs', '33': 'Zugriff verweigert' };
const REASON = { ecu_offline: 'keine Antwort vom Motorsteuergerät, Zündung an?',
    engine_running: 'Motor läuft, Löschen nur bei Motor aus', engine_state_unknown: 'Drehzahl nicht lesbar, nichts gelöscht',
    busy: 'läuft bereits', out_of_memory: 'zu wenig Speicher im WiCAN' };
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
function ecuStatus(status) {
    if (status === 'no_response') return 'keine Antwort';
    if (status === 'pending_timeout') return 'Antwort ausstehend (Zeitüberschreitung)';
    if (status === 'incomplete') return 'Antwort unvollständig';
    const m = /^nrc_([0-9A-F]{2})$/.exec(status || '');
    if (m) return (NRC[m[1]] || 'negative Antwort') + ' (' + status + ')';
    return status || '?';
}
function plainText(texts, code) {
    return texts[code] || texts[code.split('-')[0]] || '–';
}

const p = msg.payload || {};
const texts = msg.texts || {};
const action = p.action === 'clear' ? 'Löschen' : (p.action === 'read' ? 'Lesen' : (flow.get('dtc_action') || 'Befehl'));
if (p.state === 'running') {
    flow.set('dtc_seen_running', true);
    return [{ payload: action + ' ' + (p.ecu || 0) + '/' + (p.total || '?') }, null, null, { payload: 'running' }];
}
if (p.state === 'error' || p.error) {
    flow.set('dtc_seen_running', false);
    const reason = p.error ? 'Ergebnis zu groß für MQTT' : (REASON[p.reason] || p.reason || 'Fehler');
    return [{ payload: action + ': ' + reason }, null, null, { reset: true }];
}
if (p.state !== 'done' || !Array.isArray(p.ecus)) return null;
// A result without progress messages before it is the retained one of an earlier scan
const fresh = flow.get('dtc_seen_running') === true && msg.retain !== true;
flow.set('dtc_seen_running', false);
const rows = [];
let silent = 0, refused = 0, incomplete = 0, notCleared = 0, omitted = 0;
for (const ecu of p.ecus) {
    const dtcs = ecu.dtcs || [];
    const unconfirmed = p.action === 'clear' && ecu.cleared === false && dtcs.length > 0;
    if (unconfirmed) notCleared++;
    for (const dtc of dtcs) {
        rows.push({ ecu: ecu.name, code: dtc.code, text: plainText(texts, dtc.code), info: classify(dtc, ecu.protocol),
                    status: statusText(dtc) + (unconfirmed ? ', Löschen nicht bestätigt' : '') });
    }
    if (ecu.dtcs_omitted) {
        omitted++;
        rows.push({ ecu: ecu.name, code: '…', text: ecu.dtcs_omitted + ' Codes nicht übertragen (Liste zu lang für MQTT)',
                    info: '', status: 'mit Diagnosegerät lesen' });
    }
    if (ecu.status !== 'ok') {
        if (ecu.status === 'incomplete') incomplete++;
        else if (/^nrc_/.test(ecu.status || '')) refused++;
        else silent++;
        rows.push({ ecu: ecu.name, code: '–', text: '–', info: '', status: ecuStatus(ecu.status) });
    }
}
const count = p.dtc_count || 0;
// Without any trouble code or problem list every control unit, so the check is visible
if (count === 0 && rows.length === 0) {
    for (const ecu of p.ecus) rows.push({ ecu: ecu.name, code: '–', text: '–', info: '', status: 'i.O.' });
}
const notes = [];
if (silent) notes.push(silent + ' ohne Antwort');
if (refused) notes.push(refused + ' verweigert');
if (incomplete) notes.push(incomplete + ' unvollständig');
if (notCleared) notes.push(notCleared + ' nicht gelöscht');
if (omitted) notes.push('Liste gekürzt');
const summary = (count === 0 ? 'keine Fehler' : count + ' Fehler') + (notes.length ? ', ' + notes.join(', ') : '');
if (!fresh) {
    return [{ payload: 'gespeichertes Ergebnis (' + action + '), Zeitpunkt unbekannt' }, { payload: summary },
            { payload: rows }, { reset: true }];
}
const when = new Date().toLocaleString('de-DE', { day: '2-digit', month: '2-digit', hour: '2-digit', minute: '2-digit' });
return [{ payload: action + ' fertig, ' + Math.round((p.duration_ms || 0) / 1000) + ' s' },
        { payload: summary + ' (' + when + ')' },
        { payload: rows },
        { reset: true }];"""
nodes.append(function("w906_dtc_texts", "Klartexte (lokal)", texts_code % json.dumps(texts, indent=4, ensure_ascii=False), 1,
                      [["w906_dtc_evaluate"]], 330, 360))
nodes.append(function("w906_dtc_evaluate", "Ergebnis auswerten", evaluate, 4,
                      [["w906_dtc_ui_state"], ["w906_dtc_ui_summary"], ["w906_dtc_ui_table"], ["w906_dtc_watchdog"]], 560, 300))
nodes.append(text("w906_dtc_ui_state", "Status", 2, 1230, 260))
nodes.append(text("w906_dtc_ui_summary", "Ergebnis", 3, 800, 320))
nodes.append({"id": "w906_dtc_ui_table", "type": "ui_table", "z": TAB, "group": GROUP_RESULT, "name": "Fehlerliste",
              "order": 1, "width": 24, "height": 10,
              "columns": [{"field": "ecu", "title": "Steuergerät", "width": "210", "align": "left", "formatter": "plaintext", "formatterParams": {"target": "_blank"}},
                          {"field": "code", "title": "Fehlercode", "width": "100", "align": "left", "formatter": "plaintext", "formatterParams": {"target": "_blank"}},
                          {"field": "text", "title": "Klartext (lokal)", "width": "", "align": "left", "formatter": "textarea", "formatterParams": {"target": "_blank"}},
                          {"field": "info", "title": "Einordnung", "width": "260", "align": "left", "formatter": "textarea", "formatterParams": {"target": "_blank"}},
                          {"field": "status", "title": "Status", "width": "160", "align": "left", "formatter": "plaintext", "formatterParams": {"target": "_blank"}}],
              "outputs": 0, "cts": False, "className": "w906-small", "x": 800, "y": 380, "wires": []})
nodes.append({"id": "w906_dtc_css", "type": "ui_template", "z": TAB, "group": "", "name": "Schriftgröße", "order": 0,
              "width": 0, "height": 0, "format": CSS, "storeOutMessages": True, "fwdInMessages": True,
              "resendOnRefresh": True, "templateScope": "global", "className": "", "x": 150, "y": 440, "wires": [[]]})

ids = [n["id"] for n in nodes]
assert len(ids) == len(set(ids))
with open(args.output, "w", encoding="utf-8") as handle:
    json.dump(nodes, handle, indent=2, ensure_ascii=False)
    handle.write("\n")
print("%d nodes written to %s" % (len(nodes), args.output))
