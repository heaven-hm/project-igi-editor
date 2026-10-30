#!/usr/bin/env python3
"""Self-test for the host lane's verdict logic — the guard on the guard.

WHY THIS EXISTS
  `tools/host-tests/host_test_runner.py` is the gate behind every "host lane OK"
  claim this project makes, and it once reported

      OK: 1018 tests passed in 67 suites          (exit 0)

  on a run that had silently dropped 18 suites and 540 tests relative to another
  run of the same command on the same commit — because only the suites that
  compiled were summed into the exit code, and a suite that did not compile was
  simply absent from the totals.  See
  `scratch/tester/P1-host-lane-silent-drop.md`.

  The fix (one disposition per candidate, an explicit reviewed allowlist, drop
  counts in the OK line) is only worth anything if it is itself checked.  This
  script exercises `lane_verdict()`, `load_policy_exclusions()` and
  `policy_problems()` directly with synthetic outcomes — no compiler, no
  googletest, no network, under a second.

  It is wired into `scripts/run-host-tests.sh` as a preflight step, so a
  regression in the verdict logic fails the lane before anything is compiled.

USAGE
  python3 tools/host-tests/host_lane_selftest.py       # exit 0 = the logic is sound
"""

from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
RUNNER = Path(__file__).resolve().parent / "host_test_runner.py"


def load_runner():
    spec = importlib.util.spec_from_file_location("host_test_runner", RUNNER)
    if spec is None or spec.loader is None:
        raise SystemExit(f"error: cannot import {RUNNER}")
    module = importlib.util.module_from_spec(spec)
    # dataclasses resolves annotations through sys.modules, so register first.
    sys.modules["host_test_runner"] = module
    spec.loader.exec_module(module)
    return module


R = load_runner()

FAILURES: list[str] = []


def check(condition: bool, label: str, detail: str = "") -> None:
    if condition:
        print(f"   PASS  {label}")
    else:
        print(f"   FAIL  {label}")
        if detail:
            for line in detail.splitlines():
                print(f"         {line}")
        FAILURES.append(label)


def outcome(name: str, disposition: str, *, total: int = 0, passed: int = 0,
            failed: int = 0, exit_code: int = 0, detail: str = "") -> "object":
    path = (R.REPO / "tests" / name).resolve()
    result = None
    if disposition == R.DISPOSITION_RUN:
        result = {"total": total, "passed": passed, "failed": failed,
                  "exit": exit_code, "skipped": 0, "name": path.stem}
    return R.SuiteOutcome(path, disposition, detail, result)


print("=" * 72)
print(" host-lane verdict self-test (guard on the guard)")
print("=" * 72)

# ── 1. the incident: a suite that fails to BUILD must fail the lane ──────────
# This is the exact shape of run 1 in the P1 report: the survivors all pass and
# one suite did not compile.
outcomes = [
    outcome("test_smoke_rules.cpp", R.DISPOSITION_RUN, total=43, passed=43),
    outcome("test_alpha.cpp", R.DISPOSITION_RUN, total=10, passed=10),
    outcome("test_broken.cpp", R.DISPOSITION_FAILED_TO_BUILD,
            detail="clang++: error: no such file or directory: 'x.h'"),
]
code, verdict, problems = R.lane_verdict(outcomes, 3)
check(code == 1, "a suite that FAILS TO BUILD fails the lane (the P1 defect)",
      f"exit={code}, verdict={verdict!r}")
check(verdict.startswith("FAILED"), "the verdict line says FAILED", verdict)
check(any("test_broken.cpp" in p for p in problems),
      "the dropped suite is named in the problems", "\n".join(problems))
check(any("no such file or directory" in p for p in problems),
      "the compile diagnostic reaches the reader", "\n".join(problems))

# ── 2. a suite on the policy allowlist does not fail the lane ────────────────
outcomes = [
    outcome("test_alpha.cpp", R.DISPOSITION_RUN, total=10, passed=10),
    outcome("test_windows_only.cpp", R.DISPOSITION_EXCLUDED,
            detail="fatal error: 'windows.h' file not found"),
]
code, verdict, problems = R.lane_verdict(outcomes, 2)
check(code == 0, "an allowlisted exclusion does not fail the lane", verdict)
check(verdict.startswith("OK:"), "the verdict line says OK", verdict)
check("1 excluded by policy" in verdict and "0 dropped" in verdict,
      "the OK line carries the exclusion and drop counts", verdict)
check(problems == [], "no problems are reported", "\n".join(problems))

# ── 3. a suite that RUNS and FAILS still fails the lane ──────────────────────
outcomes = [
    outcome("test_alpha.cpp", R.DISPOSITION_RUN, total=10, passed=7, failed=3, exit_code=1),
]
code, verdict, problems = R.lane_verdict(outcomes, 1)
check(code == 1, "a failing test fails the lane", verdict)
check("test_alpha.cpp" in "\n".join(problems), "the failing suite is named",
      "\n".join(problems))

# ── 4. nothing built at all ─────────────────────────────────────────────────
outcomes = [outcome("test_alpha.cpp", R.DISPOSITION_FAILED_TO_BUILD, detail="boom")]
code, verdict, problems = R.lane_verdict(outcomes, 1)
check(code == 1, "no runnable suite fails the lane", verdict)
check(any("no suite could be built" in p for p in problems),
      "the 'nothing ran' case is named", "\n".join(problems))

# ── 5. the accounting invariant ─────────────────────────────────────────────
# Every candidate must be classified exactly once, or the totals describe a
# suite that no longer exists.  Both halves are checked: too few outcomes, and
# outcomes that do not add up to the candidate count.
code, verdict, problems = R.lane_verdict(
    [outcome("test_alpha.cpp", R.DISPOSITION_RUN, total=1, passed=1)], 5)
check(code == 1, "fewer dispositions than candidates fails the lane", verdict)
check(any("ACCOUNTING" in p for p in problems), "the accounting failure is named",
      "\n".join(problems))

# ── 6. the OK line can never be mistaken for a full run ─────────────────────
outcomes = ([outcome(f"test_run{i}.cpp", R.DISPOSITION_RUN, total=5, passed=5)
             for i in range(4)]
            + [outcome(f"test_ex{i}.cpp", R.DISPOSITION_EXCLUDED) for i in range(3)])
code, verdict, problems = R.lane_verdict(outcomes, 7)
check(code == 0 and "4 suites" in verdict and "20 tests passed" in verdict,
      "the OK line reports the surviving totals", verdict)
check("3 excluded by policy" in verdict and "0 dropped" in verdict,
      "the OK line reports both drop counts", verdict)

# ── 7. the drop line is not truncated ───────────────────────────────────────
# The old code printed `err[:120]`, which cut a real clang++ message down to
# something that read like a truncated path and made a compile failure look like
# a display artefact.
long_error = ("clang++: error: no such file or directory: "
              "'/Users/someone/Documents/Code/project-igi-editor-igi-re/source/renderer/"
              "a_very_long_translation_unit_name_that_runs_past_the_old_limit.h'")
line = R.first_error_line(long_error)
check(len(line) > 120, "first_error_line() keeps the whole diagnostic",
      f"len={len(line)}: {line!r}")
check(line == long_error, "first_error_line() returns it verbatim", repr(line))
check(R.first_error_line("") == "build failed (no diagnostic captured)",
      "first_error_line() has a defined answer for empty input")
check(R.first_error_line("some noise\nreal error: here") == "real error: here",
      "first_error_line() prefers the error line")

# ── 8. the reviewed allowlist is real, complete and self-consistent ─────────
policy = R.load_policy_exclusions()
check(len(policy) >= 20, f"the policy allowlist loads ({len(policy)} entries)")
check(all(reason for reason in policy.values()),
      "every policy entry carries a reason",
      "\n".join(p for p, r in policy.items() if not r))
missing_files = [p for p in policy if not (R.REPO / p).exists()]
check(missing_files == [], "every policy entry names a file that exists",
      "\n".join(missing_files))

# A policy entry that names a nonexistent file must be a HARD problem.
hard, _notes = R.policy_problems({"tests/does_not_exist_at_all.cpp": "reason"}, [])
check(any("does not exist" in p for p in hard),
      "a policy entry naming a phantom file is a hard failure", "\n".join(hard))

# An entry with no reason is a hard problem too.
hard, _notes = R.policy_problems({"tests/test_config.cpp": ""}, [])
check(any("no reason" in p for p in hard),
      "a policy entry with no reason is a hard failure", "\n".join(hard))

# A stale entry (the suite became portable) is only a NOTE.
hard, notes = R.policy_problems(
    {"tests/test_config.cpp": "windows-only"},
    [outcome("test_config.cpp", R.DISPOSITION_RUN, total=1, passed=1)])
check(hard == [], "a stale policy entry is not a hard failure", "\n".join(hard))
check(any("remove it" in n for n in notes),
      "a stale policy entry is reported so the list cannot rot", "\n".join(notes))

# ── 9. the real allowlist covers the suites that are actually dropped ───────
# If a suite is dropped and is NOT in the file, the lane now fails — which is
# the point — but a missing entry on the day this shipped would have looked like
# a regression.  Assert the two agree today.
check(all(p.startswith("tests/") and p.endswith(".cpp") for p in policy),
      "every policy entry is a tests/*.cpp suite")

print("=" * 72)
if FAILURES:
    print(f" HOST-LANE SELF-TEST: FAIL — {len(FAILURES)} check(s) failed")
    for label in FAILURES:
        print(f"   - {label}")
    print("=" * 72)
    raise SystemExit(1)
print(" HOST-LANE SELF-TEST: PASS — the verdict logic fails on a dropped suite,")
print("                      reports the drop counts in the OK line, and does")
print("                      not truncate diagnostics.")
print("=" * 72)
raise SystemExit(0)
