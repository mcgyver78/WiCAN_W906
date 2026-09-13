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


if __name__ == "__main__":
    unittest.main()
