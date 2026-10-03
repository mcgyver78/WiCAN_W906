#!/usr/bin/env python3
"""Checks the partition table of the display and, optionally, that the built image fits.

The table cannot be changed by an OTA update, so its properties are checked on every build:
two OTA slots of the same size, otadata, no overlap, everything inside the flash, app slots
aligned to 64 KB, and an image that leaves a fifth of its slot free.

  python3 check_partitions.py --partitions ../partitions.csv --flash-size 16M [--image "../build/wican-display.bin"]
"""
import argparse
import glob
import os
import sys

UNITS = {"K": 1024, "M": 1024 * 1024}
APP_ALIGN = 0x10000
TABLE_END = 0x9000      # bootloader and partition table end here


def parse_size(text):
    text = text.strip()
    if not text:
        raise ValueError("empty size")
    unit = text[-1].upper()
    if unit in UNITS:
        return int(text[:-1], 0) * UNITS[unit]
    return int(text, 0)


def parse_table(csv_text):
    """List of partitions as dicts with name, type, subtype, offset, size. Offsets are required."""
    partitions = []
    for number, line in enumerate(csv_text.splitlines(), 1):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        fields = [field.strip() for field in line.split(",")]
        if len(fields) < 5 or not fields[3]:
            raise ValueError("line %d: name, type, subtype, offset and size are required" % number)
        partitions.append({"name": fields[0], "type": fields[1], "subtype": fields[2],
                           "offset": parse_size(fields[3]), "size": parse_size(fields[4])})
    return partitions


def problems(partitions, flash_size, image_size=None, free_share=0.2):
    """Texts of everything that is wrong, empty if the table is fine."""
    found = []
    by_name = {p["name"]: p for p in partitions}
    if len(by_name) != len(partitions):
        found.append("partition names are not unique")

    slots = [p for p in partitions if p["type"] == "app" and p["subtype"].startswith("ota_")]
    if sorted(p["subtype"] for p in slots) != ["ota_0", "ota_1"]:
        found.append("exactly the two app slots ota_0 and ota_1 are required")
    elif slots[0]["size"] != slots[1]["size"]:
        found.append("ota_0 and ota_1 differ in size")
    if any(p["type"] == "app" and p["subtype"] == "factory" for p in partitions):
        found.append("a factory partition changes the rollback target")
    if not any(p["type"] == "data" and p["subtype"] == "ota" and p["size"] >= 0x2000 for p in partitions):
        found.append("otadata of at least 0x2000 bytes is missing")
    if not any(p["type"] == "data" and p["subtype"] == "nvs" for p in partitions):
        found.append("nvs is missing")

    ordered = sorted(partitions, key=lambda p: p["offset"])
    end = TABLE_END
    for p in ordered:
        if p["size"] <= 0:
            found.append("%s has no size" % p["name"])
        if p["offset"] < end:
            found.append("%s overlaps what is before it" % p["name"])
        if p["type"] == "app" and p["offset"] % APP_ALIGN != 0:
            found.append("%s is not aligned to 64 KB" % p["name"])
        end = max(end, p["offset"] + p["size"])
    if end > flash_size:
        found.append("the table ends at 0x%x, behind the flash of 0x%x" % (end, flash_size))

    if image_size is not None and slots:
        slot = min(p["size"] for p in slots)
        if image_size > slot * (1 - free_share):
            found.append("image of %d bytes leaves less than %d %% of the %d byte slot free"
                         % (image_size, round(free_share * 100), slot))
    return found


def main(arguments=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--partitions", required=True)
    parser.add_argument("--flash-size", required=True, type=parse_size)
    parser.add_argument("--image", help="path or glob of the app image, exactly one file")
    options = parser.parse_args(arguments)

    image_size = None
    if options.image:
        images = sorted(glob.glob(options.image))
        if len(images) != 1:
            print("expected exactly one image for %s, found %d" % (options.image, len(images)))
            return 1
        image_size = os.path.getsize(images[0])

    with open(options.partitions, encoding="utf-8") as file:
        try:
            partitions = parse_table(file.read())
        except ValueError as error:
            print("partition table: %s" % error)
            return 1

    found = problems(partitions, options.flash_size, image_size)
    for text in found:
        print("PROBLEM: %s" % text)
    if not found:
        print("OK: %d partitions%s" % (len(partitions), "" if image_size is None else ", image %d bytes" % image_size))
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
