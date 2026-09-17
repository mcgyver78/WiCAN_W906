import json
import os
import tempfile
import unittest

import make_car_data as mcd


class MakeCarDataTest(unittest.TestCase):
    def setUp(self):
        with open(mcd.DEFAULT_PROFILE, encoding="utf-8") as handle:
            self.profile = json.load(handle)
        with open(mcd.PARAMS, encoding="utf-8") as handle:
            self.params = json.load(handle)

    def test_real_profile_converts(self):
        config = mcd.convert(self.profile, self.params, 1000)
        cars = config["auto_pid_car_data"]["cars"]
        self.assertEqual(len(cars), 1)
        car = cars[0]
        self.assertEqual(len(car["pids"]), len(self.profile["pids"]))
        for pid in car["pids"]:
            self.assertIn("pid", pid)
            for param in pid["parameters"]:
                self.assertIn("name", param)
                self.assertIn("expression", param)
                # the firmware reads every parameter field as a string
                for value in param.values():
                    self.assertIsInstance(value, str)
                self.assertIn("type", param)
                self.assertIn("send_to", param)

    def test_every_parameter_is_in_params(self):
        # convert() raises if a profile parameter is missing from params.json; running
        # it on the shipped profile guards the profile and params.json staying in sync
        mcd.convert(self.profile, self.params, 1000)

    def test_settings_merged_as_strings(self):
        config = mcd.convert(self.profile, self.params, 1000)
        param = config["auto_pid_car_data"]["cars"][0]["pids"][0]["parameters"][0]
        for key, value in self.params[param["name"]]["settings"].items():
            self.assertEqual(param[key], str(value))

    def test_uniform_period_is_load_bearing(self):
        # The idle-mode fail counter is global; a single fast-failing PID could force a
        # running vehicle into idle only if periods were staggered. The shipped config
        # keeps one period for every parameter, which is what makes that unreachable.
        # Guard it: nothing in params.json may introduce a per-parameter period.
        config = mcd.convert(self.profile, self.params, 1234)
        for pid in config["auto_pid_car_data"]["cars"][0]["pids"]:
            for param in pid["parameters"]:
                self.assertEqual(param["period"], "1234")

    def test_extends_is_rejected(self):
        with self.assertRaises(ValueError):
            mcd.convert({"extends": "base", "pids": []}, self.params, 1000)

    def test_unknown_parameter_is_rejected(self):
        profile = {"car_model": "x", "pids": [{"pid": "22F190", "parameters": {"NOT_A_PARAM": "B4"}}]}
        with self.assertRaises(ValueError):
            mcd.convert(profile, self.params, 1000)

    def test_profile_only_fields_stripped(self):
        profile = dict(self.profile)
        profile["note"] = "x"
        profile["comment"] = "y"
        car = mcd.convert(profile, self.params, 1000)["auto_pid_car_data"]["cars"][0]
        for field in mcd.PROFILE_ONLY_FIELDS:
            self.assertNotIn(field, car)

    def test_auto_pid_settings_enable_idle_and_car_specific(self):
        settings = mcd.auto_pid_settings("Mercedes W906", "wican/x", 1000)
        self.assertEqual(settings["idle_mode"], "enable")
        self.assertEqual(settings["car_specific"], "enable")
        self.assertEqual(settings["autopid_polling"], "enable")
        self.assertEqual(settings["cycle"], "1000")

    def test_main_writes_valid_json(self):
        out = tempfile.NamedTemporaryFile("w", suffix=".json", delete=False)
        out.close()
        try:
            self.assertEqual(mcd.main([mcd.DEFAULT_PROFILE, "-o", out.name]), 0)
            with open(out.name, encoding="utf-8") as handle:
                config = json.load(handle)
            self.assertIn("auto_pid_car_data", config)
            self.assertEqual(config["auto_pid"]["idle_mode"], "enable")
        finally:
            os.unlink(out.name)


if __name__ == "__main__":
    unittest.main()
