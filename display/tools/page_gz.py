#!/usr/bin/env python3
"""Compresses the page of the display: index.html becomes index.html.gz, as the firmware embeds it.

The display sends its page compressed to every client that says it reads gzip (display/main/web.c). The
compressed page is made by this file and nowhere else:

  - the build of the firmware runs it with the Python of ESP-IDF (display/main/CMakeLists.txt) and embeds
    what it wrote into the build directory
  - the simulation of web.c on a PC does the same (display/host/platform/Makefile) and holds the result
    against the page with --check every time it runs
  - mock_display.py and test_page.py call compress()

The result is one gzip member (RFC 1952): the page as zlib packs it at level 9, behind ten bytes that are
written here and are always the same - no file name, no time, no operating system - and in front of the
check sum and the length of the page. So the same page gives the same bytes wherever the same zlib packs
it. Another zlib (another version, zlib-ng) may pack the same page into other bytes of another length;
they unpack to the same page, and what the display sends is what the build of its firmware made.

  python3 page_gz.py PAGE PACKED             writes PACKED; nothing is written that does not unpack to PAGE
  python3 page_gz.py --check PAGE PACKED     exit status 1 unless PACKED is PAGE compressed
"""
import argparse
import gzip
import struct
import sys
import zlib

# RFC 1952: the magic number, deflate, no flag (so no name, no comment and no extra field follow), no time,
# "slowest algorithm, best compression", operating system unknown
HEAD = b"\x1f\x8b\x08\x00" + b"\x00\x00\x00\x00" + b"\x02\xff"
LEVEL = 9


def compress(page):
    """The page as one gzip member"""
    packer = zlib.compressobj(LEVEL, zlib.DEFLATED, -zlib.MAX_WBITS)
    packed = packer.compress(page) + packer.flush()
    return HEAD + packed + struct.pack("<II", zlib.crc32(page) & 0xFFFFFFFF, len(page) & 0xFFFFFFFF)


def problems(page, packed):
    """What keeps `packed` from being `page` for a client that unpacks it; empty if nothing does."""
    if packed[:len(HEAD)] != HEAD:
        return ["it does not begin with the ten bytes of a gzip member without a name and without a time"]
    try:
        # Python's gzip reads every member there is and checks each check sum and length
        if gzip.decompress(packed) != page:
            return ["it does not unpack to the page"]
        # One member and nothing behind it: a client may stop at its end, or stumble over what follows
        unpacker = zlib.decompressobj(zlib.MAX_WBITS | 16)
        unpacker.decompress(packed)
        if not unpacker.eof or unpacker.unused_data != b"":
            return ["%d bytes follow the one member that is the page" % len(unpacker.unused_data)]
    except (OSError, EOFError, zlib.error) as error:
        return ["it cannot be unpacked: %s" % error]
    return []


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="write nothing: check that PACKED is PAGE compressed")
    parser.add_argument("page", metavar="PAGE", help="the page, index.html")
    parser.add_argument("packed", metavar="PACKED", help="the compressed page, index.html.gz")
    options = parser.parse_args(argv)

    with open(options.page, "rb") as file:
        page = file.read()
    if options.check:
        with open(options.packed, "rb") as file:
            packed = file.read()
    else:
        packed = compress(page)

    found = problems(page, packed)
    for text in found:
        print("PROBLEM: %s: %s" % (options.packed, text))
    if found:
        return 1
    if not options.check:
        with open(options.packed, "wb") as file:
            file.write(packed)
    print("OK: %s, %d bytes, is %s, %d bytes, compressed" % (options.packed, len(packed), options.page, len(page)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
