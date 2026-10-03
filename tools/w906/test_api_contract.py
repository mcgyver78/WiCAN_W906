"""Contract test of the HTTP API for standalone clients, tools/w906/API.md.

  Contract       every statement of API.md a client can observe, one test method each, against
                 mock_wican.py in this process on 127.0.0.1 with a simulated clock
  RedProof       the tests notice a broken contract: with each fault switch of the mock exactly the
                 tests listed here fail, and the tests no fault switch reaches fail after a change
                 of the mock source or of a fixture. The tests of AdapterMode fail after a change of
                 the lock in this file. A test nobody has seen failing makes the run fail.
  Fixtures       the answers of the mock are, byte for byte, the hand-written files in fixtures/,
                 and the files follow the sources they were written from
  Scenarios      what the mock does beyond API.md
  Rules          the rules of the mock against the firmware module main/dtc_state.c: 300
                 pseudo-random call sequences give the same state after every call (needs cc,
                 builds dtc_state_cli)
  AdapterMode    what the contract test sends when it is pointed at a real adapter, and that
                 nothing else leaves this process
  CommandLine    mock_wican.py started as a program

Against a real adapter, with the ignition on and the engine off:

  WICAN_HOST=192.168.80.1 python3 -m unittest -v test_api_contract.Contract

Then only GET requests to /api/state, /api/dtc/result, /autopid_data and /load_car_config are sent
(and to one path that does not exist). Tests that need more are skipped and say what they need:

  WICAN_ALLOW_DTC=1     requests that can start a scan: POST /api/dtc with action=read, also the
                        ones that have to be refused. About 25 scans of 35 s each, the polling
                        pauses meanwhile.
  WICAN_ALLOW_CLEAR=1   as well: requests with action=clear. THE FAULT MEMORY OF THE VEHICLE IS
                        CLEARED if the last read found trouble codes.
  WICAN_SPEED=10        only for mock_wican.py --speed 10 as the adapter: its clock runs faster.
                        WICAN_HOST is then 127.0.0.1:8906, not localhost: a POST with a host name
                        that is not the one of an adapter is forbidden.

Tests that need a situation only the mock can be put into (ignition off, restart, ...) are skipped.
"""
import collections
import contextlib
import gc
import http.client
import io
import json
import os
import pathlib
import re
import select
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import types
import unittest
import warnings

import mock_wican

THIS = pathlib.Path(__file__).resolve()
HERE = THIS.parent
REPO = HERE.parents[1]

# API.md, GET /api/state: "The fields come in exactly this order"
STATE_FIELDS = ["api", "id", "fw", "git", "boot", "up", "autopid", "pids", "ecu", "pass", "rx_age_ms", "mqtt",
                "batt_v", "sleep_in_s", "heap", "heap_min", "dtc"]
DTC_FIELDS = ["supported", "state", "action", "src", "seq", "ecu", "total", "name", "reason", "age_s", "count",
              "result_seq"]
ERROR_REASONS = ("ecu_offline", "engine_running", "engine_state_unknown", "not_supported", "out_of_memory",
                 "result_serialize_failed", "expired", "internal")
IDLE = {"supported": True, "state": "idle", "action": "", "src": "", "seq": 0, "ecu": 0, "total": 0, "name": "",
        "reason": "", "age_s": 0, "count": 0, "result_seq": 0}

# dtc_ecus[] in main/autopid.c: name, request id, protocol
UNITS = [
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
]

# What a real adapter is asked without further permission, and what it is never asked (API.md, last line)
READ_ONLY = ("/api/state", "/api/dtc/result", "/autopid_data", "/load_car_config", "/api/nothing")
NEVER = ("/check_status", "/load_config", "/scan_available_pids")

DTC_HEADER = (("X-WiCAN-DTC", "1"),)
FORBIDDEN = (403, '{"accepted":false,"reason":"forbidden","seq":0}')
BAD_REQUEST = (400, '{"accepted":false,"reason":"bad_request","seq":0}')
NOT_READY = (503, '{"accepted":false,"reason":"not_ready","seq":0}')


# Fixture files a test of RedProof lets the other tests read differently
ALTERED = {}


def fixture(name):
    if name in ALTERED:
        return ALTERED[name]
    return (HERE / "fixtures" / name).read_text(encoding="utf-8").rstrip("\r\n")


def compact(value):
    """JSON without whitespace, UTF-8 not escaped: the form API.md promises."""
    return json.dumps(value, separators=(",", ":"), ensure_ascii=False)


def accepted(seq):
    return 202, '{"accepted":true,"seq":%d}' % seq


def refused(reason, seq):
    return 409, '{"accepted":false,"reason":"%s","seq":%d}' % (reason, seq)


def permissions(environ):
    """What the environment asks for: the adapter to test instead of the mock (None: the mock), whether
    scans and whether clears may be sent to it, how many times faster than real time its clock runs."""
    return (environ.get("WICAN_HOST") or None, environ.get("WICAN_ALLOW_DTC") == "1",
            environ.get("WICAN_ALLOW_CLEAR") == "1", float(environ.get("WICAN_SPEED") or 1))


def not_permitted(method, target, allow_dtc, allow_clear):
    """Why a request must not be sent to a real adapter, None if it may be sent."""
    path = target.partition("?")[0]
    if path in NEVER:
        return "API.md: a client never calls %s" % path
    if method == "GET" and path in READ_ONLY:
        return None
    if not allow_dtc:
        return "set WICAN_ALLOW_DTC=1 to send requests that can start a scan (%s %s)" % (method, path)
    if "clear" in target and not allow_clear:
        return "set WICAN_ALLOW_CLEAR=1 as well to send requests that can clear the fault memory of the vehicle"
    return None


class Answer:
    def __init__(self, status, headers, body):
        self.status = status
        self.headers = headers
        self.body = body

    @property
    def text(self):
        # Strict: an answer that is not UTF-8 is an error
        return self.body.decode("utf-8")

    def json(self):
        return json.loads(self.text)

    def header(self, name):
        values = [value for key, value in self.headers if key.lower() == name.lower()]
        return values[0] if values else None

    def brief(self):
        return self.status, self.text


def check_result(test, text, action):
    """A result as W906.md, "Fehlerspeicher", describes it. Returns the number of trouble codes."""
    result = json.loads(text)
    test.assertEqual(compact(result), text, "the result has whitespace or escaped UTF-8")
    test.assertLessEqual(len(text.encode("utf-8")), 5119, "the result does not fit into an MQTT message")
    test.assertEqual(list(result), ["state", "action", "duration_ms", "dtc_count", "ecus"])
    test.assertEqual((result["state"], result["action"]), ("done", action))
    test.assertIs(type(result["duration_ms"]), int)
    test.assertIs(type(result["dtc_count"]), int)
    test.assertEqual([(ecu["name"], ecu["id"], ecu["protocol"]) for ecu in result["ecus"]], UNITS)

    codes = 0
    for ecu in result["ecus"]:
        keys = ["name", "id", "protocol"] + ["cleared"] * ("cleared" in ecu) + ["status", "dtcs"] \
               + ["dtcs_omitted"] * ("dtcs_omitted" in ecu)
        test.assertEqual(list(ecu), keys, ecu["name"])
        test.assertRegex(ecu["status"], r"^(ok|no_response|pending_timeout|incomplete|nrc_[0-9A-F]{2})$")
        if "cleared" in ecu:
            test.assertEqual(action, "clear", "cleared in the result of a read")
            test.assertIs(type(ecu["cleared"]), bool)
        if "dtcs_omitted" in ecu:
            test.assertIs(type(ecu["dtcs_omitted"]), int)
            test.assertGreater(ecu["dtcs_omitted"], 0)
            test.assertEqual(ecu["dtcs"], [], "codes listed although they are counted as omitted")
            codes += ecu["dtcs_omitted"]
        if ecu["status"] != "ok":
            test.assertEqual(ecu["dtcs"], [], "codes of a control unit that was not read completely")
        for dtc in ecu["dtcs"]:
            test.assertRegex(dtc["status"], r"^[0-9A-F]{2}$")
            if ecu["protocol"] == "UDS":
                test.assertEqual(list(dtc), ["code", "status", "active"])
                test.assertRegex(dtc["code"], r"^[PCBU][0-3][0-9A-F]{3}-[0-9A-F]{2}$")
                test.assertIs(dtc["active"], bool(int(dtc["status"], 16) & 1))
            else:
                test.assertEqual(list(dtc), ["code", "status"])
                test.assertRegex(dtc["code"], r"^[0-9A-F]{4}$")
        codes += len(ecu["dtcs"])
    test.assertEqual(result["dtc_count"], codes)
    return codes


class Contract(unittest.TestCase):
    """API.md, statement by statement. The comments name the place in API.md."""

    # The module under test and its fault switches; RedProof replaces both
    MOCK = mock_wican
    FAULTS = ()
    # A real adapter instead of the mock, and what may be sent to it
    ADAPTER, ALLOW_DTC, ALLOW_CLEAR, SPEED = permissions(os.environ)

    @classmethod
    def setUpClass(cls):
        cls.server = None
        if cls.ADAPTER is None:
            cls.server = cls.MOCK.Server().start()
            cls.address = ("127.0.0.1", cls.server.port)
        else:
            host, _, port = cls.ADAPTER.partition(":")
            cls.address = (host, int(port or 80))
        # One connection for all tests, as API.md asks of a client. One per test leaves 1800 local ports
        # waiting for 30 s with every run of this file; a few runs in a row then use up the ports of a Mac.
        cls.connection = http.client.HTTPConnection(cls.address[0], cls.address[1], timeout=10)

    @classmethod
    def tearDownClass(cls):
        cls.connection.close()
        if cls.server is not None:
            cls.server.close()

    def setUp(self):
        self.mock = None

    # Helpers --------------------------------------------------------------------------------

    def given(self, scenario=None, needs=None, **settings):
        """The situation a test starts from. Without scenario and settings it is the adapter as it
        is; anything else only the mock can do. needs: "read" or "clear", what the test will send."""
        if self.ADAPTER is None:
            # Fixed numbers: what a test sees must not depend on chance
            settings.setdefault("seq_seed", 100)
            self.mock = self.MOCK.Adapter(scenario or "codes", self.FAULTS, **settings)
            self.server.adapter = self.mock
            return
        if scenario is not None or settings:
            self.only_mock()
        if needs is not None:
            refusal = not_permitted("POST", "/api/dtc?action=%s" % needs, self.ALLOW_DTC, self.ALLOW_CLEAR)
            if refusal is not None:
                self.skipTest(refusal)
            # The scan of the test before may still be running
            for _ in range(300):
                if self.dtc()["state"] not in ("queued", "running"):
                    return
                self.wait(0.5)
            self.fail("the adapter stays busy")

    def only_mock(self):
        """For a test that goes through several situations: skipped as a whole, not situation by situation."""
        if self.ADAPTER is not None:
            self.skipTest("needs a situation only the mock can be put into")

    def exact(self):
        """True against the mock: its clock is simulated, times can be compared exactly."""
        return self.ADAPTER is None

    def slack(self):
        """Seconds a real adapter may be late: the requests take time, and a mock that runs faster
        than real time turns a short delay of this process into many of its seconds."""
        return 0 if self.exact() else 2 + 0.25 * self.SPEED

    def wait(self, seconds):
        if self.exact():
            self.mock.clock.advance(seconds)
        else:
            time.sleep(seconds / self.SPEED)

    def reopen_if_closed(self, connection):
        """A connection the other side has ended is not used again. An assumption, not measured: the
        HTTP server of the firmware ends it after an answer of its own like 404 or 405, without saying
        so. Nothing is repeated, the next request simply goes over a new connection."""
        # Readable although nothing was asked
        if connection.sock is not None and select.select([connection.sock], [], [], 0)[0]:
            connection.close()

    def request(self, method, target, headers=(), host=True, body=b""):
        """host: True for the address the request goes to, None for no Host header, else its text."""
        if self.ADAPTER is not None:
            refusal = not_permitted(method, target, self.ALLOW_DTC, self.ALLOW_CLEAR)
            if refusal is not None:
                self.skipTest(refusal)

        connection = self.connection
        self.reopen_if_closed(connection)
        try:
            connection.putrequest(method, target, skip_host=host is not True, skip_accept_encoding=True)
            if isinstance(host, str):
                connection.putheader("Host", host)
            for name, value in headers:
                connection.putheader(name, value)
            if method == "POST":
                connection.putheader("Content-Length", "%d" % len(body))
            connection.endheaders(body or None)
            response = connection.getresponse()
            return Answer(response.status, response.getheaders(), response.read())
        except (http.client.HTTPException, OSError) as error:
            # Not an error of this test: the adapter did not answer the request with HTTP.
            # The next request gets a new connection.
            connection.close()
            self.fail("%s %s: %r" % (method, target, error))

    def get(self, target, headers=()):
        return self.request("GET", target, headers)

    def post(self, target, headers=DTC_HEADER, host=True):
        return self.request("POST", target, headers, host)

    def state(self):
        answer = self.get("/api/state")
        self.assertEqual(answer.status, 200)
        return answer.json()

    def dtc(self):
        return self.state()["dtc"]

    def dtc_text(self):
        """The dtc object as it was sent."""
        text = self.get("/api/state").text
        return text[text.index('"dtc":') + len('"dtc":'):-1]

    def read(self):
        """Starts a read and returns its number."""
        answer = self.post("/api/dtc?action=read")
        self.assertEqual(answer.status, 202, answer.text)
        return answer.json()["seq"]

    def follow(self, seq, step=0.5, as_sent=False):
        """Asks for the state every `step` seconds until request `seq` has ended. Returns the dtc
        objects seen, as_sent: their texts."""
        seen = []
        for _ in range(int(150 / step)):
            text = self.dtc_text()
            dtc = json.loads(text)
            seen.append(text if as_sent else dtc)
            self.assertEqual(dtc["seq"], seq, "another request took the place of the one that is followed")
            if dtc["state"] not in ("queued", "running"):
                return seen
            self.wait(step)
        self.fail("request %d does not end: %s" % (seq, seen[-1]))

    def finished(self, seq):
        """Waits for the end of request `seq` and returns the dtc object."""
        if self.exact():
            self.wait(45)
        dtc = self.follow(seq)[-1]
        self.assertIn(dtc["state"], ("done", "error"))
        return dtc

    def read_done(self):
        """A complete read. Returns its number and the dtc object after it."""
        seq = self.read()
        dtc = self.finished(seq)
        self.assertEqual((dtc["state"], dtc["reason"]), ("done", ""), "the read failed (ignition on?)")
        return seq, dtc

    def kept(self, dtc):
        """What of the dtc object must not change when a request is refused."""
        return [dtc[field] for field in ("state", "action", "src", "seq", "reason", "count", "result_seq")]

    # All answers ----------------------------------------------------------------------------

    def test_state_is_json_in_utf8_without_whitespace(self):
        # "All answers are JSON in UTF-8 without whitespace"
        self.given()
        answer = self.get("/api/state")
        self.assertEqual(compact(answer.json()), answer.text)

    def test_answers_to_post_are_json_without_whitespace(self):
        self.given(needs="read")
        for answer in (self.post("/api/dtc?action=read", headers=()), self.post("/api/dtc?action=nothing"),
                       self.post("/api/dtc?action=read"), self.post("/api/dtc?action=read")):
            with self.subTest(status=answer.status):
                self.assertEqual(compact(answer.json()), answer.text)

    def test_state_and_result_are_not_cached(self):
        # "with Cache-Control: no-store"
        self.given()
        for target in ("/api/state", "/api/dtc/result"):
            with self.subTest(target=target):
                self.assertEqual(self.get(target).header("Cache-Control"), "no-store")

    def test_stored_result_is_not_cached(self):
        self.given(needs="read")
        self.read_done()
        answer = self.get("/api/dtc/result")
        self.assertEqual((answer.status, answer.header("Cache-Control")), (200, "no-store"))

    def test_answers_to_post_are_not_cached(self):
        self.given(needs="read")
        answers = [self.post("/api/dtc?action=read", headers=()), self.post("/api/dtc?action=nothing"),
                   self.post("/api/dtc?action=read"), self.post("/api/dtc?action=read")]
        self.assertEqual([answer.status for answer in answers], [403, 400, 202, 409])
        for answer in answers:
            with self.subTest(status=answer.status):
                self.assertEqual(answer.header("Cache-Control"), "no-store")

    def test_not_ready_is_not_cached(self):
        self.given("starting")
        for answer in (self.post("/api/dtc?action=read"), self.get("/api/dtc/result")):
            self.assertEqual((answer.status, answer.header("Cache-Control")), (503, "no-store"))

    def test_no_cors_headers_are_sent(self):
        # "No CORS headers are sent"
        self.given()
        for target in ("/api/state", "/api/dtc/result"):
            answer = self.get(target, headers=(("Origin", "http://page.example"),))
            self.assertEqual([name for name, _ in answer.headers if name.lower().startswith("access-control-")], [])

    def test_answers_to_post_carry_no_cors_headers(self):
        # Refused, malformed, accepted, busy
        self.given(needs="read")
        origin = (("Origin", "http://page.example"),)
        for answer in (self.post("/api/dtc?action=read", headers=origin), self.post("/api/dtc?action=nothing", DTC_HEADER + origin),
                       self.post("/api/dtc?action=read", DTC_HEADER + origin), self.post("/api/dtc?action=read", DTC_HEADER + origin)):
            with self.subTest(status=answer.status):
                self.assertEqual([name for name, _ in answer.headers if name.lower().startswith("access-control-")], [])

    def test_answers_are_declared_as_json(self):
        # "All answers are JSON": the firmware says so in Content-Type (send_json() in main/dtc_http.c)
        self.given(needs="read")
        answers = [self.get("/api/state"), self.post("/api/dtc?action=nothing", headers=()),
                   self.post("/api/dtc?action=nothing"), self.post("/api/dtc?action=read")]
        self.finished(answers[-1].json()["seq"])
        for answer in answers + [self.get("/api/dtc/result")]:
            with self.subTest(status=answer.status):
                self.assertEqual(answer.header("Content-Type"), "application/json")

    def test_preflight_for_the_header_is_not_answered(self):
        # "A web page in a browser cannot send such a request to the adapter (no CORS answer for the custom header"
        self.given(needs="read")
        answer = self.request("OPTIONS", "/api/dtc?action=read",
                              (("Origin", "http://page.example"), ("Access-Control-Request-Method", "POST"),
                               ("Access-Control-Request-Headers", "x-wican-dtc")))
        self.assertEqual([name for name, _ in answer.headers if name.lower().startswith("access-control-")], [])
        self.assertNotIn(answer.status, (200, 204))

    def test_numbers_are_integers_below_2_31(self):
        # "All numbers are integers below 2^31, except batt_v"
        self.given()
        if self.exact():
            # 102 years: up would be above 2^31
            self.wait(1.5 * 2 ** 31)
        state = self.state()
        numbers = {name: value for name, value in list(state.items()) + list(state["dtc"].items())
                   if isinstance(value, (int, float)) and not isinstance(value, bool) and name != "batt_v"}
        self.assertEqual(sorted(numbers), sorted(["api", "boot", "up", "pids", "pass", "rx_age_ms", "sleep_in_s", "heap",
                                                  "heap_min", "seq", "ecu", "total", "age_s", "count", "result_seq"]))
        for name, value in numbers.items():
            with self.subTest(field=name):
                self.assertIs(type(value), int)
                self.assertLess(value, 2 ** 31)

    # GET /api/state -------------------------------------------------------------------------

    def test_state_is_answered_with_200(self):
        # "Always 200"
        self.given()
        self.assertEqual(self.get("/api/state").status, 200)

    def test_state_is_answered_with_200_in_every_situation(self):
        self.only_mock()
        for scenario in ("ignition_off", "engine_running", "starting", "autopid_off", "unsupported", "stalled"):
            with self.subTest(scenario=scenario):
                self.given(scenario)
                self.assertEqual(self.get("/api/state").status, 200)
                # Also while a request is refused, waits, runs or has failed
                self.post("/api/dtc?action=read")
                for _ in range(4):
                    self.assertEqual(self.get("/api/state").status, 200)
                    self.wait(15)

    def test_firmware_without_the_api_answers_404(self):
        # "A 404 means the firmware does not have this API"
        self.given("upstream")
        self.assertEqual([self.get("/api/state").status, self.get("/api/dtc/result").status,
                          self.post("/api/dtc?action=read").status], [404, 404, 404])

    def test_firmware_without_the_api_has_the_upstream_paths(self):
        self.given("upstream")
        self.wait(5)
        for target in ("/autopid_data", "/load_car_config"):
            answer = self.get(target)
            self.assertEqual(answer.status, 200)
            self.assertEqual(len(answer.json()), 35)

    def test_state_fields_come_in_the_documented_order(self):
        # "The fields come in exactly this order", and the table of dtc
        self.given()
        state = self.state()
        self.assertEqual(list(state), STATE_FIELDS)
        self.assertEqual(list(state["dtc"]), DTC_FIELDS)

    def test_api_is_1(self):
        self.given()
        self.assertEqual(repr(self.state()["api"]), "1")

    def test_id_fw_and_git_are_texts(self):
        self.given()
        state = self.state()
        for field in ("id", "fw", "git"):
            with self.subTest(field=field):
                self.assertIsInstance(state[field], str)
                self.assertNotEqual(state[field], "")

    def test_id_fw_and_git_are_those_of_the_device(self):
        self.given(id="0123456789ab", fw="9.87", git="w906-v9.8.7")
        state = self.state()
        self.assertEqual((state["id"], state["fw"], state["git"]), ("0123456789ab", "9.87", "w906-v9.8.7"))

    def test_text_fields_are_escaped_and_lose_control_characters(self):
        # "Text fields are escaped like JSON strings (" and \), control characters below 0x20 are dropped"
        self.given(git='a"b\\c\td\x1fe')
        text = self.get("/api/state").text
        self.assertIn('"git":"a\\"b\\\\cde",', text)
        self.assertEqual(json.loads(text)["git"], 'a"b\\cde')

    def test_boot_is_a_number_from_1_to_2_31_minus_1(self):
        self.given()
        boot = self.state()["boot"]
        self.assertTrue(1 <= boot <= 2 ** 31 - 1, boot)
        self.assertEqual(self.state()["boot"], boot, "boot changes without a restart")

    def test_boot_is_chosen_at_random(self):
        self.given("codes")
        boots = set()
        for _ in range(5):
            self.mock.restart()
            boots.add(self.state()["boot"])
        self.assertEqual(len(boots), 5)

    def test_restart_changes_boot(self):
        # "A different value means the adapter restarted"
        self.given("codes")
        boot = self.state()["boot"]
        self.mock.restart()
        self.assertNotEqual(self.state()["boot"], boot)

    def test_restart_loses_request_and_result(self):
        # "sequence numbers start anew and the stored result is gone"
        self.given("codes")
        self.read_done()
        self.assertEqual(self.get("/api/dtc/result").status, 200)
        self.mock.restart()
        self.assertEqual(self.dtc(), IDLE)
        self.assertEqual(self.get("/api/dtc/result").status, 204)

    def test_up_counts_the_seconds_since_boot(self):
        self.given()
        before = self.state()["up"]
        self.wait(10)
        passed = self.state()["up"] - before
        self.assertTrue(10 <= passed <= 10 + self.slack(), passed)

    def test_up_starts_at_0_with_a_restart(self):
        self.given("codes")
        self.wait(100)
        self.assertEqual(self.state()["up"], 100)
        self.mock.restart()
        self.assertEqual(self.state()["up"], 0)

    def test_autopid_is_off_starting_or_run(self):
        self.given()
        self.assertIn(self.state()["autopid"], ("off", "starting", "run"))

    def test_autopid_is_starting_until_the_task_is_in_its_loop(self):
        self.given("starting")
        self.assertEqual(self.state()["autopid"], "starting")
        self.mock.set(autopid="run")
        self.assertEqual(self.state()["autopid"], "run")

    def test_autopid_is_off_if_the_protocol_is_not_autopid(self):
        self.given("autopid_off")
        self.assertEqual(self.state()["autopid"], "off")

    def test_pids_is_the_number_of_values_of_the_profile(self):
        self.given()
        state = self.state()
        if state["autopid"] == "off":
            self.skipTest("no vehicle profile is loaded")
        self.assertEqual(state["pids"], len(self.get("/load_car_config").json()))
        if self.exact():
            self.assertEqual(state["pids"], 35)

    def test_ecu_is_online_or_offline(self):
        self.given()
        self.assertIn(self.state()["ecu"], ("online", "offline"))

    def test_ecu_is_online_with_the_ignition_on(self):
        self.given("codes")
        self.wait(5)
        self.assertEqual(self.state()["ecu"], "online")

    def test_ecu_is_offline_with_the_ignition_off(self):
        self.given("ignition_off")
        self.wait(5)
        self.assertEqual(self.state()["ecu"], "offline")

    def test_pass_never_goes_back(self):
        self.given()
        before = self.state()["pass"]
        self.wait(5)
        self.assertGreaterEqual(self.state()["pass"], before)

    def test_pass_moves_while_requests_are_answered(self):
        # "incremented after every polling pass in which at least one request was answered"
        self.given("codes")
        before = self.state()["pass"]
        self.wait(30)
        self.assertGreater(self.state()["pass"], before)

    def test_pass_stands_still_while_nothing_is_answered(self):
        self.given("ignition_off")
        self.wait(30)
        self.assertEqual(self.state()["pass"], 0)

    def test_values_change_only_when_pass_moved(self):
        # "Values of /autopid_data are fresh only if this counter moved"
        self.given("engine_running")
        self.wait(5)
        seen = {}
        for _ in range(60):
            passed, values = self.state()["pass"], self.get("/autopid_data").text
            if self.state()["pass"] == passed:
                self.assertEqual(seen.setdefault(passed, values), values, "other values in the same pass")
            self.wait(0.4)
        self.assertGreater(len(set(seen.values())), 1, "the values never changed")

    def test_rx_age_ms_is_minus_1_or_a_time(self):
        self.given()
        age = self.state()["rx_age_ms"]
        self.assertIs(type(age), int)
        self.assertGreaterEqual(age, -1)

    def test_rx_age_ms_is_minus_1_without_an_answer_since_boot(self):
        self.given("ignition_off")
        self.wait(20)
        self.assertEqual(self.state()["rx_age_ms"], -1)

    def test_rx_age_ms_counts_from_the_last_answer(self):
        self.given("codes")
        self.wait(10)
        self.assertTrue(0 <= self.state()["rx_age_ms"] < 2000)
        self.mock.set(ignition=False)
        self.wait(7)
        self.assertTrue(7000 <= self.state()["rx_age_ms"] < 9000)

    def test_rx_age_ms_is_at_most_2_31_minus_1(self):
        # "at most 2^31-1": 30 days are more milliseconds than that
        self.given("codes")
        self.wait(10)
        self.mock.set(ignition=False)
        self.wait(30 * 24 * 3600)
        self.assertEqual(self.state()["rx_age_ms"], 2 ** 31 - 1)

    def test_mqtt_is_off_connected_or_disconnected(self):
        self.given()
        self.assertIn(self.state()["mqtt"], ("off", "connected", "disconnected"))

    def test_mqtt_follows_the_broker_connection(self):
        for mqtt in ("off", "connected", "disconnected"):
            self.given(mqtt=mqtt)
            self.assertEqual(self.state()["mqtt"], mqtt)

    def test_batt_v_has_one_decimal_or_is_minus_1(self):
        self.given()
        self.assertRegex(self.get("/api/state").text, r'"batt_v":(-1|[0-9]+\.[0-9]),"sleep_in_s"')

    def test_batt_v_is_the_battery_voltage(self):
        for millivolts, text in ((12400, "12.4"), (12000, "12.0"), (14449, "14.4"), (9950, "10.0")):
            self.given(batt_mv=millivolts)
            self.assertIn('"batt_v":%s,' % text, self.get("/api/state").text)

    def test_batt_v_is_minus_1_if_not_measured(self):
        self.given(batt_mv=-1)
        self.assertIn('"batt_v":-1,', self.get("/api/state").text)

    def test_sleep_in_s_is_minus_1_or_a_number_of_seconds(self):
        self.given()
        seconds = self.state()["sleep_in_s"]
        self.assertIs(type(seconds), int)
        self.assertGreaterEqual(seconds, -1)

    def test_sleep_in_s_is_minus_1_while_not_counting_down(self):
        self.given("codes")
        self.wait(300)
        self.assertEqual(self.state()["sleep_in_s"], -1)

    def test_sleep_in_s_counts_down(self):
        self.given("ignition_off")
        before = self.state()["sleep_in_s"]
        self.wait(10)
        self.assertGreater(before, 10)
        self.assertEqual(self.state()["sleep_in_s"], before - 10)

    def test_heap_min_is_not_above_heap(self):
        self.given()
        state = self.state()
        self.assertTrue(0 < state["heap_min"] <= state["heap"], state)

    # dtc ------------------------------------------------------------------------------------

    def test_dtc_is_consistent(self):
        # The table of dtc, for whatever the adapter has done so far
        self.given()
        dtc = self.dtc()
        self.assertIs(type(dtc["supported"]), bool)
        self.assertIn(dtc["state"], ("idle", "queued", "running", "done", "error"))
        if dtc["state"] == "idle":
            self.assertEqual((dtc["action"], dtc["src"], dtc["seq"]), ("", "", 0))
        else:
            self.assertIn(dtc["action"], ("read", "clear"))
            self.assertIn(dtc["src"], ("mqtt", "http"))
            self.assertGreaterEqual(dtc["seq"], 1)
        self.assertEqual(dtc["reason"] != "", dtc["state"] == "error")
        if dtc["state"] == "error":
            self.assertIn(dtc["reason"], ERROR_REASONS)
        if dtc["state"] != "running":
            self.assertEqual(dtc["name"], "")
        if dtc["state"] not in ("done", "error"):
            self.assertEqual(dtc["age_s"], 0)
        self.assertLessEqual(dtc["ecu"], dtc["total"])
        if dtc["result_seq"] == 0:
            self.assertEqual(dtc["count"], 0)

    def test_dtc_is_idle_before_the_first_request(self):
        # "idle (nothing requested since boot)", "empty while idle", "0 = none", "0 = no result"
        self.given("codes")
        self.wait(3600)
        self.assertEqual(self.dtc_text(), fixture("dtc_state_idle.json"))

    def test_supported_is_false_without_a_fault_memory_table(self):
        self.given("unsupported")
        self.assertEqual(self.dtc_text(), fixture("dtc_state_unsupported.json"))

    def test_scan_without_a_fault_memory_table_ends_with_not_supported(self):
        self.given("unsupported")
        dtc = self.finished(self.read())
        self.assertEqual((dtc["state"], dtc["reason"]), ("error", "not_supported"))
        self.assertEqual(self.get("/api/dtc/result").status, 204)

    def test_request_is_queued_then_running_then_done(self):
        self.given(needs="read")
        states = [dtc["state"] for dtc in self.follow(self.read(), step=4)]
        order = [state for index, state in enumerate(states) if index == 0 or states[index - 1] != state]
        if self.exact():
            self.assertEqual(order, ["queued", "running", "done"])
        else:
            self.assertIn(order, (["queued", "running", "done"], ["running", "done"]))

    def test_action_and_src_are_those_of_the_last_accepted_request(self):
        self.given(needs="read")
        seen = self.follow(self.read(), step=4)
        self.assertEqual({(dtc["action"], dtc["src"]) for dtc in seen}, {("read", "http")})

    def test_action_is_clear_for_a_clear(self):
        self.given("codes")
        seq, _ = self.read_done()
        clear = self.post("/api/dtc?action=clear&seq=%d" % seq).json()["seq"]
        seen = self.follow(clear, step=4)
        self.assertEqual({(dtc["action"], dtc["src"]) for dtc in seen}, {("clear", "http")})

    def test_seq_is_the_number_of_the_last_accepted_request(self):
        self.given(needs="read")
        seq = self.read()
        self.assertEqual(self.dtc()["seq"], seq)
        self.post("/api/dtc?action=read")
        self.post("/api/dtc?action=read", headers=())
        self.assertEqual(self.dtc()["seq"], seq, "a refused request took the number")
        self.finished(seq)
        self.assertEqual(self.dtc()["seq"], seq)

    def test_requests_get_different_numbers(self):
        self.given(needs="read")
        first, _ = self.read_done()
        second = self.read()
        self.assertNotEqual(second, first)
        self.assertTrue(1 <= second <= 2 ** 31 - 1)
        if self.exact():
            self.assertEqual(second, first + 1)

    def test_sequence_numbers_stay_below_2_31_and_skip_0(self):
        self.given("no_codes", seq_seed=0xFFFFFFFE)
        numbers = [self.read_done()[0] for _ in range(3)]
        self.assertEqual(numbers, [2147483646, 2147483647, 1])

    def test_ecu_counts_from_0_to_total_while_running(self):
        # "step of a running scan: 0 = engine check, 1..total = control unit being processed"
        self.given(needs="read")
        steps = [(dtc["ecu"], dtc["total"]) for dtc in self.follow(self.read()) if dtc["state"] == "running"]
        self.assertEqual(steps, sorted(steps), "the step went back")
        if self.exact():
            self.assertEqual({total for _, total in steps}, {18})
            self.assertEqual(sorted(set(step for step, _ in steps)), list(range(19)))
        else:
            # Read in dtc_scan() of main/autopid.c, not measured: between the pickup and step 0 the
            # firmware prepares the adapter and is running without a number of control units yet
            self.assertTrue(set(steps) <= {(0, 0)} | {(step, 18) for step in range(19)}, steps)
            self.assertIn(18, [total for _, total in steps])

    def test_ecu_and_total_are_0_while_queued(self):
        self.given("codes")
        self.read_done()
        self.read()
        dtc = self.dtc()
        self.assertEqual((dtc["state"], dtc["ecu"], dtc["total"]), ("queued", 0, 0))

    def test_name_is_the_control_unit_being_processed(self):
        # "control unit being processed, empty unless running"
        self.given(needs="read")
        seen = self.follow(self.read())
        names = {dtc["ecu"]: dtc["name"] for dtc in seen if dtc["state"] == "running"}
        self.assertEqual(names, {step: UNITS[step - 1][0] if step > 0 else "" for step in names})
        if self.exact():
            self.assertEqual(len(names), 19)
        self.assertEqual({dtc["name"] for dtc in seen if dtc["state"] != "running"}, {""})

    def test_running_state_is_the_fixture(self):
        self.given("codes", seq_seed=41)
        seen = self.follow(self.read(), as_sent=True)
        self.assertIn(fixture("dtc_state_queued.json"), seen)
        self.assertIn(fixture("dtc_state_running.json"), seen)
        self.assertIn(fixture("dtc_state_running_umlaut.json"), seen)

    def test_reason_is_empty_without_an_error(self):
        self.given(needs="read")
        self.assertEqual({dtc["reason"] for dtc in self.follow(self.read(), step=4)}, {""})

    def test_reason_of_an_error_is_one_of_the_documented(self):
        self.only_mock()
        for scenario, expected in (("ignition_off", "ecu_offline"), ("unsupported", "not_supported"),
                                   ("stalled", "expired")):
            with self.subTest(scenario=scenario):
                self.given(scenario)
                dtc = self.finished(self.read())
                self.assertEqual((dtc["state"], dtc["reason"]), ("error", expected))
                self.assertIn(dtc["reason"], ERROR_REASONS)

    def test_new_request_clears_the_reason(self):
        self.given("ignition_off")
        self.assertEqual(self.finished(self.read())["reason"], "ecu_offline")
        self.mock.set(ignition=True)
        self.read()
        self.assertEqual(self.dtc()["reason"], "")

    def test_age_s_is_0_until_the_request_has_ended(self):
        # "seconds since done or error, else 0"
        self.given(needs="read")
        self.wait(100)
        seen = self.follow(self.read(), step=4)
        self.assertEqual({dtc["age_s"] for dtc in seen if dtc["state"] in ("queued", "running")}, {0})

    def test_age_s_counts_the_seconds_since_done(self):
        self.given(needs="read")
        seq = self.read()
        before = self.follow(seq)[-1]["age_s"]
        self.wait(12)
        passed = self.dtc()["age_s"] - before
        self.assertTrue(12 <= passed <= 12 + self.slack(), passed)

    def test_age_s_counts_the_seconds_since_an_error(self):
        self.given("ignition_off")
        seq = self.read()
        before = self.follow(seq)[-1]
        self.assertEqual((before["state"], before["age_s"]), ("error", 0))
        self.wait(100)
        self.assertEqual(self.dtc()["age_s"], 100)

    def test_count_is_the_number_of_trouble_codes_in_the_result(self):
        self.given(needs="read")
        _, dtc = self.read_done()
        self.assertEqual(dtc["count"], self.get("/api/dtc/result").json()["dtc_count"])
        if self.exact():
            self.assertEqual(dtc["count"], 5)

    def test_count_includes_omitted_codes(self):
        self.given("many_codes")
        _, dtc = self.read_done()
        self.assertEqual(dtc["count"], 165)

    def test_result_seq_is_0_without_a_result(self):
        self.given("codes")
        seq = self.read()
        self.assertEqual({dtc["result_seq"] for dtc in self.follow(seq, step=4)[:-1]}, {0})

    def test_result_seq_is_the_number_of_the_finished_scan(self):
        self.given(needs="read")
        seq, dtc = self.read_done()
        self.assertEqual(dtc["result_seq"], seq)

    # POST /api/dtc --------------------------------------------------------------------------

    def test_read_is_accepted_with_202_and_its_number(self):
        self.given(needs="read")
        answer = self.post("/api/dtc?action=read")
        seq = answer.json()["seq"]
        self.assertEqual(answer.brief(), accepted(seq))
        self.assertTrue(1 <= seq <= 2 ** 31 - 1, seq)

    def test_answer_comes_at_once_the_scan_follows(self):
        # "The answer comes at once, the scan takes about 35 s"
        self.given(needs="read")
        seq = self.read()
        dtc = self.dtc()
        self.assertEqual(dtc["seq"], seq)
        self.assertIn(dtc["state"], ("queued", "running"))
        self.assertEqual(self.finished(seq)["state"], "done")

    def test_read_takes_about_35_s(self):
        self.given(needs="read")
        start = self.state()["up"]
        self.follow(self.read(), step=1)
        taken = self.state()["up"] - start
        self.assertTrue(25 <= taken <= 45 + self.slack(), taken)

    def test_result_is_there_when_done_with_the_number_of_the_request(self):
        # "fetches the result when state is done and result_seq equals the seq it got"
        self.given(needs="read")
        seq = self.read()
        dtc = self.finished(seq)
        self.assertEqual((dtc["state"], dtc["result_seq"]), ("done", seq))
        answer = self.get("/api/dtc/result")
        self.assertEqual((answer.status, answer.header("X-DTC-Seq")), (200, "%d" % seq))

    def test_request_body_is_ignored(self):
        # "There is no body"
        self.given("codes", seq_seed=43)
        answer = self.request("POST", "/api/dtc?action=read", DTC_HEADER, body=b'{"action":"clear"}')
        self.assertEqual(answer.brief(), accepted(43))
        self.assertEqual(self.dtc()["action"], "read")
        # The body is not taken for the next request on the connection
        self.assertEqual(self.get("/api/state").status, 200)

    def test_post_without_the_header_is_forbidden(self):
        # "Required: the request header X-WiCAN-DTC: 1"
        self.given(needs="read")
        before = self.dtc()
        self.assertEqual(self.post("/api/dtc?action=read", headers=()).brief(), FORBIDDEN)
        self.assertEqual(self.kept(self.dtc()), self.kept(before))

    def test_post_with_another_header_value_is_forbidden(self):
        # "header missing or not 1"
        self.given(needs="read")
        before = self.dtc()
        for value in ("0", "2", "11", "01", "true", "yes", "1x"):
            with self.subTest(value=value):
                self.assertEqual(self.post("/api/dtc?action=read", headers=(("X-WiCAN-DTC", value),)).brief(),
                                 FORBIDDEN)
        self.assertEqual(self.kept(self.dtc()), self.kept(before))

    def test_post_with_a_foreign_host_is_forbidden(self):
        # "or host not allowed": the name of a web page that was pointed at the adapter
        self.given(needs="read")
        before = self.dtc()
        for host in ("page.example", "page.example:80", "localhost", "localhost:8080", "wican.local",
                     "192.168.80.1.page.example", "wican_a1b2c3d4e5f6.local.page.example", "[::1]", "[::1]:80",
                     "192.168.80", "192.168.80.256", "http://192.168.80.1", None,
                     # main/dtc_api.c: four numbers of 1 to 3 digits, a port of 1 to 5 digits
                     "192.168.80.0001", "0192.168.80.1", "1920.168.80.1", "192.168..1", "192.168.80.1.", "192.168.80.1.2",
                     "192.168.80.1:", "192.168.80.1:123456", "192.168.80.1:80a", "192.168.80.1:-1",
                     # an id of 1 to 32 hexadecimal digits between "wican_" and ".local"
                     "wican_.local", "wican_%s.local" % ("a" * 33), "wican_xyz.local", "wican_a1b2xlocal",
                     "wican_a1b2.locale", "wican_a1b2.local.", "wican_a1b2.local:", "xwican_a1b2.local",
                     "wican-a1b2.local"):
            with self.subTest(host=host):
                self.assertEqual(self.post("/api/dtc?action=read", host=host).brief(), FORBIDDEN)
        self.assertEqual(self.kept(self.dtc()), self.kept(before))

    def test_post_with_an_ipv4_address_as_host_is_allowed(self):
        # "a Host header that is an IPv4 address (optionally with port)". The unknown action shows that
        # the request got past the check without starting anything.
        self.given(needs="read")
        for host in ("192.168.80.1", "192.168.80.1:80", "10.0.0.7:8080", "255.255.255.255", "0.0.0.0:65535",
                     "1.2.3.4:1", "001.002.003.004", "0.0.0.0"):
            with self.subTest(host=host):
                self.assertEqual(self.post("/api/dtc?action=nothing", host=host).brief(), BAD_REQUEST)

    def test_post_with_the_mdns_name_as_host_is_allowed(self):
        # "or a name of the form wican_<id>.local"; with a port as well: main/dtc_api.h
        self.given(needs="read")
        name = "wican_%s.local" % self.state()["id"]
        for host in (name, name.upper(), name + ":80", "wican_0.local", "WiCAN_00ff.Local:65535",
                     "wican_0123456789abcdefABCDEF0123456789.local"):
            with self.subTest(host=host):
                self.assertEqual(self.post("/api/dtc?action=nothing", host=host).brief(), BAD_REQUEST)

    def test_post_without_action_is_a_bad_request(self):
        # "action missing or unknown"
        self.given(needs="read")
        before = self.dtc()
        for target in ("/api/dtc", "/api/dtc?", "/api/dtc?seq=5", "/api/dtc?read", "/api/dtc?xaction=read",
                       "/api/dtc?actions=read", "/api/dtc?=read"):
            with self.subTest(target=target):
                self.assertEqual(self.post(target).brief(), BAD_REQUEST)
        self.assertEqual(self.kept(self.dtc()), self.kept(before))

    def test_post_with_an_unknown_action_is_a_bad_request(self):
        self.given(needs="read")
        before = self.dtc()
        # The first parameter called action counts, '&' is the only separator, nothing is decoded
        for action in ("", "scan", "READ", "Read", "read2", "rea", "read%20", "r%65ad", "delete", "nothing&action=read",
                       "read;x=1", "=read", "read=1"):
            with self.subTest(action=action):
                self.assertEqual(self.post("/api/dtc?action=%s" % action).brief(), BAD_REQUEST)
        self.assertEqual(self.kept(self.dtc()), self.kept(before))

    def test_clear_without_seq_is_a_bad_request(self):
        # "seq missing or malformed for a clear"
        self.given(needs="clear")
        for target in ("/api/dtc?action=clear", "/api/dtc?action=clear&seq", "/api/dtc?action=clear&sequence=5"):
            with self.subTest(target=target):
                self.assertEqual(self.post(target).brief(), BAD_REQUEST)

    def test_clear_with_a_malformed_seq_is_a_bad_request(self):
        # "decimal digits only, 1..2147483647"
        self.given(needs="clear")
        for seq in ("", "0", "2147483648", "4294967297", "-1", "+5", "12a", "a12", "abc", "1.0", "0x1F", "%31",
                    "1%20", "1,2"):
            with self.subTest(seq=seq):
                self.assertEqual(self.post("/api/dtc?action=clear&seq=%s" % seq).brief(), BAD_REQUEST)

    def test_clear_with_a_well_formed_seq_reaches_the_rules(self):
        self.given("codes")
        for seq in ("1", "7", "2147483647"):
            with self.subTest(seq=seq):
                self.assertEqual(self.post("/api/dtc?action=clear&seq=%s" % seq).brief(), refused("read_required", 0))

    def test_read_ignores_seq(self):
        # "It is required for clear and ignored for read"
        self.only_mock()
        for seq in ("", "abc", "0", "99999999999", "5"):
            with self.subTest(seq=seq):
                self.given("codes", seq_seed=43)
                self.assertEqual(self.post("/api/dtc?action=read&seq=%s" % seq).brief(), accepted(43))

    def test_refusals_before_the_rules_carry_the_number_0(self):
        # The table: forbidden and bad_request with "seq":0, also while the state has a number
        self.given(needs="read")
        self.assertNotEqual(self.read(), 0)
        self.assertEqual(self.post("/api/dtc?action=read", headers=()).brief(), FORBIDDEN)
        self.assertEqual(self.post("/api/dtc?action=read", host="page.example").brief(), FORBIDDEN)
        self.assertEqual(self.post("/api/dtc?action=nothing").brief(), BAD_REQUEST)

    def test_forbidden_is_decided_before_bad_request(self):
        # "The checks are made in this order: forbidden, bad_request, not_ready, then the rules"
        self.given(needs="read")
        self.assertEqual(self.post("/api/dtc?action=nothing", headers=()).brief(), FORBIDDEN)
        self.assertEqual(self.post("/api/dtc", host="page.example").brief(), FORBIDDEN)

    def test_forbidden_is_decided_before_not_ready(self):
        self.given("starting")
        self.assertEqual(self.post("/api/dtc?action=read", headers=()).brief(), FORBIDDEN)

    def test_bad_request_is_decided_before_not_ready(self):
        self.given("starting")
        self.assertEqual(self.post("/api/dtc?action=nothing").brief(), BAD_REQUEST)
        self.assertEqual(self.post("/api/dtc?action=clear").brief(), BAD_REQUEST)

    def test_not_ready_is_decided_before_the_rules_of_the_scan(self):
        self.given("codes")
        seq = self.read()
        self.mock.set(autopid="starting")
        self.assertEqual(self.post("/api/dtc?action=read").brief(), NOT_READY)
        self.assertEqual(self.post("/api/dtc?action=clear&seq=%d" % seq).brief(), NOT_READY)

    def test_not_ready_while_autopid_is_starting(self):
        # "503 ... autopid is not run"
        self.given("starting")
        self.assertEqual(self.post("/api/dtc?action=read").brief(), NOT_READY)
        self.assertEqual(self.dtc()["state"], "idle")

    def test_not_ready_while_autopid_is_off(self):
        self.given("autopid_off")
        self.assertEqual(self.post("/api/dtc?action=read").brief(), NOT_READY)
        self.assertEqual(self.dtc()["state"], "idle")

    def test_request_during_a_queued_request_is_busy(self):
        # "409 busy: a scan is queued or running; 42 is its number"
        self.given("codes", seq_seed=42)
        self.read()
        self.assertEqual(self.dtc()["state"], "queued")
        self.assertEqual(self.post("/api/dtc?action=read").brief(), refused("busy", 42))

    def test_request_during_a_running_scan_is_busy(self):
        self.given(needs="read")
        seq = self.read()
        self.wait(5)
        self.assertEqual(self.dtc()["state"], "running")
        self.assertEqual(self.post("/api/dtc?action=read").brief(), refused("busy", seq))

    def test_clear_during_a_scan_is_busy(self):
        self.given("codes")
        first, _ = self.read_done()
        second = self.read()
        self.wait(5)
        for seq in (first, second):
            self.assertEqual(self.post("/api/dtc?action=clear&seq=%d" % seq).brief(), refused("busy", second))

    def test_busy_request_does_not_disturb_the_scan(self):
        self.given(needs="read")
        seq = self.read()
        self.wait(5)
        before = self.dtc()
        self.post("/api/dtc?action=read")
        if self.exact():
            self.assertEqual(self.dtc(), before)
        dtc = self.finished(seq)
        self.assertEqual((dtc["state"], dtc["result_seq"]), ("done", seq))

    def test_clear_without_a_read_is_refused(self):
        # "409 read_required: clear, but the last request is not a read that finished with a result"
        self.given("codes")
        self.assertEqual(self.post("/api/dtc?action=clear&seq=1").brief(), refused("read_required", 0))
        self.assertEqual(self.dtc(), IDLE)

    def test_clear_after_a_failed_read_is_refused(self):
        self.given("codes")
        first, _ = self.read_done()
        self.mock.set(ignition=False)
        second = self.read()
        self.assertEqual(self.finished(second)["reason"], "ecu_offline")
        self.mock.set(ignition=True)
        for seq in (first, second):
            self.assertEqual(self.post("/api/dtc?action=clear&seq=%d" % seq).brief(), refused("read_required", second))

    def test_clear_after_a_clear_needs_a_new_read(self):
        self.given("codes")
        read, _ = self.read_done()
        clear = self.post("/api/dtc?action=clear&seq=%d" % read).json()["seq"]
        dtc = self.finished(clear)
        self.assertEqual((dtc["state"], dtc["count"]), ("done", 2))
        for seq in (read, clear):
            self.assertEqual(self.post("/api/dtc?action=clear&seq=%d" % seq).brief(), refused("read_required", clear))

    def test_clear_599_s_after_the_read_is_accepted(self):
        # "at most 600 s ago". The limit to the millisecond is compared with the firmware module in Rules.
        self.given("codes")
        seq, dtc = self.read_done()
        self.wait(599 - dtc["age_s"])
        self.assertEqual(self.dtc()["age_s"], 599)
        self.assertEqual(self.post("/api/dtc?action=clear&seq=%d" % seq).brief(), accepted(seq + 1))

    def test_clear_601_s_after_the_read_is_refused(self):
        self.given("codes")
        seq, dtc = self.read_done()
        self.wait(601 - dtc["age_s"])
        self.assertEqual(self.dtc()["age_s"], 601)
        self.assertEqual(self.post("/api/dtc?action=clear&seq=%d" % seq).brief(), refused("read_required", seq))

    def test_clear_with_the_number_of_another_read_is_stale(self):
        # "409 stale_seq: clear refers to another read than the last one"
        self.given(needs="clear")
        first, _ = self.read_done()
        second, before = self.read_done()
        others = [first, second - 1 if second > 1 else 3, second + 1 if second < 2 ** 31 - 1 else 3]
        for seq in others + ([second ^ 0x40000000] if self.exact() else []):
            with self.subTest(seq=seq):
                self.assertEqual(self.post("/api/dtc?action=clear&seq=%d" % seq).brief(), refused("stale_seq", second))
        self.assertEqual(self.kept(self.dtc()), self.kept(before))

    def test_clear_of_a_read_without_trouble_codes_is_refused(self):
        # "409 nothing_to_clear: the last read found no trouble codes"
        self.given("no_codes")
        seq, dtc = self.read_done()
        self.assertEqual(dtc["count"], 0)
        self.assertEqual(self.post("/api/dtc?action=clear&seq=%d" % seq).brief(), refused("nothing_to_clear", seq))

    def test_too_old_is_decided_before_stale(self):
        # main/dtc_state.h: "The first reason that applies is returned, in this order"
        self.given("codes")
        seq, _ = self.read_done()
        self.wait(700)
        self.assertEqual(self.post("/api/dtc?action=clear&seq=%d" % (seq + 1)).brief(), refused("read_required", seq))

    def test_stale_is_decided_before_nothing_to_clear(self):
        self.given("no_codes")
        seq, _ = self.read_done()
        self.assertEqual(self.post("/api/dtc?action=clear&seq=%d" % (seq + 1)).brief(), refused("stale_seq", seq))

    def test_clear_with_the_number_of_the_last_read_is_accepted(self):
        self.given(needs="clear")
        seq, dtc = self.read_done()
        answer = self.post("/api/dtc?action=clear&seq=%d" % seq)
        if dtc["count"] == 0:
            self.assertFalse(self.exact(), "the mock has trouble codes in this scenario")
            self.assertEqual(answer.brief(), refused("nothing_to_clear", seq))
            return
        clear = answer.json()["seq"]
        self.assertEqual(answer.brief(), accepted(clear))
        self.assertNotEqual(clear, seq)
        dtc = self.finished(clear)
        self.assertEqual((dtc["state"], dtc["action"], dtc["result_seq"]), ("done", "clear", clear))
        answer = self.get("/api/dtc/result")
        self.assertEqual(answer.header("X-DTC-Seq"), "%d" % clear)
        self.assertEqual(check_result(self, answer.text, "clear"), dtc["count"])

    def test_clear_of_a_read_over_mqtt_is_accepted(self):
        # "a scan started one way is visible the other way": the number of an MQTT read is in the state
        self.given("codes")
        self.mock.mqtt_command()
        self.wait(45)
        dtc = self.dtc()
        self.assertEqual((dtc["state"], dtc["src"], dtc["count"]), ("done", "mqtt", 5))
        self.assertEqual(self.post("/api/dtc?action=clear&seq=%d" % dtc["seq"]).status, 202)

    def test_request_not_picked_up_within_20_s_expires(self):
        # "An accepted request that the AutoPID task does not pick up within 20 s ends as error with
        # reason expired and does not run"
        self.given("codes", pickup_ms=20001, seq_seed=7)
        self.read()
        self.wait(20)
        self.assertEqual(self.dtc()["state"], "queued")
        self.wait(0.001)
        expired = dict(IDLE, state="error", action="read", src="http", seq=7, reason="expired")
        self.assertEqual(self.dtc(), expired)
        self.wait(60)
        self.assertEqual(self.dtc(), dict(expired, age_s=60), "the expired request ran")
        self.assertEqual(self.get("/api/dtc/result").status, 204)

    def test_request_picked_up_after_20_s_runs(self):
        self.given("codes", pickup_ms=20000)
        seq = self.read()
        self.wait(20)
        self.assertEqual(self.dtc()["state"], "running")
        self.assertEqual(self.finished(seq)["state"], "done")

    def test_expired_clear_keeps_the_result_and_uses_up_the_read(self):
        self.given("codes")
        read, _ = self.read_done()
        self.mock.set(pickup_ms=25000)
        clear = self.post("/api/dtc?action=clear&seq=%d" % read).json()["seq"]
        dtc = self.finished(clear)
        self.assertEqual((dtc["reason"], dtc["result_seq"], dtc["count"]), ("expired", read, 5))
        self.assertEqual(self.get("/api/dtc/result").header("X-DTC-Seq"), "%d" % read)
        self.assertEqual(self.post("/api/dtc?action=clear&seq=%d" % read).brief(), refused("read_required", clear))

    def test_scan_started_over_mqtt_is_visible(self):
        # "a scan started one way is visible the other way"
        self.given("codes", seq_seed=7)
        self.mock.mqtt_command(clear=True)
        self.assertEqual(self.dtc(), dict(IDLE, state="queued", action="clear", src="mqtt", seq=7))
        self.wait(5)
        dtc = self.dtc()
        self.assertEqual((dtc["state"], dtc["src"], dtc["seq"]), ("running", "mqtt", 7))

    def test_post_during_a_scan_started_over_mqtt_is_busy(self):
        self.given("mqtt_scan", seq_seed=7)
        self.wait(10)
        self.assertEqual(self.post("/api/dtc?action=read").brief(), refused("busy", 7))

    def test_result_of_a_scan_started_over_mqtt_is_stored(self):
        self.given("mqtt_scan", seq_seed=7)
        self.wait(50)
        answer = self.get("/api/dtc/result")
        self.assertEqual((answer.status, answer.header("X-DTC-Seq")), (200, "7"))

    def test_read_with_the_ignition_off_ends_with_ecu_offline(self):
        # W906.md: "Keine Antwort (Zündung aus): Abbruch mit ecu_offline"
        self.given("ignition_off", seq_seed=7)
        self.assertEqual(self.post("/api/dtc?action=read").brief(), accepted(7))
        dtc = self.finished(7)
        self.assertEqual(dict(dtc, age_s=0), dict(IDLE, state="error", action="read", src="http", seq=7, total=18,
                                                  reason="ecu_offline"))

    def test_clear_with_the_engine_running_ends_with_engine_running(self):
        # W906.md: "Löschen nur bei einer Drehzahl unter 50 1/min, sonst Abbruch mit engine_running"
        self.given("engine_running")
        read, _ = self.read_done()
        answer = self.post("/api/dtc?action=clear&seq=%d" % read)
        self.assertEqual(answer.brief(), accepted(read + 1))
        dtc = self.finished(read + 1)
        self.assertEqual(dict(dtc, age_s=0), dict(IDLE, state="error", action="clear", src="http", seq=read + 1,
                                                  total=18, reason="engine_running", count=5, result_seq=read))
        self.assertEqual(check_result(self, self.get("/api/dtc/result").text, "read"), 5)

    def test_clear_with_the_ignition_off_ends_with_ecu_offline(self):
        # W906.md: "Vor jedem Durchlauf", the check comes before a clear as well; no control unit is asked
        self.given("codes")
        read, _ = self.read_done()
        self.mock.set(ignition=False)
        answer = self.post("/api/dtc?action=clear&seq=%d" % read)
        self.assertEqual(answer.brief(), accepted(read + 1))
        dtc = self.finished(read + 1)
        self.assertEqual((dtc["state"], dtc["reason"], dtc["ecu"]), ("error", "ecu_offline", 0))

    def test_clear_runs_below_50_rpm_and_is_refused_from_50_rpm_on(self):
        # W906.md: "Löschen nur bei einer Drehzahl unter 50 1/min"
        self.only_mock()
        for rpm, expected in ((49, ("done", "")), (50, ("error", "engine_running"))):
            with self.subTest(rpm=rpm):
                self.given("codes", rpm=rpm)
                read, _ = self.read_done()
                self.assertEqual(self.post("/api/dtc?action=clear&seq=%d" % read).brief(), accepted(read + 1))
                dtc = self.finished(read + 1)
                self.assertEqual((dtc["state"], dtc["reason"]), expected)

    # GET /api/dtc/result --------------------------------------------------------------------

    def test_result_belongs_to_result_seq(self):
        # "200 ... with the header X-DTC-Seq", "204 ... no result stored"
        self.given()
        result_seq = self.dtc()["result_seq"]
        answer = self.get("/api/dtc/result")
        if result_seq == 0:
            self.assertEqual((answer.status, answer.body), (204, b""))
        else:
            self.assertEqual((answer.status, answer.header("X-DTC-Seq")), (200, "%d" % result_seq))

    def test_result_is_204_and_empty_before_a_scan_has_finished(self):
        self.given("codes")
        self.assertEqual(self.get("/api/dtc/result").brief(), (204, ""))
        self.read()
        self.wait(20)
        self.assertEqual(self.get("/api/dtc/result").brief(), (204, ""))

    def test_result_of_a_read_has_the_documented_format(self):
        # W906.md, "Fehlerspeicher"
        self.given(needs="read")
        _, dtc = self.read_done()
        self.assertEqual(check_result(self, self.get("/api/dtc/result").text, "read"), dtc["count"])

    def test_result_is_shortened_with_dtcs_omitted(self):
        # W906.md: "Passt das Ergebnis nicht in eine MQTT-Nachricht (5 KB), fehlen die Codes der
        # Steuergeräte mit den meisten Einträgen. Stattdessen steht dort ihre Anzahl."
        self.given("many_codes")
        self.read_done()
        text = self.get("/api/dtc/result").text
        self.assertEqual(check_result(self, text, "read"), 165)
        omitted = {ecu["name"]: ecu["dtcs_omitted"] for ecu in json.loads(text)["ecus"] if "dtcs_omitted" in ecu}
        self.assertEqual(omitted, {"N3/28 Motorelektronik (CDID3)": 80, "N30/4 ESP": 75})

    def test_result_of_a_clear_says_what_was_cleared(self):
        # W906.md: "cleared (nur beim Löschen, nur bei Steuergeräten mit Einträgen)"
        self.given("codes")
        read, _ = self.read_done()
        before = self.get("/api/dtc/result").json()
        clear = self.post("/api/dtc?action=clear&seq=%d" % read).json()["seq"]
        self.finished(clear)
        after = self.get("/api/dtc/result").json()
        self.assertEqual([ecu["name"] for ecu in after["ecus"] if "cleared" in ecu],
                         [ecu["name"] for ecu in before["ecus"] if ecu["dtcs"]])
        self.assertEqual(sorted(str(ecu["cleared"]) for ecu in after["ecus"] if "cleared" in ecu),
                         ["False", "True", "True", "True"])

    def test_result_is_503_while_autopid_is_not_active(self):
        # "503 not_ready: no memory for the copy, or AutoPID is not active"
        self.only_mock()
        for scenario in ("autopid_off", "starting"):
            with self.subTest(scenario=scenario):
                self.given(scenario)
                self.assertEqual(self.get("/api/dtc/result").brief(), NOT_READY)

    def test_result_is_kept_until_the_next_scan_finishes(self):
        # "The result is kept in RAM until the next scan finishes"
        self.given("codes")
        first, _ = self.read_done()
        text = self.get("/api/dtc/result").text
        self.wait(3600)
        second = self.read()
        self.wait(20)
        answer = self.get("/api/dtc/result")
        self.assertEqual((answer.header("X-DTC-Seq"), answer.text), ("%d" % first, text))
        dtc = self.dtc()
        self.assertEqual((dtc["state"], dtc["result_seq"], dtc["count"]), ("running", first, 5))
        self.finished(second)
        self.assertEqual(self.get("/api/dtc/result").header("X-DTC-Seq"), "%d" % second)

    def test_result_survives_a_later_error(self):
        # "It survives a later error"
        self.given("codes")
        first, _ = self.read_done()
        text = self.get("/api/dtc/result").text
        self.mock.set(ignition=False)
        dtc = self.finished(self.read())
        self.assertEqual((dtc["state"], dtc["result_seq"], dtc["count"]), ("error", first, 5))
        answer = self.get("/api/dtc/result")
        self.assertEqual((answer.status, answer.header("X-DTC-Seq"), answer.text), (200, "%d" % first, text))

    # Other requests -------------------------------------------------------------------------

    def test_autopid_data_is_a_flat_object(self):
        # "the current values as a flat JSON object"
        self.given()
        self.wait(5)
        answer = self.get("/autopid_data")
        self.assertEqual(answer.status, 200)
        values = answer.json()
        self.assertIsInstance(values, dict)
        for name, value in values.items():
            with self.subTest(name=name):
                self.assertNotIsInstance(value, (dict, list, bool, type(None)))
        if self.exact():
            self.assertEqual(len(values), 35)

    def test_autopid_data_is_empty_while_no_value_is_valid(self):
        # "{} while no value is valid"
        self.given("ignition_off")
        self.wait(30)
        self.assertEqual(self.get("/autopid_data").brief(), (200, "{}"))

    def test_autopid_data_loses_its_values_with_the_ignition(self):
        self.given("codes")
        self.wait(10)
        self.assertEqual(len(self.get("/autopid_data").json()), 35)
        self.mock.set(ignition=False)
        self.assertEqual(self.get("/autopid_data").text, "{}")

    def test_load_car_config_has_class_and_unit_of_every_value(self):
        # '{"NAME":{"class":"…","unit":"…"}, …} for every value of the profile'
        self.given()
        self.wait(5)
        config = self.get("/load_car_config").json()
        for name, entry in config.items():
            with self.subTest(name=name):
                self.assertEqual(list(entry), ["class", "unit"])
                self.assertIsInstance(entry["class"], str)
                self.assertIsInstance(entry["unit"], str)
        values = self.get("/autopid_data").json()
        self.assertTrue(set(values) <= set(config), "values without an entry in the profile")
        if self.exact():
            self.assertEqual(list(values), list(config))

    def test_load_car_config_works_with_the_ignition_off(self):
        # "also with the ignition off"
        self.given("ignition_off")
        self.wait(30)
        self.assertEqual(len(self.get("/load_car_config").json()), 35)

    def test_one_connection_serves_many_requests(self):
        # "A client should use one connection"
        self.given()
        self.get("/api/state")
        socket = self.connection.sock
        self.assertIsNotNone(socket, "the connection was closed after the first request")
        # The requests a client uses. After an answer like 404 the firmware may close the connection.
        for target in ("/api/state", "/api/dtc/result", "/autopid_data", "/load_car_config", "/api/state"):
            self.get(target)
            self.assertIs(self.connection.sock, socket, "the connection was closed after %s" % target)

    def test_unknown_path_is_404(self):
        self.given()
        self.assertEqual(self.get("/api/nothing").status, 404)

    def test_everything_else_is_404(self):
        self.given("codes")
        for method, target in (("GET", "/"), ("GET", "/api"), ("GET", "/api/"), ("GET", "/api/state/"),
                               ("GET", "/api/dtc/result/"), ("GET", "/api/dtc/results"), ("GET", "/API/STATE"),
                               ("GET", "/check_status"), ("GET", "/load_config"), ("GET", "/scan_available_pids"),
                               ("POST", "/api/state"), ("POST", "/api/dtc/result"), ("POST", "/autopid_data"),
                               ("PUT", "/api/dtc?action=read"), ("DELETE", "/api/dtc/result"),
                               ("PATCH", "/api/dtc?action=read"), ("HEAD", "/api/state"), ("HEAD", "/"),
                               ("OPTIONS", "/api/state"), ("OPTIONS", "/api/dtc?action=read")):
            with self.subTest(method=method, target=target):
                self.assertEqual(self.request(method, target, DTC_HEADER).status, 404)
        self.assertEqual(self.dtc(), IDLE)

    def test_get_does_not_start_a_scan(self):
        # "POST /api/dtc": a GET, which a web page can send from an <img>, changes nothing
        self.given(needs="read")
        before = self.dtc()
        answer = self.get("/api/dtc?action=read", headers=DTC_HEADER)
        self.assertIn(answer.status, (404, 405))
        self.assertEqual(self.kept(self.dtc()), self.kept(before))


# ------------------------------------------------------------------------------------------------
# Red proof: the contract test fails for an adapter that breaks the contract
# ------------------------------------------------------------------------------------------------

def run_tests(case, names=None, **attributes):
    """Runs the tests of `case` (all, or the named ones) with other class attributes. Returns the
    names of the tests that failed, of those that ended with an error, and the result."""
    changed = type(case.__name__ + "Changed", (case,), attributes)
    if names is None:
        suite = unittest.defaultTestLoader.loadTestsFromTestCase(changed)
    else:
        suite = unittest.TestSuite(changed(name) for name in names)
    result = unittest.TestResult()
    suite.run(result)

    def names_of(entries):
        # A failed subTest carries its test in test_case; an error in setUpClass has no test method
        tests = [getattr(test, "test_case", test) for test, _ in entries]
        return sorted({getattr(test, "_testMethodName", str(test)) for test in tests})

    return names_of(result.failures), names_of(result.errors), result


def changed(path, old, new):
    """The text of a source file with one change."""
    source = path.read_text(encoding="utf-8")
    if source.count(old) != 1:
        raise ValueError("the text occurs %d times in %s, expected once: %r" % (source.count(old), path.name, old))
    return source.replace(old, new)


def changed_mock(old, new):
    """mock_wican.py with one change, as a module of its own."""
    module = types.ModuleType("mock_wican_changed")
    module.source = changed(HERE / "mock_wican.py", old, new)
    exec(compile(module.source, "mock_wican.py (changed)", "exec"), module.__dict__)
    return module


def changed_tests(old, new):
    """This file with one change, as a module of its own."""
    module = types.ModuleType("test_api_contract_changed")
    module.__file__ = str(THIS)
    exec(compile(changed(THIS, old, new), "test_api_contract.py (changed)", "exec"), module.__dict__)
    return module


# Fault switch of the mock: the tests of Contract that fail with it, no more and no less
FAULT_FAILS = {
    "no_busy": (
        "test_answers_to_post_are_not_cached",
        "test_busy_request_does_not_disturb_the_scan",
        "test_clear_during_a_scan_is_busy",
        "test_post_during_a_scan_started_over_mqtt_is_busy",
        "test_request_during_a_queued_request_is_busy",
        "test_request_during_a_running_scan_is_busy",
        "test_seq_is_the_number_of_the_last_accepted_request",
    ),
    "clear_ignores_seq": (
        "test_clear_with_the_number_of_another_read_is_stale",
        "test_stale_is_decided_before_nothing_to_clear",
    ),
    "clear_without_read": (
        "test_clear_after_a_clear_needs_a_new_read",
        "test_clear_after_a_failed_read_is_refused",
        "test_clear_with_a_well_formed_seq_reaches_the_rules",
        "test_clear_without_a_read_is_refused",
        "test_expired_clear_keeps_the_result_and_uses_up_the_read",
    ),
    "get_triggers": (
        "test_get_does_not_start_a_scan",
    ),
    "header_not_required": (
        "test_answers_to_post_are_not_cached",
        "test_forbidden_is_decided_before_bad_request",
        "test_forbidden_is_decided_before_not_ready",
        "test_post_with_another_header_value_is_forbidden",
        "test_post_without_the_header_is_forbidden",
        "test_refusals_before_the_rules_carry_the_number_0",
    ),
    "host_not_checked": (
        "test_forbidden_is_decided_before_bad_request",
        "test_post_with_a_foreign_host_is_forbidden",
        "test_refusals_before_the_rules_carry_the_number_0",
    ),
    "no_expiry": (
        "test_expired_clear_keeps_the_result_and_uses_up_the_read",
        "test_reason_of_an_error_is_one_of_the_documented",
        "test_request_not_picked_up_within_20_s_expires",
    ),
    "result_lost_after_error": (
        "test_clear_with_the_engine_running_ends_with_engine_running",
        "test_expired_clear_keeps_the_result_and_uses_up_the_read",
        "test_result_survives_a_later_error",
    ),
    "seq_not_31_bit": (
        "test_sequence_numbers_stay_below_2_31_and_skip_0",
    ),
    "wrong_field_order": (
        "test_state_fields_come_in_the_documented_order",
    ),
}

BAD_REQUEST_CHECK = """        if action not in ("read", "clear") or (action == "clear" and seq is None):
            return self._json(400, body_json("bad_request", 0))
"""
NOT_READY_CHECK = """        if self.autopid != "run":
            return self._json(503, body_json("not_ready", 0))
"""
TOO_OLD_CHECK = """        if not read_finished or elapsed(now_ms, self._ended_ms) > CLEAR_MAX_AGE_MS:
            return "read_required"
"""
STALE_CHECK = """        if seq != self.seq and "clear_ignores_seq" not in self.faults:
            return "stale_seq"
"""
NOT_FOUND = 'return 404, [("Content-Type", "text/html")], b"Nothing matches the given URI"'
JSON_HEADERS = '        headers = [("Content-Type", "application/json"), ("Cache-Control", "no-store")]'
NO_RESULT = 'return 204, [("Cache-Control", "no-store")], b""'
STATE_ANSWER = "return self._json(200, state_json(values, dtc, self.faults))"
FORBIDDEN_HEADER = """            return self._json(403, body_json("forbidden", 0))
        if "host_not_checked"""
ACCEPTED_OR_REFUSED = "return self._json(202 if reason is None else 409, body_json(reason, number))"
CORS = '[("Access-Control-Allow-Origin", "*")]'
HOST_NAME = r"wican_[0-9a-f]{1,32}\.local"
HOST_PORT = 'r"(?::[0-9]{1,5})?", host,'
SPLIT = 'for part in query.split("&"):'
ENGINE_RUNS = "return self.rpm >= CLEAR_MAX_RPM"
SPEED_CHECK = 'if not 0 < arguments.speed < float("inf"):'
FAULT_ARGUMENT = 'parser.add_argument("--fault", action="append", default=[], choices=list(FAULTS),'
NEW_ADAPTER = "adapter = Adapter(arguments.scenario, arguments.fault, clock=RealClock(arguments.speed))"
POLL = 'self._poll_from = now if self.autopid == "run" and self._scan is None else None'
UNCHANGED_POLLING = "            if (self.ignition, self.autopid) == before:"
SLEEP = "return max(0, self.sleep_after_s - (now - self._off_since) // 1000)"
ERROR_END = "        self._rules.error(reason, self._since_boot(now))"
RESTART = "        self._result = None\n        self._scan = None\n        self._events = []\n"
DROP_BODY = '        if re.fullmatch(r"[0-9]{1,9}", length) is not None:'
NEW_RULES = "self._rules = ScanRules(self._random.getrandbits(32) if seq_seed is None else seq_seed, self.faults)"
UNIT_PROGRESS = "self._rules.progress(index + 1, len(ECUS), name)"
MOVE = "value += MOVING[name] * (passes % 5 - 2)"
STATUS_AND_CODES = """        entry["status"] = status
        entry["dtcs"] = [dtc_entry(protocol, code) for code in codes]
"""

# Changes of the mock source for the tests no fault switch reaches:
# (name, text in mock_wican.py, replacement, tests that have to fail). A name with a dot is a test of
# another class than Contract, e.g. "Fixtures.test_car_config".
CHANGES = [
    # All answers
    ("answer_with_whitespace", """return '{"accepted":true,"seq":%d}' % seq""",
     """return '{"accepted": true, "seq": %d}' % seq""",
     ("test_answers_to_post_are_json_without_whitespace", "test_read_is_accepted_with_202_and_its_number",
      "Fixtures.test_answers_to_post")),
    ("dtc_with_whitespace", """'"name":"%s","reason":"%s","age_s":%d,""", """'"name": "%s","reason":"%s","age_s":%d,""",
     ("test_state_is_json_in_utf8_without_whitespace", "test_running_state_is_the_fixture",
      "Fixtures.test_dtc_states")),
    ("answers_cached", JSON_HEADERS, '        headers = [("Content-Type", "application/json")]',
     ("test_state_and_result_are_not_cached", "test_stored_result_is_not_cached",
      "test_answers_to_post_are_not_cached", "test_not_ready_is_not_cached")),
    ("empty_result_cached", NO_RESULT, 'return 204, [], b""', ("test_state_and_result_are_not_cached",)),
    ("cors_for_everyone", JSON_HEADERS, JSON_HEADERS[:-1] + ', ("Access-Control-Allow-Origin", "*")]',
     ("test_no_cors_headers_are_sent",)),
    ("preflight_answered", NOT_FOUND,
     NOT_FOUND.replace('"text/html")]', '"text/html"), ("Access-Control-Allow-Headers", "x-wican-dtc")]'),
     ("test_preflight_for_the_header_is_not_answered",)),
    ("numbers_not_limited", 'return "%d" % min(value, NUMBER_MAX)', 'return "%d" % value',
     ("test_numbers_are_integers_below_2_31", "Fixtures.test_states_of_the_firmware_tests")),

    # GET /api/state
    ("state_is_202", STATE_ANSWER, STATE_ANSWER.replace("200", "202"),
     ("test_state_is_answered_with_200", "test_state_is_answered_with_200_in_every_situation")),
    ("state_is_503_while_starting", STATE_ANSWER, STATE_ANSWER.replace("200", '200 if self.autopid == "run" else 503'),
     ("test_state_is_answered_with_200_in_every_situation",)),
    ("upstream_has_the_api", 'if path.startswith("/api/") and not self.api:', "if False:",
     ("test_firmware_without_the_api_answers_404", "Scenarios.test_every_scenario_can_be_started")),
    ("upstream_has_no_path_at_all", 'if path.startswith("/api/") and not self.api:', "if not self.api:",
     ("test_firmware_without_the_api_has_the_upstream_paths",)),
    ("api_version", '("api", "1"),', '("api", "1.0"),', ("test_api_is_1", "Fixtures.test_state_example_of_api_md")),
    ("fw_not_quoted", '("fw", text(values["fw"])),', '("fw", values["fw"]),',
     ("test_id_fw_and_git_are_texts", "test_id_fw_and_git_are_those_of_the_device")),
    ("quote_not_escaped", """"\\\\" + char if char in '"\\\\' else char for""", "char for",
     ("test_text_fields_are_escaped_and_lose_control_characters",)),
    ("control_characters_kept", "for char in text if ord(char) >= 0x20)", "for char in text)",
     ("test_text_fields_are_escaped_and_lose_control_characters",)),
    ("boot_is_0", '("boot", number(values["boot"])),', '("boot", "0"),',
     ("test_boot_is_a_number_from_1_to_2_31_minus_1", "test_boot_is_chosen_at_random", "test_restart_changes_boot")),
    ("restart_keeps_boot", "self._power_on(self._catch_up())", "self._power_on(self._catch_up(), self._boot)",
     ("test_boot_is_chosen_at_random", "test_restart_changes_boot")),
    ("restart_keeps_the_state", NEW_RULES, NEW_RULES.replace("= ScanRules", '= getattr(self, "_rules", None) or ScanRules'),
     ("test_restart_loses_request_and_result",)),
    ("up_in_1024_ms", '"up": self._since_boot(now) // 1000,', '"up": self._since_boot(now) // 1024,',
     ("test_up_counts_the_seconds_since_boot",)),
    ("up_since_power_on", "        self._boot_at = now\n", "        self._boot_at = 0\n",
     ("test_up_starts_at_0_with_a_restart",)),
    ("autopid_in_capitals", '"autopid": self.autopid,', '"autopid": self.autopid.upper(),',
     ("test_autopid_is_off_starting_or_run", "test_autopid_is_starting_until_the_task_is_in_its_loop",
      "test_autopid_is_off_if_the_protocol_is_not_autopid")),
    ("one_value_too_many", '"pids": len(PARAMETERS) if', '"pids": len(PARAMETERS) + 1 if',
     ("test_pids_is_the_number_of_values_of_the_profile",)),
    ("no_profile_while_starting", 'len(PARAMETERS) if self.autopid != "off" else 0,',
     'len(PARAMETERS) if self.autopid == "run" else 0,',
     ("Scenarios.test_starting_adapter_has_its_profile_but_no_values",)),
    ("profile_without_autopid", 'len(PARAMETERS) if self.autopid != "off" else 0,', "len(PARAMETERS),",
     ("Scenarios.test_adapter_without_autopid_has_no_profile",)),
    ("ecu_on_off", """("ecu", '"online"' if values["ecu_online"] else '"offline"'),""",
     """("ecu", '"on"' if values["ecu_online"] else '"off"'),""",
     ("test_ecu_is_online_or_offline", "test_ecu_is_online_with_the_ignition_on",
      "test_ecu_is_offline_with_the_ignition_off")),
    ("ecu_online_without_ignition", '"ecu_online": self.ignition and self.autopid == "run",',
     '"ecu_online": self.autopid == "run",', ("test_ecu_is_offline_with_the_ignition_off",)),
    ("ecu_never_online", '"ecu_online": self.ignition and self.autopid == "run",', '"ecu_online": False,',
     ("test_ecu_is_online_with_the_ignition_on",)),
    ("pass_goes_back", '"pass": passes,', '"pass": 1000 - passes,', ("test_pass_never_goes_back",)),
    ("pass_does_not_count", "passes += spent // (REQUEST_MS * len(PARAMETERS))", "passes += 0",
     ("test_pass_moves_while_requests_are_answered",)),
    ("answers_without_ignition", "        if self._poll_from is not None and self.ignition:",
     "        if self._poll_from is not None:",
     ("test_pass_stands_still_while_nothing_is_answered", "test_rx_age_ms_is_minus_1_without_an_answer_since_boot")),
    ("values_move_with_the_time", MOVE, "value += MOVING[name] * (now // 400 % 5 - 2)",
     ("test_values_change_only_when_pass_moved",)),
    ("values_stand_still", MOVE, "value += 0", ("test_values_change_only_when_pass_moved",)),
    ("rx_age_stays_0", '"rx_age_ms": -1 if last_answer is None else now - last_answer,',
     '"rx_age_ms": -1 if last_answer is None else 0,', ("test_rx_age_ms_counts_from_the_last_answer",)),
    ("none_is_minus_2", 'return "-1" if value < 0 else number(value)', 'return "-2" if value < 0 else number(value)',
     ("test_rx_age_ms_is_minus_1_or_a_time", "test_sleep_in_s_is_minus_1_or_a_number_of_seconds",
      "test_sleep_in_s_is_minus_1_while_not_counting_down")),
    ("mqtt_on", '"mqtt": self.mqtt,', '"mqtt": "on" if self.mqtt == "connected" else self.mqtt,',
     ("test_mqtt_is_off_connected_or_disconnected", "test_mqtt_follows_the_broker_connection")),
    ("volts_with_two_decimals", '"%d.%d" % divmod((millivolts + 50) // 100, 10)', '"%.2f" % (millivolts / 1000)',
     ("test_batt_v_has_one_decimal_or_is_minus_1", "test_batt_v_is_the_battery_voltage")),
    ("volts_cut_off", "divmod((millivolts + 50) // 100, 10)", "divmod(millivolts // 100, 10)",
     ("test_batt_v_is_the_battery_voltage",)),
    ("volts_not_measured_are_0", 'return "-1" if millivolts < 0 else', 'return "0.0" if millivolts < 0 else',
     ("test_batt_v_is_minus_1_if_not_measured",)),
    ("sleep_does_not_count", "return max(0, self.sleep_after_s - (now - self._off_since) // 1000)",
     "return self.sleep_after_s", ("test_sleep_in_s_counts_down",)),
    ("heap_min_above_heap", '"heap_min": 48000,', '"heap_min": 68000,', ("test_heap_min_is_not_above_heap",)),

    # dtc
    ("idle_with_an_action", '        self.state = "idle"\n        self.action = ""',
     '        self.state = "idle"\n        self.action = "read"',
     ("test_dtc_is_consistent", "test_dtc_is_idle_before_the_first_request")),
    ("always_supported", 'self._rules.json(self.supported and self.autopid != "off", self._since_boot(now))',
     "self._rules.json(True, self._since_boot(now))", ("test_supported_is_false_without_a_fault_memory_table",)),
    ("scan_without_a_table_runs", """        if not self.supported:
            self._rules.error("not_supported", self._since_boot(now))
            return
""", "", ("test_scan_without_a_fault_memory_table_ends_with_not_supported",)),
    ("scan_takes_no_time", "        self._at(now + duration_ms, action, scan=True)", "        self._at(now, action, scan=True)",
     ("test_request_is_queued_then_running_then_done", "test_answer_comes_at_once_the_scan_follows",
      "test_read_takes_about_35_s", "test_ecu_counts_from_0_to_total_while_running")),
    ("scan_takes_73_s", "UNIT_MS = 1900", "UNIT_MS = 4000", ("test_read_takes_about_35_s",)),
    ("source_is_always_mqtt", 'self.src = "http" if http else "mqtt"', 'self.src = "mqtt"',
     ("test_action_and_src_are_those_of_the_last_accepted_request",)),
    ("source_is_always_http", 'self.src = "http" if http else "mqtt"', 'self.src = "http"',
     ("test_scan_started_over_mqtt_is_visible",)),
    ("action_is_always_read", 'self.action = "clear" if clear else "read"', 'self.action = "read"',
     ("test_action_is_clear_for_a_clear",)),
    ("number_used_twice", "self._upcoming = 1 if self._upcoming >= self._seq_end else self._upcoming + 1", "pass",
     ("test_requests_get_different_numbers",)),
    ("request_keeps_old_step", '        self.src = "http" if http else "mqtt"\n        self.step = 0\n        self.total = 0\n',
     '        self.src = "http" if http else "mqtt"\n', ("test_ecu_and_total_are_0_while_queued",)),
    ("step_one_behind", UNIT_PROGRESS, "self._rules.progress(index, len(ECUS), name)",
     ("test_ecu_counts_from_0_to_total_while_running", "test_name_is_the_control_unit_being_processed")),
    ("name_not_reported", UNIT_PROGRESS, "self._rules.progress(index + 1, len(ECUS), None)",
     ("test_name_is_the_control_unit_being_processed", "test_running_state_is_the_fixture")),
    ("request_keeps_old_reason", '        self.name = ""\n        self.reason = ""\n        self._accepted_ms',
     '        self.name = ""\n        self._accepted_ms', ("test_new_request_clears_the_reason",)),
    ("reason_without_an_error", '        self.name = ""\n        self.reason = ""\n        self._accepted_ms',
     '        self.name = ""\n        self.reason = "none"\n        self._accepted_ms',
     ("test_reason_is_empty_without_an_error",)),
    ("age_while_running", "// 1000 if ended else 0, self.count", "// 1000, self.count",
     ("test_age_s_is_0_until_the_request_has_ended",)),
    ("age_does_not_count", "// 1000 if ended else 0, self.count", "// 1000000 if ended else 0, self.count",
     ("test_age_s_counts_the_seconds_since_done", "test_age_s_counts_the_seconds_since_an_error")),
    ("count_not_stored", 'self._rules.done(min(scan["count"], 0xFFFF), self._since_boot(now))',
     "self._rules.done(0, self._since_boot(now))",
     ("test_count_is_the_number_of_trouble_codes_in_the_result", "test_count_includes_omitted_codes")),
    ("result_seq_at_pickup", '        self.state = "running"\n        return True',
     '        self.state = "running"\n        self.result_seq = self.seq\n        return True',
     ("test_result_seq_is_0_without_a_result",)),
    ("result_seq_is_1", "            self.result_seq = self.seq\n            self.count = count",
     "            self.result_seq = 1\n            self.count = count",
     ("test_result_seq_is_the_number_of_the_finished_scan",)),

    # POST /api/dtc
    ("body_read_as_the_next_request", DROP_BODY, "        if False:", ("test_request_body_is_ignored",)),
    ("host_without_port", 'r"(?::[0-9]{1,5})?", host,', 'r"", host,',
     ("test_post_with_an_ipv4_address_as_host_is_allowed", "test_post_with_the_mdns_name_as_host_is_allowed")),
    ("host_name_refused", r"|wican_[0-9a-f]{1,32}\.local)", ")",
     ("test_post_with_the_mdns_name_as_host_is_allowed",)),
    ("host_name_in_lower_case_only", '", host, re.IGNORECASE | re.ASCII)', '", host, re.ASCII)',
     ("test_post_with_the_mdns_name_as_host_is_allowed",)),
    ("host_with_any_number", "return all(part is None or int(part) <= 255 for part in match.groups())", "return True",
     ("test_post_with_a_foreign_host_is_forbidden",)),
    ("unknown_action_is_a_read", 'if action not in ("read", "clear") or', 'if action == "nothing" or',
     ("test_post_without_action_is_a_bad_request", "test_post_with_an_unknown_action_is_a_bad_request")),
    ("clear_without_seq", '(action == "clear" and seq is None)', "False",
     ("test_clear_without_seq_is_a_bad_request", "test_clear_with_a_malformed_seq_is_a_bad_request")),
    ("seq_range_not_checked", "return value if 1 <= value <= SEQ_MAX else None", "return value",
     ("test_clear_with_a_malformed_seq_is_a_bad_request",)),
    ("read_needs_seq", '(action == "clear" and seq is None)', "seq is None", ("test_read_ignores_seq",)),
    ("not_ready_before_bad_request", BAD_REQUEST_CHECK + "\n" + NOT_READY_CHECK, NOT_READY_CHECK + "\n" + BAD_REQUEST_CHECK,
     ("test_bad_request_is_decided_before_not_ready",)),
    ("post_while_not_ready", NOT_READY_CHECK + "\n        reason, number",
     NOT_READY_CHECK.replace('self.autopid != "run"', "False") + "\n        reason, number",
     ("test_not_ready_while_autopid_is_starting", "test_not_ready_while_autopid_is_off",
      "test_not_ready_is_decided_before_the_rules_of_the_scan", "test_not_ready_is_not_cached")),
    ("empty_list_is_cleared", '        if self.count == 0:\n            return "nothing_to_clear"',
     '        if False:\n            return "nothing_to_clear"',
     ("test_clear_of_a_read_without_trouble_codes_is_refused",)),
    ("clear_only_500_s", "CLEAR_MAX_AGE_MS = 600 * 1000", "CLEAR_MAX_AGE_MS = 500 * 1000",
     ("test_clear_599_s_after_the_read_is_accepted",)),
    ("clear_for_700_s", "CLEAR_MAX_AGE_MS = 600 * 1000", "CLEAR_MAX_AGE_MS = 700 * 1000",
     ("test_clear_601_s_after_the_read_is_refused",)),
    ("stale_before_too_old", TOO_OLD_CHECK + STALE_CHECK,
     TOO_OLD_CHECK.replace(" or elapsed(now_ms, self._ended_ms) > CLEAR_MAX_AGE_MS", "") + STALE_CHECK
     + TOO_OLD_CHECK.replace("not read_finished or ", ""),
     ("test_too_old_is_decided_before_stale",)),
    ("clear_only_after_a_read_over_http", 'read_finished = self.state == "done" and self.action == "read"',
     'read_finished = self.state == "done" and self.action == "read" and self.src == "http"',
     ("test_clear_of_a_read_over_mqtt_is_accepted",)),
    ("expiry_at_20_s", "elapsed(now_ms, self._accepted_ms) > HTTP_EXPIRY_MS", "elapsed(now_ms, self._accepted_ms) >= HTTP_EXPIRY_MS",
     ("test_request_picked_up_after_20_s_runs",)),
    ("nobody_sends_over_mqtt", "            self._at(now + MQTT_FIRST_MS, self._mqtt_again)", "            pass",
     ("test_post_during_a_scan_started_over_mqtt_is_busy", "test_result_of_a_scan_started_over_mqtt_is_stored")),
    ("scan_with_the_ignition_off", '        if not self.ignition:\n            self._fail("ecu_offline", now)',
     '        if False:\n            self._fail("ecu_offline", now)',
     ("test_read_with_the_ignition_off_ends_with_ecu_offline",)),

    # GET /api/dtc/result
    ("empty_result_is_200", NO_RESULT, 'return 200, [("Cache-Control", "no-store")], b"{}"',
     ("test_result_belongs_to_result_seq", "test_result_is_204_and_empty_before_a_scan_has_finished")),
    ("result_without_its_number", 'self._json(200, self._result, [("X-DTC-Seq", "%d" % self._rules.result_seq)])',
     "self._json(200, self._result)",
     ("test_result_is_there_when_done_with_the_number_of_the_request",
      "test_result_of_a_scan_started_over_mqtt_is_stored")),
    ("result_while_not_ready", '    def _stored_result(self):\n        if self.autopid != "run":',
     "    def _stored_result(self):\n        if False:", ("test_result_is_503_while_autopid_is_not_active",)),
    ("scan_drops_the_result", "        self._rules.progress(0, len(ECUS), None)\n",
     "        self._rules.progress(0, len(ECUS), None)\n        self._result = None\n",
     ("test_result_is_kept_until_the_next_scan_finishes",)),
    ("status_behind_the_codes", STATUS_AND_CODES, "".join(reversed(STATUS_AND_CODES.splitlines(True))),
     ("test_result_of_a_read_has_the_documented_format", "Fixtures.test_result_without_trouble_codes")),
    ("result_of_a_clear_says_read", '("action", "clear" if clear else "read"),', '("action", "read"),',
     ("test_clear_with_the_number_of_the_last_read_is_accepted", "Fixtures.test_result_of_a_clear")),
    ("cleared_not_reported", '                entry["cleared"] = unit["confirms"]\n', "",
     ("test_result_of_a_clear_says_what_was_cleared", "Fixtures.test_result_of_a_clear")),
    ("cleared_is_always_true", 'entry["cleared"] = unit["confirms"]', 'entry["cleared"] = True',
     ("test_result_of_a_clear_says_what_was_cleared", "Fixtures.test_result_of_a_clear")),
    ("result_up_to_6_kb", "RESULT_MAX_BYTES = 5 * 1024 - 1", "RESULT_MAX_BYTES = 6 * 1024 - 1",
     ("test_result_is_shortened_with_dtcs_omitted", "Fixtures.test_shortened_result")),
    ("omitted_codes_not_counted", '        largest["dtcs_omitted"] = len(largest["dtcs"])\n', "",
     ("test_result_is_shortened_with_dtcs_omitted", "Fixtures.test_shortened_result")),
    ("longest_list_not_omitted_first", 'largest = max(ecus, key=lambda ecu: len(ecu["dtcs"]))',
     'largest = max(ecus, key=lambda ecu: len(ecu["dtcs"]) % 76)', ("Fixtures.test_shortened_result",)),
    ("unit_with_another_id", '("N10 SAM", "662", "KWP"),', '("N10 SAM", "663", "KWP"),',
     ("test_result_of_a_read_has_the_documented_format", "Fixtures.test_units_are_those_of_the_firmware")),

    # Other requests
    ("values_nested", """values.append('"%s":%s' % (name, number_text(value)))""",
     """values.append('"%s":{"value":%s}' % (name, number_text(value)))""",
     ("test_autopid_data_is_a_flat_object", "Fixtures.test_values_with_the_ignition_on")),
    ("values_with_one_decimal", 'text = "%.2f" % value', 'text = "%.1f" % value',
     ("Fixtures.test_values_with_the_ignition_on",)),
    ("values_without_ignition", 'if not valid or self.autopid == "off":', 'if self.autopid == "off":',
     ("test_autopid_data_is_empty_while_no_value_is_valid", "test_autopid_data_loses_its_values_with_the_ignition",
      "Fixtures.test_values_with_the_ignition_off")),
    ("unit_before_class", '(("class", kind), ("unit", unit))', '(("unit", unit), ("class", kind))',
     ("test_load_car_config_has_class_and_unit_of_every_value", "Fixtures.test_car_config")),
    ("no_profile_without_ignition", '        if self.autopid == "off":\n            return "{}"\n        config',
     '        if self.autopid == "off" or not self.ignition:\n            return "{}"\n        config',
     ("test_load_car_config_works_with_the_ignition_off",)),
    ("one_request_per_connection", 'protocol_version = "HTTP/1.1"', 'protocol_version = "HTTP/1.0"',
     ("test_one_connection_serves_many_requests",)),
    ("unknown_path_is_400", NOT_FOUND, NOT_FOUND.replace("404", "400"),
     ("test_unknown_path_is_404", "test_everything_else_is_404", "test_firmware_without_the_api_answers_404")),
    ("post_to_state", 'if method == "GET" and path == "/api/state":', 'if path == "/api/state":',
     ("test_everything_else_is_404",)),
    ("options_not_answered", "do_PATCH = do_OPTIONS = do_HEAD = _answer", "do_PATCH = do_HEAD = _answer",
     ("test_everything_else_is_404",)),

    # All answers, again
    ("cors_on_refusals", FORBIDDEN_HEADER, FORBIDDEN_HEADER.replace('("forbidden", 0))', '("forbidden", 0), %s)' % CORS),
     ("test_answers_to_post_carry_no_cors_headers",)),
    ("cors_on_accepted", ACCEPTED_OR_REFUSED, ACCEPTED_OR_REFUSED[:-1] + ", %s)" % CORS,
     ("test_answers_to_post_carry_no_cors_headers", "Scenarios.test_answer_has_the_headers_of_the_firmware_and_no_others")),
    ("answers_declared_as_text", JSON_HEADERS, JSON_HEADERS.replace("application/json", "text/plain"),
     ("test_answers_are_declared_as_json", "Scenarios.test_answer_has_the_headers_of_the_firmware_and_no_others")),
    ("rx_age_not_limited", 'return "-1" if value < 0 else number(value)', 'return "-1" if value < 0 else "%d" % value',
     ("test_rx_age_ms_is_at_most_2_31_minus_1",)),

    # POST /api/dtc, again: the limits of main/dtc_api.c
    ("forbidden_with_the_number", FORBIDDEN_HEADER, FORBIDDEN_HEADER.replace('("forbidden", 0)', '("forbidden", self._rules.seq)'),
     ("test_refusals_before_the_rules_carry_the_number_0",)),
    ("foreign_host_with_the_number", '            return self._json(403, body_json("forbidden", 0))\n\n        action',
     '            return self._json(403, body_json("forbidden", self._rules.seq))\n\n        action',
     ("test_refusals_before_the_rules_carry_the_number_0",)),
    ("bad_request_with_the_number", 'return self._json(400, body_json("bad_request", 0))',
     'return self._json(400, body_json("bad_request", self._rules.seq))', ("test_refusals_before_the_rules_carry_the_number_0",)),
    ("host_port_of_6_digits", HOST_PORT, HOST_PORT.replace("{1,5}", "{1,6}"), ("test_post_with_a_foreign_host_is_forbidden",)),
    ("host_port_empty", HOST_PORT, HOST_PORT.replace("{1,5}", "{0,5}"), ("test_post_with_a_foreign_host_is_forbidden",)),
    ("host_part_of_4_digits", r"\.([0-9]{1,3})|wican_", r"\.([0-9]{1,4})|wican_", ("test_post_with_a_foreign_host_is_forbidden",)),
    ("host_first_part_of_4_digits", r"(?:([0-9]{1,3})\.", r"(?:([0-9]{1,4})\.", ("test_post_with_a_foreign_host_is_forbidden",)),
    ("host_id_empty", HOST_NAME, HOST_NAME.replace("{1,32}", "{0,32}"), ("test_post_with_a_foreign_host_is_forbidden",)),
    ("host_id_of_33_digits", HOST_NAME, HOST_NAME.replace("{1,32}", "{1,33}"), ("test_post_with_a_foreign_host_is_forbidden",)),
    ("host_id_of_any_letters", HOST_NAME, HOST_NAME.replace("a-f", "a-z"), ("test_post_with_a_foreign_host_is_forbidden",)),
    ("host_id_without_0", HOST_NAME, HOST_NAME.replace("0-9", "1-9"), ("test_post_with_the_mdns_name_as_host_is_allowed",)),
    ("host_any_character_for_the_dot", HOST_NAME, HOST_NAME.replace("\\.", "."), ("test_post_with_a_foreign_host_is_forbidden",)),
    ("host_any_character_for_the_underscore", HOST_NAME, HOST_NAME.replace("wican_", "wican."),
     ("test_post_with_a_foreign_host_is_forbidden",)),
    ("host_start_is_enough", '    match = re.fullmatch(r"(?:([0-9]{1,3})', '    match = re.match(r"(?:([0-9]{1,3})',
     ("test_post_with_a_foreign_host_is_forbidden",)),
    ("semicolon_separates_parameters", SPLIT, 'for part in query.replace(";", "&").split("&"):',
     ("test_post_with_an_unknown_action_is_a_bad_request",)),
    ("last_parameter_wins", SPLIT, 'for part in reversed(query.split("&")):',
     ("test_post_with_an_unknown_action_is_a_bad_request",)),
    ("parameter_name_as_prefix", "        if key == name:", "        if key.startswith(name):",
     ("test_post_without_action_is_a_bad_request", "test_clear_without_seq_is_a_bad_request")),
    ("clear_at_50_rpm", ENGINE_RUNS, ENGINE_RUNS.replace(">=", ">"), ("test_clear_runs_below_50_rpm_and_is_refused_from_50_rpm_on",)),
    ("clear_refused_at_49_rpm", "CLEAR_MAX_RPM = 50", "CLEAR_MAX_RPM = 49",
     ("test_clear_runs_below_50_rpm_and_is_refused_from_50_rpm_on",)),
    ("clear_with_the_ignition_off_goes_on", '        if not self.ignition:\n            self._fail("ecu_offline", now)',
     '        if not self.ignition and not self._scan["clear"]:\n            self._fail("ecu_offline", now)',
     ("test_clear_with_the_ignition_off_ends_with_ecu_offline",)),

    # Scenarios
    ("unknown_fault_ignored", '        if unknown:\n            raise ValueError("unknown fault',
     '        if False:\n            raise ValueError("unknown fault',
     ("Scenarios.test_unknown_scenario_fault_or_setting_is_refused",)),
    ("unknown_setting_ignored", "            if name not in SETTINGS:", "            if False:",
     ("Scenarios.test_unknown_scenario_fault_or_setting_is_refused",)),
    ("boot_and_number_not_taken", "self._power_on(self.clock.now_ms(), self.boot, self.seq_seed)",
     "self._power_on(self.clock.now_ms())", ("Scenarios.test_boot_and_the_first_number_can_be_set",)),
    ("restart_every_120_s", "self._at(now + self.restart_every_s * 1000, self._power_on)",
     "self._at(now + self.restart_every_s * 2000, self._power_on)",
     ("Scenarios.test_restart_scenario_restarts_every_60_s",)),
    ("mqtt_command_after_5_s", "MQTT_FIRST_MS = 3000", "MQTT_FIRST_MS = 5000",
     ("Scenarios.test_mqtt_scan_scenario_reads_3_s_after_boot_and_then_every_60_s",)),
    ("mqtt_command_only_once", "        self._at(now + self.mqtt_every_s * 1000, self._mqtt_again)", "        pass",
     ("Scenarios.test_mqtt_scan_scenario_reads_3_s_after_boot_and_then_every_60_s",)),
    ("values_of_a_standing_engine", "        running = self._engine_runs()\n", "        running = False\n",
     ("Scenarios.test_engine_running_scenario_has_the_values_of_an_idling_engine",)),
    ("battery_not_charged", "return 14100 if self._engine_runs() else 12400", "return 12400",
     ("Scenarios.test_battery_voltage_follows_the_situation",)),
    ("values_valid_at_once", "valid = valid or spent >= REQUEST_MS * len(PARAMETERS)", "valid = True",
     ("Scenarios.test_values_are_valid_after_the_first_complete_pass",
      "Scenarios.test_values_come_back_one_pass_after_the_ignition")),
    ("polling_during_a_scan", "        self._fold(now)\n        self._poll_from = None\n", "        self._fold(now)\n",
     ("Scenarios.test_polling_stands_still_during_a_scan",)),
    ("seq_of_any_length", 're.fullmatch(r"[0-9]{1,10}", text)', 're.fullmatch(r"[0-9]+", text)',
     ("Scenarios.test_seq_has_10_digits_at_most_as_in_the_firmware",)),
    ("answers_without_ignition_during_a_scan", 'status = unit["status"] if self.ignition else "no_response"',
     'status = unit["status"]',
     ("Scenarios.test_control_units_do_not_answer_once_the_ignition_goes_off_during_a_scan",)),
    ("result_measured_in_characters", 'if len(text.encode("utf-8")) <= RESULT_MAX_BYTES', "if len(text) <= RESULT_MAX_BYTES",
     ("Scenarios.test_result_is_shortened_only_above_5119_bytes",)),
    ("result_of_5119_bytes_shortened", "<= RESULT_MAX_BYTES or", "< RESULT_MAX_BYTES or",
     ("Scenarios.test_result_is_shortened_only_above_5119_bytes",)),
    ("result_of_5120_bytes_kept", "RESULT_MAX_BYTES = 5 * 1024 - 1", "RESULT_MAX_BYTES = 5 * 1024",
     ("Scenarios.test_result_is_shortened_only_above_5119_bytes",)),
    ("failed_scan_stays_in_the_way", "        self._scan = None\n" + ERROR_END, ERROR_END,
     ("Scenarios.test_polling_goes_on_after_a_scan_that_failed",)),
    ("no_polling_after_a_failed_scan", ERROR_END + "\n        self._poll(now)", ERROR_END,
     ("Scenarios.test_polling_goes_on_after_a_scan_that_failed",)),
    ("expired_request_stops_the_polling", "        if not self._rules.pickup(self._since_boot(now)):\n            return",
     "        if not self._rules.pickup(self._since_boot(now)):\n            self._poll_from = None\n            return",
     ("Scenarios.test_polling_goes_on_after_a_scan_that_failed",)),
    ("scan_prepared_at_once", "PREPARE_MS = 40", "PREPARE_MS = 0",
     ("Scenarios.test_scan_is_running_without_a_number_of_control_units_while_it_prepares",
      "Fixtures.test_result_without_trouble_codes")),
    ("answer_at_once_after_boot", "            if spent >= REQUEST_MS:", "            if True:",
     ("Scenarios.test_values_are_valid_after_the_first_complete_pass",)),
    ("sleep_counts_from_boot", "                self._valid = False\n                self._off_since = now",
     "                self._valid = False", ("Scenarios.test_sleep_countdown_starts_when_the_ignition_goes_off",)),
    ("sleep_goes_below_0", SLEEP, SLEEP.replace("max(0, ", "(") , ("Scenarios.test_sleep_countdown_starts_when_the_ignition_goes_off",)),
    ("sleep_with_the_ignition_on", "if self.sleep_after_s is None or self.ignition:", "if self.sleep_after_s is None:",
     ("Scenarios.test_sleep_countdown_starts_when_the_ignition_goes_off", "Scenarios.test_other_settings_leave_the_values_alone")),
    ("every_setting_restarts_the_polling", UNCHANGED_POLLING, "            if False:",
     ("Scenarios.test_other_settings_leave_the_values_alone",)),
    ("no_setting_restarts_the_polling", UNCHANGED_POLLING, "            if True:",
     ("Scenarios.test_values_come_when_the_task_reaches_its_loop", "Scenarios.test_values_come_back_one_pass_after_the_ignition",
      "test_rx_age_ms_is_at_most_2_31_minus_1")),
    ("polling_without_the_task", POLL, POLL.replace('self.autopid == "run" and ', ""),
     ("Scenarios.test_no_polling_while_the_task_is_not_in_its_loop", "Scenarios.test_values_come_when_the_task_reaches_its_loop")),
    ("polling_during_a_scan_after_a_setting", POLL, POLL.replace(" and self._scan is None", ""),
     ("Scenarios.test_setting_during_a_scan_does_not_start_the_polling",)),
    ("restart_lets_the_scan_go_on", RESTART, RESTART.replace("= None\n        self._events = []", '= getattr(self, "_scan", None)\n'
                                                             '        self._events = getattr(self, "_events", [])'),
     ("Scenarios.test_restart_during_a_scan_drops_it",)),
    ("restart_clears_the_vehicle", RESTART, "        self._memory = fault_memory(self.memory)\n" + RESTART,
     ("Scenarios.test_restart_keeps_the_fault_memory_of_the_vehicle",)),
    ("request_in_absolute_time", "self._rules.begin(clear, http, seq, self._since_boot(now))", "self._rules.begin(clear, http, seq, now)",
     ("Scenarios.test_times_count_from_the_restart",)),
    ("pickup_in_absolute_time", "if not self._rules.pickup(self._since_boot(now)):", "if not self._rules.pickup(now):",
     ("Scenarios.test_times_count_from_the_restart",)),
    ("end_in_absolute_time", 'self._rules.done(min(scan["count"], 0xFFFF), self._since_boot(now))',
     'self._rules.done(min(scan["count"], 0xFFFF), now)', ("Scenarios.test_times_count_from_the_restart",)),
    ("error_in_absolute_time", ERROR_END, ERROR_END.replace("self._since_boot(now)", "now"),
     ("Scenarios.test_times_count_from_the_restart",)),
    ("not_supported_in_absolute_time", '            self._rules.error("not_supported", self._since_boot(now))',
     '            self._rules.error("not_supported", now)', ("Scenarios.test_times_count_from_the_restart",)),
    ("state_in_absolute_time", 'self.autopid != "off", self._since_boot(now))', 'self.autopid != "off", now)',
     ("Scenarios.test_times_count_from_the_restart",)),
    ("engine_speed_not_the_setting", '            if name == "ENGINE_RPM":\n                value = self.rpm\n', "",
     ("Scenarios.test_engine_speed_is_the_setting",)),
    ("host_in_any_alphabet", "re.IGNORECASE | re.ASCII)", "re.IGNORECASE)",
     ("Scenarios.test_letters_and_digits_outside_ascii_are_not_taken",)),
    ("seq_in_any_digits", 're.fullmatch(r"[0-9]{1,10}", text)', 're.fullmatch(r"\\d{1,10}", text)',
     ("Scenarios.test_letters_and_digits_outside_ascii_are_not_taken",)),
    ("power_on_settings_taken_later", "        if fixed:\n", "        if False:\n",
     ("Scenarios.test_settings_read_at_power_on_cannot_be_set_later",)),

    # The HTTP server of the mock
    ("head_with_a_body", '        if self.command == "HEAD":\n            body = b""\n', "",
     ("Scenarios.test_head_is_answered_without_a_body",)),
    ("server_header_sent", 'head = ["%s %d %s" % (self.protocol_version, status, REASONS[status])]',
     'head = ["%s %d %s" % (self.protocol_version, status, REASONS[status]), "Server: mock"]',
     ("Scenarios.test_answer_has_the_headers_of_the_firmware_and_no_others",)),
    ("odd_content_length_breaks_the_request", DROP_BODY, "        if length.isdigit():",
     ("Scenarios.test_request_with_an_odd_content_length_is_answered",)),
    ("body_dropped_in_part", "            self.rfile.read(int(length))", "            self.rfile.read(min(int(length), 65536))",
     ("Scenarios.test_request_with_an_odd_content_length_is_answered",)),
    ("server_thread_left_running", "        if self._thread is not None:\n            self._httpd.shutdown()\n            self._thread.join()\n",
     "", ("Scenarios.test_server_is_used_as_its_description_says",)),
    ("server_adapter_not_given_out", "        return self._httpd.adapter\n", "        return None\n",
     ("Scenarios.test_server_is_used_as_its_description_says",)),
    ("server_keeps_its_port", "            self._thread.join()\n        self._httpd.server_close()\n", "            self._thread.join()\n",
     ("Scenarios.test_server_is_used_as_its_description_says",)),
    ("server_host_as_asked", "self.host, self.port = self._httpd.server_address[:2]",
     "self.host, self.port = bind, self._httpd.server_address[1]", ("Scenarios.test_server_is_used_as_its_description_says",)),
    ("server_never_started_keeps_its_port", "            self._thread.join()\n        self._httpd.server_close()\n",
     "            self._thread.join()\n            self._httpd.server_close()\n",
     ("Scenarios.test_server_never_started_can_be_closed",)),

    # The mock as a program
    ("speed_not_checked", SPEED_CHECK, "if False:", ("CommandLine.test_unknown_scenario_or_fault_is_refused",)),
    ("speed_0_taken", SPEED_CHECK, SPEED_CHECK.replace("0 <", "0 <="), ("CommandLine.test_unknown_scenario_or_fault_is_refused",)),
    ("speed_nan_taken", SPEED_CHECK, "if arguments.speed <= 0:", ("CommandLine.test_unknown_scenario_or_fault_is_refused",)),
    ("speed_inf_taken", SPEED_CHECK, SPEED_CHECK.replace("speed <", "speed <="),
     ("CommandLine.test_unknown_scenario_or_fault_is_refused",)),
    ("port_not_checked", "if not 0 <= arguments.port <= 65535:", "if False:",
     ("CommandLine.test_unknown_scenario_or_fault_is_refused",)),
    ("scenario_not_checked", 'default="codes", choices=list(SCENARIOS), help="see --list")', 'default="codes", help="see --list")',
     ("CommandLine.test_unknown_scenario_or_fault_is_refused",)),
    ("fault_not_checked", FAULT_ARGUMENT, FAULT_ARGUMENT.replace(" choices=list(FAULTS),", ""),
     ("CommandLine.test_unknown_scenario_or_fault_is_refused",)),
    ("only_one_fault_taken", FAULT_ARGUMENT, FAULT_ARGUMENT.replace(' action="append",', ""),
     ("CommandLine.test_arguments_are_taken",)),
    ("default_port_8907", '"--port", type=int, default=8906,', '"--port", type=int, default=8907,',
     ("CommandLine.test_without_arguments_it_is_the_adapter_of_the_description",)),
    ("default_bind_everywhere", '"--bind", default="127.0.0.1",', '"--bind", default="0.0.0.0",',
     ("CommandLine.test_without_arguments_it_is_the_adapter_of_the_description",)),
    ("default_speed_2", '"--speed", type=float, default=1.0,', '"--speed", type=float, default=2.0,',
     ("CommandLine.test_without_arguments_it_is_the_adapter_of_the_description",)),
    ("list_without_faults", '        for name, text in FAULTS.items():\n            print("  %-24s %s" % (name, text))\n', "",
     ("CommandLine.test_list_names_every_scenario_and_fault",)),
    ("program_ignores_the_scenario", NEW_ADAPTER, NEW_ADAPTER.replace("arguments.scenario", '"no_codes"'),
     ("CommandLine.test_started_as_a_program_it_runs_a_scan_in_real_time",)),
    ("program_ignores_the_faults", NEW_ADAPTER, NEW_ADAPTER.replace("arguments.fault", "()"),
     ("CommandLine.test_started_as_a_program_it_runs_a_scan_in_real_time",)),
    ("program_listens_everywhere", "server = Server(adapter, arguments.bind, arguments.port,",
     'server = Server(adapter, "0.0.0.0", arguments.port,', ("CommandLine.test_started_as_a_program_it_runs_a_scan_in_real_time",)),
    ("program_is_quiet", "None if arguments.quiet else log)", "None)",
     ("CommandLine.test_started_as_a_program_it_runs_a_scan_in_real_time",)),
]

# A change of a fixture file for the tests that look at the files only:
# (name, file, text in it, replacement, tests of Fixtures that have to fail)
FIXTURE_CHANGES = [
    ("control_unit_renamed", "dtc_result_read_empty.json", "N10 SAM", "N10 SAM (front)",
     ("test_result_fixtures_follow_the_format", "test_result_without_trouble_codes")),
    ("code_count_wrong", "dtc_result_read_codes.json", '"dtc_count":5', '"dtc_count":4',
     ("test_result_fixtures_follow_the_format", "test_result_with_uds_and_kwp_codes")),
    ("kwp_code_with_active", "dtc_result_read_codes.json", '{"code":"9301","status":"60"}',
     '{"code":"9301","status":"60","active":true}',
     ("test_result_fixtures_follow_the_format", "test_result_with_uds_and_kwp_codes")),
    ("cleared_behind_status", "dtc_result_clear.json", '"cleared":false,"status":"ok"', '"status":"ok","cleared":false',
     ("test_result_fixtures_follow_the_format", "test_result_of_a_clear")),
    ("omitted_too_much", "dtc_result_shortened.json", '"dtcs_omitted":75', '"dtcs_omitted":3',
     ("test_result_fixtures_follow_the_format", "test_shortened_fixture_is_shortened_no_more_than_needed",
      "test_shortened_result")),
    ("value_missing", "autopid_data_ignition_on.json", '"LAMBDA":1,', "",
     ("test_value_fixtures_have_the_names_of_the_profile", "test_values_with_the_ignition_on")),
    ("value_with_three_decimals", "autopid_data_ignition_on.json", "67.31", "67.312",
     ("test_value_fixtures_have_the_names_of_the_profile", "test_values_with_the_ignition_on")),
    ("unit_wrong", "car_config_w906.json", '"OIL_LEVEL":{"class":"distance","unit":"mm"}',
     '"OIL_LEVEL":{"class":"distance","unit":"cm"}',
     ("test_car_config_fixture_is_the_profile", "test_car_config")),
]

# Changes of the lock in this file that keeps requests from a real adapter:
# (name, text in this file, replacement, tests of AdapterMode that have to fail). Every text ends with
# a line end, which is written as an escape here: so it does not find itself in this list.
LOCK_CHANGES = [
    ("request_not_checked", "            refusal = not_permitted(method, target, self.ALLOW_DTC, self.ALLOW_CLEAR)\n"
     "            if refusal is not None:\n                self.skipTest(refusal)\n\n        connection = self.connection\n",
     "            pass\n\n        connection = self.connection\n", ("test_request_a_test_did_not_announce_is_not_sent",)),
    ("paths_a_client_never_calls_are_asked", "    if path in NEVER:\n", "    if False:\n",
     ("test_what_may_be_sent", "test_request_a_test_did_not_announce_is_not_sent")),
    ("post_to_a_path_that_may_be_read", '    if method == "GET" and path in READ_ONLY:\n', "    if path in READ_ONLY:\n",
     ("test_what_may_be_sent",)),
    ("every_get_is_sent", '    if method == "GET" and path in READ_ONLY:\n', '    if method == "GET":\n', ("test_what_may_be_sent",)),
    ("scans_without_permission", "    if not allow_dtc:\n", "    if False:\n", ("test_what_may_be_sent",)),
    ("unknown_path_not_asked", '"/load_car_config", "/api/nothing")\n', '"/load_car_config")\n',
     ("test_what_may_be_sent", "test_without_permission_only_get_requests_to_five_paths_are_sent")),
    ("clears_without_permission", '    if "clear" in target and not allow_clear:\n', "    if False:\n",
     ("test_what_may_be_sent", "test_with_permission_for_scans_no_clear_is_sent")),
    ("clear_looked_for_in_the_path_only", '    if "clear" in target and not allow_clear:\n',
     '    if "clear" in path and not allow_clear:\n', ("test_what_may_be_sent",)),
    ("clear_only_with_a_number", '    if "clear" in target and not allow_clear:\n',
     '    if "action=clear&" in target and not allow_clear:\n', ("test_what_may_be_sent",)),
    ("scans_allowed_unless_forbidden", 'environ.get("WICAN_ALLOW_DTC") == "1",\n', 'environ.get("WICAN_ALLOW_DTC") != "0",\n',
     ("test_environment_decides_what_is_allowed",)),
    ("scans_allowed_by_any_text", 'environ.get("WICAN_ALLOW_DTC") == "1",\n', 'bool(environ.get("WICAN_ALLOW_DTC")),\n',
     ("test_environment_decides_what_is_allowed",)),
    ("clears_allowed_with_the_scans", 'environ.get("WICAN_ALLOW_DTC") == "1",\n            environ.get("WICAN_ALLOW_CLEAR")',
     'environ.get("WICAN_ALLOW_DTC") == "1",\n            environ.get("WICAN_ALLOW_DTC")',
     ("test_environment_decides_what_is_allowed",)),
    ("class_does_not_ask_the_environment", "    ADAPTER, ALLOW_DTC, ALLOW_CLEAR, SPEED = permissions(os.environ)\n",
     "    ADAPTER, ALLOW_DTC, ALLOW_CLEAR, SPEED = None, True, True, 1.0\n", ("test_environment_decides_what_is_allowed",)),
    ("no_waiting_at_the_adapter", "            time.sleep(seconds / self.SPEED)\n", "            pass\n",
     ("test_with_every_permission_scans_run_in_real_time",)),
    ("ended_connection_used_again", "select.select([connection.sock], [], [], 0)[0]:\n", "False:\n",
     ("test_connection_the_adapter_ended_is_opened_again",)),
]


def proven_classes():
    return {"Contract": Contract, "Fixtures": Fixtures, "Scenarios": Scenarios, "CommandLine": CommandLine}


def by_class(names):
    """{class: names of its tests} for names like "Fixtures.test_car_config"; without a dot: Contract."""
    classes = {}
    for name in names:
        case, _, test = name.rpartition(".")
        classes.setdefault(proven_classes()[case or "Contract"], []).append(test)
    return classes


class RedProof(unittest.TestCase):
    """Every test of Contract, Fixtures, Scenarios and CommandLine has to be seen failing: with a fault
    switch of the mock, with a change of the mock source or with a change of a fixture. Every test of
    AdapterMode has to be seen failing with a change of the lock in this file that keeps requests from
    a real adapter."""

    def test_unchanged_mock_passes_every_test(self):
        # Without this a helper that reports failures for everything would prove nothing
        for case in proven_classes().values():
            failed, errors, result = run_tests(case, ADAPTER=None)
            self.assertEqual((failed, errors, result.skipped), ([], [], []))
            self.assertEqual(result.testsRun, len(unittest.defaultTestLoader.getTestCaseNames(case)))

    def test_every_fault_switch_has_its_proof(self):
        self.assertEqual(sorted(FAULT_FAILS), sorted(mock_wican.FAULTS))

    def test_change_without_effect_is_not_reported(self):
        module = changed_mock("PICKUP_MS = 300", "PICKUP_MS = 3 * 100")
        for case, names in by_class(sorted({name for _, _, _, tests in CHANGES for name in tests})).items():
            self.assertEqual(run_tests(case, names, MOCK=module, ADAPTER=None)[:2], ([], []))

    def test_change_that_does_not_apply_is_an_error(self):
        with self.assertRaises(ValueError):
            changed_mock("this text is not in the mock", "")
        with self.assertRaises(ValueError):
            changed_mock("self.state", "self.phase")

    def test_names_are_used_once(self):
        names = [change[0] for change in CHANGES] + [change[0] for change in FIXTURE_CHANGES] + list(FAULT_FAILS) \
                + [change[0] for change in LOCK_CHANGES]
        self.assertEqual(sorted(names), sorted(set(names)))

    def test_change_of_the_lock_that_does_not_apply_is_an_error(self):
        with self.assertRaises(ValueError):
            changed_tests("this text is " + "not in the test", "")
        # The text of a change must not find itself in the list of changes
        with self.assertRaises(ValueError):
            changed_tests("    if path in NEVER:", "    if False:")

    def test_every_test_of_the_lock_has_been_seen_failing(self):
        proven = {name for _, _, _, tests in LOCK_CHANGES for name in tests}
        tests = set(unittest.defaultTestLoader.getTestCaseNames(AdapterMode))
        self.assertEqual(sorted(proven - tests), [], "a proof names a test that does not exist")
        self.assertEqual(sorted(tests - proven), [], "tests nobody has seen failing")

    def test_every_test_has_been_seen_failing(self):
        proven = {name for tests in FAULT_FAILS.values() for name in tests}
        proven |= {name for _, _, _, tests in CHANGES for name in tests}
        proven |= {"Fixtures." + name for _, _, _, _, tests in FIXTURE_CHANGES for name in tests}
        tests = set()
        for name, case in proven_classes().items():
            prefix = "" if case is Contract else name + "."
            tests |= {prefix + test for test in unittest.defaultTestLoader.getTestCaseNames(case)}
        self.assertEqual(sorted(proven - tests), [], "a proof names a test that does not exist")
        self.assertEqual(sorted(tests - proven), [], "tests nobody has seen failing")


def _fault_proof(fault):
    def test(self):
        failed, errors, result = run_tests(Contract, FAULTS=(fault,), ADAPTER=None)
        # An error is a test that broke, not a test that noticed something
        self.assertEqual(errors, [])
        self.assertEqual(failed, sorted(FAULT_FAILS[fault]))
        self.assertEqual(result.skipped, [])
    return test


def _change_proof(old, new, tests):
    def test(self):
        module = changed_mock(old, new)
        with tempfile.TemporaryDirectory() as folder, warnings.catch_warnings():
            # For the test that starts the mock as a program
            program = pathlib.Path(folder) / "mock_wican.py"
            program.write_text(module.source, encoding="utf-8")
            # A changed mock may leave a socket open. That is what its test fails for, not worth a warning.
            warnings.simplefilter("ignore", ResourceWarning)
            try:
                for case, names in by_class(tests).items():
                    self.assertEqual(run_tests(case, names, MOCK=module, PROGRAM=program, ADAPTER=None)[:2],
                                     (sorted(names), []))
            finally:
                gc.collect()
    return test


def _lock_proof(old, new, tests):
    def test(self):
        module = changed_tests(old, new)
        self.assertEqual(run_tests(module.AdapterMode, tests)[:2], (sorted(tests), []))
    return test


def _fixture_proof(name, old, new, tests):
    def test(self):
        text = fixture(name)
        self.assertEqual(text.count(old), 1, "the text is not in fixtures/%s exactly once" % name)
        ALTERED[name] = text.replace(old, new)
        try:
            self.assertEqual(run_tests(Fixtures, tests)[:2], (sorted(tests), []))
        finally:
            del ALTERED[name]
    return test


for _fault in FAULT_FAILS:
    setattr(RedProof, "test_fault_%s_is_noticed" % _fault, _fault_proof(_fault))
for _name, _old, _new, _tests in CHANGES:
    setattr(RedProof, "test_change_%s_is_noticed" % _name, _change_proof(_old, _new, _tests))
for _name, _file, _old, _new, _tests in FIXTURE_CHANGES:
    setattr(RedProof, "test_fixture_%s_is_noticed" % _name, _fixture_proof(_file, _old, _new, _tests))
for _name, _old, _new, _tests in LOCK_CHANGES:
    setattr(RedProof, "test_lock_%s_is_noticed" % _name, _lock_proof(_old, _new, _tests))


# ------------------------------------------------------------------------------------------------
# Fixtures
# ------------------------------------------------------------------------------------------------

def api_md_example():
    """The example answer of GET /api/state in API.md."""
    text = (HERE / "API.md").read_text(encoding="utf-8")
    section = text[text.index("## GET /api/state"):]
    return section[section.index("```json\n") + len("```json\n"):section.index("\n```\n")]


def profile_values():
    """Name, class and unit of the values of the W906 profile, as make_car_data.py puts them together."""
    profile = json.loads((REPO / "vehicle_profiles" / "mercedes" / "sprinter_w906_om651.json").read_text(encoding="utf-8"))
    params = json.loads((REPO / ".vehicle_profiles" / "params.json").read_text(encoding="utf-8"))
    names = [name for pid in profile["pids"] for name in pid["parameters"]]
    return [(name, params[name]["settings"]["class"], params[name]["settings"]["unit"]) for name in names]


class Fixtures(unittest.TestCase):
    """The mock answers exactly the hand-written files in fixtures/, and the files follow the sources
    they were written from."""

    MOCK = mock_wican
    HEADERS = {"x-wican-dtc": "1", "host": "192.168.80.1"}

    def ask(self, adapter, method, target):
        status, _, body = adapter.handle(method, target, self.HEADERS)
        return status, body.decode("utf-8")

    def scan(self, adapter, target):
        """The stored result after a complete scan."""
        self.assertEqual(self.ask(adapter, "POST", target)[0], 202)
        adapter.clock.advance(60)
        status, text = self.ask(adapter, "GET", "/api/dtc/result")
        self.assertEqual(status, 200)
        return text

    # The mock against the files -----------------------------------------------------------------

    def test_dtc_states(self):
        # The call sequences of dtc_state_test.c
        rules = self.MOCK.ScanRules(41)
        self.assertEqual(rules.json(True, 500), fixture("dtc_state_idle.json"))
        self.assertEqual(rules.json(False, 500), fixture("dtc_state_unsupported.json"))
        self.assertEqual(rules.begin(False, True, 0, 1000), (None, 41))
        self.assertEqual(rules.json(True, 1500), fixture("dtc_state_queued.json"))
        self.assertTrue(rules.pickup(1200))
        rules.progress(5, 18, "N30/4 ESP")
        self.assertEqual(rules.json(True, 11000), fixture("dtc_state_running.json"))
        rules.progress(17, 18, "N2/14 Rückhaltesystem (SRS)")
        self.assertEqual(rules.json(True, 33000), fixture("dtc_state_running_umlaut.json"))
        rules.progress(18, 18, "N69/1 Fahrertür (TSG)")
        rules.done(3, 36000)
        self.assertEqual(rules.json(True, 48500), fixture("dtc_state_done.json"))
        self.assertEqual(rules.begin(True, False, 0, 50000), (None, 42))
        self.assertTrue(rules.pickup(50100))
        rules.progress(0, 18, None)
        rules.error("engine_running", 51000)
        self.assertEqual(rules.json(True, 54999), fixture("dtc_state_error.json"))

        rules = self.MOCK.ScanRules(100)
        self.assertEqual(rules.begin(False, True, 0, 5000), (None, 100))
        self.assertTrue(rules.pickup(5000))
        rules.done(3, 40000)
        self.assertEqual(rules.begin(True, True, 100, 45000), (None, 101))
        self.assertTrue(rules.pickup(45200))
        rules.progress(18, 18, "N69/1 Fahrertür (TSG)")
        rules.done(1, 80000)
        self.assertEqual(rules.json(True, 81000), fixture("dtc_state_done_clear.json"))

        rules = self.MOCK.ScanRules(0xFFFFFFFF)
        self.assertEqual(rules.begin(False, True, 0, 1000), (None, 2147483647))
        self.assertTrue(rules.pickup(1000))
        rules.progress(255, 255, "N10 SAM")
        rules.done(65535, 5000)
        self.assertEqual(rules.json(True, 5000 + 2147483647999), fixture("dtc_state_limits.json"))

    def test_answers_to_post(self):
        # fixtures/api_body_*.json belong to the host test of the firmware (dtc_api_test.c)
        for reason, seq, name in ((None, 43, "accepted"), ("busy", 42, "busy"), ("read_required", 42, "read_required"),
                                  ("stale_seq", 42, "stale_seq"), ("nothing_to_clear", 42, "nothing_to_clear"),
                                  ("not_ready", 0, "not_ready"), ("forbidden", 0, "forbidden"),
                                  ("bad_request", 0, "bad_request")):
            with self.subTest(name=name):
                self.assertEqual(self.MOCK.body_json(reason, seq), fixture("api_body_%s.json" % name))

    def test_state_example_of_api_md(self):
        values = {"id": "a1b2c3d4e5f6", "fw": "4.21", "git": "w906-v1.4.0-9-g0123abc", "boot": 1234567890, "up": 812,
                  "autopid": "run", "pids": 35, "ecu_online": True, "pass": 1234, "rx_age_ms": 140,
                  "mqtt": "connected", "batt_mv": 12400, "sleep_in_s": -1, "heap": 61000, "heap_min": 48000}
        self.assertEqual(self.MOCK.state_json(values, fixture("dtc_state_idle.json")), api_md_example())
        self.assertEqual(list(json.loads(api_md_example())), STATE_FIELDS, "the field list of this test is not that of API.md")
        self.assertEqual(list(json.loads(api_md_example())["dtc"]), DTC_FIELDS)

    def test_states_of_the_firmware_tests(self):
        # fixtures/api_state_*.json belong to the host test of the firmware (dtc_api_test.c)
        example = {"id": "a1b2c3d4e5f6", "fw": "4.21", "git": "w906-v1.4.0-9-g0123abc", "boot": 1234567890, "up": 812,
                   "autopid": "run", "pids": 35, "ecu_online": True, "pass": 1234, "rx_age_ms": 140,
                   "mqtt": "connected", "batt_mv": 12400, "sleep_in_s": -1, "heap": 61000, "heap_min": 48000}
        above = 2 ** 32 - 1
        cases = (
            ("example", example, fixture("dtc_state_idle.json")),
            ("offline", dict(example, up=4500, ecu_online=False, rx_age_ms=95000, mqtt="disconnected", batt_mv=12100,
                             sleep_in_s=87, heap=58200, heap_min=47100, **{"pass": 2710}),
             fixture("dtc_state_error.json")),
            ("scan", dict(example, id="0123456789ab", git="w906-v1.4.0", boot=41, up=3600, rx_age_ms=0, mqtt="off",
                          batt_mv=14400, heap=60000, heap_min=47999, **{"pass": 3391}),
             fixture("dtc_state_running_umlaut.json")),
            ("starting", dict(example, boot=7, up=0, autopid="starting", pids=0, ecu_online=False, rx_age_ms=-1,
                              mqtt="off", batt_mv=-1, heap=112000, heap_min=111000, **{"pass": 0}),
             fixture("dtc_state_unsupported.json")),
            ("limits", dict(example, id="ffffffffffff", git="w906-v1.4.0-9-g0123abc-dirty", boot=above, up=above,
                            pids=above, rx_age_ms=2 ** 31 - 1, batt_mv=2 ** 31 - 1, sleep_in_s=2 ** 31 - 1, heap=above,
                            heap_min=above, **{"pass": above}),
             fixture("dtc_state_limits.json")),
            ("empty", dict(example, id="", fw="", git="", boot=0, up=0, autopid="", pids=0, ecu_online=False,
                           rx_age_ms=0, mqtt="", batt_mv=0, sleep_in_s=0, heap=0, heap_min=0, **{"pass": 0}),
             "{}"),
        )
        for name, values, dtc in cases:
            with self.subTest(name=name):
                self.assertEqual(self.MOCK.state_json(values, dtc), fixture("api_state_%s.json" % name))

    def test_result_without_trouble_codes(self):
        adapter = self.MOCK.Adapter("no_codes")
        self.assertEqual(self.scan(adapter, "/api/dtc?action=read"), fixture("dtc_result_read_empty.json"))

    def test_result_with_uds_and_kwp_codes(self):
        adapter = self.MOCK.Adapter("codes")
        self.assertEqual(self.scan(adapter, "/api/dtc?action=read"), fixture("dtc_result_read_codes.json"))

    def test_result_of_a_clear(self):
        adapter = self.MOCK.Adapter("codes", seq_seed=41)
        self.scan(adapter, "/api/dtc?action=read")
        self.assertEqual(self.scan(adapter, "/api/dtc?action=clear&seq=41"), fixture("dtc_result_clear.json"))
        # The fault memory of the vehicle has changed
        self.assertEqual(json.loads(self.scan(adapter, "/api/dtc?action=read"))["dtc_count"], 2)

    def test_shortened_result(self):
        adapter = self.MOCK.Adapter("many_codes")
        self.assertEqual(self.scan(adapter, "/api/dtc?action=read"), fixture("dtc_result_shortened.json"))

    def test_values_with_the_ignition_on(self):
        adapter = self.MOCK.Adapter("codes")
        adapter.clock.advance(10)
        self.assertEqual(self.ask(adapter, "GET", "/autopid_data"), (200, fixture("autopid_data_ignition_on.json")))

    def test_values_with_the_ignition_off(self):
        adapter = self.MOCK.Adapter("ignition_off")
        adapter.clock.advance(10)
        self.assertEqual(self.ask(adapter, "GET", "/autopid_data"), (200, fixture("autopid_data_ignition_off.json")))

    def test_car_config(self):
        for scenario in ("codes", "ignition_off", "engine_running", "starting"):
            with self.subTest(scenario=scenario):
                adapter = self.MOCK.Adapter(scenario)
                self.assertEqual(self.ask(adapter, "GET", "/load_car_config"), (200, fixture("car_config_w906.json")))

    # The files against the sources --------------------------------------------------------------

    def test_result_fixtures_follow_the_format(self):
        for name, action, codes in (("dtc_result_read_empty.json", "read", 0), ("dtc_result_read_codes.json", "read", 5),
                                    ("dtc_result_clear.json", "clear", 2), ("dtc_result_shortened.json", "read", 165)):
            with self.subTest(name=name):
                self.assertEqual(check_result(self, fixture(name), action), codes)

    def test_shortened_fixture_is_shortened_no_more_than_needed(self):
        # With the shortest of the omitted lists put back it would not fit into an MQTT message
        text = fixture("dtc_result_shortened.json")
        omitted = [ecu["dtcs_omitted"] for ecu in json.loads(text)["ecus"] if "dtcs_omitted" in ecu]
        shortest_entry = len('{"code":"P0000-00","status":"00","active":true}')
        self.assertGreater(len(text.encode("utf-8")) + min(omitted) * shortest_entry, 5119)

    def test_value_fixtures_have_the_names_of_the_profile(self):
        text = fixture("autopid_data_ignition_on.json")
        self.assertEqual(list(json.loads(text)), [name for name, _, _ in profile_values()])
        self.assertEqual(len(json.loads(text)), 35)
        # Numbers as the firmware writes them: two decimals at most, no trailing zeros
        self.assertRegex(text, r'^\{("[A-Z0-9_]+":-?(0|[1-9][0-9]*)(\.[0-9]?[1-9])?,?)+\}$')
        self.assertEqual(compact(json.loads(text)), text)
        self.assertEqual(fixture("autopid_data_ignition_off.json"), "{}")

    def test_car_config_fixture_is_the_profile(self):
        expected = "{%s}" % ",".join('"%s":{"class":"%s","unit":"%s"}' % value for value in profile_values())
        self.assertEqual(fixture("car_config_w906.json"), expected)

    def test_units_are_those_of_the_firmware(self):
        source = (REPO / "main" / "autopid.c").read_text(encoding="utf-8")
        table = source[source.index("static const dtc_ecu_t dtc_ecus[] = {"):]
        table = table[:table.index("};")]
        units = re.findall(r'\{"([^"]+)",\s*0x([0-9A-F]{3}),\s*0x[0-9A-F]{3},\s*DTC_(UDS|KWP),', table)
        self.assertEqual(units, UNITS)
        self.assertEqual([tuple(unit) for unit in self.MOCK.ECUS], UNITS)


# ------------------------------------------------------------------------------------------------
# Scenarios
# ------------------------------------------------------------------------------------------------

class Scenarios(unittest.TestCase):
    """What the mock does beyond API.md, so that a client developed against it meets what the mock
    promises on the command line."""

    MOCK = mock_wican
    HEADERS = {"x-wican-dtc": "1", "host": "192.168.80.1"}

    def state(self, adapter):
        status, _, body = adapter.handle("GET", "/api/state", {})
        self.assertEqual(status, 200)
        return json.loads(body.decode("utf-8"))

    def text(self, adapter, target, method="GET"):
        return adapter.handle(method, target, self.HEADERS)[2].decode("utf-8")

    def read(self, adapter):
        """Starts a read and returns its number."""
        status, _, body = adapter.handle("POST", "/api/dtc?action=read", self.HEADERS)
        self.assertEqual(status, 202)
        return json.loads(body.decode("utf-8"))["seq"]

    def raw(self, server, request):
        """What the server answers to the bytes of a request until it closes the connection."""
        with socket.create_connection(("127.0.0.1", server.port), timeout=10) as connection:
            connection.sendall(request)
            answer = b""
            while True:
                received = connection.recv(65536)
                if not received:
                    return answer
                answer += received

    def test_every_scenario_can_be_started(self):
        for name in self.MOCK.SCENARIOS:
            with self.subTest(scenario=name):
                adapter = self.MOCK.Adapter(name)
                adapter.clock.advance(100)
                self.assertEqual(adapter.handle("GET", "/api/state", {})[0], 404 if name == "upstream" else 200)
                self.assertEqual(adapter.handle("GET", "/autopid_data", {})[0], 200)

    def test_unknown_scenario_fault_or_setting_is_refused(self):
        with self.assertRaises(ValueError):
            self.MOCK.Adapter("nothing")
        with self.assertRaises(ValueError):
            self.MOCK.Adapter("codes", ("no_busy", "nothing"))
        with self.assertRaises(TypeError):
            self.MOCK.Adapter("codes", ignitoin=False)
        with self.assertRaises(ValueError):
            self.MOCK.Adapter("codes", memory="nothing")
        with self.assertRaises(TypeError):
            self.MOCK.Adapter("codes").set(ignitoin=False)

    def test_settings_read_at_power_on_cannot_be_set_later(self):
        # Taken silently they would change nothing before the next restart
        for name in ("boot", "seq_seed", "mqtt_every_s", "restart_every_s"):
            with self.subTest(setting=name):
                self.MOCK.Adapter("codes", **{name: 60})
                with self.assertRaises(ValueError):
                    self.MOCK.Adapter("codes").set(**{name: 60})

    def test_boot_and_the_first_number_can_be_set(self):
        adapter = self.MOCK.Adapter("codes", boot=1234567890, seq_seed=41)
        self.assertEqual(self.state(adapter)["boot"], 1234567890)
        self.assertEqual(self.text(adapter, "/api/dtc?action=read", "POST"), '{"accepted":true,"seq":41}')

    def test_seq_has_10_digits_at_most_as_in_the_firmware(self):
        # API.md does not say what a number with leading zeros is; main/dtc_api.c takes up to 10 digits
        adapter = self.MOCK.Adapter("codes")
        self.assertEqual(adapter.handle("POST", "/api/dtc?action=clear&seq=0000000007", self.HEADERS)[0], 409)
        self.assertEqual(adapter.handle("POST", "/api/dtc?action=clear&seq=00000000007", self.HEADERS)[0], 400)
        self.assertEqual(adapter.handle("POST", "/api/dtc?action=clear&seq=" + "7" * 5000, self.HEADERS)[0], 400)

    def test_letters_and_digits_outside_ascii_are_not_taken(self):
        # A request over HTTP cannot carry them, a test that calls handle() can: a dotless i is an i
        # for a pattern that ignores case, an Arabic 1 is a digit for \d and for int()
        adapter = self.MOCK.Adapter("codes")
        for host in ("w\u0131can_a1b2.local", "W\u0130CAN_a1b2.local"):
            with self.subTest(host=host):
                self.assertEqual(adapter.handle("POST", "/api/dtc?action=read", {"x-wican-dtc": "1", "host": host})[0], 403)
        self.assertEqual(adapter.handle("POST", "/api/dtc?action=clear&seq=\u0661", self.HEADERS)[0], 400)
        self.assertEqual(self.state(adapter)["dtc"], IDLE)

    def test_restart_scenario_restarts_every_60_s(self):
        adapter = self.MOCK.Adapter("restart")
        first = self.state(adapter)["boot"]
        self.text(adapter, "/api/dtc?action=read", "POST")
        adapter.clock.advance(59)
        state = self.state(adapter)
        self.assertEqual((state["boot"], state["up"], state["dtc"]["state"]), (first, 59, "done"))
        adapter.clock.advance(2)
        state = self.state(adapter)
        self.assertNotEqual(state["boot"], first)
        self.assertEqual((state["up"], state["dtc"]), (1, IDLE))
        adapter.clock.advance(60)
        self.assertNotIn(self.state(adapter)["boot"], (first, state["boot"]))
        self.assertEqual(self.state(adapter)["up"], 1)

    def test_mqtt_scan_scenario_reads_3_s_after_boot_and_then_every_60_s(self):
        adapter = self.MOCK.Adapter("mqtt_scan", seq_seed=7)
        adapter.clock.advance(2.999)
        self.assertEqual(self.state(adapter)["dtc"], IDLE)
        adapter.clock.advance(0.001)
        self.assertEqual(self.state(adapter)["dtc"], dict(IDLE, state="queued", action="read", src="mqtt", seq=7))
        adapter.clock.advance(59.999)
        self.assertEqual(self.state(adapter)["dtc"]["seq"], 7)
        adapter.clock.advance(0.001)
        dtc = self.state(adapter)["dtc"]
        self.assertEqual((dtc["state"], dtc["src"], dtc["seq"], dtc["result_seq"]), ("queued", "mqtt", 8, 7))

    def test_engine_running_scenario_has_the_values_of_an_idling_engine(self):
        adapter = self.MOCK.Adapter("engine_running")
        adapter.clock.advance(10)
        values = json.loads(self.text(adapter, "/autopid_data"))
        self.assertEqual(len(values), 35)
        self.assertTrue(700 <= values["ENGINE_RPM"] <= 900, values["ENGINE_RPM"])
        self.assertGreater(values["COOLANT_TMP"], 70)

    def test_battery_voltage_follows_the_situation(self):
        for scenario, volts in (("codes", 12.4), ("ignition_off", 12.6), ("engine_running", 14.1)):
            with self.subTest(scenario=scenario):
                self.assertEqual(self.state(self.MOCK.Adapter(scenario))["batt_v"], volts)

    def test_values_are_valid_after_the_first_complete_pass(self):
        adapter = self.MOCK.Adapter("codes")
        adapter.clock.advance(0.059)
        self.assertEqual(self.state(adapter)["rx_age_ms"], -1)
        adapter.clock.advance(0.001)
        self.assertEqual(self.state(adapter)["rx_age_ms"], 0)
        adapter.clock.advance(2.039)
        self.assertEqual((self.text(adapter, "/autopid_data"), self.state(adapter)["pass"]), ("{}", 0))
        adapter.clock.advance(0.001)
        self.assertEqual((len(json.loads(self.text(adapter, "/autopid_data"))), self.state(adapter)["pass"]), (35, 1))

    def test_values_come_back_one_pass_after_the_ignition(self):
        adapter = self.MOCK.Adapter("ignition_off")
        adapter.clock.advance(100)
        adapter.set(ignition=True)
        self.assertEqual(self.state(adapter)["ecu"], "online")
        adapter.clock.advance(2)
        self.assertEqual(self.text(adapter, "/autopid_data"), "{}")
        adapter.clock.advance(0.1)
        self.assertEqual(len(json.loads(self.text(adapter, "/autopid_data"))), 35)

    def test_polling_stands_still_during_a_scan(self):
        adapter = self.MOCK.Adapter("codes")
        adapter.clock.advance(10)
        self.text(adapter, "/api/dtc?action=read", "POST")
        adapter.clock.advance(1)
        before = self.state(adapter)
        adapter.clock.advance(30)
        during = self.state(adapter)
        self.assertEqual((during["dtc"]["state"], during["pass"]), ("running", before["pass"]))
        self.assertEqual(during["rx_age_ms"], before["rx_age_ms"] + 30000)
        adapter.clock.advance(10)
        after = self.state(adapter)
        self.assertEqual(after["dtc"]["state"], "done")
        self.assertGreater(after["pass"], before["pass"])
        self.assertLess(after["rx_age_ms"], 100)

    def test_polling_goes_on_after_a_scan_that_failed(self):
        # The engine did not answer
        adapter = self.MOCK.Adapter("ignition_off")
        self.read(adapter)
        adapter.clock.advance(5)
        self.assertEqual(self.state(adapter)["dtc"]["reason"], "ecu_offline")
        adapter.set(ignition=True)
        adapter.clock.advance(10)
        self.assertEqual((len(json.loads(self.text(adapter, "/autopid_data"))), self.state(adapter)["pass"]), (35, 4))

        # The clear was refused after the engine check
        adapter = self.MOCK.Adapter("engine_running")
        read = self.read(adapter)
        adapter.clock.advance(60)
        self.assertEqual(adapter.handle("POST", "/api/dtc?action=clear&seq=%d" % read, self.HEADERS)[0], 202)
        adapter.clock.advance(5)
        before = self.state(adapter)
        self.assertEqual(before["dtc"]["reason"], "engine_running")
        adapter.clock.advance(21)
        self.assertEqual(self.state(adapter)["pass"], before["pass"] + 10)

        # The request expired, no scan ever paused the polling
        adapter = self.MOCK.Adapter("stalled")
        self.read(adapter)
        adapter.clock.advance(42)
        state = self.state(adapter)
        self.assertEqual((state["dtc"]["reason"], state["pass"]), ("expired", 20))

    def test_scan_is_running_without_a_number_of_control_units_while_it_prepares(self):
        # As the firmware: step 0 and the number of control units are reported after the adapter is prepared
        adapter = self.MOCK.Adapter("codes", seq_seed=7)
        self.read(adapter)
        adapter.clock.advance(0.299)
        self.assertEqual(self.state(adapter)["dtc"]["state"], "queued")
        running = dict(IDLE, state="running", action="read", src="http", seq=7)
        for seconds, expected in ((0.001, running), (0.039, running), (0.001, dict(running, total=18)),
                                  (0.659, dict(running, total=18)),
                                  (0.001, dict(running, total=18, ecu=1, name="N73 Elektronisches Zündschloss (EZS)"))):
            adapter.clock.advance(seconds)
            self.assertEqual(self.state(adapter)["dtc"], expected)

    def test_sleep_countdown_starts_when_the_ignition_goes_off(self):
        adapter = self.MOCK.Adapter("codes", sleep_after_s=180)
        adapter.clock.advance(100)
        self.assertEqual(self.state(adapter)["sleep_in_s"], -1)
        adapter.set(ignition=False)
        self.assertEqual(self.state(adapter)["sleep_in_s"], 180)
        adapter.clock.advance(10)
        self.assertEqual(self.state(adapter)["sleep_in_s"], 170)
        # The mock does not go to sleep
        adapter.clock.advance(1000)
        self.assertEqual(self.state(adapter)["sleep_in_s"], 0)
        adapter.set(ignition=True)
        self.assertEqual(self.state(adapter)["sleep_in_s"], -1)

    def test_other_settings_leave_the_values_alone(self):
        adapter = self.MOCK.Adapter("codes", sleep_after_s=180)
        adapter.clock.advance(10)
        before = self.state(adapter)
        adapter.set(mqtt="off", batt_mv=11900)
        after = self.state(adapter)
        self.assertEqual(len(json.loads(self.text(adapter, "/autopid_data"))), 35)
        self.assertEqual((after["pass"], after["rx_age_ms"], after["sleep_in_s"]),
                         (before["pass"], before["rx_age_ms"], -1))
        adapter.clock.advance(10)
        self.assertEqual(self.state(adapter)["pass"], 9)

    def test_values_come_when_the_task_reaches_its_loop(self):
        adapter = self.MOCK.Adapter("starting")
        adapter.clock.advance(10)
        adapter.set(autopid="run")
        adapter.clock.advance(2.099)
        self.assertEqual((self.text(adapter, "/autopid_data"), self.state(adapter)["pass"]), ("{}", 0))
        adapter.clock.advance(0.001)
        self.assertEqual((len(json.loads(self.text(adapter, "/autopid_data"))), self.state(adapter)["pass"]), (35, 1))

    def test_no_polling_while_the_task_is_not_in_its_loop(self):
        # Not even when a scan that was already running ends
        adapter = self.MOCK.Adapter("codes")
        adapter.clock.advance(10)
        self.read(adapter)
        adapter.clock.advance(1)
        adapter.set(autopid="starting")
        before = self.state(adapter)["pass"]
        adapter.clock.advance(100)
        state = self.state(adapter)
        self.assertEqual((state["dtc"]["state"], state["pass"], state["ecu"]), ("done", before, "offline"))

    def test_setting_during_a_scan_does_not_start_the_polling(self):
        adapter = self.MOCK.Adapter("codes")
        adapter.clock.advance(10)
        self.read(adapter)
        adapter.clock.advance(5)
        before = self.state(adapter)["pass"]
        adapter.set(ignition=False)
        adapter.set(ignition=True)
        adapter.clock.advance(20)
        state = self.state(adapter)
        self.assertEqual((state["dtc"]["state"], state["pass"]), ("running", before))

    def test_restart_during_a_scan_drops_it(self):
        adapter = self.MOCK.Adapter("codes")
        self.read(adapter)
        adapter.clock.advance(10)
        self.assertEqual(self.state(adapter)["dtc"]["state"], "running")
        adapter.restart()
        self.assertEqual(self.state(adapter)["dtc"], IDLE)
        # The polling starts anew, nothing of the scan is left to end later
        adapter.clock.advance(10)
        self.assertEqual((len(json.loads(self.text(adapter, "/autopid_data"))), self.state(adapter)["pass"]), (35, 4))
        adapter.clock.advance(60)
        self.assertEqual(self.state(adapter)["dtc"], IDLE)
        self.assertEqual(adapter.handle("GET", "/api/dtc/result", self.HEADERS)[0], 204)

    def test_restart_keeps_the_fault_memory_of_the_vehicle(self):
        adapter = self.MOCK.Adapter("codes")
        read = self.read(adapter)
        adapter.clock.advance(60)
        self.assertEqual(adapter.handle("POST", "/api/dtc?action=clear&seq=%d" % read, self.HEADERS)[0], 202)
        adapter.clock.advance(60)
        self.assertEqual(self.state(adapter)["dtc"]["count"], 2)
        adapter.restart()
        self.assertEqual(self.state(adapter)["dtc"]["count"], 0)
        self.read(adapter)
        adapter.clock.advance(60)
        self.assertEqual(self.state(adapter)["dtc"]["count"], 2)

    def test_times_count_from_the_restart(self):
        # 16 minutes after power on the adapter restarts; a scan then behaves as after power on
        for scenario, expected in (("codes", ("done", "", 25)), ("ignition_off", ("error", "ecu_offline", 58)),
                                   ("unsupported", ("error", "not_supported", 59)), ("stalled", ("error", "expired", 35))):
            with self.subTest(scenario=scenario):
                adapter = self.MOCK.Adapter(scenario)
                adapter.clock.advance(1000)
                adapter.restart()
                self.read(adapter)
                adapter.clock.advance(60)
                state = self.state(adapter)
                self.assertEqual((state["dtc"]["state"], state["dtc"]["reason"], state["dtc"]["age_s"]), expected)
                self.assertEqual(state["up"], 60)

    def test_engine_speed_is_the_setting(self):
        adapter = self.MOCK.Adapter("codes", rpm=2500)
        adapter.clock.advance(10)
        values = json.loads(self.text(adapter, "/autopid_data"))
        self.assertTrue(2450 <= values["ENGINE_RPM"] <= 2550, values["ENGINE_RPM"])
        self.assertGreater(values["COOLANT_TMP"], 70)
        # Turning over, but not running
        adapter.set(rpm=30)
        adapter.clock.advance(10)
        values = json.loads(self.text(adapter, "/autopid_data"))
        self.assertEqual((values["ENGINE_RPM"], values["COOLANT_TMP"]), (30, 21.5))

    def test_result_is_shortened_only_above_5119_bytes(self):
        # main/autopid.c: mqtt_publish() refuses 5120 bytes and more. Bytes, not characters.
        def result(size):
            unit = collections.OrderedDict((("name", ""), ("id", "7E0"), ("protocol", "UDS"), ("status", "ok"),
                                            ("dtcs", [{"code": "P0100-13", "status": "2F", "active": True}])))
            missing = size - len(self.MOCK.result_json(False, 0, 1, [dict(unit, dtcs=list(unit["dtcs"]))]).encode("utf-8"))
            unit["name"] = "ü" * (missing // 2) + "x" * (missing % 2)
            return self.MOCK.result_json(False, 0, 1, [unit])

        fits = result(5119)
        self.assertEqual((len(fits.encode("utf-8")), "dtcs_omitted" in fits), (5119, False))
        self.assertLess(len(fits), 2700, "the name is not made of characters of two bytes")
        shortened = result(5120)
        self.assertEqual(len(shortened.encode("utf-8")), 5120 - len('{"code":"P0100-13","status":"2F","active":true}')
                         + len(',"dtcs_omitted":1'))
        self.assertEqual(json.loads(shortened)["ecus"][0].get("dtcs_omitted"), 1)

    def test_server_is_used_as_its_description_says(self):
        adapter = self.MOCK.Adapter("engine_running")
        server = self.MOCK.Server(adapter).start()
        try:
            self.assertIs(server.adapter, adapter)
            self.assertEqual(server.host, "127.0.0.1")
            server.adapter.clock.advance(40)
            answer = self.raw(server, b"GET /api/state HTTP/1.1\r\nConnection: close\r\n\r\n")
            self.assertEqual(json.loads(answer.partition(b"\r\n\r\n")[2].decode("utf-8"))["up"], 40)
        finally:
            server.close()
        # Nothing is left behind: no thread, and the port is free again
        self.assertEqual([thread.name for thread in threading.enumerate() if thread.name.endswith("port %d" % server.port)], [])
        with self.assertRaises(OSError):
            socket.create_connection(("127.0.0.1", server.port), timeout=2).close()
        # The address is the one the socket is bound to, not the name that was asked for
        named = self.MOCK.Server(adapter, bind="localhost")
        try:
            self.assertEqual(named.host, "127.0.0.1")
        finally:
            named.close()

    def test_server_never_started_can_be_closed(self):
        # As the program does it, which serves in its main thread
        server = self.MOCK.Server(self.MOCK.Adapter("codes"))
        closing = threading.Thread(target=server.close, daemon=True)
        closing.start()
        closing.join(5)
        self.assertFalse(closing.is_alive(), "close() waits for a server that never served")
        with self.assertRaises(OSError):
            socket.create_connection(("127.0.0.1", server.port), timeout=2).close()

    def test_answer_has_the_headers_of_the_firmware_and_no_others(self):
        server = self.MOCK.Server(self.MOCK.Adapter("codes", seq_seed=43)).start()
        try:
            answer = self.raw(server, b"POST /api/dtc?action=read HTTP/1.1\r\nHost: 192.168.80.1\r\nX-WiCAN-DTC: 1\r\n"
                                      b"Connection: close\r\n\r\n")
            self.assertEqual(answer, b"HTTP/1.1 202 Accepted\r\nContent-Type: application/json\r\nCache-Control: no-store\r\n"
                                     b"Content-Length: 26\r\n\r\n" + b'{"accepted":true,"seq":43}')
        finally:
            server.close()

    def test_head_is_answered_without_a_body(self):
        # Not seen through http.client: it drops what it has read ahead with the head of an answer
        server = self.MOCK.Server(self.MOCK.Adapter("codes")).start()
        try:
            answer = self.raw(server, b"HEAD /api/nothing HTTP/1.1\r\nConnection: close\r\n\r\n")
            self.assertEqual(answer, b"HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nContent-Length: 29\r\n\r\n")
        finally:
            server.close()

    def test_request_with_an_odd_content_length_is_answered(self):
        # "\xb2" is a digit for str.isdigit() and none for int(). A body that is announced and sent is dropped.
        server = self.MOCK.Server(self.MOCK.Adapter("codes")).start()
        try:
            with contextlib.redirect_stderr(io.StringIO()) as errors:
                for length, body in ((b"\xb2", b""), (b"abc", b""), (b"-5", b""), (b"70000", b"x" * 70000)):
                    with self.subTest(length=length):
                        answer = self.raw(server, b"GET /api/state HTTP/1.1\r\nContent-Length: " + length + b"\r\n\r\n" + body
                                          + b"GET /api/nothing HTTP/1.1\r\nConnection: close\r\n\r\n")
                        self.assertEqual(re.findall(rb"HTTP/1\.1 [0-9]{3}", answer), [b"HTTP/1.1 200", b"HTTP/1.1 404"])
            self.assertEqual(errors.getvalue(), "")
        finally:
            server.close()

    def test_starting_adapter_has_its_profile_but_no_values(self):
        adapter = self.MOCK.Adapter("starting")
        adapter.clock.advance(100)
        state = self.state(adapter)
        self.assertEqual((state["pids"], state["ecu"], state["pass"], state["dtc"]["supported"]), (35, "offline", 0, True))
        self.assertEqual(self.text(adapter, "/autopid_data"), "{}")
        self.assertEqual(len(json.loads(self.text(adapter, "/load_car_config"))), 35)

    def test_adapter_without_autopid_has_no_profile(self):
        adapter = self.MOCK.Adapter("autopid_off")
        adapter.clock.advance(100)
        state = self.state(adapter)
        self.assertEqual((state["pids"], state["ecu"], state["pass"], state["dtc"]["supported"]), (0, "offline", 0, False))
        self.assertEqual((self.text(adapter, "/autopid_data"), self.text(adapter, "/load_car_config")), ("{}", "{}"))

    def test_control_units_do_not_answer_once_the_ignition_goes_off_during_a_scan(self):
        adapter = self.MOCK.Adapter("codes")
        self.text(adapter, "/api/dtc?action=read", "POST")
        adapter.clock.advance(10)
        step = self.state(adapter)["dtc"]["ecu"]
        adapter.set(ignition=False)
        adapter.clock.advance(40)
        result = json.loads(self.text(adapter, "/api/dtc/result"))
        self.assertEqual([ecu["status"] for ecu in result["ecus"][step:]], ["no_response"] * (18 - step))
        self.assertEqual(result["dtc_count"], sum(len(ecu["dtcs"]) for ecu in result["ecus"][:step]))
        self.assertEqual(check_result(self, self.text(adapter, "/api/dtc/result"), "read"), result["dtc_count"])


# ------------------------------------------------------------------------------------------------
# The rules of the mock against the firmware module
# ------------------------------------------------------------------------------------------------

class Lcg:
    """Pseudo-random numbers that are the same with every Python version."""

    def __init__(self, seed):
        self.state = seed

    def below(self, limit):
        self.state = (self.state * 1664525 + 1013904223) & 0xFFFFFFFF
        return (self.state >> 8) % limit

    def pick(self, choices):
        return choices[self.below(len(choices))]


SEEDS = (0, 1, 41, 0xFFFD, 0xFFFE, 0xFFFF, 0x7FFFFFFD, 0x7FFFFFFE, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF)
STARTS = (0, 1000, 2 ** 32 - 30000, 2 ** 32 + 5, 2 ** 40)
SMALL_STEPS = (0, 0, 1, 7, 100, 999, 1000, 1001, 5000)
LIMIT_STEPS = (19999, 20000, 20001, 35000, 599999, 600000, 600001)
HUGE_STEPS = (3600000, 2 ** 32 - 1, 2 ** 32 + 5000)
COUNTS = (0, 0, 1, 2, 3, 255, 256, 300, 65535)
NAMES = ("", "N30/4 ESP", "N2/14 Rückhaltesystem (SRS)", 'a"b\\c', "x\x01y\x1fz\x7f", "tab\there", "esc\x1bape",
         "A" * 300)
REASONS = ("ecu_offline", "engine_running", "internal", 'a"b\\c')


# What a sequence does next, by its share. "read_and_clear" is a complete read with a clear over HTTP
# behind it: the rules of a bound clear are hardly reached by single random calls.
KINDS = ("begin",) * 34 + ("pickup",) * 20 + ("progress",) * 12 + ("done",) * 10 + ("error",) * 8 + ("json",) * 6 \
        + ("init",) * 2 + ("read_and_clear",) * 8


def sequence(number, rules, length=60):
    """Commands for dtc_state_cli and the answers the rules of the mock give to the same calls."""
    choice = Lcg(number)
    now = choice.pick(STARTS)
    shown = 0
    planned = []
    seed = choice.pick(SEEDS)
    rules.init(seed)
    yield "init %d" % seed, "ok " + rules.json(True, 0)

    for _ in range(length):
        pace = choice.below(100)
        # Mostly small steps, sometimes around the two limits, sometimes very long, sometimes backwards
        if pace < 6:
            now = max(0, now - 5)
        elif pace < 70:
            now += choice.pick(SMALL_STEPS)
        elif pace < 94:
            now += choice.pick(LIMIT_STEPS)
        else:
            now += choice.pick(HUGE_STEPS)

        kind = planned.pop(0) if planned else choice.pick(KINDS)
        if kind == "read_and_clear":
            kind, planned = "read", ["pickup", "done", "clear"]

        if kind in ("begin", "read", "clear"):
            clear = kind == "clear" or (kind == "begin" and choice.below(2) != 0)
            http = kind == "clear" or choice.below(2) != 0
            seq = (rules.seq + 1, rules.seq ^ 0x40000000, rules.seq | 0x80000000, 0, rules.result_seq,
                   rules.seq, rules.seq, rules.seq)[choice.below(8)] & 0xFFFFFFFF
            reason, given = rules.begin(clear, http, seq, now)
            command = "begin %s %s %d %d" % ("clear" if clear else "read", "http" if http else "mqtt", seq, now)
            answer = "%s %d" % (reason or "accepted", given)
            shown = now
        elif kind == "pickup":
            command, answer, shown = "pickup %d" % now, "run" if rules.pickup(now) else "no", now
        elif kind == "progress":
            step, total, name = choice.below(256), choice.below(256), choice.pick(NAMES)
            rules.progress(step, total, name)
            command, answer = ("progress %d %d %s" % (step, total, name)).rstrip(" "), "ok"
        elif kind == "done":
            count = choice.pick(COUNTS)
            rules.done(count, now)
            command, answer, shown = "done %d %d" % (count, now), "ok", now
        elif kind == "error":
            reason = choice.pick(REASONS)
            rules.error(reason, now)
            command, answer, shown = "error %s %d" % (reason, now), "ok", now
        elif kind == "json":
            command, answer, shown = "json %d" % now, "ok", now
        else:
            seed = choice.pick(SEEDS)
            rules.init(seed)
            command, answer, shown = "init %d" % seed, "ok", 0
        yield command, "%s %s" % (answer, rules.json(True, shown))


class Rules(unittest.TestCase):
    """mock_wican.ScanRules and main/dtc_state.c are two implementations of main/dtc_state.h. Both get
    the same calls; dtc_state_cli.c makes the firmware module usable from here."""

    BUILD = ("cc -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all "
             "-I../../main ../../main/dtc_state.c dtc_state_cli.c -o dtc_state_cli")
    RULE_FAULTS = ("no_busy", "clear_ignores_seq", "clear_without_read", "no_expiry", "result_lost_after_error",
                   "seq_not_31_bit")

    @classmethod
    def setUpClass(cls):
        if shutil.which("cc") is None:
            raise unittest.SkipTest("no C compiler (cc): the rules of the mock were NOT compared with main/dtc_state.c")
        build = subprocess.run(cls.BUILD, shell=True, cwd=HERE, capture_output=True, encoding="utf-8", errors="replace")
        if build.returncode != 0:
            raise AssertionError("dtc_state_cli does not build:\n%s" % build.stderr)

    def cli(self, commands):
        """Answers of dtc_state_cli to the commands, its exit status and what it wrote to stderr."""
        run = subprocess.run([str(HERE / "dtc_state_cli")], input="".join(command + "\n" for command in commands).encode("utf-8"),
                             capture_output=True, timeout=60)
        answers = run.stdout.decode("utf-8", errors="replace").split("\n")
        self.assertEqual(answers.pop(), "", "the last answer has no line end")
        return answers, run.returncode, run.stderr.decode("utf-8", errors="replace")

    def differences(self, numbers, faults=()):
        """Replays the sequences. Returns a text for every sequence in which the firmware module
        and the rules of the mock disagree, about the first call that shows it."""
        expected = []
        for number in numbers:
            rules = mock_wican.ScanRules(1, faults)
            expected += [(number, index, command, answer) for index, (command, answer) in enumerate(sequence(number, rules))]

        answers, status, errors = self.cli([command for _, _, command, _ in expected])
        self.assertEqual((status, errors), (0, ""))
        self.assertEqual(len(answers), len(expected))
        different = {}
        for (number, index, command, answer), got in zip(expected, answers):
            if got != answer and number not in different:
                different[number] = "sequence %d, call %d: %s\n  firmware %s\n  mock     %s" % (number, index, command, got, answer)
        return list(different.values()), len(expected)

    def test_cli_writes_the_fixtures(self):
        # The call sequences of dtc_state_test.c, as in Fixtures.test_dtc_states
        commands = (
            ("init 41", "ok " + fixture("dtc_state_idle.json")),
            ("json 500", "ok " + fixture("dtc_state_idle.json")),
            ("begin read http 0 1000", None),
            ("json 1500", "ok " + fixture("dtc_state_queued.json")),
            ("pickup 1200", None),
            ("progress 5 18 N30/4 ESP", None),
            ("json 11000", "ok " + fixture("dtc_state_running.json")),
            ("progress 17 18 N2/14 Rückhaltesystem (SRS)", None),
            ("json 33000", "ok " + fixture("dtc_state_running_umlaut.json")),
            ("progress 18 18 N69/1 Fahrertür (TSG)", None),
            ("done 3 36000", None),
            ("json 48500", "ok " + fixture("dtc_state_done.json")),
            ("begin clear mqtt 0 50000", None),
            ("pickup 50100", None),
            ("progress 0 18", None),
            ("error engine_running 51000", None),
            ("json 54999", "ok " + fixture("dtc_state_error.json")),
            ("init 4294967295", None),
            ("begin read http 0 1000", None),
            ("pickup 1000", None),
            ("progress 255 255 N10 SAM", None),
            ("done 65535 5000", None),
            ("json 2147483648004999", None),
            ("json 2147483652999", "ok " + fixture("dtc_state_limits.json")),
        )
        answers, status, errors = self.cli([command for command, _ in commands])
        self.assertEqual((status, errors), (0, ""))
        for (command, expected), answer in zip(commands, answers):
            if expected is not None:
                with self.subTest(command=command):
                    self.assertEqual(answer, expected)
        self.assertEqual([answer.partition(" {")[0] for answer in answers[2:6]],
                         ["accepted 41", "ok", "run", "ok"])
        self.assertEqual(answers[12].partition(" {")[0], "accepted 42")

    def test_cli_answers_every_reason(self):
        answers, status, errors = self.cli((
            "init 7", "begin clear http 7 100", "begin read mqtt 0 200", "begin read http 0 300", "pickup 400",
            "pickup 500", "done 2 600", "begin clear http 8 700", "begin clear http 7 600601", "begin read http 0 800",
            "pickup 20801", "init 9", "begin read http 0 0", "pickup 0", "done 0 10", "begin clear http 9 20",
            "begin clear mqtt 0 30"))
        self.assertEqual((status, errors), (0, ""))
        self.assertEqual([answer.partition(" {")[0] for answer in answers],
                         ["ok", "read_required 0", "accepted 7", "busy 7", "run", "no", "ok", "stale_seq 7",
                          "read_required 7", "accepted 8", "no", "ok", "accepted 9", "run", "ok", "nothing_to_clear 9",
                          "accepted 10"])
        self.assertIn('"reason":"expired"', answers[10])

    def test_cli_refuses_a_line_it_does_not_understand(self):
        bad = ("", "bogus", "init", "init x", "init -1", "init 4294967296", "init 1 2", "begin read http 0",
               "begin scan http 0 0", "begin read can 0 0", "begin read http 4294967296 0", "begin read http 0 0 0",
               "pickup", "pickup 1.5", "pickup 18446744073709551616", "progress 256 18 x", "progress 1 256 x",
               "progress 1", "error 5", "error  5", "done 65536 0", "done 1", "json", "json 0x10", "json  5",
               " json 5", "json\t5", "begin read\thttp 0 0", "json 5 6", "begin read http  5", "progress  18 x",
               "x" * 3000)
        answers, status, errors = self.cli(("init 41", "begin read http 0 1000") + bad + ("json 1000",))
        self.assertEqual((status, errors), (1, ""))
        self.assertEqual([answer[:1] for answer in answers[2:-1]], ["?"] * len(bad))
        self.assertEqual(answers[-1], "ok " + fixture("dtc_state_queued.json"), "a refused line changed the state")
        self.assertEqual(self.cli(("json 5", "pickup 18446744073709551615"))[1], 0)

    def test_cli_starts_as_after_init_1(self):
        answers, status, errors = self.cli(("json 0", "begin read mqtt 0 0"))
        self.assertEqual((status, errors), (0, ""))
        self.assertEqual(answers[0], "ok " + fixture("dtc_state_idle.json"))
        self.assertEqual(answers[1].partition(" {")[0], "accepted 1")

    def test_cli_exit_status_tells_a_refused_line(self):
        self.assertEqual([self.cli(commands)[1] for commands in ((), ("json 5",), ("bogus",), ("json 5", "bogus", "json 6"),
                                                                 ("x" * 3000,))], [0, 0, 1, 1, 1])

    def test_cli_says_when_the_state_does_not_fit(self):
        # dtc_state_json() gives nothing rather than a part. The program has room for 4096 bytes:
        # a name of 1000 letters fits, 2030 quotes are 4060 bytes in JSON and do not.
        rules = mock_wican.ScanRules(7)
        rules.begin(False, True, 0, 0)
        rules.pickup(0)
        rules.progress(1, 18, "A" * 1000)
        answers, status, errors = self.cli(("init 7", "begin read http 0 0", "pickup 0", "progress 1 18 " + "A" * 1000,
                                            "progress 2 18 " + '"' * 2030, "json 5", "progress 3 18 N10 SAM"))
        self.assertEqual((status, errors), (0, ""))
        self.assertEqual(answers[3], "ok " + rules.json(True, 0))
        self.assertEqual(answers[4:6], ["ok overflow", "ok overflow"])
        rules.progress(3, 18, "N10 SAM")
        self.assertEqual(answers[6], "ok " + rules.json(True, 5))

    def test_cli_answers_each_line_at_once(self):
        # Its description promises that a test may wait for an answer before it sends the next line
        program = subprocess.Popen([str(HERE / "dtc_state_cli")], stdin=subprocess.PIPE, stdout=subprocess.PIPE, bufsize=0)
        try:
            for command, expected in (("init 41", "ok " + fixture("dtc_state_idle.json")),
                                      ("begin read http 0 1000", "accepted 41 " + fixture("dtc_state_queued.json")),
                                      ("bogus", "? unknown command")):
                program.stdin.write(command.encode("utf-8") + b"\n")
                self.assertTrue(select.select([program.stdout], [], [], 10)[0], "no answer to: %s" % command)
                self.assertEqual(program.stdout.readline().decode("utf-8"), expected + "\n")
        finally:
            program.stdin.close()
            program.stdout.close()
            try:
                status = program.wait(10)
            except subprocess.TimeoutExpired:
                # No process is left behind
                program.kill()
                status = program.wait()
        self.assertEqual(status, 1, "the exit status after a refused line")

    def test_mock_and_firmware_module_agree_after_every_call(self):
        different, calls = self.differences(range(1, 301))
        self.assertEqual(different, [])
        self.assertEqual(calls, 300 * 61)

    def test_sequences_reach_every_answer_and_state(self):
        # A comparison of sequences in which nothing happens would prove nothing
        answers = [answer for number in range(1, 301) for _, answer in sequence(number, mock_wican.ScanRules())]
        for text in ("accepted", "busy", "read_required", "stale_seq", "nothing_to_clear", "run", "no", "ok"):
            self.assertGreater(len([answer for answer in answers if answer.startswith(text + " ")]), 50, text)
        for text in ('"state":"idle"', '"state":"queued"', '"state":"running"', '"state":"done"', '"state":"error"',
                     '"reason":"expired"', '"action":"clear","src":"http"', '"seq":2147483647', '"seq":1,',
                     '"seq":65536,', '"age_s":600,', '"age_s":20,', "Rückhaltesystem", 'a\\"b\\\\c', "A" * 300):
            self.assertGreater(len([answer for answer in answers if text in answer]), 50, text)

    def test_broken_rule_of_the_mock_is_found(self):
        for fault in self.RULE_FAULTS:
            with self.subTest(fault=fault):
                different, _ = self.differences(range(1, 61), (fault,))
                self.assertGreater(len(different), 0, "the comparison does not notice the fault %s" % fault)


# ------------------------------------------------------------------------------------------------
# The contract test against a real adapter
# ------------------------------------------------------------------------------------------------

class AdapterMode(unittest.TestCase):
    """What WICAN_HOST makes the contract test send. No adapter is needed for that: the mock listens
    on 127.0.0.1 with a clock that runs in real time, and writes down what it is asked."""

    SPEED = 400
    LOOK_ONLY = (
        "test_state_is_json_in_utf8_without_whitespace", "test_state_and_result_are_not_cached",
        "test_no_cors_headers_are_sent", "test_numbers_are_integers_below_2_31", "test_state_is_answered_with_200",
        "test_state_fields_come_in_the_documented_order", "test_api_is_1", "test_id_fw_and_git_are_texts",
        "test_boot_is_a_number_from_1_to_2_31_minus_1", "test_up_counts_the_seconds_since_boot",
        "test_autopid_is_off_starting_or_run", "test_pids_is_the_number_of_values_of_the_profile",
        "test_ecu_is_online_or_offline", "test_pass_never_goes_back", "test_rx_age_ms_is_minus_1_or_a_time",
        "test_mqtt_is_off_connected_or_disconnected", "test_batt_v_has_one_decimal_or_is_minus_1",
        "test_sleep_in_s_is_minus_1_or_a_number_of_seconds", "test_heap_min_is_not_above_heap",
        "test_dtc_is_consistent", "test_result_belongs_to_result_seq", "test_autopid_data_is_a_flat_object",
        "test_load_car_config_has_class_and_unit_of_every_value", "test_one_connection_serves_many_requests",
        "test_unknown_path_is_404",
    )

    def against_mock(self, case=Contract, names=None, allow_dtc=False, allow_clear=False):
        adapter = mock_wican.Adapter("codes", clock=mock_wican.RealClock(self.SPEED))
        server = mock_wican.Server(adapter).start()
        try:
            failed, errors, result = run_tests(case, names, ADAPTER="127.0.0.1:%d" % server.port, ALLOW_DTC=allow_dtc,
                                               ALLOW_CLEAR=allow_clear, SPEED=self.SPEED)
        finally:
            server.close()
        return failed, errors, result, list(adapter.requests)

    def test_what_may_be_sent(self):
        for method, target, allow_dtc, allow_clear, sent in (
                ("GET", "/api/state", False, False, True),
                ("GET", "/api/dtc/result", False, False, True),
                ("GET", "/autopid_data", False, False, True),
                ("GET", "/load_car_config", False, False, True),
                ("GET", "/api/nothing", False, False, True),
                ("GET", "/api/dtc?action=read", False, False, False),
                ("GET", "/api/dtc", False, False, False),
                ("GET", "/", False, False, False),
                ("POST", "/api/state", False, False, False),
                ("POST", "/api/dtc?action=read", False, False, False),
                ("POST", "/api/dtc?action=clear&seq=5", False, False, False),
                ("POST", "/api/dtc?action=clear&seq=5", False, True, False),
                ("OPTIONS", "/api/dtc?action=read", False, False, False),
                ("POST", "/api/dtc?action=read", True, False, True),
                ("POST", "/api/dtc?action=nothing", True, False, True),
                ("POST", "/api/dtc", True, False, True),
                ("GET", "/api/dtc?action=read", True, False, True),
                ("OPTIONS", "/api/dtc?action=read", True, False, True),
                ("POST", "/api/dtc?action=clear&seq=5", True, False, False),
                ("POST", "/api/dtc?action=clear", True, False, False),
                ("POST", "/api/dtc?seq=5&action=clear", True, False, False),
                ("GET", "/api/dtc?action=clear&seq=5", True, False, False),
                ("POST", "/api/dtc?action=clear&seq=5", True, True, True),
                ("GET", "/check_status", True, True, False),
                ("GET", "/load_config", True, True, False),
                ("GET", "/scan_available_pids?x=1", True, True, False),
        ):
            with self.subTest(method=method, target=target, allow_dtc=allow_dtc, allow_clear=allow_clear):
                self.assertEqual(not_permitted(method, target, allow_dtc, allow_clear) is None, sent)

    def test_environment_decides_what_is_allowed(self):
        for environ, expected in (
                ({}, (None, False, False, 1.0)),
                ({"WICAN_HOST": ""}, (None, False, False, 1.0)),
                ({"WICAN_HOST": "192.168.80.1"}, ("192.168.80.1", False, False, 1.0)),
                ({"WICAN_HOST": "192.168.80.1", "WICAN_ALLOW_DTC": "1"}, ("192.168.80.1", True, False, 1.0)),
                ({"WICAN_HOST": "192.168.80.1", "WICAN_ALLOW_CLEAR": "1"}, ("192.168.80.1", False, True, 1.0)),
                ({"WICAN_HOST": "127.0.0.1:8906", "WICAN_ALLOW_DTC": "1", "WICAN_ALLOW_CLEAR": "1", "WICAN_SPEED": "10"},
                 ("127.0.0.1:8906", True, True, 10.0)),
        ):
            with self.subTest(environ=environ):
                self.assertEqual(permissions(environ), expected)
        # Only "1" allows
        for value in ("", "0", "2", "yes", "true", "on", " 1", "1 ", "01"):
            with self.subTest(value=value):
                self.assertEqual(permissions({"WICAN_HOST": "x", "WICAN_ALLOW_DTC": value, "WICAN_ALLOW_CLEAR": value})[1:3],
                                 (False, False))
        # The class reads the environment of this process
        self.assertEqual((Contract.ADAPTER, Contract.ALLOW_DTC, Contract.ALLOW_CLEAR, Contract.SPEED),
                         permissions(os.environ))

    def test_without_permission_only_get_requests_to_five_paths_are_sent(self):
        failed, errors, result, requests = self.against_mock()
        self.assertEqual((failed, errors), ([], []))
        self.assertEqual({method for method, _ in requests}, {"GET"})
        self.assertEqual({target for _, target in requests}, set(READ_ONLY))
        # The tests that have something to say about an adapter they may only look at
        skipped = {test._testMethodName for test, _ in result.skipped}
        ran = [name for name in unittest.defaultTestLoader.getTestCaseNames(Contract) if name not in skipped]
        self.assertEqual(ran, sorted(self.LOOK_ONLY))
        self.assertEqual(result.testsRun, len(ran) + len(skipped))

    def test_request_a_test_did_not_announce_is_not_sent(self):
        class Forgetful(Contract):
            def test_scan(self):
                self.given()
                self.post("/api/dtc?action=read")

            def test_scan_with_get(self):
                self.given()
                self.get("/api/dtc?action=read", DTC_HEADER)

            def test_clear(self):
                self.given()
                self.post("/api/dtc?action=clear&seq=1")

            def test_path_a_client_never_calls(self):
                self.given()
                self.get("/check_status")

        names = ["test_scan", "test_scan_with_get", "test_clear", "test_path_a_client_never_calls"]
        failed, errors, result, requests = self.against_mock(Forgetful, names)
        self.assertEqual((failed, errors, len(result.skipped), requests), ([], [], 4, []))

        failed, errors, result, requests = self.against_mock(Forgetful, names, allow_dtc=True)
        self.assertEqual((failed, errors, len(result.skipped)), ([], [], 2))
        self.assertEqual(requests, [("POST", "/api/dtc?action=read"), ("GET", "/api/dtc?action=read")])

        failed, errors, result, requests = self.against_mock(Forgetful, names, allow_dtc=True, allow_clear=True)
        self.assertEqual((failed, errors, len(result.skipped)), ([], [], 1))
        self.assertEqual(len(requests), 3)

    def test_connection_the_adapter_ended_is_opened_again(self):
        # An adapter that ends the connection after every answer and does not say so
        listener = socket.create_server(("127.0.0.1", 0))
        asked = []

        def serve():
            try:
                for _ in range(2):
                    connection, _ = listener.accept()
                    with connection:
                        asked.append(connection.recv(65536).split(b"\r\n")[0])
                        connection.sendall(b"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n")
            except OSError:
                # The listener was closed before the second connection came
                pass

        class Twice(Contract):
            def test_two_requests(self):
                self.given()
                self.assertEqual(self.get("/api/nothing").status, 404)
                # Until the end of the connection has arrived
                self.assertTrue(select.select([self.connection.sock], [], [], 10)[0])
                self.assertEqual(self.get("/api/nothing").status, 404)

        server = threading.Thread(target=serve, daemon=True)
        server.start()
        try:
            failed, errors, _ = run_tests(Twice, ["test_two_requests"], ADAPTER="127.0.0.1:%d" % listener.getsockname()[1])
            self.assertEqual((failed, errors), ([], []))
            server.join(10)
            self.assertEqual(asked, [b"GET /api/nothing HTTP/1.1"] * 2)
        finally:
            listener.close()

    def test_with_permission_for_scans_no_clear_is_sent(self):
        names = ["test_read_is_accepted_with_202_and_its_number",
                 "test_result_is_there_when_done_with_the_number_of_the_request",
                 "test_clear_with_the_number_of_another_read_is_stale",
                 "test_clear_with_the_number_of_the_last_read_is_accepted", "test_clear_without_seq_is_a_bad_request"]
        failed, errors, result, requests = self.against_mock(names=names, allow_dtc=True)
        self.assertEqual((failed, errors, len(result.skipped)), ([], [], 3))
        self.assertEqual([target for _, target in requests if "clear" in target], [])
        self.assertIn(("POST", "/api/dtc?action=read"), requests)

    def test_with_every_permission_scans_run_in_real_time(self):
        names = ["test_name_is_the_control_unit_being_processed", "test_ecu_counts_from_0_to_total_while_running",
                 "test_result_of_a_read_has_the_documented_format", "test_read_takes_about_35_s",
                 "test_clear_with_the_number_of_another_read_is_stale",
                 "test_clear_with_the_number_of_the_last_read_is_accepted"]
        failed, errors, result, requests = self.against_mock(names=names, allow_dtc=True, allow_clear=True)
        self.assertEqual((failed, errors, result.skipped), ([], [], []))
        self.assertEqual(len([target for method, target in requests if method == "POST" and "action=clear" in target]), 4)


# ------------------------------------------------------------------------------------------------
# The mock as a program
# ------------------------------------------------------------------------------------------------

class CommandLine(unittest.TestCase):
    MOCK = mock_wican
    # The file that is started as a program; RedProof starts a changed one
    PROGRAM = HERE / "mock_wican.py"

    def test_list_names_every_scenario_and_fault(self):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            self.assertEqual(self.MOCK.main(["--list"]), 0)
        names = [line.split()[0] for line in output.getvalue().splitlines() if line.startswith("  ")]
        self.assertEqual(names, list(self.MOCK.SCENARIOS) + list(self.MOCK.FAULTS))
        for name in ("ignition_off", "starting", "engine_running", "mqtt_scan", "codes", "many_codes", "unsupported",
                     "restart", "upstream"):
            self.assertIn(name, self.MOCK.SCENARIOS)

    def test_without_arguments_it_is_the_adapter_of_the_description(self):
        arguments = self.MOCK.parse_arguments([])
        self.assertEqual((arguments.bind, arguments.port, arguments.scenario, arguments.fault, arguments.speed,
                          arguments.quiet, arguments.list), ("127.0.0.1", 8906, "codes", [], 1.0, False, False))

    def test_arguments_are_taken(self):
        arguments = self.MOCK.parse_arguments(["--bind", "0.0.0.0", "--port", "0", "--scenario", "restart", "--fault", "no_busy",
                                               "--fault", "no_expiry", "--speed", "2.5", "--quiet"])
        self.assertEqual((arguments.bind, arguments.port, arguments.scenario, arguments.fault, arguments.speed,
                          arguments.quiet, arguments.list), ("0.0.0.0", 0, "restart", ["no_busy", "no_expiry"], 2.5, True, False))
        self.assertEqual(self.MOCK.parse_arguments(["--port", "65535", "--speed", "0.001"]).port, 65535)

    def test_unknown_scenario_or_fault_is_refused(self):
        # A speed of 0 or below would stop the clock or turn it back, nan and inf end every request in an exception
        for arguments in (["--scenario", "nothing"], ["--fault", "nothing"], ["--speed", "0"], ["--speed", "-1"],
                          ["--speed", "nan"], ["--speed", "inf"], ["--speed", "fast"], ["--port", "65536"], ["--port", "-1"]):
            with self.subTest(arguments=arguments), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit):
                    self.MOCK.parse_arguments(arguments)

    def test_started_as_a_program_it_runs_a_scan_in_real_time(self):
        program = subprocess.Popen([sys.executable, str(self.PROGRAM), "--port", "0", "--speed", "400",
                                    "--scenario", "codes", "--fault", "no_busy"], stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, encoding="utf-8")
        # A program that never answers must not keep the test waiting for good
        watchdog = threading.Timer(60, program.kill)
        watchdog.start()
        try:
            first_line = program.stdout.readline()
            started = re.fullmatch(r"mock WiCAN on http://127\.0\.0\.1:([0-9]+) \(scenario codes, faults no_busy, speed 400\)\n",
                                   first_line)
            self.assertIsNotNone(started, first_line)
            connection = http.client.HTTPConnection("127.0.0.1", int(started.group(1)), timeout=10)
            self.addCleanup(connection.close)
            answers = []
            # The second request is accepted during the first: the fault switch is on
            for _ in range(2):
                connection.request("POST", "/api/dtc?action=read", headers={"X-WiCAN-DTC": "1"})
                response = connection.getresponse()
                answers.append((response.status, json.loads(response.read())))
            seq = answers[0][1]["seq"]
            self.assertEqual(answers, [(202, {"accepted": True, "seq": seq}), (202, {"accepted": True, "seq": seq + 1})])
            for _ in range(500):
                connection.request("GET", "/api/state")
                dtc = json.loads(connection.getresponse().read())["dtc"]
                if dtc["state"] not in ("queued", "running"):
                    break
                time.sleep(0.01)
            self.assertEqual((dtc["state"], dtc["result_seq"]), ("done", seq + 1))
            connection.request("GET", "/api/dtc/result")
            self.assertEqual(connection.getresponse().read().decode("utf-8"), fixture("dtc_result_read_codes.json"))
        finally:
            watchdog.cancel()
            program.terminate()
            try:
                output, errors = program.communicate(timeout=10)
            except subprocess.TimeoutExpired:
                # No process is left behind
                program.kill()
                output, errors = program.communicate()
        self.assertEqual(errors, "")
        self.assertIn("POST /api/dtc?action=read -> 202 {\"accepted\":true,\"seq\":%d}" % (seq + 1), output)


if __name__ == "__main__":
    unittest.main()
