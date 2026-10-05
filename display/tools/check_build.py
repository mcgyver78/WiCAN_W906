#!/usr/bin/env python3
"""Checks that a definition made for the sources of LVGL has reached the compiler.

components/ui/CMakeLists.txt defines LV_ASSERT_HANDLER for the sources of LVGL, which are another
component: a failed assertion of LVGL has to end in abort(), which restarts the display, and not in the
loop without end LVGL has for it. A build that succeeds does not show that the definition arrived - without
it LVGL builds just as well, with the loop. What the compiler was really given is in the
compile_commands.json CMake writes next to the build, and that is what is read here.

  python3 check_build.py --commands ../build/compile_commands.json
"""
import argparse
import json
import shlex
import sys

LVGL_SOURCES = "/lvgl__lvgl/src/"
NAME = "LV_ASSERT_HANDLER"
DEFINITION = NAME + "=if((abort(),0)){}"
# LVGL 9.5.0 has several hundred sources. Fewer than this and the list is not the one of a whole build.
AT_LEAST = 100


def arguments(entry):
    """What the compiler is called with for one source, as the shell would hand it over."""
    if "arguments" in entry:
        return list(entry["arguments"])
    return shlex.split(entry["command"])


def definitions(args, name):
    """Every value the macro `name` is given on the command line, in order; None for an -U."""
    found = []
    position = 0
    while position < len(args):
        arg = args[position]
        for option in ("-D", "-U"):
            if arg == option and position + 1 < len(args):
                position += 1
                arg = option + args[position]
                break
        if arg.startswith("-D") and arg[2:].split("=", 1)[0] == name:
            found.append(arg[2:])
        elif arg.startswith("-U") and arg[2:] == name:
            found.append(None)
        position += 1
    return found


def problems(entries, definition=DEFINITION, part=LVGL_SOURCES, at_least=AT_LEAST):
    """Texts of everything that is wrong, empty if every source of LVGL is compiled with the definition."""
    name = definition.split("=", 1)[0]
    sources = [entry for entry in entries
               if part in entry.get("file", "").replace("\\", "/") and entry["file"].endswith(".c")]
    if len(sources) < at_least:
        return ["only %d sources with %s in their path, at least %d expected: not the commands of a whole build"
                % (len(sources), part, at_least)]

    # The compiler goes by the last definition of a name
    wrong = [entry["file"] for entry in sources if definitions(arguments(entry), name)[-1:] != [definition]]
    if wrong:
        return ["%d of %d sources of LVGL are not compiled with -D%s, the first: %s"
                % (len(wrong), len(sources), definition, wrong[0])]
    return []


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--commands", required=True, help="compile_commands.json of the build")
    parser.add_argument("--definition", default=DEFINITION, help="name=value as the compiler has to get it")
    options = parser.parse_args(argv)

    with open(options.commands, encoding="utf-8") as handle:
        entries = json.load(handle)
    found = problems(entries, options.definition)
    for text in found:
        print("PROBLEM: " + text)
    if not found:
        print("OK: every source of LVGL is compiled with -D" + options.definition)
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
