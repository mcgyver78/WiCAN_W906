"""Checks page_gz.py, which compresses the page of the display for the firmware, for the simulation of
web.c and for the mock.

What it makes has to unpack to the page, be the same bytes for the same page, and carry neither a name nor
a time. Its own check (--check, which the Makefile of the simulation runs before every run) has to find a
file that is not the page compressed: each way of being wrong is made once and has to be reported.

  python -m unittest -v          in display/tools, as the CI does
"""
import contextlib
import gzip
import io
import os
import struct
import tempfile
import unittest
import zlib
from unittest import mock

import page_gz

HERE = os.path.dirname(os.path.abspath(__file__))
PAGE = os.path.join(HERE, "..", "main", "web", "index.html")


def page():
    with open(PAGE, "rb") as file:
        return file.read()


def run(arguments):
    """main() with these arguments: (exit status, what it printed)"""
    printed = io.StringIO()
    with contextlib.redirect_stdout(printed):
        status = page_gz.main(arguments)
    return status, printed.getvalue()


class Compress(unittest.TestCase):
    def test_it_unpacks_to_what_was_compressed(self):
        for data in (page(), b"", b"x", bytes(range(256)) * 300):
            with self.subTest(length=len(data)):
                packed = page_gz.compress(data)
                self.assertTrue(gzip.decompress(packed) == data)
                self.assertEqual(page_gz.problems(data, packed), [])

    def test_the_page_gets_smaller(self):
        self.assertLess(len(page_gz.compress(page())), len(page()) // 2)

    def test_one_member_without_a_name_and_without_a_time(self):
        data = page()
        packed = page_gz.compress(data)
        # RFC 1952: magic, deflate, no flag, time 0, best compression, system unknown; behind the member
        # the check sum and the length of what it unpacks to
        self.assertEqual(packed[:10], b"\x1f\x8b\x08\x00\x00\x00\x00\x00\x02\xff")
        self.assertEqual(packed[-8:], struct.pack("<II", zlib.crc32(data), len(data)))
        # What lies between is the page and nothing else: no name of a file in front of it
        self.assertTrue(zlib.decompress(packed[10:-8], -zlib.MAX_WBITS) == data)
        self.assertNotIn(b"index.html", packed[:64])

    def test_the_same_page_gives_the_same_bytes(self):
        data = page()
        self.assertTrue(page_gz.compress(data) == page_gz.compress(bytes(data)))

    def test_level_9(self):
        # Not larger than what the weakest level makes of the page, and that level makes more of it
        data = page()
        weak = zlib.compressobj(1, zlib.DEFLATED, -zlib.MAX_WBITS)
        self.assertLess(len(page_gz.compress(data)) - 18, len(weak.compress(data) + weak.flush()))


class Problems(unittest.TestCase):
    def setUp(self):
        self.page = page()
        self.packed = page_gz.compress(self.page)

    def test_none_for_the_page_compressed(self):
        self.assertEqual(page_gz.problems(self.page, self.packed), [])

    def test_each_way_of_being_wrong_is_found(self):
        named = io.BytesIO()
        with gzip.GzipFile(filename="index.html", mode="wb", fileobj=named, compresslevel=9, mtime=0) as file:
            file.write(self.page)
        wrong = {
            "another page": page_gz.compress(self.page + b"\n"),
            "the page as it is": self.page,
            "nothing": b"",
            "a name in it": named.getvalue(),
            "a time in it": gzip.compress(self.page, 9, mtime=1760112000),
            "cut by a byte": self.packed[:-1],
            "cut in the middle": self.packed[:len(self.packed) // 2],
            "a byte behind it": self.packed + b"x",
            "zeros behind it": self.packed + b"\0" * 4,
            "a second member": self.packed + self.packed,
            "an empty second member": self.packed + page_gz.compress(b""),
            "a wrong check sum": self.packed[:-8] + b"\0\0\0\0" + self.packed[-4:],
            "a wrong length": self.packed[:-4] + struct.pack("<I", len(self.page) + 1),
            "a damaged byte": self.packed[:5000] + bytes([self.packed[5000] ^ 0x10]) + self.packed[5001:],
        }
        for what, packed in wrong.items():
            with self.subTest(wrong=what):
                self.assertEqual(len(page_gz.problems(self.page, packed)), 1)


class Tool(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.source = os.path.join(directory.name, "index.html")
        self.packed = os.path.join(directory.name, "index.html.gz")
        with open(self.source, "wb") as file:
            file.write(page())

    def test_it_writes_what_compress_makes_and_its_check_takes_it(self):
        status, printed = run([self.source, self.packed])
        self.assertEqual((status, printed.startswith("OK: ")), (0, True))
        with open(self.packed, "rb") as file:
            self.assertTrue(file.read() == page_gz.compress(page()))
        status, printed = run(["--check", self.source, self.packed])
        self.assertEqual((status, printed.startswith("OK: ")), (0, True))

    def test_the_check_finds_a_page_that_changed_since(self):
        run([self.source, self.packed])
        with open(self.source, "ab") as file:
            file.write(b"<!-- later -->")
        status, printed = run(["--check", self.source, self.packed])
        self.assertEqual((status, printed.startswith("PROBLEM: ")), (1, True))

    def test_the_check_writes_nothing(self):
        with open(self.packed, "wb") as file:
            file.write(b"not the page")
        self.assertEqual(run(["--check", self.source, self.packed])[0], 1)
        with open(self.packed, "rb") as file:
            self.assertEqual(file.read(), b"not the page")

    def test_nothing_is_written_that_does_not_unpack_to_the_page(self):
        with mock.patch.object(page_gz, "compress", lambda data: page_gz.HEAD + b"\x03\x00" + b"\0" * 8):
            status, printed = run([self.source, self.packed])
        self.assertEqual((status, printed.startswith("PROBLEM: ")), (1, True))
        self.assertFalse(os.path.exists(self.packed))


if __name__ == "__main__":
    unittest.main()
