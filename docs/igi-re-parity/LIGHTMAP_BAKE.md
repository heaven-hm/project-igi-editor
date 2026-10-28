# The lightmap bake button (#72) — the engine's naming, accept rule and bake parameters

Status: implemented in `source/runtime/lightmap_rules.{h,cpp}`, tested in
`tests/test_lightmap_rules.cpp`. Every constant, offset, format string and line
number below is cited to IGI-RE (`/Users/haseeb-mir/Documents/Code/IGI-RE`), the
verified C decompilation of retail `IGI.exe`. No web source, no external port and
no other reimplementation is used as authority. Nothing here is a rendered or
in-game result: the editor's GUI cannot run on this host (it needs OpenGL 4.1;
the host offers 2.1), so the evidence is the host-lane unit and corpus tests
quoted in §7.

**Verdict, in one line.** All three rules this issue needs are recoverable and are
ported as pure logic: the resource names are the literals `MISSION:lightmaps` and
`MISSION:lightmaps/lightmaps.res`; the `.olm` name is `<obj%05d>_<%05d>.olm`
under that directory; an existing `.olm` is accepted only when its magic is
`0x3DF5C28F`, its version `feq`-equals the mesh header version, its timestamp is
**strictly later** than the mesh header's, and its page count equals the mesh's
`texCount` — and even then a `full` request rebuilds it anyway.

**Update (this change).** The module now has callers: the editor's `.olm` reader
follows the engine's layout and loads **every page**, and each drawn render group
binds the page it names, which is what `set->tex[g->lightmap]` does. §5's
findings 5.1 and 5.2 are therefore fixed; §5b records what changed, the measured
retail evidence, and the two parts that remain open (the accept test on the live
path, and the bake-versus-recalculate decision).

---

## 1. The resource names are literals (Rule 1)

| Function | Address | IGI-RE | Produces |
|---|---|---|---|
| `Lightmap_GetDir` | `0x5210C0` | `src/litmap/lightmap.c:89-94` | `MISSION:lightmaps` |
| `Lightmap_GetResPath` | `0x5210E0` | `src/litmap/lightmap.c:96-101` | `MISSION:lightmaps/lightmaps.res` |

```c
char *Lightmap_GetDir(void)      { crt_sprintf(gLightmapDir, "MISSION:%s", "lightmaps"); ... }   /* :91 */
char *Lightmap_GetResPath(void)  { crt_sprintf(gLightmapResPath, "MISSION:%s/%s",
                                               "lightmaps", "lightmaps.res"); ... }             /* :98 */
```

Both format strings **and** both arguments are present as bytes in the exe: the
DATA block at `0x54F050` stores `"MISSION:%s"`, `"lightmaps"`, `"MISSION:%s/%s"`,
`"lightmaps.res"` at offsets +8 … +0x3D (`lightmap.c:433`).

Two consequences that matter for a bake button:

* `Lightmap_GetResPath` is the **archive** path, not a per-object name. It has no
  index, no object name and no `.olm` suffix. The editor's
  `lightmaps.res` lookup (`source/renderer/object_lightmap.cpp:67`) matches it.
* The directory in the `.olm` path is `MISSION:lightmaps`, which is the *build*
  path. The shipped archive entries are named `LOCAL:missions/location0/level<N>/lightmaps/objNNNNN_NNNNN.olm`
  (see §6). Whether `Resource_Exists("MISSION:lightmaps/x.olm")` resolves to the
  `LOCAL:…` entry is **not** established by the decompilation — see §8.

`Lightmap_FreeBuffers` (`0x521040`, `src/litmap/lightmap.c:66-87`) is the first
`Lightmap_*` entry point and frees the four builder scratch buffers
(`gLightmapBufA..D`, `:68-83`) and zeroes `gLightmapBufCount`/`gLightmapBuilt`
(`:84-85`). It is an allocation-lifetime function, not a naming or accept rule, so
nothing in it is ported beyond noting that `gLightmapBuilt` is written here and in
`Lightmap_BuildLod:95` and **never read anywhere in IGI-RE** (§8).

## 2. The `.olm` name is `%s/%s_%05d.olm` (Rule 2)

```c
crt_sprintf(path, "%s/%s_%05d.olm", Lightmap_GetDir(), name, index);   /* lightmapbuild.c:59 */
```

So the full string is `MISSION:lightmaps/<name>_<index>.olm`:

* separator: one literal `_`;
* index field: `%05d` — width **5**, zero padded, and it is a **minimum** width, so
  an index of 100000 prints six digits;
* extension: `.olm`, lower case.

`name` is **not** the model name. It is the `LightmapInfo` task's `name` field,
allocated by `LightmapInfo_MakeName` (`0x5235A0`, `src/litmap/lightmap.c:231-257`):

```c
crt_sprintf(name,    "%s%05d", "obj", n);   /* :240 */
crt_sprintf(li->name,"%s%05d", "obj", n);   /* :255 */
```

`n` is the first index for which **neither** holds:

* any **non-directory** entry of `Lightmap_GetDir()` whose name *contains* `obj%05d`
  as a **substring** (`crt_strstr`, `lightmap.c:188`; the `!e->isDir` test is in the
  same line), or
* any live `LightmapInfo` whose name contains it as a substring (`lightmap.c:244-251`,
  the `crt_strstr` at `:247`).

`crt_strstr` is a substring search, not equality, so a file called
`xobj00000y.olm` consumes index 0. That is the engine's rule and
`tests/test_lightmap_rules.cpp` pins it rather than "fixing" it.

The `index` argument of `Lightmap_BuildLod` is the ref's **position in its
`LightmapSet`**, advanced by `(*n)++` in `fill_refs` (`src/sector/sector.c:544-561`)
— the rule already recorded in `docs/igi-re-parity/LIGHTMAP_REFS.md`. The corpus
below is the first direct confirmation of it in the shipped file names.

## 3. The accept/reject test for an existing `.olm` (Rule 3)

```c
MeshHeader *mh = ((ModelLod *)lodp)->mesh->header;
if (mh->format != 3) return NULL;                                       /* lightmapbuild.c:53-54 */
mesh = (LightmapMesh *)mh->data;
if (!mesh->unk8 || !mesh) return NULL;                                  /* :56-57 */
crt_sprintf(path, "%s/%s_%05d.olm", Lightmap_GetDir(), name, index);    /* :59 */
if (Resource_Exists(path)) {
    Lightmaps *file = Resource_Load(path, 0);                           /* :61 */
    if (file->magic == LIGHTMAP_MAGIC                                   /* :63, magic :26 */
        && feq(mh->version, file->version)                              /* :63 */
        && DateTime_Compare(mh->time, file->time) < 0                   /* :64 */
        && mesh->texCount == file->count) {                             /* :64 */
        if (!full) { ... return file; }                                 /* :65-84 */
    } else {
        Resource_Flush(file);                                           /* :86 */
    }
}
lm = Mem_Alloc(mesh->texCount * sizeof(LightmapTex) + sizeof(Lightmaps), 4);
lm->count = mesh->texCount;                                             /* :90-91 */
```

**Four conjuncts, all mandatory, in this order:**

| # | Test | Citation | Detail |
|---|---|---|---|
| 1 | `magic == 0x3DF5C28F` | `lightmapbuild.c:26`, `:63` | `#define LIGHTMAP_MAGIC 0x3DF5C28F`; a new bake writes it at `:99` |
| 2 | `feq(mesh.version, file.version)` | `:63`, `dirlight.h:20-23` | **equal**, not "at least"; `feq(a,b) = !__builtin_islessgreater(a,b)`, so an unordered (NaN) compare counts as **equal**. A new bake writes `0.1f` = `0x3DCCCCCD` (`:98`) |
| 3 | `DateTime_Compare(mesh.time, file.time) < 0` | `:64`, `terredit/datetime.c:94-104` | the **file** must be **strictly later** than the mesh. Both sides are rendered as the text `"YYYY-MM-DD-hh-mm-ss-mmmm"` (`datetime.c:78`, `:84`, `:90` joined at `:59`, `:65`) and compared **lexicographically**, not field-wise numerically |
| 4 | `mesh.texCount == file.count` | `:64`, `:91` | exact page-count equality; a fresh bake writes `mesh->texCount` |

There is **no clamp, no tolerance and no fallback**: on any mismatch the loaded
file is flushed (`:86`) and a new `Lightmaps` is allocated (`:90`).

**`full` is a fifth input and it is not part of the test.** `return file;` (`:83`)
sits *inside* `if (!full) { … }` (`:65`), so with `full != 0` a file that passes all
four conjuncts is still ignored and rebuilt. `DecideOlmFile()` therefore returns
`kRebuild` for `full`, `kLoadPage0Only` otherwise.

Two further gates before any of this runs: the LOD's mesh must be **format 3**
(`:53-54`; `src/model/modeltypes.h:12` — "0 rigid, 1 bone, 3 lightmapped"), and
`mesh->unk8` must be non-zero (`:56`). `unk8` is unnamed in `LightmapMesh`
(`src/litmap/litmaptypes.h:138-150`), so what it means is **unverified** (§8).

### The on-disk layout the test reads

`Lightmaps` is the file header (`src/litmap/litmaptypes.h:36-48`), 0x3C bytes:

| Offset | Field | Citation |
|---|---|---|
| 0x00 | `int magic` | `litmaptypes.h:37` |
| 0x04 | `float version` | `:38` |
| 0x08 | `int time[7]` (28 bytes) | `:39` |
| 0x24 | `int fromResource` | `:40` |
| 0x28 | `int built` | `:41` |
| 0x2C | `int count` | `:42` |
| 0x30 | `int texels` | `:43` |
| 0x34 | `int texels2` | `:44` |
| 0x38 | `LightmapTexInfo *info` | `:45` |
| 0x3C | `LightmapTex tex[]` | `:46` |

each `LightmapTex` being 0x2C bytes with `short width` at +0x28 and `short height`
at +0x2A (`litmaptypes.h:11-17`), and the pixel block starting at
`0x3C + count * 0x2C` (`lightmapbuild.c:72`: `px = (char *)&file->tex[file->count]`)
with `width * height * 4` bytes per page (`:76-78`). Four bytes per texel is
corroborated by the texture allocation: `Texture_Alloc(t, NULL, t->width * 4,
t->width, t->height, 3, 0, 0xA808003C)` (`lightmapinfo.c:241`) — pitch
`width * 4`, format id 3, on texture unit 1 (`Texture_SetActiveTMU(1)`, `:240`).
The editor binds its lightmap on unit 1 as well
(`source/renderer/renderer_objects.cpp:1107`), but format id 3's enum is not
stated by the oracle, so the channel order is **unverified** (§8).

`texels` is **not** the pixel count. The corpus separates them: in 20,196 of
25,337 files `4 * texels` is strictly smaller than the pixel bytes, and in 5,141
they are equal — `texels` counts the lit texels (`lightmapbuild.c:374`), while the
buffer is always `width * height * 4` per page (`:109`). An editor must never size
a buffer from `texels`.

## 4. The parameters that decide a bake (Rule 4)

`LightmapInfo_Changed` (`src/dirlight/lightmapinfo.c:97-200`) is the only caller of
`Lightmap_BuildLod` in the decompilation (`:150`). Per ref `i`:

| Argument | Expression | Citation |
|---|---|---|
| `lod` | `ref.lod` | `lightmapinfo.c:150` (`sector.c:559` fills it) |
| `lodIndex` | `ref.lodIndex` | `:150` (`sector.c:557`) |
| `scale` | `(1 << ref.lodIndex) * info->scale * kTexelScale` | `:150`, `kTexelScale = 4096.0f` at `:75` |
| `name` | `info->name` (`obj%05d`) | `:150`, `lightmap.c:255` |
| `index` | the ref's position `i` | `:150` |
| `full` | `stale \|\| info->unkDC \|\| edited` | `:149` |
| `flags` | `edited` | `:151` |
| `unk` | `(int)float_bits(&info->unk94)` | `:151`, `float_bits` at `:80-86` |

* **`info->scale` is forced to 1.0f** at the top of the function (`:105`) before the
  scale is evaluated, so the authored **"Scale"** parameter (a `Real32` registered
  as `ParamTable_Add(…, &t.scale, Param_Real32(), 7, kScaleRange)` at
  `lightmapinfo.c:366`, with the range `{0.5f, 10.0f}` stored as the first 8 bytes
  of the DATA block at `0x54F050` and aliased `kScaleRange` at
  `lightmapinfo.c:433-434`; `RangeF` is `{min, max}`,
  `src/param/paramtypes.h:18-20`) never reaches a bake through this path. The scale
  is effectively `(1 << lodIndex) * 4096`.
* **`edited`** is set when any of three push-buttons is pressed:
  `info->unk70 || info->unk6F || info->unk71` (`:108-110`). Which authored button
  label maps to which field is **unverified**: the ParamTable registers ten
  *nameless* push-buttons (`lightmapinfo.c:374-383`), and no label table for them
  exists in the decompilation.
* **`stale`** comes from two places. The scale clause
  (`!info->unkCC && float_bits(lastScale) != 0x3F800000`, `:106-107`) can only fire
  while `unkCC == 0`; `LightmapInfo_Create` sets `unkCC = 1` (`lightmap.c:222`),
  `Changed` clears it (`:116`) and then sets `lastScale = info->scale = 1.0f`
  (`:173`), so after the first call this clause is dead. The live one is
  **`missing`**: any ref whose lightmaps were loaded but have `info == NULL`
  (`:111-114`). That is exactly what `Lightmap_BuildLod`'s load path sets
  (`lightmapbuild.c:69`: `file->info = NULL;`), so **an `.olm` accepted from disk is
  never reused across a parameter change** — the next `Changed` sees `missing`,
  sets `full`, and rebuilds.
* **The early out** (`:120`) skips the whole rebuild only when the model matched
  (`match[1]`, written by the `Mesh3D_GetEvent3` handler at `:119`), the scale is
  unchanged under `feq`, nothing is stale and `unkDC == 0`.
* **Quality = `info->unkA4`**, default **150** (`lightmap.c:212`), passed to
  `Hemicube_New` (`lightmapinfo.c:180`). `Hemicube_New` allocates a `size × size`
  top plus four `(size >> 1) × size` sides, all at 4 bytes per texel
  (`lightmap.c:110-121`); at 150 that is 22,500 top texels and 11,250 side texels.
* **The vertex-light pass** runs only when `unk6F || unk70` (`lightmapinfo.c:177`)
  and **`Task_Nothing2(info, info->unkB4)`** only when `unk71` (`:197-198`), with
  `unkB4` defaulting to 1 (`lightmap.c:216`).
* **`AppContext_GetLightmapsUsed()`** (`0x48F230`) gates only the **ref-set reload**
  (`:137-138`), **not** the `BuildLod` loop (`:147-162`): with lightmaps off, the
  previous set is reused and still baked.

The defaults a fresh `LightmapInfo` carries (`LightmapInfo_Create`,
`src/litmap/lightmap.c:211-223`): `scale = lastScale = unk90 = unk9C = 1.0f`,
`unkA4 = 150`, `unkA0 = 1000`, `unk94 = 100.0f`, `unkB4 = 1`, `unkCC = 1`,
`unkDC = 0`. `unk94`'s default 100.0f is passed as the *bit pattern* `0x42C80000`
= 1120403456 (`lightmapinfo.c:151`).

### The `.res` container lifecycle

The archive is loaded lazily by the first `LightmapInfo` and unloaded by the last
one: `if (!p->used && Resource_Exists(Lightmap_GetResPath())) Resource_Load(...)`
(`lightmap.c:200-201`) and `if (!p->used && Resource_Find(...))
Resource_Unload(...)` (`:315-316`). Both are ported as predicates.

## 5. Where the editor disagrees with the oracle

These are **findings**, not fixes. `source/renderer/*` and `source/app_editor.cpp`
are outside this module's write scope, and the Lead decides.

### 5.1 `olm_texture.cpp` uses a layout from a non-oracle source, and it is only right for `count == 1` — **FIXED**

*(The finding is kept verbatim below; §5b records the fix.)*

`source/renderer/olm_texture.cpp:9-17` documents the `.olm` layout as

```
// IGI1 .olm binary layout (see project-igi-conv Lightmap_docs.md §2.3-2.6):
//   [88]   main header
//   [16]   layer descriptor (pixel_width at +12, pixel_height at +14, uint16 LE)
//   [w*h*4] RGBA pixel data
static constexpr size_t kMainHeaderSize = 88;
static constexpr size_t kLayerDescSize  = 16;
static constexpr size_t kPixelStart     = 104;   // 88 + 16
static constexpr size_t kPwOffset       = 100;   // 88 + 12
static constexpr size_t kPhOffset       = 102;   // 88 + 14
```

Oracle: the header is **0x3C = 60** bytes, each page record **0x2C = 44** bytes
(`src/litmap/litmaptypes.h:36-48`), page 0's `width`/`height` are at
`60 + 40 = 100` and `102`, and the pixel block starts at `60 + 44 * count`
(`src/dirlight/lightmapbuild.c:72`).

For `count == 1` the two agree exactly — 60 + 44 = 104 and the two shorts land on
100/102 — which is why the reader has worked. For `count > 1` they do not: the
editor reads page 0's dimensions and `4 * w0 * h0` bytes and stops
(`olm_texture.cpp:40-48`, `:64-79`), so pages 1…n are silently dropped, and its
"truncated .olm pixels" check passes. **The retail corpus contains 13 such files**
(12 with `count = 2`, 1 with `count = 3`; §6), e.g.
`obj00006_00052.olm` = 256×256 + 256×64. The engine loads all `count` pages
(`lightmapbuild.c:73-80`) and `lm->count = mesh->texCount` (`:91`).

The editor's reader also never checks `magic` (`0x3DF5C28F`), `version`, or the
`count` field against anything, where the engine rejects a file unless all four
conjuncts of §3 hold (`lightmapbuild.c:63-64`). Its R↔B swap
(`olm_texture.cpp:72-76`) is *consistent* with format id 3 and pitch `width * 4`
(`lightmapinfo.c:241`), but the oracle does not name the format's enum, so that
part is unverified (§8).

### 5.2 `ObjectLightmapManager::LoadLevelLightmaps` accepts any plausible entry

*(Still true of that class, which has no caller — see §5b.)*

`source/renderer/object_lightmap.cpp:76-117` reads `count` at offset 44 (`:80`) and
page dimensions at `60 + i * 44 + 40` (`:90`) — the oracle's offsets — but its
guard is `count == 0 || count > 64` (`:81`), a bound the engine does not have, and
it checks **no** magic, **no** version, **no** timestamp and **no** `texCount`
comparison. On a page whose dimensions or byte count do not fit it `break`s out of
the loop and keeps the pages it already collected (`:97-101`), where the engine
loads a whole file and trusts the loader (`lightmapbuild.c:61`).

So the editor binds an `.olm` that the engine would flush and rebuild whenever the
file's timestamp is not strictly later than the mesh's, or its page count differs
from `mesh->texCount`. In the retail corpus the page count never exceeds 3, so the
`> 64` guard never fires — it is a latent difference, not an active one.

### 5.3 `GetLightmapTexture`'s format string is right; its inputs are not the oracle's

`source/renderer/object_lightmap.cpp:131` builds `"%s_%05d.olm"`, byte-for-byte the
engine's format (`lightmapbuild.c:59`). But the engine's `%s` is the
`LightmapInfo` name `obj%05d` (`lightmap.c:255`), and its `%05d` is the ref's
position in the `LightmapSet` (`sector.c:557-561`), not a draw-record index and not
a task id. The function still has zero callers, so nothing disagrees in behaviour
today; the divergence is in what the parameters are *called*. This is the same
finding as `docs/igi-re-parity/LIGHTMAP_REFS.md`, now confirmed from the shipped
file names (§6): `obj00006_00052.olm` is object `obj00006`, ref 52.

### 5.4 The editor's staleness rule is a different rule from the engine's

The editor decides reuse-vs-rebake from a **recorded bake pose** plus thresholds:
`RecalcLightmapToOlm` (`source/app_editor.cpp:519-549`) skips the recalc when
`|rotNew - rotOrig| < 0.01` and `|posNew - posOrig| < 1.0` (`:531-532`), where
`--rot-orig` is the pose recorded with the bound `.olm`
(`source/runtime/lightmap_recalc_policy.h`) and the bake itself is an external
`igi1conv lightmap recalc` call taking `--model --qsc --task-id --mef --rot-orig
--rot-new --sun-dir --sun-color --ambient`
(`source/utils_igi1conv.cpp:201-221`, driven from `source/app_editor.cpp:467-504`).

The oracle's reuse decision is instead **the four-conjunct file test** of §3
(magic, version, mesh-time < file-time, `texCount == count`), plus the `missing`
rule that forces a full rebuild as soon as a bake has been loaded from disk
(`lightmapinfo.c:111-114`). There is no pose in the engine's test and no timestamp
in the editor's. Both rules are internally consistent; they are simply not the same
rule, and the editor has no way to reject an `.olm` whose timestamp predates its
mesh. Recorded, not changed.

Note also that the oracle's own scale parameter is inert (§4): a bake UI that
copies the retail "Scale" `Real32` (range 0.5…10) would be a no-op, because
`info->scale` is reset to 1.0f at `lightmapinfo.c:105` before it is used at `:150`.

---

## 5b. What was wired, and the retail evidence for it

`source/runtime/lightmap_rules.h` now has callers, so the editor's lightmap
binding path uses the engine's own layout and page rule instead of a descriptor
guess.

### The rules that were wired

| Rule | Oracle | Where it now runs |
|---|---|---|
| the `.olm` layout is a 0x3C header + `count` × 0x2C page records + `count` pixel blocks in page order | `Lightmaps`/`LightmapTex` (`src/litmap/litmaptypes.h:11-48`); the loader's cursor `px = (char *)&file->tex[file->count]`, `px += n * 4` (`src/dirlight/lightmapbuild.c:72-78`) | `ParseOlmPageRecords`, `OlmPagePixelOffset`, `PlanOlmPages` (`source/runtime/lightmap_rules.{h,cpp}`) → `LoadOlmPages` (`source/renderer/olm_texture.cpp`) |
| a lightmapped LOD has **one page per mesh page**: `lm->count = mesh->texCount` | `HOOK(0x523D40, Lightmap_BuildLod)` `src/dirlight/lightmapbuild.c:91` | `LoadOlmPages` returns every page, not page 0 |
| the drawn page is the **render group's own** `LmGroup.lightmap`, i.e. `set->tex[g->lightmap]`; `0xFFFF` = none | `HOOK(0x49DF60, Direct3DRender_DrawRigidMesh)` `src/render/render.c:1014-1048`, the binding at `:1033`; the sentinel tested at `:1098`, `:1114`; `LmGroup.lightmap` at `+0x12` (`src/render/render.c:40-54`) | `ParsedGeometry::RenderBlock::lightmapSlot` (`source/renderer/mef_native.cpp:1058`, already parsed) → `SubMesh.lightmapPage` (`source/renderer/model.{h,cpp}`) → `igi::lightmap::PageIndexForGroup` (`source/renderer/lightmap_ref_index.h`) → the per-submesh bind in `source/renderer/renderer_objects.cpp` |
| the file is the **drawn LOD's** ref (`o->lightmaps[lod].lightmaps`), and a LOD with no bake binds no atlas | `src/render/render.c:1019`, `:1052-1054` | unchanged (`OwnerLightmapEntryForDrawnLod`), now indexed into a page vector |
| the file-side identity: `magic == 0x3DF5C28F`, `feq(version, 0.1f)` | `lightmapbuild.c:26`, `:63`, `:98`; `feq` `src/dirlight/dirlight.h:20-23` | validated in `LoadOlmPages` |

The storage shape changed with it: the per-task lightmap store is now
`taskId -> vector<LOD> of vector<page>` (`Renderer_Objects::SetLightmapForTask`),
because one `.olm` per LOD and one page per render group are two different
indices and the old single `vector<GLuint>` could only express the first.

### Measured on the retail install

```
[lightmap-rules] archives=14 files=25337 pages=25351 multiPage=13 objects=121
                 refIndices=543 count1=25324 count2=12 count3=1
                 plannedFiles=25337 plannedPages=25351 planMismatches=0
                 multiPageDistinctPixels=12
[lightmap-pages] mefs=2667 format3=2027 blocks=11484 page0=11477 offPage0=7
                 noPage=0 maxPage=1 modelsOffPage0=1
[probe] mefs=2667 withMesh=2667 timeAllZero=0 timeNonZero=2667
[probe] mesh version bits 0x3DCCCCCD -> 2667 files
```

* **13** shipped `.olm` files carry more than one page (12 with 2, one with 3).
  In **12** of them a later page is *not* a copy of page 0 — the old reader
  dropped real lightmap data. The exception is `level7/obj00019_00068.olm`, whose
  pages 0 and 1 are byte-identical 256×256 blocks (its third page, 256×16, is
  distinct).
* **Every** shipped file plans to its own end (`planMismatches=0`), so the
  streaming plan and the whole-buffer reader agree on all 25,337 files.
* Across all **2,667** retail `.mef` files, **2,027** are format 3 and contribute
  **11,484** render blocks. **11,477** name page 0, **7** name page 1 — all seven
  in one model, `428_02_1.mef` — and **none** carries the `0xFFFF` sentinel, so
  the page index is real data and the engine's unguarded `set->tex[g->lightmap]`
  read is in range on retail data.
* All **2,667** MESH headers carry version bits `0x3DCCCCCD` (0.1f), the same bits
  every shipped `.olm` carries, so the loader's version check cannot reject retail
  data (it is the engine's own `feq(mh->version, file->version)` conjunct with the
  mesh side measured).

### Still open

* **The accept test on the live path.** The four conjuncts also need
  `MeshHeader.version`, `MeshHeader.time[7]` and `LightmapMesh.texCount`, none of
  which the editor parses today (`source/lod/mef_mesh_header.h` carries
  format/center/sphere/complexity/radius; `mef_native.cpp` keeps only the LTMP
  chunk's size). Wiring `OlmHeaderAccepted` therefore needs those three words
  added to the MEF parser first. The editor's Calculate flow is a rebuild flow, so
  this is a reuse-decision refinement, not a correctness hole in what is drawn.
* **§5.4's staleness rule** is unchanged: the editor still decides reuse from a
  recorded bake pose, not from the engine's timestamp/count test.
* **`ObjectLightmapManager` (§5.2)** is still the non-oracle loader it was — and it
  still has **zero callers** (`GetLightmapTexture` is dead), so it is left alone
  rather than rewritten. The live path is `App::ResolveAndApplyLightmap` →
  `LoadOlmPages` → `SetLightmapForTask`.
* **The bake itself** is still `igi1conv lightmap recalc`, not a bake from placed
  lights. That is the decision §5's "next action" leaves to the Lead, and it is
  not made here.
* **No rendered pixel.** See §7.

---

## 6. Corpus evidence

`tests/test_lightmap_rules.cpp` walks every `lightmaps.res` under
`$IGI_GAME_PATH/MISSIONS/location0/level{1..14}/lightmaps/` through the editor's
own `RES_ForEachEntry` (`source/renderer/res_writer.h`) and validates every `.olm`
entry against §2 and §3. Verbatim output:

```
[lightmap-rules] archives=14 files=25337 pages=25351 multiPage=13 objects=121 refIndices=543 maxObj=120 maxRef=542 count1=25324 count2=12 count3=1 flags01=3111 flags10=22181 flags00=45 texelsBelowPixels=20196 texelsEqualPixels=5141
```

Read that as:

* **14** level bundles, **25,337** shipped `.olm` files, **25,351** pages.
* **Every** file has `magic == 0x3DF5C28F` and version bits exactly `0x3DCCCCCD`
  (0.1f) — the values `Lightmap_BuildLod` writes (`lightmapbuild.c:98-99`).
* **Every** file's name round-trips through `OlmFileName(ObjectLightmapName(n), ref)`
  and matches `OlmPath(LightmapDir(), name, ref)` — 25,337 of 25,337.
* **Every** file's size is exactly `0x3C + 0x2C * count + Σ 4·w·h`, so the header
  and page-record layout of §3 is exact, not approximate.
* **121** distinct object names (`obj00000` … `obj00120`), **543** distinct ref
  indices, max **542** — well inside the `%05d` field, so the minimum-width
  behaviour is not exercised by retail data.
* **13** files carry more than one page (12 with 2, one with 3). These are the
  files §5.1's reader truncates.
* The four-conjunct test against each file's **own** timestamp is false for all
  25,337 (the compare must be strictly `< 0`), and true for all 25,337 once the
  mesh timestamp is moved one millisecond earlier — the direction of the rule,
  measured on real data.
* `texels` is a lit count, not a pixel count, in every file (§3).
* The two header flags at 0x24/0x28 take the values `(0,1)` 3,111 times, `(1,0)`
  22,181 times and `(0,0)` **45** times. Both-zero rules them out as an accept
  predicate, which is consistent with the oracle not testing them
  (`lightmapbuild.c:63-64`). They are recorded here so nobody adds such a test
  later.

The entry names themselves are the other half of the evidence: all 25,337 are
`LOCAL:missions/location0/level<N>/lightmaps/obj<5d>_<5d>.olm`, e.g.
`obj00006_00052.olm` (level 4) and `obj00014_00000.olm` (level 1). The `obj%05d`
half is `LightmapInfo_MakeName` (`lightmap.c:240`) and the `_%05d` half is
`Lightmap_BuildLod` (`lightmapbuild.c:59`).

The corpus tests `GTEST_SKIP` when the install is absent — verified by running the
suite with `IGI_GAME_PATH=/nonexistent-igi` (§7, third command): `33 passed 0
failed 2 skipped`.

## 7. Verification

All commands were run from
`/Users/haseeb-mir/Documents/Code/project-igi-editor-igi-re`.

```
$ clang++ -std=c++20 -Wall -Wextra -Wpedantic -fsyntax-only -I source \
    -I third_party/glm-1.0.1 -I third_party/GL/include source/runtime/lightmap_rules.cpp
exit code: 0
```

```
$ IGI_HOST_NO_CACHE=1 IGI_HOST_OUT_DIR=build-mingw/ht-lm IGI_HOST_LOG_DIR=build-mingw/log-lm \
    IGI_HOST_TESTS=tests/test_lightmap_rules.cpp bash scripts/run-host-tests.sh
   PASS  test_lightmap_rules                  35 passed    0 failed  (35 tests, exit 0)
==> run-host-tests.sh exit=0
```

```
$ IGI_GAME_PATH=/nonexistent-igi IGI_HOST_OUT_DIR=build-mingw/ht-lm-nocorpus \
    IGI_HOST_LOG_DIR=build-mingw/log-lm IGI_HOST_TESTS=tests/test_lightmap_rules.cpp \
    bash scripts/run-host-tests.sh
   PASS  test_lightmap_rules                  33 passed    0 failed    2 skipped  (35 tests, exit 0)
==> run-host-tests.sh exit=0
```

After the §5b wiring (the two suites that cover this change):

```
$ IGI_HOST_TESTS="tests/test_lightmap_rules.cpp tests/test_lightmap_ref_index.cpp" \
    bash scripts/run-host-tests.sh
   PASS  test_lightmap_ref_index              12 passed    0 failed  (12 tests, exit 0)
   PASS  test_lightmap_rules                  38 passed    0 failed  (38 tests, exit 0)
OK: 50 tests passed in 2 suites
==> run-host-tests.sh exit=0
```

```
$ bash scripts/run-host-tests.sh                       # whole lane
OK: 1268 tests passed in 72 suites
==> run-host-tests.sh exit=0
```

```
$ bash scripts/build-windows.sh
   x64  link gate      : exit 0
   i686 compile gate   : exit 0
   build integrity     : exit 0
==> BUILD GATE OK (exit 0)
```

```
$ rm -f bin/igi_tests.exe bin/Release/igi1ed.exe && \
    cmake --build build-mingw --target igi_tests igi-editor --parallel
$ file bin/igi_tests.exe
bin/igi_tests.exe: PE32 executable (console) Intel 80386, for MS Windows
$ WINEPATH='Z:\...\scratch\wine-smoke\run' IGI_GAME_PATH='Z:\Users\haseeb-mir\Downloads\IGI' \
    wine bin/igi_tests.exe '--gtest_filter=-*VerifyLevel*' --gtest_color=no
[==========] 1707 tests from 245 test suites ran. (153880 ms total)
[  PASSED  ] 1695 tests.
[  SKIPPED ] 12 tests
==> exit 0
```

```
$ IGI_E2E_STAGE=$PWD/scratch/e2e-lm-pages bash scripts/e2e-headless.sh --fast
   PASS  tier smoke          # 14 levels; level 7 fails on exactly s332_02_1
   PASS  tier tests          # 278/278 assertions
==> E2E PASS (exit 0)
```

| Claim | Status |
|---|---|
| The three rules match IGI-RE | **verified** — citations throughout, quoted from the decompilation |
| The rules are exercised natively | **verified** — `35 passed 0 failed`, `exit 0` (see §5b for the run after the wiring: 38 + 12) |
| The corpus numbers | **verified** — printed by the test, 14 bundles / 25,337 files read |
| The corpus test self-skips without the install | **verified** — `33 passed 2 skipped`, `exit 0` |
| `-Wall -Wextra -Wpedantic` clean on the module | **verified** — `exit 0`, no diagnostics |
| The page plan matches the whole-buffer reader on every shipped file | **verified** — `plannedFiles=25337 plannedPages=25351 planMismatches=0` |
| At least one retail render group names a page other than 0 | **verified** — `offPage0=7` of 11,484 format-3 blocks, all in `428_02_1.mef` |
| The changed translation units cross-compile for i686 | **verified** — `tools/igi-re-parity/tu-check.py`, 6 units, 0 failed |
| The Windows `igi_tests` suite | **verified** — 1707 ran / 1695 passed / 12 skipped / **0 failed**, exit 0 (i686 `bin/igi_tests.exe`, `file` = `PE32 ... Intel 80386`, run from the repo root with the retail install) |
| The headless end-to-end lane | **verified** — `scripts/e2e-headless.sh --fast`: `E2E PASS`, smoke over 14 levels with the level-7 `s332_02_1` defect asserted and 0 missing textures |
| The build gate | **verified** — `scripts/build-windows.sh`: x64 link 0, i686 compile 0, build integrity 0 |
| Any rendered or in-game lightmap | **unverified** — the GUI needs GL 4.1, this host offers 2.1 |

## 8. Unverified

* **The `.olm` writer.** `Lightmap_BuildLod` only builds or loads. `gLightmapBuilt`
  is set at `lightmapbuild.c:95` (and cleared at `lightmap.c:85`) and is **never
  read** anywhere in IGI-RE, so the serialisation of a new bake to disk — and
  therefore the exact bytes `igi1conv lightmap recalc` writes — is outside the
  oracle's coverage. Expected: baking is a tool feature, not a retail code path
  (`docs/igi-re-parity/TRIAGE_REMAINING_ISSUES.md` row 72).
* **Which push-button is which.** `unk6F`/`unk70`/`unk71` are nameless ParamTable
  entries (`lightmapinfo.c:374-383`); only their *effects* are known (§4). The
  module names them by field, not by label.
* **`mesh->unk8`**, the second build gate (`lightmapbuild.c:56`), is unnamed in
  `LightmapMesh` (`litmaptypes.h:138-150`), so what it corresponds to in a `.mef`
  is not established.
* **`LOCAL:` vs `MISSION:`.** The engine builds `MISSION:lightmaps/...`
  (`lightmap.c:91`, `lightmapbuild.c:59`); the shipped archive stores
  `LOCAL:missions/location0/level<N>/lightmaps/...` (§6). Whether those two
  resource roots resolve to the same entry is a property of the resource system,
  not of these functions, and is not settled here.
* **The pixel channel order.** Format id 3 with pitch `width * 4`
  (`lightmapinfo.c:241`) is consistent with the editor's R↔B swap
  (`olm_texture.cpp`), but the oracle does not name the enum.
* **`fromResource` / `built` semantics.** Both are written to disk (§6) and the
  oracle sets one of the two on each `Lightmap_BuildLod` path (`:70-71` vs
  `:93-94`), but neither is part of the accept test, and 45 shipped files carry
  both as zero.
* **The GL upload itself.** `LoadOlmPages` is the only part of the change that
  cannot be executed on this host: `GL_RegisterTexture` needs a GL context. The
  plan it feeds the upload from is tested (`PlanOlmPages` over all 25,337 shipped
  files); the upload loop around it is not.
* **The texture's `uScale`/`vScale`.** The engine scales a group's UV2 by the
  page's `texture.uScale`/`vScale` (`Direct3DRender_UploadLightmapMesh`,
  `src/render/render.c:1117-1118`), which the D3D driver sets to
  `requested / power-of-two-padded` (`src/sysmisc/d3dtex.c:240-253`). Measured on
  the corpus: `uScale == 1` in every page, `vScale < 1` in 14,778 of 25,337 files,
  and in those files the page's `Texture.height` is the padded height while the
  record's own `LightmapTex.height` (what the pixel block is sized by) is the
  original. The editor uploads the *unpadded* page and does not scale UV2, which
  samples the same texels as the engine's padded surface plus the `vScale`
  multiply; that equivalence is arithmetic, not observed.

## 9. Files

| Path | What |
|---|---|
| `source/runtime/lightmap_rules.h` | the rules, constants and predicates, heavily cited; also the streaming page plan |
| `source/runtime/lightmap_rules.cpp` | the implementation; every branch carries its IGI-RE line |
| `source/renderer/olm_texture.{h,cpp}` | the reader: every page of a `.olm`, engine layout, magic/version checked |
| `source/renderer/model.{h,cpp}` | `SubMesh.lightmapPage` — the render group's `LmGroup.lightmap` |
| `source/renderer/lightmap_ref_index.h` | `PageIndexForGroup` (rule 5) |
| `source/renderer/renderer_objects.{h,cpp}`, `renderer.h` | per-LOD page vectors and the per-submesh bind |
| `source/app_editor.cpp` | `ResolveAndApplyLightmap` loads every page |
| `tests/test_lightmap_rules.cpp` | 34 unit + 2 corpus tests (page-table plan, pixel offsets) |
| `tests/test_lightmap_ref_index.cpp` | the page rule plus the format-3 page corpus differential |
| `CMakeLists.txt` | purely additive: one test file and one runtime source |
| `docs/igi-re-parity/LIGHTMAP_BAKE.md` | this record |
