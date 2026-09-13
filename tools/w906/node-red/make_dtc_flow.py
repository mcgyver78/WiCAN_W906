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
args = parser.parse_args()

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
     "width": "12", "collapse": False, "className": ""},
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
              "x": 140, "y": 300, "wires": [["w906_dtc_evaluate"]]})
evaluate = """const p = msg.payload || {};
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
        rows.push({ ecu: ecu.name, code: dtc.code, status: (dtc.active ? 'aktiv' : 'gespeichert') + ' (0x' + dtc.status + ')' });
    }
    if (ecu.status !== 'ok') {
        silent++;
        rows.push({ ecu: ecu.name, code: '–', status: ecu.status === 'no_response' ? 'keine Antwort' : ecu.status });
    }
}
const count = p.dtc_count || 0;
const when = new Date().toLocaleString('de-DE');
const summary = count === 0 ? 'keine Fehler gespeichert' : count + (count === 1 ? ' Fehler' : ' Fehler') + ' gespeichert';
return [{ payload: action + ' fertig (' + Math.round((p.duration_ms || 0) / 1000) + ' s)' },
        { payload: summary + (silent ? ', ' + silent + ' Steuergerät(e) ohne Antwort' : '') + ' – ' + when },
        { payload: rows }];"""
nodes.append(function("w906_dtc_evaluate", "Ergebnis auswerten", evaluate, 3,
                      [["w906_dtc_ui_state"], ["w906_dtc_ui_summary"], ["w906_dtc_ui_table"]], 380, 300))
nodes.append(text("w906_dtc_ui_state", "Status", 2, 1010, 260))
nodes.append(text("w906_dtc_ui_summary", "Ergebnis", 3, 620, 320))
nodes.append({"id": "w906_dtc_ui_table", "type": "ui_table", "z": TAB, "group": GROUP_RESULT, "name": "Fehlerliste",
              "order": 1, "width": 12, "height": 8,
              "columns": [{"field": "ecu", "title": "Steuergerät", "width": "", "align": "left", "formatter": "plaintext", "formatterParams": {"target": "_blank"}},
                          {"field": "code", "title": "Fehlercode", "width": "140", "align": "left", "formatter": "plaintext", "formatterParams": {"target": "_blank"}},
                          {"field": "status", "title": "Status", "width": "200", "align": "left", "formatter": "plaintext", "formatterParams": {"target": "_blank"}}],
              "outputs": 0, "cts": False, "x": 620, "y": 360, "wires": []})

ids = [n["id"] for n in nodes]
assert len(ids) == len(set(ids))
with open(args.output, "w", encoding="utf-8") as handle:
    json.dump(nodes, handle, indent=2, ensure_ascii=False)
    handle.write("\n")
print("%d nodes written to %s" % (len(nodes), args.output))
