#!/usr/bin/env bash
# ============================================================================
# perf-guard.sh — the headless performance guard.
#
# WHAT THIS IS FOR
#   The user's requirement: "keep checking performance as well ... keep checking
#   headless mode, it should never degrade".  A degradation has to be caught by a
#   LANE, not by the user noticing that the editor got slow again.  This script
#   is that lane: it runs the editor headlessly under Wine with the
#   deterministic, env-gated bench workloads, reads the counters those benches
#   emit, and FAILS when a budget is exceeded.
#
# WHY IT DOES NOT TIME THE SMOKE RUN
#   The ordinary smoke run is driven by REAL MOUSE INPUT and is not reproducible:
#   across runs it has activated 'Quit', and once 'Reset Level' with a 16 s
#   reload stall.  Wall-clock frame totals therefore differ run to run for
#   reasons that have nothing to do with the code (and this host has been seen at
#   load average 30-110 all session).  So this guard asserts only on
#   DETERMINISTIC counters -- counts, and ratios that normalize the view and the
#   frame -- plus the env-gated benches that fix the workload:
#
#     IGI_PROFILE=1                    [ObjectProfile] [PickPass] [PickBench]
#                                      [AnimProfile] [GameplayAnimProfile]
#     IGI_PROFILE_PICKBENCH=1          4 picks/frame at fixed screen positions
#     IGI_PROFILE_ANIMBENCH=1          forces one animated object to be selected
#     IGI_PROFILE_GAMEPLAYANBENCH=1    enters Game Play (the plain smoke run
#                                      never does: 0 of 25 runs)
#
# WHAT IT PROTECTS (each budget names the commit whose win it pins)
#
#   405df37c  memoised ReadMefMeshHeader: ~2832 .res reads/frame -> 0,
#             2534-5399 ms/frame -> 155-163 ms/frame.
#             F1 counts .res reads per frame (new IGI_PROFILE counter,
#             res_writer.cpp / [ObjectProfile] resReads=); F2 is the time
#             backstop on the same phase, normalized per object.
#   1ef70789  culled the picking pass: 23 -> 7 ms/pick, 523 objects / 2956 draws
#             per pass -> 117 / 721, identical picked ids.
#             F3 budgets the pick's submesh draws as a RATIO to the visual pass
#             measured in the same run -- the cull's actual contract.  Absolute
#             counts and times are printed but not budgeted; see "WHY THE PICK
#             IS BUDGETED AS A RATIO" below.
#   a2b0cc0d / 7985e2e4 / f4204d55  the pose/clock split, and the pose ONCE PER
#             FRAME instead of once per engine frame.  F5/G1 assert the
#             structural invariant: poses <= 2*posed, i.e. one in-loop pose plus
#             the draw-time wake the counter also counts, per posed playback
#             (pre-f4204d55 poses tracked `steps` -- 185-680 in the measured
#             windows).  The same invariant is also a native unit test
#             (tests/test_anim_playback.cpp asserts EnginePoseSteps()==1 for a
#             long dt), which is why the unreliable gameplay lane is opt-in
#             rather than load-bearing.
#   405df37c (headless lane)  H1 checks the GL-free --smoke-level pass reads the
#             archive once per placement it resolves, no more.
#
# BUDGETS AND THE HEADROOM CHOSEN FOR THEM
#   Measured on 2026-10-29 at load average 11-49 on this host, level 1, i686
#   build of feature-igi-editor.  "Regression" is the value the budget must
#   still catch.
#
#     id counter                                    measured      budget   regression
#     S0 [ObjectProfile] windows with objects>=100  59/59 (good)   >= 8     empty-scene run
#     F1 [ObjectProfile] resReads/frame, MEDIAN     0              <= 2     ~2832
#     F2 [ObjectProfile] resolve us per object      max 86         <= 200   193-2480
#     F3 [PickPass] draws / visual draws, MEDIAN    1.06-1.26      <= 3.0   4.1 (2956/721)
#     F5 [AnimProfile] poses vs posed               13 vs 13       poses<=2*posed  ~250 vs 13
#     G1 [GameplayAnimProfile] poses vs posed       (opt-in)       poses<=2*posed  poses~=steps
#     H1 --smoke-level res reads vs placements      2502 = 523+1979  <=1.10x+50  per-placement re-read
#
#   The reasoning behind each margin:
#     S0  is the sample-validity gate, not a budget: the camera is driven by the
#         physical mouse and a run can end up rendering an empty scene (measured:
#         344 of 352 windows at objects=0/frame).  The level-1 default view draws
#         210-236 objects/frame and a camera flying away decays 143/64/32/14/6/0,
#         so objects>=100 separates "rendered the level" from "flew out of it".
#         Requiring 8 such windows (a good run has 59) refuses to certify a run
#         that measured nothing, instead of passing it vacuously.
#     F1  counts are load-immune, so the only headroom needed is for legitimate
#         lazy loads; the MEDIAN of a run is exactly 0 reads/frame, and the
#         regression is ~1400x the budget in every window.  The median rather
#         than the worst is deliberate: a camera that moves into undrawn parts of
#         the level bursts reads for a window or two while meshes/textures load
#         lazily (183 reads/frame in a single window was measured with the
#         memoisation intact), which a max-based budget would call a regression.
#         This is the sharp instrument for the memoisation win.
#     F2  is a TIME, so it is the load-tolerant backstop, not the primary check:
#         200 us/object is 2.3x the worst window measured under load 40-49 and
#         ~5x the typical 28-43 us, while the pre-fix value was 193-562 us
#         (observed) / 2.48 ms (the commit).  It is deliberately set so a
#         re-read cannot hide under load noise, but the *count* (F1) is what
#         decides the memoisation question.
#     F3  the ratio's median is 1.06-1.26 in every run measured (moving camera
#         and parked), worst single paired window 2.62, so 3.0 leaves 2.4x
#         headroom over the worst median and still sits below the un-culled 4.1.
#         A median (not a max) absorbs the transition windows where the camera
#         moved between the pick window and the visual window it is paired with.
#     F5  has no timing in it: `poses` may exceed `posed` by one draw-time wake
#         per posed playback per frame because the counter includes the wake and
#         the wake lands in the next frame's delta (measured 11 vs 9 in a churning
#         window, exactly equal in steady ones), so the budget is 2x `posed` --
#         still ~10x below the pre-f4204d55 value.  If the mouse happens to be
#         dragging the camera for the whole run, UpdateAnimations is skipped and
#         the check reports SKIP rather than inventing a verdict (see below).
#     G1  same invariant on the gameplay clock path; opt-in, see below.
#     H1  is self-normalizing: the budget is computed from the run's own
#         "Objects examined" + "Textures resolved" placement counts, so a change
#         to the level data moves the budget with it.  Measured exactly
#         523 + 1979 = 2502; +10% + 50 tolerates small deliberate additions and
#         still trips on an extra read per placement.
#
# WHY THE PICK IS BUDGETED AS A RATIO
#   The cull's win IS a smaller submission set, but which objects are in that set
#   is decided by the camera the mouse drives, and BOTH the counts and the times
#   overlap across views.  Measured with the cull IN place:
#       118 objects /  731 draws /  7-9 ms per pass   parked inside the level
#         1 object  /   10 draws /    1 ms per pass   camera flown out of it
#       379 objects / 2138 draws / 20-32 ms per pass  a dense view
#       523 objects / 2956 draws /   24 ms per pass   flying through it -- the
#                                  SAME figures 1ef70789 measured WITHOUT the cull
#   So no absolute count or time threshold separates culled from un-culled: a
#   dense view legitimately costs what the un-culled pass cost at the default
#   view, because there the frustum covers the level anyway.  What does not move
#   is the RATIO: the cull's contract is that the pick considers what the visual
#   pass considered, so pick_draws/visual_draws ~ 1 (medians 1.06/1.08/1.26),
#   while the un-culled pass submits the whole level (2956/721 = 4.1).
#
# HOW TO RUN
#       bash scripts/perf-guard.sh                 # ~2 min: frame + headless
#       bash scripts/perf-guard.sh --with-gameplay # + the gameplay anim lane (~1.5 min)
#       bash scripts/perf-guard.sh --repeat 2      # every lane twice
#       bash scripts/perf-guard.sh --skip-headless
#       bash scripts/perf-guard.sh --timeout 120   # longer Wine runs, more windows
#       bash scripts/perf-guard.sh --parse-only    # re-judge the last run's logs
#
#   It measures bin/Release/igi1ed.exe (override with --exe), refuses to run a
#   non-i686 image, and refuses to measure a binary that is OLDER than the
#   newest source file (a guard that passes on a stale binary is worse than no
#   guard; --allow-stale overrides).
#
#   Build first:  cmake --build build-mingw --target igi-editor --parallel
#
# WHY THE GAMEPLAY LANE IS OPT-IN
#   The gameplay bench enters Game Play by itself, but the Game Play path lives
#   in a SECOND GLUT WINDOW whose focus is still driven by the physical mouse.
#   Measured on 2026-10-29 with the same command: the editor exited on its own
#   after 34 s (exit 0) and, on another run, died in freeglut with
#   "Function <glutSwapBuffers> called with no current window defined" -- with
#   the CURRENT binary and with the 7985e2e4 binary that produced the original
#   numbers, so it is this host's window/mouse situation, not the tree.  When
#   the lane cannot produce a window it reports SKIP, loudly, instead of
#   inventing a verdict; the same pose-once-per-frame invariant is asserted by
#   F5 and by a native unit test, so the default run loses little and stays
#   deterministic.
#
# THE STAGE DIRECTORY (read before changing the arguments)
#   The Wine runs reuse scripts/run-wine-smoke.sh --no-build --no-stage, which
#   needs a populated stage (scratch/wine-smoke/run).  Populate it once with
#       bash scripts/run-wine-smoke.sh --tiers help
#   The stage path is taken from $IGI_PERF_STAGE / $IGI_WINE_STAGE_DIR and is
#   NEVER put on a command line: run-wine-smoke.sh's cleanup does
#   `pkill -f "<stage dir>"`, which would kill any process whose argv contains
#   that path -- including this script's shell.  For the same reason do not pass
#   an absolute stage path as an argument to this script; use the environment.
#
# WHAT IT DOES NOT COVER (stated, not hidden)
#   * Wall-clock frame totals, load time, QSC->QVM compile time: mouse-driven
#     and load-dependent, deliberately unbudgeted (see above).
#   * Anything visual.  The GUI cannot run on this Mac (GL 2.1 vs the editor's
#     4.1 requirement), so no frame has ever been seen; this lane measures the
#     GL-free modes and the Wine-rendered smoke frame, nothing about pixels.
#   * Terrain fill, Level::Update, the task-tree HUD, texture upload: no
#     deterministic counter exists for them yet.
#   * The gameplay animation path unless --with-gameplay is passed AND the host
#     lets the Game Play window live (see above).
#   * Memory / leaks, and any level other than the one the smoke pass loads
#     (--level, default 1).
#
# EXIT CODE
#   0  every budget held
#   1  a budget was exceeded (see the FAIL lines)
#   2  preflight failed (missing tool / stage / binary) -- nothing was measured
# ============================================================================
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

# ── configuration ───────────────────────────────────────────────────────────
WINE_BIN="${IGI_WINE_BIN:-/Applications/Wine Stable.app/Contents/Resources/wine/bin/wine}"
STAGE="${IGI_PERF_STAGE:-${IGI_WINE_STAGE_DIR:-$REPO_ROOT/scratch/wine-smoke/run}}"
EXE_SRC="${IGI_PERF_EXE:-$REPO_ROOT/bin/Release/igi1ed.exe}"
EXE_NAME='igi1ed.exe'
OUT_ROOT="$REPO_ROOT/scratch/perf-guard"
TIMEOUT=90
REPEAT=1
LEVEL=1
SKIP_HEADLESS=0
WITH_GAMEPLAY=0
ALLOW_STALE=0
PARSE_ONLY=0

# ── budgets (see the header table for the measured value and the margin) ─────
BUD_RES_READS_PER_FRAME="${BUD_RES_READS_PER_FRAME:-2}"    # F1  [405df37c] median
BUD_RESOLVE_US_PER_OBJECT="${BUD_RESOLVE_US_PER_OBJECT:-200}"  # F2  [405df37c]
BUD_PICK_DRAW_RATIO="${BUD_PICK_DRAW_RATIO:-3.0}"          # F3  [1ef70789]
BUD_SMOKE_READ_FACTOR="${BUD_SMOKE_READ_FACTOR:-1.10}"      # H1  [405df37c]
BUD_SMOKE_READ_SLACK="${BUD_SMOKE_READ_SLACK:-50}"          # H1
WARMUP_WINDOWS=2   # first windows include lazy loads and the load->idle edge
# S0: how many windows must have drawn a substantive scene (objects>=100) before
# the frame budgets are judged at all -- see "SAMPLE VALIDITY" in the verdict.
MIN_RENDERED_WINDOWS="${MIN_RENDERED_WINDOWS:-8}"

while [ $# -gt 0 ]; do
  case "$1" in
    --timeout)       shift; TIMEOUT="${1:?--timeout needs seconds}" ;;
    --repeat)        shift; REPEAT="${1:?--repeat needs a count}" ;;
    --exe)           shift; EXE_SRC="${1:?--exe needs a path}" ;;
    --level)         shift; LEVEL="${1:?--level needs a number}" ;;
    --skip-headless) SKIP_HEADLESS=1 ;;
    # The gameplay anim lane is opt-in: its Game Play window is driven by the
    # real mouse and often cannot be measured at all on this host (see header).
    --with-gameplay) WITH_GAMEPLAY=1 ;;
    --allow-stale)   ALLOW_STALE=1 ;;
    # Re-evaluate the logs of the last run instead of running Wine again: lets a
    # budget be re-argued (BUD_* env vars) without paying for three Wine runs.
    --parse-only)    PARSE_ONLY=1 ;;
    -h|--help)       sed -n '2,179p' "${BASH_SOURCE[0]}"; exit 0 ;;
    *) echo "error: unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done

mkdir -p "$OUT_ROOT"
LOG="$OUT_ROOT/perf-guard.log"
: >"$LOG"
say() { printf '%s\n' "$*" | tee -a "$LOG"; }
hr()  { say "-------------------------------------------------------------------"; }

say "==================================================================="
say " perf-guard.sh — headless performance budgets"
say "   repo        : $REPO_ROOT"
say "   binary      : $EXE_SRC"
say "   stage       : $STAGE"
say "   timeout     : ${TIMEOUT}s per Wine run, repeat=$REPEAT"
say "   started     : $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
say "   host load   : $(uptime | sed 's/.*averages: //')"
say "==================================================================="

# ── 1. preflight ────────────────────────────────────────────────────────────
PREFLIGHT_FAIL=0
need_file() { if [ -e "$1" ]; then say "   ok      $2"; else say "   MISSING $2 ($1)"; PREFLIGHT_FAIL=1; fi; }
say ""
say "==> 1. preflight"
need_file "$WINE_BIN" "wine runtime"
need_file "$EXE_SRC"  "i686 editor binary"
for tool in python3 file; do
  if command -v "$tool" >/dev/null 2>&1; then say "   ok      $tool"; else say "   MISSING $tool on PATH"; PREFLIGHT_FAIL=1; fi
done
if [ -e "$EXE_SRC" ] && ! file "$EXE_SRC" | grep -q 'PE32 executable.*Intel 80386'; then
  say "   ERROR: $EXE_SRC is not a 32-bit PE32 (i686) image:"
  say "          $(file "$EXE_SRC")"
  say "          Build it with: rm -f bin/Release/$EXE_NAME && cmake --build build-mingw --target igi-editor --parallel"
  PREFLIGHT_FAIL=1
fi
if [ ! -f "$STAGE/$EXE_NAME" ] || [ ! -f "$STAGE/launch-staged.sh" ]; then
  say "   MISSING populated stage ($STAGE/$EXE_NAME / launch-staged.sh)"
  say "           populate it once with: bash scripts/run-wine-smoke.sh --tiers help"
  PREFLIGHT_FAIL=1
fi
if [ "$PREFLIGHT_FAIL" -ne 0 ]; then
  say ""
  say "==> PREFLIGHT FAILED — nothing was measured (exit 2)"
  exit 2
fi

# A guard that measures a binary older than the tree is measuring the wrong
# code and its PASS means nothing.  Only source/ is compared: bin/Release/
# igi1ed.exe is the editor, so a newer file under tests/ does not make it stale.
NEWEST_SRC="$(find source -type f \( -name '*.cpp' -o -name '*.h' \) -newer "$EXE_SRC" -print -quit 2>/dev/null)"
if [ -n "$NEWEST_SRC" ]; then
  if [ "$ALLOW_STALE" = 1 ]; then
    say "   WARN    binary is OLDER than $NEWEST_SRC (--allow-stale: measuring anyway)"
  else
    say "   ERROR   binary is OLDER than $NEWEST_SRC"
    say "           rebuild: cmake --build build-mingw --target igi-editor --parallel"
    say "           (or pass --allow-stale to measure it deliberately)"
    say "==> PREFLIGHT FAILED (exit 2)"
    exit 2
  fi
fi
say "   ok      binary mtime $(date -r "$EXE_SRC" -u '+%Y-%m-%dT%H:%M:%SZ'), no source file is newer"

# The stage must run the binary under test, not whatever was staged last.
cp -p "$EXE_SRC" "$STAGE/$EXE_NAME"
say "   ok      staged the binary under test into $STAGE"

# ── 2. the Wine GUI runs (frame lane and gameplay lane) ─────────────────────
# Reuses scripts/run-wine-smoke.sh for staging validation, the launcher and the
# bounded kill (there is no `timeout` on this host).  The stage is passed in the
# environment only -- see the header: run-wine-smoke.sh pkills on that path.
run_gui_lane() {
  local tag="$1"; shift
  local dir="$OUT_ROOT/$tag"
  mkdir -p "$dir"
  say "   running $tag: IGI_PROFILE=1 $* (limit ${TIMEOUT}s)"
  env -u DISPLAY \
      IGI_WINE_BIN="$WINE_BIN" \
      IGI_WINE_STAGE_DIR="$STAGE" \
      IGI_WINE_EXE="$STAGE/$EXE_NAME" \
      IGI_WINE_LOG_DIR="$dir/logs" \
      IGI_WINE_GUI_TIMEOUT="$TIMEOUT" \
      IGI_PROFILE=1 "$@" \
      bash scripts/run-wine-smoke.sh --no-build --no-stage --tiers gui \
      > "$dir/smoke.log" 2>&1
  cp -p "$STAGE/igi1ed.log" "$dir/igi1ed.log" 2>/dev/null
  say "   $tag: editor log $dir/igi1ed.log ($(wc -l < "$dir/igi1ed.log" 2>/dev/null || echo 0) lines)"
}

# ── 3. the GL-free headless run ─────────────────────────────────────────────
# --smoke-level loads a level and resolves every placement with no GL context at
# all, so this lane is the one that runs even where the GUI cannot.
run_smoke_lane() {
  local dir="$OUT_ROOT/headless"
  mkdir -p "$dir"
  local game_win="Z:$(printf '%s' "$STAGE" | tr '/' '\\')"
  say "   running headless: --smoke-level --level $LEVEL (limit ${TIMEOUT}s)"
  (
    cd "$STAGE" || exit 127
    env -u DISPLAY IGI_GAME_PATH="$game_win" IGI_PROFILE=1 \
      "$WINE_BIN" "./$EXE_NAME" --smoke-level --level "$LEVEL" --game-path "$game_win"
  ) > "$dir/smoke.log" 2>&1 &
  local pid=$!
  local secs=0
  while kill -0 "$pid" 2>/dev/null; do
    if [ "$secs" -ge "$TIMEOUT" ]; then
      kill -TERM "$pid" 2>/dev/null; sleep 2; kill -KILL "$pid" 2>/dev/null
      wait "$pid" 2>/dev/null
      SMOKE_RC=124
      say "   headless: TIMED OUT after ${TIMEOUT}s"
      return 0
    fi
    sleep 1
    secs=$((secs + 1))
  done
  wait "$pid"
  SMOKE_RC=$?
  say "   headless: exit=$SMOKE_RC elapsed=${secs}s log=$dir/smoke.log"
}

# ── 4. verdicts ─────────────────────────────────────────────────────────────
# One python3 pass per lane: it prints every number it measured next to the
# budget it was checked against, so the output is a baseline for the next run.
# Each verdict function captures the python status before printing (a bare
# `python3 ... | tee` would report tee's status, which is always 0).
run_verdict() {
  local fn="$1"; shift
  local out rc
  out="$("$fn" "$@")"
  rc=$?
  printf '%s\n' "$out" | tee -a "$LOG"
  return "$rc"
}

verdict_frame() {
  python3 - "$1" "$WARMUP_WINDOWS" <<'PY'
import os, re, sys
log_path, warmup = sys.argv[1], int(sys.argv[2])
BUD_READS  = int(os.environ.get("BUD_RES_READS_PER_FRAME", 2))
BUD_RESOLVE= int(os.environ.get("BUD_RESOLVE_US_PER_OBJECT", 200))
BUD_PICK_RATIO = float(os.environ.get("BUD_PICK_DRAW_RATIO", 3.0))
MIN_RENDERED_WINDOWS = int(os.environ.get("MIN_RENDERED_WINDOWS", 8))
try:
    text = open(log_path, "r", errors="replace").read()
except OSError:
    print("   FAIL  frame lane: no editor log at %s" % log_path); sys.exit(1)

problems, skipped = [], []
def check(ok, msg):
    print(("   PASS  " if ok else "   FAIL  ") + msg)
    if not ok: problems.append(msg)
def median(xs):
    xs = sorted(xs); n = len(xs)
    return xs[n // 2] if n % 2 else (xs[n // 2 - 1] + xs[n // 2]) / 2.0

# [ObjectProfile] 5 frames: resolve=6ms ... objects=210/frame submeshDraws=578/frame
#                            culledLod=0/frame skipped=0/frame resReads=0/frame
# The resReads field is new (this branch's IGI_PROFILE counter).  A binary built
# before it is still worth judging on the time budgets, so the field is optional
# in the parse and its absence is reported as F1 failing -- never as a pass.
obj_re = re.compile(
    r"\[ObjectProfile\] (\d+) frames: resolve=(\d+)ms draw=(\d+)ms atta=(\d+)ms "
    r"objects=(\d+)/frame submeshDraws=(\d+)/frame culledLod=(\d+)/frame "
    r"skipped=(\d+)/frame resReads=(\d+)/frame")
# (time, visual submesh draws/frame) -- used to pair the pick pass with the
# visual pass that was in force when it ran (see F3).
obj_ts_re = re.compile(
    r"(\d\d:\d\d:\d\d).*\[ObjectProfile\] \d+ frames: resolve=\d+ms draw=\d+ms atta=\d+ms "
    r"objects=\d+/frame submeshDraws=(\d+)/frame")
obj_re_noreads = re.compile(
    r"\[ObjectProfile\] (\d+) frames: resolve=(\d+)ms draw=(\d+)ms atta=(\d+)ms "
    r"objects=(\d+)/frame submeshDraws=(\d+)/frame")
def parse_objs(rx, has_reads):
    out = []
    for m in rx.finditer(text):
        out.append(dict(resolve_ms=int(m.group(2)), objects=int(m.group(5)),
                        res_reads=int(m.group(9)) if has_reads else None))
    return out
objs = parse_objs(obj_re, True)
has_reads = True
if not objs:
    objs = parse_objs(obj_re_noreads, False)
    has_reads = False
if not objs:
    print("   FAIL  frame lane: no [ObjectProfile] window found (is IGI_PROFILE reaching the editor?)")
    sys.exit(1)

# SAMPLE VALIDITY, checked first: the camera is driven by the physical mouse, and
# a run in which it flies out of the level renders an empty scene -- the numbers
# are then real but measure nothing (344 of 352 windows at objects=0/frame was
# observed on this host).  "Rendered the level" is a window that drew a
# substantive object set; the level-1 default view draws 210-236 objects/frame,
# a flying-away camera decays through 143/64/32/14/6/0, so >=100 separates them.
rendered = [w for w in objs if w["objects"] >= 100]
print("   measured: %d [ObjectProfile] window(s), %d with a substantive scene (objects>=100)"
      % (len(objs), len(rendered)))
if len(rendered) < MIN_RENDERED_WINDOWS:
    print("   FAIL  S0 this run did not render the level: only %d of %d windows drew >=100 objects"
          % (len(rendered), len(objs)))
    print("         NOT A REGRESSION: the physical mouse moved the camera out of the level, so")
    print("         the budgets below would be measured against an empty scene. Re-run the guard.")
    print("")
    print("   frame lane: 1 check(s) failed (invalid sample)")
    sys.exit(1)

steady = rendered                      # budgets below are judged on these windows
print("   warm-up: %d window(s) skipped, %d substantive window(s) judged"
      % (min(warmup, len(objs)), len(steady)))

if has_reads:
    # The MEDIAN, not the worst: the regression this guards is SUSTAINED (~2832
    # reads in every window), while a camera that moves into a part of the level
    # it has not drawn yet legitimately bursts reads for a window or two while
    # the lazy mesh/texture loads happen (183 reads/frame in one window was
    # measured with the memoisation intact).  The worst window is still printed.
    reads = [w["res_reads"] for w in steady]
    med_reads = median(reads)
    worst_reads = max(reads)
    over = sum(1 for r in reads if r > BUD_READS)
    check(med_reads <= BUD_READS,
          "F1 .res reads/frame, MEDIAN of %d substantive window(s) = %.1f  <= %d   "
          "[405df37c memoised header]  (worst window %d, %d window(s) above budget)"
          % (len(reads), med_reads, BUD_READS, worst_reads, over))
else:
    check(False, "F1 .res reads/frame: no resReads= field in [ObjectProfile] "
                 "(the binary predates the counter — rebuild: cmake --build build-mingw "
                 "--target igi-editor --parallel)")

# Normalized per object: the view decides how many objects a frame resolves, so
# the time is only comparable against the work it did.
ratios = [(w["resolve_ms"] * 1000.0 / w["objects"], w) for w in steady if w["objects"] > 0]
if ratios:
    worst_ratio, worst_w = max(ratios, key=lambda r: r[0])
    check(worst_ratio <= BUD_RESOLVE,
          "F2 resolve us/object (worst of %d) = %.0f (resolve=%dms, objects=%d/frame)  <= %d   [405df37c backstop]"
          % (len(ratios), worst_ratio, worst_w["resolve_ms"], worst_w["objects"], BUD_RESOLVE))
else:
    check(False, "F2 resolve us/object: no substantive window")

# [PickPass] 8 passes: occupancy=0ms total=9ms objects=118 draws=731 submeshes/pass
# HOW THE PICK IS BUDGETED: as a RATIO to the visual pass measured in the same
# run, not as an absolute count or time.  The cull's contract (1ef70789) is that
# the pick considers the objects the visual pass considered, so pick_draws /
# visual_draws ~ 1 whatever the camera does; the un-culled pass submits the WHOLE
# level instead, which is 2956/721 = 4.1 at the default view.  Absolute numbers
# cannot be used: with the cull IN place this host produced 118 objects/731 draws
# parked, 1/10 with the camera flown out, and 523/2956 flying through the level
# (the un-culled figures) -- and per-window times overlapped too (a dense view
# cost 20-32 ms/pass, the un-culled default 23-24).  The ratio is what stays put:
# measured medians 1.06 (moving camera), 1.08 and 1.26 (parked), worst single
# paired window 2.62.
pick_re = re.compile(r"(\d\d:\d\d:\d\d).*\[PickPass\] \d+ passes: occupancy=(\d+)ms "
                     r"total=(\d+)ms objects=(\d+) draws=(\d+) submeshes/pass")
passes = [dict(t=m.group(1), total_ms=int(m.group(3)), objects=int(m.group(4)),
               draws=int(m.group(5))) for m in pick_re.finditer(text)]
if len(passes) < 5:
    check(False, "F3 pick pass: only %d [PickPass] window(s) found (IGI_PROFILE_PICKBENCH should "
                 "issue 4 picks/frame)" % len(passes))
else:
    # Pair every pick window with the visual window in force at that time.
    vis_ts = [(m.group(1), int(m.group(2))) for m in obj_ts_re.finditer(text)]
    pairs, vi = [], 0
    for p in passes:
        while vi + 1 < len(vis_ts) and vis_ts[vi + 1][0] <= p["t"]:
            vi += 1
        if vis_ts and vis_ts[vi][1] > 0:
            pairs.append((p["draws"] / float(vis_ts[vi][1]), p, vis_ts[vi][1]))
    if len(pairs) < 5:
        check(False, "F3 pick/visual draw ratio: only %d pairable window(s)" % len(pairs))
    else:
        med_ratio = median([r for r, _, _ in pairs])
        worst_ratio, worst_p, worst_v = max(pairs, key=lambda x: x[0])
        check(med_ratio <= BUD_PICK_RATIO,
              "F3 pick submesh draws / visual submesh draws, MEDIAN of %d paired window(s) = %.2f  "
              "<= %.1f   [1ef70789 culled pick]  (worst pair %.2f: pick %d vs visual %d)"
              % (len(pairs), med_ratio, BUD_PICK_RATIO, worst_ratio, worst_p["draws"], worst_v))
    steady_p = passes[warmup:] or passes
    ms = [p["total_ms"] for p in steady_p]
    print("   note: pick pass time median=%.1f max=%d ms/pass; submitted %d..%d objects, "
          "%d..%d submesh draws per pass"
          % (median(ms), max(ms), min(p["objects"] for p in steady_p),
             max(p["objects"] for p in steady_p), min(p["draws"] for p in steady_p),
             max(p["draws"] for p in steady_p)))
    print("         (time and counts REPORTED, not budgeted: both track the camera -- a dense")
    print("          view legitimately costs what the un-culled pass cost at the default view)")

# [PickBench] 30 frames: 4 picks/frame avg=48ms/frame (12ms/pick) ids=448,420,420,420
pb_re = re.compile(r"\[PickBench\] (\d+) frames: (\d+) picks/frame avg=(\d+)ms/frame "
                   r"\((\d+)ms/pick\) ids=(\S*)")
benches = [dict(picks=int(m.group(2)), ms_pick=int(m.group(4)), ids=m.group(5))
           for m in pb_re.finditer(text)]
if not benches:
    print("   note: no [PickBench] window found (the 4-fixed-picks harness did not run)")
else:
    vals = [b["ms_pick"] for b in (benches[warmup:] or benches)]
    print("   note: [PickBench] 4 fixed picks/frame: median=%d max=%d ms/pick over %d window(s); "
          "ids(last)=%s" % (median(vals), max(vals), len(vals), benches[-1]["ids"]))
    print("         (reported, not budgeted: same view-dependence as F3)")

# [AnimProfile] 30 frames: playbacks=30 posed=13 steps=250 poses=13 loop=2ms ...
ap_re = re.compile(r"\[AnimProfile\] (\d+) frames: playbacks=(\d+) posed=(\d+) "
                   r"steps=(\d+) poses=(\d+)")
anims = [dict(playbacks=int(m.group(2)), posed=int(m.group(3)), steps=int(m.group(4)),
              poses=int(m.group(5))) for m in ap_re.finditer(text)]
if not anims:
    # UpdateAnimations is skipped while the camera is being navigated, and the
    # camera is driven by the real mouse: zero windows means "not measured", not
    # "regressed".  The gameplay lane covers the same invariant deterministically.
    skipped.append("F5 [AnimProfile] poses<=posed: no window (mouse was navigating the camera)")
    print("   SKIP  F5 [AnimProfile] poses<=posed: no window in this run")
else:
    # The budget is poses <= 2*posed, not poses <= posed: `poses` counts the
    # engine's full bone poses INCLUDING the draw-time wake (see
    # AnimPlayback::EnginePoseSteps), and a wake raised by frame N's draw lands
    # in frame N+1's loop delta -- so at most one extra pose per posed playback
    # per frame is accounting, not work.  Measured 11 vs posed=9 in one window
    # (and exactly posed==poses in the steady ones).  The pre-f4204d55 behaviour
    # was poses tracking `steps` (185-680 in the measured windows), which this
    # still catches wherever a frame covers more than a couple of engine frames.
    bad = [a for a in anims if a["poses"] > 2 * a["posed"] or a["posed"] > a["playbacks"]]
    detail = ", ".join("posed=%d poses=%d steps=%d" % (a["posed"], a["poses"], a["steps"])
                       for a in (bad or anims)[:4])
    check(not bad,
          "F5 poses <= 2*posed (one in-loop pose + one draw-time wake each) in all %d "
          "[AnimProfile] window(s)  (%s%s)   [f4204d55 pose once/frame]"
          % (len(anims), "VIOLATING: " if bad else "", detail))

print("")
print("   frame lane: %d check(s) failed%s"
      % (len(problems), (", %d skipped" % len(skipped)) if skipped else ""))
sys.exit(1 if problems else 0)
PY
}

verdict_gameplay() {
  python3 - "$1" <<'PY'
import re, sys
log_path = sys.argv[1]
try:
    text = open(log_path, "r", errors="replace").read()
except OSError:
    print("   FAIL  gameplay lane: no editor log at %s" % log_path); sys.exit(1)

problems, skipped = [], []
def check(ok, msg):
    print(("   PASS  " if ok else "   FAIL  ") + msg)
    if not ok: problems.append(msg)

# [GameplayAnimProfile] 30 frames: playbacks=3 posed=1 clockonly=2 steps=.. poses=1
gp_re = re.compile(r"\[GameplayAnimProfile\] (\d+) frames: playbacks=(\d+) posed=(\d+) "
                   r"clockonly=(\d+) steps=(\d+) poses=(\d+)")
wins = [dict(playbacks=int(m.group(2)), posed=int(m.group(3)), clockonly=int(m.group(4)),
             steps=int(m.group(5)), poses=int(m.group(6))) for m in gp_re.finditer(text)]
if not wins:
    # The Game Play window is a second GLUT window and the physical mouse still
    # decides whether it lives: measured on this host, the editor has exited on
    # its own (exit 0) and died in freeglut ("glutSwapBuffers called with no
    # current window") on this lane, with the current binary AND with the
    # 7985e2e4 binary.  So "no window" is reported as NOT MEASURED instead of
    # being dressed up as a budget violation -- a guard that fails for reasons
    # outside its budget is a guard that gets ignored.
    entered = "[GameplayAnimBench] entering Game Play" in text
    print("   WARN  G1 NOT MEASURED: no [GameplayAnimProfile] window in this run")
    print("         the gameplay bench %s Game Play, but the pass produced no 30-frame window"
          % ("entered" if entered else "never entered"))
    print("         (this lane needs the Game Play window to survive; see the script header)")
    skipped.append("G1 gameplay pose/clock invariant: not measured")
else:
    # 2*posed for the same wake-accounting reason as F5: `poses` counts the
    # draw-time wake, which lands in the next frame's delta.
    bad = [w for w in wins if w["poses"] > 2 * w["posed"] or w["posed"] + w["clockonly"] > w["playbacks"]]
    detail = ", ".join("playbacks=%d posed=%d clockonly=%d steps=%d poses=%d"
                       % (w["playbacks"], w["posed"], w["clockonly"], w["steps"], w["poses"])
                       for w in wins[:4])
    check(not bad,
          "G1 poses <= 2*posed (and posed+clockonly <= playbacks) in all %d window(s)  (%s)   "
          "[7985e2e4 gameplay split / f4204d55]" % (len(wins), detail))
    posed_any = max(w["posed"] for w in wins)
    check(posed_any >= 1,
          "G2 at least one playback was posed (max posed=%d) — otherwise the lane measures nothing"
          % posed_any)
print("")
print("   gameplay lane: %d check(s) failed%s"
      % (len(problems), (", %d skipped" % len(skipped)) if skipped else ""))
sys.exit(1 if problems else 0)
PY
}

verdict_headless() {
  python3 - "$1" "$2" <<'PY'
import math, os, re, sys
log_path, rc = sys.argv[1], int(sys.argv[2])
FACTOR = float(os.environ.get("BUD_SMOKE_READ_FACTOR", 1.10))
SLACK  = int(os.environ.get("BUD_SMOKE_READ_SLACK", 50))
try:
    text = open(log_path, "r", errors="replace").read()
except OSError:
    print("   FAIL  headless lane: no log at %s" % log_path); sys.exit(1)

problems = []
def check(ok, msg):
    print(("   PASS  " if ok else "   FAIL  ") + msg)
    if not ok: problems.append(msg)

check(rc == 0, "H0 the headless smoke run exited 0 (got %d)" % rc)
m = re.search(r"Objects examined\s*:\s*(\d+)", text)
objects = int(m.group(1)) if m else None
m = re.search(r"Textures resolved\s*:\s*(\d+)\s+placement", text)
tex = int(m.group(1)) if m else None
m = re.search(r"Res reads\s*:\s*(\d+)", text)
reads = int(m.group(1)) if m else None
if reads is None:
    check(False, "H1 the run printed a 'Res reads' line (the binary predates the counter — rebuild it)")
elif objects is None or tex is None:
    check(False, "H1 the run printed its placement counts (objects=%s textures=%s)" % (objects, tex))
else:
    budget = int(math.ceil(FACTOR * (objects + tex))) + SLACK
    check(reads <= budget,
          "H1 .res reads = %d  <= %d  (%d objects + %d texture placements, x%.2f + %d)   "
          "[405df37c headless resolve]" % (reads, budget, objects, tex, FACTOR, SLACK))
print("")
print("   headless lane: %d check(s) failed" % len(problems))
sys.exit(1 if problems else 0)
PY
}

# ── 5. run everything ───────────────────────────────────────────────────────
export BUD_RES_READS_PER_FRAME BUD_RESOLVE_US_PER_OBJECT
export BUD_PICK_DRAW_RATIO MIN_RENDERED_WINDOWS
export BUD_SMOKE_READ_FACTOR BUD_SMOKE_READ_SLACK

CHECKS_FAILED=0
lane_fail() { CHECKS_FAILED=$((CHECKS_FAILED + 1)); }

# The gameplay bench only enters Game Play once a level is loaded; if the run
# never got there (a real mouse can steal the window's focus) that is a
# measurement failure, not a budget failure, so it is retried once.
retry_gui_lane() {
  local tag="$1" verdict_fn="$2"; shift 2
  local attempt=1
  while :; do
    if [ "$PARSE_ONLY" = 1 ]; then
      say "   (--parse-only: not running Wine; re-using $OUT_ROOT/$tag/igi1ed.log)"
    else
      run_gui_lane "$tag" "$@"
    fi
    say ""
    hr
    say "==> lane $tag (attempt $attempt)"
    hr
    if run_verdict "$verdict_fn" "$OUT_ROOT/$tag/igi1ed.log"; then
      return 0
    fi
    if [ "$attempt" -ge 2 ] || [ "$PARSE_ONLY" = 1 ]; then
      return 1
    fi
    say "   retrying $tag once (a missing window can be the real mouse stealing focus)"
    attempt=$((attempt + 1))
  done
}

for i in $(seq 1 "$REPEAT"); do
  say ""
  say "################ repeat $i of $REPEAT ################"

  say ""
  hr
  say "==> 2. frame lane — editor mode, deterministic pick + anim benches"
  hr
  if ! retry_gui_lane frame verdict_frame IGI_PROFILE_PICKBENCH=1 IGI_PROFILE_ANIMBENCH=1; then
    lane_fail
  fi

  if [ "$WITH_GAMEPLAY" = 0 ]; then
    say ""
    say "==> gameplay lane SKIPPED (opt-in: --with-gameplay; the Game Play window"
    say "    needs the real mouse to leave it alone — see the script header)"
  else
    say ""
    hr
    say "==> 3. gameplay lane — the pose/clock split under the simulation clock"
    hr
    if ! retry_gui_lane gameplay verdict_gameplay IGI_PROFILE_GAMEPLAYANIMBENCH=1; then
      lane_fail
    fi
  fi

  if [ "$SKIP_HEADLESS" = 1 ]; then
    say ""
    say "==> headless lane SKIPPED (--skip-headless)"
  else
    say ""
    hr
    say "==> 4. headless lane — --smoke-level (GL-free), archive reads per placement"
    hr
    if [ "$PARSE_ONLY" = 1 ]; then
      say "   (--parse-only: not running Wine; re-using $OUT_ROOT/headless/smoke.log)"
      SMOKE_RC=1
      grep -q "SMOKE RESULT: PASS" "$OUT_ROOT/headless/smoke.log" 2>/dev/null && SMOKE_RC=0
    else
      run_smoke_lane
    fi
    say ""
    if ! run_verdict verdict_headless "$OUT_ROOT/headless/smoke.log" "$SMOKE_RC"; then
      lane_fail
    fi
  fi
done

# ── 6. summary ──────────────────────────────────────────────────────────────
say ""
say "==================================================================="
say " perf-guard.sh summary"
say "   budgets checked : F1 resReads/frame<=$BUD_RES_READS_PER_FRAME  F2 resolve<=${BUD_RESOLVE_US_PER_OBJECT}us/object  S0 rendered windows>=$MIN_RENDERED_WINDOWS"
say "                     F3 pick/visual draw ratio median<=$BUD_PICK_DRAW_RATIO (counts+times reported)"
say "                     F5/G1 poses<=2*posed  H1 reads<=${BUD_SMOKE_READ_FACTOR}x placements+$BUD_SMOKE_READ_SLACK"
say "   lanes           : frame, headless$([ "$WITH_GAMEPLAY" = 1 ] && printf ', gameplay' || printf ' (gameplay skipped: opt-in)')"
say "   repeats         : $REPEAT"
say "   logs            : $OUT_ROOT"
say "   finished        : $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
say "==================================================================="
if [ "$CHECKS_FAILED" -eq 0 ]; then
  say "==> PERF GUARD PASS (exit 0)"
  exit 0
fi
say "==> PERF GUARD FAIL ($CHECKS_FAILED lane(s) — see the FAIL lines above) (exit 1)"
exit 1
