#!/usr/bin/env bash
# ============================================================================
# run-wine-smoke.sh — link the Win32 (i686) editor and smoke-test it under Wine
# ============================================================================
#
# WHY THIS EXISTS
#   Every other gate in this repository is compile-only or host-native:
#     * scripts/build-windows.sh       — x64 link gate + i686 *compile* gate
#     * scripts/run-host-tests.sh      — macOS-clang gtest subset
#   Neither of them ever executes the shipping Win32 binary.  This script
#   produces a real 32-bit `igi1ed.exe`, stages it with the DLLs it actually
#   imports, and runs it under Wine, capturing the real output and exit status.
#
# WHAT IT DOES
#   1. preflight   — wine / launcher / retail data / toolchain present
#   2. x86 libs    — third_party/GL/lib/x86/{libfreeglut.a,libglew32.a}
#                    (not tracked: .gitignore has a blanket `x86/` rule; copied
#                    from IGI_X86_LIB_SRC when missing)
#   3. build       — `cmake --build build-mingw --target igi-editor`
#                    (the i686 tree is the tu-check.py source tree)
#   4. inspect     — `file` + `objdump -p` on the linked exe
#   5. stage       — <STAGE_DIR>: the exe, `version`, `editor/`, the 32-bit
#                    freeglut/glew32 DLLs, the MinGW runtime DLLs it imports,
#                    and symlinks to every other retail data entry
#   6. run         — through a COPY of the user's Wine launcher placed inside
#                    the staging directory, with a bounded timeout
#   7. summary     — per-tier PASS/FAIL + what the run does and does not prove
#
# THE pkill TRAP (read this before editing)
#   The launcher begins with `pkill -9 -f "<the exe name>"`, which kills ANY
#   process whose command line contains that string — including the shell that
#   typed it.  Never put the literal exe name on a command line: always call
#   this script as `bash scripts/run-wine-smoke.sh`.  This script only ever
#   references the name from a variable/file, never from a caller's argv.
#
# USAGE
#   bash scripts/run-wine-smoke.sh                    # build + all three tiers
#   bash scripts/run-wine-smoke.sh --no-build         # reuse the existing exe
#   bash scripts/run-wine-smoke.sh --tiers help       # only the CLI --help tier
#   bash scripts/run-wine-smoke.sh --tiers help,tests # skip the GL window tier
#   bash scripts/run-wine-smoke.sh --stage DIR        # custom staging dir
#   bash scripts/run-wine-smoke.sh --no-stage         # reuse the stage dir as-is
#   bash scripts/run-wine-smoke.sh --diag-log         # enable the editor's own log
#   bash scripts/run-wine-smoke.sh --keep-output      # leave bin/Release alone
#
# TIERS
#   help   `--help`       — process start, CRT, stdio.  GL-free.
#   tests  `--run-tests`  — the editor's own native parser tests, run by the
#                           *Windows* binary against the retail data.  GL-free.
#   gui    (no CLI args)  — the real editor: glutInit, window, level load.
#
# EXIT CODE
#   0  = the binary ran under Wine: `help` and `tests` exited 0, and the GUI
#        tier did not die on its own before the timeout.
#   1  = at least one tier failed (see the per-tier summary).
#   2  = preflight failure (missing tool/data) — nothing was exercised.
#
# ENVIRONMENT OVERRIDES
#   IGI_WINE_BIN, IGI_WINE_LAUNCHER, IGI_WINE_DATA_DIR, IGI_X86_LIB_SRC,
#   IGI_MINGW_I686_ROOT, IGI_I686_BUILD_DIR, IGI_WINE_STAGE_DIR,
#   IGI_WINE_LOG_DIR, IGI_WINE_EXE, IGI_WINE_HELP_TIMEOUT,
#   IGI_WINE_TESTS_TIMEOUT, IGI_WINE_GUI_TIMEOUT, IGI_JOBS
# ============================================================================
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# ── configuration ───────────────────────────────────────────────────────────
WINE_BIN="${IGI_WINE_BIN:-/Applications/Wine Stable.app/Contents/Resources/wine/bin/wine}"
LAUNCHER="${IGI_WINE_LAUNCHER:-/Users/haseeb-mir/Downloads/IGI/run-igi1ed.sh}"
DATA_DIR="${IGI_WINE_DATA_DIR:-/Users/haseeb-mir/Downloads/IGI}"
X86_LIB_SRC="${IGI_X86_LIB_SRC:-/Users/haseeb-mir/Documents/Code/project-igi-editor/third_party/GL/lib/x86}"
MINGW_ROOT="${IGI_MINGW_I686_ROOT:-/opt/homebrew/Cellar/mingw-w64/14.0.0_3/toolchain-i686/i686-w64-mingw32}"
BUILD_DIR="${IGI_I686_BUILD_DIR:-$REPO_ROOT/build-mingw}"
STAGE_DIR="${IGI_WINE_STAGE_DIR:-$REPO_ROOT/scratch/wine-smoke/run}"
LOG_DIR="${IGI_WINE_LOG_DIR:-$REPO_ROOT/scratch/wine-smoke/logs}"
JOBS="${IGI_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"
HELP_TIMEOUT="${IGI_WINE_HELP_TIMEOUT:-60}"
TESTS_TIMEOUT="${IGI_WINE_TESTS_TIMEOUT:-600}"
GUI_TIMEOUT="${IGI_WINE_GUI_TIMEOUT:-25}"

# OUTPUT_NAME "igi1ed" + the Win32 suffix, assembled here so that this script
# (and therefore its callers) never needs the literal name on a command line.
EXE_NAME="igi1ed${IGI_EXE_SUFFIX:-.exe}"
# IGI_WINE_EXE: smoke-test an explicit binary instead of the shared build
# output slot (useful when another agent's in-flight edit breaks the tree, or
# to re-test a saved artifact without touching bin/Release at all).
EXE_SRC="${IGI_WINE_EXE:-$REPO_ROOT/bin/Release/$EXE_NAME}"

# The 32-bit GL DLLs are the ones the shipping Win32 target actually loads.
GL_DLLS="freeglut.dll glew32.dll"
# DLLs Wine itself provides (builtins).  Anything not matching this and not
# found on disk is reported as UNRESOLVED instead of being silently ignored.
WINE_BUILTIN_RE='^(kernel32|user32|advapi32|ole32|opengl32|winmm|ntdll|msvcrt|ucrtbase|gdi32|shell32|shlwapi|comdlg32|comctl32|ws2_32|bcrypt|gdiplus|psapi|wbemuuid|setupapi|dbghelp|imm32|version|winspool|rpcrt4|oleaut32|d3d9|d3d11|dxgi|api-ms-win-[a-z0-9-]+)\.dll$'

DO_BUILD=1
DO_RESTORE=1
DO_STAGE=1
DIAG_LOG=0
TIERS="help,tests,gui"

while [ $# -gt 0 ]; do
  case "$1" in
    --no-build)     DO_BUILD=0 ;;
    --no-stage)     DO_STAGE=0 ;;
    --diag-log)     DIAG_LOG=1 ;;
    --keep-output)  DO_RESTORE=0 ;;
    --tiers)        shift; TIERS="${1:?--tiers needs a comma list}" ;;
    --stage)        shift; STAGE_DIR="${1:?--stage needs a dir}" ;;
    --timeout)      shift; GUI_TIMEOUT="${1:?--timeout needs seconds}" ;;
    -h|--help)      sed -n '2,66p' "${BASH_SOURCE[0]}"; exit 0 ;;
    *) echo "error: unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done

mkdir -p "$LOG_DIR" "$STAGE_DIR"
MAIN_LOG="$LOG_DIR/wine-smoke.log"
: >"$MAIN_LOG"

say() { printf '%s\n' "$*" | tee -a "$MAIN_LOG"; }
hr()  { say "-------------------------------------------------------------------"; }

say "==================================================================="
say " run-wine-smoke.sh"
say "   repo        : $REPO_ROOT"
say "   build tree  : $BUILD_DIR"
say "   stage dir   : $STAGE_DIR"
say "   data dir    : $DATA_DIR"
say "   wine        : $WINE_BIN"
say "   launcher    : $LAUNCHER (copied into the stage dir)"
say "   tiers       : $TIERS"
say "   started     : $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
say "==================================================================="

# ── 1. preflight ────────────────────────────────────────────────────────────
PREFLIGHT_FAIL=0
need_file() {
  if [ -e "$1" ]; then say "   ok      $2"
  else say "   MISSING $2 ($1)"; PREFLIGHT_FAIL=1; fi
}
say ""
say "==> 1. preflight"
need_file "$WINE_BIN"            "wine runtime"
need_file "$LAUNCHER"            "wine launcher"
need_file "$DATA_DIR/igi.exe"    "retail igi.exe (launcher/editor requirement)"
need_file "$DATA_DIR/MISSIONS/location0" "retail MISSIONS/location0"
for tool in cmake ninja i686-w64-mingw32-gcc i686-w64-mingw32-objdump; do
  if command -v "$tool" >/dev/null 2>&1; then say "   ok      $tool ($(command -v "$tool"))"
  else say "   MISSING $tool on PATH"; PREFLIGHT_FAIL=1; fi
done
if [ "$PREFLIGHT_FAIL" -ne 0 ]; then
  say ""
  say "==> PREFLIGHT FAILED — nothing was built or run (exit 2)"
  exit 2
fi

# ── 2. x86 import libraries ─────────────────────────────────────────────────
say ""
say "==> 2. i686 GL import libraries"
mkdir -p "$REPO_ROOT/third_party/GL/lib/x86"
for lib in libfreeglut.a libglew32.a; do
  dst="$REPO_ROOT/third_party/GL/lib/x86/$lib"
  if [ -f "$dst" ]; then
    say "   present $dst"
  elif [ -f "$X86_LIB_SRC/$lib" ]; then
    cp -p "$X86_LIB_SRC/$lib" "$dst"
    say "   copied  $dst  <- $X86_LIB_SRC/$lib"
  else
    say "   MISSING $lib in third_party/GL/lib/x86 and in $X86_LIB_SRC"
    say "==> cannot link i686 without it (exit 2)"
    exit 2
  fi
done

# ── 3. build (link) the i686 target ─────────────────────────────────────────
# The target's output directory is fixed to $REPO_ROOT/bin/Release by
# CMakeLists.txt, so an i686 link overwrites the x64 artifact other gates
# produce.  Back it up and restore it after staging, so this script leaves the
# shared output slot as it found it (--keep-output disables the restore).
BACKUP="$LOG_DIR/previous-$EXE_NAME"
RESTORE_NEEDED=0
if [ "$DO_BUILD" = 1 ]; then
  say ""
  say "==> 3. build: cmake --build $BUILD_DIR --target igi-editor"
  if [ -f "$EXE_SRC" ]; then
    cp -p "$EXE_SRC" "$BACKUP"
    RESTORE_NEEDED=1
    say "   backed up existing $EXE_SRC -> $BACKUP"
    # The output slot is shared by the x64 gate, so a foreign 64-bit artifact
    # can be sitting there.  Ninja would then consider the i686 link up to date
    # and this script would silently smoke-test the wrong binary, so drop it.
    if ! file "$EXE_SRC" | grep -q 'PE32 executable.*Intel 80386'; then
      say "   existing artifact is not a 32-bit image — removing it so ninja relinks"
      rm -f "$EXE_SRC"
    fi
  fi
  ( cd "$REPO_ROOT" && cmake --build "$BUILD_DIR" --target igi-editor -j "$JOBS" ) \
    2>&1 | tee "$LOG_DIR/i686-build.log" | tail -25
  BUILD_RC=${PIPESTATUS[0]}
  say "   cmake --build exit=$BUILD_RC (full log: $LOG_DIR/i686-build.log)"
  if [ "$BUILD_RC" -ne 0 ]; then
    say "==> BUILD FAILED (exit $BUILD_RC) — nothing was run"
    exit 1
  fi
else
  say ""
  say "==> 3. build skipped (--no-build)"
fi

if [ ! -f "$EXE_SRC" ]; then
  say "==> $EXE_SRC does not exist — nothing to run (exit 1)"
  exit 1
fi

# ── 4. inspect the linked binary ────────────────────────────────────────────
say ""
say "==> 4. linked binary"
say "   $(file "$EXE_SRC")"
say "   size $(wc -c <"$EXE_SRC" | tr -d ' ') bytes, mtime $(date -r "$EXE_SRC" -u '+%Y-%m-%dT%H:%M:%SZ')"
if ! file "$EXE_SRC" | grep -q 'PE32 executable.*Intel 80386'; then
  say "   ERROR: not a 32-bit PE32 (i686) image — refusing to run a foreign binary"
  say "   (delete $EXE_SRC and re-run without --no-build to force a relink)"
  exit 1
fi
IMPORTS="$(i686-w64-mingw32-objdump -p "$EXE_SRC" 2>/dev/null | awk '/DLL Name:/{print $3}')"
say "   imports ($(printf '%s\n' "$IMPORTS" | grep -c . )):"
printf '%s\n' "$IMPORTS" | sed 's/^/     /' | tee -a "$MAIN_LOG"

# ── 5. stage ────────────────────────────────────────────────────────────────
say ""
if [ "$DO_STAGE" = 0 ]; then
  say "==> 5. staging SKIPPED (--no-stage): reusing $STAGE_DIR as-is"
  if [ ! -f "$STAGE_DIR/$EXE_NAME" ] || [ ! -f "$STAGE_DIR/launch-staged.sh" ]; then
    say "   stage dir is not populated — run once without --no-stage first (exit 2)"
    exit 2
  fi
else
say "==> 5. staging into $STAGE_DIR"
rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR"
cp -p "$EXE_SRC" "$STAGE_DIR/$EXE_NAME"
say "   exe      $STAGE_DIR/$EXE_NAME"
for extra in version; do
  if [ -f "$REPO_ROOT/bin/Release/$extra" ]; then
    cp -p "$REPO_ROOT/bin/Release/$extra" "$STAGE_DIR/$extra"
    say "   file     $extra"
  fi
done
if [ -d "$REPO_ROOT/bin/Release/editor" ]; then
  cp -R "$REPO_ROOT/bin/Release/editor" "$STAGE_DIR/editor"
  say "   dir      editor/ ($(find "$STAGE_DIR/editor" -type f | wc -l | tr -d ' ') files)"
fi
# Everything else the editor needs comes from the retail install.  Symlink it
# rather than copying 1.1 GB; Wine follows host symlinks transparently.
LINKED=0
for entry in "$DATA_DIR"/* "$DATA_DIR"/.[!.]*; do
  [ -e "$entry" ] || continue
  base="$(basename "$entry")"
  [ -e "$STAGE_DIR/$base" ] && continue
  ln -s "$entry" "$STAGE_DIR/$base" && LINKED=$((LINKED + 1))
done
say "   links    $LINKED retail entries symlinked from $DATA_DIR"
# IGI_WINE_EXTRA_LINKS: extra "name=target" symlinks to add to the stage dir.
# Used for the differential experiment on tests that resolve paths relative to
# the current directory (e.g. the FNT suite wants `assets/editor/qed/*.fnt`).
for pair in ${IGI_WINE_EXTRA_LINKS:-}; do
  name="${pair%%=*}"; target="${pair#*=}"
  [ -e "$target" ] || continue
  rm -rf "$STAGE_DIR/$name"
  ln -s "$target" "$STAGE_DIR/$name"
  say "   extra    $name -> $target"
done

# The launcher resolves the exe next to itself, so copy it into the stage dir.
cp -p "$LAUNCHER" "$STAGE_DIR/launch-staged.sh"
chmod +x "$STAGE_DIR/launch-staged.sh"
say "   launcher $STAGE_DIR/launch-staged.sh (verbatim copy of $LAUNCHER)"

# DLLs the exe imports: stage the ones that live on disk (MinGW runtime + the
# 32-bit freeglut/glew32); report the ones only Wine can provide.
say ""
say "   DLL resolution:"
UNRESOLVED=""
for dll in $IMPORTS; do
  lower="$(printf '%s' "$dll" | tr '[:upper:]' '[:lower:]')"
  if printf '%s' "$lower" | grep -Eq "$WINE_BUILTIN_RE"; then
    continue
  fi
  src=""
  for dir in "$MINGW_ROOT/bin" "$MINGW_ROOT/lib" "$DATA_DIR" "$REPO_ROOT/assets/dlls/x86"; do
    if [ -f "$dir/$dll" ]; then src="$dir/$dll"; break; fi
  done
  if [ -z "$src" ]; then
    say "     UNRESOLVED $dll (not on disk; relying on Wine)"
    UNRESOLVED="$UNRESOLVED $dll"
  else
    rm -f "$STAGE_DIR/$dll"
    cp -p "$src" "$STAGE_DIR/$dll"
    say "     staged     $dll  <- $src"
  fi
done
# The GL DLLs are loaded at run time, not always visible in the import table
# of a static import lib, so stage them explicitly too.
for dll in $GL_DLLS; do
  if [ -f "$STAGE_DIR/$dll" ]; then continue; fi
  for dir in "$REPO_ROOT/assets/dlls/x86" "$DATA_DIR"; do
    if [ -f "$dir/$dll" ]; then
      cp -p "$dir/$dll" "$STAGE_DIR/$dll"
      say "     staged     $dll  <- $dir (GL runtime)"
      break
    fi
  done
done
say "   staged $(ls "$STAGE_DIR"/*.dll 2>/dev/null | wc -l | tr -d ' ') DLLs: $(cd "$STAGE_DIR" && ls *.dll 2>/dev/null | tr '\n' ' ')"

# --diag-log: the shipped qedconfig.qsc has QEDLogs(FALSE), so the editor writes
# no log and a failed startup is silent.  Config::Init() recompiles the .qsc to
# .qvm at every start, so flipping the staged copy turns the editor's own
# logging on and makes the failure diagnosable.  Staging copy only — the
# source tree is never touched.
if [ "$DIAG_LOG" = 1 ]; then
  cfg="$STAGE_DIR/editor/qed/qedconfig.qsc"
  if [ -f "$cfg" ]; then
    sed -i.bak 's/QEDLogs(FALSE);/QEDLogs(TRUE);/; s/QEDDebug(FALSE);/QEDDebug(TRUE);/' "$cfg"
    rm -f "$cfg.bak"
    say "   --diag-log: staged qedconfig.qsc now has $(grep -h 'QEDLogs\|QEDDebug' "$cfg" | tr '\n' ' ')"
  else
    say "   --diag-log: $cfg not found (logging stays off)"
  fi
fi
fi  # end of staging (DO_STAGE)

# ── run helpers ─────────────────────────────────────────────────────────────
# Kill anything still running out of the staging directory.  Matched on the
# stage path, never on the exe name, so this can never hit the caller's shell.
cleanup_staged() {
  pkill -TERM -f "$STAGE_DIR" 2>/dev/null
  sleep 1
  pkill -KILL -f "$STAGE_DIR" 2>/dev/null
  return 0
}

# run_bounded <logfile> <limit-seconds> [args...]
#   Sets: RUN_RC (124 = still alive at the limit), RUN_SECS, RUN_ALIVE
run_bounded() {
  local log="$1"; shift
  local limit="$1"; shift
  RUN_ALIVE=0
  RUN_SECS=0
  : >"$log"
  cleanup_staged
  /bin/zsh "$STAGE_DIR/launch-staged.sh" "$@" >"$log" 2>&1 &
  local pid=$!
  while kill -0 "$pid" 2>/dev/null; do
    if [ "$RUN_SECS" -ge "$limit" ]; then
      RUN_ALIVE=1
      kill -TERM "$pid" 2>/dev/null
      sleep 2
      kill -KILL "$pid" 2>/dev/null
      wait "$pid" 2>/dev/null
      RUN_RC=124
      cleanup_staged
      return 0
    fi
    sleep 1
    RUN_SECS=$((RUN_SECS + 1))
  done
  wait "$pid"
  RUN_RC=$?
  cleanup_staged
  return 0
}

tier_enabled() { case ",$TIERS," in *",$1,"*) return 0 ;; *) return 1 ;; esac; }

T_HELP="skipped"; T_TESTS="skipped"; T_GUI="skipped"
R_HELP="-"; R_TESTS="-"; R_GUI="-"
S_HELP="-"; S_TESTS="-"; S_GUI="-"
HELP_OK=1; TESTS_OK=1; GUI_OK=1

# ── 6a. tier: help ──────────────────────────────────────────────────────────
if tier_enabled help; then
  say ""
  hr
  say "==> 6a. TIER help — launcher with --help (GL-free)"
  hr
  run_bounded "$LOG_DIR/tier-help.log" "$HELP_TIMEOUT" --help
  S_HELP="$RUN_SECS"; R_HELP="$RUN_RC"
  say "   exit=$RUN_RC  elapsed=${RUN_SECS}s  (limit ${HELP_TIMEOUT}s)"
  if grep -q "IGI Editor" "$LOG_DIR/tier-help.log" 2>/dev/null; then
    say "   output contains the editor banner: yes"
  else
    say "   output contains the editor banner: NO"
  fi
  say "   --- captured output (tier-help.log) ---"
  sed 's/^/   | /' "$LOG_DIR/tier-help.log" | head -40 | tee -a "$MAIN_LOG"
  if [ "$RUN_RC" -eq 0 ]; then T_HELP="PASS"; else T_HELP="FAIL(exit=$RUN_RC)"; HELP_OK=0; fi
fi

# ── 6b. tier: tests ─────────────────────────────────────────────────────────
if tier_enabled tests; then
  say ""
  hr
  say "==> 6b. TIER tests — launcher with --run-tests (native parser tests, GL-free)"
  hr
  run_bounded "$LOG_DIR/tier-tests.log" "$TESTS_TIMEOUT" --run-tests
  S_TESTS="$RUN_SECS"; R_TESTS="$RUN_RC"
  say "   exit=$RUN_RC  elapsed=${RUN_SECS}s  (limit ${TESTS_TIMEOUT}s)"
  grep -E "Total Assertions|Passed Assertions|Failed Assertions|\[RESULT\]|\[CONFIG\]" \
      "$LOG_DIR/tier-tests.log" 2>/dev/null | sed 's/^/   | /' | tee -a "$MAIN_LOG"
  if [ "$RUN_RC" -eq 0 ]; then T_TESTS="PASS"; else T_TESTS="FAIL(exit=$RUN_RC)"; TESTS_OK=0; fi
fi

# ── 6c. tier: gui ───────────────────────────────────────────────────────────
if tier_enabled gui; then
  say ""
  hr
  say "==> 6c. TIER gui — launcher with no CLI args (the real GLUT/OpenGL editor)"
  say "    bounded to ${GUI_TIMEOUT}s; liveness sampled once per second"
  hr
  rm -f "$STAGE_DIR/igi1ed.log"
  run_bounded "$LOG_DIR/tier-gui.log" "$GUI_TIMEOUT"
  S_GUI="$RUN_SECS"; R_GUI="$RUN_RC"
  if [ "$RUN_ALIVE" = 1 ]; then
    say "   process was STILL ALIVE after ${RUN_SECS}s -> terminated by this script (SIGTERM/SIGKILL)"
    T_GUI="STAYED_UP"
  elif [ "$RUN_RC" -eq 0 ]; then
    say "   process exited on its own after ${RUN_SECS}s with exit 0"
    T_GUI="EXITED(0)"
  else
    say "   process DIED ON ITS OWN after ${RUN_SECS}s with exit $RUN_RC"
    T_GUI="FAIL(exit=$RUN_RC)"; GUI_OK=0
  fi
  say "   --- captured stdout/stderr (tier-gui.log) ---"
  sed 's/^/   | /' "$LOG_DIR/tier-gui.log" | head -60 | tee -a "$MAIN_LOG"
  if [ -f "$STAGE_DIR/igi1ed.log" ]; then
    say "   --- editor's own log ($STAGE_DIR/igi1ed.log), first 30 lines ---"
    head -30 "$STAGE_DIR/igi1ed.log" | sed 's/^/   | /' | tee -a "$MAIN_LOG"
    say "   ($(wc -l <"$STAGE_DIR/igi1ed.log" | tr -d ' ') lines total)"
  else
    say "   no igi1ed.log was written next to the exe (Logger never initialised)"
  fi
fi

# ── 7. restore the shared output slot ───────────────────────────────────────
if [ "$DO_BUILD" = 1 ] && [ "$DO_RESTORE" = 1 ] && [ "$RESTORE_NEEDED" = 1 ] && [ -f "$BACKUP" ]; then
  cp -p "$BACKUP" "$EXE_SRC"
  say ""
  say "==> restored $EXE_SRC from $BACKUP (the pre-existing artifact; --keep-output to skip)"
fi

# ── 8. summary ──────────────────────────────────────────────────────────────
say ""
say "==================================================================="
say " run-wine-smoke.sh summary"
say "   tier help   : $T_HELP  (exit $R_HELP, ${S_HELP}s)"
say "   tier tests  : $T_TESTS  (exit $R_TESTS, ${S_TESTS}s)"
say "   tier gui    : $T_GUI  (exit $R_GUI, ${S_GUI}s)"
say ""
say "   What this proves: the Win32 binary links, loads under Wine, and the"
say "   tiers above really executed (see the logs under $LOG_DIR)."
say "   What it does NOT prove: correctness of any editor feature, or that"
say "   rendering matches the retail game.  The GUI tier only shows whether"
say "   the process survived; a surviving process is not a rendered frame."
say "   logs: $LOG_DIR"
say "   finished: $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
say "==================================================================="

if [ "$HELP_OK" -eq 1 ] && [ "$TESTS_OK" -eq 1 ] && [ "$GUI_OK" -eq 1 ]; then
  say "==> SMOKE OK (exit 0)"
  exit 0
fi
say "==> SMOKE FAILED (exit 1)"
exit 1
