#!/usr/bin/env bash
#
# run-host-tests.sh — build and RUN the pure-logic gtest suites natively on the
#                     host (macOS clang++), using the googletest checkout that
#                     lives under build-mingw/_deps/googletest-src.
#
# The editor itself is Windows-only; this lane proves the parser/writer/geometry
# logic still behaves on a host build.  Suites that need windows.h are detected
# automatically and reported as not host-portable.
#
# Usage:
#   bash scripts/run-host-tests.sh                 # discover, build, run
#   bash scripts/run-host-tests.sh --list          # show discovered suites
#   bash scripts/run-host-tests.sh --show-output   # full gtest output per suite
#   bash scripts/run-host-tests.sh --out-dir DIR   # alternate scratch directory
#   bash scripts/run-host-tests.sh --corpus DIR    # installed IGI root
#
# The complete transcript is teed to build-mingw/host-tests.log and the script
# exits non-zero if any suite fails to build or fails a test.
#
# EVERY SUITE GETS A DISPOSITION
#   run / excluded_by_policy / failed_to_build.  Only a suite named in
#   tools/host-tests/host_exclusions.txt (the reviewed allowlist of suites that
#   are architecturally not host-portable) may be dropped without failing the
#   lane.  Anything else that does not build FAILS, and the OK line carries the
#   drop counts:
#
#       OK: 1614 tests passed in 90 suites (29 excluded by policy, 0 dropped)
#
#   This replaced a runner that logged a failed trial compile and then discarded
#   it, so one run silently dropped 18 suites and 540 tests and still printed OK
#   with exit 0.  See scratch/tester/P1-host-lane-silent-drop.md.  The verdict
#   logic is itself checked by tools/host-tests/host_lane_selftest.py, which runs
#   as a preflight below.
#
# ONE RUN PER OBJECT CACHE
#   Two concurrent invocations sharing build-mingw/host-tests is what exposed
#   the silent drop, so the runner now takes an exclusive lock on the cache and
#   refuses to start if another run holds it.  Give a concurrent run its own
#   cache with IGI_HOST_OUT_DIR=<dir>.
#
# Corpus-backed suites use an installed retail game tree (QSC/QVM/RES/DAT/MTP/TEX
# parsers, level weather, resource append).  Set IGI_GAME_PATH to point at one;
# the default is /Users/haseeb-mir/Downloads/IGI.  Suites whose corpus input is
# missing self-skip via GTEST_SKIP and are reported in the skipped column.
#
# See tools/host-tests/host_test_runner.py for the discovery/filter rules and
# the IGI_HOST_* environment overrides (explicit suite override list, extra
# suites, skip list, compiler, jobs, timeout).
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${IGI_HOST_OUT_DIR:-$REPO_ROOT/build-mingw/host-tests}"
LOG_DIR="${IGI_HOST_LOG_DIR:-$REPO_ROOT/build-mingw}"
LOG="$LOG_DIR/host-tests.log"

mkdir -p "$LOG_DIR" "$OUT_DIR"

# NOTE: this deliberately does not use `exec > >(tee "$LOG")`.  With process
# substitution the shell does not wait for `tee`, so whatever is still buffered
# in the pipe when the script exits is lost — the log ends up truncated to the
# first few lines.  A real pipeline is synchronous, so the log is complete as
# soon as the step returns.
{
  echo "==================================================================="
  echo " run-host-tests.sh"
  echo "   repo     : $REPO_ROOT"
  echo "   out dir  : $OUT_DIR"
  echo "   log      : $LOG"
  echo "   started  : $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
  echo "==================================================================="

  if ! command -v clang++ >/dev/null 2>&1 && ! command -v c++ >/dev/null 2>&1; then
    echo "error: no host C++ compiler on PATH (set IGI_HOST_CXX)" >&2
    exit 127
  fi

  # Guard on the guard.  The verdict logic that decides THIS lane's exit code is
  # itself checked before anything is compiled, because that logic has already
  # been wrong once in a way that mattered: it summed only the suites that
  # happened to compile, so one run dropped 18 suites and 540 tests and still
  # printed "OK: 1018 tests passed in 67 suites" with exit 0.  See
  # tools/host-tests/host_lane_selftest.py and
  # scratch/tester/P1-host-lane-silent-drop.md.
  echo "==> host-lane verdict self-test"
  if ! python3 "$REPO_ROOT/tools/host-tests/host_lane_selftest.py"; then
    echo "error: the host lane's own verdict logic failed its self-test — refusing to run" >&2
    exit 1
  fi

  # Corpus-backed suites read the installed-game root from the environment.
  CORPUS="${IGI_GAME_PATH:-/Users/haseeb-mir/Downloads/IGI}"
  if [ -d "$CORPUS" ]; then
    export IGI_GAME_PATH="$CORPUS"
    echo "==> game corpus: $IGI_GAME_PATH"
  else
    echo "==> game corpus: $CORPUS not found — corpus-backed tests will self-skip"
  fi

  python3 "$REPO_ROOT/tools/host-tests/host_test_runner.py" --out-dir "$OUT_DIR" "$@"
  exit $?
} 2>&1 | tee "$LOG"
RC=${PIPESTATUS[0]}

echo ""
echo "==> run-host-tests.sh exit=$RC  finished $(date -u '+%Y-%m-%dT%H:%M:%SZ')" | tee -a "$LOG"
exit "$RC"
