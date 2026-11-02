#!/usr/bin/env bash
# ============================================================================
# check-retail-stage.sh — self-test for scripts/lib/retail-stage.sh
# ============================================================================
#
# WHY THIS EXISTS
#   scripts/lib/retail-stage.sh is what stands between the editor and the retail
#   install.  The lanes used to build their "writable copy" out of symlinks back
#   into <IGI> while printing "the retail tree is never written to", and the
#   retail menu QVMs were corrupted twice as a result.  The library now makes a
#   REAL copy, overlays the repository's shaders, and snapshots the retail
#   tree's mtimes so a change is a failure.
#
#   Those are claims about shell code, and shell code that is only exercised by
#   a 1-2 minute Wine run is shell code nobody checks.  This script proves each
#   claim against a synthetic fixture in a temp directory: no Wine, no retail
#   install, no network, under a second.  It is the guard on the guard, in the
#   same spirit as tools/host-tests/host_lane_selftest.py.
#
#   It is wired into scripts/e2e-headless.sh as a preflight step.
#
# USAGE
#   bash scripts/check-retail-stage.sh      # exit 0 = the library behaves
# ============================================================================
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=lib/retail-stage.sh
. "$REPO_ROOT/scripts/lib/retail-stage.sh"

TMP="$(mktemp -d "${TMPDIR:-/tmp}/igi-retail-stage.XXXXXX")" || exit 2
trap 'rm -rf "$TMP"' EXIT

FAILURES=0
CHECKS=0

check() { # check <condition-as-0/1> <label> [detail]
  CHECKS=$((CHECKS + 1))
  if [ "$1" -eq 0 ]; then
    printf '   PASS  %s\n' "$2"
  else
    printf '   FAIL  %s\n' "$2"
    [ $# -ge 3 ] && printf '%s\n' "$3" | sed 's/^/         /'
    FAILURES=$((FAILURES + 1))
  fi
}

hr() { printf '%s\n' "-------------------------------------------------------------------"; }

printf '%s\n' "==================================================================="
printf '%s\n' " check-retail-stage.sh — the staging library's own behaviour"
printf '%s\n' "==================================================================="

# ── fixture: a miniature retail install ─────────────────────────────────────
# The paths are the ones the editor actually writes: the root config, the menu
# container, the QED script directory and the cache.
RETAIL="$TMP/retail"
mkdir -p "$RETAIL/menusystem" "$RETAIL/editor/qed" "$RETAIL/editor/shaders/41" "$RETAIL/cache/anims" "$RETAIL/MISSIONS/location0"
printf 'config-v1\n'                >"$RETAIL/config.qvm"
printf 'menu-v1\n'                  >"$RETAIL/menusystem/ingamemenu.qvm"
printf 'QEDLogs(FALSE);\n'          >"$RETAIL/editor/qed/qedconfig.qsc"
printf 'old-shader\n'               >"$RETAIL/editor/shaders/41/terrain_fog.frag"
printf 'anim\n'                     >"$RETAIL/cache/anims/a.bin"
printf 'level\n'                    >"$RETAIL/MISSIONS/location0/objects.qvm"
printf 'exe\n'                      >"$RETAIL/igi.exe"

# ── 1. stage_retail_tree produces a REAL, write-isolated copy ───────────────
printf '\n%s\n' "1. stage_retail_tree makes a real copy, not a symlink farm"
hr
STAGE="$TMP/stage"
stage_retail_tree "$RETAIL" "$STAGE"
rc=$?
check $rc "stage_retail_tree returns 0" "exit=$rc"

LINKS="$(find "$STAGE" -type l | wc -l | tr -d ' ')"
check "$([ "$LINKS" = "0" ] && echo 0 || echo 1)" \
      "the stage contains no symlink at all (found $LINKS)"

MISSING=""
for rel in config.qvm menusystem/ingamemenu.qvm editor/qed/qedconfig.qsc \
           editor/shaders/41/terrain_fog.frag cache/anims/a.bin \
           MISSIONS/location0/objects.qvm igi.exe; do
  [ -f "$STAGE/$rel" ] || MISSING="$MISSING $rel"
done
check "$([ -z "$MISSING" ] && echo 0 || echo 1)" \
      "every retail file is present in the stage" "missing:$MISSING"

# The property that makes this safe: writing the STAGE copy must not touch the
# source.  This is what clonefile(2) buys, and it is the whole point.
RETAIL_HASH_BEFORE="$(find "$RETAIL" -type f -exec shasum -a 256 {} \; | LC_ALL=C sort)"
printf 'config-v2-OVERWRITTEN-BY-THE-EDITOR\n' >"$STAGE/config.qvm"
printf 'menu-v2-OVERWRITTEN-BY-THE-EDITOR\n'   >"$STAGE/menusystem/ingamemenu.qvm"
printf 'QEDLogs(TRUE);\n'                      >"$STAGE/editor/qed/qedconfig.qsc"
printf 'new-qvm\n'                             >"$STAGE/editor/qed/qedconfig.qvm"
RETAIL_HASH_AFTER="$(find "$RETAIL" -type f -exec shasum -a 256 {} \; | LC_ALL=C sort)"
check "$([ "$RETAIL_HASH_BEFORE" = "$RETAIL_HASH_AFTER" ] && echo 0 || echo 1)" \
      "writing the stage copies did NOT change the source tree" \
      "$(diff <(printf '%s\n' "$RETAIL_HASH_BEFORE") <(printf '%s\n' "$RETAIL_HASH_AFTER") | head -6)"
check "$([ "$(cat "$STAGE/config.qvm")" != "$(cat "$RETAIL/config.qvm")" ] && echo 0 || echo 1)" \
      "the stage copy really did change (the write was not a no-op)"
check "$([ ! -e "$RETAIL/editor/qed/qedconfig.qvm" ] && echo 0 || echo 1)" \
      "a .qvm the editor would create in the stage is NOT created in the retail tree"

# ── 2. retail_manifest detects a changed file ───────────────────────────────
printf '\n%s\n' "2. retail_manifest detects a change to the retail tree"
hr
retail_manifest "$RETAIL" "$TMP/before.manifest"
n_before="$(wc -l <"$TMP/before.manifest" | tr -d ' ')"
check "$([ "$n_before" -ge 7 ] && echo 0 || echo 1)" \
      "the manifest records every file ($n_before of 7+)"
retail_manifest "$RETAIL" "$TMP/same.manifest"
retail_manifest_report "$TMP/before.manifest" "$TMP/same.manifest" >"$TMP/same.out" 2>&1
check $? "an unchanged tree reports no change"

# The exact incident: the editor rewrites the menu container in place.
printf 'menu-CORRUPTED\n' >"$RETAIL/menusystem/ingamemenu.qvm"
retail_manifest "$RETAIL" "$TMP/after.manifest"
retail_manifest_report "$TMP/before.manifest" "$TMP/after.manifest" >"$TMP/changed.out" 2>&1
rc=$?
check "$([ "$rc" -ne 0 ] && echo 0 || echo 1)" \
      "a changed retail file is reported as a change (exit $rc)"
check "$(grep -q 'ingamemenu.qvm' "$TMP/changed.out" && echo 0 || echo 1)" \
      "the changed file is NAMED in the report" "$(cat "$TMP/changed.out")"

# mtime-only: same size, same bytes, new mtime.  This is why mtime is in the
# manifest and a size check alone would not do.
retail_manifest "$RETAIL" "$TMP/pre-touch.manifest"
touch "$RETAIL/MISSIONS/location0/objects.qvm"
retail_manifest "$RETAIL" "$TMP/post-touch.manifest"
retail_manifest_report "$TMP/pre-touch.manifest" "$TMP/post-touch.manifest" >/dev/null 2>&1
check "$([ $? -ne 0 ] && echo 0 || echo 1)" \
      "an mtime-only change (same size, same bytes) is still detected"

# ── 3. overlay_repo_shaders corrects a stale tier ───────────────────────────
printf '\n%s\n' "3. overlay_repo_shaders replaces a stale staged tier"
hr
REPO="$TMP/repo"
mkdir -p "$REPO/shaders/21" "$REPO/shaders/41" "$REPO/shaders/45"
printf 'new-fog-g_fog_scale\n' >"$REPO/shaders/41/terrain_fog.frag"
printf 'new-fog-vert\n'        >"$REPO/shaders/41/terrain_fog.vert"
printf 'tier21\n'              >"$REPO/shaders/21/terrain_fog.frag"
printf 'tier45\n'              >"$REPO/shaders/45/terrain_fog.frag"

EDITOR="$TMP/editor"
mkdir -p "$EDITOR/shaders/41"
# The stale copy is the incident: the same filename, the OLD uniform name.
printf 'old-fog-g_fog_far\n' >"$EDITOR/shaders/41/terrain_fog.frag"
printf 'new-fog-vert\n'      >"$EDITOR/shaders/41/terrain_fog.vert"

overlay_repo_shaders "$REPO" "$EDITOR" >"$TMP/overlay.out" 2>&1
rc=$?
check $rc "overlay_repo_shaders returns 0" "$(cat "$TMP/overlay.out")"
check "$(grep -q '1 file(s) corrected' "$TMP/overlay.out" && echo 0 || echo 1)" \
      "the stale file is reported as corrected" "$(cat "$TMP/overlay.out")"
check "$(grep -q 'shaders/21 : staged (identical' "$TMP/overlay.out" && echo 0 || echo 1)" \
      "a tier with nothing stale is reported as identical" "$(cat "$TMP/overlay.out")"
check "$([ "$(cat "$EDITOR/shaders/41/terrain_fog.frag")" = "new-fog-g_fog_scale" ] && echo 0 || echo 1)" \
      "the staged file now holds the repository's content"
check "$([ -f "$EDITOR/shaders/21/terrain_fog.frag" ] && echo 0 || echo 1)" \
      "a tier absent from the stage is created"

assert_repo_shaders_staged "$REPO" "$EDITOR" >/dev/null 2>&1
check $? "assert_repo_shaders_staged passes after the overlay"

# And it must FAIL when the stage is not what the repository has -- otherwise it
# is not an assertion.
printf 'drifted-again\n' >"$EDITOR/shaders/41/terrain_fog.frag"
assert_repo_shaders_staged "$REPO" "$EDITOR" >"$TMP/assert.out" 2>&1
check "$([ $? -ne 0 ] && echo 0 || echo 1)" \
      "assert_repo_shaders_staged fails on a drifted stage" "$(cat "$TMP/assert.out")"
check "$(grep -q 'shaders/41' "$TMP/assert.out" && echo 0 || echo 1)" \
      "the drifted tier is named" "$(cat "$TMP/assert.out")"

# ── 4. a missing retail tree is an error, not a silent success ──────────────
printf '\n%s\n' "4. failure modes are loud"
hr
stage_retail_tree "$TMP/does-not-exist" "$TMP/nowhere" >/dev/null 2>&1
check "$([ $? -ne 0 ] && echo 0 || echo 1)" \
      "stage_retail_tree fails on a missing source"
overlay_repo_shaders "$TMP/no-such-repo" "$TMP/editor2" >/dev/null 2>&1
check "$([ $? -ne 0 ] && echo 0 || echo 1)" \
      "overlay_repo_shaders fails when the repository has no shaders/"

printf '%s\n' "==================================================================="
if [ "$FAILURES" -eq 0 ]; then
  printf '%s\n' " RETAIL-STAGE SELF-TEST: PASS — $CHECKS check(s)"
  printf '%s\n' "   the stage is a real copy, writes to it cannot reach the"
  printf '%s\n' "   source, an in-place rewrite of the retail tree is detected,"
  printf '%s\n' "   and the staged shaders are proven to be the repo's."
  printf '%s\n' "==================================================================="
  exit 0
fi
printf '%s\n' " RETAIL-STAGE SELF-TEST: FAIL — $FAILURES of $CHECKS check(s) failed"
printf '%s\n' "==================================================================="
exit 1
