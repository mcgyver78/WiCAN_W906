#!/usr/bin/env python3
"""Analyse CAN logs of a Mercedes-Benz Sprinter W906 (or any vehicle).

Finds the frames that wake up a silent diagnostic bus and decodes the
ISO-TP diagnostic requests/responses (KWP2000 / UDS) a tester like Xentry
sends, so they can be turned into WiCAN ATWUA wake-up frames and vehicle
profile PIDs.

Supported log formats (auto detected):
  * SavvyCAN / GVRET CSV  (Time Stamp,ID,Extended,Dir,Bus,LEN,D1..D8)
  * candump -l            ((1694600000.123456) can0 7E0#0322F19000000000)
  * candump (console)     (can0  7E0   [8]  03 22 F1 90 00 00 00 00)
  * SLCAN / Lawicel       (t7E80322F190...  or with a 4 digit ms timestamp)
    as sent on the WiCAN TCP port 3333 (SLCAN C, S6, L)

Only the Python standard library is used.
"""

import argparse
import csv
import json
import re
import sys
from collections import OrderedDict, defaultdict
from dataclasses import dataclass, field
from typing import Dict, Iterable, List, Optional, Tuple

# Diagnostic services a tester sends (KWP2000 and UDS)
REQUEST_SERVICES = {
    0x10: "DiagnosticSessionControl",
    0x11: "ECUReset",
    0x14: "ClearDiagnosticInformation",
    0x17: "ReadStatusOfDTC (KWP)",
    0x18: "ReadDTCByStatus (KWP)",
    0x19: "ReadDTCInformation",
    0x1A: "ReadECUIdentification (KWP)",
    0x21: "ReadDataByLocalIdentifier (KWP)",
    0x22: "ReadDataByIdentifier",
    0x23: "ReadMemoryByAddress",
    0x27: "SecurityAccess",
    0x28: "CommunicationControl",
    0x2E: "WriteDataByIdentifier",
    0x30: "InputOutputControlByLocalIdentifier (KWP)",
    0x31: "RoutineControl",
    0x3B: "WriteDataByLocalIdentifier (KWP)",
    0x3E: "TesterPresent",
    0x85: "ControlDTCSetting",
}

# Bytes after the service id that identify what was requested
IDENTIFIER_LENGTH = {0x10: 1, 0x11: 1, 0x19: 1, 0x1A: 1, 0x21: 1, 0x22: 2, 0x27: 1,
                     0x28: 1, 0x2E: 2, 0x30: 1, 0x31: 3, 0x3B: 1, 0x3E: 1, 0x85: 1}

READ_SERVICES = (0x21, 0x22)
RESPONSE_TIMEOUT = 1.0


@dataclass
class Frame:
    ts: float
    can_id: int
    extended: bool
    data: bytes
    direction: Optional[str] = None

    def id_str(self) -> str:
        return "%08X" % self.can_id if self.extended else "%03X" % self.can_id


@dataclass
class Message:
    """A reassembled ISO-TP message."""
    ts: float
    can_id: int
    extended: bool
    payload: bytes
    direction: Optional[str] = None


@dataclass
class Exchange:
    request_id: int
    response_id: int
    extended: bool
    request: bytes
    responses: List[bytes] = field(default_factory=list)
    negative: Dict[int, int] = field(default_factory=dict)
    count: int = 0


# --------------------------------------------------------------------------- parsing

# The data field is anchored to an even number of hex digits so RTR ("#R..."), CAN FD
# ("##...") and truncated lines don't parse as a frame with bogus data.
CANDUMP_LOG_RE = re.compile(r"^\((?P<ts>[\d.]+)\)\s+\S+\s+(?P<id>[0-9A-Fa-f]+)#(?P<data>(?:[0-9A-Fa-f]{2})*)\s*$")
CANDUMP_CONSOLE_RE = re.compile(
    r"^\s*(?:\((?P<ts>[\d.]+)\)\s+)?\S+\s+(?P<id>[0-9A-Fa-f]{3,8})\s+\[(?P<len>\d)\]\s*(?P<data>(?:[0-9A-Fa-f]{2}\s*)*)$")
# SLCAN / Lawicel: t<3 hex id><dlc><data>, T<8 hex id><dlc><data>, r/R remote frames,
# optionally followed by a 4 digit millisecond timestamp (rolls over every 60000 ms).
SLCAN_RE = re.compile(r"^(?P<kind>[tTrR])(?P<id>[0-9A-Fa-f]+)(?P<rest>[0-9A-Fa-f]*)$")


class ParseError(Exception):
    pass


def _skipped(skipped: int) -> None:
    if skipped:
        print("warning: skipped %d unparseable line(s)" % skipped, file=sys.stderr)


def _parse_savvycan(lines: List[str]) -> List[Frame]:
    frames = []
    skipped = 0
    # Files re-saved with a spreadsheet program may contain stray ';'
    reader = csv.reader(line.replace(";", " ") for line in lines)
    header = [h.strip().lower() for h in next(reader)]

    def col(*names):
        for name in names:
            if name in header:
                return header.index(name)
        return None

    i_ts = col("time stamp", "timestamp", "time")
    i_id = col("id")
    i_ext = col("extended")
    i_dir = col("dir", "direction")
    i_len = col("len", "length", "dlc")
    i_d1 = col("d1")
    if i_ts is None or i_id is None or i_len is None or i_d1 is None:
        raise ParseError("Unknown CSV header: %s" % ",".join(header))

    for row in reader:
        try:
            if len(row) <= i_len or not row[i_id].strip():
                continue
            length = int(row[i_len])
            data = bytes(int(v, 16) for v in row[i_d1:i_d1 + length] if v.strip())
            can_id = int(row[i_id], 16)
            extended = row[i_ext].strip().lower() in ("true", "1", "yes") if i_ext is not None else can_id > 0x7FF
            # SavvyCAN stores microseconds
            ts = float(row[i_ts]) / 1e6
            direction = row[i_dir].strip() if i_dir is not None and len(row) > i_dir else None
            frames.append(Frame(ts, can_id, extended, data, direction))
        except (ValueError, IndexError):
            skipped += 1
    _skipped(skipped)
    return frames


def _parse_candump(lines: Iterable[str]) -> List[Frame]:
    frames = []
    skipped = 0
    fallback_ts = 0.0
    have_ts = False
    for line in lines:
        match = CANDUMP_LOG_RE.match(line) or CANDUMP_CONSOLE_RE.match(line)
        if not match:
            skipped += 1
            continue
        try:
            id_text = match.group("id")
            data_text = re.sub(r"\s", "", match.group("data"))
            ts = match.group("ts")
            if ts is not None:
                have_ts = True
            else:
                fallback_ts += 0.001
            frames.append(Frame(float(ts) if ts else fallback_ts, int(id_text, 16), len(id_text) > 3,
                                bytes.fromhex(data_text)))
        except ValueError:
            skipped += 1
    _skipped(skipped)
    if frames and not have_ts:
        print("warning: no timestamps in the log, cycle times and wake-up gaps are meaningless",
              file=sys.stderr)
    return frames


def _parse_slcan(lines: Iterable[str]) -> List[Frame]:
    frames = []
    skipped = 0
    last_raw = None
    offset = 0.0
    have_ts = False
    for line in lines:
        match = SLCAN_RE.match(line.strip())
        if not match:
            skipped += 1
            continue
        try:
            kind = match.group("kind")
            extended = kind in ("T", "R")
            remote = kind in ("r", "R")
            id_len = 8 if extended else 3
            body = match.group("id") + match.group("rest")
            if len(body) < id_len + 1:
                raise ValueError("short frame")
            can_id = int(body[:id_len], 16)
            dlc = int(body[id_len], 16)
            after = body[id_len + 1:]
            data = b"" if remote else bytes.fromhex(after[:2 * dlc])
            stamp = after[2 * dlc if not remote else 0:]
            if len(stamp) == 4:
                have_ts = True
                raw = int(stamp, 16)
                if last_raw is not None and raw < last_raw:
                    offset += 60.0  # the 4 digit ms timestamp wraps every 60000 ms
                last_raw = raw
                ts = offset + raw / 1000.0
            else:
                ts = len(frames) * 0.001
            frames.append(Frame(ts, can_id, extended, data))
        except ValueError:
            skipped += 1
    _skipped(skipped)
    if frames and not have_ts:
        print("warning: SLCAN log has no timestamps, cycle times and wake-up gaps are meaningless",
              file=sys.stderr)
    return frames


def parse_log(path: str) -> List[Frame]:
    with open(path, "r", encoding="utf-8-sig", errors="replace") as handle:
        lines = [line.rstrip("\r\n") for line in handle if line.strip()]
    if not lines:
        return []
    first = lines[0]
    if "," in first and "id" in first.lower():
        frames = _parse_savvycan(lines)
    elif SLCAN_RE.match(first.strip()) and "#" not in first and "[" not in first:
        frames = _parse_slcan(lines)
    else:
        frames = _parse_candump(lines)
    frames.sort(key=lambda f: f.ts)
    return frames


# --------------------------------------------------------------------------- analysis

def find_wake_candidates(frames: List[Frame], gap: float, count: int) -> List[Tuple[float, List[Frame]]]:
    """Frames at the start of the log and after every silence of at least `gap` seconds."""
    bursts = []
    for index, frame in enumerate(frames):
        if index == 0 or frame.ts - frames[index - 1].ts >= gap:
            silence = frame.ts - frames[index - 1].ts if index else float("inf")
            bursts.append((silence, frames[index:index + count]))
    return bursts


def reassemble_isotp(frames: List[Frame]) -> List[Message]:
    """Reassemble ISO-TP messages per CAN id. Frames that aren't ISO-TP are ignored."""
    messages = []
    pending: Dict[Tuple[int, bool], dict] = {}

    for frame in frames:
        data = frame.data
        if not data:
            continue
        key = (frame.can_id, frame.extended)
        pci = data[0] >> 4
        if pci == 0x0:
            length = data[0] & 0x0F
            if 0 < length <= len(data) - 1:
                messages.append(Message(frame.ts, frame.can_id, frame.extended, data[1:1 + length], frame.direction))
        elif pci == 0x1 and len(data) >= 2:
            length = ((data[0] & 0x0F) << 8) | data[1]
            # A classic-CAN first frame always announces more than 7 bytes; anything
            # shorter (or the length-0 escape form) is not a diagnostic multi-frame.
            if length < 8:
                continue
            pending[key] = {"ts": frame.ts, "length": length, "payload": bytearray(data[2:]), "seq": 1,
                            "direction": frame.direction}
        elif pci == 0x2 and key in pending:
            state = pending[key]
            if (data[0] & 0x0F) != state["seq"] & 0x0F:
                del pending[key]
                continue
            state["seq"] += 1
            state["payload"].extend(data[1:])
            if len(state["payload"]) >= state["length"]:
                messages.append(Message(state["ts"], frame.can_id, frame.extended,
                                        bytes(state["payload"][:state["length"]]), state["direction"]))
                del pending[key]
    messages.sort(key=lambda m: m.ts)
    return messages


def _response_matches_request(request_id: int, response_id: int, functional: bool) -> bool:
    """True if a response id plausibly belongs to a request id (ISO 15765 addressing)."""
    if functional:
        return True
    if not (request_id > 0x7FF):  # 11 bit
        return response_id == request_id + 8 or response_id > 0x7FF or response_id < 0x700
    # 29 bit physical: 18DA<tester><ecu> request -> 18DA<ecu><tester> response
    return ((request_id ^ response_id) & 0xFFFF) != 0


def match_exchanges(messages: List[Message]) -> "OrderedDict[Tuple, Exchange]":
    """Pair diagnostic requests with the responses that follow them.

    A response is paired with an open request from a different id, preferring one whose
    id is the physical/functional peer of the response id, so overlapping requests to
    two ECUs are not crossed. Functional requests (7DF, 18DB33F1) stay open for several
    responders. A "response pending" (NRC 78) keeps the request open until the final
    answer or the timeout.
    """
    exchanges: "OrderedDict[Tuple, Exchange]" = OrderedDict()
    open_requests: List[Message] = []
    functional_ids = {0x7DF, 0x18DB33F1}

    def record(request: Message, msg: Message, is_negative: bool, id_len: int) -> None:
        key = (request.can_id, msg.can_id, request.payload[:1 + id_len])
        exchange = exchanges.get(key)
        if exchange is None:
            exchange = exchanges[key] = Exchange(request.can_id, msg.can_id, request.extended,
                                                 request.payload[:1 + id_len])
        exchange.count += 1
        if is_negative:
            exchange.negative[msg.payload[2]] = exchange.negative.get(msg.payload[2], 0) + 1
        else:
            exchange.responses.append(msg.payload)

    for msg in messages:
        if not msg.payload:
            continue
        service = msg.payload[0]
        if service in REQUEST_SERVICES:
            open_requests = [r for r in open_requests if msg.ts - r.ts <= RESPONSE_TIMEOUT and r.can_id != msg.can_id]
            open_requests.append(msg)
            continue

        is_negative = service == 0x7F and len(msg.payload) >= 3
        requested_service = msg.payload[1] if is_negative else service - 0x40
        id_len = IDENTIFIER_LENGTH.get(requested_service, 0)

        # Prefer requests whose id is the peer of the response id, newest first
        candidates = [r for r in reversed(open_requests)
                      if r.can_id != msg.can_id and msg.ts - r.ts <= RESPONSE_TIMEOUT
                      and r.payload[0] == requested_service
                      and (is_negative or msg.payload[1:1 + id_len] == request_ident(r, id_len))]
        candidates.sort(key=lambda r: not _response_matches_request(r.can_id, msg.can_id, r.can_id in functional_ids))

        for request in candidates:
            record(request, msg, is_negative, IDENTIFIER_LENGTH.get(request.payload[0], 0))
            nrc = msg.payload[2] if is_negative else None
            if nrc == 0x78:
                # response pending: keep the request open and extend its deadline
                request.ts = msg.ts
            elif request.can_id not in functional_ids:
                open_requests.remove(request)
            break
    return exchanges


def request_ident(request: Message, id_len: int) -> bytes:
    return request.payload[1:1 + id_len]


def value_series(messages: List[Message]) -> "OrderedDict[Tuple[int, str], List[Tuple[float, int]]]":
    """Values of positive 21xx/22xxxx responses (up to 4 data bytes, big endian) over time."""
    series: "OrderedDict[Tuple[int, str], List[Tuple[float, int]]]" = OrderedDict()
    for msg in messages:
        service = msg.payload[0] - 0x40
        if service not in READ_SERVICES:
            continue
        id_len = IDENTIFIER_LENGTH[service]
        value = msg.payload[1 + id_len:]
        if not 1 <= len(value) <= 4:
            continue
        key = (msg.can_id, bytes([service]).hex().upper() + msg.payload[1:1 + id_len].hex().upper())
        series.setdefault(key, []).append((msg.ts, int.from_bytes(value, "big")))
    return OrderedDict(sorted(series.items()))


def print_series(series, out=sys.stdout) -> None:
    print("\n== Value series (raw unsigned, big endian)", file=out)
    if not series:
        print("none found", file=out)
        return
    print("%-9s %-10s %6s %8s %8s %8s %8s %8s" % ("response", "request", "count", "first", "min", "median", "max", "last"),
          file=out)
    for (can_id, request), values in series.items():
        raw = sorted(v for _, v in values)
        print("%-9X %-10s %6d %8d %8d %8d %8d %8d" % (
            can_id, request, len(values), values[0][1], raw[0], raw[len(raw) // 2], raw[-1], values[-1][1]), file=out)


def byte_ranges(responses: List[bytes]) -> List[Tuple[int, int]]:
    length = min(len(r) for r in responses)
    return [(min(r[i] for r in responses), max(r[i] for r in responses)) for i in range(length)]


def response_frame_count(payload_length: int) -> int:
    if payload_length <= 7:
        return 1
    return 1 + -(-(payload_length - 6) // 7)


def wican_byte_index(payload_index: int, payload_length: int) -> int:
    """Map an index into the reassembled ISO-TP payload to the byte number the WiCAN
    expression parser uses (B0 = PCI byte). Single frame: B(p+1). Multi frame: the first
    frame carries 6 payload bytes after its 2 PCI bytes, every consecutive frame adds a
    PCI byte, so bytes across a frame boundary get their own offset."""
    if response_frame_count(payload_length) == 1:
        return payload_index + 1
    if payload_index < 6:
        return payload_index + 2
    return 9 + 8 * ((payload_index - 6) // 7) + (payload_index - 6) % 7


def build_profile_skeleton(exchanges: "OrderedDict[Tuple, Exchange]") -> dict:
    """WiCAN vehicle profile skeleton for all positive 21xx/22xxxx exchanges.

    Parameter names and expressions are placeholders and must be edited.
    Byte numbering follows the WiCAN expression parser with headers on:
    B0 is the ISO-TP PCI byte, B1 the response service id (single frame).
    """
    pids = []
    read_exchanges = [e for e in exchanges.values() if e.request[0] in READ_SERVICES and e.responses]
    headers = defaultdict(int)
    for exchange in read_exchanges:
        headers[(exchange.request_id, exchange.response_id, exchange.extended)] += exchange.count
    main_header = max(headers, key=headers.get) if headers else None
    # With more than one header every PID needs its own init, because the firmware only
    # re-sends an init on a PID type change, not per PID (a leaked header gives NO DATA).
    per_pid_init = len(headers) > 1

    for exchange in read_exchanges:
        request_hex = exchange.request.hex().upper()
        response = exchange.responses[0]
        frames = response_frame_count(len(response))
        pid = {"pid": request_hex + (str(frames) if frames <= 9 else "")}
        header = (exchange.request_id, exchange.response_id, exchange.extended)
        if per_pid_init or header != main_header:
            pid["pid_init"] = _header_init(*header)
        first_value_byte = wican_byte_index(len(exchange.request), len(response))
        changing = [wican_byte_index(i, len(response))
                    for i, (lo, hi) in enumerate(byte_ranges(exchange.responses)) if lo != hi]
        pid["parameters"] = {"TODO_%s" % request_hex: "B%d" % first_value_byte}
        note = "response %s, changing bytes: %s" % (
            response.hex(" ").upper(), ",".join("B%d" % b for b in changing) or "none")
        if frames > 1:
            note += " (multi-frame: a value must stay within one [Bx:By] run, not cross a PCI byte)"
        pid["comment"] = note
        pids.append(pid)

    extended = main_header[2] if main_header else False
    init = "ATSP7;ATST96;" if extended else "ATSP6;ATST96;"
    if main_header and not per_pid_init:
        init += _header_init(*main_header)
    return {"car_model": "Mercedes-Benz: Sprinter W906 (generated, edit me)", "init": init, "pids": pids}


def _header_init(request_id: int, response_id: int, extended: bool) -> str:
    width = 8 if extended else 3
    req = "%0*X" % (width, request_id)
    rsp = "%0*X" % (width, response_id)
    return "ATSH%s;ATCRA%s;ATFCSH%s;ATFCSD300000;ATFCSM1;" % (req, rsp, req)


# --------------------------------------------------------------------------- report

def _fmt_frame(frame: Frame) -> str:
    direction = " %-2s" % frame.direction if frame.direction else ""
    return "%12.6f%s %s [%d] %s" % (frame.ts, direction, frame.id_str(), len(frame.data), frame.data.hex(" ").upper())


def _atwua(frame: Frame) -> str:
    return "ATWUA%s,%s;" % (frame.id_str(), frame.data.hex().upper())


# Read-only services that are safe to replay as a wake-up frame over and over. A wake
# candidate that is an ECU response or a state-changing request (clear, reset, write,
# routine, session, ...) is shown with a warning instead of a ready command.
_WAKE_SAFE_SERVICES = {0x18, 0x19, 0x1A, 0x21, 0x22}


def _wake_suggestion(frame: Frame):
    """Return (atwua_string, None) for a safe wake candidate, or (None, warning)."""
    data = frame.data
    if not data:
        return None, "empty frame, not a usable wake-up frame"
    if frame.direction and frame.direction.lower() not in ("tx", "t"):
        return None, "received frame (an ECU response), do not replay it"
    pci = data[0] >> 4
    sid = data[1] if pci == 0x0 and len(data) >= 2 else (data[2] if pci == 0x1 and len(data) >= 3 else None)
    if sid is None:
        return _atwua(frame), None  # not ISO-TP, leave it to the user
    if sid >= 0x40:
        return None, "looks like an ECU response (service 0x%02X), do not replay it" % sid
    if sid == 0x3E:
        # Prefer TesterPresent with the suppress-response bit so it needs no reply
        return "ATWUA%s,%s;" % (frame.id_str(), ("023E80" + "55" * 5)), None
    if sid in _WAKE_SAFE_SERVICES:
        return _atwua(frame), None
    return None, ("service 0x%02X changes ECU state, do not use it as a repeated wake-up frame" % sid)


def report(frames: List[Frame], gap: float, burst_len: int, out=sys.stdout) -> "OrderedDict[Tuple, Exchange]":
    if not frames:
        print("No frames found.", file=out)
        return OrderedDict()

    print("== Summary", file=out)
    print("frames: %d, duration: %.3f s" % (len(frames), frames[-1].ts - frames[0].ts), file=out)
    by_id = defaultdict(list)
    for frame in frames:
        by_id[(frame.can_id, frame.extended)].append(frame)
    print("%-9s %7s %10s" % ("id", "count", "period ms"), file=out)
    for (can_id, extended), items in sorted(by_id.items()):
        period = (items[-1].ts - items[0].ts) / (len(items) - 1) * 1000 if len(items) > 1 else 0
        print("%-9s %7d %10.1f" % (items[0].id_str(), len(items), period), file=out)

    print("\n== Wake-up candidates (first frames after >= %.1f s of silence)" % gap, file=out)
    for silence, burst in find_wake_candidates(frames, gap, burst_len):
        if not burst:
            continue
        label = "log start" if silence == float("inf") else "after %.1f s silence" % silence
        print("-- %s" % label, file=out)
        for frame in burst:
            print("   " + _fmt_frame(frame), file=out)
        command, warning = _wake_suggestion(burst[0])
        print("   try: " + command if command else "   skip: " + warning, file=out)

    exchanges = match_exchanges(reassemble_isotp(frames))
    print("\n== Diagnostic exchanges", file=out)
    if not exchanges:
        print("none found", file=out)
    for exchange in exchanges.values():
        width = 8 if exchange.extended else 3
        service = exchange.request[0]
        print("%0*X -> %0*X  %-30s %-12s x%d" % (
            width, exchange.request_id, width, exchange.response_id,
            REQUEST_SERVICES.get(service, "?"), exchange.request.hex().upper(), exchange.count), file=out)
        if exchange.negative:
            print("    negative: %s" % ", ".join("NRC %02X x%d" % item for item in exchange.negative.items()), file=out)
        if exchange.responses:
            print("    last: %s" % exchange.responses[-1].hex(" ").upper(), file=out)
            if len(exchange.responses) > 1:
                ranges = byte_ranges(exchange.responses)
                length = len(exchange.responses[0])
                changing = ["B%d %02X..%02X" % (wican_byte_index(i, length), lo, hi)
                            for i, (lo, hi) in enumerate(ranges) if lo != hi]
                if changing:
                    print("    changing: %s" % ", ".join(changing), file=out)
    return exchanges


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    def positive_int(value):
        number = int(value)
        if number < 1:
            raise argparse.ArgumentTypeError("must be >= 1")
        return number

    parser.add_argument("log", help="SavvyCAN CSV, candump or SLCAN log")
    parser.add_argument("--gap", type=float, default=1.0, help="silence in seconds that marks a wake-up (default 1.0)")
    parser.add_argument("--burst", type=positive_int, default=10, help="frames to show after each silence (default 10)")
    parser.add_argument("--profile", metavar="FILE", help="write a WiCAN vehicle profile skeleton for 21xx/22xxxx reads")
    parser.add_argument("--series", action="store_true", help="print value statistics of all 21xx/22xxxx responses")
    args = parser.parse_args(argv)

    try:
        frames = parse_log(args.log)
    except ParseError as error:
        print("error: %s" % error, file=sys.stderr)
        print("supported formats: SavvyCAN GVRET CSV, candump -l, candump console, SLCAN", file=sys.stderr)
        return 2
    exchanges = report(frames, args.gap, args.burst)
    if args.series:
        print_series(value_series(reassemble_isotp(frames)))
    if args.profile:
        with open(args.profile, "w", encoding="utf-8") as handle:
            json.dump(build_profile_skeleton(exchanges), handle, indent=2)
            handle.write("\n")
        print("\nprofile skeleton written to %s" % args.profile)
    return 0 if frames else 1


if __name__ == "__main__":
    sys.exit(main())
