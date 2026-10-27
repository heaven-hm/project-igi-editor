# Full open-issue triage against IGI-RE

All **32 open issues** of `heaven-hm/project-igi-editor`, read in full (bodies, not
titles) and triaged against the current branch `fix/igi-re-known-issues`
(HEAD `7668b279`).

**Oracle.** `/Users/haseeb-mir/Documents/Code/IGI-RE` — the decompiled retail
`IGI.exe`. Every engine claim below is cited as **function + HOOK/ORIG address +
`file:line`**, resolved by grepping `symbols/names.tsv` and reading `src/`. No web
source and no other port is used as authority.

**Editor evidence.** `source/`, `docs/igi-re-parity/*.md`, `docs/ISSUE_FIXES.md`
and `git log --oneline -40` on this worktree. Every "landed" claim names the commit
or the file. Where the branch's own parity document records a limitation, that
limitation is repeated here rather than dropped.

**What "landed" means here.** A rule module that exists but has **no caller
outside its own translation unit** is *not* landed. That test was run mechanically
(`grep -rn '#include "…"' source/`), not assumed — it is the single most common way
this backlog looks done and is not.

**Bucket counts (32 total).**

| Bucket | Count | Issues |
| --- | --- | --- |
| Already landed | 10 | #29 #37 #40 #42 #44 #47 #48 #59 #67 #68 |
| Partially landed — concrete IGI-RE oracle | 14 | #43 #57 #58 #60 #62 #66 #69 #70 #72 #73 #74 #75 #79 #80 |
| Not started — concrete IGI-RE oracle | 0 | — |
| No retail counterpart (feature / refactor / umbrella) | 8 | #7 #49 #50 #51 #52 #54 #56 #63 |

There is **no** issue in this backlog that has a concrete IGI-RE oracle and no work
landed against it. Every oracle-bearing issue already has at least a ported,
unit-tested rule module; the recurring gap is **wiring**, not discovery.

**Not verified here.** This host offers OpenGL 2.1 and the editor needs 4.1, so no
frame was rendered. No claim below is a rendered or in-game result; every
"landed" claim is a source/commit/test claim. The Windows suite was not run
(the Lead owns the gate). Nothing in this document was produced by running a build.

---

## Master table

`LANDED` = the issue's acceptance criteria are met in this branch.
`PARTIAL` = a rule module and/or part of the feature is in, and the rest is named.
`NO ORACLE` = retail has no such subsystem; the work is engine design, not parity.

| # | Title | State | IGI-RE entry point (verified) | Next action |
| --- | --- | --- | --- | --- |
| 29 | OBJ → MEF Importer | **LANDED** `7668b279` | `Mesh_Load` `0x4CF390` `src/model/model.c:405`; `ModelFile_Setup` `0x4CF080` `src/model/model.c:317`; ILFF `Chunk_Data` `0x4B74D0` `src/res/chunk.c:34-49` | none — skeletal (format 1) models are the declared follow-up |
| 37 | Object rotation / placement / offsets | **LANDED** `8551c6b1` | `Matrix_SetEuler` `0x4B38E0` `src/math/matrix.c:99-110`; `Matrix_GetEuler` `0x4B3C50` `src/math/matrix.c:181-208`; `ParamReal32x9_Parse` `0x4EFBA0` `src/physobj/paramops.c:435` | none — issue's "YXZ" and MEF-pivot premises are both refuted; see §A0 |
| 40 | Moving objects, vehicles, missing rotors | **LANDED** `8551c6b1` | `Rotor_Init` `0x42D2B0` `src/rotor/rotor.c:211`; `RigidDynCubeObj_CreateMagicObjs` `0x4C4950` `src/editobj/physmagic.c:35`; `MEF_ATTA` `src/model/model.c:374` | none |
| 42 | Stationary machinegun + zipline | **LANDED** `8551c6b1` | `StationaryGun_Open` `0x473ED0` `src/statgun/statgun.c:152`; `GunHolder_Init` `0x46E370` `src/comprun/gunholder.c:168`; `HumanPlayer_UpdateZipline` `0x411F20` `src/weapon/humanplayer.c:589`; `Wire_Open` `0x43EFF0` `src/props/wire.c:113` | none — a zipline is a sloped `Wire`; there is no zipline task type |
| 44 | .gitignore, stray files, version drift, CI | **LANDED** `8fdc4826` + `8551c6b1` | none (repo hygiene, no engine behaviour) | none — `.github/workflows/tests.yml` exists, `tools/` tracks 50 files, `CMakeLists.txt:14` reads `version` |
| 47 | Dual-vertex-set AI model textures | **LANDED** `8551c6b1` | `Direct3DRender_LoadModel` `0x49FB10` `src/dxinit/d3drender.c:82`; `BoneMesh_Skin` `0x49B700` `src/dxinit/d3drender.c:313`; `MeshHeader.format` `src/model/modeltypes.h:12` | none on this premise — the `XTVC/ECFC` hypothesis is **disproved** (§A1); the residual symptom needs a new theory |
| 48 | 25 stubbed keybindings | **LANDED** `8551c6b1` | none — editor keybindings have no engine counterpart (§B5) | none — `KEYBINDINGS.md`: 2 implemented, 23 removed, 0 silent no-ops |
| 59 | Train / railroad path support | **LANDED** `8551c6b1` | `Train_Place` `0x42A2C0` `src/missile/train.c:274`; `SplineObj_GetPoints` `0x4FEC10`; `SplineObj_Open` `0x502250` `src/editobj/spline.c:225` | none |
| 67 | Magic-object table drives attachments | **LANDED** `f65d3768` | `DefineMagicObj` `0x4C4520` `src/magicobj/magicobj.c:220`; `Sector_CreateMagicObjs` `0x4DFA50` `src/shadow/sector.c:687` | none — registry consumed at `source/renderer/renderer_objects_atta.cpp:8,51-75` |
| 68 | Ladder placement parity | **LANDED** `8551c6b1` + `140d152b` | `Ladder_Use` `0x4404C0` `src/props/ladder.c:175`; `HumanPlayer_UpdateLadder` `0x4113E0` `src/weapon/humanplayer.c:340`; `kReachMax/kReachMin` `src/props/ladder.c:200,203`; `kLadderBack/kLadderUp/kLadderTopBack` `src/weapon/humanplayer.c:130-132` | none — the issue's "8192 radius" is a double-reinterpretation artifact (§A2) |
| 43 | Create/edit lights and smoke FX | **PARTIAL** `aa225a6e` | `Smoke_Init` `0x437510` `src/fxobj/smoke.c:194`; `Smoke_Create` `0x437B90` `src/fxobj/smoke.c:248`; `Smoke_Move` `0x437AD0` `src/fxobj/smoke.c:210`; `AlarmLight_Init` `0x4397C0` `src/fxobj/alarm.c:124` | call `smoke.h`/`alarm_light.h` from a draw path and add the authoring/inspector half; no retail *point-light* authoring type exists |
| 57 | Snow particle weather | **PARTIAL** `aa225a6e` + `273bb24f` | `Rain_New` `0x498960` `src/dxinit/rain.c:167` (snow is `r->snow = rain == 0`, `src/dxinit/rain.c:144-153`); `RainEffect_Init` `0x436780` `src/fxobj/rain.c:84` | wire the `R7` block of `source/runtime/rain_rules.*` into `renderer_rain.cpp`; bind `snow.tex` (absent today) |
| 58 | Rain renderer parity audit | **PARTIAL** `aa225a6e` + `273bb24f` | `Rain_New` `0x498960` `src/dxinit/rain.c:167`; `Rain_Init` `0x498AB0` `src/dxinit/rain.c:190` | same wiring as #57 — audit is written (`RAIN.md`), rules module has no caller |
| 60 | Helicopter rotor + flight preview | **PARTIAL** `273bb24f` | `Heli_Control` `0x431B70` `src/heli/heli.c:406`; `Heli_Move` `0x431E30` `src/heli/heli.c:520`; `Rotor_Init` `0x42D2B0` `src/rotor/rotor.c:211` | the rotor spin half **is** wired, via `igi::IgiRotorSpinMatrix` (`source/renderer/renderer_objects_atta.cpp:1263-1276`, citing `src/rotor/rotor.c:139-152,180-181`); `heli_rules.*` (the Heli task's control and motion) still has no caller |
| 62 | LOD virtual-model chains | **PARTIAL** `c7bd9e2a` `013b06bb` `aa225a6e` | `VirModel_InitLods` `0x4CED50` `src/model/model.c:145-163`; `ModelLod_Find` `0x4CEE30` `src/model/model.c:165-178`; `lod_select` `src/lod/mesh3d.c:161-208`; `ModelObj_GetLOD` `0x4CFCB0` `src/lod/mesh3d.c:209-227` | the per-frame LOD switch: `GetLodMeshName` exists but the drawn mesh is still the object's own name |
| 66 | Per-mission fog from FlatSky + volumes | **PARTIAL** `4d30ad3b` `19d226a4` | `GlobalLight_Open` `0x527C90` `src/nodereg/tasktypes.c:93`; `Fog_Compute` `0x4DA790` `src/glowmesh/terrainmesh.c:41`; `Fog_SetRange` `0x49B2D0` `src/dxinit/d3drender.c:136`; `FlatSky` fog fields `src/flatwater/flatsky.c:89-92` | per-level override store and `HazeLayer`-style local fog volumes; the FlatSky premise is half right (§A3) |
| 69 | Hermite road/bridge segment placement | **PARTIAL** `273bb24f` | `SplineObjSegment_Setup` `0x4E4530` `src/sound/segsetup.c:366` (decl `src/tagitem/spline.c:37`); `SplineObj_Open` `0x502250` `src/editobj/spline.c:225` | wire `spline_rules.*`'s Hermite path into `renderer_splines.cpp` (which still uses chord-direction orientation) |
| 70 | Editor sound system | **PARTIAL** `aa225a6e` `273bb24f` | `ConditionalSound_Init` `0x424E80` `src/vehicle/condsound.c:94`; `AmbientArea_Init` `0x44C130` `src/aiaction/ambientarea.c:81` | the shipped path is a 232-line `PlaySoundA` wrapper (`source/runtime/audio_system.cpp:90`), not the SoundBank/OpenAL port the issue asks for |
| 72 | Calculate Lightmaps + HDR mode | **PARTIAL** `273bb24f` | `Lightmap_GetResPath` `0x5210E0` `src/litmap/lightmap.c:101`; `Lightmap_FreeBuffers` `0x521040` `src/litmap/lightmap.c:87` (also declared `src/dirlight/lightmapinfo.c:52`); `Lightmap_BuildLod` `0x523D40` `src/dirlight/lightmapbuild.c:445`; `LightmapInfo_MakeName` `0x5235A0` `src/litmap/lightmap.c:257` | the "Calculate Lightmaps" button **already exists** (`renderer_draw.cpp:1836`, `app_editor.cpp:430-488`) but recalculates `.olm` via `igi1conv`; the bake-from-lights engine and HDR mode remain |
| 73 | Animation playback + skinning | **PARTIAL** `273bb24f` | `GenericTBA_GetAnim` `0x4485D0` `src/glass/tba.c:284`; `AIAction_PlayAnimation` `0x44D6E0` `src/aiaction/aiaction.c:524`; `Anim_Play` `0x4D61D0` `src/soldier/soldier.c:59`; `Human_PlayAnim` `0x489EA0` `src/soldier/soldier.c:180`; `BoneMesh_Skin` `0x49B700` `src/dxinit/d3drender.c:313` | wire `anim_rules.*` into the viewport and the F10 panel; `MefSkinner` is an open-igi name, not an IGI-RE symbol |
| 74 | QVM-driven in-game menu | **PARTIAL** `42a689bb` `41d4d33a` | `MenuManager_Init` `0x4170A0` `src/game/menumanager.c:98`; `MenuManager_Create` `0x417260` `src/game/menumanager.c:160`; `MenuManager_RequestScreen` `0x4174A0` `src/game/menumanager.c:224`; `MenuManager_GetString` `0x418EA0` `src/ui/ui.h:61` | edit + save-back to `ingamemenu.qvm` (read/draw/navigate are landed; no writer exists in `source/menu/`) |
| 75 | Weapon/ammo pickup parity | **PARTIAL** `aa225a6e` `273bb24f` | `touches` `src/comprun/pickup.c:180` (`kMinTouch` 2867.2 `:159`, `kTouchHeight` 4096.0 `:160`); `GenericPickup_Touch` `0x46C630` `src/comprun/pickup.c:366`; `GunPickup_Touch` `0x46D230` `src/comprun/pickup.c:616`; `GunPickup_New` `0x46C980` `src/comprun/pickup.c:449` | only `igi::TruncatePickupCount` is called (`source/app.cpp:2768`); real weapon models, radius gizmos and the inspector remain |
| 79 | OpenIGI road/track spline objects | **PARTIAL** `273bb24f` | `SplineObj_Open` `0x502250` `src/editobj/spline.c:225`; `SplineObjWaypoint_Create` `0x5008F0` `src/editobj/spline.c:49`; `SplineObjWaypoint_Changed` `0x500EF0` `src/editobj/spline.c:51`; `SplineObjSegment_Setup` `0x4E4530` `src/sound/segsetup.c:366` | expose the authored model in the task tree; the data model is ported but has no authoring surface |
| 80 | Intro/outro cutscenes | **PARTIAL** `273bb24f` | `CutScene_Open` `0x4F4B00` `src/nodereg/tasktypes.c:30`; `EditCamera_Init` `0x507F10` `src/editcam/editcamera.c:110` | wire `cutscene_rules.*` into a camera/preview path; the `.avi` IntroScreen is deliberately out of scope |
| 7 | AI & waypoint editor | **NO ORACLE** | no nav-mesh/waypoint-authoring symbol; `Waypoint*` matches are all `SplineObjWaypoint_*` (spline authoring). Retail equivalent: `PatrolPath_Open` `0x453550` `src/patrol/path.c:153`, `PatrolPathCommand_Open` `0x4533B0` `src/patrol/path.c:86`, `PatrolRoute_Current` `0x453720` `src/aitask/aitask.c:153`, `AIAction_Patrol` `0x44D0E0` `src/aiaction/aiaction.c:390` | re-scope as a `PatrolPath` editor, not a nav-mesh editor; the proposed graph format has no retail reader |
| 49 | Roadmap: configurator, scrub, Neo | **NO ORACLE** | tracking issue; the animation-scrub half is `Anim_Play` `0x4D61D0` `src/soldier/soldier.c:59` | split into concrete issues; "Neo Remastered" has no retail oracle at all |
| 50 | Split `renderer_draw.cpp`; add docs | **NO ORACLE** | pure refactor — no behavioural oracle exists or is needed | extract UI from `renderer_draw.cpp` (now **4220** lines, up from 3383); add `CONTRIBUTING.md` + architecture doc (neither exists) |
| 51 | 3-layer lightmap & shadow pipeline | **NO ORACLE** | `MeshHeader.format` `src/model/modeltypes.h:12` (`0` rigid, `1` bone, `3` lightmapped); `fill_refs` `src/sector/sector.c:544-563`; `SectorObject_LoadLightmaps` `0x4DC3D0` `src/sector/sector.c:681` | Baked/Hybrid/Dynamic already shipped and toggled (`object_lightmap.cpp:38`, `app_input_mouse.cpp:559`); layer 3 (real-time shadows for moving entities) is engine design, not parity |
| 52 | Post-processing, ACES/AgX, motion blur | **NO ORACLE** | none — retail `IGI.exe` has no post-process pass to mirror | engine feature; implement from its own spec and label it as non-parity |
| 54 | Windows runtime vertical slice | **NO ORACLE** (umbrella) | no single entry point; consumes `runtime_session.cpp`, `simulation_scheduler.cpp`, `gameplay_host.cpp` | needs a Windows host — cannot be closed from this checkout |
| 56 | Complete vanilla gameplay parity | **NO ORACLE** (umbrella) | not decomposable into one change | keep as a tracking issue; the branch handoff in the body is stale relative to `gameplay_host.cpp` |
| 63 | Infer light fixtures from emissive geometry | **NO ORACLE** | `LightFixtureExtractor` absent from `symbols/names.tsv`; nearest retail light type is `AlarmLight_Init` `0x4397C0` `src/fxobj/alarm.c:124` | the inference is new tooling, not a port; the bake side is #72's |

---

## A. Issues with a concrete IGI-RE oracle

### A0. #37 — object rotation, placement, offsets *(landed; premises refuted)*

**What the issue asks.** That placed objects clip into walls, that rotations use the
wrong Euler order, and that the fix is "YXZ" order plus a MEF local-origin offset.

**Oracle, verified.** `Matrix_SetEuler` (`0x4B38E0`, `src/math/matrix.c:99-110`)
composes `R = Rz(az)·Ry(ay)·Rx(ax)` on row-major 3x3 matrices, with `Matrix_Mul`
meaning `a·b`. `ParamReal32x9_Parse` (`0x4EFBA0`, `src/physobj/paramops.c:435`)
passes the authored triple through unchanged — so the authored order is
`Rz·Ry·Rx`, **not** `YXZ`. `Matrix_GetEuler` (`0x4B3C50`,
`src/math/matrix.c:181-208`) is the inverse direction, with its own `0.0001`
gimbal guard.

**State.** Landed, `8551c6b1`, recorded as `IGIED-EULER-ORDER-001` in
`docs/ISSUE_FIXES.md:106-161` and `docs/igi-re-parity/TRANSFORM_PIPELINE.md`.
Ten disagreeing call sites were found, not the six in the render path; the
door-offset site has its own oracle (`Door_Place` `0x446290`,
`src/door/door.c:121-138`). Handedness and SRT order were already correct, and the
MEF-pivot claim is refuted (`TRANSFORM_PIPELINE.md` §3).

**Next action.** None. Do not "fix" the Euler order to YXZ — that would introduce
the bug the issue describes.

---

### A1. #47 — dual-vertex-set AI models *(landed; root cause disproved)*

**What the issue asks.** That six AI models (`001_02_1`, …) render the wrong
body-part textures because `source/renderer/mef_native.cpp` picks the wrong one of
two parallel vertex/texture sets (`XTVC0/ECFC0` vs `XTVC1/ECFC1`).

**Oracle, verified.** `XTVC/ECFC/TAMC/HSPC` are **collision** data. The render
partitions come from the `REND`/`DNER` group chain, walked by
`Direct3DRender_LoadModel` (`FN 0x49FB10`, `src/dxinit/d3drender.c:82`) and drawn by
`Direct3DRender_DrawRigidMesh` (`FN 0x49DF60`, `src/dxinit/d3drender.c:77`). The
skin path is `BoneMesh_Skin` (`0x49B700`, `src/dxinit/d3drender.c:313`). The
lightmapped format is `MeshHeader.format == 3` (`src/model/modeltypes.h:12`,
tested at `src/litmap/hemirender.c:89`).

**State.** Landed, `8551c6b1` (`tests/test_mef_dual_set.cpp`,
`docs/igi-re-parity/MEF_DUAL_SET.md`). Measured on the exact model the issue names:
13 `DNER` groups ↔ 13 materials ↔ 13 `MTP` textures, and per-material triangle
counts match the file's own collision mesh. A 1578-file differential of the parser
change produced **byte-identical output**, so the change is a robustness fix, not a
visual one.

**Next action.** None on this premise. The symptom in the issue is **not explained**
by `XTVC/ECFC`; `MEF_DUAL_SET.md` §7 lists the remaining live suspects. Treat #47
as needing a new diagnosis, and say so rather than closing it as fixed.

---

### A2. #68 — ladder placement *(landed; the issue's "8192" is a reinterpretation)*

**What the issue asks.** Viewport gizmos for the ladder activation volume, the two
mount points and the climb line, using constants from `sub_4404C0`/`sub_4113E0`.

**Oracle, verified.** `0x4404C0` → `Ladder_Use`
(`src/props/ladder.c:175`, body `:129-175`); `0x4113E0` → `HumanPlayer_UpdateLadder`
(`src/weapon/humanplayer.c:340`).

* Face half-width: `lx > kReachMin && !(lx >= kReachMax)` with
  `kReachMax = 2867.2` / `kReachMin = -2867.2` (`src/props/ladder.c:200,203`) — the
  issue's `|localX| < 2867.2` is right.
* Facing tolerance: `angle >= kLadderAngle`, where `kLadderAngle` is aliased to
  `kQuarterPi` (`src/plane/gear.c:157`, extern `src/props/ladder.c:55`) and
  `Vec3_Angle` (`0x4B2B30`, `src/file/vec.c:189-208`) returns an **angle**. The 45°
  is right; the literal `0.70710678` does **not** appear anywhere in IGI-RE
  (`grep -rn "0\.7071" src/` → no match). Mark the cosine form as the issue's
  paraphrase.
* Mount offsets: `kLadderBack = 1515.52`, `kLadderUp = 3915.776`,
  `kLadderTopBack = 3481.6` (`src/weapon/humanplayer.c:130-132`). All three match.
* **Correction — the "radius 8192.0 units at evaluate context +120" is an
  artifact.** `Human_UpdateUse` sets `q.range = kUseRange` where
  `kUseRange = 6.0f` (`src/humanbase/human.c:168`, assigned `:1288`). The `ViewQuery`
  view declares a `double range` at `+0x78`, so reading the two words
  `0x00000000` / `0x40C00000` as a double yields `0x40C0000000000000` = **8192.0**.
  The engine compares 6.0, not 8192.0. Porting 8192 would be wrong by 1365×.

**State.** Landed, `8551c6b1` + `140d152b`
(`docs/igi-re-parity/LADDER_PLACEMENT.md`). Gizmos are wired into the existing
"Magic objects" overlay.

**Next action.** None.

---

### A3. #66 — per-mission fog *(partial)*

**What the issue asks.** Derive each mission's fog from its **FlatSky** task, with a
per-mission exception file, plus optional localized fog volumes.

**Oracle, verified — and the premise is half right.** FlatSky's ParamTable
registers real authored fog fields (`src/flatwater/flatsky.c:89-92`:
`unk74` = "Fog Amount", `unk6C` = "Z Pos", `unk70` = "Distance"), so FlatSky *is* a
fog source. But the fog the renderer actually uses is published by `GlobalLight`:
`GlobalLight_Open` (`0x527C90`, `src/nodereg/tasktypes.c:93`) and
`TerrainMesh_SetFog` (`0x50FD20`, `src/glowmesh/terrainmesh.c:302-320`), with
`Fog_Compute` (`0x4DA790`, `src/glowmesh/terrainmesh.c:41`) and `Fog_SetRange`
(`0x49B2D0`, `src/dxinit/d3drender.c:136`).

**State.** Partially landed. `4d30ad3b` + `19d226a4` derive the authored fog from
`GlobalLight` and call it at `source/level/level.cpp:542`
(`SetupFog(fog_color, fog_far_meters * WORLD_UNITS_PER_METER)`). `19d226a4` fixed a
real defect: the previous `(1/density) * 120.0f` constant made fog distance
**4.069× too short** on every level.

**What remains.** (1) `source/runtime/mission_fog.{h,cpp}` holds the ported math
(`ResolveMissionFog`, `ExponentialFogTransmittance`, `HazeGetFog`) but has **no
caller** outside its own TU — `grep -rn '#include "runtime/mission_fog.h"' source/`
matches only `mission_fog.cpp`. (2) The per-level override store does not exist.
(3) `HazeLayer` local fog volumes are unported.

**Next action.** Give `mission_fog.h` a consumer in `level.cpp`/`renderer.cpp` so
the ported exponential path replaces the remaining hand-derived constant, then add
the override store. Do **not** re-derive from FlatSky only: the measured path is
`GlobalLight` → `TerrainMesh_SetFog`.

---

### A4. #62 — LOD virtual-model chains *(partial: the per-frame switch)*

**What the issue asks.** Resolve the LOD chain by incrementing the last character of
the model name (max 5), wire chain length into lightmap page indexing, and add an
LOD override control.

**Oracle, verified.** `VirModel_InitLods` (`0x4CED50`,
`src/model/model.c:145-163`): `name[strlen(name) - 1]++`, first-miss terminator,
`VIRMODEL_LODS = 5` (`src/model/model.h:11`), distances `100/(i*i)`.
`ModelLod_Find` (`0x4CEE30`, `src/model/model.c:165-178`) is an exact `strcmp`.
Drawn-LOD selection is **not** part of the chain rule: it is `lod_select`
(`src/lod/mesh3d.c:161-208`) via `ModelObj_GetLOD` (`0x4CFCB0`,
`src/lod/mesh3d.c:209-227`), fed by `ModelLODSettings_Changed` (`0x4EDB60`,
`src/animtrig/lodsettings.c:173`).

**State.** Partially landed: `c7bd9e2a` (lightmap ref index), `013b06bb` (authored
distances), `aa225a6e` (loader wiring). Measured on the retail corpus: 6605 model
names, `onlyVnam=0 onlyRes=0 differingChains=0` — the packed archive's name set
equals the engine's VNAM set in all 14 bundles, so the wiring's existence semantics
are faithful.

**What remains.** The per-frame LOD switch. The chain is exposed
(`Renderer_Objects::GetLodChain`, `source/renderer/renderer_objects_mesh.cpp:51`)
but the drawn mesh is still the object's own name. `LOD_CHAINS.md` §6 names the
blocker: for `modelType == 3` the editor groups submeshes one-per-render-block so
that `renderer_objects.cpp`'s `lightmaps[si]` lookup lines up with
`obj00000_000NN.olm` (`source/renderer/model.cpp:239-252`), so drawing a lower LOD
without its own bake would bind the wrong atlas page or none. That design step is
the work.

**Non-applicable — per-LOD lightmap reference counts.** The engine reserves one
`LightmapRef` per LOD (`fill_refs`, `src/sector/sector.c:544-563`) for the hemicube
**bake** renderer `Hemicube_DrawLightmapSet` (`0x521E40`,
`src/litmap/hemirender.c:58`, which selects `ref->lod` at `:78-86`) and for
`<name>_<refIndex>.olm` (`src/dirlight/lightmapbuild.c:59`). The editor has
no `LightmapSet` and no hemicube bake; `ObjectLightmapManager::GetLightmapTexture`
is genuinely dead in this architecture (`LOD_CHAINS.md` §5.1) and must not be
force-wired.

**Next action.** Decide the per-LOD lightmap binding first (does a lower LOD get its
own `.olm`, or LOD 0's?), then switch the drawn mesh from `GetLodMeshName`. Add the
override control to the render menu afterwards.

---

### A5. #75 — weapon/ammo pickup parity *(partial)*

**What the issue asks.** Pickups render as a wireframe box with a 90° spin and a
1.2× scale fudge (`renderer_objects_visual.cpp:633-641`); the editor should show the
real weapon model at retail orientation, add toggleable radius gizmos at the retail
constants, and show resolved weapon/ammo types in the inspector.

**Oracle, verified — all three cited addresses resolve, and the constants are
confirmed.**

| Claim | Verified |
| --- | --- |
| `0x46C630` = `GenericPickup::Update` | `GenericPickup_Touch` — `src/comprun/pickup.c:366` (body `:361-367`) |
| `0x46D230` = `GunPickup::Update` | `GunPickup_Touch` — `src/comprun/pickup.c:616` (body `:598-616`) |
| `0x46C980` = `GunPickup_Create` | `GunPickup_New` — `src/comprun/pickup.c:449` (body `:392-449`) |
| PickupRadius 2867.2 planar | `kMinTouch = 2867.2` — `src/comprun/pickup.c:159` |
| VerticalTolerance 4096.0 | `kTouchHeight = 4096.0` — `src/comprun/pickup.c:160` |

`touches()` (`src/comprun/pickup.c:180-206`) is the real rule: `range` is the
**maximum** of the model radius and 2867.2, the test is planar
(`d.z = 0.0` before `Vec3d_Length`) and strict (`!(len >= range)`), and the vertical
test is `!(dz >= 4096.0)`. The issue's "planar" and the constant are right; "the
same half-width constant as the ladder activation" is a coincidence, not a shared
rule — the ladder uses `kReachMax` (`src/props/ladder.c:200`), which happens to be
the same number.

**State.** Partially landed. `aa225a6e` ported the rules
(`source/runtime/pickup_rules.{h,cpp}`, 14 unit + 5 corpus tests) and `273bb24f`
corrected the `GunPickup`/`AmmoPickup` schemas. But the only call into the module is
`igi::TruncatePickupCount` at `source/app.cpp:2768` — the renderer, the gizmos and
the inspector are untouched.

**Next action.** In `renderer_objects_visual.cpp`, replace the box+90°+1.2× path with
`igi::PickupLayouts()`-driven model resolution and `PickupTouchRange` /
`PickupTouchesFromModel` for the gizmo; add the radius disc and ±4096 marker behind
the existing magic-objects overlay toggle.

---

### A6. #57 / #58 — snow and rain *(partial; they are one retail system)*

**What the issues ask.** #58: audit the editor's rain renderer against a reference
and add weather enable/speed controls. #57: add snow, which the editor lacks.

**Oracle, verified — and #57's premise is wrong.** Snow is **not** a separate
system. `Rain_New` (`0x498960`, `src/dxinit/rain.c:167`) takes a third argument that
becomes `r->snow = rain == 0` (`src/dxinit/rain.c:144-153`); every snow difference is
a branch on that field. `Rain_Init` is `0x498AB0` (`src/dxinit/rain.c:190`), the
authored descriptor is `RainEffect_Init` (`0x436780`, `src/fxobj/rain.c:84`), and
`grep -i "snow" symbols/names.tsv` finds **no** snow symbol. There is no
`SnowRenderer` equivalent to port.

**State.** Partially landed. `aa225a6e`/`273bb24f` ported the rules
(`source/runtime/rain_rules.{h,cpp}`, with the snow block as `R7`) and
`docs/igi-re-parity/{RAIN,SNOW}.md` record the audit — including 11 divergences
(RAIN.md D1–D10, SNOW.md S-D8/S-D11). The weather **mode** half is wired:
`source/app_level.cpp:135-167` (`ApplyWeatherMode`, `CycleWeatherMode`) calls
`renderer_.SetRainEffect(weather.active, weather.is_snow, …)` through
`source/runtime/weather_mode.cpp`, and `weather_particle_style.h` /
`weather_visibility.h` are consumed by `renderer_rain.cpp` and
`renderer_objects_mesh.cpp`.

**What remains.** (1) `rain_rules.h` has **no caller** outside its own TU, so the
ported spawn/wrap/flake rules do not drive the **rain** GPU path — they drive the
snow path (`Renderer_Rain::DrawSnow`), while rain is still the invented 1200-drop
field of `source/renderer/weather_math.h`. (2) **DONE** — `snow.tex` is loaded and
bound: `Renderer_Objects::GetOrLoadGameTexture` resolves the engine's own identity
`LOCAL:common/textures/snow.tex` out of the pack mounted at `src/game/game.c:430`
(the engine registers it with `Game_AddTexture("snow.tex")` at `src/fxobj/rain.c:82`
and samples it at `src/dxinit/rain.c:431-432`), and `Renderer_Rain` binds it with
the engine's UVs, stage-0 MODULATE / SELECTARG2 and filters — see SNOW.md §9 and
`tests/test_weather_texture.cpp`. (3) The `r_weather_speed` 0..2× multiplier is an
editor feature with no engine counterpart (no such symbol exists in IGI-RE); the S6
fade ramp **is** implemented (`SnowTick` ramps `fade` by `kFadeStep`, rain.c:574-583).

**Next action.** Move the rain half onto the same pool/tile-lattice draw as snow
(`Rain_Update` / `Rain_FillVB` / `Rain_DrawCell`), which also removes
`weather_math.h`'s OpenIGI-derived constants; add the speed multiplier next to the
existing weather-mode cycle if it is still wanted.

---

### A7. #73 — animation playback + skinning *(partial)*

**What the issue asks.** Port key-evaluation and `MefSkinner`-style CPU skinning so
placed NPCs walk with correctly skinned limbs, with frame-step and speed control.

**Oracle, verified.** `MefSkinner` is an **open-igi class name**; IGI-RE has no
`skinning` symbol (`grep -i` over `symbols/names.tsv`, 7068 lines → no match). The
two things the word means in retail both exist:

| Meaning | IGI-RE symbol |
| --- | --- |
| Choosing/playing an authored animation | `GenericTBA_GetAnim` `0x4485D0` `src/glass/tba.c:284`; `Anim_Play` `0x4D61D0` `src/soldier/soldier.c:59`; `Human_PlayAnim` `0x489EA0` `src/soldier/soldier.c:180`; `Soldier_AnimEnter` `0x45C750` `src/soldier/soldier.c:632`; `AIAction_PlayAnimation` `0x44D6E0` `src/aiaction/aiaction.c:524` |
| Skinning vertices against bone poses | `BoneMesh_Skin` `0x49B700` `src/dxinit/d3drender.c:313` (body `:226-313`) |

**State.** Partially landed. `273bb24f` ported the key-evaluation, tick mapping,
blend/fade, the authored per-type selection and the bone/skin side into
`source/runtime/anim_rules.{h,cpp}`, tested by 72 tests
(`docs/igi-re-parity/ANIMATION.md`). But `anim_rules.h` has **no caller** outside its
own TU — the viewport does not use it, and the F10 panel still only plays/pauses.

**Next action.** Drive the preview from `anim_rules` (evaluate pose → skin → draw),
then extend the existing F10 panel with frame-step and speed. Note `ANIMATION.md`
§12.4 records that the shipped `FORM` sizes are wrong in retail data — reproduce
that, do not "fix" it.

---

### A8. #74 — QVM-driven in-game menu *(write side landed; draw/navigate wiring still open)*

**What the issue asks.** Parse `menusystem/ingamemenu.qvm`, draw the real screens,
navigate them, **edit them and save back to the `.qvm`**, with a hand-drawn fallback.

**Oracle, verified.** `MenuManager_Init` `0x4170A0` (`src/game/menumanager.c:98`),
`MenuManager_Create` `0x417260` (`:160`), `MenuManager_RequestScreen` `0x4174A0`
(`:224`), `MenuManager_ReadInput` `0x418580`, `MenuManager_OpenScreen` `0x418710`,
`MenuManager_GetString` `0x418EA0` (`src/ui/ui.h:61`).

**State.** Partially landed: `42a689bb` (menu built from the retail definition),
`41d4d33a` (labels from the language `.RES`), documented in `MENU_SYSTEM.md`,
`MENU_UI.md` and `MENU_TEXT_RES.md` — all three marked **complete** for the
containers and the reader. `source/menu/menu_ui.h:423` loads
`game_root/menusystem/ingamemenu.qvm`, with a fallback when it is absent.

The **write** half landed in `3b6605d5` (`source/menu/menu_qvm_writer.*`) and is now
verified and reachable: `MenuQvmWrite` reproduces both retail containers **byte for
byte** (11308 and 25549 bytes, zero differing bytes), the result passes the engine's
loader rules (`Qvm_Load` HOOK 0x4BD220, `src/qvm/qvm.c:433-486`), it decodes through
this repository's own reader to the shipped file's structure field for field, and an
edit survives save + reload. `app_menu::SaveEditorMenu` (`source/menu/menu_ui.h`,
`source/app_menu.cpp`) is the edit-action entry point; it refuses a fallback menu, so
the retail file cannot be overwritten by the editor's own rows. Tests:
`MenuQvmWrite` (8, `tests/test_menu_qvm.cpp`) and `MenuUiSave` (3,
`tests/test_menu_ui.cpp`). See `MENU_SYSTEM.md` §11.

**What remains.** The issue's first two acceptance criteria: the *live* pause menu is
still the hand-drawn one (`source/runtime/pause_menu_style.h`); `LoadEditorMenu` is
reached only by tests, so no UI tree edits `EditorMenu::definition` yet. Acceptance
criterion 3 ("the game loads the modified menu") cannot be run on this host — the
strongest verified claim is byte-identity plus the engine's loader validation and the
repository's reader round-trip.

**Next action.** Wire `LoadEditorMenu`/`SaveEditorMenu` into the pause menu and give
the editor options screen a menu-tree edit action that mutates `EditorMenu::definition`
through the mutable accessors in `source/menu/qvm_image.h`.

---

### A9. #72 — lightmap bake button + HDR *(partial; the button already exists)*

**What the issue asks.** A "Calculate Lightmaps" button that bakes per-object atlases
from the sun plus placed/inferred lights into `<level>/lightmaps_baked/`, plus an
HDR accumulation mode.

**Oracle, verified.** `Lightmap_GetResPath` `0x5210E0` (`src/litmap/lightmap.c:101`)
— the resource names are the literals `MISSION:lightmaps` and
`MISSION:lightmaps/lightmaps.res`. `Lightmap_FreeBuffers` `0x521040`
(`src/litmap/lightmap.c:87`; also declared `src/dirlight/lightmapinfo.c:52`).
`Lightmap_BuildLod` `0x523D40`
(`src/dirlight/lightmapbuild.c:445`) names the output `%s/%s_%05d.olm`
(`src/dirlight/lightmapbuild.c:59`). `LightmapInfo_MakeName` `0x5235A0`
(`src/litmap/lightmap.c:257`) allocates the `obj%05d` slot.

**State.** Partially landed, and the issue's premise is **stale**: the button exists.
`source/renderer/renderer_draw.cpp:1836` and `:2041` build the label;
`source/app_input_mouse.cpp:560` handles the click; `source/app_editor.cpp:430-488`
runs a staged progress flow. `aa225a6e`/`273bb24f` ported the container, accept test
and bake inputs into `source/runtime/lightmap_rules.*` (`LIGHTMAP_BAKE.md`).

**What remains.** (1) The button recalculates existing `.olm` files through the
`igi1conv` converter rather than baking new atlases from lights — `LIGHTMAP_BAKE.md`
§5 records four places where the editor disagrees with the oracle's accept/staleness
rules. (2) `lightmap_rules.h` has no caller. (3) The HDR/tonemap mode is not started
and, per #52, has no retail counterpart.

**Next action.** Decide whether the editor's bake is *the engine's* rebake (then wire
`lightmap_rules` and fix the four divergences) or a *new* tool (then stop citing the
oracle for the output format and say so). Do not conflate the two.

---

### A10. #70 — editor sound system *(partial; the shipped path is not the port)*

**What the issue asks.** Port a preview-grade version of a reference audio stack:
retail sound-bank loading, WAV PCM decode, ambient loops, a sound-test panel.

**Oracle, verified.** The authored-data side is `ConditionalSound_Init` `0x424E80`
(`src/vehicle/condsound.c:94`) and `AmbientArea_Init` `0x44C130`
(`src/aiaction/ambientarea.c:81`), with the parameter schema in
`docs/igi-re-parity/SOUND.md` §3.

**State.** Partially landed, and the two halves are different things:

* `aa225a6e`/`273bb24f` ported the authored emitters and the decision logic into
  `source/runtime/sound_emitter.{h,cpp}` — **no caller** outside its own TU.
* What actually plays is `source/runtime/audio_system.{h,cpp}` (232 lines), a
  `PlaySoundA` wrapper (`#pragma comment(lib, "winmm.lib")`, `audio_system.cpp:90`)
  wired at `source/app.cpp:139`, `source/app_level.cpp:21` and
  `source/runtime/gameplay_host.cpp:122-136`. It is **not** the SoundBank/ILSF/OpenAL
  port the issue asks for.

**Next action.** State the backend choice explicitly (winmm vs OpenAL) as the issue
allows, then wire `sound_emitter` so the authored emitters drive the shipped backend;
add the sound-test panel and the ambient loop behind a default-off toggle.

---

### A11. #60 — helicopter rotor + flight preview *(partial)*

**What the issue asks.** Match retail helicopter motion in the preview: rotor
spin rates/directions, authored collective/thrust semantics, and the tick-0 reset.

**Oracle, verified.** `Heli_Control` `0x431B70` (`src/heli/heli.c:406`, body
`:358-405`) — the tick-0 reset and the `0xBF800000` throttle sentinel are in
`HELICOPTER.md` §3; `Heli_Move` `0x431E30` (`src/heli/heli.c:520`); the rotor magic
object is `Rotor_Init` `0x42D2B0` (`src/rotor/rotor.c:211`).

**State.** Partially landed, and the rotor half **is** wired — but not through the
ported module. `source/renderer/renderer_objects_atta.cpp:1263-1276` classifies
`TASKTYPE_ROTOR` attachments and post-multiplies `igi::IgiRotorSpinMatrix`
(`source/renderer/atta_children.h`, which cites `src/rotor/rotor.c:139-152,180-181`)
about the rotor's local X axis, with an editor-chosen preview rate of 15 rad/s.
Separately, `273bb24f` ported the Heli task's control contract and motion into
`source/runtime/heli_rules.{h,cpp}` with `tests/test_heli_rules.cpp`
(`docs/igi-re-parity/HELICOPTER.md`) — and that module has **no caller**.

**Next action.** Feed the preview from `heli_rules` (the control sentinel, tick-0
reset, speed ramp, three-axis turn forces, nose-pitch post-multiply) and read the
authored "Original Thrust"/"Speed" values — note `HELICOPTER.md`'s load-bearing
finding that they are start-speed and start-velocity respectively, not thrust and
speed. The rotor spin already matches the oracle and needs no change.

---

### A12. #79 / #69 — road and track splines *(partial)*

**What the issues ask.** #79: an editor data model and authoring workflow for
`SplineObj` road/track objects, with the longitudinal axis derived from each
segment's extent and no hardcoded model id or axis. #69: replace chord-direction
segment orientation with the engine's Hermite-based placement.

**Oracle, verified.** `SplineObj_Open` `0x502250` (`src/editobj/spline.c:225`, body
`:177-225`); `SplineObjWaypoint_Create` `0x5008F0` (`src/editobj/spline.c:49`);
`SplineObjWaypoint_Changed` `0x500EF0` (`src/editobj/spline.c:51`);
`SplineObjSegment_Setup` `0x4E4530` (`src/sound/segsetup.c:366`, declared
`src/tagitem/spline.c:37`). The longitudinal axis comes from the segment model's own
extent (`ROAD_SPLINES.md` §4.2), which is exactly what #79 asks for — so #79's
"do not hardcode an axis" requirement has a verified oracle.

**State.** Partially landed. `273bb24f` ported the authored model, the clamps, the
authoring helpers and the Hermite basis into `source/runtime/spline_rules.{h,cpp}`
with `tests/test_spline_rules.cpp` (`docs/igi-re-parity/ROAD_SPLINES.md`), which
states plainly: "the port is not wired into a draw path". `spline_rules.h` has no
caller. Separately, `#59`'s rail work landed
(`source/renderer/spline_geometry.cpp`, `renderer_splines.cpp`), and
`renderer_splines.cpp` still orients segments by chord direction.

**Next action.** Point `renderer_splines.cpp` at the ported Hermite evaluation when
waypoints carry authored orientations, and take segment length from
`SplineObjSegment_Setup`'s extent-derived span. Then add the authoring UI for #79.

---

### A13. #43 — lights and smoke FX *(partial; no retail point-light type)*

**What the issue asks.** Authoring tools for point/spot/directional lights and smoke
emitters, plus lightmap baking.

**Oracle, verified.** Smoke: `Smoke_Init` `0x437510` (`src/fxobj/smoke.c:194`),
`Smoke_Create` `0x437B90` (`src/fxobj/smoke.c:248`), `Smoke_Move` `0x437AD0`
(`src/fxobj/smoke.c:210`) — 21 authored parameters. The closest retail *light* type
is `AlarmLight_Init` `0x4397C0` (`src/fxobj/alarm.c:124`).

**State.** Partially landed. `aa225a6e` ported the smoke emitter, the alarm-light
state machine and the reflector blink into `source/runtime/smoke.{h,cpp}` and
`source/runtime/alarm_light.{h,cpp}` (`docs/igi-re-parity/SMOKE_LIGHTS.md`). Neither
has a caller outside its own TU, and there is no authoring/inspector surface.

**Honest gap.** I.G.I. level data declares **no point, spot or directional light**
task type — there is no `PointLight`/`SpotLight` param table in IGI-RE. The issue's
Part 1 storage requirement ("must match what the IGI engine expects so exported
levels load correctly") therefore **cannot be satisfied as written**; only the smoke
half and the bake half have retail counterparts. `AlarmLight` is a task, not a
general light.

**Next action.** Wire the smoke rules into a preview and add the smoke inspector.
For lights, re-scope: either author them as editor-only metadata (and say they do
not export) or drop Part 1 and keep #63/#72's lightmap-driven approach.

---

### A14. #80 — intro/outro cutscenes *(partial)*

**What the issue asks.** Support for intro and outro cutscenes.

**Oracle, verified.** The retail cutscene is the `CutScene` task driving a chain of
`EditCamera` keys: `CutScene_Open` `0x4F4B00` (`src/nodereg/tasktypes.c:30`),
`CutScene_CollectKeys` `0x4F4880`, `CutScene_GetKey` `0x4F4910`,
`CutScene_UnwrapAngles` `0x4F4940`, `CutScene_Update` `0x4F5400`,
`CutScene_Spline` `0x4F6290`, and `EditCamera_Init` `0x507F10`
(`src/editcam/editcamera.c:110`).

**State.** Partially landed. `273bb24f` ported the timeline rule (flatten, clamp,
unwrap, window, interpolate, fade, letterbox, colour mix, shake) into
`source/runtime/cutscene_rules.{h,cpp}` with `tests/test_cutscene_rules.cpp` and
`docs/igi-re-parity/CUTSCENES.md`. The module has **no caller**. `CUTSCENES.md`
also corrects the earlier triage: the `MenuManager_*` entry points cited for #80 were
a red herring, and the `.avi` `IntroScreen` player is deliberately out of scope.

**Next action.** Wire `cutscene_rules` into a camera/preview path so the 120 authored
camera keys in the three cutscene levels can be scrubbed; decide separately whether
`.avi` playback is in scope (it needs a decoder the editor does not have).

---

## B. Issues with no retail counterpart

### B1. #52 — modern post-processing, HDR tone mapping, motion blur

**What it asks.** ACES/AgX tone mapping, 3D colour LUT grading, camera motion blur
and DoF, sub-tick pose interpolation, terrain detail LOD morphing.

**Oracle.** **None.** `grep -i` over `symbols/names.tsv` for `PostProcess`, `PostFx`,
`Bloom`, `Tonemap`, `MotionBlur` returns **0 matches each**. Retail `IGI.exe` is a
fixed-function Direct3D 8-era renderer; it has no post-process pass to mirror, and
no deferred G-buffer.

**State.** Not started. This is engine design, not parity. It must be labelled as
such in the issue and in any PR, or the repository's own evidence standard is
violated by citing "parity" for it.

**Next action.** Either implement from its own spec and relabel it
`[FEATURE]`, or close it. If #72's HDR mode is implemented, it inherits the same
"no oracle" status.

---

### B2. #51 — 3-layer lightmap and shadow pipeline

**What it asks.** Three layers: baked UV2 lightmaps, hybrid baked+sun, and real-time
shadows from dynamic entities.

**Oracle.** Partial and only for the parts that exist. `MeshHeader.format`
(`src/model/modeltypes.h:12`) is `0` rigid, `1` bone, `3` lightmapped — a real
retail mode set, but not the issue's three. `fill_refs`
(`src/sector/sector.c:544-563`) reserves one lightmap ref per LOD, and
`SectorObject_LoadLightmaps` (`0x4DC3D0`, `src/sector/sector.c:681`) loads the set.
There is **no** retail "3-layer" pipeline and **no** dynamic-shadow pass for moving
entities; the retail lightmap is a baked hemicube product
(`src/litmap/hemirender.c`).

**State.** Partially landed. The editor's `Baked`/`Hybrid`/`Dynamic` modes ship
(`source/renderer/object_lightmap.h:11-13`), the cycle is wired to the menu row
(`source/renderer/object_lightmap.cpp:38`,
`source/app_input_mouse.cpp:559`), and the modulation intensities differ per mode
(`source/renderer/renderer_objects.cpp:1242-1243`). Layer 3 — dynamic entities casting
real-time shadows — is not implemented and has no oracle.

**Next action.** Keep the shipped mode toggle; split layer 3 out as a separate
engine feature and stop describing the three modes as retail parity.

---

### B3. #50 — split `renderer_draw.cpp`, add docs

**What it asks.** Split the god-file into UI/picking/draw, add an architecture doc and
`CONTRIBUTING.md`.

**Oracle.** **None needed** — a pure refactor. The issue's own acceptance criterion
is "no behaviour change", which is a test claim, not an engine claim.

**State.** Not started, and the file grew: `source/renderer/renderer_draw.cpp` is now
**4220** lines (the issue measured 3383). Object picking *was* extracted to
`source/renderer/renderer_objects_picking.cpp`, so that third is done.
`CONTRIBUTING.md` and `docs/ARCHITECTURE.md` do **not** exist (`ls` returns
"No such file or directory" for both).

**Next action.** Extract the UI half of `renderer_draw.cpp` into a
`renderer_ui.cpp`, then write the two docs. Because this is a pure refactor, the
gate is the existing suite plus a smoke run — not IGI-RE citations.

---

### B4. #49 — roadmap: configurator, scrubbing, Neo Remastered

**What it asks.** Three unrelated items: a weapon/item configurator, animation
scrubbing, and Neo Remastered compatibility.

**Oracle.** The animation-scrub item maps to `Anim_Play` `0x4D61D0`
(`src/soldier/soldier.c:59`) and is already tracked under #73/#62. **"Neo Remastered"
has no oracle at all** — it is a third-party mod, not retail `IGI.exe`, so no
IGI-RE claim can be made about its formats.

**State.** Tracking issue, not work. The scrubbing item is covered by #73; the
configurator and Neo items are not started.

**Next action.** Split into three issues. Keep the Neo item explicitly outside the
IGI-RE evidence standard, with whatever format documentation the mod ships as its
only source.

---

### B5. #7 — AI & waypoint editor

**What it asks.** A visual waypoint graph editor (place/connect/delete nodes typed
PATROL/GUARD/ALERT/COVER), exported to "the IGI Graphs/Nodes format documented by
contributors".

**Oracle — corrected.** The earlier triage said "no IGI-RE navigation symbols under
those names". That is true but incomplete, and it is worth being precise because the
distinction changes the work:

* `grep -i "waypoint" symbols/names.tsv` returns 15 matches, and **every one** is a
  `SplineObjWaypoint_*` spline-authoring symbol (`SplineObjWaypoint_Create`
  `0x5008F0`, `SplineObjWaypoint_Changed` `0x500EF0`, `SplineObjWaypoint_GetParams`
  `0x502F90`, …). None is navigation.
* `NavMesh` and `Nav_` return **0 matches**. There is no nav-mesh in IGI-RE.
* The retail NPC path system is the **`Patrol*` family**: `PatrolPath_Open`
  `0x453550` (`src/patrol/path.c:153`), `PatrolPath_Create` `0x453630`,
  `PatrolPathCommand_Open` `0x4533B0` (`src/patrol/path.c:86`),
  `PatrolPathCommand_Create` `0x453470`, `PatrolRoute_Current` `0x453720`
  (`src/aitask/aitask.c:153`), `PatrolRoute_Next` `0x453730`, `PatrolRoute_Advance`
  `0x453760`, `PatrolPath_Find` `0x453840`, consumed by `AIAction_Patrol` `0x44D0E0`
  (`src/aiaction/aiaction.c:390`).

So there **is** a retail NPC-path system with authored, nameable paths — but it is a
path/command list attached to AI actions, not a typed navigation graph, and the
issue's proposed `WaypointType { PATROL, GUARD, ALERT, COVER }` and its "IGI
Graphs/Nodes format" have no reader in IGI-RE.

**State.** Not started.

**Next action.** Re-scope the issue as a **`PatrolPath`/`PatrolPathCommand` editor**
over the retail task types (which the editor already parses), and drop the invented
node-type enum and graph file format, or keep them explicitly as editor-only data.
Verify against `PatrolPath_GetParams` `0x453670` and
`PatrolPathCommand_GetParams` `0x453480` before designing any UI.

---

### B6. #54 / #56 — the gameplay umbrellas

**What they ask.** #54: a Windows-only gameplay runtime (fixed 30 Hz scheduler,
isolated runtime world, player controller, bounded QVM interpreter, one mission
success/failure path) with a playable vertical-slice validation. #56: the handoff
status of that work, listing ~40 % completion and seven remaining items.

**Oracle.** Neither decomposes into one entry point. The subsystems each have their
own oracles, already cited elsewhere in this directory (`door_params.h`,
`pickup_rules`, `heli_rules`, `sound_emitter`, …). No single function answers either
issue.

**State.** Partially landed, and #56's body is **stale** relative to the tree:
`source/runtime/gameplay_host.cpp`, `gameplay_spawn.cpp`, `gameplay_window.cpp`,
`runtime_session.cpp` and `simulation_scheduler.cpp` all exist and are compiled,
`audio_system` is wired into the gameplay host, and #56's "no Windows toolchain"
blocker still stands for the *verification* half only.

**Next action.** #54/#56 cannot be closed from this checkout — the acceptance gates
require a Windows build and a playtest. Re-baseline #56's percentage against the
current tree before reusing it as a status report, and keep both as tracking issues.

---

### B7. #63 — infer light fixtures from emissive geometry

**What it asks.** Port a `LightFixtureExtractor` that surveys emissive textures and
infers lamp positions from where they are drawn, producing an editable candidate
list.

**Oracle.** **`LightFixtureExtractor` is an open-igi class name and does not exist
in IGI-RE** (`grep -i "LightFixture" symbols/names.tsv` → 0 matches). IGI-RE's only
concrete light-task type is `AlarmLight_Init` `0x4397C0` (`src/fxobj/alarm.c:124`),
which is a lamp with an on/off state machine, not a general fixture. The issue's own
premise — "I.G.I. declares no point lights in level data" — is **confirmed**: there
is no point-light task type in the oracle.

**State.** Not started. The inference is new tooling, not a port; only the lightmap
side (#72) has an oracle.

**Next action.** Implement it as an editor feature and label it as such. The honest
inputs are the retail `.olm` lightmap pages (whose format *is* oracle-backed — see
`LIGHTMAP_BAKE.md`) plus emissive texture brightness; do not cite IGI-RE for the
inference rule itself, because there is no such rule in retail code.

---

## C. Corrections to issue premises, collected

These are places where the issue text would send an implementer in the wrong
direction. Each is verified above.

| Issue | Premise in the issue | What IGI-RE says |
| --- | --- | --- |
| #37 | Euler order is `YXZ`; MEF pivot offset causes wall clipping | `Rz·Ry·Rx` (`Matrix_SetEuler` `0x4B38E0`, `src/math/matrix.c:99-110`); the pivot claim is refuted (`TRANSFORM_PIPELINE.md` §3) |
| #68 | Ladder activation radius is 8192.0 units | `q.range = kUseRange = 6.0f` (`src/humanbase/human.c:168,1288`); 8192.0 is that pair of words read as a `double` |
| #68 | Facing tolerance is the cosine `0.70710678` | `kLadderAngle = π/4` compared against `Vec3_Angle`'s returned **angle** (`src/props/ladder.c:157`, `src/plane/gear.c:157`); the literal is absent from IGI-RE |
| #75 | Pickup radius is "the same half-width constant as the ladder activation" | Both are 2867.2, but they are independent constants (`kMinTouch` `src/comprun/pickup.c:159` vs `kReachMax` `src/props/ladder.c:200`) — do not unify them |
| #57 | Snow is a separate system to port (`SnowRenderer`) | Snow is the rain task's mode flag: `r->snow = rain == 0`, `Rain_New` `0x498960`, `src/dxinit/rain.c:144-153` |
| #62 | Every LOD level contributes an `.olm` entry to `lightmaps.res` | The one-ref-per-LOD reservation is for the hemicube **bake** (`fill_refs` `src/sector/sector.c:544-563`); the editor has no `LightmapSet`, so this is non-applicable here (`LOD_CHAINS.md` §5.1) |
| #47 | `XTVC0/ECFC0` vs `XTVC1/ECFC1` selects render partitions | Those chunks are **collision** data; partitions come from `REND`/`DNER` (`Direct3DRender_LoadModel` `0x49FB10`, `src/dxinit/d3drender.c:82`) |
| #73 | Port `MefSkinner` | `MefSkinner` is an open-igi class name; the retail equivalents are `BoneMesh_Skin` `0x49B700` (`src/dxinit/d3drender.c:313`) plus `Anim_Play` `0x4D61D0` |
| #63 | Port `LightFixtureExtractor` | The class name does not exist in IGI-RE; the inference is new tooling |
| #7 | `Waypoint*` navigation symbols exist | All 15 `Waypoint` matches are `SplineObjWaypoint_*` spline authoring; there is no nav-mesh. The retail NPC path system is `PatrolPath*`/`PatrolRoute*` |
| #74 | The menu is "hand-drawn approximations" | Read/draw/navigate come from the retail definition (`42a689bb`, `41d4d33a`) and the write-back now round-trips byte for byte (`3b6605d5` + tests, `MENU_SYSTEM.md` §11); the **live pause menu** is still the hand-drawn one |
| #72 | The "Calculate Lightmaps" button does not exist | It exists and is wired (`renderer_draw.cpp:1836`, `app_input_mouse.cpp:560`, `app_editor.cpp:430-488`); what it does is recalculate `.olm`, not bake atlases |
| #43 | Light data must be stored "in the format the IGI engine expects" | No point/spot/directional light task type exists in IGI-RE; only `AlarmLight` and `Smoke` are authored tasks |
| #80 | `MenuManager_*` are the cutscene entry points | They are a menu screen switcher; the cutscene is `CutScene_*` (`0x4F4B00`) + `EditCamera_*` (`0x507F10`) |
| #56 | "no Windows toolchain, ~40 % complete" | The runtime sources exist and compile; only the Windows verification gate is blocked |

---

## D. Recommended fix order (top 5)

Ordered by (verified oracle + user-visible effect + small remaining delta).

1. **#75 pickups** — `touches` `src/comprun/pickup.c:180`, `GenericPickup_Touch`
   `0x46C630` `:366`, `GunPickup_Touch` `0x46D230` `:616`. Rules and schemas are
   already ported and tested; only `source/renderer/renderer_objects_visual.cpp`
   needs the box path replaced and the gizmos added.
2. **#62 per-frame LOD switch** — `lod_select` `src/lod/mesh3d.c:161-208`,
   `ModelObj_GetLOD` `0x4CFCB0` `src/lod/mesh3d.c:209-227`. Measured: 60.3 % of the
   6605 retail model names have a chain longer than one level, so this is not an
   edge case. The only blocker is the per-LOD lightmap binding decision.
3. **#57 snow (+ #58 rain)** — `Rain_New` `0x498960` `src/dxinit/rain.c:167`,
   `RainEffect_Init` `0x436780` `src/fxobj/rain.c:84`. The weather mode and
   `is_snow` flag are already wired; the ported `R7` rules and the missing
   `snow.tex` bind are the remaining delta, and the divergence list is already
   written.
4. **#74 menu live wiring** — `MenuManager_Create` `0x417260`
   `src/game/menumanager.c:160`, `MenuManager_GetString` `0x418EA0`
   `src/ui/ui.h:61`. The reader is complete and documented and the write-back now
   round-trips byte for byte with tests (`MENU_SYSTEM.md` §11); what is left is
   pointing the pause menu at `LoadEditorMenu` and giving the options screen an edit
   action that calls `SaveEditorMenu`.
5. **#73 animation playback** — `Anim_Play` `0x4D61D0` `src/soldier/soldier.c:59`,
   `Human_PlayAnim` `0x489EA0` `src/soldier/soldier.c:180`, `BoneMesh_Skin`
   `0x49B700` `src/dxinit/d3drender.c:313`. 72 tests already pin the ported rules;
   the F10 panel and the viewport are the only missing consumers.

Runner-up: **#60** (`Heli_Control` `0x431B70` `src/heli/heli.c:406`) — same shape of
work as #73, one module and one consumer.

---

## E. Verification notes

* **What was verified.** Every IGI-RE citation above was resolved by grepping
  `symbols/names.tsv` for the address and then reading the `HOOK`/`ORIG`/`FN`
  declaration and the function body in `src/`. Every editor claim was checked
  against `source/`, the named commit, or the named parity document on this
  worktree. The "has a caller" test was `grep -rn '#include "…"' source/`.
* **What was not verified.** No frame was rendered (the host offers GL 2.1, the
  editor needs 4.1). No build and no test suite was run — the Lead owns the gate,
  and builds would race with other writers. The Windows suite was not run. Where a
  parity document itself marks a claim ⏳/unverified, this triage repeats that mark
  rather than upgrading it.
* **Addresses that do not resolve, and why.** `0x4E4530` is defined twice in IGI-RE:
  `HOOK(0x4E4530, SplineObjSegment_Setup)` at `src/sound/segsetup.c:366` and an
  `ORIG` declaration of the same name at `src/tagitem/spline.c:37`. Both are cited
  where relevant. `0x46C980` is defined at `src/comprun/pickup.c:392-449` and
  declared at `src/ammotype/gun.c:63`; the definition site is cited.
* **Names that do not exist in IGI-RE** (issue text cites them as if they were
  retail symbols): `MefSkinner`, `LightFixtureExtractor`, `Snow` (any symbol),
  `Waypoint*` (navigation), `RailroadQTaskID`, `PostProcess*`, `Tonemap`,
  `MotionBlur`, `Bloom`, `NavMesh`. Verified absent by `grep -i` over
  `symbols/names.tsv`.
