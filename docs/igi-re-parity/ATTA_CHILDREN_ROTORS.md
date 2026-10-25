# ATTA child nodes, parenting and rotors (issue #40)

Parity record for `#40 compound objects: ATTA child nodes, rotor attachment,
parent/child transforms`. Every rule and offset below is cited to IGI-RE, the
verified decompilation of the retail `IGI.exe`; every editor claim is cited to a
file and line in this repository.

Oracle: `/Users/haseeb-mir/Documents/Code/IGI-RE` (read-only).
Editor: this repository, branch `fix/igi-re-known-issues`.

> Status legend — **verified** = confirmed against IGI-RE source and/or the
> shipped game data with a command whose output is quoted; **unverified** =
> cannot be confirmed in this environment (no runnable retail target and the
> installed `IGI.exe` is not the IGI-RE-supported build, so no differential
> harness exists).

---

## 1. The `ATTA` chunk and its record layout

| Fact | Evidence |
| --- | --- |
| Chunk tag `MEF_ATTA` = `0x41545441` | IGI-RE `src/model/model.h:27` |
| The mesh header keeps the chunk pointer at `+0x8C` | IGI-RE `src/model/model.c:374` |
| Header field `char *attach; /* ATTA, 0x44 bytes each */` | IGI-RE `src/model/modeltypes.h:26` |
| Attachment count is `int16_t attach_count` | IGI-RE `src/model/modeltypes.h:23` |
| Record stride is exactly `0x44` | IGI-RE `src/lod/mesh3d.c:493-497` (`ModelLod_GetAttach` returns `lod_header(l)->attach + i * 0x44`) |
| Count accessor | IGI-RE `src/lod/mesh3d.c:499-503` (`ModelLod_GetAttachCount` returns `attach_count`) |

**Record layout — 0x44 (68) bytes**, from the decompiler's `MagicAttach`
(IGI-RE `src/magicobj/rigidcoll.c:362-367`), mirrored by `ModelAttach`
(IGI-RE `src/shadow/sector.c:517-521`):

```
offset  size  field
0x00      16  char name[16]   // DefineMagicObj name of the child
0x10      12  float pos[3]    // child position in the PARENT's local frame
0x1C      36  float m[9]      // child basis, stored row-major
0x40       4  int unk40       // never read by the retail code
```

**Verified:** the editor parses exactly these offsets and this stride —
`source/renderer/mef_native.h` (`MefAttachment`), `ParseMefAttachments` in
`source/renderer/mef_native.cpp`, and the byte-level editor in
`source/renderer/renderer_objects_atta.cpp` (`DisableAttaInMefBytes`,
`UpdateAttaLocalPosInMef`, stride 68, `pos` at 16/20/24, matrix at 28).
Independent byte dump of the shipped level-1 `709_01_1.mef` at the documented
offsets:

```
ATTA at 192, size 408, records 6
rec0 name=711_01_1 pos=(3.46e-07, -0.106875, 8894.792)  tail=0
rec1 name=712_01_1 pos=(-503.398, -34107.758, 7310.172) tail=0
```

---

## 2. How the game parents an ATTA child

The rule lives in `RigidDynCubeObj_CreateMagicObjs` (IGI-RE
`src/magicobj/rigidcoll.c:383-495`, `HOOK(0x4C4950, ...)`) and is duplicated for
sector-interior objects in `Sector_CreateMagicObjs` (IGI-RE
`src/shadow/sector.c:538-687`):

1. **Look the child up by name.** For every ATTA record the game compares
   `att->name` against the `DefineMagicObj` table and creates a task of that
   definition's type with that definition's model — IGI-RE
   `src/magicobj/rigidcoll.c:472-487`; the same loop at
   `src/shadow/sector.c:657`. `DefineMagicObj(name, model, type)` is IGI-RE
   `src/magicobj/magicobj.c:188-220` (`HOOK(0x4C4520, ...)`). **The parent
   object's type never participates in this decision.**
2. **Child world position** = `M * att->pos + o->pos`, with `M` the parent's
   row-major 3x3 (`o->matrix.m`) — IGI-RE `src/magicobj/rigidcoll.c:405-410`.
3. **Child local frame `R`** is built with the *orthonormalized columns* of the
   stored matrix `a` as its *rows*: `W = normalize(a[0],a[3],a[6])` (column 0),
   `E = normalize(Gram-Schmidt component of column 1 orthogonal to W)`,
   `F = W x E` — IGI-RE `src/magicobj/rigidcoll.c:412-436`.
4. **Child world frame** `P = M * R` (row-major) — IGI-RE
   `src/magicobj/rigidcoll.c:442-451` — followed by a second orthonormalization
   pass (`:452-469`) which is a no-op for the orthonormal bases the retail data
   actually contains (see §5).

For an orthonormal basis `A`, step 3 is exactly `R = transpose(A)`, and the
editor's `source/renderer/atta_children.h` `IgiAttaLocalRotation` /
`IgiAttaChildWorld` implement steps 2–4 directly.

**Verified:** `tests/test_atta_children.cpp`
`AttaChildren.ChildWorldMatchesIgiReFormulaForSyntheticRecords` compares the
editor helper against a literal transcription of IGI-RE
`src/magicobj/rigidcoll.c:405-451` for identity and non-identity parents;
`AttaChildren.RetailHeliRotorRecordsAreOrthonormalAndMatchIgiRe` does the same
for the two real rotor records below.

---

## 3. Rotors: what a "rotor" actually is

A rotor is **not** a bone or a special mesh node. It is an ordinary magic object
whose task type is `TASKTYPE_ROTOR` (IGI-RE `src/rotor/rotor.c:191-211`,
`Rotor_Init`, `HOOK(0x42D2B0, ...)`).

**Classification (verified against the shipped file).** `DefineMagicObj` is
called from the scripts in `MAGICOBJ/`; the shipped `MAGICOBJ/MAGICOBJ.QVM`
defines exactly six names as `TASKTYPE_ROTOR`:

```
700_02_1  700_03_1  700_04_1  711_01_1  711_02_1  712_01_1
```

(Decompiled with this repository's own `source/level/qvm_decompiler.cpp`; in the
retail file `attachment_name == model_id` for every one of the 387 strings.)

**Spin (verified).** `Rotor_DrawPrim` (IGI-RE `src/rotor/rotor.c:110-166`,
`HOOK(0x42C630, ...)`) draws each blade copy from a *local copy* of the part's
matrix:

```
copy = src->obj;  base = copy.matrix;                     // :112-114
a = src->value * kSpin;              // kSpin = 21.362831  // :145, :78
for (i = 0; i < d->dir; i++) {                            // :146
    Matrix_SetRotZ(&rz, tilt);                            // :149
    Matrix_SetRotX(&copy.matrix, a);                      // :150
    copy.matrix = base * copy.matrix;                     // :151-152
    copy.matrix = copy.matrix * rz;                       // :153-154
    draw(copy);                                           // :162
    a = a + step;                    // step = speed * car speed  // :131, :163
}
```

Consequences that matter for the editor:

* the spin is a **post-multiplied** rotation about the part's **own local X**
  axis (`base * RotX(a)`), not a pre-multiplied world-axis rotation;
* `a` **increases** (`a += step`, `step >= 0`), so the rotation is positive
  about local X for every rotor;
* the spin applies to the **drawing copy only** — `Rotor_Draw` hands children
  the *unspun* matrix (`f.matrix = o->obj.matrix`, IGI-RE `src/rotor/rotor.c:179`),
  and `Rotor_Move` composes children from the unspun `a->matrix`
  (IGI-RE `src/rotor/rotor.c:280`);
* `dir` and `speed` come from the rotor's own `DefinePhysicsObjType` entry
  (`Rotor_ParseDef`, IGI-RE `src/rotor/rotor.c:302-316`, reading
  `lift, tilt, dir, angle, speed, name` from index `arg + 2` of the
  `DefinePhysicsObjType` argument list, IGI-RE `src/physobj/physobjtype.c:64-122`).
  For the shipped Bell helicopter (`physicsobj/HELIS/BELL/HELI.QVM`):
  `711_01_1` → `dir=1, angle=0, speed=0.1`; `711_02_1` → `dir=17, angle=5 deg,
  speed=0.07`; `712_01_1` → `dir=10, angle=0, speed=0.12`.

**The editor does not implement the `dir` blade loop** (it draws one copy per
attachment) and does not read `HELI.QVM`, so the blade blur and the exact rate
are preview-only. See §7.

---

## 4. What the editor did before this change, and what it does now

Pre-change code (HEAD `80320697`), all in
`source/renderer/renderer_objects_atta.cpp`:

| Behaviour | Pre-change | Problem |
| --- | --- | --- |
| Child transform | inline `attLocalRot` / `worldPos` / `childWorldMat`, lines 1167-1188 | correct maths, but untestable (GL/Windows TU) and undocumented |
| Rotor classification | `current_draw_obj_type_ == "Heli"` plus a geometry guess: main rotor = child with max local Z, tail rotor = child with min local Y (lines 1190-1249) | contradicts IGI-RE: classification is per-child via `DefineMagicObj`, never via the parent's type (`src/magicobj/rigidcoll.c:472-487`); misses rotors on any other object type |
| Main rotor spin | `spun = base * mat3(rotate(angle, (1,0,0)))` then `spun[0] = n` (line ~1220, `angle = elapsed_time_secs_ * 15.0f`) | axis/order match IGI-RE; `spun[0] = n` is a mathematical no-op |
| Tail rotor spin | axis chosen as the column with the largest `|base[c].x|`, `spun = mat3(rotate(angle, n)) * base`, `angle = -elapsed_time_secs_ * 25.0f` (lines 1232-1238) | **pre-multiplied** (world-axis) rotation and a **negative** angle, i.e. the opposite composition order and sign to IGI-RE `src/rotor/rotor.c:150-154` |
| Spin vs. children | the spun matrix was stored in `childWorldMat` and passed to the recursion | the hub's spin was baked into its blades' base frame, so a nested rotor accumulated its parent's rotation; IGI-RE keeps the child frame unspun (`src/rotor/rotor.c:179, 280`) |

After this change:

* `source/renderer/atta_children.h` (new, header-only, host-portable) holds the
  three rules with their IGI-RE citations: `kAttaRecordSize`/offsets,
  `IgiAttaLocalRotation`, `IgiAttaChildWorld`, `IgiRotorSpinMatrix`.
* `source/renderer/renderer_objects_atta.cpp:19-41` — `RotorAttachmentIds()`
  reads `TASKTYPE_ROTOR` names from the user's `MAGICOBJ.QVM` via
  `igi::MagicObjectRegistry` (the same QVM the editor already parses for
  deathzones/portals), cached per path.
* `source/renderer/renderer_objects_atta.cpp:1209` — child frame from
  `igi::IgiAttaChildWorld`.
* `source/renderer/renderer_objects_atta.cpp:1217-1225` — a rotor attachment
  draws with `drawWorldMat = IgiRotorSpinMatrix(childWorldMat, t)` (local X,
  post-multiplied, single positive rate) while the recursion still receives the
  unspun `childWorldMat`.
* `source/renderer/renderer_objects_atta.cpp:1286` — the leaf draw uses
  `drawWorldMat`.

The parent-type gate and the geometry heuristics are gone, so
`current_draw_obj_type_` is no longer read by this file (it is still declared and
assigned in `source/renderer/renderer_objects.h` /
`source/renderer/renderer_objects.cpp`, which are outside this task's write
scope).

---

## 5. Retail data: the compound helicopter

Dumped from the shipped level 1 data
(`MISSIONS/location0/LEVEL1/MODELS/models/709_01_1.mef`):

| ATTA entry | name | type in `MAGICOBJ.QVM` | local position |
| --- | --- | --- | --- |
| 0 | `711_01_1` | `TASKTYPE_ROTOR` (main) | `(3.5e-07, -0.1069, 8894.79)` |
| 1 | `712_01_1` | `TASKTYPE_ROTOR` (tail) | `(-503.40, -34107.76, 7310.17)` |
| 2, 5 | `709_02_1` | `TASKTYPE_HELIWHEEL` | — |
| 3, 4 | `709_03_1` | `TASKTYPE_HELIWHEEL` | — |

`711_01_1.mef` (the hub, 217 vertices) in turn has two ATTA children both named
`711_02_1` (30 vertices each, one identity and one 180-degree-flipped), and
`712_01_1.mef` (106 vertices) has none. Both rotor records are orthonormal to
1e-6, which is why the editor's transpose simplification is safe (asserted by
`AttaChildren.RetailHeliRotorRecordsAreOrthonormalAndMatchIgiRe`).

The main rotor's local X axis in the hull frame is `~(0, 0, -1)` — the vertical
shaft — and the tail rotor's is `~(1, 0, 0)` — lateral. Since IGI-RE spins about
local X, `base * RotX(a)` turns the main disc about the mast and the tail disc
about its lateral axis; both are asserted by
`AttaChildren.RetailMainRotorLocalXAxisIsTheVerticalShaft`.

The editor's pre-change heuristic happened to pick the same axis for this hull
(max local Z for the hub, and the largest `|x|` column for the tail rotor), which
is why the earlier commits in this series kept patching it by trial and error —
but the axis choice was coincidence, and the tail rotor's sign and composition
order were wrong.

---

## 6. `MEF_NAXT` is not a child-node chain

The issue text suggested `NAXT` participates in the child chain. It does not.
`Mesh_Load` reads the `MEF_NAXT` chunks into `m->anims` as a `MeshAnim` array
(IGI-RE `src/model/model.c:379-398`, `src/model/model.h:34-40`), i.e. mesh/texture
animation state consumed by `ModelLod_GetTexAnimCount` / `SetTexAnimFrame` /
`ApplyTexAnims`, not attachment records. The only child-node source is the
`ATTA` chunk. Recorded here so the next reader does not re-investigate it.

---

## 7. Verification status

**Verified in this environment**

* ATTA record layout, stride and count accessor — IGI-RE `src/model/modeltypes.h:23,26`,
  `src/lod/mesh3d.c:493-503`, `src/magicobj/rigidcoll.c:362-367`,
  `src/shadow/sector.c:517-521`; byte dump of the shipped `709_01_1.mef` above.
* The parent/child rule — IGI-RE `src/magicobj/rigidcoll.c:405-451`; differential
  test `tests/test_atta_children.cpp` (7 tests, all passing).
* Rotor classification — IGI-RE `src/rotor/rotor.c:191-211` +
  `src/magicobj/magicobj.c:188-220`. The production query
  (`RotorAttachmentIds()`) was replayed host-side against the shipped
  `MAGICOBJ.QVM` — same calls: `QVM_Parse` -> `QVM_DecompileToString` ->
  `MagicObjectRegistry::LoadDecompiledSource` -> filter
  `task_type_name == "TASKTYPE_ROTOR"`:

  ```
  definitions=374 rotors=6
    ROTOR 700_02_1
    ROTOR 700_03_1
    ROTOR 700_04_1
    ROTOR 711_01_1
    ROTOR 711_02_1
    ROTOR 712_01_1
  ```
* Rotor spin axis/order/sign — IGI-RE `src/rotor/rotor.c:110-166`; asserted by
  `AttaChildren.RotorSpinPostMultipliesLocalXAndKeepsShaftAndPosition` and
  `AttaChildren.RotorSpinDoesNotLeakIntoTheChildFrame`.
* Cross-build: `python3 tools/igi-re-parity/tu-check.py` over all seven
  `renderer_objects*.cpp` translation units — `checked 7 translation unit(s), 0 failed`.

**Unverified (cannot be checked here)**

* Rendered output. The editor is Windows/OpenGL-only and this environment cannot
  run it, so "the rotor is visible and spins the right way on screen" is *not*
  proven by this work. What is proven is that the transform fed to the leaf draw
  matches the retail rule.
* The exact spin rate and the `dir` blade-copy loop. The retail rate is
  `dir * speed * car->v.speed` (IGI-RE `src/rotor/rotor.c:131,163,260`) and needs
  a vehicle speed the editor does not have; the preview uses a fixed
  `15 rad/s` for all rotors. Reading the rotor's `DefinePhysicsObjType` entry
  from `physicsobj/HELIS/<TYPE>/HELI.QVM` would give exact per-rotor rates but
  needs a new physics-definition parser — deliberately out of scope here.
* `TASKTYPE_WHEEL` / `TASKTYPE_HELIWHEEL` / `TASKTYPE_GEAR` / `TASKTYPE_RUDDER`
  children are still drawn as static geometry. The retail spin for those is
  driven by vehicle speed and steering input (`Car` state the editor has no
  equivalent of), so no evidence-backed preview rule was implemented.
* Whether "moving platforms have no path preview" (the other half of issue #40)
  is addressed at all — that is level-script data, not ATTA.
