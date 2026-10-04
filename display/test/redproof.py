#!/usr/bin/env python3
"""Shows that the host tests of the display logic really guard the rules they claim to guard.

A test that stays green when the rule is removed proves nothing. Every mutation removes or weakens
one rule in a source of display/components/core; the test named with it must then FAIL in at least
one check. The run is red when

  - an unchanged test does not pass,
  - a mutation no longer applies (the text is not found exactly once) or does not compile,
  - a mutation leaves the test green,
  - a mutated test aborts or hangs without a failed check (a crash names no rule).

The mutations live in mutations/<module>.py, one list MUTATIONS per file with entries
(name, test, file, text in the source, replacement); file is relative to display/, test is the
name of the test program, for example "test_fmt".
Rules for mutations: they must not change a type and must not rely on a compiler warning - gcc
(CI) and clang (Mac) differ there.

  python3 redproof.py                  all mutations
  python3 redproof.py --module fmt     the mutations of one file in mutations/
  python3 redproof.py --only NAME      one mutation
  python3 redproof.py --list
  python3 redproof.py --shard 0/4      every fourth mutation, starting with the first: the CI runs the
                                       four parts side by side
  python3 redproof.py --selftest       counter-check of this script: it runs itself with a change
                                       without effect and with a mutation that does not apply and
                                       expects exit status 1, and with a real mutation and expects 0
"""
import argparse
import concurrent.futures
import importlib.util
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
DISPLAY = HERE.parent
REPO = DISPLAY.parent
TIMEOUT = 300

# What a test may read, at the same relative paths as in the repository
COPIED = [
    "display/components/core",
    "display/test",
    "display/layouts",
    "tools/w906/fixtures",
    "vehicle_profiles/mercedes/sprinter_w906_om651.json",
]


def IGNORED(directory, names):
    """Not copied: what a build leaves behind. Somebody else may build or delete a test program at this
    very moment, and a file that vanishes during the copy would stop it."""
    return [name for name in names
            if name == "__pycache__" or name.endswith((".log", ".dSYM"))
            or (name.startswith("test_") and "." not in name)]


def load_mutations():
    """All mutations as (module, name, test, file, old, new)."""
    mutations = []
    for path in sorted((HERE / "mutations").glob("*.py")):
        spec = importlib.util.spec_from_file_location("mutations_" + path.stem, path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        for name, test, file, old, new in module.MUTATIONS:
            mutations.append((path.stem, name, test, file, old, new))
    return mutations


def tests():
    return sorted(path.stem for path in HERE.glob("test_*.c"))


def shell(command, cwd):
    """Returns (exit status or None after a timeout, stdout, stderr)."""
    try:
        run = subprocess.run(command, shell=True, cwd=cwd, capture_output=True,
                             encoding="utf-8", errors="replace", timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        return None, "", "no end within %d s" % TIMEOUT
    return run.returncode, run.stdout, run.stderr


def copy_sources(target):
    for entry in COPIED:
        source = REPO / entry
        if not source.exists():
            continue
        destination = target / entry
        if source.is_dir():
            shutil.copytree(source, destination, ignore=IGNORED)
        else:
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy(source, destination)


def run_test(tree, test):
    """Returns (state, detail) with state 'green', 'red', 'aborted' or 'broken'."""
    cwd = tree / "display" / "test"
    status, _, stderr = shell("make -s %s" % test, cwd)
    if status != 0:
        return "broken", "does not compile:\n" + stderr.strip()
    status, stdout, stderr = shell("./%s" % test, cwd)
    failed = [line[5:] for line in stdout.splitlines() if line.startswith("FAIL ")]
    if status == 0:
        if failed:
            return "broken", "the test printed FAIL but exited with 0"
        return "green", ""
    if failed:
        return "red", "%d checks failed, first: %s" % (len(failed), failed[0])
    last = (stderr.strip().splitlines() or stdout.strip().splitlines() or ["no output"])[-1]
    return "aborted", "%s without a failed check: %s" % ("timeout" if status is None else "exit %d" % status, last)


def mutate(tree, mutation):
    """Applies one mutation. Returns an error text or None."""
    _, _, _, file, old, new = mutation
    path = tree / "display" / file
    if not path.exists():
        return "does not apply: display/%s does not exist" % file
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        return "does not apply: the text occurs %d times in display/%s, expected once" % (count, file)
    path.write_text(text.replace(old, new), encoding="utf-8")
    return None


def evaluate(mutation=None, test=None):
    """State of the test after the mutation, in a fresh copy. Without a mutation: of the unchanged `test`."""
    with tempfile.TemporaryDirectory(prefix="redproof-") as directory:
        tree = pathlib.Path(directory)
        copy_sources(tree)
        if mutation is not None:
            error = mutate(tree, mutation)
            if error is not None:
                return "broken", error
            test = mutation[2]
        return run_test(tree, test)


def report(mutations, jobs):
    """Runs the mutations, returns the number of problems."""
    problems = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        for mutation, (state, detail) in zip(mutations, pool.map(evaluate, mutations)):
            name = "%s/%s" % (mutation[0], mutation[1])
            if state == "red":
                print("RED     %-48s %s" % (name, detail))
            elif state == "green":
                print("GREEN   %-48s the test does not notice this change" % name)
                problems += 1
            elif state == "aborted":
                print("ABORTED %-48s %s" % (name, detail))
                problems += 1
            else:
                print("BROKEN  %-48s %s" % (name, detail))
                problems += 1
    return problems


def selftest_mutations(mutations):
    """A change without effect and one that does not apply, for --selftest."""
    if not mutations:
        return {}
    module, _, test, file, _, _ = mutations[0]
    source = (DISPLAY / file).read_text(encoding="utf-8")
    first_line = source.splitlines(keepends=True)[0]
    return {
        "noop": (module, "selftest_without_effect", test, file, first_line, first_line + "// change without effect\n")
        if source.count(first_line) == 1 else None,
        "missing": (module, "selftest_text_not_found", test, file, "this text is not in the source", ""),
    }


def selftest(mutations):
    """Runs this script as the CI does and looks at its exit status."""
    if not mutations:
        print("selftest FAILED: there is no mutation to test with")
        return 1
    first = mutations[0]
    if selftest_mutations(mutations)["noop"] is None:
        print("selftest FAILED: the first line of display/%s is not unique" % first[3])
        return 1
    cases = [
        (["--selftest-mutation", "noop"], 1, "GREEN   %s/selftest_without_effect" % first[0],
         "a change without effect must make the run fail"),
        (["--selftest-mutation", "missing"], 1, "BROKEN  %s/selftest_text_not_found" % first[0],
         "a mutation that does not apply must make the run fail"),
        (["--only", first[1]], 0, "RED     %s/%s" % (first[0], first[1]),
         "a removed rule must be reported red and the run must pass"),
    ]
    ok = True
    for arguments, status, line, rule in cases:
        run = subprocess.run([sys.executable, str(pathlib.Path(__file__).resolve())] + arguments,
                             capture_output=True, encoding="utf-8", errors="replace")
        good = run.returncode == status and line in run.stdout
        print("%s exit %d, expected %d and the line '%s': %s"
              % ("PASS" if good else "FAIL", run.returncode, status, line.strip(), rule))
        if not good:
            print(run.stdout + run.stderr)
            ok = False
    print("selftest %s" % ("OK" if ok else "FAILED"))
    return 0 if ok else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--only", metavar="NAME", help="run a single mutation")
    parser.add_argument("--module", metavar="MODULE", help="run the mutations of mutations/MODULE.py")
    parser.add_argument("--list", action="store_true", help="list the mutations")
    parser.add_argument("--shard", metavar="I/N", help="run only part I of N parts of the selection")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 2, help="mutations run in parallel")
    parser.add_argument("--selftest", action="store_true", help="counter-check of this script")
    parser.add_argument("--selftest-mutation", choices=["noop", "missing"], help=argparse.SUPPRESS)
    arguments = parser.parse_args()

    mutations = load_mutations()
    names = [mutation[1] for mutation in mutations]

    if arguments.list:
        for mutation in mutations:
            print("%s/%s" % (mutation[0], mutation[1]))
        return 0
    if len(set(names)) != len(names):
        print("duplicate mutation names: %s" % sorted(set(n for n in names if names.count(n) > 1)))
        return 1
    known = set(tests())
    for mutation in mutations:
        if mutation[2] not in known:
            print("%s/%s names the test %s, which does not exist" % (mutation[0], mutation[1], mutation[2]))
            return 1
    # Every test has to be guarded by at least one mutation. A run for one module or one mutation does not
    # ask this of the others: several modules are written at the same time.
    unguarded = sorted(known - set(mutation[2] for mutation in mutations))
    if unguarded and not (arguments.module or arguments.only):
        print("tests without any mutation: %s" % ", ".join(unguarded))
        return 1

    if arguments.selftest:
        return selftest(mutations)

    selected = mutations
    if arguments.selftest_mutation:
        selected = [selftest_mutations(mutations)[arguments.selftest_mutation]]
    elif arguments.only:
        selected = [mutation for mutation in mutations if mutation[1] == arguments.only]
        if not selected:
            print("no mutation named %s" % arguments.only)
            return 1
    elif arguments.module:
        selected = [mutation for mutation in mutations if mutation[0] == arguments.module]
        if not selected:
            print("no mutations for module %s" % arguments.module)
            return 1

    if arguments.shard:
        try:
            part, parts = (int(number) for number in arguments.shard.split("/"))
        except ValueError:
            part, parts = -1, 0
        if not 0 <= part < parts:
            print("--shard takes I/N with 0 <= I < N, not %s" % arguments.shard)
            return 1
        selected = selected[part::parts]

    for test in sorted(set(mutation[2] for mutation in selected)):
        state, detail = evaluate(None, test)
        if state != "green":
            print("the unchanged %s is not green: %s" % (test, detail))
            return 1
    print("unchanged sources: green")

    problems = report(selected, max(1, arguments.jobs))
    print("%d mutations, %d red, %d problems" % (len(selected), len(selected) - problems, problems))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
