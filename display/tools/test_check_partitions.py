import os
import unittest

import check_partitions

HERE = os.path.dirname(os.path.abspath(__file__))
FLASH = 16 * 1024 * 1024


def table():
    with open(os.path.join(HERE, "..", "partitions.csv"), encoding="utf-8") as file:
        return check_partitions.parse_table(file.read())


def without(partitions, name):
    return [p for p in partitions if p["name"] != name]


def changed(partitions, name, **fields):
    return [dict(p, **fields) if p["name"] == name else p for p in partitions]


class CheckPartitions(unittest.TestCase):
    def test_the_table_of_the_display_is_fine(self):
        self.assertEqual(check_partitions.problems(table(), FLASH), [])

    def test_layout_as_designed(self):
        by_name = {p["name"]: p for p in table()}
        self.assertEqual(by_name["ota_0"]["size"], 0x400000)
        self.assertEqual(by_name["ota_1"]["offset"], 0x430000)
        self.assertEqual(by_name["nvs"]["size"], 0x20000)
        self.assertEqual(by_name["store"]["offset"] + by_name["store"]["size"], 0xC50000)

    def test_every_broken_table_is_reported(self):
        good = table()
        broken = {
            "second slot missing": without(good, "ota_1"),
            "slots differ": changed(good, "ota_1", size=0x300000),
            "otadata missing": without(good, "otadata"),
            "otadata too small": changed(good, "otadata", size=0x1000),
            "nvs missing": without(good, "nvs"),
            "overlap": changed(good, "ota_1", offset=0x420000),
            "not aligned": changed(good, "ota_0", offset=0x31000, size=0x3F0000),
            "behind the flash": changed(good, "store", size=0x800000),
            "starts inside the partition table": changed(good, "nvs", offset=0x8000),
            "factory partition": good + [{"name": "factory", "type": "app", "subtype": "factory",
                                          "offset": 0xC50000, "size": 0x100000}],
            "duplicate name": good + [{"name": "nvs", "type": "data", "subtype": "fat",
                                       "offset": 0xC50000, "size": 0x1000}],
            "no size": changed(good, "coredump", size=0),
        }
        for name, partitions in broken.items():
            with self.subTest(name):
                self.assertNotEqual(check_partitions.problems(partitions, FLASH), [])

    def test_flash_size_matters(self):
        self.assertNotEqual(check_partitions.problems(table(), 8 * 1024 * 1024), [])

    def test_image_has_to_leave_a_fifth_of_the_slot_free(self):
        limit = int(0x400000 * 0.8)
        self.assertEqual(check_partitions.problems(table(), FLASH, limit), [])
        self.assertNotEqual(check_partitions.problems(table(), FLASH, limit + 1), [])

    def test_offsets_are_required(self):
        with self.assertRaises(ValueError):
            check_partitions.parse_table("ota_1, app, ota_1, , 4M\n")

    def test_command_line(self):
        csv = os.path.join(HERE, "..", "partitions.csv")
        self.assertEqual(check_partitions.main(["--partitions", csv, "--flash-size", "16M"]), 0)
        self.assertEqual(check_partitions.main(["--partitions", csv, "--flash-size", "8M"]), 1)
        self.assertEqual(check_partitions.main(["--partitions", csv, "--flash-size", "16M",
                                                "--image", os.path.join(HERE, "no-such-*.bin")]), 1)


if __name__ == "__main__":
    unittest.main()
