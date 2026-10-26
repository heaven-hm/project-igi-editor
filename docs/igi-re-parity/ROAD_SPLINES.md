# Road and track splines — the authored model (#79) and the curve (#69)

Status: implemented in `source/runtime/spline_rules.{h,cpp}`; tested in
`tests/test_spline_rules.cpp`. Every constant, offset, argument count, matrix
element and line number below is cited to IGI-RE
(`/Users/haseeb-mir/Documents/Code/IGI-RE`), the faithful C decompilation of
retail `IGI.exe` (function name + `HOOK`/`ORIG` address + `file:line`). No web
source and no other port is used as authority.

Nothing in this document is a rendered result. The editor's GUI cannot run on the
development host (it needs OpenGL 4.1; the host offers 2.1), so the evidence is
the host-lane unit and corpus tests quoted in §11. No renderer, level, GUI or
common code was modified, and the port is not wired into a draw path.

**Which rule answers which issue.**

| Issue | What it asks for | Where it is answered here |
|---|---|---|
| **#79** "Add OpenIGI road and track spline objects" | the authored `SplineObj` / `SplineObjWaypoint` data model, and where the longitudinal axis of a road segment comes from | §1 (parameter tables and defaults), §2 (the authored clamps), §3 (the new-waypoint/authoring helpers), §4 (units and the model extent → `start`/`end` → the span), §5 (the mesh-part contract), §7 (cache), §8 (corpus) |
| **#69** "[PARITY][RENDERER] Roads/bridges: Hermite-based spline segment placement" | turn control points into placed geometry | §6 (the Hermite basis, `setup_segments`, `setup_rigid`, the vertex bend, the radius) |

The two issues share one module because they share one data structure: the engine
builds a `SplineObjSegment` per *pair* of waypoints, and that segment is where the
authored data (#79) and the curve (#69) meet. §9 records the two inherited
assumptions this port corrects, with the tests that pin the correction.

---

## 1. The authored model

### 1.1 `SplineObj` — the container

`SplineObj_Open` is `HOOK(0x502250)` at `src/editobj/spline.c:177-225`. It sets
one range (`src/editobj/spline.c:202-203`):

```c
gSplineObjSegmentRange.min = 2;
gSplineObjSegmentRange.max = 20;
```

then builds a 10-row table (`ParamTable_New(10)`, `:204`) and an 8-row table
(`ParamTable_New(8)`, `:215`). The rows, in registration order:

| # | Engine field (`src/editobj/spline.c:205-214`) | Authored name | Declared type | Script tokens |
|---|---|---|---|---|
| 0 | `rigid` | Linear Segments | `bool8` | 1 |
| 1 | `collide` | Display waypoints | `bool8` | 1 |
| 2 | `snap` | Snap Length | `bool8` | 1 |
| 3 | `autoOrient` | Automatic Orientation | `bool8` | 1 |
| 4 | `maxSegments` | Number of Matrices / Segment | `Int32` + range 2…20 | 1 |
| 5 | `unk20` | Collision LOD | `Int32` | 1 |
| 6 | `pos` | Position | `Real32x3` | 3 |
| 7 | `angle` | Gamma Orientation | `Angle` | 1 |
| 8 | `color1` | Dirlight Angular Effect | `RGB` | 3 |
| 9 | `color2` | Dirlight Ambient Effect | `RGB` | 3 |

16 script tokens, and the retail data agrees token for token (§8.1).

Two of the authored labels do not say what the field does, and both are
reproduced as authored:

- **"Display waypoints" writes `collide`**, which is a *collision* switch, not a
  display switch. With it set, `SplineObjWaypoint_Changed` adds each waypoint
  marker to the collision tree as a `DynCube` of radius
  `VirModel_GetRadius(model) * Waypoint_MaxScale(w)`
  (`src/tagitem/spline.c:793-797`), and clears it when unset (`:788-792`);
  `SplineObjWaypoint_CollLines`/`CollLine`/`Slot10`/`Slot11` also gate on it
  (`src/editobj/spline.c:379-404`). All six LEVEL1 containers author it FALSE.
- **"Collision LOD" writes `unk20`, which nothing reads.** `unk20` has no range
  (`:210`), is initialised to 0 (`:231`), is clamped at zero
  (`src/editobj/spline.c:316-317`), and is passed all the way into
  `SplineObjSegment_Setup`'s `int unk` parameter
  (`src/tagitem/spline.c:81`, `:819`) where the callee's first statement is
  `(void)unk;` (`src/sound/segsetup.c:266`). It is a dead authored parameter in
  this build. The port parses and stores it, and does nothing else with it, which
  is exactly what the engine does.

`SplineObj_Create` is `HOOK(0x5026E0)` at `src/editobj/spline.c:227-242` and sets
the defaults the parser mirrors: `angle = oldAngle = kPi`, `unk20 = 0`,
`rigid = collide = snap = 0`, `maxSegments = 20`, `length = 0`, `pos =
oldPos = (0,0,0)`, `color1 = (1,1,1)`, `color2 = (0,0,0)`.

### 1.2 `SplineObjWaypoint`

Same call, `src/editobj/spline.c:215-223`:

| # | Engine field | Authored name | Declared type | Script tokens |
|---|---|---|---|---|
| 0 | `rot` | Orientation | `Real32x9` | 3 |
| 1 | `point` | Position | `ObjectPos` | 3 |
| 2 | `modelName` | Waypoint Model | `String16` | 1 |
| 3 | `segModel` | Segment Model | `String16` | 1 |
| 4 | `segments` | NumAreas | `Int32` + range 2…20 | 1 |
| 5 | `straight` | Align | `bool8` | 1 |
| 6 | `reversed` | Flip | `bool8` | 1 |
| 7 | `autoOrient` | Automatic Orientation | `bool8` | 1 |

12 tokens. The two 3-token rows are the aliasing in
`src/physobj/paramops.c:139-186`: `Real32x9` consumes three `Real32` arguments
(`ParamReal32x9_Parse`, `HOOK(0x4EFBA0)`, `src/physobj/paramops.c:423-435`) and
`ObjectPos` is `Real64x3`.

**Two different models per waypoint.** "Waypoint Model" is the marker the editor
shows and the engine's collidable cube uses (`w->obj.model`); "Segment Model" is
the road/bridge geometry, and it is the one whose local X extent becomes the span
(§4). In the retail LEVEL1 data the marker is the literal string `"waypoint"` and
the segment models are `368_01_1`, `322_01_1`, `323_01_1`, `320_02_1`.

`SplineObjWaypoint_Create` is `HOOK(0x5008F0)` at `src/tagitem/spline.c:457-561`.
Its defaults are `seg = NULL`, `rigid = obj->rigid`, `segments =
obj->maxSegments`, `reversed = 0`, `inTree = 0`.

### 1.3 What the editor must also model

`SplineObj_Changed` (`HOOK(0x502740)`, `src/editobj/spline.c:246-322`) is the
container-level edit rule, and it is a *delta* rule, not an absolute one:

- **Rotate** (`:256-295`). If `angle != oldAngle`, the pivot is the FIRST
  waypoint's point (`:263`), every waypoint's position is rotated about it by
  `RotZ(angle - oldAngle)` (`:262`) in *world* space (`:266-276`), and the
  waypoint's frame is recomposed element by element (`:278-286`).
- **Translate** (`:296-315`). If any component of `pos` changed, the delta is
  rotated by `RotZ(angle)` using the **new** angle (`:304`) and added to every
  waypoint (`:305-315`).

There is a genuine inconsistency between the two halves, and the port reproduces
it rather than repairing it. For the first waypoint of the rotation, whose
`dx = dy = dz = 0`, the point does not move but the frame still changes. Working
through the nine element expressions with row-major indices
(`n[i][j] = Σ_k r[i][k]·m[k][j]`, i.e. `N = R·RotZ(Δ)`):

- the **position** rotation is a world-space pre-multiplication about the world
  Z axis through the pivot;
- the **frame** rotation is a post-multiplication, i.e. a turn about the frame's
  own local Z axis.

For a flat frame (a pure yaw) the two agree, which is why the rule looks correct
in practice. For a tilted frame they do not: the frame turns in a different
plane from the position. The retail rail has exactly such a waypoint — LEVEL1's
waypoint 6 authors `Orientation = (6.2831811904907227, 6.0213789939880371,
3.1415920257568359)` — so rotating the container that holds it would tilt the
geometry rather than turning it rigidly. This is what the code does; the port
does the same and says so here instead of "fixing" it.

Two more details of `SplineObj_Changed`: the frame's `count` field is
`w->rot.count + m.count + 1` (`:287`), and the oracle's own annotation at `:288`
records that the original renormalises a *local copy* of the matrix once that
count passes 1000 and then discards it — so the accumulated count is what is
stored either way, and the renormalisation has no effect. The port keeps angles
rather than the matrix (§10), so neither is observable.

---

## 2. The authored clamps, `src/tagitem/spline.c:759-846`

`SplineObjWaypoint_Changed` (`HOOK(0x500EF0)`) is where an authored waypoint
becomes geometry. In order:

1. **Auto-orient** (`:767-768`): `if ((obj->autoOrient || w->autoOrient) &&
   (prev || next) && !obj->rigid)`, rebuild the frame from the neighbours (§3.3).
   Note the asymmetry: the waypoint's own flag works even when the container's is
   off, but a rigid container never auto-orients.
2. **Rigid** (`:769`): `w->rigid = obj->rigid || w->straight`.
3. **Segment count** (`:771-772`): `n = min(w->segments, obj->maxSegments)`, then
   `w->segments = n > 20 ? 20 : n`. There is **no lower clamp**: `NumAreas 0`
   survives, and `inv = 1 / segments` in `setup_segments` then divides by zero
   (§6.6).
4. **Snap** (`:774-785`, §3.4).
5. **Collision cube** (`:788-798`).
6. **Frame copy** (`:800`): `w->obj.matrix = w->rot`.
7. **Marker model** (`:801-813`): if the authored name does not start with `*`,
   look it up; on failure prefix the *stored* name with `*` so the lookup is not
   retried.
8. **Segment** (`:815-828`): create it if `prev` exists, otherwise free any
   existing one (the first waypoint has no incoming span).
9. **Align** (`:830-831`, §3.5).
10. **Rail length** (`:833-839`): `obj->length = Σ p->seg->length` over the
    waypoints AFTER the first.
11. Segment colours (`:840-843`) and the attachment update (`:844`).

`SplineEffectiveSegmentCount` and `SplineSpanIsRigid` in the port are steps 2-3;
`SplineObjParams`/`SplineObjWaypointParams` carry the rest.

---

## 3. The authoring helpers (#79)

These are the rules the editor needs to create and edit a road interactively.

### 3.1 A new waypoint's position, `src/tagitem/spline.c:457-561`

`SplineObjWaypoint_Create` has three cases (`src/tagitem/spline.c:463-546`), and
they are asymmetric:

| Case | Step |
|---|---|
| with `prev` (`:463-497`) | the previous waypoint's segment extent `(seg->end - seg->start)` if it has a segment (`:483-489`), else `kDefaultSegLength = 141314.9f` (`:490-494`) |
| no `prev`, but `next` (`:498-536`) | the extent of the segment on the waypoint *after* `next` (`:518-529`), and if that one has no segment either, **one unit** (`:530-532`: only `dx = x` is assigned, so all three components stay the normalised tangent) |
| neither (`:537-546`) | the identity frame, both model names empty, a zero point, `straight = 0`, `autoOrient = 0` |

The port's `SplineNewWaypointPoint(neighbour, step, extent, forward, out)` covers
these as three explicit `SplineNewWaypointStep` values rather than relying on
null pointers, and reproduces the backward one-unit asymmetry, which is almost
certainly a decompilation-faithful bug in the original: `:531` assigns only the X
component of the offset, leaving Y and Z at their already-normalised (unit) values
instead of scaling them.

Before stepping, the neighbour's tangent is normalised with
`1.0L / x87_sqrt(x*x + y*y + z*z)` (`:476-482`, `:511-517`), so the step is
exactly one model length regardless of the frame's scale. The forward and backward
multiplications are deliberately not the same expression: forward narrows `x` as
`long double` and `y`/`z` as `float` (`:487-489`); backward narrows `dx` as
`long double` and `y`/`z` as `float` (`:527-529`). The oracle's text was followed
literally. The backward case then fires the **next** waypoint's own `Changed`
event (`:533`, `:554-559`), so inserting a waypoint before an existing one
re-running the rules on the successor is part of the rule, not a side effect.

The tail (`:547-553`): a new waypoint starts with `seg = NULL`, `rigid =
obj->rigid`, `segments = obj->maxSegments`, `reversed = 0`, `inTree = 0`, and its
marker model is resolved immediately
(`RigidDynCubeObj_Init(gCurrentWorld, w, VirModel_Find(w->modelName))`).

### 3.2 The new waypoint's frame

`SplineObjWaypoint_Create` copies the neighbour's `modelName`, `segModel`,
`point`, `rot`, `straight` and `autoOrient` (`:467-475`), then applies the step.
The backward case copies `straight` (`:509`) but **not** `autoOrient`, which it
leaves at the new task's initial value — so a waypoint inserted before another one
does not inherit its successor's auto-orient flag. The port exposes `SplineWaypointFrame` (authored Euler triple
→ engine frame via `Matrix_SetEuler`) so the caller can rebuild the frame the same
way.

### 3.3 Auto-orient, `src/tagitem/spline.c:598-676`

`Waypoint_AutoOrient` builds the frame from the summed direction to the
neighbours:

```
dir = (prev ? point - prev->point : 0) + (next ? next->point - point : 0)
```

An all-zero `dir` gives the X axis `(1,0,0)` (`:618-621`); otherwise the first
column is set to `dir` (`:623`) and later normalised into `u` (`:655-656`). The other two columns come from a Gram-Schmidt-like
construction: if the first column has no X or Y component the second is `(0,1,0)`,
else the second is `(b, a, 0)` with `b = r[1]`, `a = -r[0]` normalised by
`1.0L/x87_sqrt(b*b + a*a)`; the third is the cross product of the first and
second (`:646-654`), then `u` is normalised (`:655-656`), the third column is
recomputed as the cross product of the third and the first (`:657-659`), a fourth
vector is the cross product of that and `u` (`:660-662`), and finally the three
axes are re-normalised in the order `(c, 2,1,0)` then `(d, 0,1,2)`
(`Vec3f_NormalizeOrdered`, `:586-595`). The frame is then written as rows-from-
columns: `m[0]=u[0], m[1]=c[0], m[2]=d[0], m[3]=u[1] … m[8]=d[2]` (`:665-673`), so
the tangent read by `setup_segments` (`m[0], m[3], m[6]`) is `u`. `m.count = 0`.

### 3.4 Snap, `src/tagitem/spline.c:774-785`

```
dx = point.x - prev->point.x;  dy = ...;  dz = ...;      // dx, dy float; dz long double
len = seg->end - seg->start;                             // the segment model's X extent
k   = len / sqrt(dx*dx + dy*dy + dz*dz);
point = prev->point + k * (dx, dy, dz)
```

So "Snap Length" forces the waypoint to sit exactly one model length from its
predecessor, on the authored direction. The first waypoint has no predecessor and
is never snapped. `k` is `long double` (`:779`); `dx`/`dy` are `float` and `dz`
is `long double`, so `dx*dx + dy*dy` is a `float` sub-expression that is widened
when `dz*dz` is added.

### 3.5 Align, `src/tagitem/spline.c:686-756`

`Waypoint_Straighten` re-spaces a maximal run of consecutive "Align" waypoints and
gives the whole run one frame:

- The run is found by walking backwards from `w` (or from `next` when `w` is not
  straight) to the first non-straight waypoint, and forwards to the last straight
  one (`:698-713`).
- `count` is exactly the number of steps between those two anchors: the backward
  walk counts the non-straight waypoint it stops on (`:699-709`) and the forward
  walk counts only straight members (`:710-713`). `step = (last - first)/count` is
  therefore an even split.
- Both anchors keep their **authored positions** — the engine never writes
  `last->point` (`:749-753`) precisely because `last` is the far anchor. Only the
  interior waypoints are moved (`:735-748`).
- The frame is `Matrix_SetEuler(0, pitch, yaw)` from the step vector, so the roll
  is zero for the whole run.
- A run with a single member returns immediately (`:714-715`), so a lone "Align"
  waypoint at the very start of a rail is left completely untouched — no frame, no
  position.
- `Waypoint_SetupSegment` is re-run for `first`, the interior, `last` and the
  waypoint after `last` (`:733`, `:747`, `:753-755`).

`SplineStraightenRun` ports the geometry (positions and frames) and documents that
the segment rebuilds and collision-cube moves need the world.

---

## 4. Units, and where the longitudinal axis comes from (#79)

### 4.1 Units

`kFixed = 4096` units per metre (`src/math/mathvars.h:7`); the editor's own
conversion is `kMeter = 1/4096` (`src/editobj/spline.c:61`), i.e. the same
direction. `kFar = 4096.0f` (`src/editobj/spline.c:60`) is the radius
`SplineObj_GetInfo` reports to the renderer's far-distance cull.

### 4.2 The axis is the model's own extent, not a hardcoded axis

`SplineObjSegment_Setup` is `HOOK(0x4E4530)` at `src/sound/segsetup.c:256-366`.
After building the joints it scans the segment model's local X range
(`:277-293`):

```c
o->minX = 0; o->maxX = 0;
for (k = 0; k < LOD_COUNT(models); k++) {
    char *info = LOD_INFO(models[k]);
    if (*(int *)(info + 0x20) == 0) {              // mesh format 0 only
        char *p = *(char **)(info + 0x24);         // the render mesh
        float *v = *(float **)(p + 0x10);          // RigidMesh.vertices
        for (j = 0; j < *(int *)(p + 0xC); j++, v += 8) {
            if (!(*v >= o->minX)) o->minX = *v;
            if (*v >  o->maxX)    o->maxX = *v;
        }
    }
}
```

`SplineObjSegment.start`/`.end` are those two fields (0xFC/0x100,
`src/sound/soundtypes.h:55-56`). This is the answer to #79's "longitudinal local
axis derived from each segment extent": **it is the model's own local X range, per
model, read from the mesh at placement time** — no axis is hardcoded, and the same
road mesh stretches to fit whatever length the author drew. The port's
`SplineScanModelExtent` reproduces the scan, including two details that are easy
to "fix" and wrong to fix:

- both bounds start at **zero**, so the range always contains the model origin
  even when every vertex is positive;
- the min test is the NaN-swallowing `!(*v >= minX)` form while the max test is a
  plain `>`, so a NaN X moves `minX` to NaN and never touches `maxX`.

The scan reads the RENDER vertex array (stride 8 floats: position, normal, uv —
`src/render/render.c:57-62`), while the vertices actually bent by
`SplineMesh_GetVerts` are the COLLISION array (`MeshHeader.coll` →
`CollMesh.verts`, stride 4, `src/magicobj/rigidcoll.c:46-54`). These are distinct
arrays in the engine's data model (MREF/MDAT versus CVTX); that the retail road
models agree on X between them is **unverified** here, because the editor's MEF
reader was not exercised by this task. The port keeps the two separate:
`SplineExtentSource` (scan) and `SplineMeshPart` (bend).

### 4.3 The rigid frame, `src/sound/segsetup.c:203-251`

`setup_rigid` maps the model X range onto the chord:

```
if (Vec3d_Equal(from, to)) return radius;   // :208-209, radius stays 1, frame NOT written
dx = to.x - from.x;  d = (dx, to.y - from.y, to.z - from.z)     // :210-213
if (d == 0) d = (1, 0, 0)                   // :214-218
k  = 1 / (maxX - minX)                      // :219, long double, NO zero guard
rot = transpose(segRot[0])                  // :220, the joint-0 frame setup_segments wrote
rot[0..2] = d                               // :221-223, the chord, unscaled
r0/r1/r2 = k * rot[0..2]; rot[0..2] = r     // :224-229
if (reversed) rot[0..2] = (-r0, -r1, -r2)   // :230-235  ("Flip")
rot = transpose(rot)                        // :236
n0/n1/n2 = |row 0|, |row 1|, |row 2|        // :237-242, sqrtl
radius = max(n0, n1, n2)                    // :243-247, the `!(a >= b)` NaN form
transpose10(invRot, rot); Matrix_Invert(invRot)                 // :248-249, never read
```

One consequence of :220 that matters for bridges: the frame it starts from is the
one `setup_segments`' **rigid** branch wrote for joint 0, which is
`Matrix_SetRotZ(yaw)` alone (`:129-135`) — `pitch` is computed and then never used. So a
sloped *straight* span keeps **level** cross-sections while its positions follow the
slope, and the model's own roll is ignored. That is the authored meaning of
"Linear Segments".

Because the first column becomes `chord / (maxX - minX)`, a vertex at `minX` maps
to `-chord/2` and one at `maxX` to `+chord/2` in the segment's local space — the
whole chord, centred on the segment origin (the chord midpoint). That is why the
joint positions are stored as `curve(t) - mid`: the caller adds the segment
position and the two halves line up.

The port's `BuildSplineRigidFrame` returns the longest column and does **not**
produce `invRot`: nothing in the decompilation ever reads it. The only occurrences
are the declaration (`src/sound/segsetup.c:46`) and these two writes, and for a
curved span with 20 sub-segments the frame written for the last joint lands on top
of it anyway — `SEG_ROT(o, 20) = 0x200 + 20 * 40 = 0x520`, which is `invRot[0]`
(the macro's own comment at `:68` warns "the last one may run into `invRot`").

### 4.4 The radius, `src/sound/segsetup.c:294-322`

The bound handed to `DynCube_Move`:

- **rigid** (`:294-305`): `best` = the largest LOD radius over the segment
  model's LODs (`*(float *)(LOD_INFO(*models) + 0x78)`, walked backwards, NaN
  never winning), then `radius = best * longest_column * kRadiusScale` with
  `kRadiusScale = 0x1.19999ap+0f` (`:66`). `SplineRigidRadius` is that product,
  left to right.
- **curved** (`:306-323`): bend the first two mesh parts and take the largest
  `|v|`, with the sum ordered `x, z, y` (`v[0]*v[0] + v[2]*v[2] + v[1]*v[1]`), and
  floor the running maximum at `1.0f` **after each part** — so part 0 alone can
  raise the radius to 1 before part 1 is measured. `SplineBentRadius` does this
  over the parts the caller supplies.

---

## 5. `SplineMesh_GetVerts` — the mesh contract (#79, #69)

`HOOK(0x49A050)`, `src/dxinit/spline.c:36-105`. This is the function that turns
(a) a placed span and (b) a collision mesh part into the vertex array the
renderer draws, so its *contract* matters as much as its math:

- **Model and part indexing** (`src/dxinit/spline.c:41-43`): `m =
  LOD_INFO(o->models[model])` (i.e. `*(char **)(*(char **)(model + 0xC) + 0xC)`),
  then `p = (MeshPart *)(*(char **)(m + 0x2C) + part * sizeof(MeshPart))` with
  `sizeof(MeshPart) == 0x20`. The vertices are `float (*)[4]`, stride 4
  (`src/magicobj/rigidcoll.c:46-54`,
  `src/magicobj/magicobjtypes.h:96-103`), unlike the 8-float render array of §4.2.
  The engine calls it with the segment's current LOD (index 0 in
  `src/sound/segsetup.c:311` because `SplineObjSegment_Setup` resets `o->lod = 0`
  at `:265`; `o->lod` in `src/shadow/splineray.c:501`) and with part 0 or 1, so the
  port takes a resolved LOD extent (`SplineMeshBend::extent`) and a resolved part
  (`SplineMeshPart`) instead of re-deriving the pointer chains.
- **Return value.** It returns `out - p->vertCount * 3`, i.e. a pointer to the
  *base* of the buffer it filled, and writes the count through the last argument
  (`:103-104`). The output is 3 floats per vertex and vertex *i* of the output
  corresponds to vertex *i* of the input — the mapping is 1:1 and order-preserving,
  which is what lets the renderer keep the index buffer unchanged.
- **Buffer.** The destination is a static `float gSplineVerts[60000]`
  (`store_B46D40`, `src/dxinit/spline.c:118`) with **no** bounds check; the
  engine trusts the model. The port returns 0 when the part does not fit rather
  than writing past the end (§10).
- **Two code paths.**
  - *rigid* (`:49-59`): `mid = (start + end) * 0.5`,
    `v' = rot * (v.x - mid, v.y, v.z)`.
  - *curved* (`:60-100`): `base = -(reversed ? end : start)`,
    `scale = segments * (1/(end - start))` negated when reversed,
    `t = (base + v.x) * scale`, `k = trunc(t)`, `if (k == segments) k--`,
    `f = clamp(t - k, 0, 1)`, then
    `v' = lerp(joint[k] + rot[k]·(0, y, z), joint[k+1] + rot[k+1]·(0, y, z), f)`.

Note the second path exactly: the model's **own vertices are bent**, by blending
the two adjacent joint frames. The curve is not re-tessellated per frame and the
model's X coordinate is only a parameter, not a position (§9.2).

`inv = 1/(end - start)` is computed as a **float** before either path
(`src/dxinit/spline.c:39`), so a model with no X extent gives an infinite scale
and every vertex collapses onto joint 0 or 1 (§6.6).

The renderer has an **independent second copy** of this arithmetic in
`spline_point`/`spline_params` (`src/render/render.c:286-336`), used by
`Direct3DRender_DrawSplineMesh` (`HOOK(0x49C850)`, `:537-604`) and
`Direct3DRender_FillSplineVB` (`HOOK(0x49CC80)`, `:613-670`). It declares the same
intermediate types (`p0`/`c0` `long double`, `b1`/`b2`/`c1`/`c2` `float`), so
`SplineMeshGetVerts` describes both, with one difference worth recording:
`spline_params` computes `inv` as a `long double` division narrowed to `float`
(`:325`) whereas `SplineMesh_GetVerts` divides in `float` (`src/dxinit/spline.c:40`)
— so a near-degenerate extent can differ in the last bits between the renderer and
the shadow/ray path. The port follows `SplineMesh_GetVerts`.
`Direct3DRender_FillSplineVB` then writes the
vertex normal, transformed by the same joint frame (`dst[3] = rot·normal`,
`:660-662`), and copies the UV straight through (`:663-664`) — that is the
fixed-function lighting input, not placement, and is outside this module's scope.
Its rigid branch also confirms the centring: `center = (splineMax + splineMin) *
0.5` (`:637`) and `x = v[0] - center` (`:640`), the same midpoint
`SplineMesh_GetVerts` uses.

---

## 6. The curve (#69)

### 6.1 The basis is cubic Hermite

`src/terrdraw/flare.c:44-69`: `Hermite_Init` `HOOK(0x518130)`,
`Hermite_Eval` `HOOK(0x518190)`, `Hermite_Slope` `HOOK(0x5181C0)`:

```
h(u) = m0·u + p0 + (2p0 - 2p1 + m0 + m1)·u² + (3p1 - 3p0 - 2m0 - m1)·u³
h'(u) = m0 + 2(2p0 - 2p1 + m0 + m1)·u + 3(3p1 - 3p0 - 2m0 - m1)·u²
```

`Hermite_Eval(h, 0) = p0`, `Hermite_Eval(h, 1) = p1`, `Hermite_Slope(h, 0) = m0`,
`Hermite_Slope(h, 1) = m1`, and for `p0 = p1 = 0, m0 = 1, m1 = 0` the value at
`u = 0.5` is exactly `0.125`. That last number is what distinguishes this basis
from a Catmull-Rom or Bezier parameterisation of the same control points, and
the tests assert it.

### 6.2 `setup_segments`, `src/sound/segsetup.c:88-199`

The span between waypoints `prev` and `w`:

```c
dist = Vec3d_Distance(from, to);                         // :97
Hermite_Init(h[0], from.x, to.x, rotFrom[0]*dist, rotTo[0]*dist);   // :98-100
Hermite_Init(h[1], from.y, to.y, rotFrom[3]*dist, rotTo[3]*dist);
Hermite_Init(h[2], from.z, to.z, rotFrom[6]*dist, rotTo[6]*dist);
Matrix_GetEuler(rotFrom, &ax0, &ay0, &az0);              // :101-102
Matrix_GetEuler(rotTo,   &ax1, &ay1, &az1);
mid = (from + to) * 0.5;                                 // :103-105
inv = 1 / (long double)segments;                         // :107
```

The tangent for each axis is the frame's first **column** — `rot[0]`, `rot[3]`,
`rot[6]` — scaled by the chord length, so a straight span gets tangents equal to
the chord and the Hermite reduces to a line. `Vec3d_Distance` sums `dz*dz + dy*dy
+ dx*dx` in that order (`src/math/vec3.c:86-94`, `HOOK(0x4B32B0)`).

Then for `i = 0 .. segments`:

- `t = i * inv` (long double), `tf = t` (float) (`:109-110`).
- **rigid**: `pos = lerp(from, to, t) - mid`, `memset(rot, 0, 40)`,
  `Vec3_ToAngles(d, &pitch, &yaw)` unless `d` is all zero, `Matrix_SetRotZ(rot,
  yaw)`; at `i == 0` only, `segLength[1] = |d| * inv` and `length += |d|*inv`
  (`:112-140`). Since `SplineObjSegment_Setup` forces `segments = 1` for a rigid
  span, that is the whole length.
- **curved**: the roll unwrap, then the position, then the frame (§6.3), then
  `segLength[i] = |pos[i] - pos[i-1]|` and `length = segLength[i] + length`
  (`:141-197`).

`segLength` needs care. It is `float[22]` at 0x544, and the oracle's own field
comment records that **entry 0 is `invRot`'s spare word**
(`src/sound/segsetup.c:47`) — `segRot[i]` is a ten-word block, so the tenth word of
`segRot[19]` runs into `invRot[0]`, and `invRot` itself is nine words. The writers
all target entry `i + 1`: `o->segLength[1]` for the rigid span (`:140`) and
`o->segLength[i]` for `i >= 1` in the curved loop (`:173`, `:194`). The reader
`SEG_LEN(s, i)` is `0x548 + i*4`, i.e. entry `i + 1`, and its macro comment says
"the original also reads index -1 (the word before segLength)"
(`src/tagitem/spline.c:49-52`). So the sub-segment from joint `i` to joint `i+1`
lives in `seg_length[i + 1]`, and `SplineObj_GetPoints` relies on that off-by-one.
The port exposes `seg_length` with the same indexing and the same "entry 0 is
never written" property, and `SplineObjSegment_Setup` zeroes entry
`segments + 1` (`:272`).

### 6.3 The roll is interpolated, not zero

The Euler-X of the two endpoint frames is unwrapped and then lerped
(`src/sound/segsetup.c:144-159`, `:188`):

```c
if (ax1 - a0 > kPi)            { do a0 += kTwoPi; while (ax1 - a0 >  kPi);  a0f = a0; }
if (!(ax1 - a0 >= kMinusPi))   { do a1 += kTwoPi; while (!(a1 - a0 >= kMinusPi)); a1f = a1; }
...
ax = (a1f - a0f) * tf + a0f;
Matrix_SetEuler(rot, ax, pitch, yaw);
```

`kPi`/`kTwoPi`/`kMinusPi` are the retail floats `0x1.921fb6p+1f` and
`0x1.921fb6p+2f` (`:63-65`). The `a0` loop is skipped when the two frames are
already within π, and the `a1` loop takes the short way round, so a span whose
endpoints are authored as (say) `6.0` and `0.5` rolls through `+2π` rather than
backwards through `-5.5`. This is the single most load-bearing difference from the
zero-roll frame the editor used before #59 (§9.1).

The two endpoint frames are copied verbatim — `memcpy(rot, rotFrom, 40)` at
`i == 0` (`:164-165`) and `memcpy(rot, rotTo, 40)` at `i == segments` (`:166-171`)
— and only the interior joints get the interpolated frame, with `pitch` and `yaw`
from `Vec3_ToAngles` of the Hermite slope at that `t` (`:176-187`), or `(0, 0)`
when the slope is exactly zero (`:182-184`).

### 6.4 `Vec3_ToAngles`, `src/file/vec.c:232-245` (`HOOK(0x4B2D00)`)

```c
if (IsZero(v[0])) *yaw = 0.0f;                       // NOT atan2(0, ...)!
else              *yaw = fpatan(v[1], v[0]);
len = fsqrt(v[0]² + v[1]² + v[2]²);
if (IsZero(len)) *pitch = 0.0f;
else             *pitch = fchs(Math_ASinF(v[2] / len));   // the sign is NEGATED
```

The `v[0] == 0` guard means a pure `+Y` direction yaws to **0**, not π/2 — a
quirk that changes the roll of every span running along ±Y. `IsZero` is
`!islessgreater(x, 0.0f)` (`:57-60`), so it is true for `±0` and false for a NaN.
`Math_ASinF` (`HOOK(0x4B27D0)`, `:48-58`) clamps into `[-1, 1]` and returns
`crt_asin(-1)` for a value below -1 *or a NaN* (`if (!(d >= -1.0))`).

### 6.5 The composed transform

`Setup` receives the two authored Euler triples, converts them with
`Matrix_SetEuler` = `Rz(az)·Ry(ay)·Rx(ax)` (`HOOK(0x4B38E0)`,
`src/math/matrix.c:99-110`), and the "orientation" the editor authors is therefore
a *yaw-pitch-roll* triple in that order. `Matrix_GetEuler`
(`HOOK(0x4B3C50)`, `:181-208`) is its inverse for `cy = sqrt(m0² + m3²) > 0.0001`
and normalises the roll into `[0, 2π)`; below that threshold it falls back to
`Atan2(-m[5], m[4])` with `rz = 0`, i.e. the gimbal-locked case, which the port
reproduces including the threshold.

`Atan2` (`src/math/matrix.c:27-41`) returns `sign(y)·HALF_PI` for `x == 0` and, for
`x == 0 && y == 0`, calls `Error_Show` and then spins forever (`for (;;) ;`). The
port returns `+HALF_PI` there instead of hanging (§10).

### 6.6 Reproduced degenerate cases

| Input | What the engine does | Port |
|---|---|---|
| `from == to` (zero-length span) | `dist = 0` → all tangents 0, `mid = from`; the curve is the single point; every joint coincident; `segLength` 0 | same |
| a duplicated control point inside a span | a zero-length sub-segment and a zero slope → `Vec3_ToAngles` returns `(0,0)` | same |
| `segments == 0` (`NumAreas` 0 survives §2.3) | `inv = 1/0 = inf` → `t = 0 * inf = NaN` → the only joint is NaN; `SplineMesh_GetVerts` then refuses (fewer than two joints) | same, and the mesh path returns 0 |
| `end == start` (a model with no X extent) | `inv` infinite, `t` infinite/NaN, `k = trunc(NaN)` = 0, `f` clamped to 0 or 1 → every vertex lands on joint 0 or 1 | same; `SplineTruncateToInt` returns 0 for NaN/±inf, which is what `fistpll`'s integer-indefinite result gives |
| `k == segments` | forced to `segments - 1` so `f` can be 1 | same |
| rigid span with `NumAreas` 20 | `segments` forced to 1 | same |

The `x87_fistp` (`fistpll`) behaviour is why the NaN case is well defined:
`FPU_SetTruncate` (`HOOK(0x498460)`, `src/dxinit/dxinit.c:75-81`) sets the x87
rounding mode to round-toward-zero at startup (`src/win/appmain.c:212,223,274`;
`src/d3d/direct3d.c:227,821`), so the conversion truncates, and an invalid
conversion stores `0x8000000000000000` whose low 32 bits are 0
(`src/dxinit/dxinit.h:22-28`).

---

## 7. Two neighbours in the same translation unit

### 7.1 The cache is vestigial

`SplineCache_Remove` (`HOOK(0x49A010)`, `src/dxinit/spline.c:22-34`) loops
`for (i = 0; i < 1; i++)`, and `SplineCache_Reset` (`HOOK(0x49A340)`, `:107-111`)
clears slot 0 only, although the arrays are declared `void *gSplineCache[2]` and
`int gSplineCacheData[4]` (`:114-117`). **Nothing in the decompilation ever writes
a non-null slot**, so in this build the cache never holds an entry and every
`Remove` is a no-op. What the code does establish is the *invalidation policy*: a
segment is removed after every `SplineObjSegment_Setup`
(`src/sound/segsetup.c:327`) and the whole cache is reset by `Mesh3D_Init`
(`src/lod/mesh3d.c:468`). The port keeps the slot count, the loop bound and the
policy, and says in the header that the slots are never populated.

### 7.2 `Float_Log2` is not a spline rule

`HOOK(0x499FF0)`, `src/dxinit/spline.c:13-20`:

```c
unsigned Float_Log2(int bits) {
    if (bits > 0x3F800000) return (unsigned)(bits - 0x3F800000) >> 20;
    return 0;
}
```

It is a fog-depth helper ("8·log2 x, roughly") that happens to share the file;
callers are `src/sector/light.c:67`, `src/glowmesh/terrainmesh.c:282`,
`src/flatwater/flatwater.c:421` and `src/rotor/afterburner.c:99-102`. The oracle
declares the parameter as `int` and the callers declare the function as taking a
`float`; the port follows the callers (a `float` reinterpreted as raw IEEE-754
bits) because the 32-bit cdecl argument slot is what the original passes either
way. That ABI reasoning is **unverified** — see §11. Three consequences, all
reproduced: `1.0f` returns 0 (the comparison is strictly `>`), anything in
`[0,1)` returns 0, and because the comparison is **signed** on the raw bits every
negative float returns 0 while a *positive* NaN returns 1028 and a negative NaN
returns 0.

---

## 8. The retail corpus

Read from `MISSIONS/location0/level1/OBJECTS.QVM` with the editor's own
`QVM_Parse`, and cross-checked against the decompiled `level1/objects.qsc`.

### 8.1 The declaration

`Task_DeclareParameters("SplineObj", …)` and `(… "SplineObjWaypoint" …)` appear
once each, and the port's two `SplineParamDesc` tables match them name for name
and type for type, in order (10 rows / 16 tokens and 8 rows / 12 tokens). The
`DeclaredParametersMatchTheTables` test decodes the declaration from the
instruction stream: `PUSHIIB Task_DeclareParameters`, `CALL`, `BRA <skip>`, then
the interleaved name/type string pairs and a final `POP`. The `BRA` skips the
block, so a naive "next string" reader gets the wrong thing.

### 8.2 The inventory

Six `SplineObj` containers, 25 `SplineObjWaypoint` children, all inside
`Container "Tunnel"` / `Container "Spline objects"`:

| Container | id | waypoints | `rigid` | marker | segment models |
|---|---|---|---|---|---|
| rail | 1500 | **12** | FALSE | `waypoint` | `368_01_1` ×8, `323_01_1` ×3, `322_01_1` ×1 |
| bridge | −1 | 3 | TRUE | `""` | `320_02_1`, **`""`**, `320_02_1` |
| bridge | −1 | 4 | TRUE | `""` | `320_02_1` ×4 |
| bridge | −1 | 2 | TRUE | `""` | `320_02_1` ×2 |
| bridge | −1 | 2 | TRUE | `""` | `320_02_1` ×2 |
| bridge | −1 | 2 | TRUE | `""` | `320_02_1` ×2 |

All six author `maxSegments = 20`, `angle = 3.1415929794311523`,
`color1 = (1,1,1)`, `color2 = (0,0,0)`, `collide = FALSE`, `snap = FALSE`,
`autoOrient = FALSE`. The rail's waypoints carry the marker model name
`"waypoint"`; the bridges' carry no marker model at all. `LEVEL2` and `LEVEL4`
author no splines, and of the levels that ship an `objects.qsc` only LEVEL1
contains any: the other levels contain just the two declaration strings.

**One bridge span has no segment model.** The first bridge container's second
waypoint (`24446002, −56045108, 174433600`, orientation `(0,0,4.4707231521606445)`)
authors `Segment Model = ""`. `SplineObjSegment_Setup` calls `VirModel_Find(model)`
after `setup_segments` and returns when it finds nothing
(`src/sound/segsetup.c:272-275`), so that span keeps its correctly built Hermite
joints, contributes its length to `obj->length`, and draws nothing (there is no
mesh to bend, and `start`/`end` are never scanned). Whether `VirModel_Find("")`
returns NULL in the retail build is **unverified**; the editor's model cache would
have to answer it. Either way there is no bridge mesh there — the level ships a
gap in that bridge, which is data, not a rule.

The five bridge containers are also the corpus case for `rigid = TRUE`: each
authors `NumAreas 20`, and §2 step 3 plus `SplineObjSegment_Setup` force one
sub-segment, so a 3-waypoint bridge is two straight chords, not 40 Hermite
steps.

Orientation is `(0,0,0)` on 21 of the 25 waypoints. The exceptions are the rail's
waypoint 6 (`(6.2831811904907227, 6.0213789939880371, 3.1415920257568359)`, §1.3)
and the 3-waypoint bridge's three waypoints, which author
`(0, 0, 4.4707231521606445)`, `(0, 0, 4.4707231521606445)` and
`(0, 0, 4.4707241058349609)` — the last differing only in the float's last digit,
i.e. one authored yaw written twice. That bridge is therefore rotated relative to
the other four, which author a zero yaw.

The rail's 12 authored waypoints, in file order (orientation, position, segment
model, NumAreas):

| # | Orientation | Position | Segment Model | NumAreas |
|---|---|---|---|---|
| 1 | (0, 0, 3.1415929794311523) | (25287152, −55948040, 174410960) | `368_01_1` | 20 |
| 2 | (0, 0, 3.1415929794311523) | (25124074, −55958612, 174414016) | `322_01_1` | 10 |
| 3 | (0, 0, 3.1415929794311523) | (24857834, −55958668, 174412928) | `323_01_1` | 10 |
| 4 | (0, 0, 2.8735930919647217) | (24658190, −55807144, 174413168) | `323_01_1` | 10 |
| 5 | (0, 0, 3.1415929794311523) | (24365666, −55777100, 174413120) | `323_01_1` | 10 |
| 6 | (6.2831811904907227, 6.0213789939880371, 3.1415920257568359) | (24111474, −55776588, 174492288) | `368_01_1` | 10 |
| 7 | (0, 0, 3.1415929794311523) | (23580308, −55810864, 174507184) | `368_01_1` | 20 |
| 8 | (0, 0, 3.7815918922424316) | (23094192, −56028160, 174504480) | `368_01_1` | 20 |
| 9 | (0, 0, 4.2815909385681152) | (22755560, −56439088, 174504624) | `368_01_1` | 10 |
| 10 | (0, 0, 4.2815909385681152) | (22533198, −56922920, 174504640) | `368_01_1` | 10 |
| 11 | (0, 0, 4.2815909385681152) | (22310838, −57406752, 174504640) | `368_01_1` | 10 |
| 12 | (0, 0, 4.2815909385681152) | (22088476, −57890580, 174504640) | `368_01_1` | 10 |

The container line is `Task_New(1500, "SplineObj", "", FALSE, FALSE, FALSE,
FALSE, 20, 0, 0, 0, 0, 3.1415929794311523, 1, 1, 1, 0, 0, 0, …)` and the first
child is `Task_New(-1, "SplineObjWaypoint", "", 0, 0, 3.1415929794311523,
25287152.0, -55948040.0, 174410960.0, "waypoint", "368_01_1", 20, FALSE, FALSE,
FALSE)`.

### 8.3 The rail length — the 11-waypoint figure was the first ten spans

Twelve waypoints make **eleven** spans, and `SplineObjWaypoint_Changed` sums the
segments of every waypoint *after the first* (`src/tagitem/spline.c:833-839`):

```
obj->length = 0;
for (p = Waypoint_Next(first); p; p = Waypoint_Next(p))
    if (p->seg) obj->length = p->seg->length + obj->length;
```

`obj->length` is a **float** (`src/sound/soundtypes.h:54`) and `seg->length` is a
float too (`:54`), so this is a float accumulation in span order — not a double
sum. Running this port's `BuildSplineSpanJoints` over the eleven spans and
accumulating the way the engine does gives:

| Quantity | Value |
|---|---|
| chord sum, all 11 spans | 4 435 504.76 |
| chord sum, first 10 spans | 3 903 025.32 |
| `length`, all 11 spans (float accumulation) | **4 461 267.50** (1089.1 m) |
| the same, summed in `double` | 4 461 267.6875 |
| `length`, first 10 spans | 3 928 788.25 |
| last (11th) span | 532 479.4375 |

The earlier published figure, **3 928 793.21** for an "11-waypoint / 10-span"
rail, is the partial sum over the first **ten** spans, reproduced here to
−4.96 units in 3.9×10⁶ (1.3×10⁻⁶, i.e. last-bit noise): the twelfth waypoint and
therefore the eleventh span were missing from it. Per-span lengths, spans 1…11:
163 519.33, 266 242.63, 257 059.48, 294 956.69, 267 697.50, 534 740.81,
541 730.88, 537 875.63, 532 483.06, 532 482.25, 532 479.44.

That propagates to `Train` 1499, which authors `start_dist = -4045627.5` on rail
1500 (`Task_New(1499, "Train", "", -4045627.5, 0.20000000298023224, 1500,
"608_01_1", …)`). `SplineObj_GetPoints` (`src/tagitem/spline.c:246-256`) wraps a
negative distance by repeated addition:

```
d = dist;
if (!(d >= 0.0f)) { do d += obj->length; while (!(d >= 0.0f)); dist = d; }
if (d >= obj->length) { do d -= obj->length; while (d >= obj->length); dist = d; }
```

With the real length of 4 461 267.5 that is **one** addition and the arc lands at
**+415 640.0** exactly — inside the second span, whose cumulative end is
429 761.94. With the ten-span length it would need two additions and land at
3 811 958.92, near the far end of the rail — 3 396 319 units (829 m) away along
it. The test `Train1499WrapsItsRailDistanceInOneAdditionNotTwo` pins the
one-addition result to the exact float, so a revert fails loudly. The other 22 `Train` tasks are
unaffected: their authored distances are `0` (no wrap) or `-575416.9375` (two
tasks), `-971400.125` and `-3744122.25`, and all five are negative but larger than
`-length`, so each needs exactly one addition either way.

**The owning document has since been corrected independently.** While this module
was being written, `docs/igi-re-parity/PLACEMENT_SPLINES.md` §3.2 was rewritten by
the #59 owner for the 12-waypoint / 11-span / one-wrap structure. It was not
edited here (as instructed), and one residue is worth recording: that revision
quotes **4 461 272.69** units and a wrapped arc of **415 645.19**, which is the old
published partial (3 928 793.21) plus the corrected eleventh span rather than a
fresh sum. The engine-faithful values are 4 461 267.5 and 415 640.0 — a 5.0-unit
difference in 4.46 million (1.1×10⁻⁶), invisible to every placement decision, but
the numbers above are the ones this port produces and the ones the tests assert.

---

## 9. Findings that correct inherited assumptions

### 9.1 The joints are not a zero-roll frame (#69)

The editor's pre-#59 code built a per-vertex `BuildZeroRollBasis` frame — tangent
along X, level side in XY, up completing the basis — an assumption inherited from
open-igi's reading of `sub_4E4530` (`source/renderer/spline_geometry.cpp:14-22`).
The engine does not do that. It converts both endpoint frames to Euler angles and
**lerps the roll** between them with the float-π unwrap loop of §6.3, then rebuilds
the frame with `Matrix_SetEuler` (`src/sound/segsetup.c:144-159`, `:188`), so a
pitched or rolled joint keeps a real intermediate orientation.

`docs/igi-re-parity/PLACEMENT_SPLINES.md:389-394` already records this as the
"#69 fidelity gap"; what is new here is that the rule is now *exercised* rather
than described. The test `RollIsTheInterpolatedEulerXWithTheFloatPiUnwrap` pins
concrete numbers: endpoints authored as Euler-X `6.0` and `0.5` unwrap to
`6.7831855`, the middle joint's roll is `6.3915928` (i.e. `0.1084075` after
`Matrix_GetEuler`), and the middle frame's `rot[4]` is `0.9941278` — neither
endpoint's value (`0.9601703`, `0.8775826`) and not 1. Skipping the unwrap would
give `cos(3.25) = −0.9941`, so the test distinguishes the two rules.

A second consequence: `Vec3_ToAngles` returns yaw **0** when the tangent has no X
component (§6.4), so a span running along ±Y has a different roll than a
geometric reading would predict. That quirk is in `src/file/vec.c:236-239`.

### 9.2 The mesh is stepped in the curve parameter, not in arc length (#69)

There are two different parameterisations in the engine, and they must not be
confused:

- `SplineObj_GetPoints` walks the **joint polyline by arc length** — for each
  requested point it intersects the polyline with a circle of the requested radius
  (`src/tagitem/spline.c:219-454`). That is the train/rail sampler, ported from
  #59 as `SampleRailPoint` (`PLACEMENT_SPLINES.md:422`).
- `SplineMesh_GetVerts` walks **no distances at all**. It maps each model vertex's
  local X to `t = (base + x) * scale` and blends two adjacent joint frames (§5).
  So the surface's cross-sections are spaced equally in the Hermite parameter, not
  in arc length. On the retail rail that is measurable, and the spread tracks the
  curvature exactly:

  | Span | Sub-segments | Shortest … longest | Spread |
  |---|---|---|---|
  | 2 (nearly straight) | 10 | 26 624.18 … 26 624.38 | 0.001 % |
  | 7 | 20 | 26 461.16 … 27 634.27 | 4.4 % |
  | 3 (the sharp bend at yaw 2.8736) | 10 | 23 611.18 … 27 581.97 | **16.8 %** |
  | 9, 10, 11 (collinear) | 10 each | 53 248.31 ± 0.01 | 0.000 % |

  A "sample the curve at equal arc length" rule would move every cross-section on
  a curved span — by up to 16.8 % of a sub-segment here — so the port does not
  implement one.

### 9.3 Authority

Nothing here is taken from another port. Where an open-igi-derived assumption
survives in this repository (the zero-roll frame above), the decompilation wins,
the port follows the decompilation, and §9.1-9.2 record the difference with the
tests that pin it.

---

## 10. Deliberate divergences

Each of these is a place where the port is *not* a literal transcription,
because the engine's behaviour there is undefined, unbounded or non-terminating.
None of them changes a defined result.

| # | Engine | Port | Why |
|---|---|---|---|
| 1 | `segRot[k]` / `segRot[k+1]` are indexed with no bounds check, so a vertex outside the model's own X range reads adjacent engine memory | `k` is clamped into `[0, segments - 1]` | keeps the output defined; a model vertex outside `[start, end]` is the only way to reach it |
| 2 | `Atan2(0, 0)` calls `Error_Show` and then `for (;;) ;` | returns `+HALF_PI` | the engine hangs |
| 3 | `gSplineVerts` is an unchecked 60000-float static | returns 0 when the part does not fit | the caller supplies the capacity |
| 4 | `setup_rigid` has no guard on `maxX == minX` (§4.3) | reproduces the division by zero, but `SplineTruncateToInt` gives the `fistpll` integer-indefinite result (0) so the bend stays defined | faithful *and* defined |
| 5 | `SplineObj_Changed` stores a `Matrix` and accumulates `count` | the port stores the `Matrix_GetEuler` triple (normalised to `[0, 2π)`) because the editor's authored form is the Euler triple; the frame is equivalent because `SetEuler` only uses `sin`/`cos` | the editor's data model has no matrix |
| 6 | `Waypoint_Straighten`/`SplineObj_Changed` also rebuild segments, move collision cubes and re-run the attachment update | the port returns the geometry (positions, frames) and documents what it leaves to the caller | those need the world, the model cache and the task tree |
| 7 | `Matrix_GetEuler`'s `[0, 2π)` normalisation can turn `-0.0` into `2π` | same | no observable difference through `sin`/`cos` |
| 8 | `invRot` is written and never read (§4.3) | not produced | dead |

The port also does not model the `*`-prefixed model-name fallback (§2 step 7) as
state, because it is editor UI bookkeeping rather than geometry; `spline_rules.h`
documents it next to the model-name fields.

---

## 11. Verified, and not

**Verified by execution** on the host lane
(`IGI_GAME_PATH=/Users/haseeb-mir/Downloads/IGI`, retail install present):

```
$ cd /Users/haseeb-mir/Documents/Code/project-igi-editor-igi-re
$ IGI_HOST_NO_CACHE=1 IGI_HOST_OUT_DIR=build-mingw/ht-spline \
  IGI_HOST_LOG_DIR=build-mingw/log-spline \
  IGI_HOST_TESTS=tests/test_spline_rules.cpp bash scripts/run-host-tests.sh
   PASS  test_spline_rules                    57 passed    0 failed  (57 tests, exit 0)
   tests run             : 57
   tests passed          : 57
   tests failed          : 0
   tests skipped         : 0 (in 0 suite(s))
OK: 57 tests passed in 1 suites
[exit code: 0]

$ clang++ -std=c++20 -Wall -Wextra -Wpedantic -fsyntax-only -I source \
    -I third_party/glm-1.0.1 -I third_party/GL/include source/runtime/spline_rules.cpp
[exit code: 0]
```

57 tests in 13 suites: 49 unit tests and 8 corpus tests, 0 skipped. The unit
tests assert values derived independently of the port (a separate double-precision
`Rz·Ry·Rx` product for `SetEuler`, hand-computed Hermite values, the three-factor
radius product); the corpus tests read the retail `OBJECTS.QVM` and `objects.qsc`
and ran rather than skipping.

- **Proved:** the parse of both authored tables against the retail declaration
  (name and type, in order); the parse of all 31 retail instances (6 containers,
  25 waypoints); the container defaults; the two clamps of §2; the Hermite
  identity at both ends and at `u = 0.5`; `SetEuler` against an independent
  matrix product and its `GetEuler` round-trip; `SetRotZ`'s exact element layout;
  `Vec3_ToAngles`' zero guard, negated pitch and both singularities; the rigid
  frame's `chord/(maxX-minX)` first column, the Flip negation, the `from == to`
  identity and the `maxX == minX` division; the rigid frame's centring
  (`∓chord/2`); joints and `segLength` for rigid and curved spans including the
  `segments + 1` zeroing and the entry-0 aliasing; the roll unwrap and the
  interpolated roll; the bend mapping `x = start` → joint 0 and `x = end` →
  joint `segments`, its source-order/stride contract, the rigid path, the
  capacity refusal and the sub-two-joint refusal; `fistpll`'s NaN/±inf/out-of-range
  → 0; the extent scan's zero-inclusive range, its stride and its NaN asymmetry;
  both radius rules; the cache's single live slot; `Float_Log2`'s exact values
  including the positive/negative NaN split; the three new-waypoint steps; snap;
  auto-orient; the Align re-spacing and its single-member early return; the
  container translate and rotate; and on the corpus, the six-container inventory,
  the rail's twelve waypoints and eleven spans, the float-accumulated 11-span
  length of 4 461 267.5 and its first-ten partial 3 928 788.25, `Train` 1499's
  one-addition wrap to exactly 415 640.0, and that the spans' sub-segment lengths
  follow the curvature rather than the arc length (§9.2).
- **Not verified — no rendered result.** The GUI needs OpenGL 4.1 and this host
  provides 2.1. Nothing here was seen on screen; no renderer, level or common
  code was modified and the port is not wired into a draw path. Whether the ported
  rules reproduce the retail *appearance* of a road or a bridge is unverified.
- **Not verified — bit-exactness.** The oracle's translation units are compiled
  with `#pragma GCC optimize("excess-precision=standard")`, so its `long double`
  intermediates are 80-bit x87; the host is x86-64 SSE2 (`long double` = 80-bit
  storage but SSE for doubles). Results are exact to the declared C types but not
  guaranteed bit-identical to the retail build, and the transcendental functions
  (`sinl`/`cosl`/`sqrtl` versus the engine's `fsin`/`fcos`/`fsqrt`) may differ in
  the last bits. The Hermite and bend arithmetic is otherwise a straight
  transcription of `double`/`long double`/`float` expressions.
- **Not verified — the retail render vs collision vertex agreement** (§4.2): the
  extent scan reads the render array and the bend reads the collision array, and
  the editor's MEF reader was not exercised, so their X agreement on the retail
  road models is assumed, not shown.
- **Not verified — `Float_Log2`'s ABI** (§7.2): the `int`/`float` parameter
  mismatch is resolved by the 32-bit cdecl argument slot, which is reasoning, not
  a trace.
- **Not checked in this task:** the Windows `igi_tests` binary (which is the gate;
  the host lane does not build `tests/test_runtime_subsystems.cpp` or the MCP tool
  tests), and any in-game behaviour of the placed road, since the port is not
  wired into a draw path.

---

## 12. Relationship to the #59 work, and files

`source/renderer/spline_geometry.{h,cpp}` (from #59) already ports the
renderer-side half: `BuildSplineSpan` (the joint build, `spline_geometry.cpp:500-596`), and the
collision-mesh bend via `SplineObj_BendVerts` (`src/shadow/spline.c:24-90`). This
module deliberately does **not** duplicate them. It complements them with what a
spline *container* needs and what the renderer never touches:

- the two authored parameter tables, their parse, and the container/waypoint
  defaults (#79);
- the authoring rules — the three new-waypoint steps, snap, auto-orient, Align,
  and the container translate/rotate delta (#79);
- the Hermite primitives as named, individually citable rules, and the
  model-extent → `start`/`end` → span mapping, including the rigid frame and both
  radius rules (#79/#69);
- `SplineMesh_GetVerts`'s *contract* — the count, the source ordering, the static
  60000-float buffer, the model+part indexing and the return-base-pointer
  convention (#79/#69);
- `SplineCache_*` semantics and `Float_Log2` (§7).

The port is plain C++20 with no GLM, no engine headers and no renderer
dependency, so it can be unit-tested on the host and reused by the editor's
placement UI. Both files carry the citation table in their header comment.

| Path | What |
|---|---|
| `source/runtime/spline_rules.h`, `.cpp` | the port (13 rule groups, all cited) |
| `tests/test_spline_rules.cpp` | 49 unit + 8 corpus tests (57 total) |
| `CMakeLists.txt` | purely additive: one test file in `add_executable(igi_tests …)` and one runtime source in `target_sources(igi_tests PRIVATE …)` |
| `docs/igi-re-parity/PLACEMENT_SPLINES.md` | **not modified**; §3.2/`:514`/`:622` need the §8.3 correction |
| `docs/igi-re-parity/TRIAGE_REMAINING_ISSUES.md` | the #79 and #69 rows this closes |
