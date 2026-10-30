# Wire width + interactables (#42): wire / zipline / stationary machinegun

**Status**: implemented, applied and unit-tested. The two renderer hunks that live
in `source/renderer/renderer_objects.cpp` (outside this change's write scope) were
supplied as an exact patch and applied by the Lead at `:845-882` and `:891-892`.
Everything marked ⏳ is not verified yet — see
[Verification status](#verification-status).

**Oracle**: `/Users/haseeb-mir/Documents/Code/IGI-RE` (read-only). Every constant
and rule below cites its `file:line`. Nothing here comes from web research or
from guesswork about what the editor "should" do.

**Scope**: this document covers the two items of shared task `task-3`

| Item | Source |
| --- | --- |
| (a) Wire width | `docs/KNOWN_ISSUES.md:13-15` — "📏 Wire Width" |
| (b) Stationary machinegun + zipline not placeable/configurable/previewable | GitHub issue #42 |

---

## 0. TL;DR

1. **A `Wire` in IGI is not a line, it is a stretched rigid model.** The game puts
   the object at the midpoint of two authored anchors, points its local **+Y** axis
   along `end - start`, and sets `obj.scale[1] = |end - start| * (1/4096)` —
   `Wire_ParamsChanged` (0x43F3D0), `src/props/wire.c:160-191`.
2. **The editor never did that.** It drew the wire model unstretched at the start
   anchor with a uniform `40.96 * obj.scale` — `source/renderer/renderer_objects.cpp:814,846`.
   That, not any line-width limit, is why wires "render with incorrect
   thicknesses". The root cause stated in `docs/KNOWN_ISSUES.md:15`
   ("platform line-width rendering limits … shader-based variable wire width
   scaling") is **not supported by the oracle**; every `glLineWidth` call in the
   editor is in `source/renderer/renderer_draw.cpp` debug/overlay drawing
   (`:1237,:1242,:1291,:1304,:1348,:1370,:1377,:1439,:1483,:1490`) and is unrelated
   to wire props.
3. **There is no "zipline" task type and no zipline property block.** A zipline is
   a `Wire` whose slope exceeds 2° — `Wire_IsSloped` (0x43F8A0, `src/props/wire.c:304-317`)
   is exactly what `HumanPlayer_StartAction` case 9 tests to choose `ziplineEnter`
   over `wireEnter` (`src/humanstate/humanstate.c:958-963`). Exposing the wire's
   second anchor is therefore the whole of the zipline half of issue #42.
4. **The editor's `Wire` schema was missing the second anchor** (`End`, args 6-8),
   so a wire could not be authored or previewed at all. Fixed.
5. **`StationaryGun` and `AIStationaryGunHolder` were both mis-schematised** as
   generic `pos/ori/model` objects, so weapon, fire arc, ammo, detection angles,
   range and the AI expressions were neither parsed nor editable. Fixed.
6. **`Fence` is already correct** — it is a rigid model rotated about Z
   (`Fence_ParamsChanged` 0x43FEA0, `src/props/fence.c:206-215`), *not* a stretched
   wire. Do not "fix" fence thickness. Only its electric-fence condition
   expression (arg 8) was unexposed; added.

---

## 1. (a) Wire width — IGI-RE evidence

### 1.1 Wire_Open / Wire_ParamsChanged

| Oracle | Address | file:line |
| --- | --- | --- |
| `Wire_Open` | 0x43EFF0 | `src/props/wire.c:89-113` |
| `Wire_Create` | 0x43F1F0 | `src/props/wire.c:115-131` |
| `Wire_ParamsChanged` | 0x43F3D0 | `src/props/wire.c:160-191` |
| `Wire_GetMatrix` | 0x43F820 | `src/props/wire.c:289-301` |
| `Wire_IsSloped` | 0x43F8A0 | `src/props/wire.c:304-317` |
| `Wire_GetUseKind` (= 9) | 0x43F6C0 | `src/props/wire.c:228-232` |

Parameter table, in `ParamTable_Add` order (`src/props/wire.c:109-111`):

| QSC arg | Param type | Field |
| --- | --- | --- |
| 3,4,5 | `Param_ObjectPos` | `start` |
| 6,7,8 | `Param_ObjectPos` | `end` |
| 9 | `Param_String16` | `model` |

`Wire_ParamsChanged` (`src/props/wire.c:160-191`) is the authoritative behaviour:

```c
len = Vec3d_Length(&d);            // |end - start|
...
Matrix_SetYAxis(&m, &f);           // wire.c:177   local +Y := normalise(end - start)
w->obj.matrix = m;
w->obj.scale[1] = len * kWireScale; // wire.c:179  ONLY Y is scaled
...
mid = (start + end) / 2;           // wire.c:184-186
radius = VirModel_GetRadius(w->obj.model) * max_scale(&w->obj);  // wire.c:187-188
DynCube_Move(w->obj.world, w, &mid, radius);
```

### 1.2 Constants, units, curves

| Constant | Value | Meaning | file:line |
| --- | --- | --- | --- |
| `kWireScale` | `0.000244140625` = 1/4096 | world units → metres; `ALIAS(kUnitsToMeters, kWireScale, 0)` | `src/props/wire.c:320-321` |
| `kWireSlope` | `0.034906585f` = 2° | `Wire_IsSloped` threshold | `src/props/wire.c:323` |
| `kNearlyUp` | `0.95f` | `Matrix_SetYAxis` branch selector (`\|dir.z\| > kNearlyUp`) | `src/game/misc.c:99,136` |

**Wire "width"/thickness**: there is **no** distance- or length-dependent wire
width in the game, and no `glLineWidth` anywhere in the wire path. A wire's visible
thickness is the model's own cross-section times `obj.scale[0]` / `obj.scale[2]`,
which the wire code never writes — only `scale[1]` is set (`src/props/wire.c:179`).
What *does* scale with length is the object's **bounding radius**:
`VirModel_GetRadius(model) * max_scale(&obj)` (`src/props/wire.c:122-123,187-188`),
and `max_scale` is dominated by the stretched Y, i.e. by `length/4096`.

Worked values (editor scale convention `40.96 = 4096/100`, MEF geometry in cm):

| Authored wire length | `obj.scale[1]` (game) | editor Y scale `40.96 * length/4096` | X/Z scale |
| --- | --- | --- | --- |
| 4096 wu (1 m) | 1.0 | 40.96 | `40.96 * obj.scale` |
| 1 m × 30 | 30.0 | 1228.8 | unchanged |
| 100 m | 100.0 | 4096.0 | unchanged |

### 1.3 `Wire_IsSloped` — the exact rule, including its quirk

```c
Wire_GetMatrix(w, &m);          // src/props/wire.c:289-301
Matrix_GetEuler(&m, &ax, &ay, &az);   // 0x4B3C50, src/math/matrix.c:181-208
a = ax;
if (ax < kZero) a = -a;
return a > kWireSlope;          // wire.c:315
```

`Matrix_GetEuler` normalises its output into `[0, 2π)` (`src/math/matrix.c:198-204`
adds `TWO_PI` to any negative component). Consequences, all reproduced faithfully
by `TaskSchemaNS::Props::ComputeWireSpan` and asserted in the tests:

| Wire elevation | reported `slope` | `isZipline` |
| --- | --- | --- |
| 0° (level) | 0 | **false** |
| +1.9° uphill | 0.03316 rad | **false** |
| +2.1° uphill | 0.03665 rad | true |
| +45° uphill | 0.7854 rad | true |
| −45° downhill | 5.4978 rad (= 2π − 45°) | true |
| ±80° (steep) | 1.5708 rad (= 90°) | true |

Two quirks are *deliberate* and are **not** bugs to be fixed:

* a downhill wire reports `2π − elevation`, not `−elevation` — the classification
  (`|slope| > 2°`) is still the same as a magnitude test;
* for `|dir.z| > 0.95` (≈72°) `Matrix_SetYAxis` takes its other branch
  (`src/game/misc.c:136`) and the X-Euler saturates at 90°.

### 1.4 What the editor does today

| Where | Behaviour | file:line |
| --- | --- | --- |
| Load | `Wire` branch parses only args 3,4,5 (`pos`) and 9 (`modelId`); args 6-8 are kept in `argTokens` but never interpreted | `source/level/level_objects.cpp:123,158,289-297` |
| Load | `Wire` counted as a "missing generic" task | `source/level/level_objects.cpp:140` |
| Draw | `model = translate(pos) * Rz*Ry*Rx * scale(40.96*obj.scale)` — **uniform scale, no wire stretch, no midpoint, no +Y alignment** | `source/renderer/renderer_objects.cpp:814,823-846` |
| Draw | bound radius uses `glm::length(mesh.halfExtents) * 40.96f * max(obj.scale, 0.001f)` | `source/renderer/renderer_objects.cpp:793` |
| Save | `Wire` case writes tokens 3,4,5 and 9 only — args 6-8 survive verbatim because an unmodified object saves its raw `qscLine` | `source/level/level_objects_serialize.cpp:397-401` |
| Schema | `Wire` had `Position@3` and `Model@9` only — **no `End`** | `source/level/task_schema.cpp` (pre-change), now `:86-89` |

### 1.5 What this change does

* `source/level/task_schema.cpp:86-89` — `Wire` schema gains `End` (args 6,7,8).
  Declared as **`Real64x3`**, the type `ObjectPos` aliases
  (`src/physobj/paramops.c:185`, and both use `Param_Three`, `paramops.c:149-150`),
  **not** `ObjectPos`, because `App::ApplyPropPositionDrag`
  (`source/app_input_mouse.cpp:957-1040`) treats *every* `ObjectPos` field as the
  object's own position and writes `obj.pos` — a second `ObjectPos` field would
  drag the *start* anchor instead of the end one. `Real64x3` renders as three
  labelled numeric boxes (`source/renderer/renderer_draw.cpp:2605-2612`) and both
  the scrub-drag and the text commit write `argTokens[6..8]`
  (`source/app_input_mouse.cpp:925`, `source/app_editor.cpp:807`).
* `source/level/task_schema.cpp:7-13` — `TypeArgCount` now counts `Real64x3` as 3.
* `source/level/level_objects.cpp:673-689` — `LevelObject::WireAnchors()` resolves
  both anchors on demand from `argTokens` (so an inspector edit previews
  immediately), preferring the live `pos` for the start anchor.
* `source/level/task_schema.cpp:325-353` — `ComputeWireSpan()` ports
  `Matrix_SetYAxis` + `Matrix_GetEuler` + `Wire_IsSloped`, returning midpoint,
  direction, length, `yScale = length * kWireScale` and `isZipline`.
* `source/level/task_schema.cpp:355-365` — `YAxisBasis()` exposes the game's
  rotation as three world axis directions.
* `source/renderer/renderer_objects_visual.cpp:804-957` — `DrawPropsGizmos()` draws
  the span, both anchors, and (for a sloped wire) the downhill ride arrow plus the
  rider hang offset `kZiplineDown` below the `kWireEnd`-inset entry point.

**Applied by the Lead** in `source/renderer/renderer_objects.cpp`: the uniform
scale was replaced by the game's midpoint + `YAxisBasis` + per-axis scale
(`Y = 40.96 * length/4096`, X/Z unchanged) at `:845-882`, and
`DrawPropsGizmos(obj, ubo_mats, ubo_binding_point_);` was added after
`glUniformMatrix4fv(loc_model, ...)` at `:891-892`. The wire branch builds its
matrix from identity, so it bypasses the plain `T*R*S` chain entirely — correct,
because `Wire_ParamsChanged` overrides the object's placement
(`src/props/wire.c:177-189`).

---

### 1.6 Two editor-side behaviours resolved (Wire task, follow-up campaign)

The shipped level script declares the wire itself, and that runtime declaration is
what `TaskSchemaNS::GetSchema("Wire")` returns (registered schemas take precedence
over builtins). It is verbatim:

```
Task_DeclareParameters("Wire", "Start position", "ObjectPos",
                       "Stop position", "ObjectPos", "Model", "String16");
MISSIONS/location0/level1/objects.qsc:69
```

**(a) The runtime declaration masked the fixed `End` row — fixed.** §1.5 added the
builtin `Wire` schema's `End` field as `Real64x3` precisely so
`App::ApplyPropPositionDrag` (source/app_input_mouse.cpp) and
`App::CommitPropTextEdit` (source/app_editor.cpp) — which bind every `ObjectPos`
field to `obj.pos` — could not write the stop anchor into the start anchor's args
3-5. But the level's own declaration re-declared `"Stop position"` as `ObjectPos`,
so the row that was actually loaded was the broken one; the builtin was never
consulted on level 1. `TaskSchemaNS::ParseDeclaration` (source/level/task_schema.cpp)
now demotes every `ObjectPos` after the first to `Real64x3`, the type the engine
aliases it to (`src/physobj/paramops.c:185`; both use `Param_Three`,
`paramops.c:150`). The token count and offsets are unchanged (3 start, 3 stop,
1 model — `Wire_Open` HOOK 0x43EFF0, `src/props/wire.c:108-111`); only the editor
widget changes, from a position pad that writes `obj.pos` to three numeric boxes
that commit to `argTokens[6..8]`. The first `ObjectPos` of any declaration is
untouched, so all other task types keep their position pad.

**(b) "Snapping Wire to original QSC Z" — removed.** The log line came from
`App::SnapObjectsToTerrain` (source/app_view.cpp), which included `obj.isWire` in a
group that "restored" `obj.pos.z = obj.original_pos.z` whenever the object was not
modified. There is no such engine step, and there is no Z parameter on a Wire:

* `Wire_Open` HOOK 0x43EFF0 (`src/props/wire.c:89-113`) installs three parameters
  only: start `ObjectPos`, end `ObjectPos`, model `String16`.
* `Wire_Create` HOOK 0x43F1F0 (`wire.c:115-131`) only adds the task to the world's
  dynamic-cube list; it never calls `Coll_Drop` or any terrain query.
* `Wire_ParamsChanged` HOOK 0x43F3D0 (`wire.c:160-191`) derives the object's
  position as `(start + end) / 2` and its matrix from the anchor direction. That is
  the only thing that ever sets it.

The restore was also a no-op on a freshly parsed level (`obj.pos.z` already equals
`original_pos.z`, and the branch skips the snap below it), so the log claimed a
snap that neither the engine nor the editor performed. The branch now skips a Wire
outright and leaves both authored anchors untouched.

**What the wire model itself contains — there is nothing to spawn.** Measured on
the shipped `level1` copy of `320_01_1`
(`MISSIONS/location0/level1/MODELS/models/320_01_1.mef`, 1792 bytes, md5
`aa70a4125ee4a6c4a48edd05713192ba`, the same bytes inside `LEVEL1.RES` /
`LEVEL1_unpacked`):

* the `MESH` chunk's payload is 156 bytes, `attach_count` at +0x7E is **0**;
* the `ATTA` chunk is present but its header size word is **0** — no records;
* the render stream is `XTRV` — **18 vertices**, stride 32 — and `DNER` is one
  group of 24 indices (8 triangles), `primType` (RD3D +0x00) = **4**
  (D3DPT_TRIANGLELIST). The editor's format-0 path parses exactly this
  (`ParseRigidVertices` + `WalkGroups`, mef_native.cpp), so the rod is drawable;
* the render vertices span local `y = -2048 .. +2048` (1 m at 4096 units/m), a
  narrow prism with `|x| ≤ 54.91`, `z ≤ 127.99` world units;
* `MVTX` (the "model points" `Model_GetPoint` reads — the 16-byte records with a
  trailing magic type) holds **2 points**, at local `y = -2048` and `y = +2048`,
  `x ≈ 17.5847`, `z ≈ 121.514`. Those are the two ends `Wire_GetMatrix`
  (`wire.c:289-301`) and `Wire_Use` (`wire.c:200-226`) address as points 0 and 1.

So the wire's ±2048 Y extent is exactly what `obj.scale[1] = len/4096` maps onto
the two anchors. `Wire_Create` never calls `RigidDynCubeObj_CreateMagicObjs`
(contrast `PhysicsMagicObj_Create`, `src/editobj/physmagic.c:79-83`), so the engine
draws one model and spawns no posts, insulators or extra pieces. "Wire missing
models" can only be the editor failing to draw that one model, never a missing
child; §1.5's stretch implements the engine's placement, and the smoke run
resolves the model (§6).

**(c) "AI rotation override (horizontal only)" — not in the current tree; it was
already removed and its absence is correct.** The engine builds a human's transform
from its authored yaw alone: `Human_SetYaw` HOOK 0x463220
(`src/viewcam/human.c:328-335`) stores `baseZ`/`rotZ` and calls `Matrix_SetRotZ`;
`Human_ParamChangedHandler` HOOK 0x45F890 (`src/humanbase/human.c:449-462`) does the
same for a parameter change. There is no Alpha/Beta term on a human and no
horizontal-only override anywhere in the oracle. The removed loop also mutated the
document (it wrote `rot.x`/`rot.y`/`rot.z`, which `UpdateCoordinatesInLine` saves),
so replaying it would corrupt authored data. The code now carries only the note at
source/app_level.cpp:433-461; `grep -rn "rotation override" source/` matches
comments only.

## 2. (b) Stationary machinegun — IGI-RE evidence

### 2.1 `StationaryGun` (the player-mannable machinegun)

`StationaryGun_Open` 0x473ED0, `src/statgun/statgun.c:126-151`; param table
`src/statgun/statgun.c:142-150`:

| QSC arg | Param type | Field | Default | Range |
| --- | --- | --- | --- | --- |
| 3,4,5 | `Param_ObjectPos` | `basePos` | — | — |
| 6,7,8 | `Param_Real32x9` | `baseMatrix` (3 Euler angles, `ParamReal32x9_Parse` 0x4EFBA0) | — | — |
| 9 | `Param_String16` | `model` | — | — |
| 10 | `Param_String32` | `weapon` | `"WEAPON_ID_M2HB"` (`:116`) | — |
| 11 | `Param_Degrees` | `maxPitch` | `25.0f` (`:117`) | — |
| 12 | `Param_Degrees` | `minPitch` | `-25.0f` (`:118`) | — |
| 13 | `Param_Degrees` | `yawRange` | `90.0f` (`:118`) | — |
| 14 | `Param_Int32` | `ammo` | `-1` (`:115`) | `gStatGunAmmoRange = {0, 100000}` (`:140-141,524-525`) |

Runtime rules:

* `StationaryGun_Clamp` 0x4748B0 (`src/statgun/statgun.c:274-300`) clamps pitch to
  `[minPitch·kDegToRad, maxPitch·kDegToRad]`, and clamps yaw to `±yawRange·kDegToRad`
  **only while** `yawRange·kDegToRad < 2π` (i.e. `< 360°`); at 360° the yaw is free.
* yaw is a rotation about the base's **local Z** — `Matrix_SetRotZ(&o->obj.matrix,
  o->outYaw)` (`src/statgun/statgun.c:241`).
* pitch is the **X-Euler** composed on top — `Matrix_SetEuler(&e, o->outPitch, 0,
  π)` (`src/statgun/statgun.c:255`).
* the muzzle/fire direction is **model-driven**: `Model_GetPoint(&p, o, idx)` with
  `idx = 1` (`src/statgun/statgun.c:246-252`) fed into `Weapon_Move`. There is no
  fixed local "forward" axis constant.
* unmanned slew rate `kTurnStep = 0.08726647f` = 5°/tick (`src/statgun/statgun.c:178,183,521`);
  recoil fade `kRecoilFade = 0.9f` (`src/statgun/statgun.c:172-173`);
  `kEyeUp = 819.2f` = 0.2 m (`src/statgun/statgun.c:264-266`).
* occupancy: `StationaryGun_Attach` 0x474EE0 / `Release` 0x475340 / `IsUsed`
  0x475370; `UseQuery` 0x474E30 offers use kind **6** only when the human is on the
  gun's local +Y side.

### 2.2 `AIStationaryGunHolder` (the AI fixed gun) — a *different* task type

`GunHolder_Init` 0x46E370, `src/comprun/gunholder.c:157-165`:

| QSC arg | Param type | Field | Default |
| --- | --- | --- | --- |
| 3,4,5 | `Param_ObjectPos` | `pos` | — |
| 6,7,8 | `Param_Real32x9` | `matrix` | — |
| 9 | `Param_String16` | `model` | `""` (`:125`) |
| 10 | `Param_Degrees` | `angleX` | `360.0f` (`:124`) |
| 11 | `Param_Degrees` | `angleY` | `360.0f` (`:125`) |
| 12 | `Param_Real32` | `range` | `10.0f` (`:126`), range `{0, 100}` (`:155-156`) |
| 13 | `Param_VarString` | `activeExpr` | `0` |
| 14 | `Param_VarString` | `teamExpr` | team `1` (`:127`) |

The authored args map **straight onto the AI view cone**
(`src/comprun/gunholder.c:228-230`):

```c
t->view.maxPitch = t->angleX * kDegToRad;   // arg 10 -> pitch limit
t->view.maxYaw   = t->angleY * kDegToRad;   // arg 11 -> yaw limit
t->view.range    = t->range * kRangeScale;  // arg 12 -> world units, kRangeScale = 4096.0f (:102)
```

So `range` is authored in **metres** and is multiplied by 4096 to reach world
units — a directly citable radius, unlike the `StationaryGun` case.

### 2.3 What the editor did / does

| Where | Behaviour | file:line |
| --- | --- | --- |
| Load | `StationaryGun` and `AIStationaryGunHolder` were in the `isMissingGeneric` list | `source/level/level_objects.cpp:140` |
| Load | generic branch reads pos/ori/model and nothing else | `source/level/level_objects.cpp:400-410` |
| Schema | both fell into the generic `pos@3, ori@6, model@9` bucket — weapon, arc, ammo, angles, range and expressions invisible and uneditable | `source/level/task_schema.cpp:110-117` (pre-change) |
| Draw | no gizmo of any kind for either type | — |

### 2.4 What this change does

* `source/level/task_schema.cpp:177-186` — real `StationaryGun` schema (8 fields,
  `Degrees` for the three angles, `Int32` for ammo).
* `source/level/task_schema.cpp:195-203` — real `AIStationaryGunHolder` schema
  (8 fields, `Degrees` ×2, `Real32` range, `VarString` ×2), removed from the
  generic bucket.
* `source/level/task_schema.cpp:429-469` — `ParseStationaryGunArgs` /
  `ParseGunHolderArgs`: read args 10-14, fall back to the game's own defaults for
  any missing parameter (never to 0), strip token quotes, and report `valid=false`
  when there is no position block.
* `source/level/level_objects.cpp:691-699` — `LevelObject::StatGunParams()` /
  `GunHolderParams()` type-gated accessors.
* `source/renderer/renderer_objects_visual.cpp:879-956` — `DrawPropsGizmos()`
  draws, for a `StationaryGun`: the base frame triad, the yaw sweep in the base's
  local XY plane (`Matrix_SetRotZ`), the pitch limits as the X-Euler-rotated local
  +Y, and — for an `AIStationaryGunHolder`: the detection cone at
  `range * 4096` world units with `angleY` yaw and `angleX` pitch.

The gun gizmo's *reference lengths* (`kGizmoRefLen = 4096` = 1 m, the anchor arm
and arrow sizes) are **presentation choices**, not oracle constants: the game has
no gizmo and derives its fire direction from the model's muzzle point
(`src/statgun/statgun.c:249`), which the editor resolves in its mesh cache, not in
this function. The angles, planes and the `range * 4096` radius *are* oracle-backed.

---

## 3. Zipline — IGI-RE evidence

**A zipline is a `Wire` whose slope exceeds 2°.** There is no zipline task type, no
zipline param table, and no zipline property block anywhere in the oracle.

| Oracle | Address | file:line |
| --- | --- | --- |
| `HumanPlayer_StartAction` case 9 → `Wire_IsSloped` picks `ziplineEnter` vs `wireEnter` | 0x40CD30 | `src/humanstate/humanstate.c:958-963` |
| `HumanPlayer_ZiplineEnter` | 0x40DE90 | `src/humanstate/humanstate.c:1374-1387` |
| `HumanPlayer_ZiplineUpdate` | 0x40DEF0 | `src/humanstate/humanstate.c:1389-1400` |
| `HumanPlayer_UpdateZipline` (ride direction) | 0x411F20 | `src/weapon/humanplayer.c:518-589` |
| `HumanPlayer_UpdateWire` (hand-over-hand) | 0x411AE0 | `src/weapon/humanplayer.c:465-516` |

Ride direction (`src/weapon/humanplayer.c:557-568`): take the wire model's point 0
→ point 1 vector, **negate it when `d.z > 0`** so the ride always runs downhill,
then normalise into `h->ziplineDir`.

Speed (`src/humanstate/humanstate.c:1394-1398`), per tick:

```
speed = -ziplineDir.z * 88.30976104736328 + speed * 0.99
vel   = speed * ziplineDir
```

| Quantity | Value | Unit |
| --- | --- | --- |
| acceleration constant `0x1.613d32p+6` | `88.30976104736328` | world units / tick |
| friction | `0.99` | per tick |
| terminal speed | `8830.976… * \|dir.z\|` = `2.1560… * \|dir.z\|` | world units / tick = m / tick |
| worked case, 45° descent (`\|dir.z\| = 0.7071`) | `6244.4430881554` = `1.5245…` | wu/tick = m/tick |
| `kWireEnd` (unrideable stub at each anchor) | `3072.0` = 0.75 | wu = m (`src/weapon/humanplayer.c:136`) |
| `kZiplineDown` (rider hangs this far below the wire) | `3686.4` = 0.9 | wu = m (`src/weapon/humanplayer.c:138`) |
| `kWireDown` (hand-over-wire variant) | `2150.4` = 0.525 | wu = m (`src/weapon/humanplayer.c:137`) |

The editor now derives `isZipline` and the downhill ride direction from the two
authored anchors (`ComputeWireSpan`, `ZiplineRideDirection`) and previews both.

---

## 4. Fence — verified already correct

`Fence_Open` 0x43F900 (`src/props/fence.c:100-121`) declares `pos` (`ObjectPos`),
`angle` (`Angle`, a **Z rotation**), `model` (`String16`), `cond` (`VarString`).
`Fence_ParamsChanged` 0x43FEA0 (`src/props/fence.c:199-212`) applies
`Matrix_SetRotZ(angle)` — a fence is a rigid object rotated about Z and is **never
stretched**. The editor already matches this (`source/level/level_objects.cpp:377-383`,
`source/level/level_objects_serialize.cpp:331-336`), so nothing about fence
thickness was changed. Only the unexposed condition expression was added
(`source/level/task_schema.cpp:145-149`).

Fence constants for reference (`src/props/fence.c:304-310`): `kSparkBase = 1.5`,
`kSparkScale = 0.00012207031f`, `kShockHeight = 12288.0` (3.0 m),
`kShockSide = 1228.8` (0.3 m), `kUseAngle = 1.0471976f` (60°).

---

## 5. Files changed

| File | Change |
| --- | --- |
| `source/level/task_schema.h` | `Props` namespace: constants, `Vec3`, `WireSpan`, `Basis`, `StationaryGunParams`, `GunHolderParams`, declarations (`:56-201`) |
| `source/level/task_schema.cpp` | `TypeArgCount` + `Real64x3` (`:7-13`); `Wire` `End` (`:86-89`); `Fence` `Condition` (`:145-149`); `StationaryGun` (`:157-186`); `AIStationaryGunHolder` (`:188-210`); helper implementations (`:271-470`) |
| `source/level/level_objects.h` | `WireAnchors` / `StatGunParams` / `GunHolderParams` (`:88-102`); `DrawPropsGizmos` declaration (`:104-112`) |
| `source/level/level_objects.cpp` | accessors (`:673-699`) |
| `source/renderer/renderer_objects_visual.cpp` | `DrawPropsGizmos` + gizmo helpers (`:670-957`) |
| `tests/test_props_wire_interactables.cpp` | 29 host tests (new) |
| `CMakeLists.txt` | `tests/test_props_wire_interactables.cpp` added to `igi_tests` (`:315`) |

`source/renderer/renderer_objects.cpp` (Lead's file) now carries the two applied
hunks: `:845-882` (wire stretch) and `:891-892` (gizmo call).

### 5.1 Follow-up campaign (§1.6)

| File | Change |
| --- | --- |
| `source/level/task_schema.cpp` | `ParseDeclaration` demotes every ObjectPos after the first to `Real64x3` (the engine alias) so the level's own `Wire` declaration cannot mask the builtin `End` row; nearby `paramops.c` line citations corrected to `:185` (alias) / `:150` (Param_Three) |
| `source/app_view.cpp` | `SnapObjectsToTerrain`: Wire is skipped on its own, with the engine citations; the dead "restore original QSC Z" and its misleading log are gone |
| `tests/test_props_wire_interactables.cpp` | +3 tests: retail Wire declaration, first-ObjectPos guard, three shipped level-1 wire spans |
| `docs/igi-re-parity/PROPS_WIRE_INTERACTABLES.md` | this section + verification status |

No file was added to `CMakeLists.txt`: the test file was already registered.

---

## 6. Verification status

Proved:

* **Unit tests pass.** `IGI_HOST_TESTS=tests/test_props_wire_interactables.cpp bash scripts/run-host-tests.sh`
  → `[  PASSED  ] 29 tests.` / `OK: 29 tests passed in 1 suites` / exit 0. The
  follow-up campaign (§1.6) added three tests — the retail `Wire` declaration, the
  "first ObjectPos stays" guard, and the three shipped level-1 wire spans — and the
  suite is now `OK: 32 tests passed in 1 suites`, exit 0.
* **Compiles for every target that builds each TU** — `python3 tools/igi-re-parity/tu-check.py <file>`
  replays the real cross-build commands from `build-mingw/compile_commands.json`
  (i686-w64-mingw32-g++): `task_schema.cpp` OK ×3 (igi-editor, igi_tests, igi_mcp),
  `level_objects.cpp` OK ×2, `renderer_objects_visual.cpp` OK ×1,
  `renderer_objects.cpp` OK ×1 — `checked N translation unit(s), 0 failed` in each case.
  (Header-only files have no compile-command entry of their own; they are covered by
  the TUs that include them.)
* **The whole host suite passes on the merged tree** (at the time this document was
  written): `bash scripts/run-host-tests.sh` → `OK: 343 tests passed in 32 suites`,
  exit 0. On today's tree the full lane is NOT green for reasons unrelated to these
  files: 5 suites fail on missing fixtures / Windows-style corpus paths
  (`test_qvm_roundtrip`, `test_dat_parser`, `test_graph_parser`,
  `test_cutscene_rules`, `test_sound_rules`) and `test_terrain_ctr_rules` fails to
  compile on `source/level/../common.h:97`. Every host suite that exercises
  `ParseDeclaration` (including `test_declared_position_fallback` and
  `test_spline_task_tree`) passes.
* **Follow-up campaign (§1.6) evidence.** MinGW
  `cmake --build build-mingw --target igi-editor --parallel 1` → exit 0 (compiled
  `app_view.cpp`, `task_schema.cpp`, linked `bin/Release/igi1ed.exe`). Headless
  level-1 smoke under Wine (`--smoke-level --level 1 --smoke-verbose`): 1416 tasks,
  523 placements examined, 85 distinct models, **0 missing**, with
  `320_01_1 (object) <- .../level1/models/level1.res [this level, level 1]` — the
  wire model is resolved from the owning level archive, as `VirModel_Find` does
  (`src/model/model.c`; `Wire_ParamsChanged`, `wire.c:180`).

Not verified (honest list):

* ⏳ **The rendered wire thickness change is not visually verified.** The hunk is
  applied and compiles, and its maths is unit-tested, but no editor session was run
  to look at a stretched wire on screen. Same for the gizmos: the call site exists
  and compiles, but their on-screen geometry is unverified.
* ✅ **Retail QSC data is now validated.** `tests/fixtures/level01_simple.qsc`
  still has no Wire, but the shipped scripts were available all along under
  `MISSIONS/location0/`; §1.6 now pins the three real level-1 wire records
  (`level1/objects.qsc:888-890`) and the retail `Task_DeclareParameters("Wire", …)`
  line (`:69`) in tests. The level is loaded by the shipped binary's smoke mode,
  which resolves `320_01_1` from `level1/models/level1.res`.
* ✅ **MinGW cross build was run** for the follow-up: exit 0 for `igi-editor`.
* ⏳ **No runtime/editor session was run**, so the inspector rendering of the new
  `Degrees` / `String32` / `VarString` / `Real64x3` fields was verified only by
  reading `source/renderer/renderer_draw.cpp:2498-2712`, not by clicking them. In
  particular the fixed Wire inspector — Stop position as three numeric boxes at
  args 6-8 instead of a pad that moves the start anchor — needs the user's eyes.
* ⚠️ **`tools/igi-re-parity/tu-check.py` is currently unreliable**: the PCH in
  `build-mingw/CMakeFiles/{igi-editor,igi_tests}.dir/cmake_pch.hxx` predates these
  header edits and GCC does not reject it, so it reports bogus
  `redefinition of 'struct TaskSchemaNS::FieldDef'`. The compile checks above were
  replayed with `-include source/pch.h` instead of the stale PCH.
* ✅ Resolved: an intermediate `test_mcp_domain_tools` failure was caused by the
  Lead's concurrent `SCamera` schema rewrite (no `Real32x9` field at offset 6, which
  the old test asserted). Reported, then fixed by the Lead; the suite is green again.

---

## 7. Appendix — oracle address index

| Symbol | Address |
| --- | --- |
| `Wire_Open` | 0x43EFF0 |
| `Wire_Create` | 0x43F1F0 |
| `Wire_ParamsChanged` | 0x43F3D0 |
| `Wire_GetMatrix` | 0x43F820 |
| `Wire_IsSloped` | 0x43F8A0 |
| `Fence_Open` | 0x43F900 |
| `Fence_ParamsChanged` | 0x43FEA0 |
| `HumanPlayer_UpdateWire` | 0x411AE0 |
| `HumanPlayer_UpdateZipline` | 0x411F20 |
| `HumanPlayer_StartAction` | 0x40CD30 |
| `HumanPlayer_ZiplineEnter` | 0x40DE90 |
| `HumanPlayer_ZiplineUpdate` | 0x40DEF0 |
| `Matrix_SetEuler` | 0x4B38E0 |
| `Matrix_GetEuler` | 0x4B3C50 |
| `Matrix_SetYAxis` | 0x414A60 |
| `ParamReal32x9_Parse` | 0x4EFBA0 |
| `Param_Three` | 0x4EFB00 |
| `StationaryGun_Open` | 0x473ED0 |
| `StationaryGun_Clamp` | 0x4748B0 |
| `StationaryGun_Aim` | 0x474420 |
| `StationaryGun_Attach` | 0x474EE0 |
| `StationaryGun_Release` | 0x475340 |
| `StationaryGun_IsUsed` | 0x475370 |
| `StationaryGun_UseQuery` | 0x474E30 |
| `GunHolder_Create` | 0x46E210 |
| `GunHolder_Init` | 0x46E370 |

Data: `kWireScale` `DATA(5334B0)`, `kWireSlope` `DATA(533834)`, `gWireType`
`0057C144`, `gWireParams` `0057C148`, `gFenceParams` `0057C150`, `gFenceType`
`0057C154`, `gStatGunAmmoRange` `store_5BE380`, `kTurnStep` `DATA(53387C)`,
`kRecoilFade` `DATA(5335A8)`, `kEyeUp` `DATA(534134)`.
