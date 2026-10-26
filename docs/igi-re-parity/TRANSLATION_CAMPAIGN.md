# IGI-RE translation campaign — checkpoint

Directive (user): IGI-RE is now the actual decompiled source of the game, so the editor
must be a translation of it. Copy the engine's C, convert to C++, wire it in, build, and
commit small commits per module (MEF, QVM, QSC, …). No new tests — the engine source is
already well tested. No invented logic, no research/open-igi-derived behaviour.

Worktree: `/Users/haseeb-mir/Documents/Code/project-igi-editor-igi-re`
Oracle:   `/Users/haseeb-mir/Documents/Code/IGI-RE`
Never touch `/Users/haseeb-mir/Documents/Code/project-igi-editor` (original checkout).

## Module status (all work is UNCOMMITTED as of this checkpoint)

| Module | Files | State | Evidence |
| --- | --- | --- | --- |
| QVM | `level/qvm_parser.{h,cpp}`, `qvm_decompiler.cpp`, `menu/qvm_image.{h,cpp}` | done, compiles | 1043/1043 retail `.qvm` parse, digest identical to an independent decoder |
| QSC | `level/qsc_lexer.cpp`, `level_common.cpp` | done, compiles | retail `objects.qsc` lex+parse OK (42828/30296/26965 tokens) |
| MEF loader | `renderer/mef_native.{h,cpp}` | done, compiles | 4197 retail members: 0 differences vs an independent Python transcription |
| Pickups (#75) | `runtime/pickup_rules.{h,cpp}`, `renderer_objects_visual.cpp` | done, compiles | 2408/2408 retail MEFs yield a positive radius |
| Snow/rain (#57/#58) | `renderer_rain.{h,cpp}`, `runtime/rain_rules.{h,cpp}` | done, compiles | GLSL 1.20 pair compiles+links on the real GL 2.1 context; 48 rain tests pass |
| LOD (#62) | `lod/lod_select.h` (new), `lod_chain.h`, `lod_qvm_table.h`, `renderer_objects_mesh.cpp` | in flight | — |
| Animation (#73) | `runtime/anim_player.{h,cpp}` (new), `anim_rules.{h,cpp}` | in flight | — |
| Menu write-back (#74) | `menu/menu_qvm_writer.{h,cpp}` (new) | in flight | needs `MenuDefinition::declarations` (Lead) |
| Splines/roads (#79/#69) | — | not started | 2 subagents died without output |
| MTP/DAT + RES, GRAPHS, LEVEL/terrain | — | not started | queued behind the child cap |

## Gate state

- **i686 compile gate: green** (`scripts/build-windows.sh --arch i686`, exit 0).
- **Host lane: 4 suites / 17 tests failing** — every failure is a test that encoded
  pre-engine behaviour, not a source regression:
  - `test_mef_dual_set` (4): fixture `MakeHsem` never wrote the version word at +0.
    Engine `Mesh_Load` HOOK(0x4CF390), `src/model/model.c:367` refuses anything but
    `0x3DCCCCCD` (0.1f), and all 6190 retail members carry it. **Fixed**: the fixture now
    writes `PutF32(d, 0, 0.1f)`. The reader was right.
  - `test_qsc_lexer` (6) + `test_qsc_parser` (2) + `test_mcp_domain_tools` (5): assert
    block comments / hex literals / malformed-float behaviour the engine does not have.
    The engine's reader handles only `//` (`src/qsc/reader.c:104`) and has no block-comment
    path anywhere. Delegated to a reconciliation subagent.
  - Lead already repaired two other test files the QVM header rewrite broke:
    `test_vanilla_fixture_parity.cpp` (`version`/`unk08`) and `test_menu_qvm.cpp`
    (`code`/`codeSize`) — the old `ver_major`/`ver_minor` and `of_ctable`/`sz_ctable`
    names were editor inventions over the engine's single words.

## Outstanding hooks the Lead owns (agents may not edit these files)

1. **snow.tex** (fxobj/rain.c:82, bound rain.c:430-434): `Renderer_Objects::GetOrLoadTexture`
   and `objects_` are private, so `Renderer_Rain` cannot reach the game's `.tex` decode.
   Until bound, snow renders as the engine's own null-texture branch (opaque white quads).
2. **Pickup grant** (`app.cpp:2729-2790`): the engine grants on proximity via the touch
   handlers; the editor grants on an explicit use action. Should call
   `igi::PickupTouchesFromModel(...)`.
3. **Invented 90° Z rotation** on weapon/pickup objects: `renderer_objects.cpp:1022-1025`,
   `renderer_objects_picking.cpp:218-220`, `:432-434`. The engine applies only the authored
   matrix (`Direct3DRender_DrawRigidMesh` 0x49DF60, `render/render.c:965-966`); the ±90° Y
   turn exists only in the engine-dropped path `GunPickup_New` 0x46C980 (pickup.c:423-429).
4. **`MenuDefinition::declarations`**: hoist the reader's local `TypeDecl` so the menu writer
   can round-trip retail byte-for-byte instead of re-deriving declaration order.
5. Stale docs: `docs/igi-re-parity/PICKUPS.md` still says "not wired".

## Corrections to earlier documents (found during this pass)

- The engine **does** unescape at the token stage — `Lex_ReadToken` HOOK(0x517260),
  `src/terrdraw/lexer.c:23-137` maps `\b \n \r \t \xNN` and drops the backslash for any
  other escape. "The engine does not unescape" holds only for `Reader_ReadLine`.
- `//` **is** cut inside strings (`src/qsc/reader.c:88`), contrary to the earlier claim.
- The D3DR field meaning is format-dependent and `unk14` is the **index** stream in format 0
  but the **vertex** stream in format 3 (`src/bonemesh/bonemesh.h:18-20`). The editor's
  reader was already format-correct here, so issue #47's stated root cause stays disproved.
- The engine loads **both** collision parts unconditionally and treats a missing CMSH as a
  clean `coll = NULL` return, not an error (`src/bonemesh/d3drender.c:245-260`).
- An unsupported mesh format is **fatal** in the engine (`Error_Show` + `for(;;)`), so the
  editor must not invent a fallback for one.
