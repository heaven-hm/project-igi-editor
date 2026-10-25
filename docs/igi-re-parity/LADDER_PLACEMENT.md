# Ladder placement parity (#68): activation volume, mount points, climb line

**Status**: implemented and unit-tested. The viewport gizmos are wired into the
editor's existing "Magic objects" overlay, so they are reachable without touching
`source/renderer/renderer_objects.{h,cpp}` (outside this change's write scope).
Everything marked ⏳ is **not** verified — see
[Verification status](#6-verification-status).

**Oracle**: `/Users/haseeb-mir/Documents/Code/IGI-RE` (read-only, faithful C
decompilation of retail `IGI.exe`; each function carries `HOOK(0xADDR, Name)`).
Every constant and rule below cites its `file:line`. No web sources were used.

**Scope**: shared task `task-9`, GitHub issue #68
(*"Ladder placement parity: activation volume, mount points, climb-line gizmos"*).

---

## 0. TL;DR

1. **A ladder is defined by three magic points, not by a radius.** Magic point 1
   is the activation reference above the ladder, point 2 the foot and point 3 the
   head; point 0 is unused. The shipped ladder MEFs carry exactly four XTVM
   points (`MAGICOBJ.QVM` → 12 `TASKTYPE_LADDER` models, e.g. `400_17_1`).
2. **The activation volume is a band, not a sphere**:
   `|localY| < 8192 ∧ −2867.2 < localX < 2867.2 ∧ origin.z < player.z < point1.z`,
   plus a facing test of `< 45°` between the player's view column 1 and the
   ladder's column 1 (`Ladder_Use`, HOOK `0x4404C0`, `src/props/ladder.c:122-175`).
3. **The 8192 is not an authored constant.** It is `kUseRange = 6.0f`
   (`src/humanbase/human.c:168`) written into a `HumanUseQuery` whose `+0x78`/`+0x7C`
   are an `int` and a `float`, then read back through the `ViewQuery` view where
   `range` is the **double at `+0x78`** (`src/game/game.c:183-192`,
   `src/props/propstypes.h:6-16`). Those two words reinterpret as
   `0x40C0000000000000` = **8192.0** units = 2 m. Issue #68's `8192.0` is correct,
   but its stated reason ("activation sphere radius") is not.
4. **Mount points are fixed offsets on the ladder's own matrix columns**
   (`HumanPlayer_UpdateLadder`, HOOK `0x4113E0`, `src/weapon/humanplayer.c:273-340`):
   bottom `= point2 + col1*1515.52 + col2*3915.776`,
   top `= point3 + col2*3481.6 − col1*1638.4`. The editor already matches this
   (`source/player_ladder.cpp:47-54`).
5. **Rungs are rounded, not truncated**: `rungs = (int)round_ll(h * (1/1228.8))`
   with `h = |point2.z − point3.z|` (`src/props/ladder.c:65-93`). The editor
   truncated → **defect**, fixed here in the parity helper; the runtime copy in
   `source/player_ladder.cpp:23-27` still truncates and is reported to the Lead.
6. **The editor's runtime selection differs from IGI-RE's.** IGI-RE keeps the
   candidate with the smallest angular deviation from the view
   (`UseQuery_Consider`, `src/game/game.c:896-928`); the editor keeps the one
   nearest to the ladder origin (`source/runtime/runtime_world.cpp:1557-1600`).
7. **`source/app.cpp:3126-3138` builds the ladder orientation as `Rz*Rx*Ry`**,
   while IGI-RE's `Matrix_SetEuler` (HOOK `0x4B38E0`, `src/math/matrix.c:99-109`)
   is `Rz*Ry*Rx` → **defect** for rotated ladders (out of scope here; reported).

---

## 1. Where issue #68 disagrees with IGI-RE

| Issue #68 says | IGI-RE says | Followed |
| --- | --- | --- |
| "activation **sphere** of radius 8192" | rectangular band `\|localY\| < 8192 ∧ \|localX\| < 2867.2`, clipped by a **world-Z** slab; the 8192 comes from `kUseRange` reinterpretation | IGI-RE |
| `cos(facing) > 0.7071` | `Vec3_Angle(view_col1, ladder_col1) < kLadderAngle` = `0.7853982f` rad (`src/props/ladder.c:143-157`, `src/file/vec.c:189-208`) | IGI-RE (angle form; the cosine is the equivalent unit-vector test) |
| "activation volume" is one region | **two** independent offers: bottom (`ladder.c:130-162`) and top (`ladder.c:164-173`); the top offer runs even if the bottom one failed and the caller's flag wins | IGI-RE (both modelled separately) |
| mount offsets unstated | `1515.52` / `3915.776` / `3481.6` / `1638.4` units (`src/weapon/humanplayer.c:130-133`) | IGI-RE |
| rung count "height / 1228.8" | `round_ll`, not truncation | IGI-RE |
| "climb line gizmo" | no such thing exists in the game; the drawn line is `point2 → point3` and the **mount-to-mount** path the player actually travels | IGI-RE geometry, editor-only preview |

A note on the 45° gate: the retail constant is a `float`,
`(float)0.7853982 == 0.78539818525314331`, which is a hair **above** `π/4`
(`0.78539816339744828`). A player looking at the ladder from exactly 45° therefore
still passes `!(angle >= kLadderAngle)`. Both the editor's `FacingCosine` test and
this document's helper reproduce that acceptance.

---

## 2. Activation volume

### 2.1 The query the player carries

| Oracle | Address | file:line |
| --- | --- | --- |
| `Human_UpdateUse` builds the use query | — | `src/humanbase/human.c:1269-1300` |
| `kUseRange = 6.0f` | DATA | `src/humanbase/human.c:168` |
| `UseQuery_Consider` | `0x416AD0` | `src/game/game.c:896-928` |
| `ViewQuery` layout (`double range` at `+0x78`, `best` at `+0x80`) | — | `src/game/game.c:183-192` |
| `UseQuery` layout (props view of the same struct) | — | `src/props/propstypes.h:6-16` |
| `Ladder_Use` | `0x4404C0` | `src/props/ladder.c:122-175` |
| `Vec3_Angle` (returns the **angle**, radians) | `0x4B2B30` | `src/file/vec.c:189-208` |

`Human_UpdateUse` sets `q.pos` to the eye, `q.matrix` to the view matrix,
`q.unk70 = q.unk74 = kQuarterPi` (player) / `kPi` (NPC), `q.unk78 = 0` and
`q.range = kUseRange` (`src/humanbase/human.c:1283-1288`). `kUseRange` is `6.0f`,
so the two words at `+0x78`/`+0x7C` are `0x00000000` and `0x40C00000`; read as the
`double` the `ViewQuery` view declares there, that is `0x40C0000000000000` =
**8192.0**.

### 2.2 Bottom offer — `src/props/ladder.c:130-162`

```
dx = q->pos.x - l->obj.pos.x ; dy = ... ; dz = ...      (ladder.c:126-129)
lx = m[6]*dz + m[3]*dy + m[0]*dx                        (ladder.c:133)  local X
ly = m[7]*dz + m[1]*dx + m[4]*dy                        (ladder.c:134)  local Y
y  = |ly|
if (y < q->range && lx > kReachMin && !(lx >= kReachMax))   (ladder.c:138)
    u = (q->matrix.m[1], q->matrix.m[4], q->matrix.m[7])    view column 1
    n = (m[1], m[4], m[7])                                  ladder column 1
    if (ly > 0) n = -n                                      (ladder.c:149-153)
    angle = Vec3_Angle(u, n)
    if (angle < kLadderAngle) {                             (ladder.c:157)
        topz = Model_GetPoint(l,1).z + l->obj.pos.z
        if (topz > q->pos.z && q->pos.z > l->obj.pos.z) {   (ladder.c:157)
            q->best = l ; best_pitch = best_yaw = 0 ;
            HUMAN_LADDER_TOP(human) = 0                     (ladder.c:158-161)
        }
    }
```

* `m` is the ladder object's row-major **local→world** matrix, so `lx`/`ly` are the
  player's coordinates along the ladder's local X and Y axes — the matrix's
  columns. The editor reads the same thing as `orientation_[0]` / `orientation_[1]`.
* `kReachMax = 2867.2`, `kReachMin = −2867.2` (DATA `0x533440`, `0x533868`,
  `src/props/ladder.c:200-203`) — 70 cm each way, i.e. a 1.4 m wide face.
* `kLadderAngle` = `ALIAS(kLadderAngle, kQuarterPi)` = `0.7853982f`
  (`src/props/ladder.c:55`, `src/plane/gear.c:156-157`, DATA `0x5337C0`).
* `Model_GetPoint(out, obj, i)` returns magic point `i` rotated and scaled by the
  object matrix **without** the translation, so callers add `obj->pos`
  (`src/missile/missile.c:304-322` shows the paired `Model_GetLocalPoint` + explicit
  matrix multiply; the `Model_GetPoint` branch adds only `pos`).
* The Z gate is a **world** Z test against magic point 1 and the object origin, not
  a local-axis test. On a rotated ladder the volume is therefore a world-vertical
  extrusion of the local rectangle, which is what the gizmo draws.

### 2.3 Top offer — `src/props/ladder.c:164-173`

```
t = Model_GetPoint(l,1) + l->obj.pos                       (ladder.c:164-165)
localY(player - t) < 0 && UseQuery_Consider(q, l, &t)
    -> HUMAN_LADDER_TOP(human) = 1
```

`UseQuery_Consider` rejects when `|t − q->origin| >= q->range` or when
`dot(t − q->origin, view column 1) <= 0` (`src/game/game.c:896-928`), i.e. the
reference point must be within 8192 units of the eye **and** ahead of the view.
This block runs unconditionally and can set the flag after the bottom block
cleared it (`ladder.c:161`), so "top" wins when both match.

### 2.4 What the editor does today

| Editor | line | vs IGI-RE |
| --- | --- | --- |
| `LadderPlacement::CanActivate` | `source/player_ladder.cpp:64-108` | band, both offers, same limits |
| `ActivateRadiusUnits = 8192.0f` | `source/player_ladder.h:49` | ✅ |
| `ActivateHalfWidthUnits = 2867.2f` | `source/player_ladder.h:50` | ✅ |
| `FacingCosine = 0.70710678f`, test `facing_dot > FacingCosine` | `source/player_ladder.h:51`, `source/player_ladder.cpp:90` | equivalent for a level view; uses a **2-D yaw-derived** forward vector (`:76-84`) instead of the view matrix's 3-D column 1, so a pitched player is judged differently. Also slightly stricter than IGI-RE at exactly 45° |
| z gate `player.z > origin_.z && player.z < activation_top_.z` | `source/player_ladder.cpp:91-92` | ✅ |
| top offer: `dot(from_top, normal) < 0 && |to_top| < 8192 && top_forward > 0` | `source/player_ladder.cpp:93-102` | ✅ (no angular candidate ordering) |
| selection = nearest ladder **origin** | `source/runtime/runtime_world.cpp:1565-1588` | ❌ IGI-RE selects the smallest angular deviation (`UseQuery_Consider`, `game.c:896-928`) |

New parity helper added by this task: `TaskSchemaNS::Props::LadderActivate`
(`source/level/task_schema.cpp:537-578`), which reproduces §2.2 and §2.3 exactly
(including the strict inequalities and the top-wins ordering) and is unit-tested.

---

## 3. Mount points

### 3.1 IGI-RE — `src/weapon/humanplayer.c:273-340`

| Constant | Value | file:line |
| --- | --- | --- |
| `kLadderBack` | `1515.52` (37 cm) | `:130` |
| `kLadderUp` | `3915.776` (95.6 cm) | `:131` |
| `kLadderTopBack` | `3481.6` (85 cm) | `:132` |
| `kLadderTopUp` | `1638.4` (40 cm) | `:133` |

```
from top    (humanplayer.c:299-306):
    v = Model_GetPoint(obj,3) + obj->pos                 (magic point 3 = head)
    v += m.col2 * kLadderTopBack  -  m.col1 * kLadderTopUp
from bottom (humanplayer.c:308-314):
    v = Model_GetPoint(obj,2) + obj->pos                 (magic point 2 = foot)
    v += m.col1 * kLadderBack     +  m.col2 * kLadderUp
then        (humanplayer.c:320-322):
    Matrix_GetEuler(m, &ax, &ay, &az); az += kPi; Human_SetYaw(az)
    ladderPos = fromTop ? obj->steps - 6 : 0 ; ladderTop = obj->steps
```

`Matrix_GetEuler` (HOOK `0x4B3C50`, `src/math/matrix.c:181-191`) computes
`az = atan2(e[3], e[0])` — the editor's
`atan2(orientation_[0].y, orientation_[0].x) + π` (`source/player_ladder.cpp:58-62`)
is the same value, negated for the editor's mirrored camera-yaw convention.

### 3.2 What the editor does today

`source/player_ladder.cpp:47-54` implements exactly the two formulas above, using
`orientation_[1]` / `orientation_[2]` (correct columns) with the constants from
`source/player_ladder.h:52-55`. **No change needed**; the parity helper
(`ComputeLadderPlacement`, `source/level/task_schema.cpp:492-535`) restates it for
the editor's viewport/parity path and is unit-tested against the retail
`400_17_1.mef` numbers.

---

## 4. Climb line

### 4.1 Rung count — `src/props/ladder.c:65-93` (`Ladder_Create`, `0x440280`)

```
radius = VirModel_GetRadius(model) * max_scale(obj)          (ladder.c:76-80)
Model_GetLocalPoint(&p2, l, 2) ; Model_GetLocalPoint(&p3, l, 3)   (ladder.c:86-87)
h = |p2.z - p3.z|
l->rungs = (int)round_ll(h * kRungScale)                     (ladder.c:91)
```

`kRungScale = 1/1228.8` (DATA `0x533860`, `src/props/ladder.c:52`) → one rung every
**1228.8 units = 30 cm**. `round_ll` rounds to nearest.

### 4.2 Climb state

* `HumanPlayer_LadderUpdate` (`src/humanstate/humanstate.c:1138-1206`) steps
  `ladderPos` from `0` to `ladderTop − 6`; `kClimbTime = 6.0f`
  (`src/props/ladder.c:59-63`, DATA `0x533528`) is the traversal duration.
* The `− 6` step margin is the `obj->steps − 6` seen in
  `src/weapon/humanplayer.c:323`.

### 4.3 What the editor does today — **defect**

| Editor | line | vs IGI-RE |
| --- | --- | --- |
| `height = |top.z − bottom.z|` then `static_cast<int>(height / 1228.8f)` | `source/player_ladder.cpp:23-26` | ❌ truncates, and measures **world** Z instead of the local Z difference |
| `StepLengthUnits = 1228.8f`, `TopStepMargin = 6` | `source/player_ladder.h:27-28` | ✅ |

Worked example (`314_01_1.mef`, retail): `|7.03 − 47681.54| / 1228.8 = 38.80` →
IGI-RE **39** rungs, editor **38**. The new parity helper uses
`std::llround` (`source/level/task_schema.cpp:524`) and the local-axis projection,
and the test `LadderClimbLine.RungCountRoundsToNearest` pins the difference.

---

## 5. Changes in this task

| File | Change |
| --- | --- |
| `source/level/task_schema.h:201-292` | Ladder constants (`:216-253`), `LadderPlacement` (`:255-274`), `ComputeLadderPlacement` (`:276`), `LadderActivation` (`:283`), `LadderActivate` (`:291`) — each with its IGI-RE citation. The header stays dependency-free (no glm) so `igi_mcp` keeps compiling it |
| `source/level/task_schema.cpp:492-578` | `ComputeLadderPlacement` (mounts, local-axis height, rounded rung count, `Matrix_GetEuler`-equivalent yaw) and `LadderActivate` (both offers, exact inequalities) |
| `source/renderer/renderer_objects_visual.cpp:61-70, 158-205, 1033-1126` | Ladder gizmos: activation band (4 world-vertical edges + both rectangles), `point2 → point3` climb line, mount-to-mount line, one tick per rung, and markers for the three magic points and both mounts |
| `source/renderer/renderer_objects_visual.cpp:1022-1031`, `source/renderer/renderer_objects_internal.h:60-68` | `igi::SetLadderGizmosEnabled` / `igi::LadderGizmosEnabled` (default **on**) |
| `tests/test_ladder_placement.cpp` | 19 tests, all passing |
| `CMakeLists.txt:319` | `tests/test_ladder_placement.cpp` registered additively |

**Why the gizmos live in `DrawMagicObjSpheres`.** `Renderer_Objects`'s header is
outside this change's write scope, so a new member function could not be declared.
The ladder previews are therefore drawn by a file-local helper called from the
existing magic-object overlay (`source/renderer/renderer_objects_visual.cpp:158-205`),
which is the editor's existing view toggle: `App::show_magic_obj_spheres_`
(`source/app.h:204`, default `false` at `source/app.cpp:81`), flipped by a key in
`source/app_input.cpp:658-659` with the status message `"Magic objects: ON/OFF"`.
There is no render menu in this editor (no ImGui; overlays are GLUT keyboard
toggles), so that existing toggle **is** the on-screen switch, and
`igi::SetLadderGizmosEnabled(false)` suppresses the ladder gizmos independently.
If the Lead prefers the gizmos to be unconditional, the glue is two lines: a
declaration in `source/renderer/renderer_objects.h` and a call next to
`DrawPropsGizmos(obj, ubo_mats, ubo_binding_point_);`
(`source/renderer/renderer_objects.cpp:899`) — the helper body would need to move
into a member to reach the mesh cache, which is why it was not done here.

Gizmo colours: activation band cyan, climb line + rung ticks green, mounts red,
magic points amber. Line width stays at GL's default, matching `DrawPropsGizmos`.

---

## 6. Verification status

### Verified (command output in the task report)

| Claim | Evidence |
| --- | --- |
| Constants match IGI-RE | `tests/test_ladder_placement.cpp` — 19/19 pass, including the `0x40C00000` → `8192.0` reinterpretation assertion |
| Mount points match IGI-RE, including on a rotated ladder | same suite, `LadderMounts.*` |
| Rung count rounds (38.80 → 39) and projects along the local axis | same suite, `LadderClimbLine.*` |
| Both activation offers, strict band edges, 45° gate, 8192 radius | same suite, `LadderActivation.*` |
| Retail fixture `400_17_1.mef` reproduces the oracle maths | same suite, `LadderRetailFixture.*` |
| All changed translation units compile with the retail flag set | `python3 tools/igi-re-parity/tu-check.py source/level/task_schema.cpp source/renderer/renderer_objects_visual.cpp source/renderer/renderer_objects_atta.cpp` → `0 failed` |

### Not verified ⏳

1. **No on-screen confirmation.** The editor was not built or run on Windows in
   this session, so the gizmos have never been rendered. Their geometry is
   unit-tested only indirectly (the placement maths is; the GL lines are not).
2. **Attached ladders.** The gizmo uses the object's own `pos`/`rot`
   (`igi::IgiEulerRotationMat3(obj.rot)`,
   `source/renderer/renderer_objects_visual.cpp:174-175`), exactly like the sphere
   pass it sits next to. The runtime composes the ATTA parent chain instead
   (`App::CollectAttachedRuntimeLadders`, `source/app.cpp:3056-3107`), so a ladder
   attached to a parent object is drawn at its authored transform, not the
   composed one. The runtime placements remain authoritative for gameplay.
3. **`Model_GetPoint`'s exact body** is not decompiled; its "rotate+scale, no
   translate" semantics are inferred from the paired call sites in
   `src/missile/missile.c:304-322` and from `src/weapon/humanplayer.c:299-314`
   adding `obj->pos` explicitly.
4. **`kUseRange` reinterpretation** is derived from the struct layouts plus the
   `Human_UpdateUse` stores; it was not confirmed with a debugger on a retail
   process (the installed `IGI.exe` hash `dea34892…` is not IGI-RE's supported EU
   target `ae93669a…`, so no differential harness was possible).

### Defects reported by task-9, and their outcome

| Defect (as reported) | Outcome |
| --- | --- |
| `source/player_ladder.cpp:23-26` truncates the rung count and measures world Δz | **Fixed in task-12** (§7.1) |
| `source/app.cpp:3126-3138` builds the ladder orientation as `Rz*Rx*Ry` | **Fixed by the Lead** with `igi::IgiEulerRotationMatrix` (not redone here) |
| `source/runtime/runtime_world.cpp:1575-1589` selects the nearest ladder by origin distance | **Fixed in task-12** (§7.2) |
| `source/player_ladder.cpp:76-84` judges facing from a 2-D yaw vector | **Fixed in task-12** (§7.3) |

---

## 7. Runtime parity fixes (task-12)

The three defects above are the play-mode copy of the rules §2–§4 already
documented: `source/player_ladder.cpp` is the runtime's own `Ladder_Create` +
`Ladder_Use`, and it had drifted from `source/level/task_schema.cpp` (task-9's
copy, which was already correct). Each fix below states the IGI-RE rule, what the
editor did before, and what it does now.

One structural note first: the activation test used to be a method,
`LadderPlacement::CanActivate(position, yaw_degrees, at_top)`, which could only
judge one ladder at a time. The engine's selection is *not* per-ladder — it is one
shared query updated by every ladder in the scan — so the method was replaced by
`igi::LadderUseQuery` (`source/player_ladder.h`), whose `Consider(ladder, eye,
view, index)` applies exactly one ladder's `Ladder_Use` to that shared state, and
`source/runtime/runtime_world.cpp` drives it over the whole placement list. This is
what makes the selection rule (§7.2) expressible at all.

### 7.1 Rung count — rounds, and along the model's own Z

**IGI-RE** (`src/props/ladder.c:86-91`, `Ladder_Create` `0x440280`):

```c
Model_GetLocalPoint(&p2, l, 2);
Model_GetLocalPoint(&p3, l, 3);
h = |p2.z - p3.z|;
l->rungs = (int)round_ll(h * kRungScale);   /* kRungScale = 1/1228.8, :202 */
```

Two independent facts:

1. `round_ll` **rounds**; it is an x87 `fistpll` (`src/props/props.h:50-57`), so it
   uses the current rounding mode — round-to-nearest-even by default.
2. `Model_GetLocalPoint` returns **model-local** coordinates, so the height is the
   difference along the model's own Z axis. A ladder rotated onto its side still
   counts rungs; a world-Z difference does not.

**Editor before** (`source/player_ladder.cpp:23-26`):

```cpp
const float height = std::abs(top.z - bottom.z);          // world Δz
step_count_ = std::max(1, static_cast<int>(height / StepLengthUnits));  // truncates
```

**Editor after** (`source/player_ladder.cpp:59-80`): the caller passes the ladder's
local +Z column (`LadderPlacement`'s `orientation[2]`), the height is
`|dot(top - bottom, climb_axis)|`, and the count is
`(int)std::nearbyint(height / 1228.8f)`. `std::nearbyint` is the C++ spelling of
"round according to the current rounding mode", which is what `fistpll` does;
`std::llround` would agree on every non-tie value but rounds ties away from zero
instead of to even.

Measured effect: retail `314_01_1.mef` (foot z 7.0300, head z 47681.5352) gives
`47674.5052 / 1228.8 = 38.7975` → **39** rungs (top step 33). The editor produced
**38** before this change, exactly as task-9 reported.

**Caveat, stated plainly:** `Model_GetLocalPoint` is unscaled model space, while
the editor's `bottom`/`top` are world points already transformed by the object
matrix. The projection recovers the local difference exactly only for a
rotation (unit scale); a scaled ladder object would carry that scale into the
count. Every ladder in the retail data has unit scale, and the mount-point maths
(§3) uses the same world points, so this is consistent with the rest of the
runtime — but it is not something this change proved for scaled ladders.

### 7.2 Selection — the engine keeps the offer closest to straight ahead

**IGI-RE**: `Human_UpdateUse` (`src/humanbase/human.c:1269-1300`) walks the whole
task list and calls every task's handler with **one shared query**:

```c
q.unk70 = q.unk74 = Human_IsPlayer(h) ? kQuarterPi : kPi;   /* bestPitch, bestYaw */
q.unk78 = 0;
q.range = kUseRange;                                        /* 8192, §2.1 */
...
fn(Game_TaskListItem(Game_TaskList0(), i), &q);              /* once per task */
```

`Ladder_Use` (`src/props/ladder.c:122-175`) then updates that query, and
`UseQuery_Consider` (`src/game/game.c:897-928`) implements the ranking — the file's
own comment at `src/game/game.c:182` calls it *"keeps the item closest to straight
ahead"*:

```c
d = pos - q->origin;
if (Vec3d_Length(&d) >= q->range) return 0;          /* out of reach */
y = m[7]*dz + m[4]*dy + m[1]*dx;                     /* local Y of d */
if (!(y > 0.0)) return 0;                            /* must be in front */
pitch = |Vec3d_PitchAngle(l)|; yaw = |Vec3d_YawAngle(l)|;
if (pitch >= q->bestPitch || yaw >= q->bestYaw) return 0;   /* not closer on BOTH */
q->bestPitch = pitch; q->bestYaw = yaw; q->best = item;
```

`Vec3d_PitchAngle`/`Vec3d_YawAngle` (`src/game/misc.c:42-72`) are the signed
deviations from the view frame's local +Y, so both scores are 0 when the target is
dead ahead. Note the strict **conjunction**: a candidate must beat the incumbent on
pitch *and* yaw, so the winner is not "the smallest of one number" but the last
candidate that dominated all previous ones.

The bottom offer does not score at all. On success it overwrites the winner and
**zeroes both bounds** (`src/props/ladder.c:158-161`):

```c
q->best = l;
q->best_pitch = 0;
q->best_yaw = 0;
```

`best_pitch`/`best_yaw` are the very fields `UseQuery_Consider` compares against
(`src/props/propstypes.h:12-13` and `src/game/game.h:188-189` both place them at
0x70/0x74), so after a successful bottom offer **every later top offer is rejected**
— which is also the sensible in-game outcome: if you can grab the ladder from the
bottom, you get on at the bottom.

**Editor before** (this same function): `CanActivate` per ladder, then the winner
was the smallest `|origin - eye|²`. The origin is the
object's authored position, which has nothing to do with what the player is
looking at.

**Editor after** (`source/runtime/runtime_world.cpp:1558-1604`): build the player's
view frame (`BuildPlayerViewFrame`, same file), then

```cpp
LadderUseQuery query;                       /* bestPitch = bestYaw = kQuarterPi */
for (index = 0; index < ladder_placements_.size(); ++index)
    query.Consider(ladder_placements_[index], player_eye, view, index);
if (!query.HasBest()) return false;
ladder_traversal_.Mount(ladder_placements_[query.GetBestIndex()], query.IsBestAtTop());
```

`LadderUseQuery::Consider` is a line-by-line transcription of `Ladder_Use`
(`source/player_ladder.cpp:123-162`): the band/facing/Z-span bottom offer with the
bound-zeroing overwrite, then the unconditional top offer gated by
`dot(eye - reference, normal) < 0` and `UseQuery_Consider`. `ConsiderReferencePoint`
is `UseQuery_Consider` itself (`source/player_ladder.cpp:164-190`), including the
`>=` comparisons, the `y > 0` front test and the absolute pitch/yaw scoring.

**Where the editor deliberately differs.** The engine's scan order is the retail
task list; the editor's is its own placement vector, so *which* of two
bottom-offering ladders wins is not reproducible — the engine's rule there is
"last one in the list that passes", an artefact of list order rather than a
behaviour. Everything else (the angular ranking, the 45° bounds, the lock-out, the
range test) is now the engine's. This runtime is a deliberate simplification of the
retail game loop, not a reimplementation of it.

### 7.3 Facing — the view frame's 3-D column 1, not a 2-D yaw vector

**IGI-RE** (`src/props/ladder.c:143-157`):

```c
u = (q->matrix.m[1], q->matrix.m[4], q->matrix.m[7]);   /* view column 1 */
n = (l->obj.matrix.m[1], m[4], m[7]);                   /* ladder column 1 */
if (ly > kZeroD) n = -n;                                /* flip to the player's side */
angle = Vec3_Angle(&u, &n);
if (!(angle >= kLadderAngle) && topz > q->pos.z && q->pos.z > l->obj.pos.z) { ... }
```

`kLadderAngle` is `kQuarterPi` = `0.7853982f` (`src/plane/gear.c:156-157`), and
`Vec3_Angle` (`src/file/vec.c:189-208`) returns the angle in radians, so the gate is
`angle < 45°` — not a cosine comparison.

**Editor before** (`source/player_ladder.cpp:76-84`): a 2-D forward vector
`(sin yaw, cos yaw)` dotted with the ladder normal's xy. Two problems:

1. It threw pitch away, so a player looking steeply up or down at the ladder was
   judged by their horizontal heading only.
2. The x component had the wrong sign for this runtime. The look direction the
   editor actually uses for the aim and interaction rays is
   `(-sin yaw · cos pitch, cos yaw · cos pitch, sin pitch)`
   (`source/runtime/runtime_world.cpp:2420` and `:2496-2499`), with
   `(-sin yaw, cos yaw)` as the horizontal basis in
   `source/player_controller.cpp:130-131`. The old test used `+sin yaw`, i.e. the
   mirrored convention the ladder *yaw* helper also assumed
   (`source/player_ladder.cpp:108-113`) — so for any yaw other than 0 or 180° the
   facing test judged the wrong direction.

**Editor after**: `CanActivate`'s yaw parameter is gone. `Consider` takes a
`ViewFrame` whose column 1 is the look direction and compares
`AngleBetweenRadians(view.forward, n) < LadderPlacement::FacingAngleRadians`,
where `AngleBetweenRadians` transcribes `Vec3_Angle` including its exact-zero and
clamped branches (`source/player_ladder.cpp:22-31`). The runtime builds that frame
in `BuildPlayerViewFrame` from the same yaw/pitch the renderer uses, with
`up = right × forward` — the frame order the camera builders already use
(`source/runtime/runtime_world.cpp:1842-1848`).

Boundary note (carried over from task-9): `0.7853982f` is a hair **above** π/4
(`0.78539818525314331 > 0.78539816339744828`), so a construction whose computed
angle lands on π/4 stays inside the gate. That is a float coincidence, not a
design: the computed angle comes from `acos` of a float dot product, so at exactly
45° the outcome can fall either way. The tests therefore pin 44.9° (accepted) and
45.1° (rejected) and assert the constant's relationship to π/4 separately.

### 7.4 Changes in task-12

| File | Change |
| --- | --- |
| `source/player_ladder.h` | `ViewFrame`; `LadderClimbLine` ctor takes the local +Z column; `FacingCosine` → `FacingAngleRadians`; `CanActivate` replaced by `LadderUseQuery` (`Consider`, `Reset`, `InitialBestAngleRadians`); axis accessors |
| `source/player_ladder.cpp:10-57` | `IsZero`, `AngleBetweenRadians` (`Vec3_Angle`), `PitchAngleRadians`/`YawAngleRadians` (`src/game/misc.c:42-72`) |
| `source/player_ladder.cpp:59-80` | Rung count: local-axis height, `std::nearbyint` |
| `source/player_ladder.cpp:116-190` | `LadderUseQuery::Reset/Consider/ConsiderReferencePoint` |
| `source/runtime/runtime_world.cpp:147-163` | `BuildPlayerViewFrame` (file-local) |
| `source/runtime/runtime_world.cpp:1583-1625` | `TryMountNearestLadder` drives the shared query (name kept for the existing declaration at `source/runtime/runtime_world.h:352`) |
| `tests/test_ladder_runtime.cpp` | 17 new tests (new suite) |
| `tests/test_runtime_subsystems.cpp:1417-1442` | `PlayerLadderTest.ResolvesBottomAndTopActivationGeometry` ported to the query API. **Outside the task's enumerated write scope** — the old test called the removed `CanActivate`, and leaving it would have broken the full lane. Two expectations changed because they encoded the old per-ladder rule: the second case now stands above the reference point, and a case where both offers apply now mounts at the **bottom** (the engine's lock-out, §7.2) |
| `CMakeLists.txt:352` | `tests/test_ladder_runtime.cpp` registered additively |

### 7.5 Verification status for task-12

Verified by command output:

| Claim | Evidence |
| --- | --- |
| Rung count rounds (38.7975 → 39) and the pre-fix rule gives 38 | `IGI_HOST_NO_CACHE=1 IGI_HOST_TESTS=tests/test_ladder_runtime.cpp bash scripts/run-host-tests.sh` → `LadderRungCount.*`, 17/17 pass |
| Rung count projects along the model's local Z, not world Z | same run, `LadderRungCount.MeasuresTheLaddersLocalZAxisNotWorldZ` (world Δz = 0 → 24 rungs) |
| The angular rule beats nearest-origin | same run, `LadderUseQuery.NearestOriginLosesToTheBetterAlignedTopOffer` (5361.9 vs 7615.8 origin distance, better-aligned wins, both scan orders) |
| Bottom offer zeroes the bounds and locks out a later top offer | same run, `LadderUseQuery.BottomOfferZeroesTheAngleBoundsAndLocksOutALaterTopOffer` |
| Facing uses the view frame and the strict angle gate | same run, `LadderUseQuery.FacingGateRejectsJustOverFortyFiveDegrees`, `...BottomOfferUsesTheViewFrameNotAFlatYawVector`, `...FacingThresholdIsAHairAboveFortyFiveDegrees` |
| Band edges, 8192 radius, Z-span strictness | same run, `LadderUseQuery.BottomOffer*` / `TopOfferRejectsAtTheActivateRadius` |
| No regression in the task-9 suite | `IGI_HOST_TESTS=tests/test_ladder_placement.cpp,tests/test_ladder_runtime.cpp` → 36/36 pass |
| Changed TUs compile with the retail flag set | `python3 tools/igi-re-parity/tu-check.py source/player_ladder.cpp source/runtime/runtime_world.cpp` → `checked 4 translation unit(s), 0 failed` |
| The edited `test_runtime_subsystems.cpp` snippet behaves as asserted | compiled and ran the same statements standalone against `source/player_ladder.cpp` (both offers resolve, top case reports `IsBestAtTop`) |

Not verified:

1. **`tests/test_runtime_subsystems.cpp` could not be run on this host.** It is in
   the runner's "not host-portable (compile)" bucket because `source/app.h`
   (Lead-owned, currently being made host-portable) fails to parse under the host
   include set: `source/app.h:162:3: error: unknown type name 'Renderer'`, plus
   `ModelPickerEntry` and `PropPanel`. My edit is inside that file, so the x64 lane
   is the first place it is compiled. A standalone TU with the identical statements
   compiles and behaves correctly.
2. **No runtime play-through.** Nothing here was exercised in a live world: the
   selection and offer rules are pure functions driven by the runtime, and only
   those pure functions are tested. The mount side effects
   (`LadderTraversal::Mount`, player teleport, `EndLadderTraversal`) are unchanged.
3. **The engine's scan order is not reproduced** (§7.2) — the editor's list order
   stands in for the retail task list.
4. **`Vec3d_PitchAngle`/`Vec3d_YawAngle` are transcribed from the decompilation**
   (`src/game/misc.c:42-72`), not compared against a retail trace.

