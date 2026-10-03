#!/usr/bin/env python3
"""Shows that the host tests really guard the rules they claim to guard.

A test that stays green when the rule is removed proves nothing. Every mutation below removes or
weakens one rule in the source; the test named with it must then FAIL. The run is red when

  - the unchanged source does not pass its test,
  - a mutation no longer applies (the text is not found exactly once) or does not compile,
  - a mutation leaves the test green.

  python3 redproof.py              all mutations
  python3 redproof.py --only NAME  one mutation
  python3 redproof.py --list
  python3 redproof.py --selftest   counter-check of this script: a change without effect must be
                                   reported as "stayed green", and a real one as red
"""
import argparse
import pathlib
import shutil
import subprocess
import sys
import tempfile

REPO = pathlib.Path(__file__).resolve().parents[2]
WORKFLOW = ".github/workflows/w906-tools.yml"

# The same commands as in the workflow, run in tools/w906. The script checks that the workflow
# contains them literally, so the proof and the CI test cannot drift apart.
TESTS = {
    "dtc_state": {
        "build": "cc -Wall -Wextra -Werror -fsanitize=address,undefined -I../../main "
                 "../../main/dtc_state.c dtc_state_test.c -o dtc_state_test",
        "run": "./dtc_state_test",
    },
}

STATE = "main/dtc_state.c"

# (name, test, file, text in the source, replacement)
MUTATIONS = [
    ("busy_not_checked", "dtc_state", STATE,
     "\tif(dtc_state_busy(s)) return DTC_REJECT_BUSY;",
     "\tif(0 && dtc_state_busy(s)) return DTC_REJECT_BUSY;"),
    ("clear_without_finished_read", "dtc_state", STATE,
     "\t\tif(s->phase != DTC_STATE_DONE) return DTC_REJECT_READ_REQUIRED;",
     "\t\tif(0 && s->phase != DTC_STATE_DONE) return DTC_REJECT_READ_REQUIRED;"),
    ("clear_after_clear", "dtc_state", STATE,
     "\t\tif(s->clear) return DTC_REJECT_READ_REQUIRED;",
     "\t\tif(0 && s->clear) return DTC_REJECT_READ_REQUIRED;"),
    ("clear_age_not_checked", "dtc_state", STATE,
     "\t\tif((uint32_t)(now_ms - s->finished_ms) > DTC_CLEAR_MAX_AGE_MS) return DTC_REJECT_READ_REQUIRED;",
     "\t\tif(0 && (uint32_t)(now_ms - s->finished_ms) > DTC_CLEAR_MAX_AGE_MS) return DTC_REJECT_READ_REQUIRED;"),
    ("clear_age_boundary", "dtc_state", STATE,
     "(uint32_t)(now_ms - s->finished_ms) > DTC_CLEAR_MAX_AGE_MS",
     "(uint32_t)(now_ms - s->finished_ms) >= DTC_CLEAR_MAX_AGE_MS"),
    ("clear_seq_not_compared", "dtc_state", STATE,
     "\t\tif(seq != s->seq) return DTC_REJECT_STALE_SEQ;",
     "\t\tif(0 && seq != s->seq) return DTC_REJECT_STALE_SEQ;"),
    ("clear_of_empty_list", "dtc_state", STATE,
     "\t\tif(s->result_count == 0) return DTC_REJECT_NOTHING_TO_CLEAR;",
     "\t\tif(0 && s->result_count == 0) return DTC_REJECT_NOTHING_TO_CLEAR;"),
    ("mqtt_clear_bound_too", "dtc_state", STATE,
     "\tif(clear && check_seq)",
     "\tif(clear && (check_seq || true))"),
    ("http_request_never_expires", "dtc_state", STATE,
     "\tif(s->src == DTC_SRC_HTTP && (uint32_t)(now_ms - s->queued_ms) > DTC_HTTP_EXPIRY_MS)",
     "\tif(0 && s->src == DTC_SRC_HTTP && (uint32_t)(now_ms - s->queued_ms) > DTC_HTTP_EXPIRY_MS)"),
    ("expiry_boundary", "dtc_state", STATE,
     "(uint32_t)(now_ms - s->queued_ms) > DTC_HTTP_EXPIRY_MS",
     "(uint32_t)(now_ms - s->queued_ms) >= DTC_HTTP_EXPIRY_MS"),
    ("mqtt_request_expires_too", "dtc_state", STATE,
     "\tif(s->src == DTC_SRC_HTTP && (uint32_t)(now_ms - s->queued_ms)",
     "\tif((s->src == DTC_SRC_HTTP || true) && (uint32_t)(now_ms - s->queued_ms)"),
    ("pickup_without_request", "dtc_state", STATE,
     "\tif(s->phase != DTC_STATE_QUEUED) return false;",
     "\tif(0 && s->phase != DTC_STATE_QUEUED) return false;"),
    ("progress_in_any_phase", "dtc_state", STATE,
     "\tif(s->phase != DTC_STATE_RUNNING) return;\n\n\ts->step = step;",
     "\ts->step = step;"),
    ("error_in_any_phase", "dtc_state", STATE,
     "\tif(!dtc_state_busy(s)) return;",
     "\tif(0 && !dtc_state_busy(s)) return;"),
    ("done_in_any_phase", "dtc_state", STATE,
     "\tif(s->phase != DTC_STATE_RUNNING) return;\n\n\tfinish(s, DTC_STATE_DONE, NULL, now_ms);",
     "\tfinish(s, DTC_STATE_DONE, NULL, now_ms);"),
    ("seq_not_masked", "dtc_state", STATE,
     "\ts->next_seq = seed & DTC_SEQ_MAX;",
     "\ts->next_seq = seed;"),
    ("seq_zero_at_boot", "dtc_state", STATE,
     "\tif(s->next_seq == 0) s->next_seq = 1;",
     "\tif(0 && s->next_seq == 0) s->next_seq = 1;"),
    ("seq_no_wrap", "dtc_state", STATE,
     "\treturn seq >= DTC_SEQ_MAX ? 1 : seq + 1;",
     "\treturn seq + 1;"),
    ("request_keeps_old_step", "dtc_state", STATE,
     "\ts->step = 0;\n",
     ""),
    ("request_keeps_old_reason", "dtc_state", STATE,
     "\ts->step = 0;\n\ts->reason = NULL;\n",
     "\ts->step = 0;\n"),
    ("finish_keeps_name", "dtc_state", STATE,
     "\ts->reason = reason;\n\ts->name = NULL;\n",
     "\ts->reason = reason;\n"),
    ("result_seq_not_stored", "dtc_state", STATE,
     "\ts->result_seq = s->seq;\n",
     ""),
    ("result_count_not_stored", "dtc_state", STATE,
     "\ts->result_count = dtc_count;\n",
     "\t(void)dtc_count;\n"),
    ("json_truncated_silently", "dtc_state", STATE,
     "\t\tout->overflow = true;\n",
     ""),
    ("json_no_room_for_zero", "dtc_state", STATE,
     "\tif(out->len + 1 >= out->size)",
     "\tif(out->len >= out->size)"),
    ("json_not_escaped", "dtc_state", STATE,
     "\t\tif(c == '\"' || c == '\\\\') put_char(out, '\\\\');\n",
     ""),
    ("json_control_characters", "dtc_state", STATE,
     "\t\tif(c < 0x20) continue;\n",
     ""),
    ("json_age_while_not_finished", "dtc_state", STATE,
     "\tbool finished = s->phase == DTC_STATE_DONE || s->phase == DTC_STATE_ERROR;",
     "\tbool finished = true;"),
    ("json_action_without_request", "dtc_state", STATE,
     "\tbool requested = s->seq != 0;",
     "\tbool requested = true;"),
]

# Changes nothing the tests could notice. Used by --selftest only.
NOOP = ("selftest_without_effect", "dtc_state", STATE,
        '#include "dtc_state.h"\n',
        '#include "dtc_state.h"\n// change without effect\n')


def shell(command, cwd):
    return subprocess.run(command, shell=True, cwd=cwd, capture_output=True, text=True)


def copy_sources(target):
    """The files a host test needs, at the same relative paths."""
    (target / "main").mkdir(parents=True)
    for source in sorted((REPO / "main").glob("dtc_*.[ch]")):
        shutil.copy(source, target / "main" / source.name)
    shutil.copytree(REPO / "tools" / "w906", target / "tools" / "w906",
                    ignore=shutil.ignore_patterns("__pycache__", "*_test"))


def run_test(tree, test):
    """Returns (state, detail) with state 'green', 'red' or 'broken'."""
    cwd = tree / "tools" / "w906"
    build = shell(TESTS[test]["build"], cwd)
    if build.returncode != 0:
        return "broken", "does not compile:\n" + build.stderr.strip()
    run = shell(TESTS[test]["run"], cwd)
    failed = [line[5:] for line in run.stdout.splitlines() if line.startswith("FAIL ")]
    if run.returncode == 0:
        if failed:
            return "broken", "the test printed FAIL but exited with 0"
        return "green", ""
    if failed:
        return "red", "%d checks failed, first: %s" % (len(failed), failed[0])
    last = (run.stderr.strip().splitlines() or run.stdout.strip().splitlines() or ["no output"])[-1]
    return "red", "aborted without a failed check (exit %d): %s" % (run.returncode, last)


def mutate(tree, mutation):
    """Applies one mutation. Returns an error text or None."""
    _, _, file, old, new = mutation
    path = tree / file
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        return "does not apply: the text occurs %d times in %s, expected once" % (count, file)
    path.write_text(text.replace(old, new), encoding="utf-8")
    return None


def evaluate(mutation, test=None):
    """Returns (state, detail) of the test after the mutation, in a fresh copy of the sources.
    Without a mutation the unchanged sources are tested with `test`."""
    with tempfile.TemporaryDirectory(prefix="redproof-") as directory:
        tree = pathlib.Path(directory)
        copy_sources(tree)
        if mutation is not None:
            error = mutate(tree, mutation)
            if error is not None:
                return "broken", error
        return run_test(tree, mutation[1] if mutation is not None else test)


def check_workflow():
    """The commands here must be the ones the CI runs."""
    path = REPO / WORKFLOW
    if not path.exists():
        return ["%s not found" % WORKFLOW]
    text = path.read_text(encoding="utf-8")
    problems = []
    for name, test in TESTS.items():
        command = "%s && %s" % (test["build"], test["run"])
        if command not in text:
            problems.append("%s does not run the %s test with the command used here:\n  %s"
                            % (WORKFLOW, name, command))
    return problems


def selftest():
    ok = True

    state, detail = evaluate(NOOP)
    print("%-7s %s %s" % (state.upper(), NOOP[0], detail))
    if state != "green":
        print("selftest FAILED: a change without effect must leave the test green")
        ok = False
    elif report([NOOP]) == 0:
        print("selftest FAILED: a mutation that stays green must make this script fail")
        ok = False

    state, detail = evaluate(MUTATIONS[0])
    print("%-7s %s %s" % (state.upper(), MUTATIONS[0][0], detail))
    if state != "red":
        print("selftest FAILED: a removed rule must turn the test red")
        ok = False

    gone = ("selftest_text_not_found", "dtc_state", STATE, "this text is not in the source", "")
    state, detail = evaluate(gone)
    print("%-7s %s %s" % (state.upper(), gone[0], detail))
    if state != "broken":
        print("selftest FAILED: a mutation that does not apply must be reported")
        ok = False

    print("selftest %s" % ("OK" if ok else "FAILED"))
    return 0 if ok else 1


def report(mutations):
    """Runs the mutations, returns the number of problems."""
    problems = 0
    for mutation in mutations:
        state, detail = evaluate(mutation)
        if state == "red":
            print("RED     %-30s %s" % (mutation[0], detail))
        elif state == "green":
            print("GREEN   %-30s the test does not notice this change" % mutation[0])
            problems += 1
        else:
            print("BROKEN  %-30s %s" % (mutation[0], detail))
            problems += 1
    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--only", metavar="NAME", help="run a single mutation")
    parser.add_argument("--list", action="store_true", help="list the mutations")
    parser.add_argument("--selftest", action="store_true", help="counter-check of this script")
    arguments = parser.parse_args()

    if arguments.list:
        for mutation in MUTATIONS:
            print(mutation[0])
        return 0

    names = [mutation[0] for mutation in MUTATIONS]
    if len(set(names)) != len(names):
        print("duplicate mutation names")
        return 1

    if arguments.selftest:
        return selftest()

    for problem in check_workflow():
        print(problem)
        return 1

    for test in TESTS:
        state, detail = evaluate(None, test)
        if state != "green":
            print("the unchanged %s test is not green: %s" % (test, detail))
            return 1
    print("unchanged sources: green")

    mutations = MUTATIONS
    if arguments.only:
        mutations = [mutation for mutation in MUTATIONS if mutation[0] == arguments.only]
        if not mutations:
            print("no mutation named %s" % arguments.only)
            return 1

    problems = report(mutations)
    print("%d mutations, %d red, %d problems" % (len(mutations), len(mutations) - problems, problems))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
