#!/usr/bin/env python3
"""Stand-in for a WiCAN adapter with the W906 firmware: the HTTP API of API.md without hardware.

A client, for example a display, can be developed and tested against it. It answers
GET /api/state, POST /api/dtc, GET /api/dtc/result, GET /autopid_data and GET /load_car_config
as API.md describes them, these paths with another method with 405 and everything else with
404, and it ends the connection after a 404 or 405 as the HTTP server of the firmware does.
test_api_contract.py tests it against API.md and, through dtc_state_cli.c, against the rules of
the firmware module main/dtc_state.c.

  python3 mock_wican.py                          http://127.0.0.1:8906, a scan takes 35 s
  python3 mock_wican.py --speed 10               the clock of the adapter runs 10 times faster
  python3 mock_wican.py --scenario ignition_off  see --list
  python3 mock_wican.py --fault no_busy          an adapter that breaks one rule of the contract
  python3 mock_wican.py --bind 0.0.0.0           reachable for a display in the same network
  python3 mock_wican.py --list                   scenarios and faults

In a test:

  server = mock_wican.Server(mock_wican.Adapter("engine_running")).start()
  ... requests to server.host (127.0.0.1), server.port ...
  server.adapter.clock.advance(40)               no real waiting, the clock is simulated
  server.adapter.set(ignition=False)             the situation changes from now on
  server.close()

Taken from the sources: the rules of a scan (main/dtc_state.h, written a third time here, next to
main/dtc_state.c and the model in dtc_state_test.c), the 18 control units (main/autopid.c), the
format of a result (W906.md, "Fehlerspeicher") and the 35 values of the profile
vehicle_profiles/mercedes/sprinter_w906_om651.json with class and unit from
.vehicle_profiles/params.json.

Invented, because no contract fixes it. A client must not rely on any of this:
  - all values, trouble codes and the durations of the steps of a scan (0.3 s until the request is
    picked up, 0.7 s engine check, 1.9 s per control unit, 1.3 s for one that does not answer,
    0.8 s more for a control unit that is cleared);
  - for the first 40 ms of the engine check the scan is "running" with `ecu` 0 and `total` 0. The
    firmware never shows that: it sets "running", step 0 and the number of control units in one
    breath (autopid_task() in main/autopid.c). The mock is stricter on purpose, API.md allows a
    `total` of 0 and a client must not divide by it without looking;
  - ENGINE_RPM is the setting `rpm`, the other values only know "engine off" and "idling";
  - one request to the engine every 60 ms, a polling pass of 35 requests takes 2.1 s; the values
    are valid after the first complete pass; while a scan runs and with the ignition off neither
    `pass` nor the time of the last answer moves;
  - with the ignition off `sleep_in_s` counts down and stays at 0, the mock does not go to
    sleep. As in the firmware the rest is cut down to whole seconds (179 at once with 180 s to
    go, 0 in the last second already) and no request is accepted while it is 0;
  - GET /api/dtc/result answers 503 while `autopid` is "starting" as well as "off";
  - the profile without fault memory has the values of the W906 profile.

Not modelled: MQTT (a scan "over MQTT" is started by a scenario or by Adapter.mqtt_command()),
the wait of /load_car_config for the end of a polling pass, the sleep itself and the time the
adapter stays awake for a scan, the errors engine_state_unknown, out_of_memory,
result_serialize_failed and internal, and control units that answer with a negative response,
only "response pending" or incompletely.
Read in the sources of the firmware and not modelled either, a client has to expect all of it:
  - /autopid_data grows value by value during the first pass after the ignition (the firmware leaves
    out what it has not read yet), the mock answers {} and then all 35 values at once;
  - the firmware knows many more paths (the web interface), the mock answers 404 for them;
  - the firmware ends a connection of a client that no longer answers and, when all are taken,
    the one used least recently; the mock only ends it after a 404 or 405;
  - the firmware writes Content-Type and Content-Length before the other headers, the mock
    Content-Length last.
Taken from ESP-IDF v5.5.2 (esp_http_server), read and not measured on the device: status and
text of 404 and 405, the end of the connection after them, that HEAD is a method like any other
and gets the text as a body, and the Content-Type text/html of the empty 204.
"""
import argparse
import collections
import http.server
import json
import random
import re
import sys
import threading
import time

# main/dtc_state.h
SEQ_MAX = 2 ** 31 - 1
CLEAR_MAX_AGE_MS = 600 * 1000
HTTP_EXPIRY_MS = 20 * 1000
# API.md: all numbers are below 2^31
NUMBER_MAX = 2 ** 31 - 1
# main/autopid.c: a larger result does not fit into an MQTT message, clearing needs the engine off
RESULT_MAX_BYTES = 5 * 1024 - 1
CLEAR_MAX_RPM = 50
# main/dtc_http.c: the query is read into 64 bytes, a longer one is not read at all
QUERY_MAX_BYTES = 63

# The paths the mock has a handler for, and the answers of the HTTP server of the firmware itself:
# httpd_find_uri_handler() and httpd_resp_send_err() in ESP-IDF, main/config_server.c for the last two
KNOWN_PATHS = ("/api/state", "/api/dtc", "/api/dtc/result", "/autopid_data", "/load_car_config")
NOT_FOUND = (404, b"Nothing matches the given URI")
NOT_ALLOWED = (405, b"Specified method is invalid for this resource")
NO_DATA = '{"error":"No data available"}'
NO_CONFIG = (500, b"Failed to generate JSON")
# After these the firmware ends the connection, without a header that says so
ENDS_CONNECTION = (404, 405)

# Invented durations in ms, see above
PICKUP_MS = 300
PREPARE_MS = 40
ENGINE_CHECK_MS = 660
UNIT_MS = 1900
NO_ANSWER_MS = 1300
CLEAR_MS = 800
REQUEST_MS = 60
MQTT_FIRST_MS = 3000

# Name, request id and protocol of the control units: dtc_ecus[] in main/autopid.c, in scan order
ECUS = (
    ("N73 Elektronisches Zündschloss (EZS)", "4E0", "KWP"),
    ("N3/28 Motorelektronik (CDID3)", "7E0", "UDS"),
    ("Y3/8n4 Getriebesteuerung (NAG2)", "7E1", "KWP"),
    ("N15/5 Wählhebelmodul (EWM)", "788", "KWP"),
    ("N30/4 ESP", "784", "UDS"),
    ("N10 SAM", "662", "KWP"),
    ("N118/5 Kraftstoffpumpe (FSCU)", "778", "UDS"),
    ("N28/4 Anhängererkennung (AHE)", "730", "UDS"),
    ("N80 Mantelrohrmodul (MRM)", "792", "KWP"),
    ("N70 Dachbedieneinheit (DBE)", "667", "KWP"),
    ("N72/1 Oberes Bedienfeld (OBF)", "6A5", "UDS"),
    ("B162 Collision Prevention Assist", "65E", "UDS"),
    ("N87/8 Radio", "5D6", "UDS"),
    ("A2/30 Navigationsmodul", "633", "UDS"),
    ("A1 Kombiinstrument", "796", "KWP"),
    ("S98 Klimaanlage", "791", "KWP"),
    ("N2/14 Rückhaltesystem (SRS)", "6BC", "KWP"),
    ("N69/1 Fahrertür (TSG)", "6C8", "KWP"),
)

# Name, class, unit, value with the ignition on and the engine off, value with the engine idling.
# Names in the order of the profile, class and unit as make_car_data.py writes them.
# ENGINE_RPM is answered with the setting "rpm" instead.
PARAMETERS = (
    ("ENGINE_RPM", "frequency", "RPM", 0, 780),
    ("CHARGE_AIR_TEMP_PRE_IC", "temperature", "°C", 21.5, 38.25),
    ("CHARGE_AIR_TEMP_POST_IC", "temperature", "°C", 21.25, 27.5),
    ("EGT_PRE_TURBO", "temperature", "°C", 22, 148),
    ("EGT_POST_EGR_COOLER", "temperature", "°C", 21.75, 84.5),
    ("EGT_PRE_CAT", "temperature", "°C", 23, 131),
    ("EGT_PRE_DPF", "temperature", "°C", 24, 126),
    ("EGT_PRE_SCR", "temperature", "°C", 22, 109),
    ("FUEL_TEMP", "temperature", "°C", 20.75, 31.25),
    ("COOLANT_TMP", "temperature", "°C", 21.5, 84.75),
    ("ENGINE_OIL_TEMP", "temperature", "°C", 22.5, 88),
    ("OIL_LEVEL", "distance", "mm", 67.31, 61.94),
    ("LAMBDA", "none", "", 1, 3.42),
    ("DPF_DIFF_PRESSURE", "pressure", "hPa", 0, 6),
    ("RAIL_PRESSURE", "pressure", "bar", 0, 312),
    ("BOOST_PRESSURE", "pressure", "hPa", 1008.83, 1024.45),
    ("BOOST_PRESSURE_LP", "pressure", "hPa", 1008.83, 1019.57),
    ("EXHAUST_BACK_PRESSURE", "pressure", "hPa", 1010, 1042),
    ("BARO_PRESSURE", "pressure", "hPa", 1008.83, 1008.83),
    ("INTAKE_AIR_PRESSURE", "pressure", "hPa", 1009, 1003),
    ("INTAKE_AIR_TMP", "temperature", "°C", 20.75, 24.5),
    ("INJECTION_QUANTITY", "none", "mg", 0, 7.52),
    ("AIR_MASS_PER_STROKE", "none", "mg", 0, 412.5),
    ("EGR_RATE", "none", "%", 0, 31.4),
    ("ACCEL_PEDAL", "none", "%", 0, 0),
    ("WASTEGATE", "none", "%", 0, 62.5),
    ("FUEL_L", "none", "L", 54, 54),
    ("ECU_DISTANCE", "distance", "km", 187432, 187432),
    ("THROTTLE", "none", "%", 5.47, 5.47),
    ("EGR_VALVE", "none", "%", 0, 28.13),
    ("DPF_ASH", "weight", "g", 12.52, 12.52),
    ("DPF_KM_SINCE_REGEN", "distance", "km", 312, 312),
    ("DPF_REGEN_STATUS", "none", "", 1, 1),
    ("DPF_SOOT_MASS", "weight", "g", 4.38, 4.41),
    ("DPF_SOOT_SIM", "weight", "g", 6.21, 6.24),
)

# With the engine running these values move a little from pass to pass, a client sees them change
MOVING = {"ENGINE_RPM": 6, "RAIL_PRESSURE": 3, "AIR_MASS_PER_STROKE": 1.25}

# Settings of an adapter. A scenario and the keyword arguments of Adapter() and Adapter.set()
# replace single ones.
SETTINGS = {
    "id": "a1b2c3d4e5f6",
    "fw": "4.21",               # the example of API.md; a build of this repository reports the text of `git` here
    "git": "w906-v1.4.0-9-g0123abc",
    "api": True,                # False: upstream firmware, no path below /api/
    "autopid": "run",           # "off", "starting", "run"
    "supported": True,          # the vehicle profile has a fault memory table
    "ignition": True,
    "rpm": 0,
    "memory": "codes",          # what the control units have stored: "codes", "empty", "many"
    "mqtt": "connected",        # "off", "connected", "disconnected"
    "batt_mv": None,            # None: by the situation, negative: not measured
    "sleep_after_s": None,      # None: the adapter never counts down to sleep
    "pickup_ms": PICKUP_MS,     # how long an accepted request waits for the AutoPID task
    "mqtt_every_s": None,       # somebody sends read_dtc over MQTT again and again
    "restart_every_s": None,    # the adapter restarts again and again
    "boot": None,               # None: a random boot number
    "seq_seed": None,           # None: random sequence numbers
}

# Read once, when the adapter is switched on
AT_POWER_ON = ("boot", "seq_seed", "mqtt_every_s", "restart_every_s")

SCENARIOS = collections.OrderedDict((
    ("codes", ("ignition on, engine off, 5 trouble codes in UDS and KWP control units (default)", {})),
    ("no_codes", ("ignition on, engine off, no trouble codes: a clear ends with nothing_to_clear",
                  {"memory": "empty"})),
    ("many_codes", ("165 trouble codes, the result is shortened with dtcs_omitted",
                    {"memory": "many"})),
    ("ignition_off", ("/autopid_data is {}, ecu offline, a read ends with ecu_offline, sleep_in_s counts down",
                      {"ignition": False, "sleep_after_s": 180})),
    ("engine_running", ("values move, a read works, a clear ends with engine_running", {"rpm": 780})),
    ("starting", ("the AutoPID task is not in its loop yet: autopid starting, POST and result answer 503",
                  {"autopid": "starting"})),
    ("autopid_off", ("the protocol is not AutoPID: autopid off, no values", {"autopid": "off"})),
    ("mqtt_scan", ("a read_dtc over MQTT 3 s after boot and then every 60 s: src mqtt, a POST meanwhile is busy",
                   {"mqtt_every_s": 60})),
    ("unsupported", ("a profile without fault memory: supported false, a scan ends with not_supported",
                     {"supported": False})),
    ("restart", ("the adapter restarts every 60 s: new boot number, result gone", {"restart_every_s": 60})),
    ("stalled", ("the AutoPID task takes 25 s to pick a request up: an HTTP request expires",
                 {"pickup_ms": 25000})),
    ("upstream", ("upstream firmware: every path below /api/ answers 404, the other two work", {"api": False})),
))

# Each one breaks one rule of API.md. test_api_contract.py has to fail for each of them.
FAULTS = collections.OrderedDict((
    ("no_busy", "a request during a scan is accepted"),
    ("clear_ignores_seq", "a clear with the number of another read is accepted"),
    ("clear_without_read", "a clear is accepted although the last request is not a finished read"),
    ("get_triggers", "GET /api/dtc?action=read starts a scan"),
    ("header_not_required", "POST /api/dtc works without the header X-WiCAN-DTC"),
    ("host_not_checked", "POST /api/dtc works with every Host header"),
    ("no_expiry", "a request that waited more than 20 s runs"),
    ("result_lost_after_error", "an error drops the stored result"),
    ("seq_not_31_bit", "sequence numbers go up to 2^32-1"),
    ("wrong_field_order", "pass and rx_age_ms of /api/state change places"),
))


def json_text(text):
    """Content of a JSON string as the firmware writes it: quote and backslash escaped, control
    characters dropped, everything else (UTF-8) unchanged."""
    return "".join("\\" + char if char in '"\\' else char for char in text if ord(char) >= 0x20)


def elapsed(now_ms, then_ms):
    # A time older than a stored one counts as no time passed
    return max(0, now_ms - then_ms)


class ScanRules:
    """The rules of main/dtc_state.h: which request is accepted, what a scan in progress allows,
    what is kept. Times are milliseconds since boot."""

    def __init__(self, seed=1, faults=()):
        self.faults = frozenset(faults)
        self.init(seed)

    def init(self, seed):
        self._seq_end = 0xFFFFFFFF if "seq_not_31_bit" in self.faults else SEQ_MAX
        # 0 means "none", it is never given to a request
        self._upcoming = (seed & self._seq_end) or 1
        self.state = "idle"
        self.action = ""
        self.src = ""
        self.seq = 0
        self.step = 0
        self.total = 0
        self.name = ""
        self.reason = ""
        self.count = 0
        self.result_seq = 0
        self._accepted_ms = 0
        self._ended_ms = 0

    def busy(self):
        return self.state in ("queued", "running")

    def begin(self, clear, http, seq, now_ms):
        """A request. Returns (None, its number) if it is accepted, else (reason, the number the
        state is at); nothing changes then."""
        if self.busy() and "no_busy" not in self.faults:
            return "busy", self.seq
        if clear and http:
            # Only a clear over HTTP is bound to a read, MQTT works as before
            reason = self._clear_refused(seq, now_ms)
            if reason is not None:
                return reason, self.seq

        self.seq = self._upcoming
        self._upcoming = 1 if self._upcoming >= self._seq_end else self._upcoming + 1
        self.state = "queued"
        self.action = "clear" if clear else "read"
        self.src = "http" if http else "mqtt"
        self.step = 0
        self.total = 0
        self.name = ""
        self.reason = ""
        self._accepted_ms = now_ms
        return None, self.seq

    def _clear_refused(self, seq, now_ms):
        """The first reason that applies, in the order of the header."""
        read_finished = self.state == "done" and self.action == "read"
        if not read_finished and "clear_without_read" in self.faults:
            return None
        if not read_finished or elapsed(now_ms, self._ended_ms) > CLEAR_MAX_AGE_MS:
            return "read_required"
        if seq != self.seq and "clear_ignores_seq" not in self.faults:
            return "stale_seq"
        if self.count == 0:
            return "nothing_to_clear"
        return None

    def pickup(self, now_ms):
        """The AutoPID task takes the queued request. True if the scan has to run now."""
        if self.state != "queued":
            return False
        # The HTTP client has given up long ago, nothing may run behind its back
        late = self.src == "http" and elapsed(now_ms, self._accepted_ms) > HTTP_EXPIRY_MS
        if late and "no_expiry" not in self.faults:
            self._end("error", "expired", now_ms)
            return False
        self.state = "running"
        return True

    def progress(self, step, total, name):
        if self.state == "running":
            self.step = step
            self.total = total
            self.name = name or ""

    def error(self, reason, now_ms):
        if self.state == "running":
            self._end("error", reason or "", now_ms)

    def done(self, count, now_ms):
        if self.state == "running":
            self._end("done", "", now_ms)
            self.result_seq = self.seq
            self.count = count

    def _end(self, state, reason, now_ms):
        self.state = state
        self.reason = reason
        self.name = ""
        self._ended_ms = now_ms
        if state == "error" and "result_lost_after_error" in self.faults:
            self.result_seq = 0
            self.count = 0

    def json(self, supported, now_ms):
        """The object `dtc` of /api/state."""
        ended = self.state in ("done", "error")
        return ('{"supported":%s,"state":"%s","action":"%s","src":"%s","seq":%d,"ecu":%d,"total":%d,'
                '"name":"%s","reason":"%s","age_s":%d,"count":%d,"result_seq":%d}'
                % ("true" if supported else "false", self.state, self.action, self.src, self.seq,
                   self.step, self.total, json_text(self.name), json_text(self.reason),
                   elapsed(now_ms, self._ended_ms) // 1000 if ended else 0, self.count, self.result_seq))


def state_json(values, dtc, faults=()):
    """The answer to GET /api/state. values: id, fw, git, boot, up, autopid, pids, ecu_online,
    pass, rx_age_ms, mqtt, batt_mv, sleep_in_s, heap, heap_min. dtc: the text of ScanRules.json()."""
    def number(value):
        return "%d" % min(value, NUMBER_MAX)

    def number_or_none(value):
        return "-1" if value < 0 else number(value)

    def text(value):
        return '"%s"' % json_text(value)

    def volts(millivolts):
        # Always one decimal, rounded to the nearest tenth
        return "-1" if millivolts < 0 else "%d.%d" % divmod((millivolts + 50) // 100, 10)

    fields = [
        ("api", "1"),
        ("id", text(values["id"])),
        ("fw", text(values["fw"])),
        ("git", text(values["git"])),
        ("boot", number(values["boot"])),
        ("up", number(values["up"])),
        ("autopid", text(values["autopid"])),
        ("pids", number(values["pids"])),
        ("ecu", '"online"' if values["ecu_online"] else '"offline"'),
        ("pass", number(values["pass"])),
        ("rx_age_ms", number_or_none(values["rx_age_ms"])),
        ("mqtt", text(values["mqtt"])),
        ("batt_v", volts(values["batt_mv"])),
        ("sleep_in_s", number_or_none(values["sleep_in_s"])),
        ("heap", number(values["heap"])),
        ("heap_min", number(values["heap_min"])),
        ("dtc", dtc),
    ]
    if "wrong_field_order" in faults:
        fields[9], fields[10] = fields[10], fields[9]
    return "{%s}" % ",".join('"%s":%s' % field for field in fields)


def body_json(reason, seq):
    """Body of an answer to POST /api/dtc, reason None: accepted."""
    if reason is None:
        return '{"accepted":true,"seq":%d}' % seq
    return '{"accepted":false,"reason":"%s","seq":%d}' % (json_text(reason), seq)


def host_allowed(host):
    """Host header of a request to the adapter itself: an IPv4 address or wican_<id>.local, with or
    without port (main/dtc_api.h). The name of a foreign web page is refused."""
    if host is None:
        return False
    match = re.fullmatch(r"(?:([0-9]{1,3})\.([0-9]{1,3})\.([0-9]{1,3})\.([0-9]{1,3})|wican_[0-9a-f]{1,32}\.local)"
                         r"(?::[0-9]{1,5})?", host, re.IGNORECASE | re.ASCII)
    if match is None:
        return False
    return all(part is None or int(part) <= 255 for part in match.groups())


def parameter(query, name):
    """Value of the first parameter `name` in "a=1&b=2", None if there is none. Nothing is
    percent-decoded, a parameter without '=' has an empty value."""
    for part in query.split("&"):
        key, _, value = part.partition("=")
        if key == name:
            return value
    return None


def parse_seq(text):
    """The number of a read as API.md wants it: decimal digits only, 1..2147483647. None if not.
    At most 10 digits, as in main/dtc_api.c."""
    if text is None or re.fullmatch(r"[0-9]{1,10}", text) is None:
        return None
    value = int(text)
    return value if 1 <= value <= SEQ_MAX else None


def number_text(value):
    """A value of /autopid_data: two decimals at most, no trailing zeros (formatNumberPrecision())."""
    text = "%.2f" % value
    return text.rstrip("0").rstrip(".") if "." in text else text


def fault_memory(preset):
    """What the 18 control units have stored. Each code is (code, status byte, comes back after a
    clear). `confirms`: the control unit confirms a clear."""
    units = [{"status": "ok", "codes": [], "confirms": True} for _ in ECUS]
    if preset == "codes":
        units[1]["codes"] = [("P0100-13", "2F", False), ("P242F-FA", "68", False)]
        units[4]["codes"] = [("U0100-87", "28", False)]
        # The cause is still there: the code is back at once
        units[5]["codes"] = [("9301", "60", True)]
        # Not fitted in this vehicle
        units[11]["status"] = "no_response"
        units[16]["codes"] = [("9100", "E0", False)]
        units[16]["confirms"] = False
    elif preset == "many":
        units[1]["codes"] = [("P%04X-00" % (0x2000 + number), "28", False) for number in range(80)]
        units[4]["codes"] = [("C%04X-00" % (0x1000 + number), "28", False) for number in range(75)]
        units[5]["codes"] = [("9301", "60", False), ("9302", "60", False)]
        units[6]["codes"] = [("P019%X-11" % number, "28", False) for number in range(8)]
    elif preset != "empty":
        raise ValueError("unknown fault memory: %s" % preset)
    return units


def dtc_entry(protocol, code):
    entry = {"code": code[0], "status": code[1]}
    if protocol == "UDS":
        # Bit 0 of the UDS status byte: the test failed at the time of the request
        entry["active"] = bool(int(code[1], 16) & 0x01)
    return entry


def result_json(clear, duration_ms, count, ecus):
    """The final result, the text of the retained MQTT message. If it is too large for an MQTT
    message the code lists of the control units with the most entries are replaced by their
    number until it fits (dtc_publish_result() in main/autopid.c)."""
    result = collections.OrderedDict((
        ("state", "done"),
        ("action", "clear" if clear else "read"),
        ("duration_ms", duration_ms),
        ("dtc_count", count),
        ("ecus", ecus),
    ))
    while True:
        text = json.dumps(result, separators=(",", ":"), ensure_ascii=False)
        largest = max(ecus, key=lambda ecu: len(ecu["dtcs"]))
        if len(text.encode("utf-8")) <= RESULT_MAX_BYTES or not largest["dtcs"]:
            return text
        largest["dtcs_omitted"] = len(largest["dtcs"])
        largest["dtcs"] = []


class SimulatedClock:
    """Time only passes when a test says so."""

    def __init__(self):
        self.ms = 0

    def now_ms(self):
        return self.ms

    def advance(self, seconds):
        self.ms += round(seconds * 1000)


class RealClock:
    """Real time, `speed` times faster."""

    def __init__(self, speed=1.0):
        self.speed = speed
        self.start = time.monotonic()

    def now_ms(self):
        return int((time.monotonic() - self.start) * 1000 * self.speed)


class Adapter:
    """The adapter and the vehicle behind it. Nothing runs in the background: every request and
    every call first works off what has happened on the clock since the last one."""

    def __init__(self, scenario="codes", faults=(), clock=None, **settings):
        if scenario not in SCENARIOS:
            raise ValueError("unknown scenario: %s" % scenario)
        unknown = sorted(set(faults) - set(FAULTS))
        if unknown:
            raise ValueError("unknown fault: %s" % ", ".join(unknown))

        self.faults = frozenset(faults)
        self.clock = clock if clock is not None else SimulatedClock()
        # Method and target of the last requests, for tests that have to know what was sent
        self.requests = collections.deque(maxlen=10000)
        self._lock = threading.RLock()
        self._random = random.Random()
        self._boot = 0

        self._apply(SETTINGS)
        self._apply(SCENARIOS[scenario][1])
        self._apply(settings)
        self._power_on(self.clock.now_ms(), self.boot, self.seq_seed)

    # Situation ------------------------------------------------------------------------------

    def _apply(self, settings):
        for name, value in settings.items():
            if name not in SETTINGS:
                raise TypeError("unknown setting: %s" % name)
            setattr(self, name, value)
            if name == "memory":
                self._memory = fault_memory(value)

    def set(self, **settings):
        """Changes the situation from now on, e.g. set(ignition=False). Not for the settings of
        AT_POWER_ON: they would change nothing before the next restart, so they are refused."""
        fixed = sorted(set(settings) & set(AT_POWER_ON))
        if fixed:
            raise ValueError("only Adapter() takes: %s" % ", ".join(fixed))
        with self._lock:
            now = self._catch_up()
            before = (self.ignition, self.autopid)
            polled = self._polled(now)
            self._apply(settings)
            if (self.ignition, self.autopid) == before:
                # The polling is not disturbed, the pass it is in goes on
                return
            self._passes, self._last_answer, self._valid = polled
            if before[0] and not self.ignition:
                self._valid = False
                self._off_since = now
            self._poll(now)

    def restart(self):
        """The adapter restarts: new boot number, sequence numbers anew, the result is gone. The
        vehicle keeps its fault memory."""
        with self._lock:
            self._power_on(self._catch_up())

    def mqtt_command(self, clear=False):
        """read_dtc or clear_dtc over MQTT. Returns (None, number) or (reason, number)."""
        with self._lock:
            return self._mqtt(clear, self._catch_up())

    def _mqtt(self, clear, now):
        # autopid_request_dtc() in main/autopid.c: without the AutoPID task and once the adapter is due
        # to sleep nothing is started. A task that has not reached its loop yet takes the command.
        # "off" stands for two cases here. The firmware answers not_ready only if AutoPID is the
        # protocol and the task is missing; if AutoPID is not the protocol it publishes nothing.
        if self.autopid == "off" or self._sleep_in_s(now) == 0:
            return "not_ready", 0
        return self._request(clear, False, 0, now)

    def _power_on(self, now, boot=None, seq_seed=None):
        number = boot
        while number is None or (boot is None and number == self._boot):
            number = self._random.randrange(1, SEQ_MAX + 1)
        self._boot = number
        self._boot_at = now
        self._rules = ScanRules(self._random.getrandbits(32) if seq_seed is None else seq_seed, self.faults)
        self._result = None
        self._scan = None
        self._events = []
        self._passes = 0
        self._last_answer = None
        self._valid = False
        self._poll(now)
        self._off_since = now
        if self.mqtt_every_s is not None:
            self._at(now + MQTT_FIRST_MS, self._mqtt_again)
        if self.restart_every_s is not None:
            self._at(now + self.restart_every_s * 1000, self._power_on)

    def _mqtt_again(self, now):
        self._mqtt(False, now)
        self._at(now + self.mqtt_every_s * 1000, self._mqtt_again)

    # Time -----------------------------------------------------------------------------------

    def _at(self, time_ms, action, scan=False):
        self._events.append((time_ms, action, scan))

    def _later(self, now, duration_ms, action):
        """The next step of the scan."""
        self._at(now + duration_ms, action, scan=True)

    def _catch_up(self):
        """Works off everything that is due and returns the time."""
        now = self.clock.now_ms()
        while self._events:
            # Of several at the same time the one that was added first
            event = min(self._events, key=lambda entry: entry[0])
            if event[0] > now:
                break
            self._events.remove(event)
            event[1](event[0])
        return now

    # Polling --------------------------------------------------------------------------------

    def _polled(self, now):
        """Passes with an answer, time of the last answer (None: none since boot), values valid."""
        passes, last_answer, valid = self._passes, self._last_answer, self._valid
        if self._poll_from is not None and self.ignition:
            spent = now - self._poll_from
            passes += spent // (REQUEST_MS * len(PARAMETERS))
            if spent >= REQUEST_MS:
                last_answer = self._poll_from + spent // REQUEST_MS * REQUEST_MS
            valid = valid or spent >= REQUEST_MS * len(PARAMETERS)
        return passes, last_answer, valid

    def _fold(self, now):
        """Keeps what was polled until now. The caller says how it goes on from here."""
        self._passes, self._last_answer, self._valid = self._polled(now)

    def _poll(self, now):
        """The polling goes on from now, unless the AutoPID task is not in its loop or a scan runs."""
        self._poll_from = now if self.autopid == "run" and self._scan is None else None

    def _engine_runs(self):
        """From the engine speed on at which the firmware refuses a clear."""
        return self.rpm >= CLEAR_MAX_RPM

    def _since_boot(self, now):
        """The time the rules of the scan are given, as in the firmware."""
        return now - self._boot_at

    # Scan -----------------------------------------------------------------------------------

    def _request(self, clear, http, seq, now):
        reason, number = self._rules.begin(clear, http, seq, self._since_boot(now))
        if reason is None:
            # Only an adapter with the fault no_busy gets here during a scan: the new request wins
            self._events = [event for event in self._events if not event[2]]
            self._later(now, self.pickup_ms, self._pickup)
        return reason, number

    def _pickup(self, now):
        if not self._rules.pickup(self._since_boot(now)):
            return
        if not self.supported:
            self._rules.error("not_supported", self._since_boot(now))
            return

        # The scan pauses the polling
        self._fold(now)
        self._poll_from = None
        self._scan = {"clear": self._rules.action == "clear", "start": now, "ecus": [], "count": 0}
        self._later(now, PREPARE_MS, self._prepared)

    def _prepared(self, now):
        # Only now the firmware reports step 0 and the number of control units
        self._rules.progress(0, len(ECUS), None)
        self._later(now, ENGINE_CHECK_MS if self.ignition else NO_ANSWER_MS, self._engine_checked)

    def _engine_checked(self, now):
        if not self.ignition:
            self._fail("ecu_offline", now)
        elif self._scan["clear"] and self._engine_runs():
            self._fail("engine_running", now)
        else:
            self._unit(0, now)

    def _unit(self, index, now):
        name, request_id, protocol = ECUS[index]
        unit = self._memory[index]
        entry = collections.OrderedDict((("name", name), ("id", request_id), ("protocol", protocol)))
        status = unit["status"] if self.ignition else "no_response"
        codes = []
        spent = NO_ANSWER_MS if status == "no_response" else UNIT_MS

        self._rules.progress(index + 1, len(ECUS), name)
        if status == "ok":
            # Only complete lists with entries are cleared, and read again afterwards
            if self._scan["clear"] and unit["codes"]:
                entry["cleared"] = unit["confirms"]
                if unit["confirms"]:
                    unit["codes"] = [code for code in unit["codes"] if code[2]]
                spent += CLEAR_MS
            codes = unit["codes"]
            self._scan["count"] += len(codes)
        entry["status"] = status
        entry["dtcs"] = [dtc_entry(protocol, code) for code in codes]
        self._scan["ecus"].append(entry)

        if index + 1 < len(ECUS):
            self._later(now, spent, lambda at: self._unit(index + 1, at))
        else:
            self._later(now, spent, self._finish)

    def _finish(self, now):
        scan, self._scan = self._scan, None
        self._result = result_json(scan["clear"], now - scan["start"], scan["count"], scan["ecus"])
        self._rules.done(min(scan["count"], 0xFFFF), self._since_boot(now))
        self._poll(now)

    def _fail(self, reason, now):
        self._scan = None
        self._rules.error(reason, self._since_boot(now))
        self._poll(now)

    # Answers --------------------------------------------------------------------------------

    def handle(self, method, target, headers):
        """One request. headers: names in lower case. Returns (status, [(header, value)], body)."""
        with self._lock:
            now = self._catch_up()
            self.requests.append((method, target))
            path, _, query = target.partition("?")

            if path.startswith("/api/") and not self.api:
                return self._server_answer(NOT_FOUND)
            if method == "GET" and path == "/api/state":
                return self._state(now)
            if path == "/api/dtc" and (method == "POST" or (method == "GET" and "get_triggers" in self.faults)):
                return self._dtc(headers, query, now)
            if method == "GET" and path == "/api/dtc/result":
                return self._stored_result()
            if method == "GET" and path == "/autopid_data":
                return 200, [("Content-Type", "application/json")], self._values(now).encode("utf-8")
            if method == "GET" and path == "/load_car_config":
                return self._car_config()
            # A path with a handler for another method. HEAD is a method like any other.
            return self._server_answer(NOT_ALLOWED if path in KNOWN_PATHS else NOT_FOUND)

    @staticmethod
    def _server_answer(answer):
        """An answer of the HTTP server itself, not of a handler."""
        status, text = answer
        return status, [("Content-Type", "text/html")], text

    @staticmethod
    def _json(status, text, more=()):
        headers = [("Content-Type", "application/json"), ("Cache-Control", "no-store")]
        return status, headers + list(more), text.encode("utf-8")

    def _state(self, now):
        passes, last_answer, _ = self._polled(now)
        values = {
            "id": self.id,
            "fw": self.fw,
            "git": self.git,
            "boot": self._boot,
            "up": self._since_boot(now) // 1000,
            "autopid": self.autopid,
            "pids": len(PARAMETERS) if self.autopid != "off" else 0,
            "ecu_online": self.ignition and self.autopid == "run",
            "pass": passes,
            # autopid_rx_age_ms() in main/autopid.c keeps the time in 32 bits: 2^32 ms after the
            # last answer the age starts at 0 again
            "rx_age_ms": -1 if last_answer is None else (now - last_answer) % 2 ** 32,
            "mqtt": self.mqtt,
            "batt_mv": self._battery_mv(),
            "sleep_in_s": self._sleep_in_s(now),
            "heap": 61000,
            "heap_min": 48000,
        }
        dtc = self._rules.json(self.supported and self.autopid != "off", self._since_boot(now))
        return self._json(200, state_json(values, dtc, self.faults))

    def _battery_mv(self):
        if self.batt_mv is not None:
            return self.batt_mv
        if not self.ignition:
            return 12600
        return 14100 if self._engine_runs() else 12400

    def _sleep_in_s(self, now):
        if self.sleep_after_s is None or self.ignition:
            return -1
        # adc_task() in main/sleep_mode.c cuts the rest down to whole seconds, and time has passed
        # when it looks: 0 stands for the last second before sleep is due as well
        return max(0, (self.sleep_after_s * 1000 - (now - self._off_since) - 1) // 1000)

    def _dtc(self, headers, query, now):
        # API.md: forbidden, bad_request, not_ready, then the rules of the scan state
        if "header_not_required" not in self.faults and headers.get("x-wican-dtc") != "1":
            return self._json(403, body_json("forbidden", 0))
        if "host_not_checked" not in self.faults and not host_allowed(headers.get("host")):
            return self._json(403, body_json("forbidden", 0))

        # One character is one byte here: http.server reads the request line as ISO-8859-1
        if len(query) > QUERY_MAX_BYTES:
            query = ""
        action = parameter(query, "action")
        seq = parse_seq(parameter(query, "seq"))
        if action not in ("read", "clear") or (action == "clear" and seq is None):
            return self._json(400, body_json("bad_request", 0))

        # dtc_api_ready() in main/dtc_api.c: the task is in its loop and the adapter is not due to sleep
        if self.autopid != "run" or self._sleep_in_s(now) == 0:
            return self._json(503, body_json("not_ready", 0))

        reason, number = self._request(action == "clear", True, seq or 0, now)
        return self._json(202 if reason is None else 409, body_json(reason, number))

    def _stored_result(self):
        if self.autopid != "run":
            return self._json(503, body_json("not_ready", 0))
        # No number, no result: the text and its number are stored and dropped together
        if self._rules.result_seq == 0:
            # The firmware sets no type for the empty answer, its HTTP server then names its default
            return 204, [("Content-Type", "text/html"), ("Cache-Control", "no-store")], b""
        return self._json(200, self._result, [("X-DTC-Seq", "%d" % self._rules.result_seq)])

    def _values(self, now):
        passes, _, valid = self._polled(now)
        if self.autopid == "off":
            # autopid_data_handler() in main/config_server.c, with status 200
            return NO_DATA
        if not valid:
            return "{}"
        running = self._engine_runs()
        values = []
        for name, _, _, engine_off, idling in PARAMETERS:
            value = idling if running else engine_off
            if name == "ENGINE_RPM":
                value = self.rpm
            if running and name in MOVING:
                value += MOVING[name] * (passes % 5 - 2)
            values.append('"%s":%s' % (name, number_text(value)))
        return "{%s}" % ",".join(values)

    def _car_config(self):
        if self.autopid == "off":
            # load_car_config_handler() in main/config_server.c. The connection is kept after it.
            return self._server_answer(NO_CONFIG)
        config = collections.OrderedDict((name, collections.OrderedDict((("class", kind), ("unit", unit))))
                                         for name, kind, unit, _, _ in PARAMETERS)
        text = json.dumps(config, separators=(",", ":"), ensure_ascii=False)
        return 200, [("Content-Type", "application/json")], text.encode("utf-8")


REASONS = {200: "OK", 202: "Accepted", 204: "No Content", 400: "Bad Request", 403: "Forbidden",
           404: "Not Found", 405: "Method Not Allowed", 409: "Conflict", 500: "Internal Server Error",
           503: "Service Unavailable"}


class Handler(http.server.BaseHTTPRequestHandler):
    # One connection serves many requests, as API.md asks of a client
    protocol_version = "HTTP/1.1"

    def _answer(self):
        # The API has no request bodies. One that is sent is dropped, it must not be read as the next request.
        # Not isdigit(): it is true for "\u00b2", which int() does not take.
        length = self.headers.get("Content-Length", "")
        if re.fullmatch(r"[0-9]{1,9}", length) is not None:
            self.rfile.read(int(length))

        headers = {}
        for name, value in self.headers.items():
            headers.setdefault(name.lower(), value)
        status, answer_headers, body = self.server.adapter.handle(self.command, self.path, headers)

        # Written by hand: the firmware sends neither Server nor Date. Head and body go out in one
        # write; two small writes on a connection that is kept run into the Nagle algorithm and a delayed
        # ACK of the client, which costs 40 ms for every answer on Linux.
        head = ["%s %d %s" % (self.protocol_version, status, REASONS[status])]
        head += ["%s: %s" % header for header in answer_headers]
        head += ["Content-Length: %d" % len(body), "", ""]
        # The body also for HEAD: the firmware has no handler for it and answers it like any other method
        self.wfile.write("\r\n".join(head).encode("iso-8859-1") + body)
        if status in ENDS_CONNECTION:
            self.close_connection = True
        if self.server.log is not None:
            self.server.log("%s %s -> %d %s" % (self.command, self.path, status,
                                                body.decode("utf-8") if len(body) < 200 else "(%d bytes)" % len(body)))

    do_GET = do_POST = do_PUT = do_DELETE = do_PATCH = do_OPTIONS = do_HEAD = _answer

    def log_message(self, format, *args):
        pass


class HttpServer(http.server.ThreadingHTTPServer):
    daemon_threads = True

    def handle_error(self, request, client_address):
        # A client that drops its connection is not worth a traceback
        if not isinstance(sys.exc_info()[1], ConnectionError):
            super().handle_error(request, client_address)


class Server:
    """The mock listening on a port. `adapter` may be replaced at any time."""

    def __init__(self, adapter=None, bind="127.0.0.1", port=0, log=None):
        self._httpd = HttpServer((bind, port), Handler)
        self._httpd.adapter = adapter if adapter is not None else Adapter()
        self._httpd.log = log
        # What the socket is bound to, not what was asked for
        self.host, self.port = self._httpd.server_address[:2]
        self._thread = None

    @property
    def adapter(self):
        return self._httpd.adapter

    @adapter.setter
    def adapter(self, adapter):
        self._httpd.adapter = adapter

    def start(self):
        """Serves in a background thread until close()."""
        # A short poll interval: close() waits for it, and a test opens and closes hundreds of servers
        self._thread = threading.Thread(target=self._httpd.serve_forever, kwargs={"poll_interval": 0.002},
                                        name="mock WiCAN on port %d" % self.port, daemon=True)
        self._thread.start()
        return self

    def serve_forever(self):
        self._httpd.serve_forever()

    def close(self):
        if self._thread is not None:
            self._httpd.shutdown()
            self._thread.join()
        self._httpd.server_close()


def parse_arguments(argv=None):
    """The command line. Ends the program with a message if it asks for something there is not."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", type=int, default=8906, help="default 8906, 0 takes a free one")
    parser.add_argument("--bind", default="127.0.0.1", help="address to listen on (default 127.0.0.1)")
    parser.add_argument("--scenario", default="codes", choices=list(SCENARIOS), help="see --list")
    parser.add_argument("--fault", action="append", default=[], choices=list(FAULTS),
                        help="break one rule of the contract, may be given several times")
    parser.add_argument("--speed", type=float, default=1.0, help="the clock of the adapter runs this much faster")
    parser.add_argument("--quiet", action="store_true", help="do not print the requests")
    parser.add_argument("--list", action="store_true", help="list scenarios and faults")
    arguments = parser.parse_args(argv)
    # Not "speed <= 0": nan passes that, and every request would end in an exception
    if not 0 < arguments.speed < float("inf"):
        parser.error("--speed has to be a number above 0")
    if not 0 <= arguments.port <= 65535:
        parser.error("--port has to be 0 to 65535")
    return arguments


def main(argv=None):
    arguments = parse_arguments(argv)

    if arguments.list:
        print("scenarios:")
        for name, (text, _) in SCENARIOS.items():
            print("  %-24s %s" % (name, text))
        print("faults:")
        for name, text in FAULTS.items():
            print("  %-24s %s" % (name, text))
        return 0

    def log(line):
        print(line, flush=True)

    adapter = Adapter(arguments.scenario, arguments.fault, clock=RealClock(arguments.speed))
    server = Server(adapter, arguments.bind, arguments.port, None if arguments.quiet else log)
    print("mock WiCAN on http://%s:%d (scenario %s, faults %s, speed %g)"
          % (server.host, server.port, arguments.scenario, ", ".join(arguments.fault) or "none",
             arguments.speed), flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
