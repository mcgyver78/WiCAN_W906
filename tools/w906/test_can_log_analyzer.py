import io
import os
import tempfile
import unittest

import can_log_analyzer as cla

SAVVYCAN_LOG = """Time Stamp,ID,Extended,Dir,Bus,LEN,D1,D2,D3,D4,D5,D6,D7,D8
1000000,0000001C,false,Tx,0,8,02,10,92,00,00,00,00,00
1020000,000007E8,false,Rx,0,8,02,50,92,00,00,00,00,00
1100000,000007E0,false,Tx,0,8,03,22,80,32,00,00,00,00
1110000,000007E8,false,Rx,0,8,05,62,80,32,0B,B8,00,00
2100000,000007E0,false,Tx,0,8,03,22,80,32,00,00,00,00
2110000,000007E8,false,Rx,0,8,05,62,80,32,0C,1C,00,00
2200000,000007E0,false,Tx,0,8,03,22,F1,90,00,00,00,00
2210000,000007E8,false,Rx,0,8,10,14,62,F1,90,57,44,42
2215000,000007E0,false,Tx,0,8,30,00,00,00,00,00,00,00
2220000,000007E8,false,Rx,0,8,21,39,30,36,31,33,33,31
2230000,000007E8,false,Rx,0,8,22,4E,31,32,33,34,35,36
2300000,000007E1,false,Tx,0,8,02,21,90,00,00,00,00,00
2310000,000007E9,false,Rx,0,8,03,7F,21,12,00,00,00,00
9300000,0000001C,false,Tx,0,8,02,10,92,00,00,00,00,00
"""

CANDUMP_LOG = """(1694600000.000000) can0 01C#0210920000000000
(1694600000.100000) can0 7E0#0322803200000000
(1694600000.110000) can0 7E8#056280320BB80000
"""


def write_tmp(content):
    handle = tempfile.NamedTemporaryFile("w", suffix=".log", delete=False)
    handle.write(content)
    handle.close()
    return handle.name


class AnalyzerTest(unittest.TestCase):
    def setUp(self):
        self.path = write_tmp(SAVVYCAN_LOG)

    def tearDown(self):
        os.unlink(self.path)

    def test_parse_savvycan(self):
        frames = cla.parse_log(self.path)
        self.assertEqual(len(frames), 14)
        self.assertAlmostEqual(frames[0].ts, 1.0)
        self.assertEqual(frames[0].can_id, 0x1C)
        self.assertFalse(frames[0].extended)
        self.assertEqual(frames[0].direction, "Tx")
        self.assertEqual(frames[0].data, bytes.fromhex("0210920000000000"))

    def test_parse_candump(self):
        path = write_tmp(CANDUMP_LOG)
        try:
            frames = cla.parse_log(path)
        finally:
            os.unlink(path)
        self.assertEqual([f.can_id for f in frames], [0x01C, 0x7E0, 0x7E8])
        self.assertEqual(frames[2].data, bytes.fromhex("056280320BB80000"))

    def test_wake_candidates(self):
        bursts = cla.find_wake_candidates(cla.parse_log(self.path), gap=5.0, count=2)
        self.assertEqual(len(bursts), 2)
        self.assertEqual(bursts[0][1][0].can_id, 0x1C)
        self.assertAlmostEqual(bursts[1][0], 6.99)
        self.assertEqual(cla._atwua(bursts[1][1][0]), "ATWUA01C,0210920000000000;")

    def test_isotp_multiframe(self):
        messages = cla.reassemble_isotp(cla.parse_log(self.path))
        vin = [m for m in messages if m.payload[:3] == bytes.fromhex("62F190")]
        self.assertEqual(len(vin), 1)
        self.assertEqual(vin[0].payload[3:].decode(), "WDB9061331N123456")

    def test_exchanges(self):
        exchanges = cla.match_exchanges(cla.reassemble_isotp(cla.parse_log(self.path)))
        dids = {(e.request_id, e.response_id, e.request.hex()): e for e in exchanges.values()}
        oil = dids[(0x7E0, 0x7E8, "228032")]
        self.assertEqual(oil.count, 2)
        self.assertEqual(cla.byte_ranges(oil.responses)[3:], [(0x0B, 0x0C), (0x1C, 0xB8)])
        self.assertIn((0x7E0, 0x7E8, "22f190"), dids)
        self.assertEqual(dids[(0x7E1, 0x7E9, "2190")].negative, {0x12: 1})
        self.assertIn((0x1C, 0x7E8, "1092"), dids)

    def test_profile_skeleton(self):
        exchanges = cla.match_exchanges(cla.reassemble_isotp(cla.parse_log(self.path)))
        profile = cla.build_profile_skeleton(exchanges)
        self.assertEqual(profile["init"], "ATSP6;ATST96;ATSH7E0;ATCRA7E8;ATFCSH7E0;ATFCSD300000;ATFCSM1;")
        pids = {p["pid"]: p for p in profile["pids"]}
        self.assertEqual(set(pids), {"2280321", "22F1903"})
        self.assertEqual(pids["2280321"]["parameters"], {"TODO_228032": "B4"})
        self.assertIn("B4,B5", pids["2280321"]["comment"])

    def test_value_series(self):
        series = cla.value_series(cla.reassemble_isotp(cla.parse_log(self.path)))
        self.assertEqual(list(series), [(0x7E8, "228032")])
        self.assertEqual([v for _, v in series[(0x7E8, "228032")]], [0x0BB8, 0x0C1C])
        out = io.StringIO()
        cla.print_series(series, out=out)
        self.assertIn("228032", out.getvalue())

    def test_report_runs(self):
        out = io.StringIO()
        cla.report(cla.parse_log(self.path), gap=1.0, burst_len=3, out=out)
        text = out.getvalue()
        self.assertIn("Wake-up candidates", text)
        self.assertIn("ReadDataByIdentifier", text)
        self.assertIn("NRC 12 x1", text)


# SLCAN: t<3 hex id><dlc><data>, optionally + 4 digit ms timestamp
SLCAN_LOG = "t7E080322F19000000000\rt7E8505148001122\r"


class ParserRobustnessTest(unittest.TestCase):
    def setUp(self):
        self.path = write_tmp(SAVVYCAN_LOG)

    def tearDown(self):
        os.unlink(self.path)

    def parse(self, content):
        path = write_tmp(content)
        try:
            return cla.parse_log(path)
        finally:
            os.unlink(path)

    def test_slcan(self):
        frames = self.parse(SLCAN_LOG)
        self.assertEqual([f.can_id for f in frames], [0x7E0, 0x7E8])
        self.assertFalse(frames[0].extended)
        self.assertEqual(frames[0].data, bytes.fromhex("0322F19000000000"))
        self.assertEqual(len(frames[1].data), 5)

    def test_slcan_timestamp_rollover(self):
        # two frames near the 60000 ms (0xEA60) wrap must stay in order
        frames = self.parse("t7E0201AAE900\rt7E0201BB0010\r")
        self.assertEqual(len(frames), 2)
        self.assertLess(frames[0].ts, frames[1].ts)

    def test_first_frame_length_zero(self):
        # 10 00 .. then 21 .. must not produce an empty ISO-TP message or crash
        frames = self.parse("(1.0) can0 3E9#1000AABBCCDDEEFF\n(1.1) can0 3E9#2111223344556677\n")
        messages = cla.reassemble_isotp(frames)
        self.assertEqual(messages, [])
        # the whole CLI must still run and report
        out = io.StringIO()
        cla.report(frames, gap=1.0, burst_len=3, out=out)
        self.assertIn("Summary", out.getvalue())

    def test_nrc_78_keeps_request_open(self):
        log = (
            "(1.000000) can0 7E0#0322803200000000\n"
            "(1.010000) can0 7E8#037F227800000000\n"   # response pending
            "(1.400000) can0 7E8#056280320BB80000\n"   # final answer 400 ms later
        )
        exchanges = cla.match_exchanges(cla.reassemble_isotp(self.parse(log)))
        key = (0x7E0, 0x7E8, bytes.fromhex("228032"))
        self.assertIn(key, exchanges)
        self.assertEqual(exchanges[key].negative, {0x78: 1})
        self.assertEqual(exchanges[key].responses[-1], bytes.fromhex("6280320BB8"))

    def test_multiframe_byte_numbering(self):
        # 21 01 block: first frame + one consecutive frame, tester FC on 7E1
        log = (
            "(1.0) can0 7E1#0221010000000000\n"
            "(1.01) can0 7E9#100C610111223345\n"
            "(1.02) can0 7E1#3000000000000000\n"
            "(1.03) can0 7E9#2155667788990000\n"
        )
        exchanges = cla.match_exchanges(cla.reassemble_isotp(self.parse(log)))
        profile = cla.build_profile_skeleton(exchanges)
        pid = profile["pids"][0]
        # payload 61 01 11 22 33 45 55 66 77 88 99; value bytes start at payload index 2 -> B4
        self.assertEqual(pid["parameters"], {"TODO_2101": "B4"})
        self.assertIn("multi-frame", pid["comment"])

    def test_pairing_prefers_matching_id(self):
        # two ECUs answer the same DID with overlapping requests; ids must not be crossed
        log = (
            "(1.000) can0 7E0#03228032AA\n"
            "(1.001) can0 7E1#03228032AA\n"
            "(1.010) can0 7E8#0562803201AA\n"
            "(1.011) can0 7E9#0562803202AA\n"
        )
        exchanges = cla.match_exchanges(cla.reassemble_isotp(self.parse(log)))
        self.assertIn((0x7E0, 0x7E8, bytes.fromhex("228032")), exchanges)
        self.assertIn((0x7E1, 0x7E9, bytes.fromhex("228032")), exchanges)

    def test_wake_suggestion_filters_dangerous(self):
        clear = cla.Frame(0.0, 0x7E0, False, bytes.fromhex("0414FFFFFF000000"), "Tx")
        self.assertEqual(cla._wake_suggestion(clear)[0], None)
        reset = cla.Frame(0.0, 0x7E0, False, bytes.fromhex("0211010000000000"), "Tx")
        self.assertEqual(cla._wake_suggestion(reset)[0], None)
        answer = cla.Frame(0.0, 0x7E8, False, bytes.fromhex("0562803201020000"), "Rx")
        self.assertEqual(cla._wake_suggestion(answer)[0], None)
        tester = cla.Frame(0.0, 0x7E0, False, bytes.fromhex("023E000000000000"), "Tx")
        self.assertEqual(cla._wake_suggestion(tester)[0], "ATWUA7E0,023E805555555555;")
        read = cla.Frame(0.0, 0x7E0, False, bytes.fromhex("0322803200000000"), "Tx")
        self.assertEqual(cla._wake_suggestion(read)[0], "ATWUA7E0,0322803200000000;")

    def test_truncated_lines_are_skipped(self):
        frames = self.parse(
            "Time Stamp,ID,Extended,Dir,Bus,LEN,D1,D2,D3,D4,D5,D6,D7,D8\n"
            "1000000,000007E0,false,Tx,0,3,03,22,80\n"
            "1010000,000007E8,false,Rx,0,\n"   # truncated, must be skipped not crash
        )
        self.assertEqual(len(frames), 1)

    def test_candump_rtr_and_fd_ignored(self):
        # RTR and CAN FD lines must not become zero-data frames
        frames = self.parse("(1.0) can0 7E0#R\n(1.1) can0 7E0##155\n(1.2) can0 7E0#0322803200000000\n")
        self.assertEqual([f.can_id for f in frames], [0x7E0])
        self.assertEqual(len(frames[0].data), 8)

    def test_no_frames_exit_code(self):
        path = write_tmp("nothing to see here\n")
        try:
            self.assertEqual(cla.main([path]), 1)
        finally:
            os.unlink(path)

    def test_burst_must_be_positive(self):
        with self.assertRaises(SystemExit):
            cla.main([self.path, "--burst", "0"])


if __name__ == "__main__":
    unittest.main()
