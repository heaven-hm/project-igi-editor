# Issue Fixes and Safe Release Notes

This file is the single issue-history file promoted to `develop`. Keep future
bug fixes here with the symptom, root cause, implementation, verification, and
remaining release gates. Do not record a bug as fully fixed from a focused unit
test alone.

## IGIED-FOREIGN-MODEL-TEXTURES-001

Status: fixed and verified (import path). Release binary deployed to `D:\IGI1`.

### Symptom

Importing a model from another level could bind same-named textures from the
destination or an unrelated level. Character parts could therefore receive the
wrong appearance (notably Sniper `001_02_1`), and an import could report
success without a complete model/DAT/MTP publication.

### Root cause

Model IDs and texture IDs are not globally unique asset identities. The old
resolver used loose fallbacks and destination state, so it could select the
wrong level-local mesh or texture bundle. Two concrete defects:

1. The `001_02_1` forensics: level `.res` archives are polluted with
   same-name entries from other levels, while the authoritative bytes live in
   the shared `common` archive (`missions/location0/common/.../location0.res`).
   `FindTextureDataFromLevel` / `FindMeshDataFromLevel` searched only the level
   archive, so a lookup for a texture that exists only in `common` missed its
   own source level and fell through to same-name bytes from an unrelated
   level's archive.
2. Session staleness: after publishing an import, `texture_sources_` still held
   the stale pre-import mapping, so a later import in the same session resolved
   the model to the wrong level's archive. Existing RES entries also were not
   replaced when their bytes differed.

3. Import skip on natively-present destinations: `CommitPropTextEdit` called
   `ModelSourceRequiresImport` first, so committing a model field whose model
   already exists in the destination inventory (e.g. `001_02_1` is native to
   level 12) silently skipped the import — which is why levels 1/2/9 worked
   while level 12 appeared to do nothing. An explicit Enter on the model field
   is now always an import request; the staged replace path skips
   byte-identical entries, so refreshing a natively-present model is a safe
   no-op when bytes already match. If the import fails but the model exists
   natively, the commit falls back to the reference change instead of flagging
   the model as missing.

### Resolution

- Track exact `(model ID, source level)` provenance through the picker and CLI.
- Resolve ordered DAT texture mappings from the selected source level.
- Load mesh and texture bytes from that same source level only.
- Reject an untagged import when candidate source bundles differ in mapping,
  mesh bytes, or texture bytes; require explicit source provenance.
- Validate MEF material slots against the ordered mapping before publication.
- Stage and validate model RES, texture RES, DAT, and regenerated MTP output,
  then publish them together with rollback backups.
- Replace differing same-name RES entries instead of silently retaining stale
  destination bytes.
- Resolve cross-family ATTA dependencies recursively before publication.
- Make the E2E harness fail on an unobserved or nonzero importer exit and enforce
  the required 30 MB process-memory gate.
- Include the shared `common` archive in the source bundle: `FindTextureDataFromLevel`
  / `FindMeshDataFromLevel` now resolve `levelN.res` first, then
  `common/.../location0.res`, via the shared `FindModelSourceEntry` helper
  (level wins on name collisions; format-suffix fallback stays per-archive).
- Upsert `texture_sources_` (plus `texture_level_map_` and texture-cache
  invalidation) immediately after publishing, so later imports in the same
  session resolve the just-published destination bundle.
- Dedupe the model picker to one row per model ID (current level wins,
  otherwise smallest owning level), with direct Up/Down navigation and Enter
  committing the highlighted row's deterministic source level.

### Verification

- `igi_tests`: 13/13 `ModelTextureResolution.*` tests passed, including the two
  synthetic-`.res` integration tests (`LevelMissingFallsBackToCommonArchive`,
  `LevelArchiveWinsOverCommonArchive`) and the picker tests (dedupe,
  nav-preview, enter-commit, click).
- Release x86 build of `igi_tests` and `igi1ed.exe` passed.
- Disposable-copy explicit-source import of Sniper `001_02_1` (source level 2)
  passed into every destination level 1–5: matching model bytes, 13/13 matching
  texture bytes, and ordered DAT mapping equality per level
  (`D:\e2e-ai-swap-L1..L5-artifacts\report.json`, all `status=PASS`, exit 0).
- Disposable fresh-import control (`006_01_1`, level2-only → level1) passed with
  `destinationFilesChanged: [level1.dat, level1.mtp, models-level1.res,
  textures-level1.res]`.
- Disposable automatic-source negative control correctly rejected the divergent
  `001_02_1` bundle as ambiguous with no destination mutation; the installed
  `D:\IGI1` corpus was not modified during E2E.
- Level 12 regression (import looked like a no-op): disposable-copy explicit
  import of `001_02_1` (source level 2 → destination level 12) passed after the
  always-import fix — `status=PASS`, exit 0, 0 texture mismatches, and
  `destinationFilesChanged: []` (level 12's archives already carry
  byte-identical content, so the safe refresh was a no-op)
  (`D:\e2e-l12-alwaysimport2-artifacts\report.json`).

### Release gate still required

The full test executable reports pre-existing corpus-gated failures
(`DatParser/GraphParser/ResParser/TexParser FileExists*` — the checkout lacks
the installed mission corpus under `bin\Release\missions`), unrelated to this
change. The live picker scenario harness separately needs a UI-targeting fix
(it applied `463_03_1` instead of the filtered `003_02_1`).

## IGIED-EULER-ORDER-001 (camera/object orientation)

Status: fixed, unit-verified against a transcription of the engine's matrix code;
not verified in a running Windows editor.

### Symptom

Camera orientations and generic object rotations in the viewport did not match the
angles the game uses for the same authored values (`docs/KNOWN_ISSUES.md` §1
"Camera Orientation").

### Root cause

The editor composed Euler rotations in a different order from the engine. The
decompiled `Matrix_SetEuler` (`IGI-RE src/math/matrix.c:99-110`, `HOOK 0x4B38E0`)
computes `R = Rz(az) · Ry(ay) · Rx(ax)` on row-major 3x3 matrices, with
`Matrix_Mul(out, a, b)` meaning `a · b`, and `ParamReal32x9_Parse` (`HOOK 0x4EFBA0`)
passes the QSC values through unchanged.

### Implementation

`source/renderer/igi_transform.h` provides `igi::IgiEulerRotationMatrix` (`Rz·Ry·Rx`),
`IgiEulerRotationMat3`, `IgiObjectMatrix` and — for the inverse direction —
`IgiMatrixToEuler`, a transcription of the engine's own `Matrix_GetEuler`
(`IGI-RE src/math/matrix.c:181-208`, `HOOK 0x4B3C50`) including its 0.0001 gimbal
guard. A tree-wide sweep for every site that composes *or decomposes* an authored
Euler triple found **ten** sites that disagreed with the engine, not just the six in
the render path:

| Site | Before | Consequence |
| --- | --- | --- |
| `renderer_objects.cpp` (render path, `:819-882`) + `renderer_objects_picking.cpp`, `app_view.cpp`, `app_input_keyboard.cpp`, `runtime/graph_camera_target.h` | `Z→X→Y` + an `IsZyxEulerModel` whitelist | multi-axis objects/cameras at the wrong angle |
| `app.cpp` runtime ladder-parent orientation | `Z→X→Y` | attached ladders got wrong mount offsets and band axes |
| `app.cpp` door slide offset | `Rz(closed)·Rx(rot.x)·Ry(rot.y)` | a rotated door slid along the wrong world axis |
| `app_level.cpp:754-761` | `Rz·Rx·Ry` | a promoted ATTA child on a rotated parent was written back to the MEF with the wrong local rotation |
| `debug_command_manager.cpp:395-401` | `Rz·Rx·Ry` | "centre on object" targeted the wrong point |
| `app_editor.cpp:1223-1243` | an `Rz·Rx·Ry` **decomposition** | a promoted ATTA proxy inherited the wrong orientation |

The door site has its own oracle: `IGI-RE Door_Place` (`HOOK 0x446290`,
`src/door/door.c:121-138`) rotates the slide offset by the door's **closed** basis
(`x = m[1]*v + m[0]*u`, `y = m[4]*v + m[3]*u`, `z = m[7]*v + m[6]*u`, `m = base.m`),
and the opening rotation is post-multiplied on top of it
(`Matrix_RotateZ(&d->o.matrix, &d->base, angle)`, `:446`) — which is why the closed yaw
is used and not `rot.z`.

### Verification

`tests/test_igi_transform.cpp` (9 tests) transcribes `Matrix_SetEuler` /
`Matrix_SetRotX/Y/Z` / `Matrix_Mul` line by line from IGI-RE and checks the helper
against it, including a 343-case grid over all three angles; it also transcribes
`Matrix_GetEuler` and checks `IgiMatrixToEuler` against it on a 245-case grid, plus
round-trips and a case that fails on the pre-fix `Rz·Rx·Ry` extraction. That last test
earned its keep immediately: it caught a wrong index in the first version of the
decomposition (the gimbal term used `m[1][0]` where the engine's `e[3]` is `m[0][1]`),
which the round-trip test alone had hidden — I had written both sides from the same
wrong mental model.

## IGIED-SCAMERA-PARAMS-001 (cutscene camera layout)

Status: fixed, unit- and retail-data-verified; not verified in a running editor.

### Symptom

`SCamera` tasks exposed a `Real32x9` rotation at arg 6 (which let a rotation edit
overwrite arg 7 — a *string* model id), drew the model from arg 10, and dropped the
arg 9 yaw offset on load and on save.

### Root cause

The `SCamera` parameter layout in the property/MCP layer did not match the engine's
table (`IGI-RE src/scamera/scamera.c:116-131`, `HOOK 0x442730`, `ParamTable_New(15)`).
Arg 7 is the model the task itself draws (`RigidDynCubeObj_SetModel(c,
VirModel_Find(c->model))`, `scamera.c:240`); arg 10 is the separate `SCameraLens`
child's model (`scameralens.c:204`), a child the editor does not model. Arg 9 is an
extra yaw, not roll (`scameralens.c:204`). The hard-coded defaults at
`scamera.c:181,184,185` and the one real `LEVEL1` line agree with that order.

### Implementation

`source/level/level_objects.cpp` (arg map), `level_objects_serialize.cpp` (parse +
write-back of args 6/8/9/7/10/11), `level_objects.h` (`yawOffset`),
`task_schema.cpp` (15 correctly-offset fields), `mcp_tools_objects.cpp` and
`game_data_service.cpp` (model resolves `"Model"` or `"Holder Model"`).

### Verification

`tests/test_scamera_params.cpp` (4 tests) plus a replay of every `SCamera` line in the
14 retail scripts; `tests/test_mcp_domain_tools.cpp` asserts `model_id` is parameter 7
and that no `rotation_radians` field can clobber it. See
`docs/igi-re-parity/SCAMERA_PARAMS.md`.

### Visible change

The editor now draws arg 7 (`313_01_1` in retail) instead of arg 10 (`313_02_1`).
Deliberate — arg 7 is what the game draws at the camera's place.

## IGIED-DECLARED-POSITION-001 (objects loading at the origin)

Status: fixed, unit-verified against real retail declarations; end-to-end load path
compile-verified only.

### Symptom

2438 of the 2944 `Task_New` lines in the retail scripts had no dispatch branch in
`LevelObjects::LoadRecursive` and loaded at `(0,0,0)`; 1757 of them belong to types
that carry a real world position at args 3/4/5 (`TerrainLightMap` 813,
`EditRigidObj` 582, `EditCamera` 111, …).

### Root cause

Not the parser (the recorded cause, "unmapped coordinate offsets in the parsed binary
QSC/QVM files", is refuted): the loader's final `else` read only the task id.

### Impact

`app_level.cpp` runs `SnapObjectsToTerrain()` right after load and the save path writes
`pos` back, so those objects were snapped to the terrain height at the origin and then
persisted there — silent level corruption.

### Implementation

The final `else` reads args 3/4/5 as a candidate and marks the object; `Load()` then
validates it against the level's own `Task_DeclareParameters` and keeps it only when
the type declares `("Position","ObjectPos")` as its first parameter (arg 3), reverting
types that merely have numbers there (`LightmapInfo`, `Dirlight`, `FlatSky`,
`PatrolPathCommand`, `AddAmmo`, …).

### Verification

`tests/test_declared_position_fallback.cpp` (5 tests) over 15 positional and 15
non-positional declaration strings copied verbatim from the retail scripts. See
`docs/igi-re-parity/DECLARED_POSITION_FALLBACK.md`.

## IGIED-FENCE-PARAMS-001 (Fence loader read a layout the engine does not have)

Status: fixed; compile-verified, unit-verified through the shared schema tests. No Windows
editor session.

### Symptom

None observed in-game — the two offending cases were dead — but the loader documented and
partially implemented a `Fence` parameter layout that contradicts the engine.

### Root cause

`LevelObjects::LoadRecursive`'s `Fence` branch read arg 8 as `rot.x` and arg 9 as `rot.y`.
IGI-RE `src/props/fence.c:115-119` registers exactly four parameters — `pos`
(`Param_ObjectPos` → args 3/4/5), `angle` (`Param_Angle` → arg 6, commented "about z"),
`model` (`Param_String16` → arg 7) and `cond` (`Param_VarString` → arg 8) — and
`struct Fence` (`:63-72`) holds a single `float angle` and no other Euler component. So
arg 8 is the electric-fence condition **string** (the case was guarded on `DBL`, so it never
fired) and arg 9 does not exist. `source/level/task_schema.cpp` already had the correct
four-field layout, and `level_objects_serialize.cpp` never wrote args 8/9, so the only
defect was the misleading loader branch.

### Implementation

`source/level/level_objects.cpp` `Fence` branch: both cases removed and replaced with the
cited four-parameter layout. Arg 8 is left untouched so a level's condition expression
round-trips verbatim; the editor does not evaluate it.

### Verification

`tests/test_scamera_params.cpp` / the corpus replay and `tests/test_mcp_domain_tools.cpp`
cover the schema-driven layout derivation; the branch itself is exercised by the level
loader, which cannot run on the host build (see `docs/KNOWN_ISSUES.md` §2).

## IGIED-DOOR-PARAMS-001 (Door loader read three arguments from the wrong slot)

Found while closing the Euler-order class of defect (#37): the same `SetupRuntimeDoors`
path also read its arguments by counting, and the count was wrong from argument 8 onward.

### Symptom

Not visible on the shipped retail levels, because the three mis-read arguments happen to
be empty or zero in the doors that use them -- but any door authored with a lock
expression or an open expression was driven by the wrong condition:

* 6 retail doors carry `lockExpr = "1"`, meaning *permanently locked*. The editor put that
  value in `open_expression`, so those doors would open instead of staying locked.
* 6 retail doors carry a real `openExpr` such as
  `Switch_506.isLastPressed || Door_409.isOpen`. The editor put that in
  `close_expression`, inverting the door's behaviour.

### Root cause

IGI-RE `src/door/door.c:257-269` registers the parameter table in this order:

| arg | field | arg | field |
| --- | --- | --- | --- |
| 3,4,5 | `place` (ObjectPos) | 13 | `openAngle` (degrees) |
| 6 | `slide[0]` | 14 | `openTime` (seconds) |
| 7 | `slide[1]` | 15 | `pickable` |
| 8 | `slidePos` | 16 | `pickTime` |
| 9,10,11 | `base` (`Real32x9`) | 17 | `lockExpr` |
| 12 | `model` | 18 | `openExpr` |
|  |  | 19 | `closeExpr` |
|  |  | 20,21,22 | open/close/move sound |

Two facts decide the whole mapping, and both were checked directly:

* **`Real32x9` is an Euler triple, not nine arguments.** `ParamReal32x9_Parse`
  (HOOK `0x4EFBA0`, `src/physobj/paramops.c:423-436`) calls `ParamCursor_Need(c, 3)`, reads
  three reals and advances the cursor by 3, then hands them to `Matrix_SetEuler`. Every
  retail door has its yaw in argument 11 and its model name in argument 12, which is only
  consistent with a three-argument matrix.
* **`struct Door` carries `float slide[2]`** (`src/door/door.h:45`). `Door_Place`
  (HOOK `0x446290`, `door.c:121-138`) uses only `slide[0]` and `slide[1]`; argument 8 is
  the *initial open fraction*, folded into the offset as `u = slide[0] * slidePos * kSlideScale`.

The call site therefore had three defects:

1. `slide_offset_units` was built from arguments 6/7/**8**, giving the offset a Z component
   the engine does not have. Argument 8 is `slidePos`; it is 0 in all 33 retail doors, so
   retail data was unaffected, but the read was wrong.
2. The three expression arguments were read one slot early, rotating the lock/open/close
   expressions onto each other.
3. The per-argument readers were duplicated inline in `app.cpp`, so the layout could not be
   tested without a GL context.

### Implementation

The layout moved into `source/runtime/door_params.h` as `igi::ApplyDoorArgumentLayout`
(header-only, `RuntimeDoorDefinition` only), with the argument positions named as constants
and each IGI-RE citation inline. `App::SetupRuntimeDoors` now calls it once and no longer
carries the readers. The offset's Z component is `0.0f` with a comment saying why.

Units were verified rather than assumed: the authored values are metres
(`gDoorSlideRange` is -10..10) and the engine scales by `kSlideScale`, aliased to
`kMeter`/`k4096` at `0x5333B8` (`src/addr.c:25-29`) -- 4096 units per metre.
`igi::PlayerController::WORLD_METER` is 4096 (`source/player_controller.h:60`), so the
editor's conversion *is* the engine's scale, not an approximation of it.

### Verification

`tests/test_door_params.cpp` (5 tests) drives the pure function with retail task lines:

* `OrientationIsAnEulerTripleSoTheModelIsArgumentTwelve` -- the real first `Door` of
  `LEVEL1/objects.qsc`, verbatim.
* `SlideHasNoZComponentAndArgumentEightIsNotAnOffset` -- a door with `slidePos = 0.75`.
* `ExpressionsAreLockOpenCloseInThatOrder` -- asserts `locked_expression == "1"` and
  `open_expression == "Switch_506.isLastPressed || Door_409.isOpen"`, i.e. exactly the two
  swaps that were wrong.
* `MissingArgumentsFallBackInsteadOfReadingGarbage` -- a truncated line falls back to the
  engine's defaults instead of reading past the end.
* `EveryRetailDoorTaskKeepsItsSoundsAndSlideInTheDocumentedSlots` -- replays every `Door`
  task in the installed corpus (33 tasks; 6 with `lockExpr`, 6 with `openExpr`, 31 naming
  sounds, argument 8 zero in all) and pins the structural facts that fix the mapping:
  argument 11 is a quarter-turn yaw and argument 12 is a quoted model name.

Observed: `IGI_HOST_NO_CACHE=1 IGI_HOST_TESTS=tests/test_door_params.cpp` ->
`OK: 5 tests passed in 1 suites`, exit 0. The full lane then reported
`486 tests run, 485 passed, 0 failed`, and the x64 cross-build gate exited 0.

### Not verified

No runtime door was opened: the runtime door path needs the Windows build and a loaded
level. The claim proved here is the argument mapping, not door gameplay. The initial open
fraction (argument 8) is still not consumed by the editor's door state, so a door authored
as initially open still starts closed -- a behaviour gap, not a read error, and it is
recorded in `docs/KNOWN_ISSUES.md`.

## Safe future fix and release procedure

1. Reproduce the issue with a disposable copy; hash all protected inputs.
2. Add a general regression test without hardcoding a user query or special
   model ID in production logic.
3. Make the smallest source change, then run the focused test and a clean x86
   build.
4. Run the relevant live WMI Session 1 test with fresh logs, screenshots, file
   hashes, and mutation/rollback checks.
5. Run the full suite and record skips and failures separately from passes.
6. Update `CHANGELOGS.md`, review the complete diff, and only then create a
   versioned ZIP with binaries and a SHA-256 manifest.
7. Push the implementation branch first. Promote only this Markdown file to
   `develop` unless the maintainer explicitly requests more files.
8. Create or update the GitHub release only after authentication, remote
   checks, tests, and external review gates pass.
