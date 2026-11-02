#!/usr/bin/env python3
"""Host-native gtest runner for the Project IGI Editor pure-logic test suites.

The editor is Windows-only, so the aggregate `igi_tests` binary cannot be built
on macOS.  A large part of the suite is nevertheless pure logic (parsers,
writers, geometry, utils) that compiles with the host compiler once the
Windows-only translation units are filtered out.

This runner:

  1. reads the test files registered in `CMakeLists.txt` (`tests/*.cpp`),
  2. reads the `target_sources(igi_tests PRIVATE ...)` source pool,
  3. trial-compiles every candidate with the host compiler (clang++),
     keeping only the translation units that do not need `windows.h`,
  4. archives the portable engine sources into a static library so the host
     linker pulls in exactly the objects each suite needs,
  5. links, runs and reports every suite that survives, aggregating the real
     gtest pass/fail counts.

Exit code 0 only when at least one suite ran and no suite failed.

Environment overrides:
  IGI_HOST_CXX          host compiler              (default: clang++ or c++)
  IGI_HOST_JOBS         parallel jobs              (default: CPU count)
  IGI_GTEST_SRC         googletest checkout root
  IGI_HOST_TESTS        explicit, space-separated suite list (overrides discovery)
  IGI_HOST_TESTS_EXTRA  extra suites added to the discovered list
  IGI_HOST_TESTS_SKIP   suites removed from the discovered list
  IGI_HOST_TEST_TIMEOUT per-suite timeout in seconds (default: 300)
  IGI_HOST_NO_CACHE     set to 1 to ignore the object cache
"""

from __future__ import annotations

import argparse
import concurrent.futures
import dataclasses
import hashlib
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
CMAKE_LISTS = REPO / "CMakeLists.txt"

TEST_FILE_RE = re.compile(r"(tests/[A-Za-z0-9_./+-]+\.cpp)")
SOURCE_FILE_RE = re.compile(r"(source/[A-Za-z0-9_./+-]+\.cpp)")
TARGET_SOURCES_RE = re.compile(
    r"target_sources\(\s*igi_tests\s+PRIVATE(.*?)\n\s*\)", re.DOTALL
)

INCLUDE_DIRS = [
    "source",
    "source/runtime",
    "source/cli",
    "third_party/glm-1.0.1",
    "third_party/GL/include",
    "tests",
]
# NOTE: the repository root must NOT be an include directory.  It contains a
# plain-text file named `version`, which then shadows the C++ standard header
# <version> and breaks every translation unit that includes it.

BASE_FLAGS = ["-std=c++20", "-O0", "-g0", "-fno-omit-frame-pointer"]


def log(msg: str = "") -> None:
    print(msg, flush=True)


def die(msg: str) -> "None":
    log(f"error: {msg}")
    raise SystemExit(2)


# ─────────────────────────────────────────────────────────────────────────────
# dispositions: every candidate suite gets exactly one, and the unexpected one
# is a lane failure
# ─────────────────────────────────────────────────────────────────────────────
#
# THE DEFECT THIS REPLACES (P1, scratch/tester/P1-host-lane-silent-drop.md)
#
# This runner used to classify suites into four buckets -- run, not_portable,
# unlinkable, runtime_missing -- and sum ONLY the `run` bucket into the exit
# code.  A suite that failed to COMPILE was therefore not a failure; it was
# simply absent from the totals, and the lane printed OK with the survivors'
# numbers and no indication that anything had been dropped.  On 2026-10-30 two
# runs of the same mandated command on the same commit produced
#
#     run 1:  67 suites, 1018 tests -> "OK: 1018 tests passed in 67 suites", exit 0
#     run 2:  85 suites, 1558 tests -> exit 1
#
# Run 1 dropped 18 suites and 540 tests relative to run 2 and called it OK.  The
# six suites it silently dropped included test_smoke_rules (43 tests) and
# test_smoke_particles (24), both of which compiled and PASSED minutes later.
#
# The trigger was two concurrent host-lane invocations sharing the default
# object cache (build-mingw/host-tests): the trial compiles saw a half-built
# cache and clang++ failed.  The collision only exposed the defect -- ANY
# compile failure was invisible, including a genuine one.
#
# The fix: one classification pass, an explicit reviewed allowlist for the
# suites that are architecturally not host-portable, and a hard failure for
# anything else that does not build.

DISPOSITION_RUN = "run"
DISPOSITION_EXCLUDED = "excluded_by_policy"
DISPOSITION_FAILED_TO_BUILD = "failed_to_build"

POLICY_FILE = REPO / "tools" / "host-tests" / "host_exclusions.txt"


@dataclasses.dataclass
class SuiteOutcome:
    """What became of one candidate suite.  Exactly one disposition."""

    path: Path
    disposition: str
    detail: str = ""
    result: "dict | None" = None

    @property
    def name(self) -> str:
        return self.path.stem

    @property
    def rel(self) -> str:
        try:
            return str(self.path.relative_to(REPO))
        except ValueError:
            return str(self.path)


def load_policy_exclusions(path: "Path | None" = None) -> dict[str, str]:
    """The reviewed allowlist of suites the host lane deliberately does not build.

    One entry per line: `<repo-relative path>  # <reason>`.  Blank lines and
    lines whose first non-space character is `#` are ignored.

    A MISSING file is a hard error, not an empty policy: an empty policy would
    silently re-enable the exact defect this exists to fix (every drop becomes
    "by policy" again).  Adding an entry is a visible, reviewable diff.
    """
    path = path or POLICY_FILE
    if not path.exists():
        die(f"the host-lane policy allowlist is missing: {path}\n"
            f"       It names the suites that are deliberately not host-portable. "
            f"Without it every drop would be silent again.")
    policy: dict[str, str] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        body, _, reason = raw.partition("#")
        entry = body.strip()
        if not entry:
            continue
        policy[entry] = reason.strip()
    return policy


def first_error_line(text: str) -> str:
    """The first line that names an error, IN FULL.

    The drop lines used to print `err[:120]`.  That cut a real clang++ message
    down to what read like a truncated path --

        clang++: error: no such file or directory: '.../build-mingw/host-

    -- and made a compile failure look like a display artefact rather than a
    failure.  Nothing here truncates: a path is long, and the diagnosis is worth
    the width.
    """
    for line in (text or "").splitlines():
        if "error:" in line or "fatal error" in line:
            return line.strip()
    lines = [ln.strip() for ln in (text or "").splitlines() if ln.strip()]
    return lines[0] if lines else "build failed (no diagnostic captured)"


def policy_problems(policy: dict[str, str],
                    outcomes: list[SuiteOutcome]) -> "tuple[list[str], list[str]]":
    """Check the allowlist itself.  Returns (hard problems, notes).

    A policy entry that names a file which is not in the repository is a hard
    problem: it is a manifest naming a phantom, the same failure this project
    has already committed four times in CMakeLists.txt.  An entry that is no
    longer needed is only a note -- a suite becoming portable is an improvement,
    not a regression -- but it must be visible so the list does not rot.
    """
    problems: list[str] = []
    notes: list[str] = []
    by_path = {o.rel: o for o in outcomes}
    for entry, reason in sorted(policy.items()):
        if not (REPO / entry).exists():
            problems.append(f"policy entry names a file that does not exist: {entry}")
            continue
        if not reason:
            problems.append(f"policy entry has no reason: {entry}")
            continue
        outcome = by_path.get(entry)
        if outcome is None:
            notes.append(f"policy entry is not a candidate suite any more: {entry}")
        elif outcome.disposition == DISPOSITION_RUN:
            notes.append(f"policy entry now builds and runs -- remove it: {entry}")
    return problems, notes


def lane_verdict(outcomes: list[SuiteOutcome], candidates: int) -> "tuple[int, str, list[str]]":
    """Return (exit_code, verdict line, problem lines).  Pure: no I/O, no globals.

    THE INVARIANT: `candidates == run + excluded_by_policy + failed_to_build`.
    Every candidate suite has exactly one disposition, and a suite that FAILS TO
    BUILD fails the lane unless the reviewed policy allowlist names it.

    `candidates` is passed in rather than derived so the accounting is checked
    against discovery, not against the outcome list itself (which would make the
    invariant true by construction).
    """
    ran = [o for o in outcomes if o.disposition == DISPOSITION_RUN]
    excluded = [o for o in outcomes if o.disposition == DISPOSITION_EXCLUDED]
    dropped = [o for o in outcomes if o.disposition == DISPOSITION_FAILED_TO_BUILD]

    total = sum((o.result or {}).get("total", 0) for o in ran)
    passed = sum((o.result or {}).get("passed", 0) for o in ran)
    failed = sum((o.result or {}).get("failed", 0) for o in ran)
    failed_suites = [o for o in ran if (o.result or {}).get("exit", 0) != 0]

    problems: list[str] = []

    accounted = len(ran) + len(excluded) + len(dropped)
    if accounted != candidates or len(outcomes) != candidates:
        problems.append(
            f"ACCOUNTING: {candidates} candidate suite(s) but "
            f"{len(outcomes)} disposition(s) "
            f"({len(ran)} run + {len(excluded)} excluded + {len(dropped)} dropped = "
            f"{accounted}). Every candidate must be classified exactly once, or the "
            f"lane's totals do not describe the suite.")

    for outcome in dropped:
        problems.append(
            f"DROPPED (failed to build, not on the policy allowlist): {outcome.rel}\n"
            f"         {first_error_line(outcome.detail)}")

    if not ran:
        problems.append("no suite could be built and run on the host")

    for outcome in failed_suites:
        problems.append(
            f"FAILED: {outcome.rel} "
            f"({(outcome.result or {}).get('failed', 0)} test(s), "
            f"exit {(outcome.result or {}).get('exit')})")

    counts = (f"{len(ran)} suite(s) run, {len(excluded)} excluded by policy, "
              f"{len(dropped)} dropped, {total} test(s)")

    if problems:
        return 1, (f"FAILED: {len(dropped)} suite(s) failed to BUILD, "
                   f"{len(failed_suites)} suite(s) failed, {failed} test(s) failed "
                   f"({counts})"), problems

    return 0, (f"OK: {total} tests passed in {len(ran)} suites "
               f"({len(excluded)} excluded by policy, 0 dropped)"), problems


class CacheLock:
    """Exclusive lock on the shared object cache directory.

    Two host-lane invocations sharing `build-mingw/host-tests` is what produced
    the run-1/run-2 divergence in the P1 report: the trial compiles saw a
    half-built cache and clang++ failed, and the lane called it OK.  A shared
    mutable cache between concurrent runs is not safe, so this fails loudly
    instead.  `flock` is released by the kernel when the process exits, so a
    killed run cannot leave a stale lock.
    """

    def __init__(self, out_dir: Path):
        self.path = out_dir / ".host-lane.lock"
        self.handle = None

    def acquire(self) -> "str | None":
        """Returns None on success, or a human-readable reason on failure."""
        try:
            import fcntl
        except ImportError:  # non-POSIX host; the lane is macOS-only in practice
            return None
        try:
            self.handle = open(self.path, "w")
            fcntl.flock(self.handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            if self.handle is not None:
                self.handle.close()
                self.handle = None
            return (f"another host-lane run holds the object cache at {self.path.parent}\n"
                    f"       A shared cache between concurrent runs is what made one run "
                    f"drop 18 suites and 540 tests and still print OK\n"
                    f"       (scratch/tester/P1-host-lane-silent-drop.md). Wait for the "
                    f"other run, or give this one its own cache:\n"
                    f"           IGI_HOST_OUT_DIR=<some-other-dir> bash scripts/run-host-tests.sh")
        return None

    def release(self) -> None:
        if self.handle is not None:
            try:
                self.handle.close()
            except OSError:
                pass
            self.handle = None


# ─────────────────────────────────────────────────────────────────────────────
# discovery
# ─────────────────────────────────────────────────────────────────────────────

def registered_test_files() -> list[Path]:
    """Every `tests/*.cpp` referenced from CMakeLists.txt, in file order."""
    text = CMAKE_LISTS.read_text()
    seen: list[Path] = []
    for m in TEST_FILE_RE.finditer(text):
        p = (REPO / m.group(1)).resolve()
        if p.exists() and p not in seen:
            seen.append(p)
    return seen


def engine_source_pool() -> list[Path]:
    """Sources compiled into the host-portable static library."""
    text = CMAKE_LISTS.read_text()
    m = TARGET_SOURCES_RE.search(text)
    if not m:
        die("could not find `target_sources(igi_tests PRIVATE ...)` in CMakeLists.txt")
    pool: list[Path] = []
    for sm in SOURCE_FILE_RE.finditer(m.group(1)):
        p = (REPO / sm.group(1)).resolve()
        if p.exists() and p not in pool:
            pool.append(p)
    if not pool:
        die("target_sources(igi_tests PRIVATE ...) contains no source/*.cpp entries")
    return pool


def split_env(name: str) -> list[str]:
    raw = os.environ.get(name, "").strip()
    return [tok for tok in re.split(r"[\s,]+", raw) if tok]


def resolve_named(name: str) -> Path | None:
    p = Path(name)
    if not p.is_absolute():
        p = REPO / name
    return p.resolve() if p.exists() else None


def select_suites() -> tuple[list[Path], list[Path]]:
    """Return (suites, excluded) after applying the explicit overrides."""
    override = split_env("IGI_HOST_TESTS")
    if override:
        suites: list[Path] = []
        missing: list[str] = []
        for name in override:
            p = resolve_named(name)
            if p is None:
                missing.append(name)
            elif p not in suites:
                suites.append(p)
        if missing:
            die(f"IGI_HOST_TESTS names files that do not exist: {', '.join(missing)}")
    else:
        suites = registered_test_files()

    excluded: list[Path] = []
    skip_names = set(split_env("IGI_HOST_TESTS_SKIP"))
    if skip_names:
        kept: list[Path] = []
        for p in suites:
            if p.name in skip_names or str(p.relative_to(REPO)) in skip_names:
                excluded.append(p)
            else:
                kept.append(p)
        suites = kept

    for name in split_env("IGI_HOST_TESTS_EXTRA"):
        p = resolve_named(name)
        if p is None:
            die(f"IGI_HOST_TESTS_EXTRA names a file that does not exist: {name}")
        if p not in suites:
            suites.append(p)
    return suites, excluded


def find_gtest() -> Path:
    candidates = []
    env = os.environ.get("IGI_GTEST_SRC")
    if env:
        candidates.append(Path(env))
    candidates.append(REPO / "build-mingw" / "_deps" / "googletest-src")
    candidates.append(REPO / "build-testing-master" / "_deps" / "googletest-src")
    for c in candidates:
        if (c / "googletest" / "src" / "gtest-all.cc").exists():
            return c.resolve()
    die(
        "googletest source not found. Run `bash scripts/build-windows.sh` once, or set "
        "IGI_GTEST_SRC to a googletest checkout."
    )


# ─────────────────────────────────────────────────────────────────────────────
# compilation
# ─────────────────────────────────────────────────────────────────────────────

class Builder:
    def __init__(self, cxx: str, out_dir: Path, gtest: Path, flags: list[str], jobs: int,
                 no_cache: bool):
        self.cxx = cxx
        self.out_dir = out_dir
        self.gtest = gtest
        self.flags = flags
        self.jobs = jobs
        self.no_cache = no_cache
        self.obj_dir = out_dir / "obj"
        self.obj_dir.mkdir(parents=True, exist_ok=True)
        self.sig = hashlib.sha1(
            " ".join([cxx] + flags).encode()
        ).hexdigest()[:12]
        self.header_mtime = self._newest_header_mtime()

    @staticmethod
    def _newest_header_mtime() -> float:
        """Newest mtime across every header the lane can include.

        The object cache is keyed on the source file and the compiler flags, so
        without this a header edit silently reuses stale objects.  That is the
        same class of trap as a stale PCH and it produces both false failures
        and false passes: a header that used to call a Windows API and now
        inlines the same helper changes whether a suite needs the symbol.
        """
        newest = 0.0
        for pattern in ("source/**/*.h", "source/**/*.hpp",
                        "tests/**/*.h", "tests/**/*.hpp"):
            for path in REPO.glob(pattern):
                try:
                    newest = max(newest, path.stat().st_mtime)
                except OSError:
                    pass
        try:
            newest = max(newest, (REPO / "CMakeLists.txt").stat().st_mtime)
        except OSError:
            pass
        return newest

    def _obj_for(self, src: Path, tag: str) -> Path:
        digest = hashlib.sha1(str(src).encode()).hexdigest()[:8]
        return self.obj_dir / tag / f"{src.stem}-{digest}.o"

    def compile(self, src: Path, tag: str) -> tuple[Path, str]:
        """Compile one file. Returns (object, error) — object is None on failure."""
        obj = self._obj_for(src, tag)
        obj.parent.mkdir(parents=True, exist_ok=True)
        stamp = obj.with_suffix(".sig")
        if not self.no_cache and obj.exists() and stamp.exists():
            obj_mtime = obj.stat().st_mtime
            if (stamp.read_text().strip() == self.sig
                    and obj_mtime >= src.stat().st_mtime
                    and obj_mtime >= self.header_mtime):
                return obj, ""
        cmd = [self.cxx, *self.flags, "-c", str(src), "-o", str(obj)]
        proc = subprocess.run(cmd, cwd=REPO, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")
        if proc.returncode != 0:
            if obj.exists():
                obj.unlink()
            first = next(
                (ln.strip() for ln in proc.stdout.splitlines() if " error:" in ln),
                proc.stdout.strip().splitlines()[0] if proc.stdout.strip() else "compile failed",
            )
            return None, first
        stamp.write_text(self.sig)
        return obj, ""

    def compile_many(self, items: list[tuple[Path, str]], label: str) -> dict[Path, tuple[Path, str]]:
        results: dict[Path, tuple[Path, str]] = {}
        total = len(items)
        done = 0
        with concurrent.futures.ThreadPoolExecutor(max_workers=self.jobs) as pool:
            futures = {pool.submit(self.compile, src, tag): src for src, tag in items}
            for fut in concurrent.futures.as_completed(futures):
                src = futures[fut]
                results[src] = fut.result()
                done += 1
                if done % 10 == 0 or done == total:
                    log(f"    {label}: {done}/{total}")
        return results

    def build_gtest(self) -> Path:
        lib = self.out_dir / "libgtest_host.a"
        srcs = [self.gtest / "googletest" / "src" / "gtest-all.cc",
                self.gtest / "googletest" / "src" / "gtest_main.cc"]
        objs = []
        gflags = ["-std=c++17", "-O0", "-g0",
                  f"-I{self.gtest / 'googletest' / 'include'}",
                  f"-I{self.gtest / 'googletest'}"]
        for s in srcs:
            o = self.obj_dir / "gtest" / (s.stem + ".o")
            o.parent.mkdir(parents=True, exist_ok=True)
            proc = subprocess.run([self.cxx, *gflags, "-c", str(s), "-o", str(o)],
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")
            if proc.returncode != 0:
                die(f"failed to build googletest object {s}:\n{proc.stdout}")
            objs.append(o)
        if lib.exists():
            lib.unlink()
        subprocess.run(["ar", "rcs", str(lib), *[str(o) for o in objs]], check=True)
        return lib

    def archive(self, objs: list[Path], name: str) -> Path:
        lib = self.out_dir / name
        if lib.exists():
            lib.unlink()
        subprocess.run(["ar", "rcs", str(lib), *[str(o) for o in objs]], check=True)
        return lib

    def link(self, obj: Path, out: Path, sources_lib: Path,
             gtest_lib: Path) -> tuple[bool, str, str, str]:
        """Returns (ok, mode, error, strict_error).

        mode is 'strict' or 'lazy'.  The strict-link error is kept even when the
        lazy fallback succeeds: it names the symbols the host build cannot
        resolve, which is what a lazily linked binary will abort on if it ever
        calls them.
        """
        base = [self.cxx, str(obj), str(sources_lib), str(gtest_lib),
                str(sources_lib), "-o", str(out)]
        proc = subprocess.run(base, cwd=REPO, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")
        if proc.returncode == 0:
            return True, "strict", "", ""
        strict_err = proc.stdout
        lazy = base[:-2] + ["-Wl,-undefined,dynamic_lookup", "-o", str(out)]
        proc2 = subprocess.run(lazy, cwd=REPO, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")
        if proc2.returncode == 0:
            return True, "lazy", "", strict_err
        tail = "\n".join(proc2.stdout.strip().splitlines()[-6:])
        return False, "failed", tail, strict_err


# ─────────────────────────────────────────────────────────────────────────────
# running
# ─────────────────────────────────────────────────────────────────────────────

GTEST_RAN = re.compile(r"\[==========\] (\d+) tests? from (\d+) test suites? ran")
GTEST_PASSED = re.compile(r"\[  PASSED  \] (\d+) tests?")
GTEST_FAILED = re.compile(r"\[  FAILED  \] (\d+) tests?, listed below")
GTEST_SKIPPED = re.compile(r"\[  SKIPPED \] (\d+) tests?")

# ld prints unresolved symbols as `  "symbol", referenced from:` blocks.
LD_UNDEFINED = re.compile(r'^  "([^"]+)"', re.M)


def first_undefined(strict_err: str) -> str:
    m = LD_UNDEFINED.search(strict_err or "")
    return m.group(1) if m else "?"

# Environment variables the engine/tests use to locate an installed game.  All
# of them take the install root (the directory holding MISSIONS/ and COMMON/).
CORPUS_ENV_VARS = ("IGI_GAME_PATH", "IGI_VANILLA_ROOT", "IGI_WEATHER_CORPUS")
DEFAULT_CORPUS = "/Users/haseeb-mir/Downloads/IGI"


def run_suite(binary: Path, timeout: int) -> dict:
    start = time.time()
    try:
        proc = subprocess.run([str(binary), "--gtest_color=no"], cwd=REPO,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True, encoding="utf-8", errors="replace", timeout=timeout)
        out, rc = proc.stdout, proc.returncode
    except subprocess.TimeoutExpired as exc:
        out = (exc.stdout or "") if isinstance(exc.stdout, str) else ""
        out += f"\n[runner] TIMEOUT after {timeout}s\n"
        rc = 124
    elapsed = time.time() - start
    ran = GTEST_RAN.search(out)
    passed = GTEST_PASSED.search(out)
    failed = GTEST_FAILED.search(out)
    skipped = GTEST_SKIPPED.search(out)
    # A lazily-linked binary that calls a Windows-only symbol aborts inside dyld
    # before any test result exists.  That is a portability gap, not a test
    # failure, so classify it separately.
    missing_symbol = "missing symbol called" in out
    return {
        "output": out,
        "exit": rc,
        "seconds": elapsed,
        "total": int(ran.group(1)) if ran else 0,
        "suites": int(ran.group(2)) if ran else 0,
        "passed": int(passed.group(1)) if passed else 0,
        "failed": int(failed.group(1)) if failed else 0,
        "skipped": int(skipped.group(1)) if skipped else 0,
        "summary": bool(ran),
        "missing_symbol": missing_symbol,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out-dir", default=str(REPO / "build-mingw" / "host-tests"))
    ap.add_argument("--jobs", type=int, default=int(os.environ.get("IGI_HOST_JOBS")
                                                    or (os.cpu_count() or 4)))
    ap.add_argument("--list", action="store_true", help="list candidate suites and exit")
    ap.add_argument("--show-output", action="store_true",
                    help="print the full gtest output of every suite")
    ap.add_argument("--keep-going", action="store_true",
                    help="run every suite even after a failure (default: on)")
    ap.add_argument("--corpus", default=os.environ.get("IGI_GAME_PATH") or DEFAULT_CORPUS,
                    help="installed IGI root used by corpus-backed suites "
                         f"(default: {DEFAULT_CORPUS})")
    ns = ap.parse_args()

    cxx = os.environ.get("IGI_HOST_CXX") or shutil.which("clang++") or shutil.which("c++")
    if not cxx:
        die("no host C++ compiler found (set IGI_HOST_CXX)")

    # Corpus-backed suites read the install root from the environment.  Point
    # every known variable at it unless the caller already set one explicitly.
    corpus = Path(ns.corpus).expanduser() if ns.corpus else None
    corpus_ok = bool(corpus and corpus.is_dir())
    if corpus_ok:
        for var in CORPUS_ENV_VARS:
            os.environ.setdefault(var, str(corpus))

    suites, excluded = select_suites()
    pool = engine_source_pool()

    log("=" * 72)
    log(" run-host-tests.sh — host-native pure-logic gtest lane")
    log(f"   repo        : {REPO}")
    log(f"   compiler    : {cxx}")
    log(f"   jobs        : {ns.jobs}")
    log(f"   candidate suites : {len(suites)}")
    log(f"   engine sources   : {len(pool)}")
    if corpus_ok:
        log(f"   game corpus : {corpus}")
        log(f"                 ({', '.join(f'{v}={os.environ[v]}' for v in CORPUS_ENV_VARS)})")
    else:
        log(f"   game corpus : NOT FOUND ({corpus}) — corpus-backed tests will self-skip")
    if excluded:
        log(f"   excluded by IGI_HOST_TESTS_SKIP: "
            f"{', '.join(str(p.relative_to(REPO)) for p in excluded)}")
    log("=" * 72)

    if ns.list:
        for p in suites:
            log(f"  {p.relative_to(REPO)}")
        return 0

    gtest = find_gtest()
    log(f"==> googletest: {gtest}")

    out_dir = Path(ns.out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    bin_dir = out_dir / "bin"
    bin_dir.mkdir(parents=True, exist_ok=True)

    # One host-lane run per object cache.  Concurrent runs sharing
    # build-mingw/host-tests is what produced the run-1/run-2 divergence in the
    # P1 report, so refuse to start rather than produce a result nobody can
    # trust.  See CacheLock.
    cache_lock = CacheLock(out_dir)
    lock_problem = cache_lock.acquire()
    if lock_problem is not None:
        log("")
        log("FAILED: the host lane did not start")
        for line in lock_problem.splitlines():
            log(f"   {line}")
        return 1
    try:
        return run_lane(ns, suites, pool, corpus, corpus_ok, cxx,
                        out_dir, bin_dir, gtest)
    finally:
        cache_lock.release()


def run_lane(ns, suites, pool, corpus, corpus_ok, cxx,
             out_dir, bin_dir, gtest) -> int:
    """Build, link and run the suites, then return the lane's exit code."""
    flags = list(BASE_FLAGS)
    for inc in INCLUDE_DIRS:
        flags.append(f"-I{(REPO / inc).resolve()}")
    flags.append(f"-I{(gtest / 'googletest' / 'include').resolve()}")

    builder = Builder(cxx, out_dir, gtest, flags, ns.jobs,
                      no_cache=os.environ.get("IGI_HOST_NO_CACHE") == "1")

    log("==> building googletest")
    gtest_lib = builder.build_gtest()

    log(f"==> trial-compiling {len(pool)} engine sources")
    src_results = builder.compile_many([(p, "engine") for p in pool], "engine")
    portable_src = [p for p in pool if src_results[p][0] is not None]
    skipped_src = [p for p in pool if src_results[p][0] is None]
    log(f"    portable engine sources: {len(portable_src)}/{len(pool)}")
    for p in skipped_src:
        log(f"        excluded {p.relative_to(REPO)}: {src_results[p][1][:110]}")

    if not portable_src:
        die("no engine source compiled on the host — nothing to link against")

    sources_lib = builder.archive([src_results[p][0] for p in portable_src],
                                  "libigi_host_sources.a")

    log(f"==> trial-compiling {len(suites)} candidate suites")
    test_results = builder.compile_many([(p, "tests") for p in suites], "tests")

    policy = load_policy_exclusions()

    # One classification pass.  Every candidate ends up in exactly one of
    # RUN / EXCLUDED_BY_POLICY / FAILED_TO_BUILD -- see lane_verdict().
    outcomes: list[SuiteOutcome] = []
    runnable: list[tuple[Path, Path]] = []
    for p in suites:
        obj, err = test_results[p]
        if obj is None:
            rel = str(p.relative_to(REPO))
            if rel in policy:
                outcomes.append(SuiteOutcome(p, DISPOSITION_EXCLUDED, err))
            else:
                outcomes.append(SuiteOutcome(p, DISPOSITION_FAILED_TO_BUILD, err))
        else:
            runnable.append((p, obj))
    log(f"    suites that compile on the host: {len(runnable)}/{len(suites)}")

    timeout = int(os.environ.get("IGI_HOST_TEST_TIMEOUT", "300"))

    log(f"==> linking and running {len(runnable)} suites")
    for src, obj in runnable:
        binary = bin_dir / (src.stem + ".bin")
        ok, mode, err, strict_err = builder.link(obj, binary, sources_lib, gtest_lib)
        if not ok:
            # A link failure is a build failure: the suite did not run.
            outcomes.append(SuiteOutcome(src, DISPOSITION_FAILED_TO_BUILD, err))
            continue
        res = run_suite(binary, timeout)
        res["name"] = src.stem
        res["path"] = src
        res["link"] = mode
        if res["missing_symbol"] and not res["summary"]:
            # The binary linked lazily and then died inside dyld the first time
            # it called a symbol the host build could not resolve.  Name that
            # symbol instead of guessing: it is the first one ld reported.
            sym = first_undefined(strict_err)
            log("")
            log(f"--- {src.stem}  [{mode} link]  NOT HOST-PORTABLE: the binary "
                f"aborted on unresolved symbol {sym}")
            rel = str(src.relative_to(REPO))
            detail = f"aborted on unresolved symbol {sym}"
            if rel in policy:
                outcomes.append(SuiteOutcome(src, DISPOSITION_EXCLUDED, detail))
            else:
                outcomes.append(SuiteOutcome(src, DISPOSITION_FAILED_TO_BUILD, detail))
            continue
        outcomes.append(SuiteOutcome(src, DISPOSITION_RUN, "", res))
        log("")
        log(f"--- {src.stem}  [{mode} link]  exit={res['exit']}  "
            f"{res['seconds']:.2f}s")
        if ns.show_output or res["exit"] != 0:
            log(res["output"].rstrip())
        else:
            for line in res["output"].splitlines():
                if line.startswith(("[==========]", "[----------]", "[  PASSED  ]",
                                    "[  FAILED  ]", "[  SKIPPED ]")):
                    log("    " + line)

    results = [o.result for o in outcomes if o.disposition == DISPOSITION_RUN and o.result]
    total = sum(r["total"] for r in results)
    passed = sum(r["passed"] for r in results)
    failed = sum(r["failed"] for r in results)
    skipped = sum(r["skipped"] for r in results)
    failed_suites = [r for r in results if r["exit"] != 0]
    corpus_suites = [r for r in results if r["skipped"] > 0]
    excluded_outcomes = [o for o in outcomes if o.disposition == DISPOSITION_EXCLUDED]
    dropped_outcomes = [o for o in outcomes if o.disposition == DISPOSITION_FAILED_TO_BUILD]
    policy_hard, policy_notes = policy_problems(policy, outcomes)

    log("")
    log("=" * 72)
    log(" HOST TEST SUMMARY")
    log("=" * 72)
    for r in sorted(results, key=lambda r: r["name"]):
        status = "PASS" if r["exit"] == 0 else "FAIL"
        skip_note = f"  {r['skipped']:>3} skipped" if r["skipped"] else ""
        log(f"   {status}  {r['name']:<34} {r['passed']:>4} passed"
            f"  {r['failed']:>3} failed{skip_note}  ({r['total']} tests, exit {r['exit']})")
    log("")
    log(f"   candidate suites      : {len(suites)}")
    log(f"   suites attempted      : {len(runnable)}")
    log(f"   suites run            : {len(results)}")
    log(f"   suites failed         : {len(failed_suites)}")
    log(f"   tests run             : {total}")
    log(f"   tests passed          : {passed}")
    log(f"   tests failed          : {failed}")
    log(f"   tests skipped         : {skipped} (in {len(corpus_suites)} suite(s))")
    log(f"   game corpus           : {corpus if corpus_ok else 'not available'}")
    log(f"   suites excluded by policy (did not build): {len(excluded_outcomes)}")
    for o in sorted(excluded_outcomes, key=lambda o: o.rel):
        log(f"       - {o.rel}: {first_error_line(o.detail)}")
    log(f"   suites DROPPED (did not build, NOT on the policy allowlist): "
        f"{len(dropped_outcomes)}")
    for o in sorted(dropped_outcomes, key=lambda o: o.rel):
        log(f"       - {o.rel}: {first_error_line(o.detail)}")
    log(f"   engine sources excluded (compile) : {len(skipped_src)}")
    for o in policy_notes:
        log(f"   note: {o}")
    log("=" * 72)

    exit_code, verdict, problems = lane_verdict(outcomes, len(suites))

    # The allowlist is part of the manifest: an entry naming a file that is not
    # in the repository, or with no reason, is a hard failure (this project has
    # already committed a manifest naming phantom files four times).
    if policy_hard:
        exit_code = 1
        problems = list(problems) + policy_hard

    for problem in problems:
        for line in problem.splitlines():
            log(f"   {line}")
    if problems:
        log("")
    log(verdict)
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
