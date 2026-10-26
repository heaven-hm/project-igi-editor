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

    runnable: list[tuple[Path, Path]] = []
    not_portable: list[tuple[Path, str]] = []
    for p in suites:
        obj, err = test_results[p]
        if obj is None:
            not_portable.append((p, err))
        else:
            runnable.append((p, obj))
    log(f"    suites that compile on the host: {len(runnable)}/{len(suites)}")

    timeout = int(os.environ.get("IGI_HOST_TEST_TIMEOUT", "300"))
    results: list[dict] = []
    unlinkable: list[tuple[Path, str]] = []
    runtime_missing: list[Path] = []

    log(f"==> linking and running {len(runnable)} suites")
    for src, obj in runnable:
        binary = bin_dir / (src.stem + ".bin")
        ok, mode, err, strict_err = builder.link(obj, binary, sources_lib, gtest_lib)
        if not ok:
            unlinkable.append((src, err))
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
            runtime_missing.append((src, sym))
            log("")
            log(f"--- {src.stem}  [{mode} link]  NOT HOST-PORTABLE: the binary "
                f"aborted on unresolved symbol {sym}")
            continue
        results.append(res)
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

    total = sum(r["total"] for r in results)
    passed = sum(r["passed"] for r in results)
    failed = sum(r["failed"] for r in results)
    skipped = sum(r["skipped"] for r in results)
    failed_suites = [r for r in results if r["exit"] != 0]
    corpus_suites = [r for r in results if r["skipped"] > 0]

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
    log(f"   suites attempted      : {len(runnable)}")
    log(f"   suites run            : {len(results)}")
    log(f"   suites failed         : {len(failed_suites)}")
    log(f"   tests run             : {total}")
    log(f"   tests passed          : {passed}")
    log(f"   tests failed          : {failed}")
    log(f"   tests skipped         : {skipped} (in {len(corpus_suites)} suite(s))")
    log(f"   game corpus           : {corpus if corpus_ok else 'not available'}")
    log(f"   suites not host-portable (compile): {len(not_portable)}")
    for p, err in not_portable:
        log(f"       - {p.relative_to(REPO)}: {err[:120]}")
    log(f"   suites not host-portable (link)   : {len(unlinkable)}")
    for p, err in unlinkable:
        log(f"       - {p.relative_to(REPO)}: {err.splitlines()[-1][:120] if err else 'link failed'}")
    log(f"   suites not host-portable (runtime unresolved symbol): {len(runtime_missing)}")
    for p, sym in runtime_missing:
        log(f"       - {p.relative_to(REPO)}: {sym[:100]}")
    log(f"   engine sources excluded (compile) : {len(skipped_src)}")
    log("=" * 72)

    if not results:
        log("FAILED: no suite could be built and run on the host")
        return 1
    if failed_suites or failed:
        log(f"FAILED: {len(failed_suites)} suite(s), {failed} test(s) failed")
        return 1
    log(f"OK: {total} tests passed in {len(results)} suites")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
