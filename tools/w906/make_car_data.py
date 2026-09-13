#!/usr/bin/env python3
"""Turn a WiCAN vehicle profile into a file for the WiCAN web interface.

The WiCAN web interface only lists the profiles published in
meatpiHQ/wican-fw. This script converts a profile from vehicle_profiles/
into the format /store_car_data expects and wraps it in a config backup
file ("auto_pid_car_data"), which can be loaded with the config upload
in the web interface.
"""

import argparse
import json
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DEFAULT_PROFILE = os.path.join(ROOT, "vehicle_profiles", "mercedes", "sprinter_w906_om651.json")
PARAMS = os.path.join(ROOT, ".vehicle_profiles", "params.json")
PROFILE_ONLY_FIELDS = ("note", "comment", "add_to_docs", "extends")


def convert(profile: dict, params: dict, period_ms: int) -> dict:
    if "extends" in profile:
        raise ValueError("profiles using 'extends' are not supported")

    car = {key: value for key, value in profile.items() if key not in PROFILE_ONLY_FIELDS}
    car["pids"] = []
    for pid in profile["pids"]:
        parameters = []
        for name, expression in pid["parameters"].items():
            if name not in params:
                raise ValueError("parameter %s is missing in params.json" % name)
            parameter = {"name": name, "expression": expression}
            # The firmware reads these fields as strings
            parameter.update({key: str(value) for key, value in params[name]["settings"].items()})
            parameter.setdefault("period", str(period_ms))
            parameter.setdefault("type", "Default")
            parameter.setdefault("send_to", "")
            parameters.append(parameter)
        car["pids"].append({**{k: v for k, v in pid.items() if k != "parameters"}, "parameters": parameters})
    return {"auto_pid_car_data": {"cars": [car]}}


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("profile", nargs="?", default=DEFAULT_PROFILE, help="vehicle profile (default: W906 OM651)")
    parser.add_argument("-o", "--output", default="wican_w906_car_data.json", help="output file")
    parser.add_argument("--period", type=int, default=5000, help="polling period per parameter in ms (default 5000)")
    args = parser.parse_args(argv)

    with open(args.profile, encoding="utf-8") as handle:
        profile = json.load(handle)
    with open(PARAMS, encoding="utf-8") as handle:
        params = json.load(handle)

    with open(args.output, "w", encoding="utf-8") as handle:
        json.dump(convert(profile, params, args.period), handle, indent=2, ensure_ascii=False)
        handle.write("\n")
    print("written %s" % args.output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
