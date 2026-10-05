"""Checks the page of the display (display/main/web/index.html) and the stand-in it is tried against
(mock_display.py), both against display/API.md.

The page runs in a browser, and there is none here: what is checked is its text. It names no path that
API.md does not name, every request goes through the one function that adds the header of a change, it
has none of the constructs that make HTML or code out of text, it loads nothing from anywhere, and it is
small enough for the flash. Whether its buttons do what they say is not checked here.

The mock is asked what API.md says the display answers: the examples (where display/test/fixtures has
the text, byte for byte) and each refusal, in the documented order. That holds the mock to API.md, not
the device to it: the firmware has its own tests (display/test).

CounterCheck breaks each thing once - the page on a copy of its text, the mock by a class or a number
that is wrong in one place - and expects the check that guards it to fail.

  python -m unittest -v          in display/tools, as the CI does
"""
import http.client
import os
import re
import unittest
from unittest import mock

import mock_display

HERE = os.path.dirname(os.path.abspath(__file__))
PAGE = os.path.join(HERE, "..", "main", "web", "index.html")
API = os.path.join(HERE, "..", "API.md")
FIXTURES = os.path.join(HERE, "..", "test", "fixtures")

# Below 60 KB, counted as the project counts its flash (partitions.csv: "nvs 128 KB" is 0x20000)
SIZE_MAX = 60 * 1024
HOST = "192.168.1.77"
SLOT = mock_display.SLOT_SIZE


def file_text(path):
    with open(path, encoding="utf-8") as file:
        return file.read()


def fixture(name):
    """A hand-written answer of display/test/fixtures, without the line end of the file"""
    return file_text(os.path.join(FIXTURES, name)).rstrip("\n")


# ----------------------------------------------------------------------------------------------------
# The page

def documented_requests(api_text):
    """The requests of the table "Requests" in API.md as (method, path with its query)"""
    table = api_text.split("## Requests")[1].split("## Refusals")[0]
    return re.findall(r"^\| `(GET|PUT|POST) ([^` ]+)` \|", table, re.M)


def script_of(page):
    """The JavaScript of the page: it has one script element, and that has no src"""
    return "\n".join(re.findall(r"<script>(.*?)</script>", page, re.S))


def code_of(script):
    """The script as (code, strings): the code without its comments and with every string literal emptied,
    and the literals. The page writes its strings in double quotes only (see construct_problems)."""
    code, strings = [], []
    position = 0
    while position < len(script):
        if script.startswith("//", position):
            position = script.find("\n", position) if "\n" in script[position:] else len(script)
        elif script.startswith("/*", position):
            position = script.index("*/", position) + 2
        elif script[position] == "\"":
            end = position + 1
            while script[end] != "\"":
                end += 2 if script[end] == "\\" else 1
            strings.append(script[position + 1:end])
            code.append("\"\"")
            position = end + 1
        else:
            code.append(script[position])
            position += 1
    return "".join(code), strings


def request_problems(page, api_text):
    """What keeps the page from talking to the display through the requests of API.md only, with the
    header of a change on every request that is no GET."""
    found = []
    script = script_of(page)
    code, strings = code_of(script)
    documented = set(documented_requests(api_text))

    # Every call of api() names its method and its path as literals; one path ends with a number
    calls = re.findall(r"(?<![\w.])api\((.{0,60})", script)
    used = set()
    for call in calls:
        if call.startswith("method, path"):
            continue    # the function itself
        literal = re.match(r"\"(GET|PUT|POST)\", \"(/[^\"]*)\"( \+ id)?[,)]", call)
        if literal is None:
            found.append("a request whose method and path are not written out: api(%s" % call)
            continue
        method, path, numbered = literal.groups()
        request = (method, path + "N" if numbered else path)
        if request not in documented:
            found.append("%s %s is no request of API.md" % request)
        used.add(request)
    for request in sorted(documented - used - {("GET", "/")}):
        found.append("the page never asks for %s %s" % request)

    # No path anywhere else
    paths = {path[:-1] if path.endswith("N") else path for _, path in documented}
    for literal in strings:
        if literal.startswith("/") and literal not in paths:
            found.append("a path that is no request of API.md: %s" % literal)
    if "/mock" in page:
        found.append("the page knows the mock")

    # One function sends, and it adds the header to everything that is no GET
    function = re.search(r"\nfunction api\(method, path, body, progress\)\n\{\n(.*?)\n\}\n", script, re.S)
    if function is None:
        found.append("no function api(method, path, body, progress)")
    else:
        if "xhr.open(method, path);" not in function.group(1):
            found.append("api() does not open the request it was asked for")
        if "\n\t\tif(method !== \"GET\") xhr.setRequestHeader(\"X-Display\", \"1\");\n" not in function.group(1):
            found.append("api() does not add X-Display: 1 to every request that is no GET")
    if code.count("XMLHttpRequest") != 1 or code.count(".open(") != 1:
        found.append("requests are made in more than one place")
    for other in ("fetch(", "sendBeacon", "WebSocket", "EventSource", ".submit(", "location.href", "location.assign",
                  "location.replace", "window.open", "importScripts", "import("):
        if other in code:
            found.append("another way to send a request: %s" % other)
    for element in ("<form", "<a ", "<iframe", "<object", "<embed"):
        if element in page.lower():
            found.append("an element that sends requests by itself: %s" % element)
    return found


def construct_problems(page):
    """What could turn a text that came over the network into HTML or into code. None of it is allowed at
    all, with data or without: nobody has to judge then where a text came from."""
    found = []
    for word in ("innerHTML", "outerHTML", "insertAdjacentHTML", "document.write", "eval(", "new Function",
                 "setAttribute", "srcdoc", "DOMParser", "createContextualFragment", "javascript:",
                 "setTimeout(\"", "setInterval(\""):
        if word in page:
            found.append("forbidden: %s" % word)
    # An event handler as an attribute or as a property: the page listens with addEventListener only
    for handler in re.findall(r"\bon[a-z]+\s*=", page):
        found.append("an event handler that is no listener: %s" % handler)
    # code_of() reads double quotes only, and a text between other quotes would pass every check above
    script = script_of(page)
    if "'" in script or "`" in script:
        found.append("a quote in the script that is no double quote")
    if page.count("<script") != 1 or "<script>" not in page:
        found.append("not exactly one script element without attributes")
    return found


def external_problems(page):
    """What the page would load from somewhere, or where it would send somebody"""
    found = []
    if "://" in page:
        found.append("an address with a scheme")
    for attribute, value in re.findall(r"\b(src|href|action|poster|data|srcset)\s*=\s*\"([^\"]*)\"", page):
        if (attribute, value) != ("href", "data:,"):
            found.append("%s=\"%s\"" % (attribute, value))
    code, strings = code_of(script_of(page))
    for assigned in re.findall(r"\b(?:href|src)\b\s*[:=]\s*[^,;}]*", code):
        if not assigned.startswith("href: URL.createObjectURL("):
            found.append("an address set by the script: %s" % assigned)
    if any(literal.startswith("//") for literal in strings):
        found.append("an address without its scheme")
    styles = "".join(re.findall(r"<style>(.*?)</style>", page, re.S)) + "".join(re.findall(r"\bstyle\s*=\s*\"[^\"]*\"", page))
    for word in ("url(", "@import", "@font-face", "image-set("):
        if word in styles or any(word in literal for literal in strings):
            found.append("a style that loads something: %s" % word)
    for word in ("<img", "<video", "<audio", "<source", "<base", "<script src"):
        if word in page.lower():
            found.append("loads something: %s" % word)
    if len(re.findall(r"<link\b", page)) != page.count("<link rel=\"icon\" href=\"data:,\">"):
        found.append("a link element that is not the empty icon")
    return found


def size_problems(data):
    return ["%d bytes, at most %d" % (len(data), SIZE_MAX - 1)] if len(data) >= SIZE_MAX else []


class Page(unittest.TestCase):
    def setUp(self):
        with open(PAGE, "rb") as file:
            self.data = file.read()
        self.page = self.data.decode("utf-8")
        self.api = file_text(API)

    def test_the_table_of_api_md_is_read(self):
        requests = documented_requests(self.api)
        self.assertEqual(len(requests), 18)
        self.assertIn(("PUT", "/api/layout?mode=apply"), requests)
        self.assertIn(("GET", "/api/ticket?id=N"), requests)

    def test_the_script_is_read(self):
        code, strings = code_of("call(\"a\\\"b\", 1); // \"no string\"\n/* \"nor this\" */ x = \"//c\";")
        self.assertEqual(strings, ["a\\\"b", "//c"])
        self.assertEqual(code, "call(\"\", 1); \n x = \"\";")
        self.assertGreater(len(code_of(script_of(self.page))[1]), 300)

    def test_requests_are_those_of_api_md_and_carry_the_header(self):
        self.assertEqual(request_problems(self.page, self.api), [])

    def test_nothing_makes_html_or_code_of_a_text(self):
        self.assertEqual(construct_problems(self.page), [])

    def test_nothing_is_loaded_from_anywhere(self):
        self.assertEqual(external_problems(self.page), [])

    def test_size(self):
        self.assertEqual(size_problems(self.data), [])


# ----------------------------------------------------------------------------------------------------
# The mock

def reader(data, meanwhile=None):
    """read(count) over the bytes of a body. meanwhile: called once, before the last bytes are handed out."""
    state = {"at": 0, "called": meanwhile is None}

    def read(count):
        piece = data[state["at"]:state["at"] + count]
        state["at"] += len(piece)
        if state["at"] >= len(data) and not state["called"]:
            state["called"] = True
            meanwhile()
        return piece
    return read


class Api(unittest.TestCase):
    """The mock against display/API.md. The class of the display is an attribute so that CounterCheck can
    run a test against one that is wrong."""
    display_class = mock_display.Display

    def setUp(self):
        self.clock = mock_display.SimulatedClock()
        self.display = self.display_class(clock=self.clock)

    def call(self, method, target, body=None, host=HOST, header=True, length=True, read=None):
        """One request as a browser sends it: (status, body), or None without an answer"""
        headers = {}
        if host is not None:
            headers["host"] = host
        if method != "GET" and header:
            headers["x-display"] = "1" if header is True else header
        if method != "GET" and length:
            headers["content-length"] = str(len(body or b"") if length is True else length)
        answer = self.display.handle(method, target, headers, read or reader(body or b""))
        if answer is None:
            return None
        self.assertEqual(answer[1], "text/html; charset=utf-8" if (method, target, answer[0]) == ("GET", "/", 200) else "application/json")
        return answer[0], answer[2].decode("utf-8")

    def release(self):
        self.display.release(True)

    def ask_reset(self):
        """A question that waits, the simplest one"""
        answer = self.call("POST", "/api/reset")
        self.assertEqual(answer[0], 202)
        return answer

    # ------------------------------------------------------------------------------------------------
    # Bodies

    def test_info(self):
        # The example of API.md: 60 s of a release are gone, the firmware waits for "Update in Ordnung?"
        display = self.display
        display.access.switch_on(4651 * 1000)
        display.update_pending = True
        display.source = "stored"
        self.assertEqual(display.info_json(4711 * 1000), fixture("web_info.json"))
        # And what a request gets: the same writer, the state of now
        display.update_pending = False
        self.clock.advance(4711)
        status, body = self.call("GET", "/api/info")
        self.assertEqual(status, 200)
        self.assertEqual(body, display.info_json(4711 * 1000))
        self.assertIn("\"up\":4711,", body)
        self.assertNotIn("geheim123", body)

    def test_catalog(self):
        self.display.catalog = [["@BATT_V", "V", "", False, False]]
        self.assertEqual(self.call("GET", "/api/catalog"), (200, fixture("app_web_catalog_start.json")))
        self.display.catalog = [["@BATT_V", "V", "", False, True], ["SPEED", "km/h", "speed", True, True],
                                ["RPM", "rpm", "frequency", True, True], ["COOLANT", "°C", "temperature", True, True],
                                ["FUEL", "%", "none", True, True], ["INTAKE_TEMP", "°C", "temperature", True, True]]
        self.assertEqual(self.call("GET", "/api/catalog"), (200, fixture("app_web_catalog.json")))
        # The catalogue of the mock itself: the battery first, then the profile in its order
        names = re.findall(r"\"([^\"]+)\":\{\"unit\"", mock_display.Display().catalog_json())
        self.assertEqual(names[:3], ["@BATT_V", "ENGINE_RPM", "CHARGE_AIR_TEMP_PRE_IC"])
        self.assertEqual(len(names), 38)

    def test_values(self):
        display = self.display
        display.values = lambda now: [("ENGINE_RPM", 812.5, now), ("DPF_REGEN_STATUS", "on", now - 5000)]
        self.assertEqual(self.call("GET", "/api/values"), (200, fixture("web_values.json")))
        display.view = lambda: "ecu_offline"
        display.values = lambda now: [("COOLANT_TMP", 88.25, now), ("GLOW_ACTIVE", "off", now - 2999),
                                      ("@BATT_V", 12.4, now - 3000), ("ECU_DISTANCE", 187432, now),
                                      ("RAIL_PRESSURE", -0.0, now - 9999), ("OUTSIDE_TMP", -40, now),
                                      ("GONE", 1, now - 10000)]
        self.clock.advance(20)
        self.assertEqual(self.call("GET", "/api/values"), (200, fixture("web_values_mixed.json")))

    def test_values_of_the_mock_move_and_age(self):
        first = self.call("GET", "/api/values")[1]
        self.assertIn("\"view\":\"live\"", first)
        self.assertEqual(first.count("\"age\":\"fresh\""), 38)
        self.clock.advance(1)
        second = self.call("GET", "/api/values")[1]
        rpm = [re.search(r"\"ENGINE_RPM\":\{\"v\":([0-9.]+),", body).group(1) for body in (first, second)]
        self.assertNotEqual(rpm[0], rpm[1])
        # Fresh under 3 s, old under 10 s, then left out
        self.display.set_adapter(False)
        self.clock.advance(2.999)
        self.assertEqual(self.call("GET", "/api/values")[1].count("\"age\":\"fresh\""), 38)
        self.clock.advance(0.001)
        self.assertEqual(self.call("GET", "/api/values")[1].count("\"age\":\"old\""), 38)
        self.clock.advance(6.999)
        self.assertEqual(self.call("GET", "/api/values")[1].count("\"age\":\"old\""), 38)
        self.clock.advance(0.001)
        self.assertEqual(self.call("GET", "/api/values"), (200, "{\"view\":\"no_answer\",\"values\":{}}"))

    def test_layout_reports(self):
        with open(mock_display.BUILTIN_LAYOUT, "rb") as file:
            builtin = file.read()
        self.assertEqual(self.call("GET", "/api/layout"), (200, builtin.decode("utf-8")))
        for name, status, report in (("app_web_layout_refused.json", 400, "app_web_report_refused.json"),
                                     ("app_web_layout_warning.json", 200, "app_web_report_warning.json"),
                                     ("app_web_layout_hidden.json", 200, "app_web_report_hidden.json")):
            with self.subTest(name):
                body = (fixture(name) + "\n").encode("utf-8")
                self.assertEqual(self.call("PUT", "/api/layout?mode=check", body), (status, fixture(report)))
        self.assertEqual(self.call("PUT", "/api/layout?mode=check", builtin), (200, fixture("app_web_report_builtin.json")))
        # The check needs no release and changes nothing
        self.assertEqual(self.call("GET", "/api/layout"), (200, builtin.decode("utf-8")))
        self.assertIn("\"layout\":{\"name\":\"W906 OM651 Standard\",\"source\":\"builtin\"}", self.call("GET", "/api/info")[1])
        # "unknown" is empty while no catalogue is loaded
        self.display.catalog = self.display.catalog[:1]
        body = (fixture("app_web_layout_warning.json") + "\n").encode("utf-8")
        self.assertIn("\"unknown\":[]", self.call("PUT", "/api/layout?mode=check", body)[1])

    def test_layout_problems(self):
        def layout(page):
            return ("{\"format\":\"wican-display-layout\",\"v\":1,\"pages\":[%s]}" % page).encode("utf-8")

        def item(members):
            return layout("{\"items\":[{%s}]}" % members)

        refused = {
            b"{\"format\":": ("", "not valid JSON"),
            b"[]": ("", "not a JSON object"),
            b"{\"format\":\"wican-display-layout\",\"v\":1,\"pages\":[" + b"[" * 8 + b"]" * 8 + b"]}": ("", "not valid JSON"),
            b"{\"format\":\"other\",\"v\":1,\"pages\":[]}": ("format", "not wican-display-layout"),
            b"{\"format\":\"wican-display-layout\",\"v\":1.0,\"pages\":[]}": ("v", "not an integer"),
            b"{\"format\":\"wican-display-layout\",\"v\":2,\"pages\":[]}": ("v", "layout of a newer display"),
            b"{\"format\":\"wican-display-layout\",\"v\":0,\"pages\":[]}": ("v", "below 1"),
            b"{\"format\":\"wican-display-layout\",\"v\":1,\"name\":7,\"pages\":[]}": ("name", "not a text"),
            b"{\"format\":\"wican-display-layout\",\"v\":1}": ("pages", "missing"),
            b"{\"format\":\"wican-display-layout\",\"v\":1,\"pages\":{}}": ("pages", "not an array"),
            layout(""): ("pages", "empty"),
            layout(",".join(["{\"items\":[{\"key\":\"K\"}]}"] * 13)): ("pages", "too many pages"),
            layout("7"): ("pages[0]", "not an object"),
            layout("{\"title\":\"%s\",\"items\":[{\"key\":\"K\"}]}" % ("ä" * 12 + "x")): ("pages[0].title", "too long"),
            layout("{\"hidden\":1,\"items\":[{\"key\":\"K\"}]}"): ("pages[0].hidden", "not a boolean"),
            layout("{}"): ("pages[0].items", "missing"),
            layout("{\"items\":[]}"): ("pages[0].items", "empty"),
            layout("{\"items\":[%s]}" % ",".join(["{\"key\":\"K\"}"] * 7)): ("pages[0].items", "too many items"),
            layout("{\"items\":[{\"key\":\"K\"}]},{\"items\":[{\"key\":\"A\"},{}]}"): ("pages[1].items[1].key", "missing"),
            item("\"key\":\"\""): ("pages[0].items[0].key", "empty"),
            item("\"key\":\"a\\u0007\""): ("pages[0].items[0].key", "control character"),
            item("\"key\":\"K\",\"label\":\"\\ud83d\""): ("pages[0].items[0].label", "half a surrogate pair"),
            item("\"key\":\"K\",\"unit\":\"123456789\""): ("pages[0].items[0].unit", "too long"),
            item("\"key\":\"K\",\"dec\":4"): ("pages[0].items[0].dec", "not an integer 0 to 3"),
            item("\"key\":\"K\",\"widget\":7"): ("pages[0].items[0].widget", "not a text"),
            item("\"key\":\"K\",\"scale\":0"): ("pages[0].items[0].scale", "zero"),
            item("\"key\":\"K\",\"min\":null"): ("pages[0].items[0].min", "not a number"),
            item("\"key\":\"K\",\"max\":1e999"): ("pages[0].items[0].max", "not finite"),
            item("\"key\":\"K\",\"min\":2,\"max\":2"): ("pages[0].items[0].min", "min is not below max"),
            item("\"key\":\"K\",\"map\":[]"): ("pages[0].items[0].map", "not an object"),
            item("\"key\":\"K\",\"map\":{%s}" % ",".join("\"%d\":\"x\"" % n for n in range(9))): ("pages[0].items[0].map", "too many entries"),
            item("\"key\":\"K\",\"map\":{\"1\":\"a\",\"2\":2}"): ("pages[0].items[0].map[1]", "not a text"),
            item("\"key\":\"K\",\"map\":{\"123456789012\":\"a\"}"): ("pages[0].items[0].map[0]", "too long"),
            # Of several problems the first in the order of layout.h is the one named
            item("\"dec\":9,\"key\":\"K\",\"label\":7"): ("pages[0].items[0].label", "not a text"),
        }
        for body, (path, problem) in refused.items():
            with self.subTest(body=body[:90]):
                self.assertEqual(self.call("PUT", "/api/layout?mode=check", body),
                                 (400, "{\"ok\":false,\"path\":\"%s\",\"problem\":\"%s\"}" % (path, problem)))
        # The first of two members with the same name counts, as for the reader of the display
        self.assertEqual(self.call("PUT", "/api/layout?mode=check", item("\"key\":\"K\",\"dec\":1,\"dec\":9"))[0], 200)
        # Taken with a warning; the first one is told, the others are counted
        status, body = self.call("PUT", "/api/layout?mode=check", (fixture("layout_warnings.json") + "\n").encode("utf-8"))
        self.assertEqual(status, 200)
        self.assertIn("\"warnings\":6,\"warning_path\":\"pages[0].items[1].widget\",\"warning\":\"unknown widget, shown as number\"", body)
        status, body = self.call("PUT", "/api/layout?mode=check", (fixture("layout_every_member.json") + "\n").encode("utf-8"))
        self.assertEqual((status, "\"warnings\":0," in body), (200, True))

    def test_layout_apply_save_reset(self):
        sent = (fixture("app_web_layout_hidden.json") + "\n").encode("utf-8")
        self.release()
        self.assertEqual(self.call("PUT", "/api/layout?mode=apply", sent), (200, fixture("app_web_report_hidden.json")))
        # The text that was sent, byte for byte; applied but not stored
        self.assertEqual(self.call("GET", "/api/layout"), (200, sent.decode("utf-8")))
        self.assertIn("\"layout\":{\"name\":\"Nichts zu sehen\",\"source\":\"preview\"}", self.call("GET", "/api/info")[1])
        # A restart ends the preview
        self.display.restart()
        self.clock.advance(5)
        self.assertIn("\"source\":\"builtin\"", self.call("GET", "/api/info")[1])

        self.release()
        self.assertEqual(self.call("PUT", "/api/layout?mode=save", sent)[0], 200)
        self.assertIn("\"source\":\"stored\"", self.call("GET", "/api/info")[1])
        self.display.restart()
        self.clock.advance(5)
        self.assertEqual(self.call("GET", "/api/layout"), (200, sent.decode("utf-8")))
        # A layout that is refused changes nothing
        self.release()
        self.assertEqual(self.call("PUT", "/api/layout?mode=save", b"{}")[0], 400)
        self.assertEqual(self.call("GET", "/api/layout"), (200, sent.decode("utf-8")))
        # The reset answers the report of the views the display then uses; its body is not looked at
        self.assertEqual(self.call("POST", "/api/layout/reset", b"whatever"), (200, fixture("app_web_report_builtin.json")))
        self.assertIn("\"source\":\"builtin\"", self.call("GET", "/api/info")[1])
        self.display.restart()
        self.clock.advance(5)
        self.assertIn("\"name\":\"W906 OM651 Standard\",\"source\":\"builtin\"", self.call("GET", "/api/info")[1])

    def test_dtc_last(self):
        self.display.set_lists("none")
        self.assertEqual(self.call("GET", "/api/dtc/last"), (200, fixture("web_dtc_last_none.json")))
        # The example of API.md, taken apart into its two lists
        example = fixture("web_dtc_last.json")
        read, rest = example[len("{\"read\":"):].split(",\"read_age_s\":95,\"before_clear\":")
        self.display.read, self.display.before_clear = read, rest[:-1]
        self.display.read_ms = self.display.now()
        self.clock.advance(95.999)
        self.assertEqual(self.call("GET", "/api/dtc/last"), (200, example))
        self.display.read = None
        self.assertEqual(self.call("GET", "/api/dtc/last"), (200, "{\"read\":null,\"read_age_s\":0,\"before_clear\":%s}" % rest[:-1]))
        # What the mock holds when it starts is a result of the adapter
        self.assertIn("{\"read\":{\"state\":\"done\",\"action\":\"read\",", mock_display.Display().dtc_last_json(0))

    def test_wifi(self):
        display = self.display
        display.profiles = [["Werkstatt", "geheim123", "192.168.1.50"], ["WiCAN_a1b2c3d4e5f6", "@meatpi#", ""], ["Camping", "", ""]]
        display.seen = [["Werkstatt", -52, True], ["", -60, True], ["WiCAN_a1b2c3d4e5f6", -40, True], ["Freifunk", -88, False]]
        self.assertEqual(self.call("GET", "/api/wifi"), (200, fixture("web_wifi.json")))
        display.profiles, display.seen, display.current = [], [], ""
        self.assertEqual(self.call("GET", "/api/wifi"), (200, fixture("web_wifi_empty.json")))
        self.assertIn("\"wifi\":{\"ssid\":\"\",\"ip\":\"\",\"rssi\":0,\"ap\":true,", self.call("GET", "/api/info")[1])
        display.profiles = [["WiCAN_", "x" * 8, ""]]
        self.assertIn("\"wican_ap\":false", self.call("GET", "/api/wifi")[1])

    def test_wifi_store_and_forget(self):
        display = self.display
        self.release()
        self.assertEqual(self.call("POST", "/api/wifi", (fixture("web_wifi_request.json")).encode("utf-8")), (202, fixture("app_web_asked_1.json")))
        self.assertEqual(display.asked_detail, "Werkstatt")
        self.clock.advance(2)
        self.assertEqual(display.press(), "wifi")
        self.assertEqual(self.call("GET", "/api/ticket?id=1"), (200, fixture("app_web_ticket_confirmed.json")))

        # Without a password the stored one is kept; for a new network it means an open one. A new one is
        # the first of the list, and only the knob stores anything.
        for body in (b"{\"ssid\":\"Werkstatt\",\"host\":\"\"}", b"{\"ssid\":\"Neu\"}"):
            self.assertEqual(self.call("POST", "/api/wifi", body)[0], 202)
            self.assertNotIn("Neu", self.call("GET", "/api/wifi")[1])
            self.clock.advance(2)
            display.press()
        self.assertEqual(display.profiles, [["Neu", "", ""], ["Werkstatt", "geheim123", ""]])
        answer = self.call("GET", "/api/wifi")[1]
        self.assertTrue(answer.startswith("{\"current\":\"Werkstatt\",\"profiles\":[{\"ssid\":\"Neu\",\"host\":\"\",\"password\":false,"))
        self.assertNotIn("geheim123", answer)
        # Up to 4 networks: with a fifth the last one falls out
        for name in ("A", "B", "C"):
            self.call("POST", "/api/wifi", ("{\"ssid\":\"%s\",\"password\":\"12345678\"}" % name).encode("utf-8"))
            self.clock.advance(2)
            display.press()
        self.assertEqual([profile[0] for profile in display.profiles], ["C", "B", "A", "Neu"])

        for body in (b"", b"[]", b"{\"password\":\"12345678\"}", b"{\"ssid\":\"\"}", b"{\"ssid\":7}", b"{\"ssid\":\"" + b"x" * 33 + b"\"}",
                     b"{\"ssid\":\"a\\nb\"}", b"{\"ssid\":\"N\",\"password\":\"1234567\"}", b"{\"ssid\":\"N\",\"password\":\"" + b"x" * 65 + b"\"}",
                     b"{\"ssid\":\"N\",\"host\":\"" + b"h" * 40 + b"\"}", b"{\"ssid\":\"N\",\"ssid\":\"\"}"):
            with self.subTest(body=body):
                self.assertEqual(self.call("POST", "/api/wifi", body), (400, fixture("app_web_body.json")))
        self.assertEqual(self.call("GET", "/api/ticket?id=7")[1], "{\"ticket\":7,\"state\":\"unknown\",\"left_s\":0}")

        display.profiles.append(["Camping", "", ""])
        forget = fixture("web_forget_request.json").encode("utf-8")
        self.assertEqual(self.call("POST", "/api/wifi/forget", forget), (200, fixture("app_web_ok.json")))
        self.assertEqual(self.call("POST", "/api/wifi/forget", forget), (404, fixture("app_web_not_found.json")))
        self.assertEqual(self.call("POST", "/api/wifi/forget", b"{\"host\":\"x\"}"), (400, fixture("app_web_body.json")))

    def test_settings(self):
        self.release()
        self.assertEqual(self.call("POST", "/api/settings", b"{}"), (200, fixture("app_web_settings_defaults.json")))
        self.assertEqual(self.call("POST", "/api/settings", b"{\"brightness\":40,\"reverse\":true,\"standby_s\":0,\"later\":[1]}"),
                         (200, fixture("app_web_settings_changed.json")))
        self.assertEqual(self.call("POST", "/api/settings", fixture("app_web_settings_browser.json").encode("utf-8")),
                         (200, fixture("app_web_settings_browser.json")))
        self.assertIn("\"settings\":" + fixture("app_web_settings_browser.json"), self.call("GET", "/api/info")[1])
        refused = {
            b"": "none", b"[]": "none", b"{\"brightness\":80": "none",
            b"{\"brightness\":4}": "brightness", b"{\"brightness\":101}": "brightness", b"{\"brightness\":80.0}": "brightness",
            b"{\"night\":\"25\"}": "night", b"{\"night_mode\":1}": "night_mode", b"{\"reverse\":null}": "reverse",
            b"{\"standby_s\":3601}": "standby_s", b"{\"standby_s\":-1}": "standby_s",
            # The first one in the order of the text, and nothing of the text is taken
            b"{\"night\":50,\"standby_s\":1e2,\"brightness\":0}": "standby_s",
            b"{\"night\":50,\"night\":4}": "night",
        }
        for body, member in refused.items():
            with self.subTest(body=body):
                self.assertEqual(self.call("POST", "/api/settings", body), (400, fixture("app_web_member_%s.json" % member)))
        self.assertEqual(self.call("POST", "/api/settings", b"{\"standby_s\":3600,\"brightness\":5}")[1],
                         "{\"brightness\":5,\"night\":10,\"night_mode\":true,\"reverse\":true,\"standby_s\":3600}")

    # ------------------------------------------------------------------------------------------------
    # The release and the knob

    def test_release(self):
        locked = (403, fixture("app_web_locked.json"))
        changes = (("PUT", "/api/layout?mode=apply"), ("PUT", "/api/layout?mode=save"), ("POST", "/api/layout/reset"),
                   ("POST", "/api/wifi"), ("POST", "/api/wifi/forget"), ("POST", "/api/settings"), ("POST", "/api/reboot"),
                   ("POST", "/api/reset"), ("POST", "/api/ota"))
        for method, target in changes:
            with self.subTest(target):
                self.assertEqual(self.call(method, target, b"{}"), locked)
        self.assertEqual(self.call("GET", "/api/info")[1].count("\"release\":{\"open\":false,\"left_s\":0}"), 1)

        # 10 minutes after it was given
        self.release()
        self.clock.advance(0.001)
        self.assertIn("\"release\":{\"open\":true,\"left_s\":600}", self.call("GET", "/api/info")[1])
        self.clock.advance(599.998)
        self.assertIn("\"release\":{\"open\":true,\"left_s\":1}", self.call("GET", "/api/info")[1])
        self.clock.advance(0.001)
        self.assertEqual(self.call("POST", "/api/settings", b"{}"), locked)

        # or after the last accepted change - also one that is then refused for another reason
        self.release()
        self.clock.advance(500)
        self.assertEqual(self.call("POST", "/api/settings", b"no settings")[0], 400)
        self.clock.advance(599)
        self.assertEqual(self.call("POST", "/api/wifi/forget", b"{\"ssid\":\"Nirgends\"}")[0], 404)
        self.clock.advance(599)
        # at the latest 30 minutes after it was switched on, whatever was changed since
        self.assertEqual(self.call("POST", "/api/settings", b"{}")[0], 200)
        self.assertIn("\"release\":{\"open\":true,\"left_s\":102}", self.call("GET", "/api/info")[1])
        self.clock.advance(101.999)
        self.assertEqual(self.call("GET", "/api/values")[0], 200)
        self.assertIn("\"left_s\":1}", self.call("GET", "/api/info")[1])
        self.clock.advance(0.001)
        self.assertEqual(self.call("POST", "/api/settings", b"{}"), locked)

        # A question to the knob that is not asked renews nothing, neither does a check
        self.release()
        self.clock.advance(500)
        self.assertEqual(self.call("POST", "/api/wifi", b"no network"), (400, fixture("app_web_body.json")))
        self.assertEqual(self.call("PUT", "/api/layout?mode=check", b"{}")[0], 400)
        self.clock.advance(100)
        self.assertEqual(self.call("POST", "/api/settings", b"{}"), locked)

        # It ends when it is switched off at the display, and with a restart
        self.release()
        self.display.release(False)
        self.assertEqual(self.call("POST", "/api/settings", b"{}"), locked)
        self.release()
        self.display.restart()
        self.clock.advance(5)
        self.assertEqual(self.call("POST", "/api/settings", b"{}"), locked)

    def test_tickets(self):
        display = self.display
        self.release()
        # The examples of API.md: the 17th question, 18 of its 60 seconds gone
        for _ in range(16):
            self.ask_reset()
            display.press_long()
        self.assertEqual(self.ask_reset(), (202, fixture("web_asked.json")))
        self.clock.advance(18)
        self.assertEqual(self.call("GET", "/api/ticket?id=17"), (200, fixture("web_ticket.json")))
        self.assertEqual(self.call("GET", "/api/ticket?id=017")[1], fixture("web_ticket.json"))
        # Known are the last ticket and the one before it
        self.assertEqual(self.call("GET", "/api/ticket?id=16")[1], "{\"ticket\":16,\"state\":\"refused\",\"left_s\":0}")
        self.assertEqual(self.call("GET", "/api/ticket?id=15")[1], "{\"ticket\":15,\"state\":\"unknown\",\"left_s\":0}")
        self.assertEqual(self.call("GET", "/api/ticket?id=18")[1], "{\"ticket\":18,\"state\":\"unknown\",\"left_s\":0}")
        # Nobody pressed the knob
        self.clock.advance(41.999)
        self.assertEqual(self.call("GET", "/api/ticket?id=17")[1], "{\"ticket\":17,\"state\":\"waiting\",\"left_s\":1}")
        self.clock.advance(0.001)
        self.assertEqual(self.call("GET", "/api/ticket?id=17")[1], "{\"ticket\":17,\"state\":\"expired\",\"left_s\":0}")
        self.assertIsNone(display.press())

    def test_ticket_ends(self):
        display = self.display
        self.release()
        self.assertEqual(self.ask_reset(), (202, fixture("app_web_asked_1.json")))
        self.assertEqual(self.call("GET", "/api/ticket?id=1"), (200, fixture("app_web_ticket_waiting.json")))
        # A press in the first 1.5 seconds does not count
        self.clock.advance(1.499)
        self.assertIsNone(display.press())
        self.assertEqual(self.call("GET", "/api/ticket?id=1")[1], "{\"ticket\":1,\"state\":\"waiting\",\"left_s\":59}")
        self.clock.advance(0.001)
        self.assertEqual(display.press(), "reset")
        # After a confirmed factory reset the display restarts and cannot answer any more
        self.assertIsNone(self.call("GET", "/api/ticket?id=1"))
        self.clock.advance(5)
        # The numbers start again at 1 after a restart; WiFi and settings are gone, the views stay
        self.assertEqual(self.call("GET", "/api/ticket?id=1")[1], fixture("app_web_ticket_unknown.json").replace("7", "1"))
        self.assertEqual(self.call("GET", "/api/wifi")[1][:27], "{\"current\":\"\",\"profiles\":[]")

        # Refused at the display, also at once; the release goes on
        self.release()
        self.assertEqual(self.ask_reset(), (202, fixture("app_web_asked_1.json")))
        display.press_long()
        self.assertEqual(self.call("GET", "/api/ticket?id=1"), (200, fixture("app_web_ticket_refused.json")))
        # Refused because the release was switched off, or ended (the last minute of its 30)
        self.assertEqual(self.ask_reset()[1], fixture("app_web_asked_2.json"))
        display.release(False)
        self.assertEqual(self.call("GET", "/api/ticket?id=2")[1], "{\"ticket\":2,\"state\":\"refused\",\"left_s\":0}")
        self.release()
        for _ in range(3):
            self.clock.advance(590)
            self.assertEqual(self.call("POST", "/api/settings", b"{}")[0], 200)
        self.assertEqual(self.ask_reset()[1], fixture("app_web_asked_3.json"))
        self.assertEqual(self.call("GET", "/api/ticket?id=3")[1], "{\"ticket\":3,\"state\":\"waiting\",\"left_s\":30}")
        self.clock.advance(30)
        self.assertEqual(self.call("GET", "/api/ticket?id=3")[1], "{\"ticket\":3,\"state\":\"refused\",\"left_s\":0}")

    # ------------------------------------------------------------------------------------------------
    # The firmware

    def test_ota(self):
        display = self.display
        image = mock_display.firmware_image(version="0.2.0", size=300000)
        self.release()
        self.assertEqual(self.call("POST", "/api/ota", image), (202, fixture("app_web_asked_1.json")))
        # The display shows the version of the file and asks for the knob; only the press starts it
        self.assertEqual((display.asked_detail, display.version, display.uploading), ("0.2.0", "0.1.0", False))
        self.clock.advance(2)
        self.assertEqual(display.press(), "firmware")
        self.assertIsNone(self.call("GET", "/api/info"))
        self.clock.advance(5)
        info = self.call("GET", "/api/info")[1]
        self.assertIn("\"version\":\"0.2.0\",", info)
        self.assertIn("\"slot\":\"ota_1\",", info)
        self.assertIn("\"rolled_back\":false,\"update_pending\":true,", info)
        # No upload while the running firmware still waits for "Update in Ordnung?"
        self.release()
        self.assertEqual(self.call("POST", "/api/ota", image), (409, fixture("app_web_busy.json")))
        # Without an answer within 5 minutes the version before it runs again
        self.clock.advance(298.999)
        self.assertEqual(self.call("GET", "/api/values")[0], 200)
        self.clock.advance(0.001)
        self.assertIsNone(self.call("GET", "/api/info"))
        self.clock.advance(5)
        info = self.call("GET", "/api/info")[1]
        self.assertIn("\"version\":\"0.1.0\",", info)
        self.assertIn("\"slot\":\"ota_0\",", info)
        self.assertIn("\"rolled_back\":true,\"update_pending\":false,", info)

        # Confirmed at the display, it stays - also over a restart
        self.release()
        self.assertEqual(self.call("POST", "/api/ota", image)[0], 202)
        self.clock.advance(2)
        display.press()
        self.clock.advance(5)
        display.update_ok()
        display.restart()
        self.clock.advance(5)
        self.assertIn("\"version\":\"0.2.0\",", self.call("GET", "/api/info")[1])
        # and any restart before that takes it back
        self.release()
        self.assertEqual(self.call("POST", "/api/ota", mock_display.firmware_image(version="0.3.0", size=5000))[0], 202)
        self.clock.advance(2)
        display.press()
        self.clock.advance(5)
        display.restart()
        self.clock.advance(5)
        self.assertIn("\"version\":\"0.2.0\",", self.call("GET", "/api/info")[1])

    def test_ota_refused_files(self):
        good = mock_display.firmware_image(size=4000)
        files = {
            "too_short": good[:111],
            "no_image": b"\x00" + good[1:],
            "wrong_chip": mock_display.firmware_image(size=4000, chip=0x0005),
            "no_description": good[:32] + b"\x00\x00\x00\x00" + good[36:],
            "wrong_project": mock_display.firmware_image("wican-fw", "4.21", 4000),
        }
        self.release()
        for word, data in files.items():
            with self.subTest(word):
                self.assertEqual(self.call("POST", "/api/ota", data), (422, fixture("app_web_%s.json" % word)))
                self.assertEqual((self.display.uploading, self.display.access.ticket), (False, 0))
        # A project name without its end is no description either
        self.assertEqual(self.call("POST", "/api/ota", good[:80] + b"x" * 32 + good[112:])[1], fixture("app_web_no_description.json"))
        self.assertEqual(self.call("POST", "/api/ota", b""), (413, fixture("app_web_too_large.json")))
        self.assertEqual(self.call("POST", "/api/ota", good, length=SLOT + 1), (413, fixture("app_web_too_large.json")))
        self.assertEqual(self.call("POST", "/api/ota", mock_display.firmware_image(size=SLOT))[0], 202)

    def test_ota_that_breaks_or_comes_late(self):
        display = self.display
        image = mock_display.firmware_image(size=50000)
        # A broken upload
        self.release()
        self.assertEqual(self.call("POST", "/api/ota", image, read=reader(image[:20000])), (500, fixture("app_web_upload.json")))
        self.assertEqual((display.uploading, display.access.ticket), (False, 0))
        # One the display had ended (here: by a restart)
        self.assertEqual(self.call("POST", "/api/ota", image, read=reader(image, lambda: setattr(display, "uploading", False))),
                         (500, fixture("app_web_upload.json")))
        # The release has ended when the upload is complete: the file is not asked for
        self.assertEqual(self.call("POST", "/api/ota", image, read=reader(image, lambda: self.clock.advance(600))),
                         (403, fixture("app_web_locked.json")))
        self.assertEqual(display.access.ticket, 0)

        # An upload begins only while the release lasts another 5 minutes: not in the last 5 of its 30
        self.release()
        for _ in range(2):
            self.clock.advance(590)
            self.assertEqual(self.call("POST", "/api/settings", b"{}")[0], 200)
        # 1500.001 s after it was given 299.999 s are left, which are 300 rounded up
        self.clock.advance(320.001)
        self.assertEqual(self.call("POST", "/api/ota", image)[0], 202)
        display.press_long()
        self.clock.advance(0.999)
        self.assertEqual(self.call("POST", "/api/ota", image), (403, fixture("app_web_locked.json")))
        self.assertIn("\"release\":{\"open\":true,\"left_s\":299}", self.call("GET", "/api/info")[1])
        self.release()
        self.assertEqual(self.call("POST", "/api/ota", image)[0], 202)
        # While a question waits no upload begins, while one runs no second one
        self.assertEqual(self.call("POST", "/api/ota", image), (409, fixture("app_web_asking.json")))
        display.press_long()
        display.uploading = True
        self.assertEqual(self.call("POST", "/api/ota", image), (409, fixture("app_web_busy.json")))

    # ------------------------------------------------------------------------------------------------
    # The refusals

    def test_refusals(self):
        display = self.display
        answers = {word: "{\"error\":\"%s\"}" % word for word in ("not_found", "method", "host", "header", "query", "length", "too_large",
                                                                    "busy", "asking", "body", "upload")}
        self.release()

        for target in ("/api/nothing", "/api/info/", "/API/INFO", "/api", "//api/info", "/api/info%3F", "/index.html", ""):
            self.assertEqual(self.call("GET", target), (404, answers["not_found"]), target)
        for method, target in (("POST", "/api/info"), ("GET", "/api/reboot"), ("PUT", "/api/wifi"), ("POST", "/api/layout"),
                               ("OPTIONS", "/api/layout"), ("DELETE", "/api/wifi"), ("HEAD", "/"), ("POST", "/")):
            self.assertEqual(self.call(method, target, b"{}"), (405, answers["method"]), (method, target))

        for host in (None, "", "wican-display", "wican-display.local.example.org", "example.org", "192.168.1.256", "192.168.1",
                     "192.168.01.1", "192.168.1.77:", "192.168.1.77:123456", " 192.168.1.77", "[::1]", "localhost"):
            self.assertEqual(self.call("GET", "/api/info", host=host), (403, answers["host"]), host)
        for host in ("wican-display.local", "WiCAN-Display.LOCAL:80", "10.0.0.1:8080", "0.0.0.0", "255.255.255.255:1"):
            self.assertEqual(self.call("GET", "/api/info", host=host)[0], 200, host)

        for header in (False, "2", "true", "11"):
            self.assertEqual(self.call("POST", "/api/reboot", header=header), (403, answers["header"]), header)
        self.assertEqual(self.call("PUT", "/api/layout?mode=check", b"{}", header=False), (403, answers["header"]))

        for target in ("/api/layout", "/api/layout?", "/api/layout?mode=", "/api/layout?mode=Check", "/api/layout?mode=check&mode=check",
                       "/api/layout?mode=check&x=1", "/api/layout?x=1&mode=check", "/api/layout?" + "m" * 64):
            self.assertEqual(self.call("PUT", target, b"{}"), (400, answers["query"]), target)
        for target in ("/api/ticket", "/api/ticket?id=", "/api/ticket?id=0", "/api/ticket?id=-1", "/api/ticket?id=1x",
                       "/api/ticket?id=4294967296", "/api/ticket?id=00000000001", "/api/ticket?id=1&id=1", "/api/ticket?ID=1",
                       "/api/info?x", "/api/wifi?id=1", "/?x=1"):
            self.assertEqual(self.call("GET", target), (400, answers["query"]), target)
        self.assertEqual(self.call("POST", "/api/reboot?now", b""), (400, answers["query"]))
        self.assertEqual(self.call("GET", "/api/ticket?id=4294967295")[0], 200)
        self.assertEqual(self.call("GET", "/api/info?")[0], 200)

        for method, target in (("PUT", "/api/layout?mode=check"), ("POST", "/api/layout/reset"), ("POST", "/api/reboot"), ("POST", "/api/ota")):
            self.assertEqual(self.call(method, target, length=False), (411, answers["length"]), target)
            self.assertEqual(self.call(method, target, length="12 "), (411, answers["length"]), target)

        self.assertEqual(self.call("PUT", "/api/layout?mode=check", b" " * 16385), (413, answers["too_large"]))
        self.assertEqual(self.call("PUT", "/api/layout?mode=check", b" " * 16384)[0], 400)
        for target in ("/api/layout/reset", "/api/wifi", "/api/wifi/forget", "/api/settings", "/api/reboot", "/api/reset"):
            self.assertEqual(self.call("POST", target, b" " * 513), (413, answers["too_large"]), target)
        self.assertEqual(self.call("POST", "/api/settings", b"{}" + b" " * 510)[0], 200)

        # Busy: the display reads or clears the fault memory, shows the clear dialog, or receives a firmware
        for busy in ("reading", "uploading"):
            setattr(display, busy, True)
            for target in ("/api/reboot", "/api/reset", "/api/ota"):
                self.assertEqual(self.call("POST", target, b"x"), (409, answers["busy"]), (busy, target))
            setattr(display, busy, False)
        # A question to the knob while a firmware is received; a read does not keep the WiFi question away
        display.uploading = True
        self.assertEqual(self.call("POST", "/api/wifi", b"{\"ssid\":\"N\"}"), (409, answers["busy"]))
        display.uploading = False
        display.reading = True
        self.assertEqual(self.call("POST", "/api/wifi", b"{\"ssid\":\"N\"}")[0], 202)
        display.reading = False

        # A question to the knob, or the begin of an upload, while a question still waits
        for target, body in (("/api/wifi", b"{\"ssid\":\"N\"}"), ("/api/reset", b""), ("/api/ota", mock_display.firmware_image(size=200))):
            self.assertEqual(self.call("POST", target, body), (409, answers["asking"]), target)
        # What needs no knob goes on, and so does the restart: the question is lost with it
        self.assertEqual(self.call("POST", "/api/settings", b"{}")[0], 200)
        display.press_long()

        self.assertEqual(self.call("POST", "/api/wifi", b"{}"), (400, answers["body"]))
        self.assertEqual(self.call("POST", "/api/wifi/forget", b"{\"ssid\":\"Nirgends\"}"), (404, answers["not_found"]))
        self.assertEqual(self.call("POST", "/api/ota", b"x" * 200), (422, "{\"error\":\"no_image\"}"))

        # An answer that has no room in the 20480 bytes of the display; views that have no text
        display.catalog = [["\x01" * 32, "\x02" * 11, "\x03" * 23, True, True]] * 96
        self.assertEqual(self.call("GET", "/api/catalog"), (500, answers["too_large"]))
        display.layout = b""
        self.assertEqual(self.call("GET", "/api/layout"), (500, answers["too_large"]))
        image = mock_display.firmware_image(size=9000)
        self.assertEqual(self.call("POST", "/api/ota", image, read=reader(image[:8999])), (500, answers["upload"]))

        self.assertEqual(self.call("POST", "/api/reboot", b""), (200, "{\"ok\":true}"))
        self.assertIsNone(self.call("GET", "/api/info"))

    def test_refusals_in_the_documented_order(self):
        display = self.display
        image = mock_display.firmware_image(size=200)

        def status(method, target, body=b"{}", **how):
            answer = self.call(method, target, body, **how)
            return "%d %s" % (answer[0], re.search(r"\"error\":\"([a-z_]+)\"", answer[1]).group(1))

        # The release is closed, the display is busy and nothing is right about the request: one reason
        # after the other is taken away, and the next one of the table is the answer
        display.reading = True
        self.assertEqual(status("POST", "/api/nothing", host="example.org", header=False, length=False), "404 not_found")
        self.assertEqual(status("GET", "/api/reset?x=1", host="example.org"), "405 method")
        self.assertEqual(status("POST", "/api/reset?x=1", host="example.org", header=False, length=False), "403 host")
        self.assertEqual(status("POST", "/api/reset?x=1", header=False, length=False), "403 header")
        self.assertEqual(status("POST", "/api/reset?x=1", length=False), "403 locked")
        self.assertEqual(status("PUT", "/api/layout?mode=nothing", length=False), "403 locked")
        self.release()
        self.assertEqual(status("POST", "/api/reset?x=1", length=False), "400 query")
        self.assertEqual(status("PUT", "/api/layout?mode=nothing", length=False), "400 query")
        self.assertEqual(status("POST", "/api/reset", length=False), "411 length")
        self.assertEqual(status("POST", "/api/reset", b" " * 513), "413 too_large")
        self.assertEqual(status("POST", "/api/ota", b""), "413 too_large")
        self.assertEqual(status("POST", "/api/reset"), "409 busy")
        self.assertEqual(status("POST", "/api/ota", b"no firmware"), "409 busy")
        display.reading = False

        # Answered by the request itself: locked, busy, asking, the rest
        self.assertEqual(self.ask_reset()[0], 202)
        display.uploading = True
        self.assertEqual(status("POST", "/api/wifi", b"no network"), "409 busy")
        display.uploading = False
        self.assertEqual(status("POST", "/api/wifi", b"no network"), "409 asking")
        self.assertEqual(status("POST", "/api/ota", b"no firmware"), "409 asking")
        display.press_long()
        self.assertEqual(status("POST", "/api/wifi", b"no network"), "400 body")
        self.assertEqual(status("POST", "/api/ota", b"no firmware"), "422 too_short")
        large = mock_display.firmware_image(size=9000)
        self.assertEqual(status("POST", "/api/ota", large, read=reader(large[:8999])), "500 upload")
        self.assertEqual(status("POST", "/api/wifi/forget", b"no network"), "400 body")
        self.assertEqual(status("POST", "/api/wifi/forget", b"{\"ssid\":\"Nirgends\"}"), "404 not_found")
        # The begin of an upload: busy (a firmware that waits to be confirmed) before asking
        display.update_pending = True
        self.assertEqual(self.ask_reset()[0], 202)
        self.assertEqual(status("POST", "/api/ota", image), "409 busy")
        display.update_pending = False
        self.assertEqual(status("POST", "/api/ota", image), "409 asking")
        # and locked before asking: in the last five minutes of a release no upload begins
        display.press_long()
        self.release()
        for _ in range(2):
            self.clock.advance(590)
            self.assertEqual(self.call("POST", "/api/settings", b"{}")[0], 200)
        self.clock.advance(321)
        self.assertEqual(self.ask_reset()[0], 202)
        self.assertEqual(status("POST", "/api/ota", image), "403 locked")

    # ------------------------------------------------------------------------------------------------
    # The server

    def test_over_http(self):
        server = mock_display.Server(self.display).start()
        self.addCleanup(server.close)
        self.assertEqual(server.host, "127.0.0.1")

        def ask(method, target, headers, body=None):
            connection = http.client.HTTPConnection(server.host, server.port, timeout=5)
            self.addCleanup(connection.close)
            connection.putrequest(method, target, skip_host=True, skip_accept_encoding=True)
            for name, value in headers.items():
                connection.putheader(name, value)
            connection.endheaders(body)
            answer = connection.getresponse()
            return answer.status, dict((name.lower(), value) for name, value in answer.getheaders()), answer.read()

        own = {"Host": "%s:%d" % (server.host, server.port)}
        change = dict(own, **{"X-Display": "1"})
        with open(PAGE, "rb") as file:
            page = file.read()
        answers = [
            ask("GET", "/", own),
            ask("GET", "/api/info", own),
            ask("GET", "/api/nothing", own),
            # What a browser asks before a page of another origin may send the header: never answered with a yes
            ask("OPTIONS", "/api/layout", dict(own, Origin="http://example.org", **{"Access-Control-Request-Method": "PUT"})),
            ask("POST", "/api/settings", dict(change, **{"Content-Length": "2"}), b"{}"),
            ask("GET", "/api/info", {"Host": "localhost:%d" % server.port}),
            ask("GET", "/mock", own),
            ask("POST", "/mock/release?on", own),
            ask("POST", "/mock/release?on", change),
            ask("POST", "/api/reboot", change),
            ask("POST", "/api/ota", dict(change, **{"Content-Length": "5000"}), mock_display.firmware_image(size=5000)),
            ask("GET", "/mock/firmware?wican", own),
        ]
        self.assertEqual([status for status, _, _ in answers], [200, 200, 404, 405, 403, 403, 200, 403, 200, 411, 202, 200])
        self.assertEqual((answers[0][1]["content-type"], answers[0][2]), ("text/html; charset=utf-8", page))
        self.assertEqual(answers[1][1]["content-type"], "application/json")
        self.assertEqual(answers[4][2], fixture("app_web_locked.json").encode("utf-8"))
        self.assertEqual(answers[5][2], b"{\"error\":\"host\"}")
        self.assertEqual(mock_display.ota_check(answers[11][2][:112], 1, 1), ("wrong_project", "4.21"))
        # The protection by the header rests on this: no answer ever allows another origin anything
        for _, headers, _ in answers:
            self.assertEqual([name for name in headers if name.startswith("access-control")], [])

    def test_the_strip_is_no_part_of_the_display(self):
        display = self.display
        own = {"host": HOST}
        self.assertEqual(display.control("GET", "/mock", own)[0], 200)
        self.assertEqual(display.control("GET", "/mock", {"host": "example.org"})[0], 403)
        self.assertEqual(display.control("POST", "/mock/release?on", own)[0], 403)
        own["x-display"] = "1"
        self.assertEqual(display.control("POST", "/mock/release?on", own)[0], 200)
        self.assertEqual(self.call("POST", "/api/reset")[0], 202)
        self.assertIn("\"question\": \"Werkseinstellungen?\"", display.control("GET", "/mock/state", own)[2].decode("utf-8"))
        display.control("POST", "/mock/skip?2", own)
        self.assertIn("\"down\": true", display.control("POST", "/mock/press", own)[2].decode("utf-8"))
        for target in ("/mock/skip?-5", "/mock/skip?x", "/mock/lists?all"):
            self.assertEqual(display.control("POST", target, own)[0], 400, target)
        self.assertEqual(display.control("POST", "/mock/nothing", own)[0], 404)
        # The display itself knows none of it
        self.clock.advance(5)
        self.assertEqual(self.call("GET", "/mock/state")[0], 404)


# ----------------------------------------------------------------------------------------------------
# The checks fail when they have to

def swapped(text, old, new):
    """The text with one thing changed - and an error if the thing is not there to be changed"""
    if old not in text:
        raise AssertionError("the page has no %r to break" % old)
    return text.replace(old, new, 1)


# For each check of the page: what is broken in a copy of its text - a piece of it and what stands there
# instead, or a function that makes the broken text
PAGE_MUTATIONS = {
    "request_problems": {
        "a path API.md does not name": ("api(\"GET\", \"/api/values\")", "api(\"GET\", \"/api/value\")"),
        "a method API.md does not name for the path": ("api(\"POST\", \"/api/reboot\")", "api(\"GET\", \"/api/reboot\")"),
        "a path that is put together": ("api(\"GET\", \"/api/wifi\")", "api(\"GET\", \"/api/\" + S.tab)"),
        "a path outside of api()": ("const SETTINGS = ", "const ELSEWHERE = \"/api/elsewhere\";\nconst SETTINGS = "),
        "a request left out": ("api(\"POST\", \"/api/layout/reset\")", "api(\"POST\", \"/api/reboot\")"),
        "the header only for POST": ("if(method !== \"GET\") xhr.setRequestHeader", "if(method === \"POST\") xhr.setRequestHeader"),
        "the header left out": ("\t\tif(method !== \"GET\") xhr.setRequestHeader(\"X-Display\", \"1\");\n", ""),
        "the header with another value": ("setRequestHeader(\"X-Display\", \"1\")", "setRequestHeader(\"X-Display\", \"0\")"),
        "a second way to send": ("poll();\n</script>", "fetch(\"/api/reboot\", {method: \"POST\"});\npoll();\n</script>"),
        "a second request object": ("poll();\n</script>", "new XMLHttpRequest().open(\"POST\", \"/api/reboot\");\npoll();\n</script>"),
        "a form": ("<main>", "<main><form method=\"post\"><button>x</button></form>"),
        "the mock": ("<main>", "<main><p>/mock</p>"),
    },
    "construct_problems": {
        "markup from a text": ("$(id).textContent = message || \"\";", "$(id).innerHTML = message || \"\";"),
        "markup next to a field": ("field.parentNode.insertBefore(",
                                   "field.insertAdjacentHTML(\"afterend\", message); field.parentNode.insertBefore("),
        "a written document": ("poll();\n</script>", "document.write(S.view);\npoll();\n</script>"),
        "evaluated text": ("data = JSON.parse(xhr.responseText);", "data = eval(\"(\" + xhr.responseText + \")\");"),
        "a function made of text": ("data = JSON.parse(xhr.responseText);", "data = new Function(\"return \" + xhr.responseText)();"),
        "a timer with text": ("setTimeout(poll, 3000);", "setTimeout(\"poll()\", 3000);"),
        "a handler as an attribute": ("<button id=\"reload\">", "<button id=\"reload\" onclick=\"location.reload()\">"),
        "a handler set as an attribute": ("node.addEventListener(\"click\", click);", "node.setAttribute(\"onclick\", click);"),
        "a handler as a property": ("node.addEventListener(\"click\", click);", "node.onclick = click;"),
        "a script address": ("<main>", "<main><p title=\"javascript:alert(1)\"></p>"),
        "a text the reader of the script does not see": ("poll();\n</script>", "api('GET', '/api/elsewhere');\npoll();\n</script>"),
        "a second script": ("</body>", "<script>S.view = 1;</script></body>"),
    },
    "external_problems": {
        "a style sheet": ("<title>", "<link rel=\"stylesheet\" href=\"style.css\"><title>"),
        "a script from elsewhere": ("<title>", "<script src=\"https://example.org/x.js\"></script><title>"),
        "a picture": ("<main>", "<main><img src=\"logo.png\" alt=\"\">"),
        "a font": ("*{box-sizing:border-box}", "@font-face{font-family:x;src:url(x.woff2)}*{box-sizing:border-box}"),
        "an address in a text": ("const SETTINGS = ", "const HELP = \"http://example.org\";\nconst SETTINGS = "),
        "an address without a scheme": ("const SETTINGS = ", "const HELP = \"//example.org/x\";\nconst SETTINGS = "),
        "a link that leads away": ("href: URL.createObjectURL(new Blob([json], {type: \"application/json\"}))", "href: S.view"),
    },
    "size_problems": {
        "one byte too many": lambda page: page + " " * (SIZE_MAX - len(page.encode("utf-8"))),
    },
}


class WrongRoute(mock_display.Display):
    """Looks at the header before the host"""

    def route(self, method, path, query, headers, now):
        if method != "GET" and headers.get("x-display") != "1" and any(entry[:2] == (method, path) for entry in mock_display.ROUTES):
            return None, (403, "header")
        return super().route(method, path, query, headers, now)


class WrongStage(mock_display.Display):
    """Looks at the question before the busy display"""

    def _ask_refused(self, busy, now):
        if self.access.is_open(now) and self.access.waiting(now) is not None:
            return 409, mock_display.error_body("asking")
        return super()._ask_refused(busy, now)


class NoHostRule(mock_display.Display):
    def route(self, method, path, query, headers, now):
        return super().route(method, path, query, dict(headers, host="192.168.1.77"), now)


class WrongInfo(mock_display.Display):
    def info_json(self, now):
        return super().info_json(now).replace("\"up\":", "\"uptime\":")


class WrongCatalog(mock_display.Display):
    def catalog_json(self):
        return super().catalog_json().replace("\"delivered\":true", "\"delivered\":false")


class ReportsNothingUnknown(mock_display.Display):
    def report_json(self, layout):
        return re.sub(r"\"unknown\":\[.*\]", "\"unknown\":[]", super().report_json(layout))


class WrongLists(mock_display.Display):
    def dtc_last_json(self, now):
        return super().dtc_last_json(now).replace("\"read_age_s\":95", "\"read_age_s\":96")


class TellsPasswords(mock_display.Display):
    def wifi_json(self):
        return super().wifi_json().replace("\"password\":false", "\"password\":true")


class StoresAtOnce(mock_display.Display):
    """Stores a network without the knob"""

    def _ask(self, question, detail, now):
        if question == "wifi":
            self._store_network(self.asked_network)
        return super()._ask(question, detail, now)


class TakesHalfTheSettings(mock_display.Display):
    def _settings(self, data):
        status, body = super()._settings(data)
        if status == 400:
            self.settings["night"] = 50
        return status, body


class KeepsThePreview(mock_display.Display):
    def restart(self, reason="sw"):
        layout, name, source = self.layout, self.layout_name, self.source
        super().restart(reason)
        self.layout, self.layout_name, self.source = layout, name, source


class NoRollback(mock_display.Display):
    def _settle(self):
        self.access.waiting(self.now())


class AsksForBrokenUploads(mock_display.Display):
    def _upload(self, read, received, file_size):
        status, body = super()._upload(read, received, file_size)
        return (status, body) if status != 500 else self._ask("firmware", self.upload_version, self.now())


class AnswersTheStrip(mock_display.Display):
    def control(self, method, target, headers):
        return super().control(method, target, dict(headers, **{"x-display": "1"}))


class AllowsOrigins(mock_display.Handler):
    def _answer(self):
        self.wfile = Tapped(self.wfile)
        super()._answer()

    do_GET = do_POST = do_PUT = do_OPTIONS = _answer


class Tapped:
    """A stream whose answers allow every origin"""

    def __init__(self, stream):
        self.stream = stream

    def write(self, data):
        return self.stream.write(data.replace(b"\r\nContent-Type:", b"\r\nAccess-Control-Allow-Origin: *\r\nContent-Type:", 1))

    def __getattr__(self, name):
        return getattr(self.stream, name)


def taking_every_layout(data):
    """check_layout() that refuses nothing"""
    try:
        return CHECK_LAYOUT(data)
    except mock_display.Refused:
        return {"name": "", "pages": [["K"]], "warnings": []}


CHECK_LAYOUT = mock_display.check_layout

# For each test of the mock: a display that is wrong in one place (a class), or a rule of the mock that
# is another (the name of something in mock_display, and what it is replaced by)
API_MUTATIONS = {
    "test_info": WrongInfo,
    "test_catalog": WrongCatalog,
    "test_values": ("VALUE_FRESH_MS", 2999),
    "test_values_of_the_mock_move_and_age": ("VALUE_KEPT_MS", 9999),
    "test_layout_reports": ReportsNothingUnknown,
    "test_layout_problems": ("check_layout", taking_every_layout),
    "test_layout_apply_save_reset": KeepsThePreview,
    "test_dtc_last": WrongLists,
    "test_wifi": TellsPasswords,
    "test_wifi_store_and_forget": StoresAtOnce,
    "test_settings": TakesHalfTheSettings,
    "test_release": ("OPEN_MAX_MS", 1801 * 1000),
    "test_tickets": ("CONFIRM_MS", 61 * 1000),
    "test_ticket_ends": ("ASK_SHOWN_MS", 1499),
    "test_ota": NoRollback,
    "test_ota_refused_files": ("OTA_PROJECT", "wican-fw"),
    "test_ota_that_breaks_or_comes_late": ("UPLOAD_LEFT_S", 299),
    "test_refusals": NoHostRule,
    "test_refusals_in_the_documented_order": WrongRoute,
    "test_over_http": ("Handler", AllowsOrigins),
    "test_the_strip_is_no_part_of_the_display": AnswersTheStrip,
}
# More than one thing a test guards is broken for it
API_MUTATIONS_MORE = {
    "test_refusals_in_the_documented_order": [WrongStage],
    "test_refusals": [("BODY_SMALL_MAX", 513), ("OUT_SIZE", 60000), AsksForBrokenUploads],
    "test_release": [("OPEN_MS", 601 * 1000)],
    "test_ota_that_breaks_or_comes_late": [AsksForBrokenUploads],
}


def failures(name, mutation):
    """Runs one test of Api against a mock that is wrong; returns what the test found"""
    class Broken(Api):
        pass

    result = unittest.TestResult()
    if isinstance(mutation, tuple):
        with mock.patch.object(mock_display, mutation[0], mutation[1]):
            Broken(name).run(result)
    else:
        Broken.display_class = mutation
        Broken(name).run(result)
    return result.failures + result.errors


class CounterCheck(unittest.TestCase):
    def test_every_check_of_the_page_is_broken_once(self):
        checks = {"request_problems", "construct_problems", "external_problems", "size_problems"}
        self.assertEqual(set(PAGE_MUTATIONS), checks)
        self.assertEqual({name for name in globals() if name.endswith("_problems")}, checks)

    def test_a_broken_page_is_found(self):
        page = file_text(PAGE)
        api_text = file_text(API)
        for check, mutations in PAGE_MUTATIONS.items():
            for what, mutate in mutations.items():
                with self.subTest(check=check, broken=what):
                    broken = mutate(page) if callable(mutate) else swapped(page, *mutate)
                    found = {"request_problems": lambda: request_problems(broken, api_text),
                             "construct_problems": lambda: construct_problems(broken),
                             "external_problems": lambda: external_problems(broken),
                             "size_problems": lambda: size_problems(broken.encode("utf-8"))}[check]()
                    self.assertNotEqual(found, [])

    def test_a_request_api_md_no_longer_names_is_found(self):
        page = file_text(PAGE)
        api_text = file_text(API)
        self.assertNotEqual(request_problems(page, swapped(api_text, "| `POST /api/reboot` |", "| `POST /api/restart` |")), [])
        self.assertNotEqual(request_problems(page, swapped(api_text, "| `GET /api/ticket?id=N` |", "| `GET /api/ticket?n=N` |")), [])

    def test_every_test_of_the_mock_is_broken_once(self):
        tests = {name for name in dir(Api) if name.startswith("test_")}
        self.assertEqual(set(API_MUTATIONS), tests)
        self.assertLessEqual(set(API_MUTATIONS_MORE), tests)

    def test_a_wrong_mock_is_found(self):
        for name, first in API_MUTATIONS.items():
            for mutation in [first] + API_MUTATIONS_MORE.get(name, []):
                label = mutation.__name__ if isinstance(mutation, type) else "%s = %r" % mutation
                with self.subTest(test=name, wrong=label):
                    self.assertNotEqual(failures(name, mutation), [])


if __name__ == "__main__":
    unittest.main()
