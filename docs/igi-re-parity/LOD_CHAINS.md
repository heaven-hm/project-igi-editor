# LOD virtual-model chains (`0x4CED50`) — issue #62

Oracle: `/Users/haseeb-mir/Documents/Code/IGI-RE`, the faithful C decompilation of retail
`IGI.exe`. Every function there carries `HOOK(0xADDR, Name)`; every claim below cites
`file:line`. Retail corpus: `/Users/haseeb-mir/Downloads/IGI` (14 levels, 1578 `.mef`).

**Anchor.** `0x4CED50` resolves through `symbols/names.tsv:3747` →
`004ced50	VirModel_InitLods	manual	entry`, i.e. `src/model/model.c:163`
`HOOK(0x4CED50, VirModel_InitLods)`. The function body is `src/model/model.c:145-163`.

**Verdict, in one line.** The engine's rule is a character-arithmetic name increment with a
first-miss terminator and a 5-level cap; the editor implements **no chain at all** (one
`<name>.mef` per object), so there is nothing to correct in existing resolution code — the
rule is now landed as a tested primitive (`source/lod/lod_chain.h`) and the wiring into the
loader, the lightmap page indexing and the viewport is the remaining feature work, named in
§6. Measured: **60.3 % of the 6605 retail model names have a chain longer than one level**,
so the missing chain is not an edge case.

---

## 1. The rule, traced

`src/model/model.c:145-163`:

```c
void VirModel_InitLods(VirModel *m)
{
    char name[32];
    int i, n = 0;

    m->unk24 = 0;
    strcpy(name, m->name);
    crt_strstr(name, "johan");
    for (i = 1; i <= VIRMODEL_LODS; i++) {
        m->lod_dist[i - 1] = (uint16_t)(100 / (i * i));
        m->lods[i - 1] = ModelLod_Find(name);
        if (!m->lods[i - 1])
            break;
        name[strlen(name) - 1]++;
        n++;
    }
    m->lod_count = (uint16_t)n;
}
HOOK(0x4CED50, VirModel_InitLods);
```

| # | Rule | Oracle |
| --- | --- | --- |
| 1 | LOD 1 is the requested name; LOD *i*+1 is the previous name with its **last character incremented by one** — character arithmetic, not a numeric parse | `src/model/model.c:160` (`name[strlen(name) - 1]++`) |
| 2 | The chain **stops at the first name that does not resolve**; a missing variant is never skipped over | `src/model/model.c:157-158` |
| 3 | At most **5** levels | `src/model/model.c:155`, `VIRMODEL_LODS 5` at `src/model/model.h:11` |
| 4 | Per-level distance is `100 / (i * i)` in integer arithmetic → 100, 25, 11, 6, 4 — **written but never read**; the drawn LOD comes from the authored table or complexity ratios (§2.1) | `src/model/model.c:154`, `src/model/modeltypes.h:57` |
| 5 | `lod_count` counts only the levels that resolved; the distance of the failing level is written but never consumed | `src/model/model.c:152-162` |
| 6 | Resolution is an exact `strcmp` against `ModelLod.name` over every LOD of every **loaded** ModelFile | `src/model/model.c:165-178`, `HOOK(0x4CEE30, ModelLod_Find)` |
| 7 | Those names come from the file's MTP **VNAM** string table, one per INST record, so a chain links sibling entries of a loaded bundle | `ModelFile_Setup`, `src/model/model.c:274-289` |
| 8 | A name that resolves to nothing gives `lod_count == 0`, which `VirModel_Get` reports as "not found"; `VirModel_Find` then falls back to the model literally named `"missing"` | `src/model/model.c:118-139`, `:107-116` |
| 9 | The chain is built **from whatever name was asked for**, so asking for `_3` covers `_3`..`_5` and never reaches back to `_1` | `src/model/model.c:146-152` |

Note on the terminator: the engine increments the name **after** each success, including
after the last one, so the final increment is written but never looked up. A faithful port
may skip it (as `source/lod/lod_chain.h` does) — unobservable.

Note on `crt_strstr(name, "johan")` (`:150`): a string search whose result is discarded; it
has no effect on the chain. Recorded so nobody re-derives it as meaningful.

**Not part of this rule: `0x2000`.** The `0x2000`-and-up convention in `ModelFile_Setup` is a
*cross-bundle reference* scheme, not an LOD rule: an index ≥ `0x2000` in an `MTP_INST` record
selects a mesh, texture or texinfo from the **first** loaded model file instead of the current
one (`src/model/model.c:250` comment, `:281-282` mesh, `:302-303` texture, `:308-309`
texinfo). It never participates in `VirModel_InitLods` or in `ModelLod_Find`.

## 2. Why the chain matters: it is the LOD list itself

`VirModel` (`src/model/modeltypes.h:51-58`) is `ModelLod *lods[5]`, `char name[16]`,
`uint16_t lod_count`, `uint16_t lod_dist[5]`. Everything that needs "all detail levels of
this object" reads that array:

* distance-based detail selection and the complexity warnings — `src/lod/mesh3d.c:160-208`
  (`lod_header(vm->lods[i])->complexity` vs `lods[0]`);
* per-object lightmap refs — §4;
* sector counting — `src/sector/sector.c:513,532,549-559,658` (`*count += lod_count`).

### 2.1 Where the LOD switch distances actually come from

Rule 4's `lod_dist[5] = 100/(i*i)` is **not** what the switch uses. `grep -rn "lod_dist" src/`
matches only the write at `model.c:154` and the declaration at `modeltypes.h:57` — no reader.
`lod_select` (`src/lod/mesh3d.c:160-208`) has two branches:

| Branch | Condition | Source of distances |
| --- | --- | --- |
| Authored table | `VirModel.unk24 != 0` | `VirModel.unk28[5]`, filled by `ModelLODSettings_Changed`; `dists[4]` is the cull distance (`d > dists[4]` → `-1`) |
| Complexity ratios | `unk24 == 0` | `lod_header(lods[i])->complexity / lod_header(lods[0])->complexity`, squared, against the level-wide `LODSettings` `detail`/`blend`/`range`/`cull` values |

The authored path is traced end to end:

* `ModelLODSettings` is a task type `{ char name[16]; float dist[5]; }` whose six params are
  the model name plus five distances (`gModelLODSettingsParams`,
  `src/animtrig/lodsettings.c:95,104-109`); the level-wide `LODSettings` type carries
  `splineDetail/rigidDetail/boneDetail/splineCull/rigidCull/boneCull/blend/range`
  (`:19-28`, `:96-103`).
* `ModelLODSettings_Changed` (`HOOK(0x4EDB60)`) first **clamps the table to be
  non-decreasing** (`if (i && s->dist[i] < s->dist[i-1]) s->dist[i] = s->dist[i-1];`), then
  `VirModel_Find(s->name)` and, if the model exists, writes `model+0x24 = 1` and
  `model+0x28[i] = s->dist[i] * 4096` — i.e. the raw-unit distance table in `unk28`.
* These tasks are read from the level's `lod.qsc` (file header comment,
  `src/animtrig/lodsettings.c:1-29`); the compiled `lod.qvm` is the same data.
* A change to the container re-broadcasts `Param_GetChangedEvent`
  (`Lod_SendChanged`, `src/physobj/physobjtype.c:47-53`), so the table can be re-applied.

The editor already parses this data: `source/renderer/renderer_objects_metadata.cpp:13-57`
loads `lod.qvm`, keeps lines containing `"ModelLODSettings"`, and reads the five distances
(`tokens[4..7]` = LOD 1..4, `tokens[8]` = "Distance to cutoff") — the same 6-parameter
layout, in the same order, as `gModelLODSettingsParams`. So the *distance* half of the
issue's requirement 3 has a correct data source today; what the editor lacks is the chain
itself, hence any per-LOD geometry to switch between.

## 3. Measured corpus result

Measured over all 14 retail level bundles, using each bundle's **VNAM** name set as the
resolver (rule 6/7 above). Tool: a standalone MTP reader following the chunk layout
(`FORM` + BE size + `MTP `, then `fourcc` + BE size; `VNAM` = `u32 count`, `count` u32
offsets, packed names — matching `src/model/model.c:274-275`).

| Level | VNAM names | chain > 1 | hist 1:2:3:4:5 |
| --- | --- | --- | --- |
| 1 | 415 | 246 | 169 : 100 : 85 : 46 : 15 |
| 2 | 259 | 169 | 90 : 63 : 56 : 33 : 17 |
| 3 | 521 | 296 | 225 : 130 : 99 : 46 : 21 |
| 4 | 300 | 174 | 126 : 70 : 55 : 33 : 16 |
| 5 | 366 | 219 | 147 : 93 : 73 : 39 : 14 |
| 6 | 556 | 357 | 199 : 142 : 119 : 64 : 32 |
| 7 | 619 | 396 | 223 : 152 : 134 : 72 : 38 |
| 8 | 671 | 427 | 244 : 171 : 142 : 78 : 36 |
| 9 | 437 | 281 | 156 : 111 : 94 : 55 : 21 |
| 10 | 454 | 269 | 185 : 103 : 93 : 50 : 23 |
| 11 | 611 | 373 | 238 : 151 : 122 : 69 : 31 |
| 12 | 505 | 283 | 222 : 117 : 92 : 51 : 23 |
| 13 | 488 | 280 | 208 : 113 : 92 : 52 : 23 |
| 14 | 403 | 211 | 192 : 82 : 70 : 40 : 19 |
| **total** | **6605** | **3981 (60.3 %)** | **2624 : 1598 : 1326 : 728 : 329** |

Three further measured facts:

* **VNAM == MODS in all 14 bundles.** `ModelLod.name` comes from VNAM (`model.c:274-289`)
  while the editor's DAT view reads MODS; the two name sets are identical in every retail
  level, so there is no naming divergence to reconcile.
* **Zero gap-terminated chains.** In 6605 names there is not one case where a variant exists
  *beyond* a missing one (a gap that the engine's terminator hides but a "collect every
  existing suffix" implementation would find). Rule 2 is therefore a latent difference on
  retail data — it matters for arbitrary or modded names, not for the shipped corpus.
* **18 non-digit endings**, where the increment is character arithmetic rather than a numeric
  suffix. All 18 terminate at length 1 in retail, so the *mechanism* differs from a
  numeric parse while the *observed chain length* does not:

  | name | increment attempted | occurrences |
  | --- | --- | --- |
  | `waypoint` | `waypoinu` | 8 levels (1, 3, 7, 9, 10, 12, 13, 14) |
  | `dummy` | `dummz` | 5 levels (2, 3, 4, 5, 6) |
  | `joint_fixer` | `joint_fixes` | 3 levels (12, 13, 14) |
  | `colbox` | `colboy` | 1 level (10) |
  | `cylinder` | `cylindes` | 1 level (9) |

Concrete chains, printed by the same reader (distances from rule 4):

```
LEVEL1  435_01_1 -> 435_01_1(100) 435_01_2(25) 435_01_3(11) 435_01_4(6) 435_01_5(4)   lod_count=5
LEVEL1  210_01_1 -> 210_01_1(100) 210_01_2(25) 210_01_3(11)  [210_01_4 absent]         lod_count=3
LEVEL7  001_02_1 -> 001_02_1(100) 001_02_2(25) 001_02_3(11) 001_02_4(6) 001_02_5(4)   lod_count=5
LEVEL7  waypoint -> waypoint(100)  [waypoinu absent]                                 lod_count=1
LEVEL7  018_03_1 -> [not in this bundle]                                             lod_count=0
```

The first line is the issue's own example (`435_01_1` pulls in `435_01_2 … 435_01_5`),
confirmed exactly.

## 4. Per-LOD lightmap counts

The chain length decides how many lightmap entries a lightmapped object contributes. Traced:

| Step | Oracle |
| --- | --- |
| One `LightmapRef` per LOD: `fill_refs` loops `i < VirModel.lod_count` and sets `r->lodIndex = i`, `r->lodCount = lod_count`, `r->lod = lods[i]` | `src/sector/sector.c:546-562` |
| The ref count is pre-computed as `lod_count` per lightmapped object | `src/sector/sector.c:508-521` (`SectorObject_CountMagicLods`), `:523-542` (`SectorObject_CountRigidLods`) |
| One `.olm` per ref: path is `"%s/%s_%05d.olm", Lightmap_GetDir(), info->name, refIndex` | `src/dirlight/lightmapbuild.c:59` |
| Pages per ref = that LOD mesh's `texCount` | `src/dirlight/lightmapbuild.c:85` (`lm->count = mesh->texCount`) |
| No entry at all when the LOD's mesh is not format 3 | `src/dirlight/lightmapbuild.c:45-46` |
| `.olm` on disk **is** the `Lightmaps` struct (header 0x3C, `magic`@0, `count`@0x2C) | `src/litmap/litmaptypes.h:34-35`, `_Static_assert(sizeof(Lightmaps) == 0x3C)` |
| `LightmapInfo.name` is an opaque per-set slot id `obj%05d`, allocated as the first index not already used by a file in the lightmap folder or another `LightmapInfo` — **not** a model name | `src/litmap/lightmap.c:231-252` (`LightmapInfo_MakeName`, `HOOK(0x5235A0)`) |
| Archive path is `MISSION:lightmaps/lightmaps.res` | `src/litmap/lightmap.c:96-100` |

Measured on the retail `lightmaps.res` of all 14 levels:

| Level | `.olm` entries | sets | refs | contiguous sets | sets with holes |
| --- | --- | --- | --- | --- | --- |
| 1 | 1308 | 22 | 1308 | 22 | 0 |
| 2 | 643 | 17 | 643 | 17 | 0 |
| 3 | 2538 | 31 | 2538 | 31 | 0 |
| 4 | 1143 | 15 | 1143 | 15 | 0 |
| 5 | 1587 | 14 | 1587 | 14 | 0 |
| 6 | 1766 | 18 | 1766 | 18 | 0 |
| 7 | 864 | 28 | 864 | 28 | 0 |
| 8 | 2077 | 24 | 2077 | 24 | 0 |
| 9 | 868 | 26 | 868 | 26 | 0 |
| 10 | 968 | 31 | 968 | 31 | 0 |
| 11 | 1858 | 36 | 1858 | 36 | 0 |
| 12 | 3067 | 38 | 3067 | 28 | 10 |
| 13 | 2518 | 60 | 2518 | 42 | 18 |
| 14 | 4132 | 121 | 4132 | 12 | 109 |

* Every entry is a well-formed `Lightmaps` struct: for LEVEL7, **864/864** have
  `magic == 0x3DF5C28F`, 0 malformed.
* **863 of LEVEL7's 864 entries carry exactly 1 page** (`count` at 0x2C), one carries 3. So
  in practice one `.olm` ≈ one lightmap page per LOD, which is the issue's "one lightmap per
  LOD level" — but the general rule is `mesh->texCount`, not the constant 1.
* LEVEL7's set-size histogram: `5:10, 9:4, 11:3, 13:1, 31:1, 35:1, 43:1, 52:1, 57:1, 61:1,
  62:1, 90:1, 95:1, 206:1` (28 sets). Ten sets of exactly 5 are consistent with a single
  lightmapped object on a 5-level chain — LEVEL7 has 38 names with chain length 5 — and the
  ref indices inside every set are contiguous `0..k-1`.
* Sets with holes appear only in levels 12-14. The mechanism for a hole is cited
  (`lightmapbuild.c:45-46`: a ref whose LOD mesh is not format 3 writes no file) but mapping
  a specific `objNNNNN` set back to a specific object is not possible from the shipped data,
  so **"holes == non-format-3 LOD refs" is inferred, not independently proven** (§7).

## 5. What the editor does today

| Behaviour | Editor location | Consequence |
| --- | --- | --- |
| Loads exactly one `<modelId>.mef` per object; no chain, no `lod_count`, no distance switch | `Renderer_Objects::FindMeshData`, `source/renderer/renderer_objects.cpp:484-488` | a `_2.._5` variant is only ever drawn if something names it explicitly |
| No chain: no `VirModel`/`lod_count` equivalent, no `lods[]`, no per-LOD geometry, no distance switch | `grep -rniE "\blod\b\|lods\|lod_count\|detail level"` over `source/renderer/` and `source/level/level_objects*.cpp` matches only terrain LOD (`source/level/terrain_lod.cpp`), the texture-table fallback below, the ATTA family notes, and the `lod.qvm` metadata reader | the "chain length" input the lightmap indexing needs is simply absent |
| **Does** parse the authored LOD distances from `lod.qvm` (`ModelLODSettings` tasks, 5 distances per model) | `source/renderer/renderer_objects_metadata.cpp:13-57` | matches the engine's `gModelLODSettingsParams` layout (§2.1) — a verified positive, and the correct data source for requirement 3 |
| Nearest existing *resolution* logic is a **texture-table** fallback `NNN_VV_N -> NNN_VV_1` (e.g. `001_02_2 -> 001_02_1`), i.e. borrow the primary's texture list | `source/renderer/renderer_objects_texture.cpp:155-177` | a different rule, opposite direction to the engine's chain, and it does not create LOD levels |
| Lightmap manager discovers the real retail `.olm` names and keys them by bare name | `ObjectLightmapManager::LoadForLevel`, `source/renderer/object_lightmap.cpp:76-118` | discovery is correct |
| `GetLightmapTexture(object_name, draw_record_index)` formats `"%s_%05d.olm"` | `source/renderer/object_lightmap.cpp:125-137` | **no callers** — nothing binds an object to a lightmap page yet, and the index it would need is the per-set ref index, which counts `lod_count` refs per object |
| `.olm` header parsing offsets (`count`@44, `pixel_start = 60 + count*44`, w@+40/h@+42) | `source/renderer/object_lightmap.cpp:79-99` | **matches IGI-RE's ABI** (`sizeof(Lightmaps)==0x3C`, `sizeof(LightmapTex)==0x2C`, `width`/`height` at +40/+42) — a verified positive |

Editor-side data check: `LEVEL7/MODELS/LEVEL7.RES` holds **619 `.mef` entries for 619 bundle
names**, one per name, and the last-digit distribution (1:222, 2:152, 3:134, 4:72, 5:38)
matches the chain histogram of §3 for LEVEL7 (223/152/134/72/38) — the single difference is
`waypoint`, which is chain-1 but does not end in a digit. So every chain member is already
present in the editor's own archives; only the walk is missing.

## 6. What landed, and what remains

**Landed** (task-10, minimal and additive):

* `source/lod/lod_chain.h` — the rule of §1 as a dependency-free, header-only primitive:
  `kVirModelLods`, `LodDistance(level)`, `IncrementLodName(name)` (character arithmetic,
  empty-name safe) and `BuildLodChain(modelName, exists)` returning `count`, `names[]`,
  `distance[]`. The resolver is an injected predicate, i.e. the equivalent of
  `ModelLod_Find` returning non-NULL; no file I/O, no globals. Every step carries its
  `src/model/model.c` line.
* `tests/test_lod_chain.cpp` — 12 tests, registered at `CMakeLists.txt:313`. Eleven are
  pure-logic (increment on digit and non-digit endings, empty name, numeric suffixes never
  invented, distances, the 5-level cap, first-miss terminator, gaps not skipped, absent own
  name, chain started at a variant, the issue's `435_01_1` example). The twelfth parses all
  14 retail `LEVEL*.MTP` VNAM tables and asserts the §3 numbers, so the corpus measurement is
  a re-runnable test rather than a claim in a document.

**Not landed, and why** (the feature port of issue #62; each item names its blocking file):

1. *Chain in the model loader* (issue requirement 1) — the loader is
   `Renderer_Objects::FindMeshData` / the `GetModelData` path in
   `source/renderer/renderer_objects.cpp` and `renderer_objects_mesh.cpp`, which the Lead owns
   and has just edited (removal of `current_draw_obj_type_`). Building a chain also means
   deciding whether lower LODs are lazily extracted from the level archive; that is a
   design decision for the owner of those files, not a drive-by edit.
2. *Lightmap page indexing* (requirement 2) — needs the per-set ref index, which requires the
   chain (item 1) plus a mapping from a level object to its `objNNNNN` `LightmapInfo` slot.
   The slot allocation rule (`lightmap.c:231-252`) is a first-free-index search over the
   lightmap folder *and* the live `LightmapInfo` pool, so reproducing it needs the level's
   object iteration order, not just the archive.
3. *Viewport LOD override / distance switch* (requirement 3) — UI + render-state work in the
   Lead's files. The distance *data* is already parsed (`renderer_objects_metadata.cpp:13-57`),
   so this item is the override control and the per-LOD draw selection, not the metadata.
4. Requirement 4 (document the rule in code comments citing `0x4CED50`) is satisfied by
   `source/lod/lod_chain.h` and this document.

This is the negative-result half of the task, stated plainly: **for the resolution rule there
is no existing editor behaviour to correct**, because the editor never implemented it. The
rule is now available, cited and tested; wiring it in is a feature, and its blocking files
are outside this task's write scope.

## 7. Verified / unverified

| Claim | Status | Evidence |
| --- | --- | --- |
| `0x4CED50` is `VirModel_InitLods` | **Verified** | `symbols/names.tsv:3747`, `src/model/model.c:163` |
| Increment-last-character, first-miss terminator, cap 5, distances `100/(i*i)` | **Verified** | `src/model/model.c:145-163`, `:154-160`; `src/model/model.h:11` |
| Resolution is exact-name against VNAM-derived `ModelLod.name` | **Verified** | `src/model/model.c:165-178`, `:274-289` |
| Chains exist at scale in retail | **Verified** | 6605 names / 3981 chains > 1 across 14 bundles (§3); asserted by the corpus test |
| VNAM == MODS in all 14 bundles | **Verified** | corpus test (`mismatchedMods` empty) |
| No gap-hidden variants in retail | **Verified** | corpus test asserts `gaps == 0` |
| The issue's `435_01_1` example is a 5-level chain | **Verified** | printed chain, §3 |
| One `.olm` per (object, LOD) ref; pages = `mesh->texCount` | **Verified (oracle)** | `src/sector/sector.c:546-562`, `src/dirlight/lightmapbuild.c:59,85` |
| Retail `.olm` files are `Lightmaps` structs | **Verified** | 864/864 LEVEL7 entries carry `magic 0x3DF5C28F`, 0 malformed |
| Retail `.olm` pages are 1 per LOD in practice | **Verified** | LEVEL7 `count` histogram 1:863, 3:1 |
| `LightmapInfo.name` is an opaque `obj%05d` slot, not a model name | **Verified** | `src/litmap/lightmap.c:231-252` + the observed retail names |
| `lod_dist[]` (`100/(i*i)`) is dead data; the switch uses authored distances or complexity ratios | **Verified** | `grep -rn "lod_dist" src/` matches only `model.c:154` + `modeltypes.h:57`; `src/lod/mesh3d.c:160-208` |
| The authored per-model distances come from `lod.qsc` `ModelLODSettings` and land in `VirModel.unk24/unk28` (×4096, non-decreasing clamped) | **Verified** | `src/animtrig/lodsettings.c:1-29,37-40,95,104-109` + `ModelLODSettings_Changed` `HOOK(0x4EDB60)` |
| The editor already reads those distances from `lod.qvm` | **Verified** | `source/renderer/renderer_objects_metadata.cpp:13-57` |
| The editor implements no LOD chain | **Verified** | `renderer_objects.cpp:484-488`; no `lod_count`/`lods[]` equivalent in `source/` |
| The editor's `.olm` parse matches IGI-RE's ABI | **Verified** | `object_lightmap.cpp:79-99` vs `sizeof(Lightmaps)==0x3C`, `sizeof(LightmapTex)==0x2C` |
| Holes in the LEVEL12-14 ref index spaces are non-format-3 LOD refs | **Inferred, not proven** | mechanism cited at `lightmapbuild.c:45-46`; `objNNNNN` sets cannot be mapped back to objects from the shipped data |
| A per-object prediction of a level's `.olm` count from its `objects.qsc` | **Not attempted** | needs the level object list + the `LightmapInfo` allocation order |
| The chain wired into the renderer / viewport | **Not done** | §6 items 1-3 |
| `source/lod/lod_chain.h` and `tests/test_lod_chain.cpp` compile with the project's cross-compiler flags | **Verified** | `tu-check.py` over the changed TUs: `checked 6 translation unit(s), 0 failed`; the new TU also replayed by hand with the DB's exact `i686-w64-mingw32-g++` command → exit 0 |
| The new TU links into the x64 `igi_tests` aggregate | **Pending build-gate** | header-only and dependency-free, so no link impact expected |

## 8. Reproduction

```bash
# Pure logic + corpus assertion (corpus runs via the runner's IGI_GAME_PATH)
IGI_HOST_NO_CACHE=1 IGI_HOST_TESTS="tests/test_lod_chain.cpp" bash scripts/run-host-tests.sh

# Cross-compiler syntax check (needs a configure to have registered the file first)
python3 tools/igi-re-parity/tu-check.py tests/test_lod_chain.cpp
```

The §3 and §4 tables come from scratch tools (deliberately not committed: they duplicate the
committed corpus test or read live game data):

* an MTP VNAM reader that walks `FORM` + BE size + `MTP `, then `fourcc` + BE size chunks,
  decoding `VNAM` as `u32 count`, `count` u32 offsets, packed names;
* `RES_BuildIndex` + `RES_ReadEntry` over each level's `lightmaps/lightmaps.res`, grouping
  `objNNNNN_%05d.olm` entries by prefix and reading `count` at offset 0x2C.
