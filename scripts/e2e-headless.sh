#!/usr/bin/env bash
# ============================================================================
# e2e-headless.sh — automated end-to-end test of the headless (no OpenGL)
#                   modes across all 14 levels, under Wine.
# ============================================================================
#
# WHY THIS EXISTS
#   The GUI cannot run on this host (Apple's GL 2.1 / GLSL 1.20 profile; the
#   editor's shader tiers are 21/41/45 and only the 2.1 tier compiles), so a
#   human cannot "just start the editor" to see whether a level still loads.
#   The editor already ships three GL-free modes that together cover most of
#   what a level load does:
#
#     --smoke-level --level N ...   loads objects.qvm -> QSC -> LevelObjects,
#                                   then resolves EVERY placement's model and
#                                   every texture through the renderer's own
#                                   resolution path.  Prints per-level counts,
#                                   the names of anything missing, and exits
#                                   non-zero if anything is missing.
#     --verify-level --level N --skip-launch
#                                   decompiles objects.qvm and reports the QSC
#                                   parse (it is not a standalone oracle: the
#                                   AI comparison needs a game-run log).
#     --run-tests                   the native parser unit tests.
#     --mef <in.obj> --import-mef <out.mef>
#                                   the OBJ -> MEF importer (issue #29).  Run
#                                   here because the dispatch lives in
#                                   source/cli/cli_handler.cpp, which no host
#                                   lane compiles -- this is the only gate that
#                                   exercises the shipping binary's dispatch.
#
#   This script drives all three over levels 1..14, asserts the KNOWN baseline
#   (see "EXPECTED OUTCOME" below) and exits non-zero the moment reality stops
#   matching it.  A new failure is therefore distinguishable from the one
#   documented retail defect, instead of both looking like "level 7 fails".
#
# EXPECTED OUTCOME (the baseline this script pins)
#   * Levels 1..14 all load and all of their models and textures resolve,
#     EXCEPT level 7, which fails on exactly ONE dangling retail reference:
#
#         s332_02_1 — a model id that no .res archive and no loose .mef on the
#         retail install provides.  This is a KNOWN RETAIL DEFECT (the shipped
#         level7/objects.qvm names a model the shipped data does not contain),
#         not an editor regression, and it is asserted as such: the script
#         checks that level 7 fails with exactly that one id and NOTHING else.
#         If level 7 ever starts passing, that is reported as a change to the
#         documented baseline (FAIL), because the expectation must be updated
#         deliberately rather than silently.
#
#   * Zero missing textures on every level, and zero distinct missing textures
#     in the whole run.  A missing texture is always a regression.
#
# IS IT IN THE INNER LOOP?
#   It is not the *inner* loop, but it is cheap enough to be an acceptance step:
#   a full 14-level run measured 47-98 s wall on this host (one Wine process for
#   the smoke pass, one for --run-tests, one for the 14 verify passes), because
#   none of these modes builds a GL context, loads terrain or draws a frame.
#   It still needs Wine, the retail install and a staged tree, so it belongs in
#   the Windows/acceptance lane rather than next to the native host tests.
#
#       bash scripts/e2e-headless.sh                 # everything (~1 min)
#       bash scripts/e2e-headless.sh --fast          # smoke + unit tests only
#       bash scripts/e2e-headless.sh --levels "1 7"  # a subset
#
#   The inner loop stays scripts/run-host-tests.sh (native, no Wine).
#
# BUILD THE i686 EDITOR FIRST
#   bin/Release/igi1ed.exe is a single output slot shared by the i686 tree
#   (build-mingw) and the x64 tree (build-mingw-x64), and scripts/build-windows.sh
#   runs the x64 link gate last — so after the build gate that slot holds a 64-bit
#   image.  This script refuses a foreign binary instead of running it:
#
#       rm -f bin/Release/igi1ed.exe
#       cmake --build build-mingw --target igi-editor --parallel
#       bash scripts/e2e-headless.sh
#
# WHAT THIS DOES NOT PROVE
#   Resolution, not rendering.  --smoke-level never parses a mesh, binds a
#   texture or draws a frame; --verify-level never launches the editor.  A
#   green run here means "every level's objects still resolve", not "the
#   viewport looks right".  Only a run of the real GUI on a GL 4.1 host can
#   show that.
#
# WRITES
#   Everything is written under the staging directory (default
#   scratch/e2e-headless/).  The retail install is READ-ONLY, and that is now a
#   CHECKED claim rather than a printed sentence: the staged game tree is a REAL
#   copy of the retail tree (an APFS clone, ~3.6 s for 1.5 GB), and this script
#   compares mtime+size for every file under the retail install before and after
#   the run, failing if anything changed.
#
#   It has to be a real copy.  Two code paths write into the game tree --
#   Config::Init recompiles <game>/editor/qed/*.qsc into .qvm at every start
#   (source/config.cpp:20-27, :216-236) and the menu editor writes the menu back
#   to the file it was read from (source/app_menu.cpp:806), which for the pause
#   menu is <game>/menusystem/ingamemenu.qvm.  The old staging made every
#   top-level entry a SYMLINK back into the retail install, so those writes went
#   straight through -- that is what corrupted the retail menu QVMs.  The
#   symlink farm is gone; see scripts/lib/retail-stage.sh.
#
#   The shaders the editor loads come from <exeDir>/editor/shaders
#   (source/common.cpp:907), not from this repository.  The staging step
#   therefore overlays the repository's shaders/ onto the staged editor
#   directory, so a run can never execute a stale tier, and asserts afterwards
#   that what is on disk is byte-identical to shaders/.
#
# USAGE / OPTIONS
#   --levels "1 2 ..."   levels to exercise            (default: 1..14)
#   --game-path DIR      retail install to copy from   (default: $IGI_GAME_PATH or
#                                                       /Users/haseeb-mir/Downloads/IGI)
#   --exe PATH           i686 igi1ed.exe               (default: bin/Release/igi1ed.exe)
#   --stage DIR          staging root                  (default: scratch/e2e-headless)
#   --fast               smoke + --run-tests only (skip the 14 verify passes)
#   --perf-guard         also run scripts/perf-guard.sh (headless perf budgets,
#                        +~2 min; off by default, also settable with
#                        IGI_E2E_PERF_GUARD=1)
#   --keep-stage         reuse the existing stage (skip the re-copy)
#   --diag-log           turn the staged copy's QEDLogs/QEDDebug on
#   --smoke-timeout S    wall limit for the smoke run  (default: 1800)
#   -h | --help          this text
#
# EXIT CODE
#   0  every assertion held
#   1  an assertion failed (see the FAIL lines and the captured logs)
#   2  preflight failed (missing tool/data/exe) — nothing was exercised
# ============================================================================
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

# Shared staging helpers.  See scripts/lib/retail-stage.sh for why the writable
# copy must be a REAL copy and why the repo's shaders are overlaid onto it.
# shellcheck source=lib/retail-stage.sh
. "$REPO_ROOT/scripts/lib/retail-stage.sh"

WINE_BIN="${IGI_WINE_BIN:-/Applications/Wine Stable.app/Contents/Resources/wine/bin/wine}"
IGI_DIR="${IGI_GAME_PATH:-/Users/haseeb-mir/Downloads/IGI}"
EXE_SRC="${IGI_E2E_EXE:-$REPO_ROOT/bin/Release/igi1ed.exe}"
STAGE="${IGI_E2E_STAGE:-$REPO_ROOT/scratch/e2e-headless}"
MINGW_ROOT="${IGI_MINGW_I686_ROOT:-/opt/homebrew/Cellar/mingw-w64/14.0.0_3/toolchain-i686/i686-w64-mingw32}"

LEVELS=""
FAST=0
KEEP_STAGE=0
DIAG_LOG=0
# The perf guard is a separate lane (scripts/perf-guard.sh) because it needs its
# own stage, ~3.5 min and the deterministic bench workloads.  It is wired here
# only as an opt-in step, so the default run stays as fast as it was.
PERF_GUARD="${IGI_E2E_PERF_GUARD:-0}"
SMOKE_TIMEOUT="${IGI_E2E_SMOKE_TIMEOUT:-1800}"

while [ $# -gt 0 ]; do
  case "$1" in
    --levels)      shift; LEVELS="${1:?--levels needs a list}" ;;
    --game-path)   shift; IGI_DIR="${1:?--game-path needs a dir}" ;;
    --exe)         shift; EXE_SRC="${1:?--exe needs a path}" ;;
    --stage)       shift; STAGE="${1:?--stage needs a dir}" ;;
    --fast)        FAST=1 ;;
    --keep-stage)  KEEP_STAGE=1 ;;
    --diag-log)    DIAG_LOG=1 ;;
    --perf-guard)  PERF_GUARD=1 ;;
    --smoke-timeout) shift; SMOKE_TIMEOUT="${1:?--smoke-timeout needs seconds}" ;;
    -h|--help)     sed -n '2,126p' "${BASH_SOURCE[0]}"; exit 0 ;;
    *) echo "error: unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done

if [ -z "$LEVELS" ]; then
  LEVELS="$(seq 1 14 | tr '\n' ' ')"
fi
# shellcheck disable=SC2206
LEVEL_ARR=($LEVELS)
LEVEL_COUNT=${#LEVEL_ARR[@]}

RUN_DIR="$STAGE/run"
GAME_DIR="$STAGE/game"
LOG_DIR="$STAGE/logs"
mkdir -p "$LOG_DIR"

LOG="$LOG_DIR/e2e-headless.log"
: >"$LOG"
say() { printf '%s\n' "$*" | tee -a "$LOG"; }
hr()  { say "-------------------------------------------------------------------"; }

# Win32 path form of the staged game copy: the editor is a Windows binary and
# resolves --game-path as a Windows path (Z:\... is the host root under Wine).
GAME_WIN="Z:$(printf '%s' "$GAME_DIR" | tr '/' '\\')"

say "==================================================================="
say " e2e-headless.sh — headless end-to-end across $LEVEL_COUNT level(s)"
say "   repo        : $REPO_ROOT"
say "   levels      : $LEVELS"
say "   retail      : $IGI_DIR (read-only; copied)"
say "   stage       : $STAGE"
say "   exe         : $EXE_SRC"
say "   wine        : $WINE_BIN"
say "   started     : $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
say "==================================================================="

# ── 1. preflight ────────────────────────────────────────────────────────────
PREFLIGHT_FAIL=0
need_file() { if [ -e "$1" ]; then say "   ok      $2"; else say "   MISSING $2 ($1)"; PREFLIGHT_FAIL=1; fi; }
say ""
say "==> 1. preflight"
need_file "$WINE_BIN"           "wine runtime"
need_file "$IGI_DIR/igi.exe"    "retail igi.exe"
need_file "$IGI_DIR/MISSIONS/location0" "retail MISSIONS/location0"
need_file "$EXE_SRC"            "i686 editor binary"
for tool in cmake i686-w64-mingw32-objdump; do
  if command -v "$tool" >/dev/null 2>&1; then say "   ok      $tool"; else say "   MISSING $tool on PATH"; PREFLIGHT_FAIL=1; fi
done
if [ "$PREFLIGHT_FAIL" -ne 0 ]; then
  say ""
  say "==> PREFLIGHT FAILED — nothing was run (exit 2)"
  exit 2
fi

# Guard on the guard.  The staging library is what stands between this lane and
# the retail install, and it has already been wrong once in the way that
# corrupted the retail menu QVMs.  Its claims (a real copy, writes to the stage
# cannot reach the source, an in-place retail rewrite is detected, the staged
# shaders are the repository's) are checked in a temp directory, before this
# lane touches anything.
say ""
say "==> staging library self-test (scripts/check-retail-stage.sh)"
if ! bash "$REPO_ROOT/scripts/check-retail-stage.sh" | sed 's/^/   /'; then
  say "==> PREFLIGHT FAILED — scripts/lib/retail-stage.sh does not behave as claimed (exit 2)"
  exit 2
fi
if ! file "$EXE_SRC" | grep -q 'PE32 executable.*Intel 80386'; then
  say "   ERROR: $EXE_SRC is not a 32-bit PE32 (i686) image:"
  say "          $(file "$EXE_SRC")"
  say "          Build it with: rm -f bin/Release/igi1ed.exe && cmake --build build-mingw --target igi-editor --parallel"
  say "==> PREFLIGHT FAILED (exit 2)"
  exit 2
fi
say "   ok      i686 binary: $(file "$EXE_SRC" | cut -c1-70)"

# ── 2. stage the editor and a writable copy of the game ─────────────────────
#
# The writable copy is a REAL copy (an APFS clone), not a symlink farm.  See
# scripts/lib/retail-stage.sh: the editor recompiles <game>/editor/qed/*.qsc
# into .qvm at every start (source/config.cpp:20-27, :216-236) and the menu
# editor writes <game>/menusystem/ingamemenu.qvm back through
# source/app_menu.cpp:806.  With those symlinked, both wrote through to the
# retail install -- which is what corrupted the retail menu QVMs.
#
# The retail tree's mtimes are recorded before anything touches it and compared
# again at the end; an unexplained change is a FAIL, not a note.
say ""
say "==> 2. retail tree manifest (before)"
RETAIL_BEFORE="$STAGE/retail-before.manifest"
retail_manifest "$IGI_DIR" "$RETAIL_BEFORE"
say "   $(wc -l <"$RETAIL_BEFORE" | tr -d ' ') file(s) recorded under $IGI_DIR"

if [ "$KEEP_STAGE" = 1 ] && [ -f "$RUN_DIR/igi1ed.exe" ] && [ -d "$GAME_DIR" ]; then
  say ""
  say "==> 3. staging SKIPPED (--keep-stage): reusing $STAGE"
  # The shader overlay is re-applied even on --keep-stage: the point of it is
  # that the run can never load a tier that is not the repository's, and a
  # reused stage may predate a shader edit.  Section 8 asserts the result.
  say "   shaders (re-overlaid from the repository):"
  if overlay_repo_shaders "$REPO_ROOT" "$RUN_DIR/editor" | sed 's/^/   /'; then
    :
  else
    say "   ERROR: could not overlay the repository's shaders onto the reused stage"
    PREFLIGHT_FAIL=1
  fi
  if [ "$PREFLIGHT_FAIL" -ne 0 ]; then
    say ""
    say "==> STAGING FAILED — the reused stage could not be brought up to date (exit 2)"
    exit 2
  fi
else
  say ""
  say "==> 3. staging"
  rm -rf "$RUN_DIR" "$GAME_DIR"
  mkdir -p "$RUN_DIR" "$GAME_DIR"

  cp -p "$EXE_SRC" "$RUN_DIR/igi1ed.exe"
  [ -f "$REPO_ROOT/bin/Release/version" ] && cp -p "$REPO_ROOT/bin/Release/version" "$RUN_DIR/version"
  # The exe directory supplies editor/tools/IGIModels.json (renderer_objects_metadata.cpp:64)
  # and the shader tiers; it is also where the logger writes igi1ed.log.
  if [ -d "$REPO_ROOT/bin/Release/editor" ]; then
    cp -R "$REPO_ROOT/bin/Release/editor" "$RUN_DIR/editor"
    say "   exe dir     : $RUN_DIR (editor/ = $(find "$RUN_DIR/editor" -type f | wc -l | tr -d ' ') files)"
  else
    say "   WARNING: bin/Release/editor is missing — IGIModels.json and the shader tiers will not resolve"
  fi

  # The shaders the editor loads come from <exeDir>/editor/shaders
  # (source/common.cpp:907), i.e. from bin/Release/editor/shaders — which the
  # POST_BUILD step fills from assets/editor/shaders and shaders/21.  Overlay
  # the REPOSITORY's shaders so a run can never execute a stale tier.  This is
  # the fix for the incident that made the terrain render fully fogged: the C++
  # and shaders/ were updated, assets/editor/shaders was not, and nothing in
  # this lane noticed.
  say ""
  say "   shaders (overlaid from the repository, not from the build output):"
  if overlay_repo_shaders "$REPO_ROOT" "$RUN_DIR/editor" | sed 's/^/   /'; then
    :
  else
    say "   ERROR: could not overlay the repository's shaders onto the stage"
    PREFLIGHT_FAIL=1
  fi

  # MinGW runtime + GL DLLs the import table names.  Anything Wine provides
  # (opengl32, kernel32, ...) is skipped; anything else must be found on disk.
  WINE_BUILTIN_RE='^(kernel32|user32|advapi32|ole32|opengl32|winmm|ntdll|msvcrt|ucrtbase|gdi32|shell32|shlwapi|comdlg32|comctl32|ws2_32|bcrypt|gdiplus|psapi|wbemuuid|setupapi|dbghelp|imm32|version|winspool|rpcrt4|oleaut32|d3d9|d3d11|dxgi|api-ms-win-[a-z0-9-]+)\.dll$'
  IMPORTS="$(i686-w64-mingw32-objdump -p "$EXE_SRC" 2>/dev/null | awk '/DLL Name:/{print $3}')"
  UNRESOLVED=""
  for dll in $IMPORTS; do
    lower="$(printf '%s' "$dll" | tr '[:upper:]' '[:lower:]')"
    printf '%s' "$lower" | grep -Eq "$WINE_BUILTIN_RE" && continue
    src=""
    for dir in "$MINGW_ROOT/bin" "$MINGW_ROOT/lib" "$IGI_DIR" "$REPO_ROOT/assets/dlls/x86"; do
      [ -f "$dir/$dll" ] && { src="$dir/$dll"; break; }
    done
    if [ -n "$src" ]; then cp -p "$src" "$RUN_DIR/$dll"; else UNRESOLVED="$UNRESOLVED $dll"; fi
  done
  for dll in freeglut.dll glew32.dll; do
    [ -f "$RUN_DIR/$dll" ] && continue
    for dir in "$REPO_ROOT/assets/dlls/x86" "$IGI_DIR"; do
      [ -f "$dir/$dll" ] && { cp -p "$dir/$dll" "$RUN_DIR/$dll"; break; }
    done
  done
  say "   DLLs        : $(ls "$RUN_DIR"/*.dll 2>/dev/null | wc -l | tr -d ' ') staged${UNRESOLVED:+ (unresolved:$UNRESOLVED)}"

  # The game copy: a REAL copy of the retail tree (an APFS clone, so it shares
  # blocks with the source and costs no disk for unchanged files), NOT a
  # symlink farm.  Every path a code path can write is therefore private to the
  # run:
  #
  #   editor/qed/          Config::Init recompiles *.qsc -> *.qvm at every start
  #                        (source/config.cpp:20-27, :216-236) and GetQEDDirectory
  #                        prefers the GAME tree over the exe directory.
  #   menusystem/          SaveEditorMenu writes the menu back to the file it was
  #                        read from (source/app_menu.cpp:806) --
  #                        menusystem/ingamemenu.qvm (source/menu/menu_ui.h:419).
  #   cache/               the animation and music caches.
  #   config.qvm, igi.cfg  the game's own configuration, rewritten on exit.
  #   editor/              tools, shaders, docs.
  #
  # A symlinked menusystem/ or config.qvm wrote STRAIGHT THROUGH to the retail
  # install.  That is what corrupted the retail menu QVMs, and it is why this
  # step no longer uses `ln -s`.
  say ""
  say "   game copy   : $GAME_DIR (a real copy of $IGI_DIR)"
  if stage_retail_tree "$IGI_DIR" "$GAME_DIR"; then
    say "                 $(find "$GAME_DIR" -type f | wc -l | tr -d ' ') file(s), $(du -sh "$GAME_DIR" 2>/dev/null | awk '{print $1}') logical (APFS clone)"
  else
    say "   ERROR: could not stage a real copy of $IGI_DIR"
    PREFLIGHT_FAIL=1
  fi
  # Belt and braces: a write-isolated stage must contain no symlink that points
  # back into the retail install.  A stray one would reintroduce exactly the
  # corruption this step exists to prevent.
  ESCAPING_LINKS="$(find "$GAME_DIR" -type l -exec readlink {} \; 2>/dev/null | grep -c "^$IGI_DIR" || true)"
  if [ "${ESCAPING_LINKS:-0}" -eq 0 ]; then
    say "                 no symlink escapes back into the retail tree"
  else
    say "   ERROR: $ESCAPING_LINKS staged symlink(s) point back into $IGI_DIR"
    find "$GAME_DIR" -type l -exec readlink {} \; 2>/dev/null | grep "^$IGI_DIR" | head -5 | sed 's/^/          /'
    PREFLIGHT_FAIL=1
  fi

  # The game tree also carries an editor/shaders copy (from the retail install).
  # It is not the one the editor loads -- that is <exeDir>/editor/shaders,
  # overlaid above -- but overlaying it too means no copy anywhere in the stage
  # can be stale.
  if [ -d "$GAME_DIR/editor" ]; then
    overlay_repo_shaders "$REPO_ROOT" "$GAME_DIR/editor" >/dev/null 2>&1 || true
  fi

  if [ "$PREFLIGHT_FAIL" -ne 0 ]; then
    say ""
    say "==> STAGING FAILED — the stage is not write-isolated (exit 2)"
    exit 2
  fi

  if [ "$DIAG_LOG" = 1 ]; then
    cfg="$GAME_DIR/editor/qed/qedconfig.qsc"
    if [ -f "$cfg" ]; then
      sed -i.bak 's/QEDLogs(FALSE);/QEDLogs(TRUE);/; s/QEDDebug(FALSE);/QEDDebug(TRUE);/' "$cfg"
      rm -f "$cfg.bak"
      say "   --diag-log  : staged copy now has $(grep -h 'QEDLogs\|QEDDebug' "$cfg" | tr '\n' ' ')"
    fi
  fi
fi

# ── run helper ──────────────────────────────────────────────────────────────
# run_mode <logfile> <limit-seconds> [args...]
#   Runs the editor from the exe directory (the same CWD the user's launcher
#   uses) with DISPLAY unset, and enforces a wall limit without `timeout`
#   (absent on this host) by polling the child.
#
#   Sets RUN_RC, RUN_SECS, RUN_ALIVE and RUN_SIGNAL.  RUN_SIGNAL matters: a run
#   killed from OUTSIDE leaves RUN_ALIVE=0 and an exit code of 128+signal, which
#   is indistinguishable from a clean exit unless it is recorded.  That is not
#   hypothetical -- /Users/<user>/Downloads/IGI/run-igi1ed.sh begins with
#   `pkill -9 -f "igi1ed.exe"`, which kills ANY process whose command line
#   contains that string, so a concurrent scripts/run-wine-smoke.sh kills a
#   running e2e-headless.sh smoke pass.  Observed: exit=137 (SIGKILL) 161 s into
#   a 14-level smoke run, which then looked like "level 8 never printed a
#   Result line".
RUN_RC=0; RUN_SECS=0; RUN_ALIVE=0; RUN_SIGNAL=0
run_mode() {
  local log="$1"; shift
  local limit="$1"; shift
  RUN_ALIVE=0; RUN_SECS=0; RUN_SIGNAL=0
  : >"$log"
  (
    cd "$RUN_DIR" || exit 127
    # IGI_GAME_PATH is how the modes that take no --game-path (--run-tests)
    # find the game tree; the smoke/verify tiers set the same value through
    # --game-path.  It must be the Windows form of the path: the binary is a
    # Windows image, and Z:\ is the host root under Wine.
    env -u DISPLAY IGI_GAME_PATH="$GAME_WIN" "$WINE_BIN" ./igi1ed.exe "$@"
  ) >"$log" 2>&1 &
  local pid=$!
  while kill -0 "$pid" 2>/dev/null; do
    if [ "$RUN_SECS" -ge "$limit" ]; then
      RUN_ALIVE=1
      kill -TERM "$pid" 2>/dev/null; sleep 2; kill -KILL "$pid" 2>/dev/null
      wait "$pid" 2>/dev/null
      RUN_RC=124
      return 0
    fi
    sleep 1
    RUN_SECS=$((RUN_SECS + 1))
  done
  wait "$pid"
  RUN_RC=$?
  # 128+N means "terminated by signal N", which on this host is most often the
  # launcher's `pkill -9 -f "igi1ed.exe"` in a CONCURRENT run-wine-smoke.sh.
  # Record it so the verdict can say that instead of blaming a level.
  if [ "$RUN_RC" -ge 128 ] && [ "$RUN_RC" -lt 256 ]; then
    RUN_SIGNAL=$((RUN_RC - 128))
  fi
  return 0
}

# Assertion bookkeeping.
CHECKS_RUN=0
CHECKS_FAILED=0
assert_ok()   { CHECKS_RUN=$((CHECKS_RUN+1)); say "   PASS  $1"; }
assert_fail() { CHECKS_RUN=$((CHECKS_RUN+1)); CHECKS_FAILED=$((CHECKS_FAILED+1)); say "   FAIL  $1"; }

# The documented retail defect this script pins (see EXPECTED OUTCOME above).
KNOWN_DEFECT_LEVEL=7
KNOWN_DEFECT_MODEL="s332_02_1"

# ── 4. tier: --smoke-level across every level ───────────────────────────────
say ""
hr
say "==> 4. TIER smoke — --smoke-level over $LEVEL_COUNT level(s)"
hr
SMOKE_ARGS=()
for n in "${LEVEL_ARR[@]}"; do SMOKE_ARGS+=(--level "$n"); done
SMOKE_ARGS+=(--game-path "$GAME_WIN")
SMOKE_LOG="$LOG_DIR/smoke.log"
say "   $WINE_BIN igi1ed.exe --smoke-level --level ${LEVELS} --game-path $GAME_WIN"
run_mode "$SMOKE_LOG" "$SMOKE_TIMEOUT" --smoke-level "${SMOKE_ARGS[@]}"
say "   exit=$RUN_RC  elapsed=${RUN_SECS}s  log=$SMOKE_LOG"

python3 - "$SMOKE_LOG" "$RUN_RC" "$RUN_ALIVE" "$RUN_SIGNAL" "${LEVEL_ARR[@]}" <<'PY' | tee -a "$LOG" > "$LOG_DIR/smoke-verdict.txt"
import re, sys

log_path, rc, alive, signalled, *levels = sys.argv[1:]
rc = int(rc); alive = int(alive); signalled = int(signalled)
levels = [int(x) for x in levels]
text = open(log_path, "r", errors="replace").read()
lines = [ln.rstrip() for ln in text.splitlines()]

# Section the output: one block per " SMOKE LEVEL n", then " SMOKE TOTALS — ..."
sections = {}
totals_lines = []
cur = None
for ln in lines:
    m = re.match(r"^\s*SMOKE LEVEL (\d+)\s*$", ln)
    if m:
        cur = int(m.group(1)); sections.setdefault(cur, [])
        continue
    if re.match(r"^\s*SMOKE TOTALS", ln):
        cur = "TOTALS"
        continue
    if cur == "TOTALS":
        totals_lines.append(ln)
    elif cur is not None:
        sections[cur].append(ln)

def parse_block(block):
    out = {"result": None, "missing_models": None, "missing_textures": None,
           "models_missing_distinct": None, "textures_missing_distinct": None,
           "missing_model_ids": [], "missing_texture_ids": []}
    grab = None
    for ln in block:
        m = re.match(r"^\s*Result:\s*(PASS|FAIL.*)$", ln)
        if m: out["result"] = m.group(1).strip()
        m = re.match(r"^\s*Models MISSING\s*:\s*(\d+) placement\(s\),\s*(\d+) distinct", ln)
        if m: out["models_missing_distinct"] = int(m.group(2))
        m = re.match(r"^\s*Textures MISSING\s*:\s*(\d+) placement\(s\),\s*(\d+) distinct", ln)
        if m: out["textures_missing_distinct"] = int(m.group(2))
        m = re.match(r"^\s*MISSING MODELS \((\d+) distinct\):", ln)
        if m: grab = "model"; continue
        m = re.match(r"^\s*MISSING TEXTURES \((\d+) distinct\):", ln)
        if m: grab = "texture"; continue
        m = re.match(r"^\s+-\s+(\S+)\s", ln)
        if m and grab:
            out["missing_model_ids" if grab == "model" else "missing_texture_ids"].append(m.group(1))
            continue
        if ln.strip():
            grab = None
    return out

problems = []
def check(cond, msg):
    print(("   PASS  " if cond else "   FAIL  ") + msg)
    if not cond: problems.append(msg)

# The smoke run must not have crashed or timed out: a crashed run truncates the
# output, and a truncated output would otherwise look like "fewer failures".
check(alive == 0, "smoke run finished on its own (no timeout kill)")
# A process killed from OUTSIDE leaves alive == 0 and an exit code of 128+N, so
# without this check a killed run looks like "the level never printed a Result
# line" and sends the reader hunting for a level regression that does not exist.
# Observed for real: exit=137 (SIGKILL) 161 s into a 14-level smoke pass, caused
# by a concurrent scripts/run-wine-smoke.sh -- whose launcher starts with
# `pkill -9 -f "igi1ed.exe"`.
check(signalled == 0,
      "smoke run was not killed by a signal (exit %d = SIG%d%s)" %
      (rc, signalled,
       "; a concurrent run-wine-smoke.sh launcher runs `pkill -9 -f igi1ed.exe`" if signalled == 9 else ""))
check("unknown exception" not in text and "Fatal exception" not in text,
      "smoke run reported no unhandled exception")

parsed = {}
for n in levels:
    if n not in sections:
        check(False, "level %d: section present in the smoke output" % n)
        continue
    parsed[n] = parse_block(sections[n])
    check(parsed[n]["result"] is not None, "level %d: printed a Result line" % n)

# ── the documented baseline ─────────────────────────────────────────────────
KNOWN_DEFECT_LEVEL = 7
KNOWN_DEFECT_MODEL = "s332_02_1"
KNOWN_DEFECT_TEXTURE = "s332_02_1"

for n in levels:
    p = parsed.get(n)
    if p is None: continue
    if n == KNOWN_DEFECT_LEVEL:
        # KNOWN RETAIL DEFECT: level7/objects.qvm names s332_02_1, which no
        # shipped .res archive and no loose .mef provides.  Assert the exact
        # shape of the defect so a NEW failure cannot hide behind it.
        check(p["result"] is not None and p["result"].startswith("FAIL"),
              "level %d: FAILs (known retail defect)" % n)
        check(sorted(p["missing_model_ids"]) == [KNOWN_DEFECT_MODEL],
              "level %d: the ONLY missing model is %s (got %s)" %
              (n, KNOWN_DEFECT_MODEL, sorted(p["missing_model_ids"]) or "none"))
        check(p["models_missing_distinct"] == 1,
              "level %d: exactly 1 distinct missing model (got %s)" %
              (n, p["models_missing_distinct"]))
        check(p["textures_missing_distinct"] == 0,
              "level %d: 0 missing textures (got %s)" % (n, p["textures_missing_distinct"]))
    else:
        check(p["result"] == "PASS", "level %d: PASS (got %r)" % (n, p["result"]))
        check(p["models_missing_distinct"] == 0,
              "level %d: 0 missing models (got %s)" % (n, p["models_missing_distinct"]))
        check(p["textures_missing_distinct"] == 0,
              "level %d: 0 missing textures (got %s)" % (n, p["textures_missing_distinct"]))
        check(p["missing_model_ids"] == [] and p["missing_texture_ids"] == [],
              "level %d: no missing asset was named" % n)

# ── whole-run totals ────────────────────────────────────────────────────────
run = parse_block(totals_lines)
check(run["textures_missing_distinct"] == 0,
      "whole run: 0 distinct missing textures (got %s)" % run["textures_missing_distinct"])
check(sorted(run["missing_model_ids"]) == [KNOWN_DEFECT_MODEL],
      "whole run: the only distinct missing model is %s (got %s)" %
      (KNOWN_DEFECT_MODEL, sorted(run["missing_model_ids"]) or "none"))
expected_failed = 1 if KNOWN_DEFECT_LEVEL in levels else 0
m = re.search(r"SMOKE RESULT: FAIL \((\d+) of (\d+) level\(s\)\)", text)
if expected_failed == 0:
    check(re.search(r"SMOKE RESULT: PASS", text) is not None,
          "whole run: SMOKE RESULT: PASS")
else:
    check(m is not None and int(m.group(1)) == expected_failed and int(m.group(2)) == len(levels),
          "whole run: SMOKE RESULT reports %d of %d level(s) failing" % (expected_failed, len(levels)))
check(rc != 0 if expected_failed else rc == 0,
      "smoke process exit code %d matches %d expected failing level(s)" % (rc, expected_failed))

print("")
print("   smoke: %d level section(s) parsed, %d problem(s)" % (len(parsed), len(problems)))
sys.exit(1 if problems else 0)
PY
SMOKE_VERDICT=${PIPESTATUS[0]}
if [ "$SMOKE_VERDICT" -eq 0 ]; then assert_ok "tier smoke"; else assert_fail "tier smoke"; fi

# ── 5. tier: --run-tests (native parser unit tests) ─────────────────────────
say ""
hr
say "==> 5. TIER tests — --run-tests (native parser unit tests, GL-free)"
hr
TESTS_LOG="$LOG_DIR/tests.log"
run_mode "$TESTS_LOG" 900 --run-tests
say "   exit=$RUN_RC  elapsed=${RUN_SECS}s  log=$TESTS_LOG"
grep -E "Total Assertions|Passed Assertions|Failed Assertions|\[RESULT\]" "$TESTS_LOG" 2>/dev/null | sed 's/^/   | /' | tee -a "$LOG"
FAILED_ASSERTIONS="$(grep -oE "Failed Assertions[^0-9]*([0-9]+)" "$TESTS_LOG" 2>/dev/null | grep -oE "[0-9]+" | tail -1)"
if [ "$RUN_RC" -eq 0 ] && [ "${FAILED_ASSERTIONS:-0}" = "0" ]; then
  assert_ok "tier tests (exit 0, failed assertions=${FAILED_ASSERTIONS:-0})"
else
  assert_fail "tier tests (exit=$RUN_RC, failed assertions=${FAILED_ASSERTIONS:-unknown})"
fi

# ── 5b. tier: --import-mef (OBJ -> MEF importer, issue #29) ─────────────────
#
# WHY THIS TIER EXISTS
#   tests/test_mef_import.cpp covers the OBJ parser, the MEF builder, the ILFF
#   serialiser and the argument parser, but it runs in the host lane, which does
#   not compile source/cli/cli_handler.cpp (that file is in SOURCES_CLI, built
#   only into the Windows igi-editor target).  So the question that issue #29
#   actually asks -- "does igi1ed.exe --mef <in.obj> --import-mef <out.mef>
#   work from the CLI?" -- is answered by no gate unless it is answered here, by
#   running the shipping binary.  This tier is that gate: it is the only place
#   the real dispatch, the real OBJ parser and the real serialiser run together.
#
#   It is GL-free: --import-mef is dispatched before any window or GL context is
#   created, so it needs no OpenGL 4.1 and runs on this host.
#
#   What it does NOT prove: that the written .mef renders (that needs the GUI) or
#   that the geometry survived (that is the host lane's round trip, which parses
#   the writer's output with the shipping reader).  This tier proves the file is
#   produced by the command the issue names, and that the container it produces
#   is a well-formed ILFF with a self-consistent declared size.
say ""
hr
say "==> 5b. TIER import — --mef <in.obj> --import-mef <out.mef> (issue #29, GL-free)"
hr
IMPORT_DIR="$STAGE/import"
IMPORT_WIN="Z:$(printf '%s' "$IMPORT_DIR" | tr '/' '\\')"
mkdir -p "$IMPORT_DIR"
rm -f "$IMPORT_DIR"/*.mef
# A two-material mesh, so the OBJ parser, the group builder and the DNER group
# chain are all exercised rather than a single-triangle trivial case.
cat > "$IMPORT_DIR/quad_hp.obj" <<'OBJ'
# OBJ fixture for the e2e import tier (issue #29).
v -1.0 0.0 -1.0
v  1.0 0.0 -1.0
v  1.0 0.0  1.0
v -1.0 0.0  1.0
v  0.0 1.0  0.0
vt 0.0 0.0
vt 1.0 0.0
vt 1.0 1.0
vt 0.0 1.0
vt 0.5 0.5
vn 0.0 1.0 0.0
usemtl wall
f 1/1/1 4/4/1 3/3/1
f 1/1/1 3/3/1 2/2/1
usemtl trim
f 1/1/1 2/2/1 5/5/1
f 2/2/1 3/3/1 5/5/1
f 3/3/1 4/4/1 5/5/1
f 4/4/1 1/1/1 5/5/1
OBJ

# The exact command form issue #29 asks for: --mef <input.obj> --import-mef <out.mef>.
IMPORT_LOG="$LOG_DIR/import.log"
run_mode "$IMPORT_LOG" 300 --mef "$IMPORT_WIN\\quad_hp.obj" --import-mef "$IMPORT_WIN\\quad_hp.mef"
say "   wine igi1ed.exe --mef <in.obj> --import-mef <out.mef>"
say "   exit=$RUN_RC  elapsed=${RUN_SECS}s  log=$IMPORT_LOG"
grep -E "^\[MEF\]" "$IMPORT_LOG" 2>/dev/null | sed 's/^/   | /' | tee -a "$LOG"
MEF_OUT="$IMPORT_DIR/quad_hp.mef"
if [ "$RUN_RC" -eq 0 ] && grep -q "\[MEF\] SUCCESS" "$IMPORT_LOG" 2>/dev/null && [ -s "$MEF_OUT" ]; then
  assert_ok "issue #29 CLI form: --mef <in.obj> --import-mef <out.mef> wrote $MEF_OUT"
else
  assert_fail "issue #29 CLI form (exit=$RUN_RC, output $([ -s "$MEF_OUT" ] && echo present || echo missing))"
fi

# The container the importer emits must be a well-formed ILFF: magic, and a
# declared total size that matches the bytes on disk.  Read independently of the
# editor (xxd/wc), so this is not the writer checking itself.
if [ -s "$MEF_OUT" ]; then
  MEF_MAGIC="$(xxd -p -l 4 "$MEF_OUT" 2>/dev/null)"
  MEF_BYTES="$(wc -c < "$MEF_OUT" | tr -d ' ')"
  MEF_DECL_HEX="$(xxd -p -s 4 -l 4 "$MEF_OUT" 2>/dev/null | sed 's/^\(..\)\(..\)\(..\)\(..\)$/\4\3\2\1/')"
  MEF_DECL=$((16#$MEF_DECL_HEX))
  if [ "$MEF_MAGIC" = "494c4646" ]; then
    assert_ok "written file starts with the ILFF magic"
  else
    assert_fail "written file magic is '$MEF_MAGIC', expected '494c4646' (ILFF)"
  fi
  if [ "$MEF_DECL" = "$MEF_BYTES" ]; then
    assert_ok "ILFF declared size ($MEF_DECL) matches the file size ($MEF_BYTES)"
  else
    assert_fail "ILFF declared size ($MEF_DECL) != file size ($MEF_BYTES)"
  fi
else
  assert_fail "no output .mef to inspect"
fi

# The second accepted form, --import-mef <in.obj> <out.mef>, must take the same
# path and produce the same bytes.
run_mode "$IMPORT_LOG.two" 300 --import-mef "$IMPORT_WIN\\quad_hp.obj" "$IMPORT_WIN\\quad_hp_two.mef"
say "   wine igi1ed.exe --import-mef <in.obj> <out.mef>"
say "   exit=$RUN_RC  elapsed=${RUN_SECS}s"
if [ "$RUN_RC" -eq 0 ] && [ -s "$IMPORT_DIR/quad_hp_two.mef" ] &&
   cmp -s "$MEF_OUT" "$IMPORT_DIR/quad_hp_two.mef"; then
  assert_ok "--import-mef <in.obj> <out.mef> produces byte-identical output"
else
  assert_fail "--import-mef <in.obj> <out.mef> (exit=$RUN_RC, or output differs)"
fi

# A refusal must be a refusal: non-zero exit and no file left behind.  The
# importer builds everything before touching the disk for exactly this reason.
MEF_ABSENT="$IMPORT_DIR/absent.mef"
rm -f "$MEF_ABSENT"
run_mode "$IMPORT_LOG.bad" 300 --mef "$IMPORT_WIN\\does_not_exist.obj" --import-mef "$IMPORT_WIN\\absent.mef"
say "   wine igi1ed.exe --mef <missing.obj> --import-mef <out.mef>"
say "   exit=$RUN_RC  elapsed=${RUN_SECS}s"
if [ "$RUN_RC" -ne 0 ] && [ ! -e "$MEF_ABSENT" ]; then
  assert_ok "an unreadable OBJ fails (exit $RUN_RC) and leaves no output file"
else
  assert_fail "an unreadable OBJ must fail and leave no file (exit=$RUN_RC, file $([ -e "$MEF_ABSENT" ] && echo present || echo absent))"
fi

# ── 6. tier: --verify-level --skip-launch per level ─────────────────────────
if [ "$FAST" = 1 ]; then
  say ""
  say "==> 6. TIER verify SKIPPED (--fast)"
else
  say ""
  hr
  say "==> 6. TIER verify — --verify-level --skip-launch (QSC parse) over $LEVEL_COUNT level(s)"
  hr
  VERIFY_LOG="$LOG_DIR/verify.log"
  VERIFY_ARGS=()
  for n in "${LEVEL_ARR[@]}"; do VERIFY_ARGS+=(--level "$n"); done
  VERIFY_ARGS+=(--skip-launch --game-path "$GAME_WIN" --delay 0)
  run_mode "$VERIFY_LOG" "$SMOKE_TIMEOUT" --verify-level "${VERIFY_ARGS[@]}"
  say "   exit=$RUN_RC  elapsed=${RUN_SECS}s  log=$VERIFY_LOG"

  python3 - "$VERIFY_LOG" "$RUN_RC" "$RUN_ALIVE" "$RUN_SIGNAL" "${LEVEL_ARR[@]}" <<'PY' | tee -a "$LOG" > "$LOG_DIR/verify-verdict.txt"
import re, sys
log_path, rc, alive, signalled, *levels = sys.argv[1:]
rc = int(rc); alive = int(alive); signalled = int(signalled); levels = [int(x) for x in levels]
text = open(log_path, "r", errors="replace").read()

problems = []
def check(cond, msg):
    print(("   PASS  " if cond else "   FAIL  ") + msg)
    if not cond: problems.append(msg)

check(alive == 0, "verify run finished on its own (no timeout kill)")
check(signalled == 0,
      "verify run was not killed by a signal (exit %d = SIG%d%s)" %
      (rc, signalled,
       "; a concurrent run-wine-smoke.sh launcher runs `pkill -9 -f igi1ed.exe`" if signalled == 9 else ""))
check("Fatal exception" not in text and "[ERROR]" not in text,
      "verify run reported no fatal exception / [ERROR]")

# One "Processing Level N" + one "LEVEL N VERIFICATION REPORT" per requested level.
for n in levels:
    check(("Processing Level %d" % n) in text, "level %d: processed" % n)
    check(("LEVEL %d VERIFICATION REPORT" % n) in text,
          "level %d: produced a verification report" % n)

# The QSC parse is the part that is a standalone oracle.  A parse overflow or a
# failed decompile is a real failure; the AI comparison is skipped without a
# game-run log, so "Result: FAIL" on its own is not.
check("parse overflowed" not in text.lower() and "HadOverflow" not in text,
      "no QSC parse overflow on any level")
check("QVM decompile failed" not in text and "cannot parse QVM" not in text,
      "no QVM decompile/parse failure on any level")

# Each level must decompile and parse to a non-empty object set.  This is the
# assertion that would catch a QSC/QVM regression; the "missing" tables below it
# are all zero-matched because --skip-launch has no game-run log to compare
# against (verify_level says so itself: "No 'LoadLevel() START for level N'").
for n in levels:
    blocks = text.split("Processing Level %d" % n, 1)
    seg = blocks[1].split("Processing Level", 1)[0] if len(blocks) > 1 else ""
    m = re.search(r"QSC:\s*(\d+) buildings,\s*(\d+) objects,\s*(\d+) AI", seg)
    check(m is not None, "level %d: QSC parse reported counts" % n)
    if m:
        b, o, a = (int(m.group(1)), int(m.group(2)), int(m.group(3)))
        check(o > 0 and b > 0, "level %d: QSC parsed %d buildings / %d objects / %d AI"
              % (n, b, o, a))

results = re.findall(r"Result:\s*(PASS|FAIL[^\n]*)", text)
failed = [r for r in results if r.startswith("FAIL")]
print("   verify: %d report(s), %d Result line(s) (%d FAIL)" % (len(levels), len(results), len(failed)))
if failed:
    print("   note: those FAILs are the AI/log comparison, which cannot run without a")
    print("         game-run log (--skip-launch).  The assertions above cover what")
    print("         this mode can decide on its own: decompile + QSC parse.")
sys.exit(1 if problems else 0)
PY
  VERIFY_VERDICT=${PIPESTATUS[0]}
  if [ "$VERIFY_VERDICT" -eq 0 ]; then assert_ok "tier verify"; else assert_fail "tier verify"; fi
fi

# ── 7. tier: perf guard (opt-in, --perf-guard / IGI_E2E_PERF_GUARD=1) ───────
# A separate lane on purpose: it measures PERFORMANCE, which none of the tiers
# above do, and it needs its own stage (scratch/wine-smoke/run) plus ~3.5 min of
# Wine runs with the deterministic bench workloads.  Running it by default would
# slow this lane down and mix "the levels still load" with "the frame budget
# still holds", so it is off unless asked for.  Its own exit code is the verdict:
# 0 all budgets held, 1 a budget was exceeded, 2 preflight (stage not populated).
if [ "$PERF_GUARD" = 1 ]; then
  say ""
  hr
  say "==> 7. TIER perf — scripts/perf-guard.sh (headless performance budgets)"
  hr
  PERF_LOG="$LOG_DIR/perf-guard.log"
  PERF_RC=0
  bash scripts/perf-guard.sh >"$PERF_LOG" 2>&1 || PERF_RC=$?
  grep -E "^   (PASS|FAIL|SKIP|measured|note)|^==>" "$PERF_LOG" 2>/dev/null | sed 's/^/   | /' | tee -a "$LOG"
  if [ "$PERF_RC" -eq 0 ]; then
    assert_ok "tier perf (all budgets held)"
  else
    assert_fail "tier perf (exit $PERF_RC — full log: $PERF_LOG)"
  fi
fi

# ── 8. the stage did not reach the retail tree, and ran the repo's shaders ──
#
# Two assertions that exist because both of them were false once:
#
#   1. THE RETAIL TREE IS UNCHANGED.  The lanes used to claim this in a printed
#      sentence while building the "writable copy" out of symlinks back into
#      <IGI>.  It was not true, and the retail menu QVMs were corrupted twice as
#      a result (Config::Init rewrites <game>/editor/qed/*.qvm at every start,
#      source/config.cpp:20-27/:216-236; SaveEditorMenu rewrites
#      <game>/menusystem/ingamemenu.qvm, source/app_menu.cpp:806).  A sentence
#      is not a check, so this compares mtime+size for every file before and
#      after.
#
#   2. THE SHADERS THE RUN LOADED ARE THE REPOSITORY'S.  The editor reads
#      <exeDir>/editor/shaders (source/common.cpp:907), so a stale copy there is
#      invisible to every reader of shaders/.  That divergence made the terrain
#      render fully fogged on every GL 4.1 host (commit 33368d7c renamed the fog
#      uniform in the C++ and in shaders/, and assets/editor/shaders -- what the
#      build copies -- kept the old name).
say ""
hr
say "==> 8. stage isolation and shader provenance"
hr

say "   retail tree (before): $(wc -l <"$RETAIL_BEFORE" | tr -d ' ') file(s) recorded"
RETAIL_AFTER="$STAGE/retail-after.manifest"
retail_manifest "$IGI_DIR" "$RETAIL_AFTER"
RETAIL_CHANGES_LOG="$LOG_DIR/retail-changes.txt"
: >"$RETAIL_CHANGES_LOG"
if retail_manifest_report "$RETAIL_BEFORE" "$RETAIL_AFTER" >"$RETAIL_CHANGES_LOG" 2>&1; then
  assert_ok "the retail tree is byte-for-byte unchanged (mtime+size, $(wc -l <"$RETAIL_AFTER" | tr -d ' ') files)"
else
  sed 's/^/   | /' "$RETAIL_CHANGES_LOG" | tee -a "$LOG"
  say "   (full list: $RETAIL_CHANGES_LOG)"
  assert_fail "the retail tree at $IGI_DIR was WRITTEN TO during this run — see above"
fi

# The stage must be write-isolated: no symlink may point back into the retail
# tree, and no path under it may be a symlink to a retail file.
ESCAPING_LINKS="$(find "$GAME_DIR" -type l -exec readlink {} \; 2>/dev/null | grep -c "^$IGI_DIR" || true)"
if [ "${ESCAPING_LINKS:-0}" -eq 0 ]; then
  assert_ok "the staged game copy contains no symlink back into the retail tree"
else
  assert_fail "$ESCAPING_LINKS staged symlink(s) escape into $IGI_DIR"
fi

if assert_repo_shaders_staged "$REPO_ROOT" "$RUN_DIR/editor"; then
  assert_ok "every shader tier in the stage is byte-identical to the repository's shaders/"
else
  assert_fail "a shader tier in the stage differs from the repository (see above)"
fi

# ── 9. summary ──────────────────────────────────────────────────────────────
say ""
say "==================================================================="
say " e2e-headless.sh summary"
say "   levels           : $LEVELS"
say "   tiers            : smoke, tests, import$([ "$FAST" = 1 ] && printf '' || printf ', verify')$([ "$PERF_GUARD" = 1 ] && printf ', perf' || printf '')"
say "   assertions       : $CHECKS_RUN run, $CHECKS_FAILED failed"
say "   known defect     : level $KNOWN_DEFECT_LEVEL fails on $KNOWN_DEFECT_MODEL (retail data)"
say "   stage            : $STAGE (a real copy of $IGI_DIR; the retail tree was verified unchanged)"
say "   shaders          : the repository's shaders/ were overlaid onto the stage before the run"
say "   logs             : $LOG_DIR"
say "   finished         : $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
say "==================================================================="
if [ "$CHECKS_FAILED" -eq 0 ]; then
  say "==> E2E PASS (exit 0)"
  exit 0
fi
say "==> E2E FAIL ($CHECKS_FAILED assertion(s) — see the FAIL lines above) (exit 1)"
exit 1
