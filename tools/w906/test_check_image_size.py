import os
import tempfile
import unittest

import check_image_size

TABLE = """# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,      0x9000,  0x4000,
otadata,  data, ota,      0xd000,  0x2000,
ota_0,  app,  ota_0,  0x10000,  1740K,
ota_1,    app,  ota_1,    ,         1740K,
storage,  data, spiffs, ,        300K,
"""

# Measured in CI on 2026-10-03 (run 37128631704): image of 1737840 bytes in a slot of 1781760 bytes
IMAGE = 1737840
FREE = 43920


class CheckImageSize(unittest.TestCase):
    def test_sizes_as_written_in_partition_tables(self):
        self.assertEqual(check_image_size.parse_size("1740K"), 1781760)
        self.assertEqual(check_image_size.parse_size("0x1b3000"), 1781760)
        self.assertEqual(check_image_size.parse_size(" 4M "), 4194304)
        self.assertEqual(check_image_size.parse_size("4096"), 4096)
        with self.assertRaises(ValueError):
            check_image_size.parse_size("")

    def test_only_app_partitions_count(self):
        self.assertEqual(check_image_size.app_slots(TABLE), {"ota_0": 1781760, "ota_1": 1781760})

    def test_smallest_slot_decides(self):
        ok, message = check_image_size.check(1000, {"ota_0": 5000, "ota_1": 2000}, 1500)
        self.assertFalse(ok)
        self.assertIn("ota_1", message)

    def test_measured_image_passes_with_32_kb_and_fails_with_48_kb(self):
        slots = check_image_size.app_slots(TABLE)
        self.assertTrue(check_image_size.check(IMAGE, slots, 32768)[0])
        self.assertFalse(check_image_size.check(IMAGE, slots, 49152)[0])

    def test_limit_is_inclusive(self):
        slots = check_image_size.app_slots(TABLE)
        self.assertTrue(check_image_size.check(IMAGE, slots, FREE)[0])
        self.assertFalse(check_image_size.check(IMAGE, slots, FREE + 1)[0])
        self.assertFalse(check_image_size.check(IMAGE + 1, slots, FREE)[0])

    def test_table_without_app_partition_fails(self):
        self.assertFalse(check_image_size.check(IMAGE, {}, 0)[0])

    def test_command_line(self):
        with tempfile.TemporaryDirectory() as directory:
            table = os.path.join(directory, "partitions.csv")
            image = os.path.join(directory, "wican-fw_obd_test.bin")
            with open(table, "w", encoding="utf-8") as file:
                file.write(TABLE)
            with open(image, "wb") as file:
                file.write(b"\0" * IMAGE)
            pattern = os.path.join(directory, "wican-fw_*.bin")
            base = ["--image", pattern, "--partitions", table]
            self.assertEqual(check_image_size.main(base + ["--min-free", "32768"]), 0)
            self.assertEqual(check_image_size.main(base + ["--min-free", "49152"]), 1)
            # No image and two images are errors, not a pass
            self.assertEqual(check_image_size.main(["--image", os.path.join(directory, "none*.bin"),
                                                    "--partitions", table, "--min-free", "0"]), 1)
            with open(os.path.join(directory, "wican-fw_obd_other.bin"), "wb") as file:
                file.write(b"\0")
            self.assertEqual(check_image_size.main(base + ["--min-free", "0"]), 1)


if __name__ == "__main__":
    unittest.main()
