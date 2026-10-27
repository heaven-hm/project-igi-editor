# LOD virtual-model chains (`0x4CED50`) — issue #62

Oracle: `/Users/haseeb-mir/Documents/Code/IGI-RE`, the faithful C decompilation of retail
`IGI.exe`. Every function there carries `HOOK(0xADDR, Name)`; every claim below cites
`file:line`. Retail corpus: `/Users/haseeb-mir/Downloads/IGI` (14 levels, 1578 `.mef`).

**Anchor.** `0x4CED50` resolves through `symbols/names.tsv:3747` →
`004ced50	VirModel_InitLods	manual	entry`, i.e. `src/model/model.c:163`
`HOOK(0x4CED50, VirModel_InitLods)`. The function body is `src/model/model.c:145-163`.

**Verdict, in one line.** The engine's rule is a character-arithmetic name increment with a
first-miss terminator and a 5-level cap; the editor now resolves that chain in its mesh lookup
path under the engine's exact-name semantics and exposes it (`source/lod/lod_chain.h`,
`Renderer_Objects::GetLodChain`), but still **draws one `<name>.mef` per object** — the
per-frame distance switch is the remaining feature item, named in §6. Measured: **60.3 % of the
6605 retail model names have a chain longer than one level**, so the chain is not an edge case.

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

**Editor applicability:** none of the ref bookkeeping above is reachable in the editor
today. §5.1 gives the reason and the evidence for why the per-LOD ref count has no
consumer in this architecture.

## 5. What the editor does now

The chain half of #62 is wired: `Renderer_Objects` resolves a model's chain under
the engine's rule and exposes it to the draw path. The mesh that is *drawn* is
still the object's own name, which is the one remaining feature item (§6 item 1).

| Behaviour | Editor location | Notes |
| --- | --- | --- |
| Resolves the LOD chain for a model it loads: exact-name existence, last-character increment, first-miss terminator, 5-level cap | `Renderer_Objects::GetLodChain`, `MeshNameResolves` — `source/renderer/renderer_objects_mesh.cpp:16-92`; rule in `source/lod/lod_chain.h` | called from `GetOrLoadMesh` (`renderer_objects_mesh.cpp:110-115`) so every model the editor loads carries its chain; cached per level and dropped whenever the loaded `.res` set changes (`renderer_objects.cpp:367-379`, `:435`) |
| Existence is an EXACT `<name>.mef` key — the indexed level/common `.res` archives first, then an exact on-disk `<name>.mef` | `MeshNameResolves`, `renderer_objects_mesh.cpp:16-49` | `FindModelFile`'s fuzzy pass is deliberately not used: it would resolve `435_01_6` to `435_01_1` and invent a level the engine's `strcmp` never sees |
| Exposes the chain (`count`, `names[]`, `distance[]`) and the authored-branch LOD selection | `GetLodChain`, `GetLodMeshName`, `GetAuthoredLodDistances`, `SelectLodForDistance` — `renderer_objects_mesh.cpp:51-92`, declared `renderer_objects.h:169-213` | `SelectLodForDistance` delegates to `igi::lod::SelectAuthoredLod` and returns -1 when the model has no chain or no authored entry (the complexity branch is deliberately not ported: the editor always has `lod.qvm`) |
| Parses the authored `ModelLODSettings` tables out of `lod.qvm` and keeps the engine's copy (non-decreasing clamp, then x4096) | `EnsurePortalDistancesLoaded`, `source/renderer/renderer_objects_metadata.cpp:9-56`; tokenizer in `source/lod/lod_qvm_table.h` | same 6-parameter layout as `gModelLODSettingsParams`; the tokenizer is now a single host-tested implementation instead of an inline loop |
| Still loads and draws exactly one `<modelId>.mef` per object | `GetOrLoadMesh`, `renderer_objects_mesh.cpp:110-239` | the per-frame distance switch in `Draw` is not enabled — §6 item 1 |
| Lightmap binding is **task-keyed**, not the engine's per-set ref index | `App::ResolveAndApplyLightmap` `source/app_editor.cpp:341-399` -> `SetLightmapForTask`; the draw reads `GetLightmapForTask` (`renderer_objects.cpp:990`) and binds `(*lightmaps)[si]` per submesh (`:1096-1102`) | resolves `.olm` paths through `igi1conv lightmap resolve` keyed by model + task id (or authored position), `source/utils_igi1conv.cpp:177-190` |
| `ObjectLightmapManager::GetLightmapTexture` still has **zero callers** | `source/renderer/object_lightmap.cpp:125-137` | genuinely dead in this architecture — see §6 item 2, not merely unwired |

### 5.1 Why the per-LOD lightmap ref count is not needed here

The engine's `lod_count` reaches the lightmap system through
`SectorObject_CountMagicLods` / `SectorObject_CountRigidLods` / `fill_refs`
(`src/sector/sector.c:508-563`), which reserve one `LightmapRef` per LOD, and
those refs are consumed by the **bake** renderer `Hemicube_DrawLightmapSet`
(`src/litmap/hemirender.c:78-86` — it picks, per ref, the mesh at the requested
LOD, with "the coarsest LOD stands in for higher ones") and by
`Lightmap_BuildLod`'s `<name>_<refIndex>.olm` path (`src/dirlight/lightmapbuild.c:59`).
Both belong to a `LightmapSet` built by `SectorObject_LoadLightmaps`
(`src/sector/sector.c:641-680`, `HOOK(0x4DC3D0)`) walking the engine's task tree.

The editor has no `LightmapSet`, no hemicube bake renderer and no per-object ref
cursor. Its bakes come from `igi1conv`, and the per-object `.olm` group is already
named by that binding, so the ref index adds nothing. Reproducing it would require
(a) the engine's sector-object walk order (`SectorObject_FillMagicRefs`,
`sector.c:565-575`) over a task tree the editor's flat `LevelObject` list does not
model, and (b) `LightmapInfo_MakeName`'s first-free `obj%05d` slot allocation over
the lightmap folder *and* the live `LightmapInfo` pool
(`src/litmap/lightmap.c:231-252`, `HOOK(0x5235A0)`). `source/renderer/lightmap_ref_index.h`
and `tests/test_lightmap_ref_index.cpp` therefore stay a documented, tested rule
with no consumer until a per-frame LOD switch exists to need them (§6 item 1).
This is an editor-architecture determination, not an IGI-RE claim.

## 6. What landed, and what remains

**Landed this pass** (issue requirement 1 — the chain in the model loader):

1. `Renderer_Objects::MeshNameResolves` — exact-name existence over the editor's
   loaded model set (indexed `.res` archives + exact on-disk `.mef`), the
   equivalent of `ModelLod_Find`'s `strcmp` (`src/model/model.c:165-178`).
2. `Renderer_Objects::GetLodChain` / `GetLodMeshName` — the chain itself
   (`BuildLodChain`, `source/lod/lod_chain.h`), cached per level and consulted by
   `GetOrLoadMesh`, i.e. by the mesh lookup path.
3. `Renderer_Objects::GetAuthoredLodDistances` / `SelectLodForDistance` — the
   authored table in world units and `lod_select`'s authored branch, so a draw
   path can pick a mesh name without inventing a rule.
4. `source/lod/lod_qvm_table.h` — the `lod.qvm` `ModelLODSettings` tokenizer as a
   single host-testable primitive; `EnsurePortalDistancesLoaded` now also keeps
   the engine's clamped, x4096 table (`VirModel.unk28`) instead of only the
   culling cutoff.
5. Two corpus tests in `tests/test_lod_chain.cpp` (§7) — the archive name set
   resolves the engine's VNAM chain exactly, and the retail authored tables select
   inside their chains.

**Not landed, and why:**

1. *The per-frame LOD switch in `Draw`* (requirement 3, the override/selection
   control). The rule and the data are now exposed, but turning the drawn mesh
   into a lower LOD changes the submesh set, and the editor's lightmap binding is
   a per-task texture vector indexed by submesh of the **LOD 0** mesh
   (`source/renderer/model.cpp:241-250`: one submesh per render block "so
   `renderer_objects.cpp`'s `lightmaps[si]` lookup lines up exactly with
   `obj00000_000NN.olm`"). Drawing LOD *n* without its own bake would bind the
   wrong atlas page or none. Deciding that is a design step in files this pass
   does not own, and it is what would give `lightmap_ref_index.h` a consumer.
2. *Per-LOD lightmap ref counts* (requirement 2) — non-applicable to this
   architecture: §5.1. `ObjectLightmapManager::GetLightmapTexture`
   (`object_lightmap.cpp:125-137`) is genuinely dead and is **not** force-wired;
   the editor binds retail lightmaps by task key through `igi1conv`, and the
   ref index cannot be computed without the engine's sector-object walk and
   `LightmapInfo` slot allocation.
3. Requirement 4 (document the rule in code comments citing `0x4CED50`) is
   satisfied by `source/lod/lod_chain.h`, `source/lod/lod_settings.h`,
   `source/lod/lod_qvm_table.h` and this document.

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
| The refs are consumed by the hemicube **bake** renderer, per requested LOD | **Verified (oracle)** | `src/litmap/hemirender.c:78-86`; `LightmapSet` built at `src/sector/sector.c:641-680` |
| Retail `.olm` files are `Lightmaps` structs | **Verified** | 864/864 LEVEL7 entries carry `magic 0x3DF5C28F`, 0 malformed |
| Retail `.olm` pages are 1 per LOD in practice | **Verified** | LEVEL7 `count` histogram 1:863, 3:1 |
| `LightmapInfo.name` is an opaque `obj%05d` slot, not a model name | **Verified** | `src/litmap/lightmap.c:231-252` + the observed retail names |
| `lod_dist[]` (`100/(i*i)`) is dead data; the switch uses authored distances or complexity ratios | **Verified** | `grep -rn "lod_dist" src/` matches only `model.c:154` + `modeltypes.h:57`; `src/lod/mesh3d.c:160-208` |
| The authored per-model distances come from `lod.qsc` `ModelLODSettings` and land in `VirModel.unk24/unk28` (x4096, non-decreasing clamped) | **Verified** | `src/animtrig/lodsettings.c:1-29,37-40,95,104-109` + `ModelLODSettings_Changed` `HOOK(0x4EDB60)` |
| The editor reads those distances from `lod.qvm` and now keeps the clamped, x4096 table | **Verified** | `renderer_objects_metadata.cpp:9-54`, `source/lod/lod_qvm_table.h` |
| **The editor resolves the LOD chain for a model it loads, with exact-name existence** | **Verified (host)** | `renderer_objects_mesh.cpp:16-92`; corpus test `ThePackedModelArchiveResolvesTheEngineVnamChainExactly` |
| The packed model archive's `.mef` stem set equals the engine's VNAM set in all 14 levels, and every name's chain is identical under both | **Verified (host, measured)** | `[lod-res] levels=14 resMefNames=6605 vnamNames=6605 onlyVnam=0 onlyRes=0 differingChains=0` |
| The retail authored tables select inside their chain and are culled past the authored cutoff | **Verified (host, measured)** | `[lod-qvm] markerLines=462 authoredEntries=462 distinctModels=450 trailing_1=462 resolvedInBundles=431 commonArchiveNames=189 monotonizeChanged=0 selected=431 culled=431` |
| Authored entry count of `LOD.QVM` | **Corrected by measurement**: 462 `ModelLODSettings` lines covering 450 distinct model names (all ending `_1`), not 449 | the `[lod-qvm]` line above; the tokenizer is the renderer's previous inline loop, moved verbatim |
| 31 authored entries name models no shipped bundle contains; the engine would write them onto the `"missing"` fallback model | **Verified (oracle + measured)** | `VirModel_Find` substitutes `"missing"`, `src/model/model.c:107-116`, called by `ModelLODSettings_Changed` `src/animtrig/lodsettings.c:161-171`; unresolved examples printed by the test |
| `source/renderer/lightmap_ref_index.h` has no consumer because no LOD switch exists | **Verified (editor architecture)** | `GetLightmapForTask`/`SetLightmapForTask` path above; `GetLightmapTexture` zero callers |
| The changed translation units compile with the project's cross-compiler flags | **Verified** | `tu-check.py` over `renderer_objects.cpp`, `renderer_objects_mesh.cpp`, `renderer_objects_metadata.cpp`, `renderer_draw.cpp`: `checked 4 translation unit(s), 0 failed` |
| Holes in the LEVEL12-14 ref index spaces are non-format-3 LOD refs | **Inferred, not proven** | mechanism cited at `lightmapbuild.c:45-46`; `objNNNNN` sets cannot be mapped back to objects from the shipped data |
| The chain wired into the renderer / viewport | **Partly done** — chain resolution and exposure are in the mesh lookup path; the per-frame switch is not enabled (§6 item 1) | §5, §6 |
| Any rendered LOD or lightmap is correct | **Unverified** — the editor needs GL 4.1+ compatibility and this host offers GL 2.1, so no GUI run is possible | — |

## 8. Reproduction

```bash
# Narrowest: the rule, the corpus histogram, the archive-set equivalence and the
# authored-table pipeline (corpus via the runner's IGI_GAME_PATH).
IGI_HOST_NO_CACHE=1 IGI_HOST_TESTS="tests/test_lod_chain.cpp" bash scripts/run-host-tests.sh

# Full host lane (regression).
IGI_HOST_NO_CACHE=1 bash scripts/run-host-tests.sh

# Cross-compiler syntax check of the wired translation units.
python3 tools/igi-re-parity/tu-check.py source/renderer/renderer_objects.cpp \
    source/renderer/renderer_objects_mesh.cpp source/renderer/renderer_objects_metadata.cpp
```

The §3 and §4 tables come from scratch tools (deliberately not committed: they
duplicate the committed corpus tests or read live game data):

* an MTP VNAM reader that walks `FORM` + BE size + `MTP `, then `fourcc` + BE size
  chunks, decoding `VNAM` as `u32 count`, `count` u32 offsets, packed names;
* `RES_BuildIndex` + `RES_ReadEntry` over each level's `lightmaps/lightmaps.res`,
  grouping `objNNNNN_%05d.olm` entries by prefix and reading `count` at offset 0x2C.

The §5-§7 measurements are produced by the two corpus tests, which print their
numbers (`[lod-res]`, `[lod-qvm]`) so the figures can be re-read without a
scratch tool.

## 9. The per-frame cost of the LOD switch (measured, then fixed)

The per-frame switch **is** wired now (`Renderer_Objects::Draw` calls
`SelectLodForDistance` per placement), and it had a cost that dominated the whole
frame on a GL translation layer. §6 item 1's warning was about *lightmap*
correctness; the cost was somewhere else entirely.

`ModelObj_GetLOD` needs three words out of LOD 0's `MeshHeader` — `valid`,
`center`, `sphere` (`src/lod/mesh3d.c:220-224`) — *before* it can call
`lod_select`. The draw path read them with

```cpp
igi::lod::ReadMefMeshHeader(FindMeshData(obj.modelId))
```

per placement, per pass, per frame. `FindMeshData` builds the archive-identity key
and walks the loaded indexes, and `RES_ReadEntry` (`source/renderer/res_writer.cpp:463`)
**opens the `.res` file and reads the whole entry off disk** — so a level with 1416
placements performed ~2832 file opens + full `.mef` reads every frame to obtain a
value that cannot change while the level is loaded. `SelectLodForDistance` already
memoized the same header in its LOD chain (`chain.header[]`, `renderer_objects_mesh.cpp:180`);
the draw path simply did not use it.

Fixed by `Renderer_Objects::GetLod0MeshHeader` (`renderer_objects_mesh.cpp:284`), a
per-`(level, modelId)` memo of that exact call, cleared with the `.res` indexes it
derives from (`ClearResCache`, and `RefreshTextureResCache` for the import path).
It is pure memoization: the same `FindMeshData` + `ReadMefMeshHeader` inputs, read
once instead of thousands of times, so the returned words are unchanged.

Measured under Wine (i686, Apple GL 2.1 fallback), `IGI_PROFILE=1`, level 1,
`[ObjectProfile]` counters from the same binary before and after:

| Phase (per frame) | Before | After |
| --- | --- | --- |
| object resolve (mesh + LOD header + lightmap key) | 581 ms | 14 ms |
| GL submission (721 submesh draws) | 25 ms | 19 ms |
| attachments | 32 ms | 25 ms |
| `[FrameProfile]` end-to-end | 2534-5339 ms/frame | 155-163 ms/frame |

The GL submission column is the point worth keeping: 721 submesh draws cost
~20-25 ms, i.e. Wine's GL thunking was **never** the bottleneck — the redundant
per-object disk read was, by ~40x. On an idle host the same change takes the frame
from 1415 ms to 32 ms (`[RenderProfile]`).

**Not measured:** the ~18-40 ms/frame that remains outside the objects pass
(`[FrameProfile]` minus `[RenderProfile] total`). The task-tree HUD
(`renderer_draw.cpp:781+`) walks every object and draws immediate-mode text per
visible row, and `App::Frame` rebuilds a large parameter struct per frame; neither
is instrumented, and neither was changed.
