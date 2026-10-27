#!/usr/bin/env bash
#
# check-build-integrity.sh — refuse to trust a gate result produced by a
#                            mixed-layout (partially stale) build tree.
#
# WHY THIS EXISTS
#   A shipping binary crashed with a null write in App::Input_OnMouse.  There was
#   no source bug.  `build-mingw/CMakeFiles/igi-editor.dir/source/app_input_mouse.cpp.obj`
#   had been built hours earlier and was NEVER recompiled while `app.cpp.obj` and
#   33 others were rebuilt against a different `App` layout, because ninja had no
#   dependency record for that object:
#
#       $ <cached-ninja> -C build-mingw -t deps CMakeFiles/igi-editor.dir/source/app_input_mouse.cpp.obj
#       ...app_input_mouse.cpp.obj: #deps 0, deps mtime ... (VALID)      <-- #deps 0
#       $ <cached-ninja> -C build-mingw -t deps CMakeFiles/igi-editor.dir/source/app.cpp.obj
#       ...app.cpp.obj: #deps 668, deps mtime ... (VALID)                <-- 668
#
#   With `#deps 0` ninja believes the object includes no headers, so editing a
#   header that changes a struct layout (source/renderer/renderer_rain.h changes
#   Renderer_Rain, a member of Renderer, a member of App) never invalidates it.
#   The result is a binary whose translation units disagree about struct offsets:
#   a stale object reads a member at the old offset, lands in the zero-initialised
#   SSO buffer of an empty std::string, and writes through NULL.  It looks exactly
#   like a source bug.
#
#   The dependency DB is lost when a *different* ninja version drives the build
#   dir — this repo has two (the CMake-cached one, and Homebrew's on PATH) and the
#   newer one prints "build log version is too old; starting over" and rebuilds the
#   deps log from nothing.
#
# WHAT IT CHECKS (every rule is a hard FAIL; see docs/igi-re-parity/BUILD_GATE.md §8)
#
#   A. STALE_PCH    — a `.obj` older than its own `cmake_pch.hxx.gch`.  THIS IS
#                     THE PRIMARY CHECK, and it is load-bearing because of how a
#                     CMake PCH build records dependencies.  Measured on this tree:
#
#                       ai_events.cpp.obj:        #deps 1     (just the .cpp)
#                       animation_motion.cpp.obj: #deps 3
#                       cmake_pch.hxx.gch:        #deps 623   (incl. renderer_rain.h)
#
#                     An object compiled *through* the PCH never re-reads the
#                     headers, so its depfile lists only the .cpp; the PCH's own
#                     depfile carries the whole header closure.  A normal object
#                     therefore has NO header in its dependency list at all, and
#                     the only header-level dependency it has is the implicit
#                     `| cmake_pch.hxx.gch` edge in build.ninja:
#
#                       build CMakeFiles/igi-editor.dir/source/app_ui.cpp.obj: ... \
#                           source/app_ui.cpp | cmake_pch.hxx cmake_pch.hxx.gch || ...
#
#                     Header-change propagation is therefore: header changes ->
#                     gch is rebuilt -> gch is newer than every object -> ninja
#                     rebuilds every object through that implicit edge.  An object
#                     OLDER than its gch is one ninja did not recompile after the
#                     last PCH regeneration, i.e. the tree is not in a post-build
#                     state and a gate result on it cannot be trusted.
#   B. NO_RECORD    — a `.obj` with no entry at all in `ninja -t deps`.
#   C. EMPTY_DEPS   — a `.obj` whose entry is `#deps 0`.  This is the exact
#                     signature of the shipped bug.  Scoped to `.obj` on purpose:
#                     `generated/igi-editor.rc.res` legitimately has `#deps 0`.
#   D. DEPS_DESYNC  — ninja's own verdict, `(STALE)`/`(MISSING)` instead of
#                     `(VALID)`: the object's mtime no longer matches the mtime
#                     recorded in the deps log, i.e. the log is out of sync with
#                     the artifact.
#   E. STALE_DEP    — any file in the object's own dependency list (from ninja's
#                     dep DB) is newer than the object.  Because of the PCH this
#                     is a *secondary* rule: it mainly catches a stale `.cpp`, and
#                     the objects that were compiled WITHOUT the PCH (the 659-dep
#                     outliers, whose depfile really does list every header).
#                     It is also why the coarse "older than the newest header
#                     under source/" rule is NOT used globally: with only 1-3 deps
#                     per object there is no per-object include set to compare
#                     against, and the coarse rule would flag all 352 objects the
#                     moment any header is touched.  It is used only as a fallback
#                     for objects whose dep record is already broken (rules B/C),
#                     where the object is a FAIL regardless.
#   F. NINJA_VERSION — the ninja CMake has cached (`CMAKE_MAKE_PROGRAM`) reports a
#                     different version than the `ninja` on PATH.  A build driven
#                     by the wrong one discards the dependency DB — the root cause
#                     above.  Different paths with the SAME version are only a note.
#
# USAGE
#   bash scripts/check-build-integrity.sh              # check, exit 1 if inconsistent
#   bash scripts/check-build-integrity.sh --quiet      # only the summary + offenders
#   bash scripts/check-build-integrity.sh --fix        # delete stale objects + PCHs, rebuild
#   bash scripts/check-build-integrity.sh --build-dir DIR
#
# ENVIRONMENT
#   IGI_BUILD_DIR         build tree to inspect (default <repo>/build-mingw)
#   IGI_INTEGRITY_TARGETS CMake target dirs to check (default "igi-editor igi_tests")
#
# EXIT CODES
#   0  tree consistent (or nothing built yet — nothing to distrust)
#   1  tree INCONSISTENT: at least one rule fired
#   2  usage / environment error (no CMakeCache, no cached ninja)
#
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${IGI_BUILD_DIR:-$REPO_ROOT/build-mingw}"
TARGETS="${IGI_INTEGRITY_TARGETS:-igi-editor igi_tests}"
QUIET=0
FIX=0

while [ $# -gt 0 ]; do
  case "$1" in
    -q|--quiet)   QUIET=1 ;;
    --fix)        FIX=1 ;;
    --build-dir)  shift; BUILD_DIR="${1:?--build-dir needs a value}" ;;
    --targets)    shift; TARGETS="${1:?--targets needs a value}" ;;
    -h|--help)    sed -n '2,90p' "${BASH_SOURCE[0]}"; exit 0 ;;
    *) echo "error: unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done

say() { [ "$QUIET" = 1 ] || printf '%s\n' "$*"; }
# Problems are always printed, even under --quiet.  They go to stdout, not
# stderr, so the offenders stay in order with the summary when a caller pipes
# the whole transcript through `tee` (build-windows.sh does exactly that).
warn() { printf '%s\n' "$*"; }
err()  { printf '%s\n' "$*" >&2; }

echo "==================================================================="
echo " check-build-integrity.sh"
echo "   repo      : $REPO_ROOT"
echo "   build dir : $BUILD_DIR"
echo "   targets   : $TARGETS"
echo "==================================================================="

# ── 0. nothing built / not configured → nothing to distrust ──────────────────
if [ ! -d "$BUILD_DIR" ]; then
  say "PASS: $BUILD_DIR does not exist — nothing built, nothing to distrust"
  exit 0
fi
CACHE="$BUILD_DIR/CMakeCache.txt"
if [ ! -f "$CACHE" ]; then
  say "PASS: $BUILD_DIR is not configured (no CMakeCache.txt) — nothing to distrust"
  exit 0
fi

# ── 1. the ninja CMake has cached (NOT a bare `ninja` from PATH) ─────────────
# Evidence: build-mingw/CMakeCache.txt:CMAKE_MAKE_PROGRAM:FILEPATH=...
CACHED_NINJA="$(sed -n 's/^CMAKE_MAKE_PROGRAM:[^=]*=//p' "$CACHE" | head -1)"
if [ -z "$CACHED_NINJA" ]; then
  err "error: CMAKE_MAKE_PROGRAM is not set in $CACHE"
  exit 2
fi
if [ ! -x "$CACHED_NINJA" ]; then
  err "error: the ninja CMake has cached is not executable: $CACHED_NINJA"
  err "       (set CMAKE_MAKE_PROGRAM in the cache, or reconfigure the tree)"
  exit 2
fi

# Resolve symlinks so the Homebrew `ninja` symlink and its Cellar binary compare
# equal instead of looking like two different programs.
resolve_path() {
  local p="$1" d
  while [ -L "$p" ]; do
    d="$(cd "$(dirname "$p")" 2>/dev/null && pwd -P)" || break
    p="$(readlink "$p")"
    case "$p" in /*) ;; *) p="$d/$p" ;; esac
  done
  d="$(cd "$(dirname "$p")" 2>/dev/null && pwd -P)" || { printf '%s\n' "$p"; return 0; }
  printf '%s/%s\n' "$d" "$(basename "$p")"
}

CACHED_REAL="$(resolve_path "$CACHED_NINJA")"
CACHED_VER="$("$CACHED_NINJA" --version 2>/dev/null | head -1)"
PATH_NINJA="$(command -v ninja 2>/dev/null || true)"
PATH_VER=""
PATH_REAL=""
[ -n "$PATH_NINJA" ] && PATH_REAL="$(resolve_path "$PATH_NINJA")"
[ -n "$PATH_NINJA" ] && PATH_VER="$("$PATH_NINJA" --version 2>/dev/null | head -1)"

say "   cached ninja : $CACHED_NINJA"
say "   version      : ${CACHED_VER:-<unknown>}"
if [ -z "$PATH_NINJA" ]; then
  say "   PATH ninja   : <none>"
elif [ "$PATH_REAL" = "$CACHED_REAL" ]; then
  say "   PATH ninja   : $PATH_NINJA (same program)"
elif [ "$PATH_VER" = "$CACHED_VER" ]; then
  say "   PATH ninja   : $PATH_NINJA (different path, SAME version — harmless)"
else
  say "   PATH ninja   : $PATH_NINJA  version ${PATH_VER:-<unknown>}"
fi
echo "-------------------------------------------------------------------"

TMP="$(mktemp -d "${TMPDIR:-/tmp}/igi-integrity.XXXXXX")" || exit 2
trap 'rm -rf "$TMP"' EXIT

PROBLEMS=0

# ── rule F: two ninja versions driving one build dir ─────────────────────────
if [ -n "$PATH_NINJA" ] && [ "$PATH_REAL" != "$CACHED_REAL" ] && [ "$PATH_VER" != "$CACHED_VER" ]; then
  PROBLEMS=$((PROBLEMS+1))
  warn "FAIL [NINJA_VERSION] the ninja CMake has cached and the ninja on PATH are different versions:"
  warn "       cached (drives cmake --build): $CACHED_NINJA  -> ${CACHED_VER:-<unknown>}"
  warn "       PATH   (bare \`ninja\` calls)  : $PATH_NINJA  -> ${PATH_VER:-<unknown>}"
  warn "       A build run by the PATH ninja discards this tree's dependency DB"
  warn "       (\"build log version is too old; starting over\"), which is how"
  warn "       objects end up with no dep record. Put the cached one first on PATH."
fi

# ── collect objects, PCHs, and the dep DB ────────────────────────────────────
: >"$TMP/need_stat.txt"
: >"$TMP/analysis.tsv"

PREFIXES=""
OBJ_TOTAL=0
for t in $TARGETS; do
  dir="$BUILD_DIR/CMakeFiles/$t.dir"
  PREFIXES="$PREFIXES CMakeFiles/$t.dir/"
  if [ ! -d "$dir" ]; then
    say "note: no such target dir: $dir (skipped)"
    continue
  fi
  gch="$dir/cmake_pch.hxx.gch"
  printf '%s\n' "$gch" >>"$TMP/need_stat.txt"
  while IFS= read -r obj; do
    [ -n "$obj" ] || continue
    rel="${obj#"$BUILD_DIR"/}"
    printf 'O\t%s\n' "$rel" >>"$TMP/analysis.tsv"
    printf 'P\t%s\t%s\n' "$rel" "$gch" >>"$TMP/analysis.tsv"
    printf '%s\n' "$obj" >>"$TMP/need_stat.txt"
    OBJ_TOTAL=$((OBJ_TOTAL+1))
  done < <(find "$dir" -name '*.obj' 2>/dev/null | sort)
done

# Alternation of the target dirs, for the dep-dump parser.  Strip the surrounding
# whitespace BEFORE joining, or the empty first alternative makes `^(|a|b)` match
# every target in the dump.
PREFIX_RE="^($(printf '%s' "$PREFIXES" | sed 's/^ *//; s/ *$//; s/ /|/g; s/\./\\./g'))"

# Which objects are compiled THROUGH the PCH?  Not all of them: 69 of the 352
# objects in this tree carry `FLAGS = -O3 -DNDEBUG -std=gnu++20` with no
# `-Winvalid-pch -include .../cmake_pch.hxx` and no `| cmake_pch.hxx.gch` implicit
# input, so they never read the PCH at all — the gch's mtime is irrelevant to them
# and they carry the full ~650-entry header depfile instead.  Rule A must be
# restricted to the PCH-compiled objects, or it reports every no-PCH object that
# happens to be older than the gch (measured: 20 false positives).
# Evidence: build-mingw/build.ninja
#   build CMakeFiles/igi-editor.dir/source/ai_events.cpp.obj: ... source/ai_events.cpp \
#       | CMakeFiles/igi-editor.dir/cmake_pch.hxx CMakeFiles/igi-editor.dir/cmake_pch.hxx.gch || ...
#   build CMakeFiles/igi-editor.dir/source/app_ui.cpp.obj: ... source/app_ui.cpp || ...
: >"$TMP/pch_objs.txt"
PCH_OBJ_TOTAL=0
PCH_LIST_KNOWN=0
if [ -f "$BUILD_DIR/build.ninja" ]; then
  sed -n 's/^build \(CMakeFiles\/[^:]*\.obj\):.*cmake_pch\.hxx\.gch.*/\1/p' \
    "$BUILD_DIR/build.ninja" | grep -E "$PREFIX_RE" | sort -u >"$TMP/pch_objs.txt"
  awk '{ printf "U\t%s\n", $0 }' "$TMP/pch_objs.txt" >>"$TMP/analysis.tsv"
  PCH_OBJ_TOTAL="$(wc -l <"$TMP/pch_objs.txt" | tr -d ' ')"
  PCH_LIST_KNOWN=1
else
  say "note: $BUILD_DIR/build.ninja is missing — rule A (STALE_PCH) cannot be"
  say "      restricted to PCH-compiled objects and is skipped"
fi

if [ "$OBJ_TOTAL" -eq 0 ]; then
  say "PASS: no .obj files under $BUILD_DIR/CMakeFiles/{$(printf '%s' "$TARGETS" | tr ' ' ',')}.dir — nothing built"
  [ "$PROBLEMS" -eq 0 ] || { warn "==> BUILD INTEGRITY: FAIL"; exit 1; }
  exit 0
fi

if ! "$CACHED_NINJA" -C "$BUILD_DIR" -t deps >"$TMP/deps.txt" 2>"$TMP/deps.err"; then
  err "error: \`$CACHED_NINJA -C $BUILD_DIR -t deps\` failed:"
  sed 's/^/       /' "$TMP/deps.err" >&2
  exit 2
fi

# `ninja -t deps` shape (one line per target, then indented absolute dep paths):
#   CMakeFiles/igi-editor.dir/source/app.cpp.obj: #deps 668, deps mtime 1793... (VALID)
#       /abs/path/source/app.cpp
#       /abs/path/source/app.h
awk -v re="$PREFIX_RE" '
  /^[^ ]/ {
    if (match($0, /: #deps [0-9]+, deps mtime [0-9]+ \((VALID|STALE|MISSING)\)/)) {
      tgt = substr($0, 1, RSTART-1)
      rest = substr($0, RSTART+2)
      n = rest; sub(/^#deps /, "", n); sub(/,.*/, "", n)
      mk = rest; sub(/.*\(/, "", mk); sub(/\).*/, "", mk)
      if (tgt ~ re) { cur = tgt; printf "R\t%s\t%s\t%s\n", tgt, n, mk } else { cur = "" }
    } else { cur = "" }
    next
  }
  /^[ ]/ && cur != "" { d = $0; sub(/^[ ]+/, "", d); printf "E\t%s\t%s\n", cur, d }
' "$TMP/deps.txt" >>"$TMP/analysis.tsv"

awk -F'\t' '$1=="E" { print $3 }' "$TMP/analysis.tsv" | sort -u >>"$TMP/need_stat.txt"

# One batched stat for every object, every PCH and every dep — never one stat
# per file (that would be ~230k process spawns).
sort -u "$TMP/need_stat.txt" \
  | xargs -n 200 stat -f '%Fm %N' 2>/dev/null \
  | awk '{ print $1 "\t" substr($0, length($1)+2) }' >"$TMP/mtimes.tsv"

MTIME_ROWS="$(wc -l <"$TMP/mtimes.tsv" | tr -d ' ')"
say "   objects      : $OBJ_TOTAL"
if [ "$PCH_LIST_KNOWN" = 1 ]; then
  say "   via PCH      : $PCH_OBJ_TOTAL  (rule A applies to these only)"
  say "   no PCH       : $((OBJ_TOTAL - PCH_OBJ_TOTAL))  (rule E applies; their depfile lists every header)"
else
  say "   via PCH      : unknown (build.ninja missing; rule A skipped)"
fi
say "   files stat'd : $MTIME_ROWS"
echo "-------------------------------------------------------------------"

awk -F'\t' -v bd="$BUILD_DIR" '
  # `ninja -t deps` keys targets RELATIVE to the build dir ("CMakeFiles/<t>.dir/x.obj")
  # but lists dependencies as ABSOLUTE paths, and stat gives absolute paths.  Index
  # every file under both keys, or the object lookup silently misses and rules A/E
  # become no-ops.
  NR == FNR {
    mt[$2] = $1 + 0
    if (index($2, bd "/") == 1) mt[substr($2, length(bd) + 2)] = $1 + 0
    next
  }
  $1 == "O" { allobj[$2] = 1; next }
  $1 == "U" { usespch[$2] = 1; next }
  $1 == "R" { nd[$2] = $3 + 0; mk[$2] = $4; next }
  $1 == "P" { gch[$2] = $3; next }
  $1 == "E" {
    o = $2; d = $3
    if (!(o in mt) || !(d in mt)) next
    if (mt[d] > mt[o]) {
      if (!(o in newdep) || mt[d] > mt[newdep[o]]) newdep[o] = d
      nbad[o]++
    }
    next
  }
  END {
    for (o in allobj) {
      if (!(o in nd))                      printf "NO_RECORD\t%s\t-\t0\n", o
      else if (nd[o] == 0)                 printf "EMPTY_DEPS\t%s\t-\t0\n", o
      else if (mk[o] != "VALID")           printf "DEPS_DESYNC\t%s\t%s\t0\n", o, mk[o]
      # Rule A applies ONLY to objects compiled through the PCH: for the no-PCH
      # objects the gch is not an input at all and its mtime means nothing.
      if ((o in usespch) && (o in gch) && (gch[o] in mt) && (o in mt) && mt[gch[o]] > mt[o])
        printf "STALE_PCH\t%s\t%s\t%.0f\n", o, gch[o], mt[gch[o]]
      if (o in nbad)
        printf "STALE_DEP\t%s\t%s\t%.0f\n", o, newdep[o], mt[newdep[o]]
    }
  }
' "$TMP/mtimes.tsv" "$TMP/analysis.tsv" | sort -k1,1 -k2,2 >"$TMP/verdicts.tsv"

fmt_time() { # ns -> local timestamp
  # NOTE: BSD `stat -f %Fm` is SECONDS with a fractional part (1793111290.867653905),
  # not nanoseconds — dividing by 1e9 here would print 1970-01-01 for every row.
  local ns="${1:-0}" secs
  secs="${ns%%.*}"
  case "$secs" in ''|*[!0-9]*) printf '?'; return ;; esac
  [ "$secs" -gt 0 ] 2>/dev/null || { printf '?'; return; }
  date -r "$secs" '+%Y-%m-%d %H:%M:%S' 2>/dev/null || printf '%s' "$ns"
}

# ── report ───────────────────────────────────────────────────────────────────
report_rule() {
  local rule="$1" label="$2" detail="$3"
  local n
  n="$(awk -F'\t' -v r="$rule" '$1==r' "$TMP/verdicts.tsv" | wc -l | tr -d ' ')"
  [ "$n" -gt 0 ] || return 0
  PROBLEMS=$((PROBLEMS + n))
  warn ""
  warn "FAIL [$rule] $label — $n object(s)"
  awk -F'\t' -v r="$rule" '$1==r' "$TMP/verdicts.tsv" | while IFS=$'\t' read -r _ o extra _ts; do
    case "$rule" in
      STALE_PCH)   warn "       $o  is older than its PCH $extra ($(fmt_time "$_ts"))" ;;
      STALE_DEP)   warn "       $o  older than dependency $extra ($(fmt_time "$_ts"))" ;;
      DEPS_DESYNC) warn "       $o  dep record is $extra, not VALID" ;;
      *)           warn "       $o" ;;
    esac
  done
  warn "       ($detail)"
}

report_rule STALE_PCH   "object older than its own cmake_pch.hxx.gch" \
                        "CMake compiles every TU through that PCH; the object cannot predate it"
report_rule NO_RECORD   "object has NO entry in the ninja dependency log" \
                        "ninja -t deps lists nothing for it, so no header can ever invalidate it"
report_rule EMPTY_DEPS  "object has '#deps 0' in the ninja dependency log" \
                        "THE EXACT SIGNATURE OF THE SHIPPED BUG: ninja believes it includes no headers"
report_rule DEPS_DESYNC "object's dep record is out of sync with the artifact" \
                        "ninja reports STALE/MISSING instead of VALID: the log does not match the file"
report_rule STALE_DEP   "object older than a header it actually includes" \
                        "its real dependency list (from ninja's dep DB) contains a newer file"

# Coarse fallback, only for objects whose dep record is already broken: without a
# record we have no include set, so "older than the newest header under source/"
# is the strongest available statement — and those objects are already a FAIL.
if grep -qE '^(NO_RECORD|EMPTY_DEPS)\b' "$TMP/verdicts.tsv" 2>/dev/null; then
  NEWEST_HDR="$(find "$REPO_ROOT/source" "$REPO_ROOT/tests" \
                     \( -name '*.h' -o -name '*.hpp' \) 2>/dev/null \
                  | xargs -n 200 stat -f '%Fm %N' 2>/dev/null | sort -rn | head -1 || true)"
  if [ -n "$NEWEST_HDR" ]; then
    warn ""
    warn "note: newest header under source/ + tests/ is"
    warn "      $(printf '%s' "$NEWEST_HDR" | awk '{ print substr($0, length($1)+2) }')"
    warn "      (objects without a dep record cannot be proven fresh against it)"
  fi
fi

echo "==================================================================="
if [ "$PROBLEMS" -eq 0 ]; then
  echo " BUILD INTEGRITY: PASS — all $OBJ_TOTAL objects are consistent with their"
  echo "                  dependencies, their PCH, and the ninja dependency log."
  echo "==================================================================="
  exit 0
fi

warn " BUILD INTEGRITY: FAIL — $PROBLEMS problem(s) in $BUILD_DIR"
warn " The tree is NOT internally consistent. A gate run on it may validate a"
warn " mixed-layout binary whose stale objects disagree about struct offsets."
warn "==================================================================="
warn ""
warn " RECOVERY — force a full rebuild of both targets' objects and their PCHs:"
warn ""
warn "   find $BUILD_DIR/CMakeFiles/igi-editor.dir \\"
warn "        $BUILD_DIR/CMakeFiles/igi_tests.dir -name '*.obj' -delete"
warn "   rm -f $BUILD_DIR/CMakeFiles/igi-editor.dir/cmake_pch.hxx.gch \\"
warn "         $BUILD_DIR/CMakeFiles/igi_tests.dir/cmake_pch.hxx.gch"
warn "   cmake --build $BUILD_DIR --target igi_tests igi-editor --parallel"
warn ""
warn " (or re-run this script with --fix to do exactly that)"
warn ""

if [ "$FIX" = 1 ]; then
  warn "==> --fix: deleting objects and PCHs, then rebuilding"
  find "$BUILD_DIR/CMakeFiles/igi-editor.dir" \
       "$BUILD_DIR/CMakeFiles/igi_tests.dir" -name '*.obj' -delete 2>/dev/null
  rm -f "$BUILD_DIR/CMakeFiles/igi-editor.dir/cmake_pch.hxx.gch" \
        "$BUILD_DIR/CMakeFiles/igi_tests.dir/cmake_pch.hxx.gch"
  cmake --build "$BUILD_DIR" --target igi_tests igi-editor --parallel
  rc=$?
  warn "==> --fix: cmake --build exited $rc"
  exit "$rc"
fi

exit 1
