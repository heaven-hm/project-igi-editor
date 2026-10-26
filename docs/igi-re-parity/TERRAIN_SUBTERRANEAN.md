# Level 12/13 subterranean terrain — engine load path vs editor (issue #22)

Oracle: IGI-RE (`/Users/haseeb-mir/Documents/Code/IGI-RE`). Every claim below is
function + HOOK address + file:line. "Verified" means a command or a numeric
comparison produced the number, not that the editor was run (it cannot run on
this host: GL 2.1 vs GL 4.1 shaders).

## The engine's load path (the order the editor must match)

`LevelStart` 0x415B30 (`src/game/game.c:405-407`) is the caller:

```
crt_sprintf(terrain, "%sterrain/terrain", gMissionPath);
World_LoadTextures(gGame->world, terrain);   /* 0x4C5F00  src/magicobj/world.c:295 */
Terrain_Load(gGame->world, terrain);         /* 0x4C6240  src/terrain/terrain.c:70  */
...
LevelLoad(gMissionPath, 30);                 /* 0x4F0E10 -> Level_Load 0x4F0D40 -> objects.qsc */
```

* `World_LoadTextures` 0x4C5F00: `<path>.tex` (TexSet_Load 0x5175A0), palette, then
  `<path>.lmp` (src/magicobj/world.c:305-322). The `.lmp` name is kept for the later
  `Terrain_LoadLightmaps` 0x4C6160 pass (src/terrain/terrain.c:36-67).
* `Terrain_Load` 0x4C6240: `QCube_Reset` (:78), `.bit` (:80-98), `.hmp` (:100-122),
  `.cmd` (:124-126), `.ctr` (:128-130), `.qsc` (:132-140), ctr leaf fixup (:142-143),
  `unkDC0/unkDC4` (:145-151), `Terrain_InvalidateLighting` (:152).
* Net file order: **tex, lmp, bit, hmp, cmd, ctr, qsc**. The editor's `Terrain::Load`
  now follows it (landed 03:57 in `source/level/terrain_io.cpp`, with the `.bit`/`.hmp`
  `size < 0xC00` guards from src/terrain/terrain.c:84-87 / :107-110).

## The cell grid that places the terrain child tasks

All four task types snap their script position to the same power-of-two grid and use
the cell **centre** as the object position; the cell minimum becomes the origin of the
task's bitmap/lightmap/heightmap:

```
int cell2 = (0x40000000 >> shift) * 2;   /* shift = QSC "Level" */
int mask  = -cell2;
min.x     = fistp(pos.x) & mask;         /* pos already includes w->offset[] */
centre.x  = min.x + cell2 / 2;
<map>->x  = <task>->pos.x - (0x40000000 >> shift);   /* == min.x */
```

* `TextureModifier_Changed` 0x5062A0 — src/editcam/texmod.c:235-296, :459-460
* `TerrainLightMap_Changed` 0x4F8780 — src/graphnode/lightmap.c:325-370;
  draw origin `TerrainLightMap_SetDrawInfo` 0x4F8EF0, src/graphnode/lightmap.c:434-437
* `HeightMap_Changed` 0x507410 — src/editcam/heightmap.c:265-320, :459-460
* `DiscardTerrain_Changed` 0x52C290 — src/terredit/discard.c:200-248
* `fistp` = `fistpll`, round-to-nearest-even (src/graphnode/lightmap.c:108-112);
  `w->offset[]` is 0 for a fresh world (`World_New` 0x4C5B20, src/magicobj/world.c:183-185)

Transcribed into `Terrain_SnapCell` / `Terrain_EngineFistp`
(`source/level/terrain_io.cpp:37-50`) and applied at :678 (TextureModifier),
:783 (TerrainLightMap), :913 (HeightMap), :993 (DiscardTerrain).

**Verified equivalent, not a behaviour change on this data.** For all 290 level-12
child tasks (22 `DiscardTerrain` + 268 `TerrainLightMap` in the editor's decompiled
`bin/Release/editor/qed/temp/objects.qsc`) the old `Level::GetDynCube` bit-walk centre
equals the engine's snapped centre exactly, so `cube_min = centre - half` and
`cube_min = snapped min` agree. The transcription matters only at a cell boundary or
when the position lies outside the ctr tree, where `GetDynCube`
(`source/level/level.cpp:721-890`) follows `children_[idx]` without testing
`children_mask_` and can read ctr record 0.

## Differences that were measured and are *not* the cause

* **Ground drop.** The engine drops the placed point to the ground before snapping for
  `TerrainLightMap` (lightmap.c:337-350) and `HeightMap` (heightmap.c:279-292) with
  `Coll_DropPointsMat` 0x4CB9F0, `dz = kDropZ = -2^29`, up to 5 attempts. The editor
  uses the authored Z. Measured against the level-12 ctr/cmd geometry: for 268/268
  level-12 lightmaps the authored Z and the terrain-surface Z snap to the **same cell**,
  so the missing drop does not change level-12 placement. (One task is 158619 units off
  the ground but does not cross a cell boundary.)
* **Discard extent.** `DiscardTerrain_Visit` 0x52C0B0 sets `gQCubeSkipCells`
  (src/lod/qcubewalk.c:294) while the walk is inside the box; the box is the cell at
  `lod == shift`, i.e. the same subtree the editor marks with
  `CUBE_FLAG_DISCARD_TERRAIN`, so the extents already agree.

## Differences still open

1. **Level 12's tile maps index textures its `.tex` does not have — leading candidate
   for #22.** Verified by parsing `MISSIONS/location0/LEVEL12/TERRAIN/TERRAIN.TEX`:
   `layer_count = 12` (header at 0x20), layer records at 52 + i*32, pixel data
   [436, 393652) with the footer at 393652 — exactly 12 textures, indices 0..11
   (matches `TexSet_Load` 0x5175A0, src/terrdraw/texset.c:52-90, which also reads the
   count at `p+0x20` and layers from `p+0x34` with stride 0x20). But
   `LEVEL12/TERRAIN/terrain.qsc` has `CreateTerrainTileMap(5, ...)` referencing
   textures 12,13 and `CreateTerrainTileMap(6, ...)` referencing texture 14.
   `source/renderer/renderer_terrain.cpp:230` does
   `GL_BindTexture2D(0, mat_tex_.texs_[rc->tex_idx_])` with no bounds check while only
   12 entries were appended (`LoadMatTex`, :146-155), so those terrain tiles bind an
   uninitialised slot (texture 0). The engine reads past its created records too —
   `new_set` allocates `count*0x24 + 0x30` (src/terrdraw/texset.c:41) — so the retail
   data is tolerated there. LEVEL13 and LEVEL5 `.tex` both carry 18 textures.
   Fix belongs outside this module: `renderer_terrain.cpp` (log + clamp) or the `.tex`
   loader in `level_common.cpp`.
2. **HeightMap `.hmp` slot allocation.** `HeightMap_Changed` 0x507410 takes the first
   free `.hmp` slot when the script's Bitmap ID slot has no data
   (src/editcam/heightmap.c:330-346), and spawns one child box per cell height above and
   below for terrain that is higher/lower than a single cell (:410-441). The editor does
   neither (it skips a task whose slot has no data). Affects levels 1,3,6,8,9,10 — those
   are the levels that ship a `TERRAIN.HMP`; 12 and 13 have none.
3. **Global terrain vertex/index cap.** `source/level/terrain_mesh.cpp:786-796` breaks
   out of the per-cube loop when `MAX_TERRAIN_VERTICES` (65536, `source/common.h:57`)
   or `MAX_TERRAIN_INDICES` is reached, silently truncating the frame's terrain. The
   engine has no global cap: each cell gets its own pool element
   (src/glowmesh/terrainmesh.c:485-505). Unverified for level 12 — the GUI cannot run
   on this host.

## Verification run

```
python3 tools/igi-re-parity/tu-check.py source/level/terrain_io.cpp \
    source/level/terrain_mesh.cpp source/level/terrain_files.cpp
==> source/level/terrain_io.cpp
OK    (source/level/terrain_io.cpp)
==> source/level/terrain_mesh.cpp
OK    (source/level/terrain_mesh.cpp)
==> source/level/terrain_files.cpp
OK    (source/level/terrain_files.cpp)
checked 4 translation unit(s), 0 failed      # exit 0
```

Compiling is not behavioural equivalence: no editor run, no rendered-surface check.
