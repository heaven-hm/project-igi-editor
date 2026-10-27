#!/usr/bin/env bash
#
# build-windows.sh — reproducible Windows cross-build gate for the editor.
#
# Two independent gates, run from a macOS/Linux host with MinGW-w64:
#
#   1. x64 full link gate   — build-mingw-x64/ (x86_64-w64-mingw32)
#      configure + build of `igi-editor` and `igi_tests`.  The committed GL
#      import libraries (third_party/GL/lib/x64/{freeglut,glew32}.lib) are
#      MSVC x64 import libs and MinGW-w64 consumes them directly, so this is the
#      architecture a fresh checkout can always LINK for.  This gate must exit 0.
#      (x64 links but does not RUN: no x64 freeglut/glew runtime DLLs exist, see
#      docs/igi-re-parity/WINE_SMOKE.md.  The runnable target is i686.)
#
#   2. i686 compile-only gate — build-mingw/ (i686-w64-mingw32)
#      The shipping target is Win32, and tools/igi-re-parity/tu-check.py replays
#      the 32-bit compile commands from build-mingw/compile_commands.json, so
#      this tree stays i686.  The x86 GL import libraries
#      (third_party/GL/lib/x86/{libfreeglut,libglew32}.a) are NOT tracked -- the
#      original repository's .gitignore has a blanket `x86/` rule -- so this gate
#      does not assume a link is possible and does not attempt one: every object
#      target of `igi-editor` / `igi_tests` / `igi_mcp` is built explicitly, which
#      still fails the gate on any 32-bit compile error.  When those two archives
#      are present, scripts/run-wine-smoke.sh links this same tree into a runnable
#      igi1ed.exe; the gate stays compile-only on purpose, so it passes on a
#      machine that has only the tracked files.
#
#   3. build-integrity check — runs immediately after gate 2, before the summary.
#      scripts/check-build-integrity.sh proves the i686 tree is internally
#      consistent: no object predates the PCH it is compiled through, no object's
#      ninja dependency record is missing or empty (`#deps 0`), no object is older
#      than a header it includes, and the ninja CMake has cached is the one on
#      PATH.  A FAIL here means a gate result on this tree cannot be trusted (a
#      mixed-layout binary may have been validated); the check prints the exact
#      rebuild command.  It cannot be skipped.
#
# Usage:
#   bash scripts/build-windows.sh                 # both gates
#   bash scripts/build-windows.sh --arch x64      # x64 link gate only
#   bash scripts/build-windows.sh --arch i686     # i686 compile gate only
#   bash scripts/build-windows.sh --reconfigure   # force a fresh configure
#   bash scripts/build-windows.sh --clean         # wipe both build trees first
#   bash scripts/build-windows.sh --keep-going    # ninja -k 0 (collect all errors)
#   bash scripts/build-windows.sh --fresh-pch     # drop stale GCC PCHs first
#   bash scripts/build-windows.sh --online        # allow FetchContent downloads
#
# Logs:
#   build-mingw/build-windows.log          combined transcript (both gates)
#   build-mingw/build-windows-i686.log     raw i686 compile gate log
#   build-mingw-x64/build-windows.log      raw x64 link gate log
#
# Environment overrides:
#   IGI_JOBS             parallel compile jobs (default: CPU count)
#   IGI_LINK_TARGETS     x64 targets to build      (default: igi-editor igi_tests)
#   IGI_COMPILE_TARGETS  i686 object-gate targets  (default: igi-editor igi_tests igi_mcp)
#
# NOTE on `set -e`: this script does NOT enable errexit.  Every step's status is
# checked explicitly, because a gate must report both gates' results instead of
# aborting at the first failure.
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
X64_DIR="$REPO_ROOT/build-mingw-x64"
I686_DIR="$REPO_ROOT/build-mingw"
COMBINED_LOG="$I686_DIR/build-windows.log"
I686_LOG="$I686_DIR/build-windows-i686.log"
X64_LOG="$X64_DIR/build-windows.log"

JOBS="${IGI_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"
LINK_TARGETS="${IGI_LINK_TARGETS:-igi-editor igi_tests}"
COMPILE_TARGETS="${IGI_COMPILE_TARGETS:-igi-editor igi_tests igi_mcp}"

ARCH="both"
RECONFIGURE=0
CLEAN=0
KEEP_GOING=0
OFFLINE="auto"
FRESH_PCH=0

while [ $# -gt 0 ]; do
  case "$1" in
    --arch)        shift; ARCH="${1:?--arch needs x64|i686|both}" ;;
    --reconfigure) RECONFIGURE=1 ;;
    --clean)       CLEAN=1; RECONFIGURE=1 ;;
    --keep-going)  KEEP_GOING=1 ;;
    --fresh-pch)   FRESH_PCH=1 ;;
    --online)      OFFLINE=0 ;;
    --offline)     OFFLINE=1 ;;
    --jobs)        shift; JOBS="${1:?--jobs needs a value}" ;;
    -h|--help)     sed -n '2,47p' "${BASH_SOURCE[0]}"; exit 0 ;;
    *) echo "error: unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done

case "$ARCH" in
  both|x64|i686) ;;
  *) echo "error: --arch must be x64, i686 or both (got '$ARCH')" >&2; exit 2 ;;
esac

if [ "$CLEAN" = 1 ]; then
  echo "==> removing $X64_DIR and $I686_DIR"
  rm -rf "$X64_DIR" "$I686_DIR"
fi
mkdir -p "$I686_DIR" "$X64_DIR"

# Every message goes to the terminal and is appended to the combined log.  The
# build output itself is teed into the combined log by run_step() below, which
# keeps the log complete and greppable as soon as a step returns.
say() { printf '%s\n' "$*" | tee -a "$COMBINED_LOG"; }

: >"$COMBINED_LOG"

say "==================================================================="
say " build-windows.sh   arch=$ARCH"
say "   repo        : $REPO_ROOT"
say "   x64 tree    : $X64_DIR   (full link gate)"
say "   i686 tree   : $I686_DIR  (compile-only gate; tu-check.py source)"
say "   jobs        : $JOBS"
say "   combined log: $COMBINED_LOG"
say "   started     : $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
say "==================================================================="

require_tool() {
  if ! command -v "$1" >/dev/null 2>&1; then
    say "error: required tool not found on PATH: $1"
    exit 127
  fi
}

require_tool cmake
require_tool ninja

# ── helpers ──────────────────────────────────────────────────────────────────
# FetchContent downloads googletest into <tree>/_deps/googletest-src.  Once one
# tree has it, copy it into the other so both configure offline.
seed_gtest() {
  local dst="$1" src="$2"
  if [ ! -f "$dst/_deps/googletest-src/CMakeLists.txt" ] \
     && [ -f "$src/_deps/googletest-src/CMakeLists.txt" ]; then
    say "==> seeding googletest into $(basename "$dst")/_deps (offline)"
    mkdir -p "$dst/_deps"
    cp -R "$src/_deps/googletest-src" "$dst/_deps/"
  fi
}

gtest_disconnected_flag() {
  local tree="$1"
  case "$OFFLINE" in
    1) echo ON ;;
    0) echo OFF ;;
    auto)
      if [ -f "$tree/_deps/googletest-src/CMakeLists.txt" ]; then echo ON; else echo OFF; fi
      ;;
  esac
}

configure_tree() {
  local tree="$1" cc="$2" cxx="$3" rc="$4"
  if [ "$RECONFIGURE" = 0 ] && [ -f "$tree/CMakeCache.txt" ]; then
    echo "==> reuse existing configure in $(basename "$tree") (--reconfigure to redo)"
    return 0
  fi
  local disconnected
  disconnected="$(gtest_disconnected_flag "$tree")"
  echo "==> configure $(basename "$tree")  (cc=$cc, FETCHCONTENT_FULLY_DISCONNECTED=$disconnected)"
  cmake -S "$REPO_ROOT" -B "$tree" -G Ninja \
    -DCMAKE_SYSTEM_NAME=Windows \
    -DCMAKE_C_COMPILER="$cc" \
    -DCMAKE_CXX_COMPILER="$cxx" \
    -DCMAKE_RC_COMPILER="$rc" \
    -DCMAKE_BUILD_TYPE=Release \
    -DFETCHCONTENT_FULLY_DISCONNECTED="$disconnected" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
}

# A GCC PCH that was generated while headers were being edited concurrently can
# be accepted by the compiler yet carry stale `#pragma once` bookkeeping, which
# surfaces as bogus "redefinition of <X>" errors in headers that plainly have
# `#pragma once`.  --fresh-pch forces a clean PCH rebuild in both trees, and
# both gates additionally self-heal: if a build fails with that symptom the PCHs
# are dropped and the build is retried once.
clear_pch() {
  local tree="$1"
  [ -d "$tree" ] && find "$tree" -name '*.gch' -delete
}

stale_pch_symptom() {
  local log="$1"
  [ -f "$log" ] && grep -q "redefinition of" "$log"
}

NINJA_EXTRA=()
if [ "$KEEP_GOING" = 1 ]; then
  NINJA_EXTRA=(-k 0)
fi

if [ "$FRESH_PCH" = 1 ]; then
  say "==> --fresh-pch: removing precompiled headers"
  clear_pch "$X64_DIR"
  clear_pch "$I686_DIR"
fi

X64_RC=0
I686_RC=0

# ── gate 1: x86_64 full link ─────────────────────────────────────────────────
if [ "$ARCH" = "both" ] || [ "$ARCH" = "x64" ]; then
  say ""
  say "###################################################################"
  say "# GATE 1/2 — x86_64 full link gate (build-mingw-x64)"
  say "###################################################################"
  require_tool x86_64-w64-mingw32-gcc
  require_tool x86_64-w64-mingw32-g++
  require_tool x86_64-w64-mingw32-windres
  seed_gtest "$X64_DIR" "$I686_DIR"
  seed_gtest "$I686_DIR" "$X64_DIR"

  : >"$X64_LOG"
  (
    configure_tree "$X64_DIR" x86_64-w64-mingw32-gcc x86_64-w64-mingw32-g++ \
                   x86_64-w64-mingw32-windres
    CFG_RC=$?
    if [ "$CFG_RC" -ne 0 ]; then
      echo "==> x64 configure FAILED (exit $CFG_RC)"
      exit "$CFG_RC"
    fi
    echo "==> x64 build: $LINK_TARGETS"
    # shellcheck disable=SC2086
    cmake --build "$X64_DIR" --target $LINK_TARGETS -j "$JOBS" -- "${NINJA_EXTRA[@]}"
    exit $?
  ) 2>&1 | tee -a "$X64_LOG" "$COMBINED_LOG"
  X64_RC=${PIPESTATUS[0]}

  if [ "$X64_RC" -ne 0 ] && stale_pch_symptom "$X64_LOG"; then
    say "==> GATE 1: stale-PCH symptom (bogus 'redefinition of') — clearing PCHs and retrying once"
    clear_pch "$X64_DIR"
    (
      # shellcheck disable=SC2086
      cmake --build "$X64_DIR" --target $LINK_TARGETS -j "$JOBS" -- "${NINJA_EXTRA[@]}"
      exit $?
    ) 2>&1 | tee -a "$X64_LOG" "$COMBINED_LOG"
    X64_RC=${PIPESTATUS[0]}
  fi

  if [ "$X64_RC" -ne 0 ]; then
    say "==> GATE 1 (x64 link) FAILED (exit $X64_RC)"
    grep -E "error:" "$X64_LOG" | head -20 || true
  else
    say "==> GATE 1 (x64 link) OK (exit 0)"
    for exe in "$REPO_ROOT/bin/Release/igi1ed.exe" "$REPO_ROOT/bin/igi_tests.exe" \
               "$X64_DIR/bin/Release/igi1ed.exe" "$X64_DIR/bin/igi_tests.exe"; do
      [ -f "$exe" ] && say "$(printf '    %s (%s bytes)' "$exe" "$(wc -c <"$exe" | tr -d ' ')")"
    done
    true
  fi
fi

# ── gate 2: i686 compile-only ────────────────────────────────────────────────
if [ "$ARCH" = "both" ] || [ "$ARCH" = "i686" ]; then
  say ""
  say "###################################################################"
  say "# GATE 2/2 — i686 compile-only gate (build-mingw)"
  say "#   no link: third_party/GL/lib/x86 does not exist in this repo"
  say "###################################################################"
  require_tool i686-w64-mingw32-gcc
  require_tool i686-w64-mingw32-g++
  require_tool i686-w64-mingw32-windres
  seed_gtest "$I686_DIR" "$X64_DIR"

  : >"$I686_LOG"
  (
    configure_tree "$I686_DIR" i686-w64-mingw32-gcc i686-w64-mingw32-g++ \
                   i686-w64-mingw32-windres
    CFG_RC=$?
    if [ "$CFG_RC" -ne 0 ]; then
      echo "==> i686 configure FAILED (exit $CFG_RC)"
      exit "$CFG_RC"
    fi

    # Enumerate every object target of the requested targets and build those.
    PATTERN="^CMakeFiles/(${COMPILE_TARGETS// /|})\\.dir/.*\\.(obj|res)$"
    mapfile -t OBJ_TARGETS < <(
      ninja -C "$I686_DIR" -t targets all 2>/dev/null \
        | sed 's/:.*//' \
        | grep -E "$PATTERN" \
        | sort -u
    )
    if [ "${#OBJ_TARGETS[@]}" -eq 0 ]; then
      echo "error: no object targets found for '$COMPILE_TARGETS' in $I686_DIR"
      exit 3
    fi
    echo "==> i686 compile-only build of ${#OBJ_TARGETS[@]} object targets"
    cmake --build "$I686_DIR" --target "${OBJ_TARGETS[@]}" -j "$JOBS" -- "${NINJA_EXTRA[@]}"
    exit $?
  ) 2>&1 | tee -a "$I686_LOG" "$COMBINED_LOG"
  I686_RC=${PIPESTATUS[0]}

  if [ "$I686_RC" -ne 0 ] && stale_pch_symptom "$I686_LOG"; then
    say "==> GATE 2: stale-PCH symptom (bogus 'redefinition of') — clearing PCHs and retrying once"
    clear_pch "$I686_DIR"
    (
      PATTERN="^CMakeFiles/(${COMPILE_TARGETS// /|})\\.dir/.*\\.(obj|res)$"
      mapfile -t OBJ_TARGETS < <(
        ninja -C "$I686_DIR" -t targets all 2>/dev/null \
          | sed 's/:.*//' \
          | grep -E "$PATTERN" \
          | sort -u
      )
      cmake --build "$I686_DIR" --target "${OBJ_TARGETS[@]}" -j "$JOBS" -- "${NINJA_EXTRA[@]}"
      exit $?
    ) 2>&1 | tee -a "$I686_LOG" "$COMBINED_LOG"
    I686_RC=${PIPESTATUS[0]}
  fi

  if [ "$I686_RC" -ne 0 ]; then
    say "==> GATE 2 (i686 compile) FAILED (exit $I686_RC)"
    grep -E "error:" "$I686_LOG" | head -20 || true
  else
    say "==> GATE 2 (i686 compile) OK (exit 0)"
  fi
fi

# ── build-integrity check: is the i686 tree internally consistent? ───────────
# A binary once shipped with a null write in App::Input_OnMouse that was NOT a
# source bug: `app_input_mouse.cpp.obj` had been built hours earlier and was never
# recompiled (ninja had `#deps 0` for it, so no header could invalidate it) while
# `app.cpp.obj` and 33 others were rebuilt against a different `App` layout.  The
# mixed-layout binary's stale object read a member at the old offset, landed in
# the zero-initialised SSO buffer of an empty std::string, and wrote through NULL.
# Any gate run on such a tree may have validated a corrupted binary.
#
# This check runs immediately AFTER the i686 build and BEFORE the summary, i.e.
# on exactly the tree the Windows test suite is about to consume.  It must run
# after the build, not before: an object that is merely older than a header the
# gate is about to rebuild is not a defect, so a pre-build check would cry wolf.
# See scripts/check-build-integrity.sh and docs/igi-re-parity/BUILD_GATE.md §8.
INTEGRITY_RC=0
if [ "$ARCH" != "x64" ] && [ "$I686_RC" -eq 0 ]; then
  say ""
  say "###################################################################"
  say "# BUILD-INTEGRITY CHECK — i686 objects vs their dependencies + PCH"
  say "###################################################################"
  bash "$REPO_ROOT/scripts/check-build-integrity.sh" 2>&1 | tee -a "$COMBINED_LOG"
  INTEGRITY_RC=${PIPESTATUS[0]}
  if [ "$INTEGRITY_RC" -ne 0 ]; then
    say "==> BUILD-INTEGRITY FAILED (exit $INTEGRITY_RC)"
    say "    The i686 tree is NOT internally consistent. Do NOT trust this run:"
    say "    a mixed-layout binary may have been validated. Rebuild from scratch"
    say "    (the recovery command is printed above) and re-run this gate."
  else
    say "==> BUILD-INTEGRITY OK (exit 0)"
  fi
fi

say ""
say "==================================================================="
say " build-windows.sh summary"
if [ "$ARCH" != "i686" ]; then say "   x64  link gate      : exit $X64_RC"; fi
if [ "$ARCH" != "x64" ];  then say "   i686 compile gate   : exit $I686_RC"; fi
if [ "$ARCH" != "x64" ];  then say "   build integrity     : exit $INTEGRITY_RC"; fi
say "   combined log        : $COMBINED_LOG"
say "   finished            : $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
say "==================================================================="

if [ "$X64_RC" -ne 0 ] || [ "$I686_RC" -ne 0 ] || [ "$INTEGRITY_RC" -ne 0 ]; then
  say "==> BUILD GATE FAILED"
  exit 1
fi
say "==> BUILD GATE OK (exit 0)"
exit 0
