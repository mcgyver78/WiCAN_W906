#!/usr/bin/env python3
"""Stand-in for the WiCAN display on a PC: the web interface of display/API.md without the board.

The page of the display (display/main/web/index.html) can be tried against it in a browser, and
test_page.py holds it against the examples of display/API.md.

  python3 mock_display.py                    http://127.0.0.1:8907
  python3 mock_display.py --port 8907
  python3 mock_display.py --upload-rate 64   a firmware arrives with 64 KiB a second (0: at once)

  http://127.0.0.1:8907/        the page, read from its file with every request
  http://127.0.0.1:8907/mock    the same page under a strip with what one does at the device:
                                release on and off, press the knob, press it long (refuse), and the
                                situations around it. The page itself knows nothing of /mock.
                                The strip has no button for a touch on the screen: no question of
                                the display is answered by one (nav.h).

The mock listens on 127.0.0.1 and nowhere else: it has no login, like the display, and it is no
display. Open it by that address, not as "localhost": the display answers only to an IPv4 address
or to wican-display.local in the Host header, and so does the mock.

In a test:

  display = mock_display.Display(clock=mock_display.SimulatedClock())
  status, kind, body = display.handle("GET", "/api/info", {"host": "192.168.1.77"}, lambda count: b"")
  display.release(True); display.clock.advance(601)      no real waiting
  server = mock_display.Server(display).start(); ...; server.close()

Taken from the sources, read and written a second time here:
  - which request is what and what is refused before a handler runs (components/core/web_route.c),
    in that order; what each request does and answers (app_web.c), with the order locked, busy,
    asking, hot, the rest; the bodies (web_json.c, catalog.c, settings.c), byte for byte
  - the release and the question to the knob (access.c): 10 minutes after the last accepted change,
    30 minutes at most, 60 seconds for the knob, a press in the first 1.5 seconds does not count,
    known are the last ticket and the one before it
  - the check of a layout (layout.c): the same checks in the same order with the same paths and
    texts, so that the first problem named is the one the display would name
  - the check of the first bytes of a firmware (ota_check.c), the settings (settings.c), the WiFi
    requests (web_json.c) and how a network is stored (net_select.c, app.c)
  - the catalogue of the W906 (tools/w906/fixtures/car_config_w906.json), its values with the
    ignition on (autopid_data_ignition_on.json), fault memory lists (dtc_result_*.json) and the
    built-in views (display/layouts/w906_default.json)

What the check of a layout does not check as the display does:
  - a text that is not UTF-8. The display reads bytes and passes them on; the mock answers
    "not valid JSON" (the same holds for the bodies of the WiFi requests and the settings: 400)
Everything else of it is the rule book of layout.c a second time. test_page.py holds it against the
hand-written reports in display/test/fixtures. Against the C code itself the mock was held once, on
2026-10-05, by a program that is not part of the repository: the same random requests (layouts,
settings and networks damaged at random, the release, the knob and the clock in between) to the mock
and to components/core, answers compared byte for byte, no difference. Nothing repeats that when
either side changes: the refusal "hot" and "busy" for storing and forgetting a network while the
display reads or clears came later on both sides and were never compared that way.

Invented, because nothing fixes it. The page must not rely on any of this:
  - every number of /api/info, the networks in range, the network the display is in. Three names
    are chosen to show at once if the page ever puts what it received into its HTML: two networks
    in range and a value the adapter delivers outside its profile
  - the values: those of the fixture, of which a few move with the time, and two that are not in
    the profile (a binary one, and the one with the name above)
  - a restart takes 4 seconds, in which no request is answered (the connection is closed)
  - "busy" is a switch of the strip: it stands for a read or a clear at the display and for the
    clear dialog. The device tells the two apart where a network is stored or forgotten: that is
    refused while the read or the clear itself is under way, not while the dialog only shows. The
    mock refuses it whenever the switch is on
  - "zu heiß" is a switch of the strip as well: the heat keeps the backlight off (heat "off" in
    /api/info, with a temperature of 88 degrees) until "abgekühlt". The device measures its chip
    and switches the light off from 85 degrees on, back on below 80 (guard.h)
  - the display stays in its network when another one is stored or forgotten, and it joins a
    network that was stored if it is in none and that network is in range. The device leaves its
    network with every change of the list and looks anew (link.h)
  - after a factory reset the mock goes on answering at its address; the device is then only
    reachable through its own access point

Not modelled:
  - views made from the catalogue (layout_from_catalog): the built-in views always suit the
    catalogue of the mock, POST /api/layout/reset always answers them
  - the safe mode, the heat that only dims the backlight, the stored layout "one step back"
  - a firmware image is not verified beyond its first 112 bytes; every upload that arrives whole
    counts as good
  - the display ends an upload that brings nothing for 30 seconds from its own clock (app_tick);
    the mock ends it when its socket has been silent for that long
  - every answer ends its connection, the HTTP server of the device keeps connections
  - a Content-Length that is no number is answered 411 here; the HTTP server of ESP-IDF refuses
    such a request by itself. A method the Python server does not know gets its 501
"""
import argparse
import http.server
import json
import math
import os
import re
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
PAGE = os.path.join(HERE, "..", "main", "web", "index.html")
BUILTIN_LAYOUT = os.path.join(HERE, "..", "layouts", "w906_default.json")
ADAPTER_FIXTURES = os.path.join(HERE, "..", "..", "tools", "w906", "fixtures")

# web_route.h
BODY_LAYOUT_MAX = 16384
BODY_SMALL_MAX = 512
# display/partitions.csv: ota_0 and ota_1
SLOT_SIZE = 0x400000
# app_web.h
OUT_SIZE = 20480
UPLOAD_LEFT_S = 300
# access.h
OPEN_MS = 600 * 1000
OPEN_MAX_MS = 1800 * 1000
CONFIRM_MS = 60 * 1000
ASK_SHOWN_MS = 1500
# app.h
UPDATE_CONFIRM_MS = 300 * 1000
UPLOAD_IDLE_S = 30
# Invented: the temperature /api/info names while the heat keeps the backlight off (guard.h switches it
# off from 85 degrees on), and otherwise
HOT_C = 88
COOL_C = 47
# layout.h and json.h
LAYOUT_FORMAT = "wican-display-layout"
LAYOUT_VERSION = 1
PAGES_MAX = 12
ITEMS_MAX = 6
MAP_MAX = 8
TITLE_BYTES = 24
UNIT_BYTES = 8
NAME_BYTES = 32
KEY_BYTES = 32
MAP_RAW_BYTES = 11
MAP_TEXT_BYTES = 23
TOKENS = 4096
DEPTH = 8
NUMBER_BYTES = 47
# net_select.h and net_select.c
PROFILES_MAX = 4
SSID_BYTES = 32
PASSWORD_BYTES = 64
PASSWORD_MIN = 8
HOST_BYTES = 39
FACTORY_PASSWORD = "@meatpi#"
WICAN_AP_PREFIX = "WiCAN_"
# values.h
VALUE_FRESH_MS = 3000
VALUE_KEPT_MS = 10000
# catalog.h
CATALOG_BATTERY = "@BATT_V"
# ota_check.h
OTA_CHECK_BYTES = 112
OTA_PROJECT = "wican-display"
OTA_CHIP_ESP32S3 = 0x0009
OTA_DESCRIPTION_MAGIC = 0xABCD5432

# Invented
RESTART_MS = 4000
HOSTILE_VALUE = "X<img src=x onerror=hacked=1>"
HOSTILE_NETWORKS = ("<img src=x onerror=hacked=1>", "\"><script>hacked=1</script>")

LOCKED_HINT = "Am Display: Menü > Web-Zugriff freigeben"
ASKED_HINT = "Am Display bestätigen: Knopf drücken"

# web_route.c: method, path, route, the change needs the knob, refused while the display is busy
ROUTES = (
    ("GET", "/", "page", False, False),
    ("GET", "/api/info", "info", False, False),
    ("GET", "/api/catalog", "catalog", False, False),
    ("GET", "/api/values", "values", False, False),
    ("GET", "/api/layout", "layout", False, False),
    ("PUT", "/api/layout", "layout_put", False, False),
    ("POST", "/api/layout/reset", "layout_reset", False, False),
    ("GET", "/api/dtc/last", "dtc_last", False, False),
    ("GET", "/api/wifi", "wifi", False, False),
    ("POST", "/api/wifi", "wifi_store", True, False),
    ("POST", "/api/wifi/forget", "wifi_forget", False, False),
    ("POST", "/api/settings", "settings", False, False),
    ("POST", "/api/reboot", "reboot", False, True),
    ("POST", "/api/reset", "reset", True, True),
    ("POST", "/api/ota", "ota", True, True),
    ("GET", "/api/ticket", "ticket", False, False),
)
LAYOUT_MODES = {"mode=check": "layout_check", "mode=apply": "layout_apply", "mode=save": "layout_save"}

REASONS = {200: "OK", 202: "Accepted", 400: "Bad Request", 403: "Forbidden", 404: "Not Found",
           405: "Method Not Allowed", 409: "Conflict", 411: "Length Required", 413: "Content Too Large",
           422: "Unprocessable Content", 500: "Internal Server Error"}
JSON = "application/json"

# An IPv4 address or the name of the display, with or without a port (web_host_allowed()). Letters are
# compared without case, and only the letters of ASCII have a second form.
HOST = re.compile(r"(?:wican-display\.local|(?:(?:25[0-5]|2[0-4][0-9]|1[0-9]{2}|[1-9]?[0-9])\.){3}"
                  r"(?:25[0-5]|2[0-4][0-9]|1[0-9]{2}|[1-9]?[0-9]))(?::[0-9]{1,5})?", re.ASCII | re.IGNORECASE)

MISSING = object()


class Members(list):
    """The members of a JSON object as (name, value) in the order of the text: the display takes the
    first of two members with the same name, a dict would keep the last."""


class Number:
    """A JSON number as it was written: the display asks whether it is whole."""

    def __init__(self, literal, whole):
        self.literal = literal
        self.whole = whole
        self.value = float(literal)


class Refused(Exception):
    """A layout the display does not take: where, and why (layout_report_t)."""

    def __init__(self, path, problem):
        super().__init__(path, problem)
        self.path = path
        self.problem = problem


def read_json(data):
    """The JSON value of bytes as the reader of the display sees it (json.c): objects as Members,
    numbers as Number. ValueError for what it refuses: no JSON, more than TOKENS values, containers
    nested deeper than DEPTH. And for bytes that are no UTF-8, which the display would pass on."""
    def refuse(constant):
        raise ValueError("%s is no JSON" % constant)

    def measure(value, level):
        if isinstance(value, Members):
            inside = [measure(member, level + 1) for _, member in value]
            return 1 + len(value) + sum(count for count, _ in inside), max([level + 1] + [depth for _, depth in inside])
        if isinstance(value, list):
            inside = [measure(element, level + 1) for element in value]
            return 1 + sum(count for count, _ in inside), max([level + 1] + [depth for _, depth in inside])
        return 1, level

    try:
        value = json.loads(data.decode("utf-8"), object_pairs_hook=Members, parse_constant=refuse,
                           parse_int=lambda literal: Number(literal, True),
                           parse_float=lambda literal: Number(literal, False))
        tokens, depth = measure(value, 0)
    except RecursionError:
        raise ValueError("nested too deep") from None
    if tokens > TOKENS or depth > DEPTH:
        raise ValueError("too large for the reader of the display")
    return value


def member(value, name):
    """json_member(): the first member with this name, MISSING if there is none or it is no object."""
    if isinstance(value, Members):
        for key, found in value:
            if key == name:
                return found
    return MISSING


def is_array(value):
    return isinstance(value, list) and not isinstance(value, Members)


def is_integer(value, low, high):
    """json_integer() and the range a caller asks for: a number without fraction and exponent."""
    return isinstance(value, Number) and value.whole and low <= int(value.literal) <= high


def utf8_length(text):
    return len(text.encode("utf-8", "surrogatepass"))


def text_problem(value, limit):
    """text_of() in layout.c: what is wrong with a text of at most `limit` bytes, None if nothing.
    Character by character, so the first problem in the order of the text is the one named."""
    used = 0
    for character in value:
        code = ord(character)
        if 0xD800 <= code <= 0xDFFF:
            return "half a surrogate pair"
        if code < 0x20 or code == 0x7F:
            return "control character"
        used += utf8_length(character)
        if used > limit:
            return "too long"
    return None


def check_layout(data):
    """layout_parse(): the layout as what the reports need - its name, the keys of its pages (one list
    per page), and the warnings as (path, text) - or Refused with the first problem."""
    warnings = []

    def optional_text(members, name, limit, where):
        value = member(members, name)
        if value is MISSING:
            return ""
        if not isinstance(value, str):
            raise Refused(where + name, "not a text")
        problem = text_problem(value, limit)
        if problem is not None:
            raise Refused(where + name, problem)
        return value

    def optional_number(item, name, where):
        value = member(item, name)
        if value is MISSING:
            return None
        if not isinstance(value, Number) or len(value.literal) > NUMBER_BYTES:
            raise Refused(where + name, "not a number")
        if not math.isfinite(value.value):
            raise Refused(where + name, "not finite")
        return value.value

    def map_entries(item, where):
        texts = member(item, "map")
        if texts is MISSING:
            return 0
        if not isinstance(texts, Members):
            raise Refused(where + "map", "not an object")
        if len(texts) > MAP_MAX:
            raise Refused(where + "map", "too many entries")
        for position, (raw, shown) in enumerate(texts):
            entry = "%smap[%d]" % (where, position)
            problem = text_problem(raw, MAP_RAW_BYTES)
            if problem is not None:
                raise Refused(entry, problem)
            if not isinstance(shown, str):
                raise Refused(entry, "not a text")
            problem = text_problem(shown, MAP_TEXT_BYTES)
            if problem is not None:
                raise Refused(entry, problem)
        return len(texts)

    def read_item(item, path):
        where = path + "."
        if not isinstance(item, Members):
            raise Refused(path, "not an object")
        key = member(item, "key")
        if key is MISSING:
            raise Refused(where + "key", "missing")
        if key == "":
            raise Refused(where + "key", "empty")
        optional_text(item, "key", KEY_BYTES, where)
        optional_text(item, "label", TITLE_BYTES, where)
        optional_text(item, "unit", UNIT_BYTES, where)

        if member(item, "dec") is not MISSING and not is_integer(member(item, "dec"), 0, 3):
            raise Refused(where + "dec", "not an integer 0 to 3")

        widget = member(item, "widget")
        if widget is MISSING:
            widget = "number"
        elif not isinstance(widget, str):
            raise Refused(where + "widget", "not a text")
        elif widget not in ("number", "arc", "bar", "state"):
            warnings.append((where + "widget", "unknown widget, shown as number"))
            widget = "number"

        if optional_number(item, "scale", where) == 0:
            raise Refused(where + "scale", "zero")
        low = optional_number(item, "min", where)
        high = optional_number(item, "max", where)
        if low is not None and high is not None and not low < high:
            raise Refused(where + "min", "min is not below max")
        for name in ("warn_lo", "warn_hi", "crit_lo", "crit_hi"):
            optional_number(item, name, where)
        entries = map_entries(item, where)

        if widget in ("arc", "bar") and (low is None or high is None):
            warnings.append((where + "widget", "arc or bar without min and max, shown as number"))
        if widget == "state" and entries == 0:
            warnings.append((where + "widget", "state without map, shown as number"))
        return key

    def read_page(page, path):
        where = path + "."
        if not isinstance(page, Members):
            raise Refused(path, "not an object")
        optional_text(page, "title", TITLE_BYTES, where)
        if member(page, "hidden") is not MISSING and not isinstance(member(page, "hidden"), bool):
            raise Refused(where + "hidden", "not a boolean")
        items = member(page, "items")
        if items is MISSING:
            raise Refused(where + "items", "missing")
        if not is_array(items):
            raise Refused(where + "items", "not an array")
        if len(items) == 0:
            raise Refused(where + "items", "empty")
        if len(items) > ITEMS_MAX:
            raise Refused(where + "items", "too many items")
        return [read_item(item, "%sitems[%d]" % (where, position)) for position, item in enumerate(items)]

    if len(data) > BODY_LAYOUT_MAX:
        raise Refused("", "text too long")
    try:
        root = read_json(data)
    except ValueError:
        raise Refused("", "not valid JSON") from None
    if not isinstance(root, Members):
        raise Refused("", "not a JSON object")

    if member(root, "format") != LAYOUT_FORMAT:
        raise Refused("format", "not " + LAYOUT_FORMAT)
    version = member(root, "v")
    if not is_integer(version, -2 ** 63, 2 ** 63 - 1):
        raise Refused("v", "not an integer")
    if int(version.literal) > LAYOUT_VERSION:
        raise Refused("v", "layout of a newer display")
    if int(version.literal) < 1:
        raise Refused("v", "below 1")
    name = optional_text(root, "name", NAME_BYTES, "")
    optional_text(root, "profile_hint", NAME_BYTES, "")

    pages = member(root, "pages")
    if pages is MISSING:
        raise Refused("pages", "missing")
    if not is_array(pages):
        raise Refused("pages", "not an array")
    if len(pages) == 0:
        raise Refused("pages", "empty")
    if len(pages) > PAGES_MAX:
        raise Refused("pages", "too many pages")
    keys = [read_page(page, "pages[%d]" % position) for position, page in enumerate(pages)]
    return {"name": name, "pages": keys, "warnings": warnings}


def quote(text, delete_too=True):
    """A text as the writers of the display put it into JSON: " and \\ with a backslash, what lies below
    0x20 as \\u00xx, everything else as it is. web_json.c writes 0x7F as \\u007f as well, catalog.c not."""
    written = []
    for character in text:
        code = ord(character)
        if code < 0x20 or (code == 0x7F and delete_too):
            written.append("\\u%04x" % code)
        elif character in "\"\\":
            written.append("\\" + character)
        else:
            written.append(character)
    return "\"" + "".join(written) + "\""


def boolean(value):
    return "true" if value else "false"


def error_body(word):
    """web_error_body()"""
    return "{\"error\":\"%s\"%s}" % (word, ",\"hint\":\"%s\"" % LOCKED_HINT if word == "locked" else "")


def take_text(value, limit):
    """take_text() in web_json.c: a member of a WiFi request, None if it is refused"""
    if not isinstance(value, str) or utf8_length(value) > limit:
        return None
    if any(ord(character) < 0x20 or 0xD800 <= ord(character) <= 0xDFFF for character in value):
        return None
    return value


def read_wifi_request(data, ssid_only):
    """web_wifi_parse() and web_forget_parse(): {"ssid", "password" (None: keep the stored one),
    "host"}, or None if the text is refused. Of two members with the same name the last counts, and
    both have to be valid."""
    request = {"ssid": None, "password": None, "host": ""}
    try:
        root = read_json(data)
    except ValueError:
        return None
    if not isinstance(root, Members):
        return None
    for name, value in root:
        if name == "ssid":
            request["ssid"] = take_text(value, SSID_BYTES)
            if not request["ssid"]:
                return None
        elif ssid_only:
            continue
        elif name == "password":
            request["password"] = take_text(value, PASSWORD_BYTES)
            if request["password"] is None or 0 < utf8_length(request["password"]) < PASSWORD_MIN:
                return None
        elif name == "host":
            request["host"] = take_text(value, HOST_BYTES)
            if request["host"] is None:
                return None
    return request if request["ssid"] is not None else None


def ota_check(first, file_size, slot_size):
    """ota_check(): (the word of the refusal or None, the version of the image)"""
    def text(offset):
        field = first[offset:offset + 32]
        return field[:field.index(0)].decode("utf-8", "replace") if 0 in field else None

    if len(first) < OTA_CHECK_BYTES:
        return "too_short", ""
    if first[0] != 0xE9:
        return "no_image", ""
    if int.from_bytes(first[12:14], "little") != OTA_CHIP_ESP32S3:
        return "wrong_chip", ""
    if int.from_bytes(first[32:36], "little") != OTA_DESCRIPTION_MAGIC:
        return "no_description", ""
    version, project = text(48), text(80)
    if version is None or project is None:
        return "no_description", ""
    if project != OTA_PROJECT:
        return "wrong_project", version
    if file_size == 0 or file_size > slot_size:
        return "too_large", ""
    return None, version


def firmware_image(project=OTA_PROJECT, version="0.2.0", size=600 * 1024, chip=OTA_CHIP_ESP32S3):
    """The beginning of an ESP-IDF application image as ota_check.h describes it, filled up to `size`:
    enough for the display to ask for the knob, nothing a chip could start."""
    image = bytearray(b"\xff" * max(size, OTA_CHECK_BYTES))
    image[0:32] = bytes(32)
    image[0] = 0xE9
    image[12:14] = chip.to_bytes(2, "little")
    image[32:112] = bytes(80)
    image[32:36] = OTA_DESCRIPTION_MAGIC.to_bytes(4, "little")
    image[48:48 + len(version.encode("utf-8"))] = version.encode("utf-8")
    image[80:80 + len(project.encode("utf-8"))] = project.encode("utf-8")
    return bytes(image)


class SimulatedClock:
    """Stands still until a test moves it."""

    def __init__(self):
        self._now_ms = 0

    def now_ms(self):
        return self._now_ms

    def advance(self, seconds):
        self._now_ms += int(round(seconds * 1000))


class RealClock:
    """The time since it was made. advance() skips time: nobody waits 30 minutes for a release to end."""

    def __init__(self):
        self._start = time.monotonic()
        self._skipped_ms = 0

    def now_ms(self):
        return int((time.monotonic() - self._start) * 1000) + self._skipped_ms

    def advance(self, seconds):
        self._skipped_ms += int(round(seconds * 1000))


class Access:
    """access.c: who may change something, and the question to the knob. Times are milliseconds since
    the start of the display. Unlike access.c the functions that only ask note what ran out as well:
    the mock has one clock, which never runs backwards, so nobody can tell the difference."""

    def __init__(self):
        self.open = False
        self.open_until = 0
        self.open_max = 0
        self.clock = 0
        self.asking = None
        self.asking_since = 0
        self.asking_until = 0
        self.ticket = 0
        self.ticket_end = "unknown"
        self.previous_end = "unknown"

    def _end(self, end):
        if self.asking is not None:
            self.asking = None
            self.ticket_end = end

    def _settle(self, now):
        self.clock = max(self.clock, now)
        # A question whose own time was over when the release ended has expired; one that still waited
        # then was refused
        if self.clock >= self.asking_until and self.asking_until <= self.open_until:
            self._end("expired")
        if self.clock >= self.open_until:
            self._end("refused")
            self.open = False

    def _renew(self):
        self.open_until = min(self.clock + OPEN_MS, self.open_max)

    def switch_on(self, now):
        self._settle(now)
        self.open = True
        self.open_max = self.clock + OPEN_MAX_MS
        self._renew()

    def switch_off(self, now):
        self._settle(now)
        self._end("refused")
        self.open = False

    def is_open(self, now):
        self._settle(now)
        return self.open

    def seconds_left(self, now):
        self._settle(now)
        return (self.open_until - self.clock + 999) // 1000 if self.open else 0

    def write(self, now):
        self._settle(now)
        if not self.open:
            return False
        self._renew()
        return True

    def waiting(self, now):
        """What waits for the knob: "wifi", "firmware", "reset" or None"""
        self._settle(now)
        return self.asking

    def ask(self, question, now):
        """The ticket of a question, which waiting() and is_open() allowed at this time"""
        self._settle(now)
        self.previous_end = self.ticket_end
        self.ticket = 1 if self.ticket == 2 ** 32 - 1 else self.ticket + 1
        self.ticket_end = "waiting"
        self.asking = question
        self.asking_since = self.clock
        self.asking_until = self.clock + CONFIRM_MS
        self._renew()
        return self.ticket

    def ask_seconds_left(self, now):
        self._settle(now)
        end = min(self.asking_until, self.open_until)
        return (end - self.clock + 999) // 1000 if self.asking is not None else 0

    def confirm(self, now):
        self._settle(now)
        # A press that comes this soon was meant for the screen the question appeared over
        if self.clock - self.asking_since < ASK_SHOWN_MS:
            return None
        confirmed = self.asking
        self._end("confirmed")
        return confirmed

    def refuse(self, now):
        self._settle(now)
        self._end("refused")

    def state(self, ticket, now):
        self._settle(now)
        if ticket == self.ticket:
            return self.ticket_end
        if ticket == (2 ** 32 - 1 if self.ticket == 1 else self.ticket - 1):
            return self.previous_end
        return "unknown"


def fixture(name):
    with open(os.path.join(ADAPTER_FIXTURES, name), encoding="utf-8") as file:
        return file.read().strip()


class Display:
    """The display as its web interface shows it. Everything a test may want to set is an attribute;
    handle() is one request, the other methods are what happens at the device."""

    def __init__(self, clock=None, page=PAGE, upload_rate=0):
        self.clock = clock if clock is not None else RealClock()
        self.page = page
        self.upload_rate = upload_rate
        self.lock = threading.Lock()
        self.boot_ms = self.clock.now_ms()
        self.down_until_ms = 0          # of the clock: no answer before
        self.away = False               # no answer at all: the phone has left the WiFi

        with open(BUILTIN_LAYOUT, "rb") as file:
            self.builtin = file.read()
        self.stored = None              # the layout the user stored, bytes
        self.layout = self.builtin      # the text of the views in use
        self.layout_name = check_layout(self.builtin)["name"]
        self.source = "builtin"

        # name, unit, class, in the profile, delivered
        self.catalog = [[CATALOG_BATTERY, "V", "", False, True]]
        self.catalog += [[name, entry["unit"], entry["class"], True, True]
                         for name, entry in json.loads(fixture("car_config_w906.json")).items()]
        self.catalog += [["GLOW_ACTIVE", "", "", False, True], [HOSTILE_VALUE, "", "", False, True]]
        self.base_values = json.loads(fixture("autopid_data_ignition_on.json"))
        self.adapter = True
        self.adapter_seen_ms = 0        # the last answer of the adapter, while it is away

        # ssid, password, host
        self.profiles = [["Werkstatt", "geheim123", "192.168.1.50"]]
        self.current = "Werkstatt"
        # ssid, rssi, secure
        self.seen = [["Werkstatt", -52, True], ["WiCAN_a1b2c3d4e5f6", -40, True], ["Freifunk", -88, False],
                     [HOSTILE_NETWORKS[0], -70, False], [HOSTILE_NETWORKS[1], -75, True], ["Café ☕ 2,4 GHz", -81, True]]
        self.settings = {"brightness": 80, "night": 25, "night_mode": False, "reverse": False, "standby_s": 60}

        self.version = "0.1.0"
        self.git = "display-v0.1.0-3-g1a2b3c4"
        self.slot = "ota_0"
        self.reset_reason = "poweron"
        self.rolled_back = False
        self.update_pending = False
        self.previous = None            # version, git and slot the boot loader goes back to
        self.numbers = {"heap": 182340, "heap_min": 151200, "psram": 7340032, "psram_min": 7100416, "temp_c": COOL_C}
        self.ip = "192.168.1.77"
        self.rssi = -61
        self.ap_ssid = "WiCAN-Display"
        self.wican_host = "192.168.1.50"
        self.wican_id = "a1b2c3d4e5f6"
        self.wican_fw = "4.21"
        self.http = {"ok": 12345, "failed": 7, "reconnects": 2}

        self.read = None                # the list the display read itself, as its text
        self.read_ms = 0                # when that read ended
        self.before_clear = None
        self.set_lists("read")

        self.access = Access()
        self.reading = False            # stands for a read, a clear and the clear dialog
        self.hot = False                # the heat keeps the backlight off: nobody can see a question
        self.uploading = False
        self.upload_version = ""
        self.asked_detail = ""          # what the screen shows with the question
        self.asked_network = None

    # ------------------------------------------------------------------------------------------------
    # What happens at the device

    def now(self):
        """Milliseconds since the start of the display"""
        return self.clock.now_ms() - self.boot_ms

    def release(self, on):
        if on:
            self.access.switch_on(self.now())
        else:
            self.access.switch_off(self.now())
            self.asked_network = None

    def press(self):
        """A short press of the knob: the answer to a question that waits. Returns what it confirmed."""
        confirmed = self.access.confirm(self.now())
        if confirmed == "wifi":
            self._store_network(self.asked_network)
        elif confirmed == "firmware":
            self.previous = (self.version, self.git, self.slot)
            self.version = self.upload_version
            self.git = "display-v" + self.upload_version
            self.slot = "ota_1" if self.slot == "ota_0" else "ota_0"
            self.rolled_back = False
            self.restart("sw")
            self.update_pending = True
        elif confirmed == "reset":
            self.profiles = []
            self.current = ""
            self.settings = {"brightness": 80, "night": 25, "night_mode": False, "reverse": False, "standby_s": 60}
            self.wican_id = ""
            self.wican_host = ""
            self.restart("sw")
        if confirmed is not None:
            self.asked_network = None
        return confirmed

    def press_long(self):
        """A long press: no."""
        self.access.refuse(self.now())
        self.asked_network = None

    def update_ok(self):
        """"Update in Ordnung?" was answered at the device, with the knob"""
        self.update_pending = False

    def set_hot(self, on):
        """The heat switches the backlight off, or the board has cooled down (app_temperature()). A
        question nobody can see is none: one that waits is refused. The update question stays."""
        self.hot = on
        self.numbers["temp_c"] = HOT_C if on else COOL_C
        if on:
            self.access.refuse(self.now())
            self.asked_network = None

    def restart(self, reason="sw"):
        """The display starts anew: the release is closed, the tickets count from 1 again, a preview is
        over, and a firmware nobody confirmed is taken back by the boot loader. For RESTART_MS it
        answers nothing."""
        if self.update_pending:
            self.version, self.git, self.slot = self.previous
            self.update_pending = False
            self.rolled_back = True
        self.down_until_ms = self.clock.now_ms() + RESTART_MS
        self.boot_ms = self.down_until_ms
        self.reset_reason = reason
        self.access = Access()
        self.uploading = False
        self.reading = False
        self.asked_network = None
        if self.stored is not None:
            self._use(self.stored, "stored")
        else:
            self._use(self.builtin, "builtin")

    def set_adapter(self, on):
        if self.adapter and not on:
            self.adapter_seen_ms = self.now()
        self.adapter = on

    def set_lists(self, which):
        """The fault memory lists the display holds: "none", "read", "cleared" (the list before the
        clear is all that is left) or "both"."""
        codes = fixture("dtc_result_read_codes.json")
        self.read = {"none": None, "read": codes, "cleared": None, "both": fixture("dtc_result_read_empty.json")}[which]
        self.before_clear = codes if which in ("cleared", "both") else None
        self.read_ms = self.now()

    def busy(self):
        """app_busy()"""
        return self.reading or self.uploading

    def _use(self, data, source):
        """These views are the ones in use from now on. `data` is a text check_layout() takes."""
        self.layout = data
        self.layout_name = check_layout(data)["name"]
        self.source = source

    def _store_network(self, asked):
        """store_wifi() in app.c with net_store(): a known network in its place, a new one first"""
        if asked is None:
            return
        stored = [profile for profile in self.profiles if profile[0] == asked["ssid"]]
        password = asked["password"]
        if password is None:
            password = stored[0][1] if stored else ""
        if stored:
            stored[0][1:] = [password, asked["host"]]
        else:
            self.profiles = ([[asked["ssid"], password, asked["host"]]] + self.profiles)[:PROFILES_MAX]
        if self.current == "" and any(network[0] == asked["ssid"] for network in self.seen):
            self.current = asked["ssid"]

    def _settle(self):
        """What happened by itself since the last request"""
        # Nobody answered "Update in Ordnung?": the display restarts
        if self.update_pending and self.now() >= UPDATE_CONFIRM_MS:
            self.restart("sw")
        if self.access.waiting(self.now()) is None:
            self.asked_network = None

    # ------------------------------------------------------------------------------------------------
    # The bodies

    def info_json(self, now):
        release = self.access.is_open(now)
        return ("{\"project\":\"wican-display\",\"version\":%s,\"git\":%s,\"slot\":%s,\"reset\":%s,\"up\":%d,"
                "\"safe_mode\":false,\"rolled_back\":%s,\"update_pending\":%s,"
                "\"heap\":%d,\"heap_min\":%d,\"psram\":%d,\"psram_min\":%d,\"temp_c\":%d,\"heat\":\"%s\","
                "\"release\":{\"open\":%s,\"left_s\":%d},"
                "\"wifi\":{\"ssid\":%s,\"ip\":%s,\"rssi\":%d,\"ap\":%s,\"ap_ssid\":%s},"
                "\"wican\":{\"host\":%s,\"id\":%s,\"fw\":%s,\"view\":%s},"
                "\"layout\":{\"name\":%s,\"source\":%s},"
                "\"http\":{\"ok\":%d,\"failed\":%d,\"reconnects\":%d},\"settings\":%s}"
                % (quote(self.version), quote(self.git), quote(self.slot), quote(self.reset_reason), now // 1000,
                   boolean(self.rolled_back), boolean(self.update_pending),
                   self.numbers["heap"], self.numbers["heap_min"], self.numbers["psram"], self.numbers["psram_min"],
                   self.numbers["temp_c"], "off" if self.hot else "normal", boolean(release), self.access.seconds_left(now),
                   quote(self.current), quote(self.ip if self.current else ""), self.rssi if self.current else 0,
                   boolean(not self.profiles), quote(self.ap_ssid),
                   quote(self.wican_host), quote(self.wican_id), quote(self.wican_fw), quote(self.view()),
                   quote(self.layout_name), quote(self.source),
                   self.http["ok"], self.http["failed"], self.http["reconnects"], self.settings_json()))

    def settings_json(self):
        return ("{\"brightness\":%d,\"night\":%d,\"night_mode\":%s,\"reverse\":%s,\"standby_s\":%d}"
                % (self.settings["brightness"], self.settings["night"], boolean(self.settings["night_mode"]),
                   boolean(self.settings["reverse"]), self.settings["standby_s"]))

    def view(self):
        return "live" if self.adapter else "no_answer"

    def catalog_json(self):
        return "{%s}" % ",".join("%s:{\"unit\":%s,\"class\":%s,\"profile\":%s,\"delivered\":%s}"
                                 % (quote(name, False), quote(unit, False), quote(kind, False), boolean(profile),
                                    boolean(delivered))
                                 for name, unit, kind, profile, delivered in self.catalog)

    def values(self, now):
        """The values the display holds: (name, a number or "on" or "off", when the adapter renewed it).
        A few of them move with the time, so that the page has something to follow."""
        seen = now if self.adapter else self.adapter_seen_ms
        seconds = seen / 1000
        moving = dict(self.base_values)
        moving["ENGINE_RPM"] = round((790 + 35 * math.sin(seconds / 1.9)) * 4) / 4
        moving["COOLANT_TMP"] = round((21.5 + min(seconds, 600) * 0.115) * 4) / 4
        moving["ACCEL_PEDAL"] = round(max(0.0, 40 * math.sin(seconds / 7)), 2)
        moving["DPF_KM_SINCE_REGEN"] = 312 + int(seconds / 30)
        values = [(CATALOG_BATTERY, round(14.1 + 0.2 * math.sin(seconds / 11), 1), seen)]
        values += [(name, value, seen) for name, value in moving.items()]
        values += [("GLOW_ACTIVE", "on" if int(seconds / 20) % 2 == 0 else "off", seen), (HOSTILE_VALUE, 1, seen)]
        return values

    def values_json(self, now):
        written = []
        for name, value, seen in self.values(now):
            age = max(0, now - seen)
            if age >= VALUE_KEPT_MS:
                continue
            # printf would write minus zero as "-0"
            shown = "\"%s\"" % value if isinstance(value, str) else "%.9g" % (value if value != 0 else 0)
            written.append("%s:{\"v\":%s,\"age\":\"%s\"}" % (quote(name), shown, "fresh" if age < VALUE_FRESH_MS else "old"))
        return "{\"view\":%s,\"values\":{%s}}" % (quote(self.view()), ",".join(written))

    def wifi_json(self):
        profiles = ["{\"ssid\":%s,\"host\":%s,\"password\":%s,\"factory\":%s,\"wican_ap\":%s}"
                    % (quote(ssid), quote(host), boolean(password != ""), boolean(password == FACTORY_PASSWORD),
                       boolean(ssid.startswith(WICAN_AP_PREFIX) and len(ssid) > len(WICAN_AP_PREFIX)))
                    for ssid, password, host in self.profiles]
        # A scan lists hidden networks with an empty SSID
        seen = ["{\"ssid\":%s,\"rssi\":%d,\"secure\":%s}" % (quote(ssid), rssi, boolean(secure))
                for ssid, rssi, secure in self.seen if ssid != ""]
        return "{\"current\":%s,\"profiles\":[%s],\"seen\":[%s]}" % (quote(self.current), ",".join(profiles), ",".join(seen))

    def dtc_last_json(self, now):
        age = min(max(0, now - self.read_ms) // 1000, 2 ** 32 - 1) if self.read else 0
        return "{\"read\":%s,\"read_age_s\":%d,\"before_clear\":%s}" % (self.read or "null", age, self.before_clear or "null")

    def report_json(self, layout):
        """web_layout_report_json() for a layout that is taken"""
        names = [entry[0] for entry in self.catalog]
        keys = [key for page in layout["pages"] for key in page]
        unknown = []
        # Before the catalogue is there every key would be unknown
        if any(name != CATALOG_BATTERY for name in names):
            unknown = [key for key in dict.fromkeys(keys) if key not in names]
        first = layout["warnings"][0] if layout["warnings"] else ("", "")
        return ("{\"ok\":true,\"name\":%s,\"pages\":%d,\"items\":%d,\"warnings\":%d,\"warning_path\":%s,\"warning\":%s,"
                "\"unknown\":[%s]}" % (quote(layout["name"]), len(layout["pages"]), len(keys), len(layout["warnings"]),
                                       quote(first[0]), quote(first[1]), ",".join(quote(key) for key in unknown)))

    # ------------------------------------------------------------------------------------------------
    # The requests

    def route(self, method, path, query, headers, now):
        """web_route(): (the route, the ticket asked for) if the request goes on to its handler, else
        (None, (status, word)). The checks in the order of web_route.h, the first that fails is the answer."""
        entries = [entry for entry in ROUTES if entry[1] == path]
        if not entries:
            return None, (404, "not_found")
        entry = next((entry for entry in entries if entry[0] == method), None)
        if entry is None:
            return None, (405, "method")
        if HOST.fullmatch(headers.get("host", "")) is None:
            return None, (403, "host")
        if method != "GET" and headers.get("x-display") != "1":
            return None, (403, "header")

        route = entry[2]
        changes = method != "GET"
        query_fits = query == ""
        limit = BODY_SMALL_MAX
        ticket = 0
        if route == "layout_put":
            # Only the check changes nothing. A query that names no mode has to pass the release like a
            # change: "locked" goes before "query".
            route = LAYOUT_MODES.get(query)
            changes = route != "layout_check"
            query_fits = route is not None
            limit = BODY_LAYOUT_MAX
        if route == "ticket":
            found = re.fullmatch(r"id=([0-9]{1,10})", query)
            ticket = int(found.group(1)) if found is not None and int(found.group(1)) < 2 ** 32 else 0
            query_fits = ticket != 0
        if route == "ota":
            limit = SLOT_SIZE

        if changes and not self.access.is_open(now):
            return None, (403, "locked")
        if not query_fits:
            return None, (400, "query")
        if method != "GET":
            length = content_length(headers)
            if length is None:
                return None, (411, "length")
            if length > limit or (route == "ota" and length == 0):
                return None, (413, "too_large")
        if entry[4] and self.busy():
            return None, (409, "busy")
        return route, ticket

    def handle(self, method, target, headers, read):
        """One request. headers: the names in lower case. read(count): the next bytes of the body, fewer
        than asked for if the connection broke. Returns (status, content type, body as bytes), or None
        if the display gives no answer."""
        path, mark, query = target.partition("?")
        length = content_length(headers) or 0

        with self.lock:
            self._settle()
            if self.away or self.clock.now_ms() < self.down_until_ms:
                return None
            route, more = self.route(method, path, query, headers, self.now())
            if route == "page":
                with open(self.page, "rb") as file:
                    return 200, "text/html; charset=utf-8", file.read()
            if route is None:
                status, body = more[0], error_body(more[1])
            elif method == "GET":
                status, body = self._get(route, more, self.now())

        # The body is received without the lock; a request whose body did not arrive whole is none
        if route == "ota":
            # The first bytes are judged before anything is erased
            first = read(min(length, 4096))
            if len(first) < min(length, 4096):
                return None
            with self.lock:
                self._settle()
                status, body = self._upload_begin(first, length, self.now())
            if status == 0:
                status, body = self._upload(read, len(first), length)
            else:
                self._drop(read, length - len(first))
        elif route is None:
            # Refused before its body was read: the body is taken off the line, as the server of the
            # device does, or the answer would be lost with the connection
            if method != "GET":
                self._drop(read, length)
        elif method != "GET":
            data = read(length)
            if len(data) < length:
                return None
            with self.lock:
                self._settle()
                status, body = self._change(route, data, self.now())

        data = body.encode("utf-8")
        # An answer that has no room in the buffer of the display
        if len(data) >= OUT_SIZE:
            status, data = 500, error_body("too_large").encode("utf-8")
        return status, JSON, data

    @staticmethod
    def _drop(read, count):
        while count > 0:
            piece = read(min(count, 65536))
            if not piece:
                break
            count -= len(piece)

    def _get(self, route, ticket, now):
        """app_web_get()"""
        if route == "info":
            return 200, self.info_json(now)
        if route == "catalog":
            return 200, self.catalog_json()
        if route == "values":
            return 200, self.values_json(now)
        if route == "layout":
            # Views that have no text are no answer
            if not self.layout:
                return 500, error_body("too_large")
            return 200, self.layout.decode("utf-8")
        if route == "dtc_last":
            return 200, self.dtc_last_json(now)
        if route == "wifi":
            return 200, self.wifi_json()
        state = self.access.state(ticket, now)
        left = self.access.ask_seconds_left(now) if state == "waiting" else 0
        return 200, "{\"ticket\":%d,\"state\":\"%s\",\"left_s\":%d}" % (ticket, state, left)

    def _ask_refused(self, busy, now):
        """ask_refused() in app_web.c: who may not change anything learns nothing else"""
        if not self.access.is_open(now):
            return 403, error_body("locked")
        if busy:
            return 409, error_body("busy")
        if self.access.waiting(now) is not None:
            return 409, error_body("asking")
        # Nobody could see the question on a screen the heat keeps dark
        if self.hot:
            return 409, error_body("hot")
        return None

    def _ask(self, question, detail, now):
        self.asked_detail = detail
        return 202, "{\"ticket\":%d,\"hint\":\"%s\"}" % (self.access.ask(question, now), ASKED_HINT)

    def _change(self, route, data, now):
        """app_web_layout(), app_web_wifi(), app_web_settings(), app_web_action(): every one calls the
        release anew, it may have ended since web_route() looked"""
        if route == "layout_check":
            return self._layout(route, data)
        if route == "wifi_store":
            # Under an upload the screen takes no input: the question would wait unseen. And with the new
            # network the display leaves the one it is in: not while it reads or clears the fault memory
            refused = self._ask_refused(self.uploading or self.reading, now)
            if refused is not None:
                return refused
            request = read_wifi_request(data, False)
            if request is None:
                return 400, error_body("body")
            self.asked_network = request
            return self._ask("wifi", request["ssid"], now)
        if route == "reset":
            refused = self._ask_refused(self.busy(), now)
            if refused is not None:
                return refused
            self.asked_network = None
            return self._ask("reset", "", now)

        if not self.access.write(now):
            return 403, error_body("locked")
        if route in ("layout_apply", "layout_save", "layout_reset"):
            return self._layout(route, data)
        if route == "wifi_forget":
            # The display leaves its network with every network that is forgotten: a read would be left
            # without its list, a clear without its outcome
            if self.reading:
                return 409, error_body("busy")
            request = read_wifi_request(data, True)
            if request is None:
                return 400, error_body("body")
            left = [profile for profile in self.profiles if profile[0] != request["ssid"]]
            if len(left) == len(self.profiles):
                return 404, error_body("not_found")
            self.profiles = left
            if self.current == request["ssid"]:
                self.current = ""
            return 200, "{\"ok\":true}"
        if route == "settings":
            return self._settings(data)
        # The restart: one would leave a read without its list and a clear without its outcome
        if self.busy():
            return 409, error_body("busy")
        self.restart("sw")
        return 200, "{\"ok\":true}"

    def _layout(self, route, data):
        if route == "layout_reset":
            # The views the display chooses by itself were read without anything to put right
            self.stored = None
            self._use(self.builtin, "builtin")
            return 200, self.report_json(dict(check_layout(self.builtin), warnings=[]))
        try:
            layout = check_layout(data)
        except Refused as refused:
            return 400, "{\"ok\":false,\"path\":%s,\"problem\":%s}" % (quote(refused.path), quote(refused.problem))
        if route == "layout_apply":
            self._use(data, "preview")
        if route == "layout_save":
            self.stored = data
            self._use(data, "stored")
        return 200, self.report_json(layout)

    def _settings(self, data):
        """settings_from_json(): nothing changes before the whole text is known to be good"""
        ranges = {"brightness": (5, 100), "night": (5, 100), "standby_s": (0, 3600)}
        changed = dict(self.settings)
        try:
            root = read_json(data)
        except ValueError:
            root = None
        if not isinstance(root, Members):
            return 400, "{\"error\":\"body\",\"member\":\"\"}"
        for name, value in root:
            if name in ranges and is_integer(value, *ranges[name]):
                changed[name] = int(value.literal)
            elif name in ("night_mode", "reverse") and isinstance(value, bool):
                changed[name] = value
            elif name in changed:
                return 400, "{\"error\":\"body\",\"member\":\"%s\"}" % name
        self.settings = changed
        return 200, self.settings_json()

    def _upload_begin(self, first, file_size, now):
        """app_web_upload_begin(): 0 if the upload may go on"""
        if not self.access.write(now) or self.access.seconds_left(now) < UPLOAD_LEFT_S:
            return 403, error_body("locked")
        # While the running firmware is not confirmed the other slot is what the boot loader goes back to
        if self.busy() or self.update_pending:
            return 409, error_body("busy")
        if self.access.waiting(now) is not None:
            return 409, error_body("asking")
        # The question at the end of the upload could not be seen
        if self.hot:
            return 409, error_body("hot")
        word, version = ota_check(first, file_size, SLOT_SIZE)
        if word is not None:
            return 422, error_body(word)
        self.uploading = True
        self.upload_version = version
        return 0, ""

    def _upload(self, read, received, file_size):
        """The rest of a firmware, in pieces and without the lock, then app_web_upload_end()"""
        while received < file_size:
            piece = read(min(4096, file_size - received))
            received += len(piece)
            with self.lock:
                running = self.uploading
            if not piece or not running:
                break
            if self.upload_rate > 0:
                time.sleep(len(piece) / (self.upload_rate * 1024))

        with self.lock:
            running = self.uploading
            self.uploading = False
            if received < file_size or not running:
                return 500, error_body("upload")
            refused = self._ask_refused(False, self.now())
            if refused is not None:
                return refused
            self.asked_network = None
            return self._ask("firmware", self.upload_version, self.now())

    # ------------------------------------------------------------------------------------------------
    # The strip: what one does at the device, for the page /mock. No part of the display.

    def control(self, method, target, headers):
        path, mark, query = target.partition("?")
        if HOST.fullmatch(headers.get("host", "")) is None:
            return 403, JSON, error_body("host").encode("utf-8")
        if method == "GET" and path == "/mock":
            return 200, "text/html; charset=utf-8", STRIP.encode("utf-8")
        if method == "GET" and path == "/mock/firmware":
            images = {"good": firmware_image(), "wican": firmware_image("wican-fw", "4.21"),
                      "chip": firmware_image(chip=0x0005), "text": b"Das ist keine Firmware.\n" * 8}
            if query not in images:
                return 404, JSON, error_body("not_found").encode("utf-8")
            return 200, "application/octet-stream", images[query]
        with self.lock:
            if method == "GET" and path == "/mock/state":
                return 200, JSON, self._state_json().encode("utf-8")
            # A page of another origin must not play with the mock either
            if method != "POST" or headers.get("x-display") != "1":
                return 403, JSON, error_body("header").encode("utf-8")
            actions = {
                "/mock/release": lambda: self.release(query == "on"),
                "/mock/press": self.press,
                "/mock/refuse": self.press_long,
                "/mock/busy": lambda: setattr(self, "reading", query == "on"),
                "/mock/hot": lambda: self.set_hot(query == "on"),
                "/mock/adapter": lambda: self.set_adapter(query == "on"),
                "/mock/away": lambda: setattr(self, "away", query == "on"),
                "/mock/lists": lambda: self.set_lists(query),
                # Digits only: the strip cannot turn the clock back
                "/mock/skip": lambda: self.clock.advance(int(re.fullmatch(r"[0-9]{1,5}", query).group(0))),
                "/mock/restart": self.restart,
                "/mock/update_ok": self.update_ok,
            }
            if path not in actions:
                return 404, JSON, error_body("not_found").encode("utf-8")
            try:
                actions[path]()
            except (KeyError, AttributeError):
                return 400, JSON, error_body("query").encode("utf-8")
            return 200, JSON, self._state_json().encode("utf-8")

    def _state_json(self):
        """What somebody in front of the device would see, for the strip"""
        now = self.now()
        self._settle()
        question = {"wifi": "WLAN speichern?", "firmware": "Firmware installieren?", "reset": "Werkseinstellungen?",
                    None: ""}[self.access.waiting(now)]
        return json.dumps({
            "down": self.clock.now_ms() < self.down_until_ms, "away": self.away,
            "release": self.access.seconds_left(now), "question": question,
            "detail": self.asked_detail if question else "", "left": self.access.ask_seconds_left(now),
            "busy": self.reading, "hot": self.hot, "uploading": self.uploading, "adapter": self.adapter,
            "update_pending": self.update_pending, "version": self.version, "source": self.source,
        }, ensure_ascii=False)


def content_length(headers):
    """The value of Content-Length, None if the header was not sent (or is no number)"""
    value = headers.get("content-length", "")
    # Not isdigit(): it is true for "²", which int() does not take
    return int(value) if re.fullmatch(r"[0-9]{1,10}", value) is not None else None


# The page /mock: the page of the display in a frame, and above it the device. Nothing here is part of
# the display.
STRIP = """<!doctype html>
<html lang="de">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Mock: WiCAN-Display</title>
<style>
html, body { height: 100%; margin: 0; }
body { display: flex; flex-direction: column; font: 14px/1.3 system-ui, sans-serif; }
#strip { background: #222; color: #eee; padding: 6px 8px; }
#strip button { font: inherit; margin: 2px; padding: 5px 8px; }
#strip a { color: #9cf; margin-right: 8px; }
#screen { font-weight: 600; margin: 4px 2px; }
iframe { flex: 1; border: 0; width: 100%; }
</style>
</head>
<body>
<div id="strip">
<div id="screen">…</div>
<div id="buttons"></div>
<div>Test-Firmware:
<a href="/mock/firmware?good" download="wican-display.bin">wican-display 0.2.0</a>
<a href="/mock/firmware?wican" download="wican-fw.bin">falsches Projekt</a>
<a href="/mock/firmware?chip" download="anderer-chip.bin">anderer Chip</a>
<a href="/mock/firmware?text" download="kein-image.bin">keine Firmware</a>
</div>
</div>
<iframe src="/" title="Seite des Displays"></iframe>
<script>
"use strict";
const BUTTONS = [
	["Freigabe ein", "release?on"], ["Freigabe aus", "release?off"],
	["Knopf drücken", "press"], ["Knopf lang (nein)", "refuse"],
	["+1 min", "skip?60"], ["+9 min", "skip?540"],
	["beschäftigt", "busy?on"], ["frei", "busy?off"],
	["zu heiß", "hot?on"], ["abgekühlt", "hot?off"],
	["Adapter weg", "adapter?off"], ["Adapter da", "adapter?on"],
	["Listen: keine", "lists?none"], ["gelesen", "lists?read"], ["gelöscht", "lists?cleared"], ["beide", "lists?both"],
	["Neustart", "restart"], ["Update in Ordnung", "update_ok"],
	["Display weg", "away?on"], ["wieder da", "away?off"]
];

function show(state)
{
	const parts = [];
	parts.push(state.away ? "Display nicht erreichbar" : state.down ? "Display startet neu …" : "Display " + state.version);
	parts.push(state.release > 0 ? "Web-Zugriff frei, noch " + state.release + " s" : "Web-Zugriff gesperrt");
	if(state.question) parts.push("Bildschirm: " + state.question + " " + state.detail + " · Drücken = ja · lang = nein (" + state.left + " s)");
	if(state.update_pending) parts.push("Bildschirm: Update in Ordnung? Knopf drücken");
	if(state.busy) parts.push("liest den Fehlerspeicher");
	if(state.hot) parts.push("zu heiß: Bildschirm aus");
	if(state.uploading) parts.push("empfängt Firmware");
	if(!state.adapter) parts.push("Adapter antwortet nicht");
	parts.push("Ansichten: " + state.source);
	document.getElementById("screen").textContent = parts.join(" | ");
}

async function ask(path, method)
{
	try
	{
		const answer = await fetch("/mock/" + path, {method: method, headers: method === "POST" ? {"X-Display": "1"} : {}});
		if(answer.ok) show(await answer.json());
	}
	catch(error)
	{
		document.getElementById("screen").textContent = "Der Mock antwortet nicht.";
	}
}

BUTTONS.forEach(function(entry)
{
	const button = document.createElement("button");
	button.type = "button";
	button.textContent = entry[0];
	button.addEventListener("click", function() { ask(entry[1], "POST"); });
	document.getElementById("buttons").appendChild(button);
});
setInterval(function() { ask("state", "GET"); }, 1000);
ask("state", "GET");
</script>
</body>
</html>
"""


class Handler(http.server.BaseHTTPRequestHandler):
    # A connection that brings nothing for this long is given up, as the display gives up an upload
    timeout = UPLOAD_IDLE_S

    def _answer(self):
        headers = {}
        for name, value in self.headers.items():
            headers.setdefault(name.lower(), value)

        def read(count):
            try:
                return self.rfile.read(count)
            except OSError:
                return b""

        display = self.server.display
        if self.path.partition("?")[0].startswith("/mock"):
            answer = display.control(self.command, self.path, headers)
        else:
            answer = display.handle(self.command, self.path, headers, read)
        self.close_connection = True
        if answer is None:
            self._log("no answer")
            return
        status, kind, body = answer
        head = ["HTTP/1.0 %d %s" % (status, REASONS[status]), "Content-Type: " + kind,
                "Content-Length: %d" % len(body), "Cache-Control: no-store", "", ""]
        self.wfile.write("\r\n".join(head).encode("iso-8859-1") + body)
        self._log("%d %s" % (status, body.decode("utf-8") if kind == JSON and len(body) < 160 else "(%d bytes)" % len(body)))

    def _log(self, outcome):
        # The requests of the strip are no part of the display. Never a body of a request: one of them is
        # a WiFi password.
        if self.server.log is not None and not self.path.startswith("/mock/state"):
            self.server.log("%s %s -> %s" % (self.command, self.path, outcome))

    do_GET = do_POST = do_PUT = do_DELETE = do_PATCH = do_OPTIONS = do_HEAD = _answer

    def log_message(self, format, *args):
        pass


class HttpServer(http.server.ThreadingHTTPServer):
    daemon_threads = True

    def handle_error(self, request, client_address):
        # A client that drops its connection is not worth a traceback
        if not isinstance(sys.exc_info()[1], OSError):
            super().handle_error(request, client_address)


class Server:
    """The mock listening on a port of 127.0.0.1."""

    def __init__(self, display=None, port=0, log=None):
        self._httpd = HttpServer(("127.0.0.1", port), Handler)
        self._httpd.display = display if display is not None else Display()
        self._httpd.log = log
        self.host, self.port = self._httpd.server_address[:2]
        self._thread = None

    @property
    def display(self):
        return self._httpd.display

    def start(self):
        """Serves in a background thread until close()."""
        self._thread = threading.Thread(target=self._httpd.serve_forever, kwargs={"poll_interval": 0.01},
                                        name="mock display on port %d" % self.port, daemon=True)
        self._thread.start()
        return self

    def serve_forever(self):
        self._httpd.serve_forever()

    def close(self):
        if self._thread is not None:
            self._httpd.shutdown()
            self._thread.join()
        self._httpd.server_close()


def main(argv=None):
    parser = argparse.ArgumentParser(description="Stand-in for the WiCAN display: its web interface on a PC.")
    parser.add_argument("--port", type=int, default=8907, help="port on 127.0.0.1 (default 8907)")
    parser.add_argument("--upload-rate", type=int, default=256, metavar="KIB",
                        help="a firmware arrives with this many KiB a second, 0: at once (default 256)")
    parser.add_argument("--quiet", action="store_true", help="do not print the requests")
    arguments = parser.parse_args(argv)

    def log(line):
        print(line, flush=True)

    server = Server(Display(upload_rate=arguments.upload_rate), arguments.port, None if arguments.quiet else log)
    print("mock display: http://%s:%d/ (the page), http://%s:%d/mock (the page with the device above it)"
          % (server.host, server.port, server.host, server.port), flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
