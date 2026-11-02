#!/usr/bin/env bash
# ============================================================================
# scripts/lib/retail-stage.sh — shared staging helpers for the lanes that RUN
#                               the editor against the retail install.
#
# SOURCED, not executed.  Used by:
#   scripts/e2e-headless.sh     (the 14-level headless acceptance lane)
#   scripts/run-wine-smoke.sh   (the i686 link + Wine smoke lane)
#
# ============================================================================
# WHY THIS LIBRARY EXISTS (read before changing any of it)
# ============================================================================
# Both lanes used to build their "writable copy" of the retail install out of
# SYMLINKS back into <IGI>, and e2e-headless.sh then printed
#
#     game copy   : ... (46 symlinks + a real editor/qed)
#                   the retail tree is never written to
#
# which was FALSE, and the falsehood is what corrupted the retail menu QVMs
# (twice).  Two code paths write into the game tree:
#
#   * Config::Init recompiles every <game>/editor/qed/*.qsc into a sibling .qvm
#     at EVERY start -- source/config.cpp:20-27 (GetQEDDirectory prefers the
#     GAME tree) and source/config.cpp:216-236.
#   * The menu editor writes the menu back to the file it was read from --
#     source/app_menu.cpp:806, SaveEditorMenu -> MenuQvmWriteFile.  For the
#     pause menu that file is <game>/menusystem/ingamemenu.qvm
#     (source/menu/menu_ui.h:419, kRetailMenuRelativePath).
#
# With `menusystem/` and the root `config.qvm` / `igi.cfg` symlinked, both of
# those wrote straight through to the retail install.
#
# Two copies of the fix would drift apart, so there is one library and both
# lanes source it.
#
# ============================================================================
# WHAT IT PROVIDES
# ============================================================================
#   stage_retail_tree <retail-src> <stage-dst>
#       A REAL copy of the retail tree.  On APFS `cp -Rc` uses clonefile(2), so
#       the copy is copy-on-write and costs no disk for unchanged blocks:
#       measured 3.6 s for this host's 1.5 GB / 15 927-file tree.
#
#   overlay_repo_shaders <repo-root> <stage-editor-dir>
#       Replaces <stage-editor-dir>/shaders/<tier> with the repository's
#       shaders/<tier>, and reports any file it had to correct.  The editor
#       loads its shaders from <exeDir>/editor/shaders (source/common.cpp:907),
#       NOT from the repo, so a run can otherwise execute a stale copy -- which
#       is exactly how the terrain came to render fully fogged after the fog
#       uniform was renamed (commit 33368d7c): the C++ and shaders/ were
#       updated, assets/editor/shaders (what the build copies) was not.
#
#   retail_manifest <dir> <out-file>
#       mtime + size + path for every file under <dir>, sorted.
#
#   retail_manifest_report <before> <after>
#       Prints what changed.  Exit 0 when nothing did.
# ============================================================================

# Guard against double-sourcing.
[ -n "${IGI_RETAIL_STAGE_LIB:-}" ] && return 0
IGI_RETAIL_STAGE_LIB=1

# ── retail tree manifest ────────────────────────────────────────────────────
#
# mtime is the field that matters: the editor rewrites .qvm files in place, so a
# size check alone would miss a same-length rewrite.  Size is included because
# it is free and makes a truncation obvious.
#
# `.DS_Store` is excluded: Finder writes it when a folder is browsed, it is
# desktop metadata rather than game data, and including it would make this
# assertion fire for reasons that have nothing to do with the editor.
retail_manifest() {
  local dir="$1" out="$2"
  : >"$out"
  [ -d "$dir" ] || return 0
  find "$dir" -type f ! -name '.DS_Store' -print0 2>/dev/null \
    | xargs -0 -n 200 stat -f '%Fm %z %N' 2>/dev/null \
    | LC_ALL=C sort >"$out"
  return 0
}

# retail_manifest_report <before> <after>
#   Exit 0 and print nothing when the two manifests are identical; otherwise
#   print the first 40 changed lines and exit 1.
retail_manifest_report() {
  local before="$1" after="$2"
  if cmp -s "$before" "$after"; then
    return 0
  fi
  printf '%s\n' "       the retail tree CHANGED during the run:"
  diff "$before" "$after" 2>/dev/null | sed -n '1,40p' | sed 's/^/         /'
  local n
  n="$(diff "$before" "$after" 2>/dev/null | grep -c '^[<>]' || true)"
  printf '%s\n' "       ($n differing line(s); format is 'mtime size path')"
  return 1
}

# ── the writable copy ───────────────────────────────────────────────────────
#
# stage_retail_tree <retail-src> <stage-dst>
#   Makes <stage-dst> a REAL, write-isolated copy of <retail-src>.
#
#   `-c` asks cp for clonefile(2).  On a filesystem without clone support cp
#   falls back to copyfile(2) by itself, and if the option is not understood at
#   all (a non-macOS host) the whole thing is retried as a plain recursive copy,
#   so the result is a real copy either way.  It is NEVER a symlink farm: that
#   is the property this function exists to guarantee.
stage_retail_tree() {
  local src="$1" dst="$2"
  if [ ! -d "$src" ]; then
    printf '%s\n' "error: stage_retail_tree: no such retail tree: $src" >&2
    return 2
  fi
  rm -rf "$dst"
  mkdir -p "$dst"
  if cp -Rc "$src"/. "$dst"/ 2>/dev/null; then
    return 0
  fi
  # -c unavailable or clonefile failed part-way: start over with a plain copy.
  rm -rf "$dst"
  mkdir -p "$dst"
  cp -R "$src"/. "$dst"/ || return 1
  return 0
}

# ── the shaders the editor will actually load ───────────────────────────────
#
# overlay_repo_shaders <repo-root> <stage-editor-dir>
#   <stage-editor-dir> is the `editor/` directory NEXT TO THE EXE, because that
#   is where source/common.cpp:907 looks for shaders.
#
#   Prints one line per tier: `staged (N file(s) corrected)` when the staged
#   copy disagreed with the repository, or `staged (identical)`.  A non-zero
#   correction count is a real signal -- it means the build output or the
#   shipped assets/editor/shaders copy is stale -- so it is reported, not
#   hidden.  The overlay itself is unconditional: a run must never execute a
#   stale shader.
#
#   Returns 1 if the repository has no shader tier directory at all.
overlay_repo_shaders() {
  local repo="$1" editor_dir="$2"
  local found=0
  [ -d "$repo/shaders" ] || return 1
  mkdir -p "$editor_dir/shaders"
  local tier src dst corrected
  for src in "$repo"/shaders/*/; do
    [ -d "$src" ] || continue
    found=1
    tier="$(basename "$src")"
    dst="$editor_dir/shaders/$tier"
    corrected=0
    if [ -d "$dst" ]; then
      corrected="$(diff -rq "$src" "$dst" 2>/dev/null | wc -l | tr -d ' ')"
    fi
    rm -rf "$dst"
    cp -R "$src" "$dst" || return 1
    if [ "$corrected" = "0" ]; then
      printf '%s\n' "   shaders/$tier : staged (identical to the repository)"
    else
      printf '%s\n' "   shaders/$tier : staged ($corrected file(s) corrected from a stale copy)"
    fi
  done
  [ "$found" = "1" ] || return 1
  return 0
}

# assert_repo_shaders_staged <repo-root> <stage-editor-dir>
#   Re-reads what is on disk and proves the overlay took: every tier the
#   repository has must be byte-identical in the stage.  A copy that silently
#   did nothing would otherwise look like a pass.
assert_repo_shaders_staged() {
  local repo="$1" editor_dir="$2"
  local rc=0 src tier dst
  for src in "$repo"/shaders/*/; do
    [ -d "$src" ] || continue
    tier="$(basename "$src")"
    dst="$editor_dir/shaders/$tier"
    if [ ! -d "$dst" ]; then
      printf '%s\n' "       shaders/$tier is missing from the staged editor directory"
      rc=1
      continue
    fi
    if ! diff -rq "$src" "$dst" >/dev/null 2>&1; then
      printf '%s\n' "       shaders/$tier in the stage still differs from the repository:"
      diff -rq "$src" "$dst" 2>/dev/null | sed -n '1,10p' | sed 's/^/         /'
      rc=1
    fi
  done
  return $rc
}
