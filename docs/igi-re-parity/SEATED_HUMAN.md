# The seated human: a human the level authors inside a vehicle (issue: "jones sitting")

Parity record for the user report, verbatim:

> some human in the vehicle like jones sitting is upside not exactly on their exact
> position so you need to fix that as well

Every rule below is cited to IGI-RE (the verified decompilation of the retail
`IGI.exe`, `/Users/haseeb-mir/Documents/Code/IGI-RE`, read-only) as
**function + HOOK address + file:line**; every editor claim is cited to a file in
this repository.

> Status legend — **verified** = confirmed against IGI-RE and/or the shipped game
> data with a command whose output is quoted; **unverified** = cannot be checked
> in this environment (the GUI needs OpenGL 4.1 and this host offers 2.1, so no
> viewport screenshot exists).

---

## 1. What the shipped levels actually author

A passenger/driver is an ordinary `HumanSoldier` task **nested inside the vehicle
task** (or inside a container inside it). Read from the decompiled scripts the
editor itself writes (`<IGI>/MISSIONS/location0/<level>/objects.qsc`):

| level | task | authored position | model | Bone Heirachy | Stand Animation | parent |
| --- | --- | --- | --- | --- | --- | --- |
| LEVEL1 `objects.qsc:1159` | `HumanSoldier "Jones drive truck"` | (24957688, -56093496, 174419120), Gamma 3.1415929 | `000_01_1` | 1 | **61** | `ConditionalContainer` (the truck `Car 1099` is its sibling) |
| LEVEL1 `objects.qsc:926` | `HumanSoldier` (Jones in the heli) | (22683652, -60990340, 174936032), Gamma 0 | `000_01_1` | 1 | -1 | **`Heli 1502`** |
| LEVEL4 `objects.qsc:1041` | `HumanSoldier "GI"` (×5, "Harrison & Men in Heli") | e.g. (111716104, -54603588, 175401152), Gamma 0.5236 | `021_01_1` | 0 | **243** | `ConditionalContainer` inside **`Heli 800`** |

Two facts follow, and both matter:

* the **Stand Animation** of a seated human is a *sit* clip (61 on skeleton 1;
  240/243 on skeleton 0 — measured: clip 61's skinned mesh spans −0.64 m to
  +0.83 m about the root and its lowest foot bone sits 0.72 m *forward* of it,
  i.e. the legs are folded forward), and the editor auto-plays exactly that clip
  (`App::LoadLevel` → `ComputeAnimationIdsForObject` → `GetClipByAnimId`), which
  is why the user saw Jones *sitting*;
* the authored Z is ~1.1 m above the truck's origin and 0.2–0.45 m below the
  helicopter's origin — seat heights, not feet heights.

---

## 2. The engine's rule: where a human's object origin is

**A human's object origin is the SKELETON'S ROOT (the pelvis); the mesh hangs
below it.** Three independent places say so:

1. **The pose.** `BoneObj_Update` HOOK 0x4D38C0 (`src/boneobj/boneobj.c:371-419`)
   poses the tree with `pos = {0, 0, 0}` (`:376`) under the object's matrix, and
   `BoneObj_UpdateBone` HOOK 0x4D3A90 (`:423-525`) computes
   `b->pos = parentPos + parentMatrix * b->offset` (`:434-439`). The root bone's
   `offset` is TLST[0] = (0, 0, 0) for every shipped human skeleton
   (`BoneObj_Load` HOOK 0x4D67A0, `:1570-1576`; measured on 000.IFF / 001.IFF /
   006.IFF). So bone 0 sits **at** the object's origin.
2. **The draw.** `BoneDynCubeObj_EnterHandler` HOOK 0x5124C0
   (`src/glowmesh/bonedynd3d.c:36-61`) draws with `obj->pos` / `obj->matrix`
   (`:41-42`), and `BoneMesh_Skin` HOOK 0x49B700 (`src/dxinit/d3drender.c:259-264`)
   computes `Bone::matrix * v + Bone::pos` — the skinned vertices are in that same
   frame, so the model's pivot→feet lift is *below* the origin, never at it.
3. **The floor.** `Human_ProbeGround` HOOK 0x462390
   (`src/humanbase/human.c:1674-1857`) ends with
   `to[2] = foot[2] + hits[0].pos.z` (`:1816`) where
   `foot = Human_GetBonePos(buf, h, 0)` (`:1693`) — the object origin is the floor
   **plus bone 0's animated height**. That height is the clip's root position
   track: the shipped stand clips carry ≈ 3877–3990 raw units (0.95–0.97 m) and
   the shipped sit clips ≈ 2329 raw units (0.57 m) — a seated pelvis height.
   This is also why the engine's position track is clip-dependent at all.

The authored parameters themselves are only a hint: `Human_ParamChangedHandler`
HOOK 0x45F890 (`src/humanbase/human.c:448-461`) sends `ObjectPos` to the
collision cube (`DynCube_Move(Game_GetWorld(), h, h->placePos, h->radius)`, `:457`)
and takes only the **rotation** from the authored `Gamma`
(`Matrix_SetRotZ(h->matrix.m, h->yaw)`, `:460`).

**A human inside a vehicle is carried, never re-based.** `Vehicle_NewParts`
HOOK 0x4F4800 (`src/vehbase/vehicle.c:251-256`) builds `v->attached` from the
MODEL's ATTA list only (`RigidDynCubeObj_CreateMagicObjs`), so a script child
never receives the vehicle's move event; what keeps a passenger in place is the
ground probe above (the vehicle's own collision floor) plus
`Human_UpdatePlatformMove` HOOK 0x460CF0 (`src/humanbase/human.c:1190-1238`) and
the platform branch of `Human_UpdateBody` HOOK 0x4610C0 (`:1316-1365`), which
takes the body's rotation from the platform's matrix.

**There is no "seat" transform for a level-placed human.** The only seat
composition in the whole engine is the player taking a car's gunner seat —
`HumanPlayer_Use34Enter` HOOK 0x40CB90 (`src/humanstate/humanstate.c:868-900`):
`Car_GetGunnerPos` HOOK 0x42FDF0 (`src/heli/car.c:320-328`) = `Model_GetPoint(pos,
car, 8)` + `car.pos`, plus `side * kUse34Side(-2048) + up * kUse34Up(1433.6)`
rotated by the car's matrix (`mat[1],mat[4],mat[7]` = its local Y column,
`mat[2],mat[5],mat[8]` = its local Z column), with the yaw from
`Matrix_GetEuler`. Nothing in the shipped data uses it to *place* a task.

---

## 3. What the editor did wrong

`ParseMefGeometry` re-bases every human mesh to the **feet**:
`SkinSkinnedVertices` sums the rig's bone world positions into each vertex
(`source/renderer/mef_native.cpp:1122-1129`), and that rig's root pivot is
`(0, 0, 3990.4)` raw units (`RigRootOriginRaw`, `source/renderer/mef_native.h`;
`source/renderer/hardcoded_bones.h`). `App::SnapObjectsToTerrain`
(`source/app_view.cpp`) then drops a human so those feet land on the terrain —
and **that pair is self-consistent**, which is what the user validated when the
AI-below-ground defect was fixed (`source/renderer/posed_draw_transform.h`,
commit `5d277f21`).

The pair never runs for a passenger:

* a human inside a vehicle is not on the terrain (`DecideTerrainSnap` keeps it,
  `source/level/terrain_snap_rules.h:96-114`), and
* `SnapObjectsToTerrain` skips the Jones model outright
  (`if (obj.modelId == "000_01_1")`, `source/app_view.cpp:517-519`).

So the authored position keeps the **engine's** meaning — the object origin —
while the editor drew the mesh's **feet** there. The measured consequence, for
the shipped placements (metres, model frame):

```
STATIC bind-pose bake   z 0.002 .. 1.851     (feet at ~0, head at 1.85)
engine skinned sit pose z -0.636 .. 0.833    (feet 0.64 below the root, head 0.83 above)
rig root origin         (0, 0, 3990.4 raw) = 0.974 m
```

so the seated human was drawn **one pivot→feet lift (3990.4 raw units = 0.974 m)
above its seat** — standing through the vehicle's roof. That is the user's "not
exactly on their exact position", and (reading "upside" as *up above*) the "upside".

---

## 4. The fix

`source/renderer/human_seat_frame.h` (new, host-portable) holds the one decision:

* `IsVehicleTaskType` / `IsHumanTaskType` / `HumanSitsInVehicle(objects, index)` —
  a bounded walk of the `parentIndex` chain; true only for a **human** under a
  **vehicle**;
* `StaticModelOriginMetres(rig, engine_origin_frame)` and
  `PosedModelOriginMetres(rig, engine_origin_frame)` — the model-origin term each
  draw path must apply. In the seat frame the static path subtracts the bake's
  root pivot and the posed path adds nothing, so **both** put the model's ORIGIN
  (the pelvis) at `pos`, which is where the engine puts it.

Wired at three places, all of which must agree or the pick/gizmo would disagree
with the pixels:

| site | file | what it does |
| --- | --- | --- |
| posed draw | `source/app.cpp` (`PosedObjectMatrix(..., seat_frame)`) | no rig-origin term for a passenger |
| static draw | `source/renderer/renderer_objects.cpp` | `-rig_origin` model-local translation for a passenger |
| picking | `source/renderer/renderer_objects_picking.cpp` (both passes) | the same term, so the click lands on the drawn body |

`PosedObjectMatrix` (`source/renderer/posed_draw_transform.h`) gained a defaulted
`engine_origin_frame` parameter, so every existing caller and test keeps its
meaning.

### A second, separate defect found in the same path: the engine-matrix conversion

`IgiEngineMatrixToGlm` / `IgiEngineMatrixToGlm4` (`source/renderer/atta_children.h`)
were the **identity on the array**, i.e. the TRANSPOSE of the engine's product:
they mapped the local X axis to `(m[0], m[1], m[2])`. The engine's own product is
the array's first **column**, `(m[0], m[3], m[6])`:

* `Mesh3D_GetWorldVertex` HOOK 0x4D0A50 (`src/lod/mesh3d.c:434-436`):
  `r0 = z*m[2] + y*m[1] + x*m[0]` — row 0 dotted with the vector;
* `part_place` (`src/vehicle/parts.c:81-90`) and `Heli_Control`
  (`src/heli/heli.c:390-392`) — the same form;
* `HumanPlayer_Use34Enter` (`src/humanstate/humanstate.c:880-882`) — the local
  offset `(0, side, -up)` becomes `side*column1 - up*column2`;
* `Direct3D_MakeMatrix` HOOK 0x4950F0 (`src/d3d/direct3d.c:1211-1237`) — sets the
  D3D row-vector matrix's first ROW to `(rot[0], rot[3], rot[6])`, and D3D9
  applies `v * M`, so that row *is* the image of X.

The identity conversion drew the placed-helicopter flight preview with the
**inverse** of the engine's rotation (and composed its ATTA children — the rotors
— against that wrong root), which is exactly the "upside down" class of failure.
`IgiAttaLocalRotation` in the same header already transposed (the ATTA record's
columns are its child frame's rows, `src/magicobj/rigidcoll.c:419-436`), so the
two conventions could not both be right; the conversion now matches the child
composition, and `tests/test_atta_children.cpp` pins that they agree.

**This changes what the viewport draws for a previewed helicopter.** It is cited
to six engine sites and to the editor's own `spline_geometry::ToGlm`
(`source/renderer/spline_geometry.cpp:102-120`), which transposes correctly and
is what places trains on their rails today — but it is a rendered-output change
this environment cannot look at (no GL 4.1), so it is reported as **verified by
construction, unverified on screen**.

---

## 5. Verification status

**Verified in this environment**

* The engine's origin rule — `BoneObj_Update` 0x4D38C0, `BoneObj_UpdateBone`
  0x4D3A90, `BoneDynCubeObj_EnterHandler` 0x5124C0, `Human_ProbeGround` 0x462390,
  `Human_ParamChangedHandler` 0x45F890, `Vehicle_NewParts` 0x4F4800 (all cited
  above with line numbers).
* The shipped placements and clips — the `objects.qsc` lines in §1 and the cached
  BEF clips (`<IGI>/cache/anims/001/001_anim_061.BEF`,
  `<IGI>/cache/anims/000/000_anim_243.BEF`).
* The differential test — `tests/test_seated_human.cpp`, 3 tests:
  `OnlyAHumanUnderAVehicleIsAPassenger` (the rule, including a parent cycle),
  `TheSeatFramePutsTheModelOriginAtThePlacement` (both frames, both paths, and
  the 3990.4-unit defect the seat frame removes), and
  `TheShippedPassengersSitUprightAtTheirAuthoredPosition` (the two shipped
  passengers through the renderer's own matrix + the engine's own pose: the
  pelvis at the authored position, the body upright, its height a human's, and
  the feet-frame negative control > 3900 units away).
* The engine-matrix conversion — `tests/test_heli_preview.cpp` (X/Y/Z images
  against the stored array, plus the 4x4 form) and
  `tests/test_atta_children.cpp::ThePreviewRootConversionAgreesWithTheChildComposition`
  (the preview root conversion and the ATTA child composition agree, and the
  child frame still matches the literal IGI-RE transcription).

### Gates run (all on the content above, in this worktree)

| gate | command | result |
| --- | --- | --- |
| build (i686) | `cmake --build build-mingw --target igi_tests igi-editor --parallel` | `BUILD_EXIT=0` (213 steps, `bin/igi_tests.exe` + `bin/Release/igi1ed.exe` relinked); `scratch/seated-human-build.log` |
| build integrity | `bash scripts/check-build-integrity.sh` | **PASS** — 397 objects consistent (one stale `renderer_objects_texture.cpp.obj` with `#deps 0` was recompiled first, see below) |
| host lane | `IGI_HOST_NO_CACHE=1 bash scripts/run-host-tests.sh` | 87 suites / 1583 tests: my 4 suites pass; 14 failures in `test_menu_qvm`, `test_menu_ui`, `test_pause_menu_style`, `test_qvm_roundtrip` — **another agent's in-flight menu work in this shared tree** (none of those suites includes any file this change touches). Re-run with those four skipped: **83 suites / 1453 tests / 0 failed** |
| e2e (headless) | `IGI_E2E_STAGE=$PWD/scratch/e2e-veh bash scripts/e2e-headless.sh --fast` | **PASS (exit 0)** — 7 assertions, 0 failed; the pinned level-7 `s332_02_1` retail defect intact |
| perf guard | `bash scripts/perf-guard.sh` | **PASS (exit 0)** — F1 0.0 reads/frame ≤ 2; F2 52 µs/object ≤ 200; F3 pick/visual 1.26 ≤ 3.0; F5 poses 13 ≤ 2×13; H1 2502 reads ≤ 2803 |
| Windows suite (i686 under Wine) | `wine bin/igi_tests.exe '--gtest_filter=SeatedHuman.*:AttaChildren.*:HeliPreview.*:SkinnedStaticParity.*'` | **37 tests, all PASSED, exit 0** (`scratch/seated-human-winsuite.log`) |
| Windows suite (full) | `wine bin/igi_tests.exe '--gtest_filter=-*VerifyLevel*'` | the same menu suites fail (MenuQvm / MenuUiRetail / MenuUiLive / MenuUiSave / MenuTextRes / PauseMenuRetailStyleCorpus) — the other agent's work, not this change |

The integrity gate first reported one problem, unrelated to this change but
fatal to a gate run: `CMakeFiles/igi-editor.dir/source/renderer/
renderer_objects_texture.cpp.obj` had `#deps 0` in the ninja dependency log (the
exact signature `scripts/check-build-integrity.sh` documents) and was older than
a header another agent had just added. Deleting that single object and rebuilding
`igi-editor` (one TU + link, `scratch/seated-human-rebuild.log`, `REBUILD_EXIT=0`)
restored a fully consistent tree — reported here because a gate result on the
mixed tree would have been meaningless.

**Unverified (cannot be checked here)**

* **The rendered result.** The editor needs OpenGL 4.1 and this host offers 2.1,
  so "the seated human now sits on the seat and looks upright in the viewport" is
  *not* proven by this work. What is proven is that the matrix handed to both
  draw paths is the engine's composition.
* **Where the engine would put the passenger's Z on the frame it is drawn.** The
  engine takes it from a collision query (`Human_ProbeGround` 0x462390 → the
  vehicle's floor + bone 0's animated height). The editor has no collision query
  at all, so it keeps the authored Z — which is the level author's own seat
  placement — and only stops re-basing it. A vehicle whose floor moves relative
  to the authored seat (a flying helicopter) still leaves its passengers behind,
  because the editor has no platform-move simulation; that is a *different*
  feature (the helicopter preview, `docs/igi-re-parity/HELICOPTER.md`) and is not
  claimed here.
* **The `Car_GetGunnerPos` seat rule** (`HumanPlayer_Use34Enter` 0x40CB90) is
  documented in §2 for completeness but is **not** used to place anything: the
  engine only applies it when the player *enters* a car, and no shipped task
  authors it. No editor code was changed for it.
