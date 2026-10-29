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
#   scratch/e2e-headless/).  The retail install is READ-ONLY: the game path
#   handed to the editor is a copy whose editor/qed is a real copy, because
#   Config::Init recompiles <IGI>/editor/qed/*.qsc into .qvm on every start
#   (source/config.cpp:216-236) and that must never touch the retail tree.
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
    -h|--help)     sed -n '2,104p' "${BASH_SOURCE[0]}"; exit 0 ;;
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
if ! file "$EXE_SRC" | grep -q 'PE32 executable.*Intel 80386'; then
  say "   ERROR: $EXE_SRC is not a 32-bit PE32 (i686) image:"
  say "          $(file "$EXE_SRC")"
  say "          Build it with: rm -f bin/Release/igi1ed.exe && cmake --build build-mingw --target igi-editor --parallel"
  say "==> PREFLIGHT FAILED (exit 2)"
  exit 2
fi
say "   ok      i686 binary: $(file "$EXE_SRC" | cut -c1-70)"

# ── 2. stage the editor and a writable copy of the game ─────────────────────
if [ "$KEEP_STAGE" = 1 ] && [ -f "$RUN_DIR/igi1ed.exe" ] && [ -d "$GAME_DIR" ]; then
  say ""
  say "==> 2. staging SKIPPED (--keep-stage): reusing $STAGE"
else
  say ""
  say "==> 2. staging"
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

  # The game copy: every retail top-level entry symlinked (read-only use) ...
  LINKED=0
  for entry in "$IGI_DIR"/* "$IGI_DIR"/.[!.]*; do
    [ -e "$entry" ] || continue
    base="$(basename "$entry")"
    [ "$base" = "editor" ] && continue
    ln -s "$entry" "$GAME_DIR/$base" && LINKED=$((LINKED + 1))
  done
  # ... except editor/qed, which is a REAL copy: Config::Init recompiles
  # <IGI>/editor/qed/*.qsc -> .qvm at every start (config.cpp:216-236), so this
  # is the one directory the editor writes into.
  mkdir -p "$GAME_DIR/editor"
  cp -R "$IGI_DIR/editor/qed" "$GAME_DIR/editor/qed"
  for sub in tools shaders docs; do
    [ -e "$IGI_DIR/editor/$sub" ] && ln -s "$IGI_DIR/editor/$sub" "$GAME_DIR/editor/$sub"
  done
  [ -f "$IGI_DIR/editor/editor.bat" ] && ln -s "$IGI_DIR/editor/editor.bat" "$GAME_DIR/editor/editor.bat"
  say "   game copy   : $GAME_DIR ($LINKED symlinks + a real editor/qed)"
  say "                 the retail tree is never written to"

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
RUN_RC=0; RUN_SECS=0; RUN_ALIVE=0
run_mode() {
  local log="$1"; shift
  local limit="$1"; shift
  RUN_ALIVE=0; RUN_SECS=0
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

# ── 3. tier: --smoke-level across every level ───────────────────────────────
say ""
hr
say "==> 3. TIER smoke — --smoke-level over $LEVEL_COUNT level(s)"
hr
SMOKE_ARGS=()
for n in "${LEVEL_ARR[@]}"; do SMOKE_ARGS+=(--level "$n"); done
SMOKE_ARGS+=(--game-path "$GAME_WIN")
SMOKE_LOG="$LOG_DIR/smoke.log"
say "   $WINE_BIN igi1ed.exe --smoke-level --level ${LEVELS} --game-path $GAME_WIN"
run_mode "$SMOKE_LOG" "$SMOKE_TIMEOUT" --smoke-level "${SMOKE_ARGS[@]}"
say "   exit=$RUN_RC  elapsed=${RUN_SECS}s  log=$SMOKE_LOG"

python3 - "$SMOKE_LOG" "$RUN_RC" "$RUN_ALIVE" "${LEVEL_ARR[@]}" <<'PY' | tee -a "$LOG" > "$LOG_DIR/smoke-verdict.txt"
import re, sys

log_path, rc, alive, *levels = sys.argv[1:]
rc = int(rc); alive = int(alive)
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

# ── 4. tier: --run-tests (native parser unit tests) ─────────────────────────
say ""
hr
say "==> 4. TIER tests — --run-tests (native parser unit tests, GL-free)"
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

# ── 5. tier: --verify-level --skip-launch per level ─────────────────────────
if [ "$FAST" = 1 ]; then
  say ""
  say "==> 5. TIER verify SKIPPED (--fast)"
else
  say ""
  hr
  say "==> 5. TIER verify — --verify-level --skip-launch (QSC parse) over $LEVEL_COUNT level(s)"
  hr
  VERIFY_LOG="$LOG_DIR/verify.log"
  VERIFY_ARGS=()
  for n in "${LEVEL_ARR[@]}"; do VERIFY_ARGS+=(--level "$n"); done
  VERIFY_ARGS+=(--skip-launch --game-path "$GAME_WIN" --delay 0)
  run_mode "$VERIFY_LOG" "$SMOKE_TIMEOUT" --verify-level "${VERIFY_ARGS[@]}"
  say "   exit=$RUN_RC  elapsed=${RUN_SECS}s  log=$VERIFY_LOG"

  python3 - "$VERIFY_LOG" "$RUN_RC" "$RUN_ALIVE" "${LEVEL_ARR[@]}" <<'PY' | tee -a "$LOG" > "$LOG_DIR/verify-verdict.txt"
import re, sys
log_path, rc, alive, *levels = sys.argv[1:]
rc = int(rc); alive = int(alive); levels = [int(x) for x in levels]
text = open(log_path, "r", errors="replace").read()

problems = []
def check(cond, msg):
    print(("   PASS  " if cond else "   FAIL  ") + msg)
    if not cond: problems.append(msg)

check(alive == 0, "verify run finished on its own (no timeout kill)")
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

# ── 6. tier: perf guard (opt-in, --perf-guard / IGI_E2E_PERF_GUARD=1) ───────
# A separate lane on purpose: it measures PERFORMANCE, which none of the tiers
# above do, and it needs its own stage (scratch/wine-smoke/run) plus ~3.5 min of
# Wine runs with the deterministic bench workloads.  Running it by default would
# slow this lane down and mix "the levels still load" with "the frame budget
# still holds", so it is off unless asked for.  Its own exit code is the verdict:
# 0 all budgets held, 1 a budget was exceeded, 2 preflight (stage not populated).
if [ "$PERF_GUARD" = 1 ]; then
  say ""
  hr
  say "==> 6. TIER perf — scripts/perf-guard.sh (headless performance budgets)"
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

# ── 7. summary ──────────────────────────────────────────────────────────────
say ""
say "==================================================================="
say " e2e-headless.sh summary"
say "   levels           : $LEVELS"
say "   tiers            : smoke, tests$([ "$FAST" = 1 ] && printf '' || printf ', verify')$([ "$PERF_GUARD" = 1 ] && printf ', perf' || printf '')"
say "   assertions       : $CHECKS_RUN run, $CHECKS_FAILED failed"
say "   known defect     : level $KNOWN_DEFECT_LEVEL fails on $KNOWN_DEFECT_MODEL (retail data)"
say "   logs             : $LOG_DIR"
say "   finished         : $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
say "==================================================================="
if [ "$CHECKS_FAILED" -eq 0 ]; then
  say "==> E2E PASS (exit 0)"
  exit 0
fi
say "==> E2E FAIL ($CHECKS_FAILED assertion(s) — see the FAIL lines above) (exit 1)"
exit 1
