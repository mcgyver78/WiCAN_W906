import contextlib
import io
import json
import os
import tempfile
import unittest

import check_build

LVGL = "/project/display/managed_components/lvgl__lvgl/src/"
WANTED = "LV_ASSERT_HANDLER=if((abort(),0)){}"
# The same definition the three ways a build system may write it into a command for the shell
QUOTED = [
    '-DLV_ASSERT_HANDLER="if((abort(),0)){}"',
    "-DLV_ASSERT_HANDLER='if((abort(),0)){}'",
    "-DLV_ASSERT_HANDLER=if\\(\\(abort\\(\\),0\\)\\)\\{\\}",
]


def command(source, *options):
    return {"directory": "/project/display/build", "file": source,
            "command": " ".join(("xtensa-esp32s3-elf-gcc", "-DESP_PLATFORM") + options + ("-c", source))}


def build(count=120, options=(QUOTED[0],)):
    """The commands of a build: `count` sources of LVGL, and two of other components without the definition."""
    entries = [command("%score/lv_file_%d.c" % (LVGL, number), *options) for number in range(count)]
    entries.append(command("/project/display/components/ui/ui.c"))
    entries.append(command("/project/display/main/main.c"))
    return entries


class CheckBuild(unittest.TestCase):
    def test_a_build_with_the_definition_is_fine(self):
        for quoted in QUOTED:
            self.assertEqual(check_build.problems(build(options=(quoted,))), [], quoted)

    def test_arguments_as_a_list_are_read_as_well(self):
        entries = build()
        # The list is the one that counts where an entry has both: the command here lacks the definition
        for entry in entries:
            if LVGL in entry["file"]:
                entry["command"] = "gcc -c " + entry["file"]
                entry["arguments"] = ["gcc", "-D" + WANTED, "-c", entry["file"]]
        self.assertEqual(check_build.problems(entries), [])
        entries[5]["arguments"] = ["gcc", "-D", WANTED, "-c", entries[5]["file"]]
        self.assertEqual(check_build.problems(entries), [])

    def test_a_build_without_the_definition_is_reported(self):
        found = check_build.problems(build(options=()))
        self.assertEqual(len(found), 1)
        self.assertIn("120 of 120", found[0])

    def test_one_source_without_it_is_reported_by_name(self):
        entries = build()
        entries[7] = command(LVGL + "draw/lv_draw_other.c")
        found = check_build.problems(entries)
        self.assertEqual(len(found), 1)
        self.assertIn("1 of 120", found[0])
        self.assertIn("lv_draw_other.c", found[0])

    def test_another_value_is_not_the_definition(self):
        # Cut at a semicolon, as CMake would cut a definition that had one; the loop LVGL has by itself
        for other in ('-DLV_ASSERT_HANDLER="abort()"', '-DLV_ASSERT_HANDLER="while(1)"', "-DLV_ASSERT_HANDLER",
                      '-DLV_ASSERT_HANDLER_INCLUDE="stdlib.h"', '-DXLV_ASSERT_HANDLER="if((abort(),0)){}"'):
            found = check_build.problems(build(options=(other,)))
            self.assertEqual(len(found), 1, other)
            self.assertIn("120 of 120", found[0])

    def test_a_name_that_only_begins_like_it_is_another_macro(self):
        later = '-DLV_ASSERT_HANDLER_INCLUDE="stdlib.h"'
        self.assertEqual(check_build.problems(build(options=(QUOTED[0], later))), [])

    def test_the_last_definition_counts(self):
        self.assertEqual(check_build.problems(build(options=('-DLV_ASSERT_HANDLER="while(1)"', QUOTED[0]))), [])
        for later in ('-DLV_ASSERT_HANDLER="while(1)"', "-ULV_ASSERT_HANDLER", "-U LV_ASSERT_HANDLER"):
            found = check_build.problems(build(options=(QUOTED[0], later)))
            self.assertEqual(len(found), 1, later)

    def test_too_few_sources_are_no_build(self):
        for count in (0, 99):
            found = check_build.problems(build(count=count))
            self.assertEqual(len(found), 1)
            self.assertIn("only %d sources" % count, found[0])
        self.assertEqual(check_build.problems(build(count=100)), [])

    def test_sources_of_other_components_do_not_count(self):
        # Neither as sources of LVGL that lack the definition nor towards their number
        entries = [command("/project/display/components/ui/ui_%d.c" % number, QUOTED[0]) for number in range(200)]
        found = check_build.problems(entries)
        self.assertEqual(len(found), 1)
        self.assertIn("only 0 sources", found[0])

    def test_command_line(self):
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, "compile_commands.json")
            for entries, definition, status, word in ((build(), None, 0, "OK"),
                                                      (build(options=()), None, 1, "PROBLEM"),
                                                      (build(), "LV_ASSERT_HANDLER=while(1);", 1, "PROBLEM")):
                with open(path, "w", encoding="utf-8") as handle:
                    json.dump(entries, handle)
                argv = ["--commands", path] + (["--definition", definition] if definition else [])
                output = io.StringIO()
                with contextlib.redirect_stdout(output):
                    self.assertEqual(check_build.main(argv), status)
                self.assertTrue(output.getvalue().startswith(word), output.getvalue())


if __name__ == "__main__":
    unittest.main()
