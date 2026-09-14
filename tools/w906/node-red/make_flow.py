#!/usr/bin/env python3
"""Generate the Node-RED dashboard flow for the WiCAN W906 profile.

  make_flow.py OUT.json                         with an own MQTT broker config node for localhost:1883
  make_flow.py OUT.json --broker-id ID           use an existing broker config node of your Node-RED instead
"""
import argparse
import json

parser = argparse.ArgumentParser()
parser.add_argument("output")
parser.add_argument("--broker-id", default="", help="id of an existing mqtt-broker config node")
parser.add_argument("--topic", default="wican/sprinter/engine")
args = parser.parse_args()

BROKER = args.broker_id or "w906_mqtt_broker"
TAB, UI_TAB = "w906_flow_tab", "w906_ui_tab"
G = {"status": ("w906_grp_status", "Status", 6, 1), "engine": ("w906_grp_engine", "Motor", 6, 2),
     "fuel": ("w906_grp_fuel", "Kraftstoff", 6, 3), "air": ("w906_grp_air", "Ladeluft", 6, 4),
     "exhaust": ("w906_grp_exhaust", "Abgas", 6, 5), "dpf": ("w906_grp_dpf", "DPF / AGR", 6, 6),
     "charts": ("w906_grp_charts", "Verlauf", 12, 7)}

nodes = [{"id": TAB, "type": "tab", "label": "Sprinter Motor", "disabled": False,
          "info": "Live-Daten WiCAN W906 via MQTT " + args.topic},
         {"id": UI_TAB, "type": "ui_tab", "name": "Sprinter", "icon": "fa-truck", "order": 20, "disabled": False, "hidden": False}]
if not args.broker_id:
    nodes.append({"id": BROKER, "type": "mqtt-broker", "name": "Venus OS MQTT", "broker": "localhost", "port": "1883",
                  "clientid": "", "autoConnect": True, "usetls": False, "protocolVersion": "4", "keepalive": "60",
                  "cleansession": True, "autoUnsubscribe": True, "birthTopic": "", "birthQos": "0", "birthPayload": "",
                  "birthMsg": {}, "closeTopic": "", "closeQos": "0", "closePayload": "", "closeMsg": {}, "willTopic": "",
                  "willQos": "0", "willPayload": "", "willMsg": {}, "userProps": "", "sessionExpiry": ""})
for gid, name, width, order in G.values():
    nodes.append({"id": gid, "type": "ui_group", "name": name, "tab": UI_TAB, "order": order, "disp": True,
                  "width": str(width), "collapse": False, "className": ""})

# name, label, unit, group, kind, min, max, sectors
VALUES = [
    ("ENGINE_RPM", "Drehzahl", "1/min", "engine", "gauge", 0, 4500, (3500, 4200)),
    ("ACCEL_PEDAL", "Fahrpedal", "%", "engine", "gauge", 0, 100, (80, 95)),
    ("COOLANT_TMP", "Kühlmittel", "°C", "engine", "gauge", 0, 120, (60, 105)),
    ("ENGINE_OIL_TEMP", "Motoröl", "°C", "engine", "gauge", 0, 140, (60, 120)),
    ("OIL_LEVEL", "Ölstand", "mm", "engine", "text", None, None, None),
    ("ECU_DISTANCE", "Laufleistung (ECU)", "km", "engine", "text", None, None, None),
    ("RAIL_PRESSURE", "Raildruck", "bar", "fuel", "gauge", 0, 2000, (1600, 1850)),
    ("INJECTION_QUANTITY", "Einspritzmenge", "mg", "fuel", "text", None, None, None),
    ("FUEL_L", "Tankinhalt", "L", "fuel", "text", None, None, None),
    ("FUEL_TEMP", "Kraftstoff", "°C", "fuel", "text", None, None, None),
    ("BOOST_PRESSURE", "Ladedruck", "hPa", "air", "gauge", 900, 3000, (2500, 2800)),
    ("BOOST_PRESSURE_LP", "Ladedruck nach ND-Lader", "hPa", "air", "text", None, None, None),
    ("AIR_MASS_PER_STROKE", "Luftmasse", "mg/Hub", "air", "text", None, None, None),
    ("EGR_RATE", "AGR-Rate", "%", "air", "text", None, None, None),
    ("WASTEGATE", "Wastegate", "%", "air", "text", None, None, None),
    ("INTAKE_AIR_TMP", "Ansaugluft", "°C", "air", "text", None, None, None),
    ("INTAKE_AIR_PRESSURE", "Ansaugluftdruck", "hPa", "air", "text", None, None, None),
    ("BARO_PRESSURE", "Atmosphärendruck", "hPa", "air", "text", None, None, None),
    ("CHARGE_AIR_TEMP_PRE_IC", "Ladeluft vor LLK", "°C", "air", "text", None, None, None),
    ("CHARGE_AIR_TEMP_POST_IC", "Ladeluft nach LLK", "°C", "air", "text", None, None, None),
    ("EGT_PRE_TURBO", "vor Turbo", "°C", "exhaust", "gauge", 0, 800, (600, 750)),
    ("EGT_PRE_CAT", "vor Kat", "°C", "exhaust", "gauge", 0, 700, (550, 650)),
    ("EGT_PRE_DPF", "vor DPF", "°C", "exhaust", "gauge", 0, 700, (550, 650)),
    ("EGT_PRE_SCR", "vor SCR", "°C", "exhaust", "gauge", 0, 600, (450, 550)),
    ("EXHAUST_BACK_PRESSURE", "Abgasgegendruck", "hPa", "exhaust", "gauge", 900, 3000, (2200, 2600)),
    ("DPF_DIFF_PRESSURE", "DPF Differenzdruck", "hPa", "exhaust", "gauge", 0, 200, (100, 160)),
    ("EGT_POST_EGR_COOLER", "nach AGR-Kühler", "°C", "exhaust", "text", None, None, None),
    ("LAMBDA", "Lambda", "", "exhaust", "text", None, None, None),
    ("THROTTLE", "Drosselklappe", "%", "air", "text", None, None, None),
    ("EGR_VALVE", "AGR-Ventil", "%", "dpf", "text", None, None, None),
    ("DPF_SOOT_MASS", "Rußmasse (gemessen)", "g", "dpf", "gauge", 0, 8, (6, 7.5)),
    ("DPF_SOOT_SIM", "Rußmasse (simuliert)", "g", "dpf", "text", None, None, None),
    ("DPF_KM_SINCE_REGEN", "km seit Regeneration", "km", "dpf", "text", None, None, None),
    ("DPF_ASH", "Aschegehalt DPF", "g", "dpf", "text", None, None, None),
    ("DPF_REGEN_STATUS", "Regeneration", "", "dpf", "text", None, None, None),
]

nodes.append({"id": "w906_mqtt_engine", "type": "mqtt in", "z": TAB, "name": "WiCAN Motordaten", "topic": args.topic,
              "qos": "0", "datatype": "json", "broker": BROKER, "nl": False, "rap": True, "rh": 0, "inputs": 0,
              "x": 150, "y": 100, "wires": [["w906_split", "w906_charts_fn", "w906_live_trigger"]]})

meta = {v[0]: {"kind": v[4], "unit": v[2], "digits": 2 if v[0] == "LAMBDA" else (0 if v[0] in ("OIL_LEVEL", "ECU_DISTANCE", "FUEL_L", "BOOST_PRESSURE_LP", "INTAKE_AIR_PRESSURE", "BARO_PRESSURE", "AIR_MASS_PER_STROKE", "DPF_KM_SINCE_REGEN", "DPF_REGEN_STATUS") else 1)} for v in VALUES}
split_code = (
    "// The WiCAN sends {\"ecu\":\"offline\"} instead of stale values while the vehicle is asleep\n"
    "const meta = %s;\n"
    "const keys = %s;\n"
    "const p = msg.payload || {};\n"
    "const offline = p.ecu === 'offline';\n"
    "const out = keys.map(k => {\n"
    "    const m = meta[k];\n"
    "    if (offline) return { topic: k, payload: m.kind === 'gauge' ? 0 : '–' };\n"
    "    if (typeof p[k] !== 'number') return null;\n"
    "    if (m.kind === 'gauge') return { topic: k, payload: p[k] };\n"
    "    if (k === 'DPF_REGEN_STATUS') return { topic: k, payload: p[k] === 1 ? 'nicht aktiv' : 'Code ' + p[k] };\n    return { topic: k, payload: (p[k].toFixed(m.digits) + ' ' + m.unit).trim() };\n"
    "});\n"
    "out.push({ payload: offline ? 'schläft (IGN aus)' : 'aktiv' });\n"
    "return out;" % (json.dumps(meta, ensure_ascii=False), json.dumps([v[0] for v in VALUES])))

wires = []
y = 40
for i, (name, label, unit, grp, kind, lo, hi, sectors) in enumerate(VALUES):
    nid = "w906_ui_%s" % name.lower()
    wires.append([nid])
    order = sum(1 for v in VALUES[:i + 1] if v[3] == grp)
    if kind == "gauge":
        nodes.append({"id": nid, "type": "ui_gauge", "z": TAB, "name": label, "group": G[grp][0], "order": order,
                      "width": 3, "height": 3, "gtype": "gage", "title": label, "label": unit,
                      "format": "{{value | number:0}}", "min": lo, "max": hi,
                      "colors": ["#00b500", "#e6e600", "#ca3838"], "seg1": str(sectors[0]), "seg2": str(sectors[1]),
                      "diff": False, "className": "", "x": 620, "y": y, "wires": []})
    else:
        nodes.append({"id": nid, "type": "ui_text", "z": TAB, "group": G[grp][0], "order": order, "width": 6,
                      "height": 1, "name": label, "label": label, "format": "{{msg.payload}}", "layout": "row-spread",
                      "className": "", "style": False, "font": "", "fontSize": 16, "color": "#000000",
                      "x": 620, "y": y, "wires": []})
    y += 40
wires.append(["w906_ui_ecu"])
nodes.append({"id": "w906_split", "type": "function", "z": TAB, "name": "Werte verteilen", "func": split_code,
              "outputs": len(VALUES) + 1, "timeout": 0, "noerr": 0, "initialize": "", "finalize": "", "libs": [],
              "x": 380, "y": 300, "wires": wires})

charts_code = (
    "const p = msg.payload || {};\n"
    "const temps = {COOLANT_TMP: 'Kühlmittel', ENGINE_OIL_TEMP: 'Öl', FUEL_TEMP: 'Kraftstoff'};\n"
    "const egt = {EGT_PRE_TURBO: 'vor Turbo', EGT_PRE_CAT: 'vor Kat', EGT_PRE_DPF: 'vor DPF', EGT_PRE_SCR: 'vor SCR'};\n"
    "const series = map => Object.keys(map).filter(k => typeof p[k] === 'number').map(k => ({ topic: map[k], payload: p[k] }));\n"
    "return [series(temps), series(egt)];")
nodes.append({"id": "w906_charts_fn", "type": "function", "z": TAB, "name": "Diagrammdaten", "func": charts_code,
              "outputs": 2, "timeout": 0, "noerr": 0, "initialize": "", "finalize": "", "libs": [], "x": 380, "y": 680,
              "wires": [["w906_chart_temp"], ["w906_chart_egt"]]})
for cid, label, ymax, cy, order in (("w906_chart_temp", "Temperaturen (°C)", "130", 660, 1),
                                    ("w906_chart_egt", "Abgastemperaturen (°C)", "800", 700, 2)):
    nodes.append({"id": cid, "type": "ui_chart", "z": TAB, "name": label, "group": G["charts"][0], "order": order,
                  "width": 12, "height": 5, "label": label, "chartType": "line", "legend": "true", "xformat": "HH:mm",
                  "interpolate": "linear", "nodata": "keine Daten", "dot": False, "ymin": "0", "ymax": ymax,
                  "removeOlder": "60", "removeOlderPoints": "", "removeOlderUnit": "60", "cutout": 0,
                  "useOneColor": False, "useUTC": False,
                  "colors": ["#1f77b4", "#ff7f0e", "#2ca02c", "#d62728", "#9467bd", "#8c564b", "#e377c2", "#7f7f7f", "#bcbd22"],
                  "outputs": 1, "useDifferentColor": False, "className": "", "x": 640, "y": cy, "wires": [[]]})

def status_text(nid, label, order, y):
    return {"id": nid, "type": "ui_text", "z": TAB, "group": G["status"][0], "order": order, "width": 6, "height": 1,
            "name": label, "label": label, "format": "{{msg.payload}}", "layout": "row-spread", "className": "",
            "style": False, "font": "", "fontSize": 16, "color": "#000000", "x": 620, "y": y, "wires": []}

nodes.append(status_text("w906_ui_ecu", "ECU", 2, 620))
nodes.append({"id": "w906_live_trigger", "type": "trigger", "z": TAB, "name": "Datenfluss", "op1": "live",
              "op2": "keine Daten (>30 s)", "op1type": "str", "op2type": "str", "duration": "30", "extend": True,
              "overrideDelay": False, "units": "s", "reset": "", "bytopic": "all", "topic": "topic", "outputs": 1,
              "x": 380, "y": 760, "wires": [["w906_ui_data"]]})
nodes.append(status_text("w906_ui_data", "Daten", 3, 760))
nodes.append({"id": "w906_mqtt_status", "type": "mqtt in", "z": TAB, "name": "WiCAN Status", "topic": "wican/+/can/status",
              "qos": "0", "datatype": "json", "broker": BROKER, "nl": False, "rap": True, "rh": 0, "inputs": 0,
              "x": 140, "y": 820, "wires": [["w906_status_fn"]]})
nodes.append({"id": "w906_status_fn", "type": "function", "z": TAB, "name": "Status-Text",
              "func": "msg.payload = (msg.payload && msg.payload.status) || String(msg.payload);\nreturn msg;",
              "outputs": 1, "timeout": 0, "noerr": 0, "initialize": "", "finalize": "", "libs": [], "x": 380, "y": 820,
              "wires": [["w906_ui_wican"]]})
nodes.append(status_text("w906_ui_wican", "WiCAN", 1, 820))

ids = [n["id"] for n in nodes]
assert len(ids) == len(set(ids))
with open(args.output, "w", encoding="utf-8") as handle:
    json.dump(nodes, handle, indent=2, ensure_ascii=False)
    handle.write("\n")
print("%d nodes written to %s" % (len(nodes), args.output))
