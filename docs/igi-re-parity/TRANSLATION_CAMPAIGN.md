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
| host lane (`scripts/run-host-tests.sh`) | OK: 1095 tests passed in 64 suites, exit 0 |
| Windows suite (i686 `igi_tests` under Wine) | 1526 ran, 1514 passed, 0 failed, exit 0 |

**Trap when re-running the Windows suite:** `build-windows.sh` relinks `bin/igi_tests.exe`
as **x64** in its gate-1 pass, so a Wine run straight afterwards fails with exit 53
(wrong architecture). Force an i686 relink first:
`rm -f bin/igi_tests.exe && cmake --build build-mingw --target igi_tests --parallel`.

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

## The parse-path drift thread is closed

Five defects came out of comparing the functions that dispatch on `obj.type`, and all five
are fixed with tests:

1. `ParseTaskLine` never set `isSplineWaypoint` and never read the waypoint's args 11-14.
2. The save path wrote only args 3-10, dropping the same four parameters.
3. `AIGraph` and `HumanAI` were handled by `LoadRecursive` only, so a line parsed through
   `ParseTaskLine` had no graph origin, node count or graph id.
4. Eleven types in the rotating-placement group had no `LoadRecursive` branch, so they
   loaded with a position and no rotation.
5. `UpdateCoordinatesInLine` has no generic fallback and did not name `AIGraph`, so a moved
   graph silently reverted to its old origin on save.

The comparisons that closed it, for anyone tempted to redo them:
`LoadRecursive` handles 56 types and `ParseTaskLine` 54, with a true gap of 15 -- of which 3
are already covered by the confirmed-candidate fallback, 10 declare no position at all, and
`AnimTask`/`CutScene` were the only two needing a branch (both fixed).  `AIGraph` was the
only one of the six parser-types missing from the coordinate writer with coordinates to
write; `HumanAI` declares none and `Container`/`Dynamic`/`Level`/`Static` are engine
grouping types with no retail declaration.  `SerializeObjectRecursive` has no type branches
at all.

Also note, since it looks like an inconsistency and is not: the coordinate writer uses
`saveZ = obj.pos.z - obj.snap_z_offset` for types that snap to terrain.  `AIGraph` writes
`obj.pos.z` because its origin is an abstract world origin that the parse and load paths
both read directly, with no snap offset.

And the mechanism behind "3 of the 15 need no branch": the fallback's candidate is confirmed
against the **level's own** `Task_DeclareParameters` lines, collected at
`level_objects.cpp:44-48` and tested with `TaskDeclarationHasPositionAt3` at `:55-57` -- not
against the static schema in `task_schema.cpp`, which holds only 15 types.  So `HeightMap`,
`TerrainLightMap` and `TextureModifier` keep their candidate position because their level
declares "Position" ObjectPos first, and any type whose level does not is reverted to the
origin as intended (`:61-66`).  Reading the static schema instead would wrongly suggest
those three were broken.


## Still to do - the wiring, which is the point of the objective

The modules are transcribed but several have **no caller**, so the editor does not yet
use the engine's method on those paths:

1. **DONE since this note was written:** the invented clamp is gone; the four parameters
   are parsed by BOTH parse paths (`LoadRecursive` and `ParseTaskLine` -- they had drifted,
   and `ParseTaskLine` was silently dropping them); the save path writes them back (it was
   dropping them too); and `tests/test_spline_waypoint_params.cpp` proves the mapping,
   including the one-at-a-time boolean cases the retail corpus cannot distinguish.  The
   remaining step is the draw-path swap itself.

   **`igi::spline` still has no caller** - `renderer_splines.cpp:218` calls the editor's
   own `BendSegmentMesh`. The engine's normals differ by ~22 degrees. The span inputs the
   engine's path needs are now parsed and round-tripping.

   **Swap recipe, for whoever starts it.** The function to replace is
   `spline_geometry::BendSegmentMesh` (`source/renderer/spline_geometry.cpp:264-386`, 122
   lines).  Its result type is `BentSegmentMesh { std::vector<float> interleaved;
   std::vector<int> submeshVertexCounts; }` (`spline_geometry.h:73-76`), and the only
   consumer is `UploadBentSegmentMesh`, which walks `bent.interleaved` in float counts
   (`renderer_splines.cpp:49`, `:66`, `:220`).  The engine side is
   `igi::spline::BuildSplineSpanGeometry` -> `FillSplineSpanVertices` ->
   `PackSplineMeshSource`, producing `SplineRenderVertex { float pos[3]; float normal[3];
   float uv[2]; }` (`source/level/spline_geometry.h:197-201`) -- 8 floats per vertex, so the
   conversion into `interleaved` should be a straight copy plus one
   `submeshVertexCounts` entry.  `SplineSpanInput` (`:125-140`) wants `from`/`to`
   `SplineControlPoint`s, `segments` (the authored NumAreas, already clamped), `rigid`
   (`w->rigid || w->straight`), `reversed` (the authored "Flip"), `spline_min`/`spline_max`
   from `igi::SplineScanModelExtent`, and `matrix[10]` (`ModelObj.matrix`, used by the
   straight branch only).  The stride question is now answered: the renderer's interleaved
   vertex is **10 floats**, not 8 -- `UploadBentSegmentMesh` binds attrib 0 (3 floats, pos),
   1 (3, normal) and 2 (2, uv) at a 10-float stride (`renderer_splines.cpp:68-73`), and the
   producer fills the last two from the source vertex's second UV:
   `AppendBentVertex(verts, bentPositions[i], bentNormals[i], src.uv, src.uv2)`
   (`spline_geometry.cpp:376`, `submeshVertexCounts.push_back(verts.size() / 10)` at `:379`).
   So `SplineRenderVertex` (pos/normal/uv only) is NOT a drop-in: the swap has to supply
   `uv2` per vertex from the corresponding source vertex.  **That correspondence is now
   established and is the identity.**  `FillSplineVertices` (the body
   `FillSplineSpanVertices` delegates to, `source/level/spline_geometry.cpp:165-...`) walks
   `for (int i = 0; i < src.vert_count; i++, v += src.stride)` and writes `out[i]`, so
   output vertex i is source vertex i, same order, no reindexing.  Note also that the
   ENGINE's vertex is 8 floats -- `PackSplineMeshSource` packs position/normal/uv into
   `RigidMesh.vertices`, "which is also what gVBNormal holds (32-byte stride,
   src/render/render.c:626-641)" -- so the editor's trailing `uv2` pair is its own
   extension, not something the engine produces.

   With that, the swap is fully specified: call `FillSplineSpanVertices` to fill 8-float
   vertices in source order, then write each into the renderer's 10-float interleaved
   vertex, taking `uv2` from source vertex i's own `uv2`.

   **Scope warning -- the swap is NOT geometry-only.**  The engine transforms the model's
   vertex buffer in place and then draws it INDEXED with the model's own indices:
   `Direct3DRender_FillSplineVB` (HOOK 0x49CC80, src/render/render.c:613-670) fills the VB,
   and the draw is `IDirect3DDevice7_DrawIndexedPrimitiveVB(DEV, d->primType, vb,
   start + g->base, g->vertexCount, ...)` (`:526`, `:531`).  The editor instead EXPANDS each
   indexed triangle into its own vertices (`AppendBentVertex` per index,
   `spline_geometry.cpp:376`) and draws non-indexed
   (`glDrawArrays(GL_TRIANGLES, 0, sub.vertexCount)`, `renderer_splines.cpp:256`).
   So replacing only the geometry function would produce a vertex list whose triangle count
   no longer matches the source mesh's index buffer.  A faithful swap has to make the
   upload and draw indexed as well -- `UploadBentSegmentMesh` and the draw loop in
   `renderer_splines.cpp` are part of the change, not just the 122-line function.

   Convenient detail for that work: `RenderVertex` is 10 contiguous floats
   (vec3 pos, vec3 normal, vec2 uv, vec2 uv2 = 40 bytes), so the engine's `SplineMeshSource`
   can be filled directly with `verts = reinterpret_cast<const float*>(geometry.vertices.data())`
   and `stride = 10`; the engine reads v[0..2] as position, v[3..5] normal, v[6..7] uv, which
   is exactly that layout's prefix.

   **De-risked for the swap:** no build change is needed. `source/level/spline_geometry.cpp`
   is already compiled into `igi-editor` through the `source/level/*.*` glob
   (`CMakeLists.txt:76`, `:145`; its object file exists under
   `build-mingw/CMakeFiles/igi-editor.dir/`), and `${PROJECT_SOURCE_DIR}/source` is on the
   include path (`:220`), so `#include "level/spline_geometry.h"` works from the renderer.
   What remains is purely the code change: build a `SplineSpanInput` from the two waypoints,
   call `BuildSplineSpanGeometry` / `PackSplineMeshSource` / `FillSplineSpanVertices`, and
   feed the result to the existing GPU upload in place of `BendSegmentMesh`.

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

   **The field mapping is now CONFIRMED, not inferred.** `SplineObj_Open` HOOK(0x502250),
   `src/editobj/spline.c:184-225`, registers the waypoint ParamTable explicitly at `:215`:

       gSplineObjWaypointParams = ParamTable_New(8);
       ... &w.rot         Param_Real32x9()
       ... &w.point       Param_ObjectPos()
       ... w.modelName    Param_String16()
       ... w.segModel     Param_String16()
       ... &w.segments    Param_Int32()  &gSplineObjSegmentRange
       ... &w.straight    Param_Bool8()
       ... &w.reversed    Param_Bool8()
       ... &w.autoOrient  Param_Bool8()

   Position for position that matches the authored order (Orientation, Position, Waypoint
   Model, Segment Model, NumAreas, Align, Flip, Automatic Orientation), so:

       arg 11 NumAreas  -> w.segments    (Int32)
       arg 12 Align     -> w.straight    (bool8)
       arg 13 Flip      -> w.reversed    (bool8)
       arg 14 Auto Orient -> w.autoOrient (bool8)

   The editor parses only up to arg 10, which is why the engine's draw path cannot be
   fed today.

   **Correction to commit 523364e0.** That commit removed a lower clamp bound of 2 and
   called it "an invented lower bound the engine does not have". That wording was wrong:
   the engine does declare `gSplineObjSegmentRange.min = 2; max = 20;`
   (`src/editobj/spline.c:202-203`) and attaches it to both the container's `maxSegments`
   (`:209`) and the waypoint's `segments` (`:220`). What is true is that it is stored in the
   ParamTable entry's `extra` field (`src/param/param.c:167`) and nothing reads a RangeI
   back, so it is authored UI metadata rather than a load-time bound -- the runtime rule
   remains SplineObjWaypoint_Changed's cap (`:771-772`). The code change (std::min(x, 20))
   is right for runtime fidelity; only the "invented" characterisation was wrong.
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

### The two parse paths still disagree for 15 types -- LOAD side

`LoadRecursive` (`level_objects.cpp`) handles **56** types; `ParseTaskLine`
(`level_objects_serialize.cpp`) handles 54.  The 15 the line parser has and the load path
does not are:

    AnimTask CutScene Dirlight DirlightKeyframe DiscardTerrain FlatSky FlatSkyLayer
    GlobalLight GlobalLightKeyframe HeightMap LODSettings MipMapControl SoundSource
    TerrainLightMap TextureModifier

**Correction.** An earlier revision of this note said 33 types and listed AlarmLight,
Elevator, Generator, GenericPickup, GenericTBA, Plane, Radio, RotatingObject, Siren,
StationaryGun, HumanPlayer and others.  That was wrong, and the error was in the
measurement, not the code: the extraction regex
(`is[A-Za-z]+ = \(typeStr == "X"`) captured only the FIRST name of an OR-chain, so every
type after the first in a predicate like

    bool isSoldier = (typeStr == "HumanSoldier" || typeStr == "HumanSoldierFemale" ||
                      typeStr == "HumanPlayer" || typeStr == "HumanSoldierRPG" ||
                      typeStr == "Cabinet");

was counted as missing when it was handled.  Extracting every `typeStr == "X"` instead of
the first per predicate gives 56, and the true gap of 15.  The round-22 fix for the
rotating-placement group was still correct and still needed -- those types really were
absent -- but the size of the remaining problem was overstated by more than half.

Four defects have come out of this drift (see the parse-path test files), so the 15 are
still the place to look.  But it is NOT a mechanical fix: `LoadRecursive`'s fallback treats
args 3/4/5 as a *candidate* position and reverts it unless the level's own
`Task_DeclareParameters` agrees (`level_objects.cpp:540-558`), precisely because the layout
is not uniform across types.  Verify each against its retail declaration and its engine
ParamTable before adding a branch.
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
