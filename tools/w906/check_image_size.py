#!/usr/bin/env python3
"""Fails when the firmware image leaves too little room in its OTA slot.

The image of the WiCAN OBD (ESP32-C3) fills its 1740K slot almost completely. This guard makes the
build red before an addition no longer fits, instead of at the next OTA update.

  python3 check_image_size.py --image "build/wican-fw_*.bin" --partitions wican_partitions_table.csv --min-free 32768
"""
import argparse
import glob
import os
import sys

UNITS = {"K": 1024, "M": 1024 * 1024}


def parse_size(text):
    """Size as written in an ESP-IDF partition table: 0x1000, 4096, 1740K, 4M."""
    text = text.strip()
    if not text:
        raise ValueError("empty size")
    unit = text[-1].upper()
    if unit in UNITS:
        return int(text[:-1], 0) * UNITS[unit]
    return int(text, 0)


def app_slots(csv_text):
    """Sizes of all app partitions of a partition table, by name."""
    slots = {}
    for line in csv_text.splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        fields = [field.strip() for field in line.split(",")]
        if len(fields) < 5 or fields[1] != "app":
            continue
        slots[fields[0]] = parse_size(fields[4])
    return slots


def check(image_size, slots, min_free):
    """Returns (ok, message)."""
    if not slots:
        return False, "no app partition in the partition table"
    name, slot = min(slots.items(), key=lambda item: item[1])
    free = slot - image_size
    message = ("image %d bytes, smallest app slot %s %d bytes, free %d bytes, required %d bytes"
               % (image_size, name, slot, free, min_free))
    return free >= min_free, message


def main(arguments=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--image", required=True, help="path or glob of the app image, exactly one file")
    parser.add_argument("--partitions", required=True, help="partition table CSV")
    parser.add_argument("--min-free", required=True, type=lambda text: int(text, 0), help="bytes that must stay free")
    options = parser.parse_args(arguments)

    images = sorted(glob.glob(options.image))
    if len(images) != 1:
        print("expected exactly one image for %s, found %d: %s" % (options.image, len(images), images))
        return 1
    with open(options.partitions, encoding="utf-8") as file:
        slots = app_slots(file.read())

    ok, message = check(os.path.getsize(images[0]), slots, options.min_free)
    print("%s: %s" % ("OK" if ok else "TOO LARGE", message))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
