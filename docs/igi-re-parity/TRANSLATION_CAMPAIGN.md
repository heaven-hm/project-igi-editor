# IGI-RE translation campaign — checkpoint

Directive (user): IGI-RE is the decompiled source of the game and the working
reference. Every behaviour is a translation of it — copy the C, convert to C++, wire it
into the editor's real call path. No invented logic, no research/open-igi-derived
behaviour. No new tests; the engine source is already exercised. Small commits per
module. Nothing is pushed until the user confirms the binary.

Worktree: `/Users/haseeb-mir/Documents/Code/project-igi-editor-igi-re`
Oracle:   `/Users/haseeb-mir/Documents/Code/IGI-RE`
Never touch `/Users/haseeb-mir/Documents/Code/project-igi-editor` (original checkout).

## Gate — all three green

| gate | result |
| --- | --- |
| build gate (`scripts/build-windows.sh`) | x64 link OK, i686 compile OK, exit 0 |
| host lane (`scripts/run-host-tests.sh`) | OK: 1090 tests passed in 63 suites, exit 0 |
| Windows suite (i686 `igi_tests` under Wine) | 1521 ran, 1509 passed, 0 failed, exit 0 |

The Windows suite is the gate that finds what the host lane cannot, and it is the one
that mattered: it opened at **19 failures** and reached 0 across rounds 3-7. Every fix in
that stretch was the editor failing to match IGI-RE, never an engine defect.

## Committed (all unpushed, in order)

Translation modules: QVM + QSC readers; MEF loader; pickups / snow-rain / LOD;
menu write-back (#74, byte-for-byte with retail); animation rules; animation driver +
skinning; splines/roads (#79/#69); level/terrain (#22) + engine cell-grid snap; GRAPHS
(311/311 retail graphs byte-identical); RES/MTP texture + archive loading.

Corrections and reconciliations:
* `res_chunks.h` shipped without an include guard and broke both arches - fixed.
* menu label resolution had stopped (`RES_BuildIndex` keyed by the engine's resource
  identity; `menu_text_res.cpp` still assumed leaf names) - 35/35 ingame labels resolve.
* `AnimPlayback::Update` treated engine ticks as milliseconds, running every preview
  4.8x slow (`kTicksPerSecond` 4800, `src/boneobj/anim.c:9`) - fixed.
* the invented 90-degree turn on weapon/ammo pickups removed from all four draw paths
  (the engine applies only the authored matrix; `Direct3DRender_DrawRigidMesh` 0x49DF60,
  `src/render/render.c:965-966`).
* test reconciliations, each with its IGI-RE citation: graph route-table preservation,
  animation tick timing, LOD corpus name spelling, texture member identity, FNT fixture
  container/structs.

**The index-key lesson:** one change to `RES_BuildIndex` (keys became the engine's
upper-cased, slash-folded resource identity) silently broke three consumers - menu
labels, the LOD corpus test, and the texture preview. All three were the same defect.

## Still to do - the wiring, which is the point of the objective

The modules are transcribed but several have **no caller**, so the editor does not yet
use the engine's method on those paths:

1. **`igi::spline`** - `renderer_splines.cpp:218` still calls the editor's own
   `BendSegmentMesh`. The engine's normals differ by ~22 degrees. The invented
   `std::clamp(count, 2, 20)` was removed (the engine's rule is a cap, `spline.c:771-772`),
   but the span inputs are still missing because the parser drops them.

   Verified from the retail data, so this part is not guesswork. The shipped declaration is
   in `MISSIONS/location0/level1/objects.qsc`:

       Task_DeclareParameters("SplineObjWaypoint", "Orientation", "Real32x9", "Position",
         "ObjectPos", "Waypoint Model", "String16", "Segment Model", "String16",
         "NumAreas", "Int32", "Align", "bool8", "Flip", "bool8",
         "Automatic Orientation", "bool8");

   and a shipped instance reads

       Task_New(-1, "SplineObjWaypoint", "", 0, 0, 3.1415929794311523, 25287152.0,
                -55948040.0, 174410960.0, "waypoint", "368_01_1", 20, FALSE, FALSE, FALSE)

   so the token indices are `[9]` model, `[10]` segment model, `[11]` NumAreas (Int32,
   20 or 10 in the retail rows), `[12]` Align, `[13]` Flip, `[14]` Automatic Orientation.
   The editor parses only up to `[10]`.

   The engine's waypoint fields are `int segments` plus `char straight`, `char rigid`,
   `char reversed`, `char autoOrient` (`src/editobj/editobjtypes.h:6-25`), and
   `SplineObjWaypoint_Changed` derives `rigid = obj->rigid || w->straight` (`:769`).

   **What is NOT verifiable from IGI-RE:** the mapping from those authored params to those
   fields. The ParamTable for the type is registered on `gSplineObjWaypointType`, a runtime
   global that is *used* (`src/editobj/spline.c:115`, `src/tagitem/spline.c:69`) but never
   defined in the decompilation, so there is no table to read. `NumAreas -> segments` is
   certain by name and by the retail values; `Automatic Orientation -> autoOrient` is
   near-certain; `Align -> straight` and `Flip -> reversed` are inferences. Do not wire
   those two on a guess - a wrong mapping puts authored data in the wrong field, which is
   worse than not parsing it.
2. **Per-frame LOD switch (#62)** - prerequisites all exist (`GetLodMeshName`,
   `SelectLodForDistance`, `view_define_.tan_half_fovx_` at `common.h:255`). It needs the
   LOD-0 `MeshHeader` read (`igi::lod::ReadMefMeshHeader`), the mesh lookup switched to
   the selected LOD at `renderer_objects.cpp:971`, AND the lightmap index changed from
   `(*lightmaps)[si]` to the drawn LOD's entry at `:1271/1277`. Do not do the first
   without the second: a lower LOD without its own bake binds the wrong atlas.
3. **Animation driver** - no consumer. `app.cpp:1259-1261` still evaluates through
   `animRegistry_.EvaluateWorld`; `renderer.cpp:403` still calls `SkinSkinnedVertices`.
4. **`snow.tex`** - `Renderer_Objects::GetOrLoadTexture` and `objects_` are private, so
   `Renderer_Rain` cannot reach the game's `.tex` decode. Until bound, snow renders as the
   engine's own null-texture branch (opaque white quads).
5. **Pickup proximity grant** - `app.cpp:2729-2790` grants on an explicit use action; the
   engine grants on proximity via the touch handlers (`igi::PickupTouchesFromModel`).

Stale comments worth correcting when those files are next touched:
`renderer_objects.h:196-213` still says the complexity branch "is not implemented,
because the editor always has lod.qvm" - but `lod.qvm` names only 450 distinct retail
models, so the complexity branch is the common case and it IS implemented now.
`docs/igi-re-parity/LOD_CHAINS.md` says the same. `docs/igi-re-parity/PICKUPS.md` still
says "not wired".

## Open judgement calls (need the user, not a guess)

* **`renderer_terrain.cpp:230`** binds `mat_tex_.texs_[rc->tex_idx_]` unguarded, and
  `LEVEL12/TERRAIN.TEX` holds 12 textures while level 12's tile maps reference 12, 13 and
  14. The engine over-reads there too - `new_set` allocates `count*0x24+0x30`
  (`src/terrdraw/texset.c:41`) - so adding a clamp would invent a guard the engine lacks.
  Recorded, not changed.
* **Two duplicate RES modules** exist (`res_writer.*` / `res_chunks.h` from one writer,
  `res_compiler.*` from another, each re-deriving the chunk layer). They compile and the
  suite is green, but one of them should be removed once someone decides which.
* **Host-lane coverage** reads 1090 tests in 63 suites where an earlier run read 1100 in
  64. Zero failures, so nothing is red, but 10 tests stopped running and that is
  unexplained.
