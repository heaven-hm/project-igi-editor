# Placement, splines, rails and trains (#59, #69, `docs/KNOWN_ISSUES.md` §2)

Parity record for the `SplineObj` / `SplineObjWaypoint` / `Train` placement work:
the KNOWN_ISSUES entries **📍 Missing Position** and **🎬 Train Placements in
Cutscenes**, GitHub issue **#59** (rail/signed scalar) and issue **#69** (Hermite
roads/bridges). Every rule, offset and constant below is cited to IGI-RE, the
verified decompilation of the retail `IGI.exe`; every editor claim is cited to a
file and line in this repository.

Oracle: `/Users/haseeb-mir/Documents/Code/IGI-RE` (read-only).
Editor: this repository, branch `fix/igi-re-known-issues`.
Retail data: `/Users/haseeb-mir/Downloads/IGI/MISSIONS/location0/LEVEL1/objects.qsc`.

> Status legend — **verified** = confirmed against IGI-RE source and/or shipped
> retail data with a command whose output is quoted; **unverified** = cannot be
> confirmed in this environment (no runnable retail target on macOS and the
> installed `IGI.exe` is not the IGI-RE-supported build, so there is no
> differential harness for live level load or cutscene playback).

---

## 1. Task argument layouts

The QSC argument index is `3 + ParamTable position`, because args 0/1/2 are the
task id, the type string and the name. The tables below are the engine's own
`ParamTable_Add` order, cross-checked against retail `Task_DeclareParameters`
lines in the shipped mission data.

### 1.1 `SplineObj` (the container)

| arg | engine field | type | retail label |
| --- | --- | --- | --- |
| 3 | `rigid` | Bool8 | Linear Segments |
| 4 | `collide` | Bool8 | Display waypoints |
| 5 | `snap` | Bool8 | Snap Length |
| 6 | `autoOrient` | Bool8 | Automatic Orientation |
| 7 | `maxSegments` | Int32, range 2..20 | Number of Matrices / Segment |
| 8 | `unk20` | Int32 | Collision LOD |
| **9-11** | **`pos`** | **Real32x3** | **Position** |
| 12 | `angle` | Angle | Gamma Orientation |
| 13-15 | `color1` | RGB | Dirlight Angular Effect |
| 16-18 | `color2` | RGB | (second colour) |

Evidence — engine: IGI-RE `src/editobj/spline.c:204-214` (`SplineObj_Open`,
`HOOK(0x502250, SplineObj_Open)` at `:225`; the segment range 2..20 is set at
`:203-204`). Retail data: `objects.qsc` `Task_DeclareParameters("SplineObj", …)`
and the line itself, whose four leading flags are `FALSE, FALSE, FALSE, FALSE`,
then `20, 0`, then the three zero position components, then `3.1415929794311523`
(the gamma angle) and two RGB triples.

**Position lives at args 9-11, never at 3-5.** Args 3-5 are booleans; reading a
position from them collapses the container to the map origin.

### 1.2 `SplineObjWaypoint`

| arg | engine field | type | retail label |
| --- | --- | --- | --- |
| 3-5 | `rot` | Real32x9 → 3 Euler angles | Orientation |
| 6-8 | `point` | ObjectPos | Position |
| 9 | `modelName` | String16 | Waypoint Model |
| 10 | `segModel` | String16 | Segment Model |
| 11 | `segments` | Int32, range 2..20 | NumAreas |
| 12 | `straight` | Bool8 | Align |
| 13 | `reversed` | Bool8 | Flip |
| 14 | `autoOrient` | Bool8 | Automatic Orientation |

Evidence — engine: IGI-RE `src/editobj/spline.c:215-223`. Retail data: the
`Task_DeclareParameters("SplineObjWaypoint", …)` line and, e.g.,
`Task_New(-1, "SplineObjWaypoint", "", 0, 0, 3.1415929794311523, 25287152.0,
-55948040.0, 174410960.0, "waypoint", "368_01_1", 20, FALSE, FALSE, FALSE)` —
three angles, three position components, the model strings, `NumAreas = 20`, and
three flags. A `Real32x9` orientation is serialized as **three** numbers (the
Euler triple), not nine.

Note the editor's own fixture `tests/fixtures/level01_simple.qsc` writes an
integer `20` where retail writes the Segment Model *string*; retail is the truth.

### 1.3 `Train`

| arg | engine field | type |
| --- | --- | --- |
| 3 | `start_dist` | Real32 — **signed arc distance** |
| 4 | `start_speed` | Real32 |
| 5 | `path` | Int32 — task id of the `SplineObj` container |
| 6 | `model` | String256 |
| 7 | `accel` | Real32 |
| 8 | `max_speed` | Real32 |
| 9 | `reverse` | Bool8 |
| 10 | `offset[0]` | Real32, range −5..+5 |
| 11 | `offset[1]` | Real32, range −5..+5 |
| 12 | `script` | VarString |

Evidence — engine: IGI-RE `src/missile/train.c:379-393` (`Train_Open`:
`gTrainParams = ParamTable_New(10)`, then `start_dist`, `start_speed`, `path`,
`model`, `accel`, `max_speed`, `reverse`, `offset[0]`, `offset[1]`, `script`),
with `gTrainOffsetRange[0] = -5.0f; gTrainOffsetRange[1] = 5.0f;` at
`:380-381`. Retail data: `Task_New(1499, "Train", "", -4045627.5,
0.20000000298023224, 1500, "608_01_1", 0.0010000000474974513, 200.0, TRUE, 0, 0,
"", …)` — `start_dist` is **negative**, `path` is `1500` (the `SplineObj` task id
that follows it), and `reverse` is `TRUE`.

---

## 2. 📍 Missing Position

### 2.1 Root cause, split in two

**(a) The container's position is not where the generic code looks.**
`LevelObjects::Load` (the live load path) handles `isSplineContainer` correctly at
`source/level/level_objects.cpp:235-243` — `linearSegments` from arg 3,
`splineSegmentCount` from arg 7, `pos` from args **9/10/11**. But its final `else`
branch (`source/level/level_objects.cpp:444-447`) reads **no coordinates at all**,
so every task type outside the enumerated list loads at `(0,0,0)`. `AnimTask` and
`CutScene` are two of those types, which is the concrete "loaded without valid
coordinates, defaulting to the map origin" instance and the reason the cutscene
classes are affected (§3.2).

**(b) The editor's own QSC reader/writer used the generic layout for `SplineObj`.**
`LevelObjects::ParseTaskLine` (`source/level/level_objects_serialize.cpp`) had no
`SplineObj` branch and fell through to its `argTokens.size() > 8` generic case,
which reads `pos` from args **3/4/5** — for a container those are the three
booleans, so the position collapsed to the origin. The mirrored writer had the
same shape: with no `SplineObj` branch, `UpdateCoordinatesInLine` wrote nothing at
all for the container, so a moved container's position was never persisted.

### 2.2 Fix

`source/level/level_objects_serialize.cpp:194-213` — new `SplineObj` branch in
`ParseTaskLine`: `linearSegments` from arg 3, `splineSegmentCount` from arg 7,
`pos` from args 9/10/11, matching `level_objects.cpp:235-243` and
IGI-RE `src/editobj/spline.c:204-214`.

`source/level/level_objects_serialize.cpp:360-371` — new `SplineObj` branch in
`UpdateCoordinatesInLine`: writes `pos` to args **9/10/11** and leaves args 3-8
(the flags and the segment count) exactly as authored.

### 2.3 What could not be fixed here

`source/level/level_objects.cpp` is owned by the props-parity workstream, so the
loader gap in §2.1(a) is reported, not patched. The one-line-per-argument hunk the
final `else` at `source/level/level_objects.cpp:444-447` needs is:

```cpp
} else {
    switch (arg_idx) {
        case 0: obj.taskId = TaskIdFromArg(cur_a); break;
        case 2: if (cur_a->type_ == QSC::arg_s::type_t::STR) {
                    obj.name = cur_a->str_;
                    obj.original_name = cur_a->str_;
                    obj.has_original_name = true;
                } break;
        case 3: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.x = cur_a->dbl_; obj.original_pos.x = cur_a->dbl_; } break;
        case 4: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.y = cur_a->dbl_; obj.original_pos.y = cur_a->dbl_; } break;
        case 5: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.z = cur_a->dbl_; obj.original_pos.z = cur_a->dbl_; } break;
        case 6: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.rot.x = cur_a->dbl_; obj.original_rot.x = cur_a->dbl_; } break;
        case 7: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.rot.y = cur_a->dbl_; obj.original_rot.y = cur_a->dbl_; } break;
        case 8: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.rot.z = cur_a->dbl_; obj.original_rot.z = cur_a->dbl_; } break;
        default: break;
    }
}
```

That is the generic `pos`/`rot` layout used by `AnimTask`, `CutScene`, `SCamera`
and every other unenumerated task type. It is only safe if those types really do
carry `pos` at 3-5; each type's own `ParamTable` order should be checked before it
is applied. **Unverified** in this environment (no level load).

---

## 3. 🎬 Train Placements in Cutscenes

### 3.1 There is no cutscene-specific train path in the engine

IGI-RE has exactly one train placement routine: `Train_Place`
(`HOOK(0x42A2C0, Train_Place)`, `src/missile/train.c:209-274`). Neither
`src/rotor/train.c` nor `src/cutscene/cutscene.c` contains any train or
`SplineObj` code, and no cutscene module references `gTrainType`. The KNOWN_ISSUES
root cause ("hardcoded spline overrides and coordinate offsets specifically during
cutscenes") does not correspond to a distinct engine code path. What the engine
does have is a small set of fixed conventions that any placement code must
reproduce:

| Convention | Value | Evidence |
| --- | --- | --- |
| Arc distance is **signed**, measured from the end when negative | `d += length` while `d < 0` | `src/tagitem/spline.c:242-255` |
| Displacement is clamped to ±5 m | `gTrainOffsetRange = {-5.0f, 5.0f}` | `src/missile/train.c:380-381` |
| Displacement is converted to game units by ×4096 | `kFixed = 4096` | `src/math/mathvars.h:7`; `src/missile/train.c:212-213` |
| The lead car's rail spacing is `∓t->length` | `offsets[0] = t->reverse ? t->length : -t->length` | `src/missile/train.c:221` |
| The blend point is `frac` of that spacing | `t->frac = |a.x| / length` | `src/missile/train.c:142-167` |
| The model frame is remapped after `Matrix_SetDirection` | `orient()`: row0 = −row2, then transpose | `src/missile/train.c:172-190` |
| Wagon spacing is the wagon's **model-derived** length | `offsets[i] = ∓wagon->length` | `src/missile/train.c:222-225` |

So the cutscene symptom is a *consequence* of these conventions not being
reproduced, plus the loader gap in §2.1(a) — not of a separate override.

### 3.2 Retail evidence for the signed scalar

`LEVEL1/objects.qsc:485-497` authors a **12**-waypoint `SplineObj` (task id 1500)
and every `Train` in the level names it as its rail — task id 1499 uses
`start_dist = -4045627.5`. Twelve waypoints give **11** Hermite spans, and span *i*
takes waypoint *i*'s own `NumAreas` (`20/10/10/10/10/10/20/20/10/10/10` — the first
eleven of the twelve authored values; the twelfth waypoint contributes as the final
span's endpoint). The engine's `SplineObj.length` — the sum of the per-joint
`segLength`s (`SplineObjWaypoint_Changed`, `src/tagitem/spline.c:833-839`) — is
**4 461 272.69 game units = 1089.1 m**. The Hermite length exceeds the chord sum
because each span is a curve, not a chord.

`-4045627.5` is **less than one rail length back from the end**
(4045627.5 < 4461272.69), so the engine's wrap loop
`d = dist; if (!(d >= 0)) do d += obj->length; while (!(d >= 0));`
(`SplineObj_GetPoints`, `src/tagitem/spline.c:246-256`) runs **once**:

```
wrap(-4045627.5, 4461272.69):
   d = -4045627.5 + 4461272.69 = 415645.19   >= 0 -> done, one addition
```

So on this train the old editor arithmetic `clamp(totalLen + rawPos, 0, totalLen)`
happens to reach the same arc:

```
engine: arc 415645.19 -> pos (24871953.53, ..., 174412944.16)   span 1, just short of waypoint 2
old   : arc 415645.19 -> same
```

The wrap **loop** is still the engine's rule and the editor must keep it — a clamp
is not equivalent in general, and it is what the replacement in
`source/app_lookup.cpp` implements — but **no retail LEVEL1 train exercises the
multi-wrap path**: all four authored `start_dist` values (`-4045627.5`,
`-575416.9375`, `-971400.125`, `-3744122.25`) are inside one rail length. The loop
is therefore pinned on synthetic input
(`TrainRailPlacement.WrapRailDistanceLoopsUntilNonNegative`), and the four retail
trains are pinned as one-add cases.

#### Precision note — the engine accumulates `obj->length` in `float`

The `4 461 272.69` above is a **double-precision** sum of the eleven span lengths,
and that is not what the engine produces. `SplineObj.length` is a `float`
(`src/sound/soundtypes.h:54`) and the accumulation rounds to `float` on **every**
addition:

```c
obj->length = 0;                                  /* float */
for (p = Waypoint_Next(p); p; p = Waypoint_Next(p))
    if (p->seg) obj->length = p->seg->length + obj->length;   /* spline.c:836-838 */
```

Summing the same eleven spans in `float` gives **4 461 267.50** units, and the
train-1499 wrap is therefore exactly **415 640.00**, not `415 645.19`. The residual
is **5.19 units — 1.1 × 10⁻⁶ relative**, i.e. last-bits of a 23-bit mantissa
accumulated over eleven additions.

It changes no decision: `415640.00` and `415645.19` are both non-negative on the
first addition, both land in span 1, and both place the train at
`(24871953.5…, …, 174412944.1…)`. The wrap **count** (one) and the placement
**span** (1) are identical either way — which is why the pinned assertions
(`tests/test_train_rail.cpp:152-174`) still describe the outcome correctly while
their length constants are 5.19 units high.

Two known locations carry the same double-vs-float divergence and are recorded
rather than silently "corrected":

- The **test fixture** (`tests/test_train_rail.cpp`) builds the rail with the
  editor's builder, which sums `length` in `double`.
- The **production builder**, `source/renderer/spline_geometry.cpp:588-594`
  (`out.length = 0.0; out.length += len;`), also sums in `double`, so the editor's
  arc-length axis runs ~5 units high on this rail versus the engine. This is a
  genuine (if negligible) parity gap in the shipped rule, not just in the doc: to
  match the oracle exactly the accumulator must be `float`. No placement or
  collision decision on retail data depends on it, and the fix is left for a
  dedicated change so that it is tested against the engine's per-joint
  `segLength` values rather than approximated here.

#### Correction — this section was wrong

An earlier revision claimed an 11-waypoint rail, 10 spans, 3 928 793.21 units and a
**two**-wrap wrap. All of those numbers came from one off-by-one: the rail's twelfth
waypoint (`22088476.0, -57890580.0, 174504640.0`, `objects.qsc:497`) was dropped, so
the final span — **532 479.48 units** — was missing. The shortened rail made
`|start_dist|` exceed the length and turned a one-wrap train into an apparent
two-wrap one. The old `NumAreas` list was already the eleven *span* counts, which is
why the data looked self-consistent.

`tests/test_train_rail.cpp` carried the same truncated fixture and asserted the
wrong length, wrapped arc and position. Both the fixture and the doc are corrected;
the test now asserts 12 waypoints, 11 spans, 4 461 272.69 units, a single wrap to arc
415 645.19 and the position that arc implies.

Both figures were produced by the shipped port itself, driving the 11 retail
waypoints through `BuildRailPath`, and are pinned by
`TrainRailPlacement.RetailLevel1Train1499NeedsMoreThanOneWrap` in
`tests/test_train_rail.cpp`.

**Correction:** an earlier revision of this section reported a 12-waypoint rail of
4 435 504.8 units and a 95.18 m error. That came from a chord-sum estimate with a
mis-counted waypoint list; the figures above are computed from the implementation
against the real data.

### 3.3 What the editor does today

`source/app_lookup.cpp:427-462` already recognises the convention:

```cpp
double arcPos = (rawPos < 0.0) ? sd->totalLen + rawPos : rawPos;   // :448
arcPos = glm::clamp(arcPos, 0.0, sd->totalLen);
```

Two divergences remain, both in that file (not in this task's write scope):

1. **One addition instead of a loop.** The engine loops
   (`src/tagitem/spline.c:243-246`). For a `start_dist` more negative than the
   rail is long, `totalLen + rawPos` is still negative and the clamp then drops
   the train at arc 0. `spline_geometry::WrapRailDistance`
   (`source/renderer/spline_geometry.cpp:612-626`) implements the loop.
2. **Polyline instead of the engine curve, and a hand-rolled facing.** The editor
   samples a straight-line polyline (`sd.pts`/`cumDist`) and derives the heading
   with `atan2(-fwd.y, -fwd.x)` / `asin(-fwd.z)`
   (`source/app_lookup.cpp:415-422`), whereas the engine evaluates the Hermite
   curve through the waypoint frames and then applies
   `Matrix_SetDirection` + `orient()`. Wagon spacing is a hardcoded
   `WAGON_SPACING = 52756.0` (`source/app_lookup.cpp:466`) where the engine uses
   each wagon's own model-derived length.

The replacement primitives are ready and tested in
`source/renderer/spline_geometry.h`:
`BuildRailPath`, `WrapRailDistance`, `SampleRailPoint`, `TrackDirectionMatrix`,
`PlaceTrainOnRail` (§6). Wiring them into `app_lookup.cpp` is a follow-up for the
owner of that file.

---

## 4. Spline maths: waypoints to joints

All of this is `setup_segments`, IGI-RE `src/sound/segsetup.c:88-199`, reached
from `SplineObjSegment_Setup` (`HOOK(0x4E4530, SplineObjSegment_Setup)`,
`src/sound/segsetup.c:256-366`).

1. **Tangent per axis.** `dist = |to - from|`, then
   `Hermite_Init(h[a], from[a], to[a], rotFrom[3a] * dist, rotTo[3a] * dist)`
   (`src/sound/segsetup.c:99-101`). `rotFrom[0]`, `rotFrom[3]`, `rotFrom[6]` are
   the **first column** of the waypoint's row-major 3×3 frame — the frame's local
   X axis. `rotFrom[1]/[2]`, `[4]/[5]`, `[7]/[8]` are never read: the tangent has
   no roll component.
2. **Basis.** `h(u) = m0·u + p0 + (2p0 − 2p1 + m0 + m1)·u² + (3p1 − 3p0 − 2m0 − m1)·u³`
   (`Hermite_Init`/`Hermite_Eval`, `HOOK(0x518130)`/`HOOK(0x518190)`,
   `src/terrdraw/flare.c:45-59`). The editor's existing
   `SampleSegment`/`SampleSegmentTangent` are algebraically identical — they were
   left untouched.
3. **Mid point.** `mid = (from + to) / 2` (`src/sound/segsetup.c:103-105`). Every
   joint position and every bent vertex is expressed **relative to this mid**, so
   a span's mesh origin is the chord midpoint, not `from`.
4. **Joint count.** `o->segments = segments; if (rigid) o->segments = 1;`
   (`src/sound/segsetup.c:267-270`). The waypoint's own count is clamped to
   `min(w->segments, obj->maxSegments)` and then to 20 by
   `SplineObjWaypoint_Changed` (`HOOK(0x500EF0)`, `src/tagitem/spline.c:759-846`),
   and defaults to `obj->maxSegments` in `SplineObjWaypoint_Create`
   (`HOOK(0x5008F0)`, `src/tagitem/spline.c:457-561`).
5. **Rigid branch** (`src/sound/segsetup.c:117-141`): every joint gets
   `pos = lerp(from, to, t) - mid` and `rot = Matrix_SetRotZ(yaw)` with
   `(pitch, yaw) = Vec3_ToAngles(to - from)`. `segLength[1] = |to - from| * inv`
   with `inv = 1/segments = 1`.
6. **Curved branch** (`src/sound/segsetup.c:142-199`): joint `i` gets
   `pos = Hermite(t) - mid`; joint 0 copies `rotFrom`, joint `segments` copies
   `rotTo`, and every interior joint gets
   `Matrix_SetEuler(ax, pitch, yaw)` where `(pitch, yaw) = Vec3_ToAngles(Hermite_Slope(t))`
   and `ax = (a1f - a0f)·t + a0f` is the roll interpolated between the two
   endpoints' `Matrix_GetEuler` X angles, with the endpoint unwrap loop at
   `src/sound/segsetup.c:149-163` (`ax1 - a0 > π` → `a0 += 2π`; `!(ax1 - a0 >= -π)`
   → `a1 += 2π`) so the roll takes the short way round.
7. **Joint length.** `segLength[i] = |pos[i] - pos[i-1]|` for `i = 1..segments`
   (`src/sound/segsetup.c:186-191`), and `o->length` is their sum
   (`src/tagitem/spline.c:824-830`).
8. **Rigid frame** — `setup_rigid`, `src/sound/segsetup.c:203-250`:
   `k = 1/(maxX - minX)`, `d = to - from` (replaced by `(1,0,0)` only if it is
   exactly zero), then `o->rot` = `[ d·k | col1(RotZ(yaw)) | col2(RotZ(yaw)) ]`,
   with `d` negated when `o->reversed`, followed by `invRot = invert(rot)`.
   `minX`/`maxX` are the model's own local X extent, scanned at
   `src/sound/segsetup.c:281-294`.

`Vec3_ToAngles` (`HOOK(0x4B2D00)`, `src/file/vec.c:232-246`) is
`yaw = 0 if v[0] == 0 else atan2(v[1], v[0])` and
`pitch = 0 if |v| == 0 else -asin(v[2]/|v|)`. The `v[0] == 0` guard means a chord
with no X component produces **yaw 0**, not ±π/2 — a rigid span that runs along
+Y keeps the identity frame. `EngineEulerMatrix`
(`source/renderer/spline_geometry.cpp:486-488`) reproduces `Matrix_SetEuler`
(`HOOK(0x4B38E0)`, `src/math/matrix.c:99-110`) = `Rz(az)·Ry(ay)·Rx(ax)`, and
`BuildSplineSpan` (`source/renderer/spline_geometry.cpp:490-586`) reproduces all
of the above.

---

## 5. Bending: `SplineObj_BendVerts` → `BendSegmentMesh`

`SplineObj_BendVerts` (`HOOK(0x4E1440)`, `src/shadow/spline.c:24-91`), driven by
the `SegObj`/`SplineObjSegment` layout at `src/sound/soundtypes.h:41-69`
(`rot[10]` @0x70, `segPos[21][3]` @0x104, `segRot[20][10]` @0x200,
`segLength[21]` @0x544).

**Rigid** (`src/shadow/spline.c:29-46`):
`mid = (start + end)·0.5`, then for each vertex
`v' = M · (v.x − mid, v.y, v.z)` with `M = s->rot` — the row-major
`[d·k | col1 | col2]` frame from §4.8. Because `d·k = (to − from)/(maxX − minX)`,
the model's local X range maps exactly onto the chord.

**Curved** (`src/shadow/spline.c:48-89`):

```
inv   = 1 / (end - start)                       // end = maxX, start = minX
base  = -(reversed ? end : start)
scale = segments * inv            (negated when reversed)
f     = (base + v.x) * scale
k     = (int)x87_fistp(f)                       // truncates toward zero
if (k == segments) k--
frac  = clamp(f - k, 0, 1)
v'    = lerp( segPos[k] + segRot[k]·(0, v.y, v.z),
              segPos[k+1] + segRot[k+1]·(0, v.y, v.z), frac )
```

The x87 control word is switched to round toward zero at startup —
`FPU_SetTruncate` (`HOOK(0x498460)`, `src/dxinit/dxinit.c:75-81`, "Switches the
x87 to round toward zero (float to int casts then truncate)") is called from
`src/win/appmain.c:212` (and again at `:223`, `:274`). The `(int)x87_fistp(f)`
therefore **truncates**, so the port uses `static_cast<int>(f)` for the `f >= 0`
the branch guarantees (`source/renderer/spline_geometry.cpp:240-242`,
`TruncateToInt`). Choosing round-to-nearest instead would move the cross-section
onto the next joint frame whenever `frac > 0.5`.

Two properties fall out, and both are asserted by tests (§9): the surface is
**continuous across a joint boundary** (both sides use the same joint pair), and
the joint frames are **blended** rather than replaced by a zero-roll frame — this
is the #69 fidelity gap. The editor previously built a per-vertex true curve with
a `BuildZeroRollBasis` frame, which drops the waypoints' roll entirely and
disagrees with the engine on every rolled or pitched joint.

`BendSegmentMesh` (`source/renderer/spline_geometry.cpp:264-484`) is the port. It
returns offsets relative to the span mid in the editor's 10-float VBO layout
(position, normal, uv, uv2) with the position divided by the editor's render
scale of 40.96 (`source/renderer/renderer_splines.cpp:231-236` sets the model
matrix origin to `(p0 + p1) * 0.5`).

---

## 6. Rail evaluation and train placement

`SplineObj_GetPoints` (`HOOK(0x4FEC10)`, `src/tagitem/spline.c:219-454`) is the
only rail sampler:

1. `IsZero(obj->length)` → `at_start` (`:241-242`), which copies the first
   waypoint's matrix and fills every output point with the first waypoint's
   position (`:447-453`).
2. Signed wrap, `:242-255`:
   `if (!(d >= 0)) { do d += obj->length; while (!(d >= 0)); }` and the mirror
   `d -= length` loop. `IsZeroL(d)` → `at_start` (`:256-257`); `!(d > 0)` →
   `*found = 0` (`:258-261`).
3. Walk the segments accumulating each `seg->length`, then sub-joints by
   `SEG_LEN(s,i) = 0x548 + i*4 = segLength[i+1]`; clamp with `goto off_end`
   (`:362`, `:409`), which copies joint 0 or joint `segments`
   (`:441-443`).
4. For each of the `count` requested points, step `|spacing[i]|` along the
   polyline with a circle/angle intersection. Because `prev` always lies on the
   polyline, this reduces to an arc-length step.

The port (`source/renderer/spline_geometry.{h,cpp}`):

| Function | Line | Reproduces |
| --- | --- | --- |
| `BuildRailPath` | `spline_geometry.cpp:588-610` | concatenates spans, dropping each subsequent span's duplicated first joint, and builds the cumulative `arc` table |
| `WrapRailDistance` | `:612-626` | the full `while` wrap of `src/tagitem/spline.c:242-255` |
| `SampleRailPoint` | `:651-656` | the arc walk |
| `TrackDirectionMatrix` | `:658-701` | `Matrix_SetDirection` (`HOOK(0x4B4010)`, `src/math/matrix.c:244-277`) plus `orient()` (`src/missile/train.c:172-190`) |
| `PlaceTrainOnRail` | `:703-760` | `Train_Place` (`src/missile/train.c:209-274`) |

`Train_Place` in full, for reference:

```
offsets[0] = t->reverse ? t->length : -t->length
SplineObj_GetPoints(t->path, 0, pts, &found, t->dist, t->wagons + 1, offsets, 0)
if (!Vec3d_Equal(&pts[0], &pts[1])) {
    dir = pts[1] - pts[0]
    Matrix_SetDirection(&t->v.matrix, &dir); orient(&t->v.matrix)
    pos = pts[0] + (pts[1] - pts[0]) * t->frac
} else {
    identity(&t->v.matrix); pos = pts[0]
}
sx = t->offset[0] * kFixed;  sy = t->offset[1] * kFixed
pos.x += sx*m[1] + sy*m[2]
pos.y += sy*m[5] + sx*m[4]
pos.z += sx*m[7] + sy*m[8]
```

`orient()` is the transposed remap `row0' = −row2, row2' = old row0`; for a track
running along +X the final frame maps the model's local X to −X, which the
`TrackDirectionMatrixAppliesTheTrainAxisRemap` test pins.

---

## 7. Fixes landed in this task

| File | Lines | Change |
| --- | --- | --- |
| `source/renderer/spline_geometry.h` | 86-165 | `EngineEulerMatrix`, `SplineJoint`, `SplineSpan`, `SplineSpanJoints`, `BuildSplineSpan`, `BendSegmentMesh`, `RailPath`, `BuildRailPath`, `WrapRailDistance`, `SampleRailPoint`, `TrackDirectionMatrix`, `TrainPlacement`, `PlaceTrainOnRail` |
| `source/renderer/spline_geometry.cpp` | 90-242 | engine `Mat3` (row-major), `RotX/Y/Z`, `SetEuler`, `GetEuler`, single-axis `Hermite`, `Vec3_ToAngles`, `TruncateToInt`; `kPi` local constant |
| | 264-484 | `BendSegmentMesh` rewritten as a faithful `SplineObj_BendVerts` port (rigid frame + joint blend, mid-relative output) |
| | 486-586 | `EngineEulerMatrix`, `BuildSplineSpan` |
| | 588-760 | `BuildRailPath`, `WrapRailDistance`, `SampleRailPoint`, `TrackDirectionMatrix`, `PlaceTrainOnRail` |
| `source/renderer/renderer_splines.{h,cpp}` | `.h:58,60`, `.cpp:33,197,210-234` | `BentSpanKey::segments` added to the key, hash and `operator==`; the span is built from the container's `splineSegmentCount` (clamped 2..20) and `linearSegments` (the rigid flag); the model matrix origin is the span mid |
| `source/level/level_objects_serialize.cpp` | 194-213, 360-371 | the `SplineObj` container branches in `ParseTaskLine` and `UpdateCoordinatesInLine` (§2.2) |
| `tests/test_placement_splines.cpp` | new | 14 tests (§9) |
| `tests/test_spline_geometry.cpp` | 1-9 | added `<glm/gtc/constants.hpp>`; `glm::half_pi` was only reachable through the precompiled header, which the host gtest lane does not use |
| `CMakeLists.txt` | 312 | registers `tests/test_placement_splines.cpp` |

`source/level/level_objects_serialize.cpp`'s `ParseTaskLine` currently has no call
sites in `source/` — the live path is `UpdateCoordinatesInLine` (called from
`app_input_mouse.cpp`, `app_view.cpp`, `app_input.cpp` and
`SaveSubtreeToQSC`). Both were fixed so the reader and the writer agree;
`UpdateCoordinatesInLine` is the observable one.

---

## 8. Known remaining divergences (not in this task's write scope)

| Gap | Where | Evidence | Status |
| --- | --- | --- | --- |
| No coordinates for unenumerated task types (`AnimTask`, `CutScene`, `SCamera`, …) | `source/level/level_objects.cpp:444-447` | §2.1(a); hunk in §2.3 | owner: Lead |
| Rail wrap adds the length once, not in a loop | `source/app_lookup.cpp:448` (pre-fix) | `src/tagitem/spline.c:243-246` | **fixed in task-7 (§11)** |
| Rail sampled as a polyline, heading from `atan2/asin` | `source/app_lookup.cpp:415-422` (pre-fix) | §4, §6 | **fixed in task-7 (§11)** |
| Hardcoded `WAGON_SPACING = 52756.0`, sign from the wrong field | `source/app_lookup.cpp:463,481-482` (pre-fix) | `src/missile/train.c:221-225` | **fixed in task-7 (§11)**; the constant survives as the documented car length (§13) |
| `LevelObject` has no `straight`/`reversed` fields, so a waypoint's Align/Flip cannot be modelled | `source/level/level_objects.h:35-70` | `src/editobj/spline.c:220-222` (args 12/13) | open — see §15 |
| The container's `rigid` (arg 3) is the only rigid source; `LevelObject` has no per-span flag | `source/level/level_objects.h:65` | `src/sound/segsetup.c:267-270` | open |
| A waypoint's own `NumAreas` (arg 11) is not loaded | `source/level/level_objects.cpp:300-320` | `src/sound/segsetup.c:267-270` | open; measured negligible on rail 1500 (§13) |

---

## 9. Tests

`tests/test_placement_splines.cpp` (registered at `CMakeLists.txt:312`, run by
`bash scripts/run-host-tests.sh`):

| Test | Pins |
| --- | --- |
| `EngineEulerMatrixComposesRzRyRx` | `Rz·Ry·Rx`, checked against `glm`'s own axis rotations |
| `WaypointTangentIsTheEngineLocalXAxis` | column 0 = the Hermite tangent |
| `StraightSpanJointsSpanTheChord` | joint count, mid, chord endpoints |
| `JointLengthsSumToSpanLength` | `segLength[i]` and `length` |
| `RigidSpanHasOneSubSegmentAndRotatesWithTheChordHeading` | `segments = 1`, `Matrix_SetRotZ(yaw)` |
| `RigidSpanHeadingIsZeroForAChordWithoutAnXComponent` | the `IsZero(v[0]) → yaw = 0` guard |
| `RigidBendMapsTheModelXRangeOntoTheChordEnds` | `setup_rigid` + the rigid bend |
| `CurvedBendUsesTruncatedJointIndexLikeTheEngine` | truncation at `f = 3.6` (round-to-nearest would pick joint 4) |
| `CurvedBendIsContinuousAcrossAJointBoundary` | joint-pair blending is seam-free |
| `SignedRailScalarWrapsFromTheEnd` | the full `while` wrap |
| `RailPathChainsSpansWithoutDuplicatingSharedWaypoints` | shared waypoint handling, arc table |
| `TrackDirectionMatrixAppliesTheTrainAxisRemap` | `Matrix_SetDirection` + `orient()` |
| `TrainPlacementHonoursTheNegativeSignedScalar` | `−971400` lands at arc 3 728 600, never at the origin |
| `TrainDisplacementIsClampedToFiveMetresAndScaledBy4096` | `gTrainOffsetRange` × `kFixed` |

Observed output (host lane, `bash scripts/run-host-tests.sh`):

```
   PASS  test_placement_splines               14 passed    0 failed  (14 tests, exit 0)
   PASS  test_spline_geometry                 13 passed    0 failed  (13 tests, exit 0)
```

---

## 10. Verified / unverified

**Verified**

* Every offset, transform, constant and branch cited above against IGI-RE
  `file:line`; the container's `pos` at args 9-11 is additionally confirmed by the
  shipped `LEVEL1/objects.qsc` and its `Task_DeclareParameters` line.
* The signed train scalar and its magnitude on real data: `-4045627.5` on the
  4 461 272.69-unit rail 1500 wraps once to 415 645.19 units (§3.2). The wrap
  *loop* is additionally pinned on synthetic input, because no retail LEVEL1 train
  needs more than one addition.
* Both changed translation units compile with the exact i686-w64-mingw32 flags
  from `build-mingw/compile_commands.json`.
* Both gtest suites pass in the host lane (§9). The two tests that carry the
  #69/#59 fidelity claims were checked to be *discriminating* rather than merely
  green: `CurvedBendUsesTruncatedJointIndexLikeTheEngine` also asserts that the
  round-to-nearest result is more than 1.0 unit away from the value the port
  produces, and `TrainPlacementHonoursTheNegativeSignedScalar` asserts the
  placement is strictly off the map origin. The tests exercise the new API, so
  they were not run against the pre-fix implementation.

**Unverified**

* Live level load and cutscene playback cannot run on this macOS host (the editor
  is Windows-only and the installed `IGI.exe` is not the IGI-RE-supported build),
  so the placement fixes are verified at the unit and data level only. In
  particular, the exact rendered position of train 1499 in `LEVEL1` was **not**
  observed at runtime; §3.2 is a computation from the shipped QSC, not a trace.
* The container-position round trip through a real `objects.qsc` save has not been
  exercised end to end; the test drives `ParseTaskLine`/`UpdateCoordinatesInLine`
  directly.
* The hunk in §2.3 is a proposal for the props-parity workstream and has not been
  applied or tested.
* `test_mcp_domain_tools.DerivesTransformAndModelLayoutsFromSchemas` fails in the
  host lane with `map::at: key not found`. It is unrelated to this workstream
  (the schema layer, `source/level/task_schema.cpp`, is being edited in parallel)
  and was left untouched.

---

# Follow-up (task-7): the helpers applied to `app_lookup.cpp`

`App::EvaluateTrainTrackPositions` (`source/app_lookup.cpp:372`) now runs on the
§6 helpers instead of its own arithmetic. This section records what changed, what
it fixed, and what is still an approximation.

## 11. The three defects

| # | Old behaviour | Error | Now |
| --- | --- | --- | --- |
| 1 | `arcPos = (rawPos < 0) ? totalLen + rawPos : rawPos; arcPos = clamp(arcPos, 0, totalLen);` (`:448-449` of the old file) | one addition, so a scalar more than one rail length back collapses to arc 0 — retail train 1499 was **394 m** off (§3.2) | `spline_geometry::WrapRailDistance`, the engine's `do/while` loop (src/tagitem/spline.c:243-246) |
| 2 | `evalOnSpline` (`:406-422`): straight-line polyline through the waypoints, heading from `atan2(-fwd.y, -fwd.x)` / `asin(-fwd.z)` | a chord is not the rail (25 768 units short on rail 1500), and the heading ignored the engine's fixed axis remap (`orient()`, src/missile/train.c:172-190) | `BuildRailPath` / `PlaceTrainOnRail`, i.e. the Hermite spans of §4 and `Matrix_SetDirection` + `orient()` |
| 3 | `WAGON_SPACING = 52756.0` with `dir` taken from the sign of the parent's authored position (`:463, :481-482`) | the sign came from the wrong field — Flip Direction (arg 9) is independent of `start_dist` — and every wagon was stepped by a constant | the flip flag is read from the token, and the per-car arc/direction step follows `Train_Place` (§12) |

`getSpline` now builds a `spline_geometry::RailPath` from the container's
waypoint children (`source/app_lookup.cpp:428-465`): one span per waypoint pair,
tangents from each waypoint's own frame via `EngineEulerMatrix(rot)` (the loader
already reads a waypoint's Orientation from args 3-5 at
`source/level/level_objects.cpp:302-304`), `segments` from the container's arg 7,
and `rigid` from its arg 3.

## 12. Consist chaining

`Train_Place` (src/missile/train.c:217-225) steps each point off the *previous*
car's length, so with equal cars the m-th car sits at
`arc = dist ± m·length`, and its heading is the step to the car after it:

```
pts[0] = arc dist                  (lead car base)
pts[1] = arc dist + offsets[0]     (lead car's heading step, offsets[0] = ±length)
pts[m] = arc dist + Σ_{j<m} offsets[j]
```

Two consequences the old code got wrong and the new code implements:

* **The heading step is one car length, never the cumulative offset.** On a
  bending rail those give different frames; pinned by
  `TrainRailPlacement.HeadingComesFromASingleCarLengthStep`.
* **The consist walk clamps at the rail ends; it does not wrap.** The engine's
  `goto off_end` (src/tagitem/spline.c:441-443) copies the end joint for a point
  it cannot reach, so a car whose own arc is beyond the end is placed at its arc
  and only its *heading* is clamped. Only the lead car's own scalar is wrapped
  (src/tagitem/spline.c:242-255). `app_lookup.cpp` therefore pre-clamps a wagon's
  distance to `[0, length]` before calling `PlaceTrainOnRail`, which makes that
  helper's internal wrap a no-op — see the comment at `source/app_lookup.cpp:546-553`.

The flip flag is captured for every train *before* pass 1 rewrites `obj.pos`
(`source/app_lookup.cpp:467-475`), because the historical fallback
(`start_dist < 0`) reads the authored position that pass 1 overwrites. In the
shipped corpus the fallback and the token agree: all four non-zero `start_dist`
values in `LEVEL1` are negative and all four carry `Flip = TRUE`, while the 13
zero-position wagons are `FALSE`.

## 13. Remaining approximation

One number is still not engine-derived: the car length. The engine computes it
per car from the model's local points 0 and 1 —
`t->length = |a.x - b.x|` (`Train_ParamsChanged`, src/missile/train.c:142-167) —
and the editor does not parse those points, so
`kDefaultCarLength = 52756.0` (`source/app_lookup.cpp:408-413`) is a documented
constant. It only shifts cars along the rail; it cannot move the rail.

A waypoint's own `NumAreas` (arg 11) is also not loaded, so the editor uses the
container's single segment count for every span. Measured against the real rail
1500 the difference is 4 461 592.91 vs 4 461 272.69 units — **320 units in 4.46
million, 0.0072%** — pinned by
`TrainRailPlacement.UniformSegmentCountIsANegligibleApproximation`.

A third, smaller divergence: a car clamped to arc 0 takes `PlaceTrainOnRail`'s
`at_start` path (identity frame, first joint), whereas the engine's `off_end` for
a backwards step copies joint 0's *frame* (`Segment_CopyJoint(out, m, seg, 0)`).
The position is identical; the frame differs when the consist hangs off the start
of a curved rail. Not fixed — it needs the consist to start within one car length
of the rail's start, which no shipped train does.

## 14. Tests added

`tests/test_train_rail.cpp` (registered at `CMakeLists.txt:315`, 12 tests). The
retail-data tests use the real 11 waypoints of rail 1500 and the real
`start_dist` values, so they are regression tests against shipped mission data,
not synthetic fixtures.

| Test | Pins |
| --- | --- |
| `SignedScalarWrapsMoreThanOnceLikeTheEngine` | the wrap loop vs the old single add + clamp |
| `PlacementUsesTheWrappedArcNotTheClampedOne` | the placement follows the wrapped arc |
| `RetailLevel1Train1499NeedsMoreThanOneWrap` | the 394 m retail error (§3.2) |
| `TheOtherRetailTrainsAgreeWithASingleAdd` | why the bug hid — the other three trains agree |
| `UniformSegmentCountIsANegligibleApproximation` | the 320-unit segment-count substitution |
| `HeadingComesFromASingleCarLengthStep` | one-length step, not the cumulative offset |
| `FlipDirectionSetsTheSignOfTheSpacing` | Flip sets the spacing sign and the frame |
| `CarsPastTheRailEndClampTheHeadingNotTheArc` | `off_end` clamps, never wraps |
| `ConsecutiveCarsAreOneCarLengthApart` | the consist chain |
| `DisplacementFollowsTheEnginesAxisOrder` | `gTrainOffsetRange` × `kFixed`, and the engine's element order |
| `AWrappedScalarOfZeroTakesTheEngineStartPath` | `IsZeroL(d) -> at_start` |
| `DegenerateRailsAreNotPlaced` | not-found for an empty/non-finite rail |

**Verified**

* Red/green: with `WrapRailDistance` temporarily replaced by the old
  single-add-and-clamp arithmetic, the suite reports
  `FAIL test_train_rail 9 passed 3 failed (12 tests)` — `SignedScalarWraps…`,
  `PlacementUsesTheWrappedArc…` and `RetailLevel1Train1499NeedsMoreThanOneWrap`.
  The original function was then restored byte-identically and the suite returns
  12/12. Command:
  `IGI_HOST_NO_CACHE=1 IGI_HOST_TESTS=tests/test_train_rail.cpp bash scripts/run-host-tests.sh --show-output`.
* Green: `PASS test_train_rail 12 passed 0 failed (12 tests, exit 0)`; together
  with the two task-4 suites, `OK: 39 tests passed in 3 suites` (14 + 13 + 12),
  exit 0, re-run against the current tree after the shared-header and
  `LevelObject` changes landed. Command:
  `IGI_HOST_NO_CACHE=1 IGI_HOST_TESTS=tests/test_train_rail.cpp,tests/test_placement_splines.cpp,tests/test_spline_geometry.cpp bash scripts/run-host-tests.sh`.
  (An earlier revision of this section reported 36 tests in 3 suites; that was the
  same three suites while `test_train_rail` still held 9 of its 12 tests.)
* `python3 tools/igi-re-parity/tu-check.py source/app_lookup.cpp
  source/renderer/spline_geometry.cpp source/renderer/renderer_splines.cpp
  source/level/level_objects_serialize.cpp tests/test_train_rail.cpp
  tests/test_placement_splines.cpp tests/test_spline_geometry.cpp` → `OK`,
  0 failed, re-run against the current tree.
* The rail length and every wrapped arc in §3.2 were produced by driving the real
  waypoint data through this implementation, not by hand arithmetic.

**Unverified**

* Live cutscene playback and a live level load still cannot run on this macOS
  host, so the corrected train positions have not been observed in the renderer.
  The evidence is the shipped data plus the engine's arithmetic, not a trace.
* `EngineMatrixToEuler` is now used to store the placement frame in
  `LevelObject::rot`. It is the exact inverse of the editor's own
  `igi::IgiEulerRotationMatrix` (`source/renderer/igi_transform.h:37-42`, which is
  `Rz·Ry·Rx`, the same composition as `Matrix_SetEuler`), but no rendered frame
  was inspected.
* The `argTokens[9]`/`[10]`/`[11]` reads for Flip Direction and the two
  displacements are verified only against the shipped corpus and by compilation;
  they are not unit-tested, because the lambdas live in a translation unit that
  needs `windows.h`. The placement arithmetic they feed is unit-tested.

---

# 15. Answer: is the missing waypoint `straight`/`reversed` a real gap?

Question from the Lead: `LevelObject` has no fields for a waypoint's Align (arg 12)
and Flip (arg 13) — is that a behavioural gap in the editor's rendering of paths,
or purely a missing edit surface?

**Answer: it is behavioural, and it is more than a rendering flag — Align also
*moves* waypoints. But no shipped mission uses either flag, so it is unreachable
in retail data.**

## 15.1 What the two flags do in the engine

**Align → `w->straight`.** Three separate effects, none of them cosmetic:

1. It forces the span to be rigid, by the same path as the container's flag:
   `w->rigid = obj->rigid || w->straight;` (src/tagitem/spline.c:769) and
   `SplineObjSegment_Setup(w->seg, …, w->rigid || w->straight, w->reversed, …)`
   (src/tagitem/spline.c:80-81). A rigid span has exactly one sub-segment
   (src/sound/segsetup.c:267-270) and its frame comes from `setup_rigid`
   (src/sound/segsetup.c:203-250), so the joint positions, the bend and the
   segment lengths all change.
2. It makes the waypoint part of a *straightened run*.
   `Waypoint_Straighten` ("lines up a run of straight waypoints evenly between its
   two ends", src/tagitem/spline.c:686-746) walks the maximal run of consecutive
   `straight` waypoints, then **overwrites the intermediate waypoints' positions**
   — `pos += step` where `step = (last - first) / count` — and **overwrites every
   waypoint's orientation in the run** with a single
   `Matrix_SetEuler(0, pitch, yaw)` built from that step, calling
   `Waypoint_SetupSegment` and `DynCube_Move` as it goes. Align therefore changes
   where waypoints *are*, not just how a span is drawn.
3. It changes the span-length calculation:
   `if (w->straight || (next && next->straight))` (src/tagitem/spline.c:830).

**Flip → `w->reversed`.** Passed straight into the segment setup
(src/tagitem/spline.c:80-81) and consumed twice: `setup_rigid` negates the frame's
first column (`d` negated when `o->reversed`, src/sound/segsetup.c:230), and
`SplineObj_BendVerts` negates its base and scale (src/shadow/spline.c:48-51). The
net effect is that the model is **mirrored along the span**.

## 15.2 Reachability in shipped data

Scanned with the retail corpus at `/Users/haseeb-mir/Downloads/IGI`:

```
LEVEL1/objects.qsc : 19 SplineObjWaypoint, 6 SplineObj, 18 Train
                     waypoint (Align, Flip, autoOrient) -> (FALSE, FALSE, FALSE) x 19
                     container (rigid, maxSegments) -> (TRUE, 20) x 5, (FALSE, 20) x 1
                     train (start_dist, path, reverse) -> (-4045627.5, 1500, TRUE),
                          (-575416.9375, 1500, TRUE) x2, (-971400.125, 1500, TRUE),
                          (-3744122.25, 1500, TRUE), (0, 1500, FALSE) x 13
LEVEL2/objects.qsc : no SplineObj, no SplineObjWaypoint, no Train
LEVEL4/objects.qsc : no SplineObj, no SplineObjWaypoint, no Train
```

Every waypoint in the only mission that has rails is `Align = FALSE, Flip =
FALSE`. Note that 5 of the 6 containers *do* set Linear Segments (the container's
own `rigid`, arg 3) — so rigid rails are the common case and the editor already
honours them.

## 15.3 Recommendation

* **For parity with the shipped game: low priority.** The flags are never set, so
  no retail level can be drawn wrong by their absence, and the container's `rigid`
  covers the rigid case that actually occurs.
* **For authoring: a real correctness gap.** The inspector can already toggle
  Align/Flip on a waypoint (they round-trip through `argTokens`), and the editor
  will then draw a curved, unmirrored span where the game would draw a straight,
  mirrored one — and, for a run of Align waypoints, would place the intermediate
  waypoints somewhere else entirely. If the editor is meant to author new rails,
  the fix needs both fields *and* the `Waypoint_Straighten` pass; if it is only
  meant to edit shipped levels, logging it as a known limitation is enough.

The honest framing for the issue tracker: *behavioural but not reachable* —
"unreachable in shipped data" is the reason to defer it, not "cosmetic".
