# MEF dual-vertex-set AI models — render partition / material selection

Scope: issue #47 / `docs/KNOWN_ISSUES.md` → "Character-model material precedence".
Target file: `source/renderer/mef_native.cpp`.

Oracle (read-only): `/Users/haseeb-mir/Documents/Code/IGI-RE` — the faithful C
decompilation of retail `IGI.exe`. Every claim below cites `file:line`.
Real assets used for measurement: `/Users/haseeb-mir/Downloads/IGI` (retail install,
1578 `.mef` files, level `.DAT` and `.MTP` tables).

## 1. Conclusion up front

The hypothesis this task started from — that the editor selects render partitions /
material slots from the wrong chunk set (`XTVC0/ECFC0/TAMC0` vs `XTVC1/ECFC1/TAMC1`)
— is **disproved**. `XTVC/ECFC/TAMC/HSPC` are *collision* data; the render partitions
come from the `REND`/`DNER` group chain, and the editor already reads them correctly.

Measured on the actual model named in the issue (`001_02_1`, extracted from
`LEVEL7.RES`), the editor's render-partition → material assignment is **already
correct**:

* 13 `DNER` groups ↔ 13 materials ↔ 13 `MTP` textures, same order (§4);
* per-material triangle counts match the file's own collision mesh exactly (§5).

The change delivered here (`mef_native.cpp`) makes the `DNER` walk identical to the
game's walk. It is **provably a no-op on all 1578 retail MEFs** (§6), so it is a
parity/safety alignment and **not** the fix for the reported symptom. Section 7 lists
what the evidence leaves as the live suspects.

## 2. What the chunks actually are (oracle)

`Direct3DRender_LoadModel` (`src/bonemesh/d3drender.c:204`, `HOOK(0x49FB10, …)` at
`:262`) is the only loader for `.mef` render data. Chunk ids (`:49-54`):

| define | value | file bytes |
|---|---|---|
| `ID_RD3D` | `0x52443344` | `D3DR` |
| `ID_CMSH` | `0x434D5348` | `HSMC` |
| `ID_CVTX` | `0x43565458` | `XTVC` |

Per model format (`d3drender.c:204-261`):

* **format 0 (rigid)** — `data = RD3D`; `r->unk10 = XTRV`; `r->unk14 = REND`;
  `MeshBatch_Add(0, header, r->unkC)`.
* **format 1 (bone/skinned, AI characters)** — `r->unk18 = XTRV`; `r->unk1C = REND`.
* **format 3 (lightmapped building)** — `r->unk14 = XTRV`; `r->unk1C = REND`;
  `r->unk18 = LTMP`; `MeshBatch_Add(1, header, r->unk10)`.

Then `d3drender.c:234-261` reads the `HSMC` **collision** chunk and fills exactly
**two** collision parts (`for (i = 0; i < 2; i++)`) from the 1st and 2nd
`XTVC`/`ECFC`/`TAMC`/`HSPC` sets. So the two "vertex sets" are the two halves of the
collision mesh — they have nothing to do with render partitions.

Render partitions are the `REND`/`DNER` group chain:

* `src/render/rendertypes.h:6-18` — `MeshGroup`: `center[3]`@0, `indexCount`@12,
  `next`@14 ("bytes from the indices to the next group, `0xFFFF` = last"), `texture`@16
  (`0xFFFF` = untextured), `base`@18, `vertexCount`@20, `sorted`@22, `flags`@24,
  `unk19[2]`@25, `alpha`@27, `indices[]`@0x1C.
* `src/bonemesh/d3drender.c:163-183` (format 1 draw) — walk:
  `g = (MeshGroup *)((char *)g + sizeof(MeshGroup) + g->next)`, stop when
  `g->next == 0xFFFF`; texture = `((void **)lod->textures[g->texture])[8]`; draw
  `DEV_DrawIndexedPrimitiveVB(..., g->base + base, g->vertexCount, g->indices, g->indexCount, 0)`.
* `src/render/render.c:487-489` (format 0) — `g = (char *)g + g->next + 0x1C`.
* `src/render/render.c:1047,1081,1110-1120` (format 3) — `g = (char *)g + g->next + 0x20`;
  `LmGroup` at `render.c:40-54` (`indices[]`@0x20, asserted at `render.c:151`).
* `src/render/render.c:216-219` — `lod_texture(l, i) = ((Texture *)l->textures[i])->surface`.
* `src/model/model.c:287-313` — per-LOD `textures[i]` list, built from the level `MTP`.

**No oracle code reads an RD3D header field as a group count.** The editor used
`RD3D+8` (formats 0/1) / `RD3D+12` (format 3) as a record bound; those are
`MeshRender.unk8` and `MeshRender.unkC`, and `unkC` is the *extra influence count*
(`src/bonemesh/bonemesh.h:21-31`, read by `BoneMesh_Skin`, §3).

## 3. The dual vertex set (oracle)

`BoneMesh_Skin` (`src/dxinit/d3drender.c:226`, `HOOK(0x49B700, …)` at `:313`) takes
the `MeshRender` as its first argument and reads:

* `count = *(int *)(mesh + 0x10)` — base vertex count → output vertex buffer size;
* `v = *(SkinVertex **)(mesh + 0x18)` — the `XTRV` data;
* extra influences = `*(int *)(mesh + 0xC)`; extra vertices start at
  `*(SkinVertex **)(mesh + 0x18) + *(int *)(mesh + 0x10)` and accumulate into
  `out[v->target]`.

`SkinVertex` (`src/dxinit/d3drender.c:37-52`, 0x28 bytes): `p[3]`@0, `n[3]`@12,
`uv[2]`@24, `w`@32, `target`@36, `bone`@38.

The editor's mapping is the same and was already correct
(`source/renderer/mef_native.cpp:673-674`):
`extraInfluenceCount = RD3D+12`, `baseVertexCount = RD3D+16`.

## 4. Measured: the real `001_02_1`

`001_02_1.mef` is not loose on disk; it lives in the level 7/8 model archives. It was
extracted with the editor's own `RES_Extract` (`source/renderer/res_writer.h`):

```
RES_Extract LEVEL7.RES "LOCAL:models/001_02_1.mef" -> 91748 bytes
ParseMefFile: type=1 verts=1112 tris=658 blocks=13
  slots=0:0:14, 1:14:28, 2:42:42, 3:84:102, 4:186:58, 5:244:26, 6:270:100,
        7:370:100, 8:470:16, 9:486:8, 10:494:94, 11:588:34, 12:622:36
  xtvc=337 ecfc=658 tamc=13
```

13 render groups, slots `0..12`, no gaps.

The game's own texture list for the model, read from `LEVEL7.MTP` the way
`src/model/model.c:287-313` reads it (`VNAM` count → `names = (const char *)(inst + count + 1)`;
`INST` = `{idx, n, g[]}` per model; `GTT` pairs; `TEXF` = the level texture-name table
indexed by the `GTT` `t` value):

```
MTP  : 018_03_1 007_06_1 001_06_1 001_07_1 001_02_1 015_13_1 001_05_1
       004_13_1 000_09_1 000_10_1 001_04_1 001_01_1 001_03_1
DAT  : 018_03_1 007_06_1 001_06_1 001_07_1 001_02_1 015_13_1 001_05_1
       004_13_1 000_09_1 000_10_1 001_04_1 001_01_1 001_03_1
ORDER MATCH: True
```

So `renderBlocks[k].materialSlot == k` indexes the same texture the game uses for
group `k`, with the same count (13) and the same order. There is no out-of-range slot
and therefore no trip through the "AI materialSlot out of range" fallback in
`source/renderer/renderer_objects_texture.cpp`.

## 5. Independent cross-check: the file's own collision mesh

The collision arrays are a second, parallel copy of the same triangles: `ecfc == 658`
== `tris`, and `TAMC[i].diffuse == i` (0…12). Grouping the render triangles by
`materialSlot` and the collision faces by `TAMC[face.mat].diffuse` gives:

| material | render triangles | collision faces |
|---|---|---|
| 0 | 14 | 14 |
| 1 | 28 | 28 |
| 2 | 42 | 42 |
| 3 | 102 | 102 |
| 4 | 58 | 58 |
| 5 | 26 | 26 |
| 6 | 100 | 100 |
| 7 | 100 | 100 |
| 8 | 16 | 16 |
| 9 | 8 | 8 |
| 10 | 94 | 94 |
| 11 | 34 | 34 |
| 12 | 36 | 36 |

Exact agreement on all 13 materials, from data the editor never uses for rendering.
The same check passes for `005_01_1` and `001_01_1`. This is strong evidence that the
`DNER` walk + `texture` field interpretation are correct on retail assets.

For `001_01_1` there is also a dconv ASCII export; its per-material face counts
(`001_11_1`…`016_07_1`, 12 materials) equal the binary blocks' triangle counts
`232, 54, 14, 50, 96, 44, 110, 12, 32, 26, 8, 16` in the same order.

## 6. The change in this branch, and its measured effect

`source/renderer/mef_native.cpp`, `ParsePackedRenderTriangles` (`:311`):

* reads `next` at +14 as `uint16_t` (was `int16_t nextoffs`);
* drops the `expectedRecords` bound (the unread RD3D field, call site `:698`);
* terminates on `next == 0xFFFF` and steps `cursor += headerSize + next`
  (was `cursor += headerSize + indexBytes`), matching
  `d3drender.c:163-183` / `render.c:487-489` / `render.c:1110-1120`.

Measured over every retail MEF (`find /Users/haseeb-mir/Downloads/IGI -iname '*.mef'`,
1578 files), parsing with the pre-change and post-change parser and diffing a dump of
`{type, verts, tris, block count, layout, per-block slot/start/count, xtvc, ecfc, tamc}`:

```
old lines=1578  new lines=1578
diff lines: 0
```

**Byte-identical output on all 1578 files.** Retail chains always satisfy
`next == indexCount*2` and `RD3D count == chain length`, so this change cannot regress
retail content; it removes a dependency on a field the game never reads and makes the
terminator the game's terminator. It does **not** change any shipped model's appearance.

Residual risk: a malformed chain with no `0xFFFF` terminator now walks to the chunk end
instead of stopping at the RD3D count. Each step consumes ≥ 28 bytes and the loop is
bounded by `cursor + headerSize <= chunk.size`, so it cannot hang or overrun.

### 6.1 Real-asset confirmation

`tests/test_mef_native_lightmap.cpp` asserts, on real assets, `modelType == 1`,
`renderLayout == "type1 packed DNER"`, non-empty `renderBlocks`/`triangles`,
`baseVertexCount + extraInfluenceCount == vertices.size()` and `materialSlot == i` for
every block. Its fixtures are Windows paths, so it used to skip on this host; the
fixtures now also accept `IGI_MEF_SNIPER` / `IGI_MEF_WATERTOWER` (the `D:\IGI1\…` paths
remain the fallback, so Windows behaviour is unchanged). Run against the real assets:

```
IGI_MEF_SNIPER=<001_02_1.mef from LEVEL7.RES> \
IGI_MEF_WATERTOWER=<.../LEVEL1/MODELS/models/435_01_1.mef> mef_lightmap
  [ RUN/OK ] MefNativeLightmapTest.ModelType3PopulatesDistinctUv2Channel
  [ RUN/OK ] MefNativeRenderLayoutTest.SkinnedModelsUsePackedDrawRecords
  [  PASSED  ] 2 tests.
```

Both also pass against the **pre-change** parser, i.e. the same no-op conclusion reached
from the 1578-file differential, now confirmed on the exact model named in the issue.

### 6.2 Host-native gate

```
$ IGI_MEF_SNIPER=<001_02_1.mef> \
  IGI_MEF_WATERTOWER=<.../LEVEL1/MODELS/models/435_01_1.mef> \
  IGI_HOST_TESTS="tests/test_mef_dual_set.cpp tests/test_mef_native_lightmap.cpp" \
  bash scripts/run-host-tests.sh
   PASS  test_mef_dual_set                     4 passed    0 failed  (4 tests, exit 0)
   PASS  test_mef_native_lightmap              2 passed    0 failed  (2 tests, exit 0)
   suites run: 2   suites failed: 0   tests run: 6   tests passed: 6   tests failed: 0
   ==> run-host-tests.sh exit=0
```

`IGI_MEF_SNIPER` / `IGI_MEF_WATERTOWER` are optional: without them the two real-asset
tests skip, as before, and `test_mef_dual_set` (synthetic fixtures) still runs.

## 7. What is *not* explained — live suspects

The evidence above removes render-partition/material *selection* as the cause of the
reported symptom ("a clothing/sign texture appears on the face/neck/hands"). What is
left, all outside `mef_native.cpp`:

1. **Texture resolution for a model loaded outside its home level.** `001_02_1` exists
   only in `LEVEL7`/`LEVEL8` `.DAT`/`.MTP`. The 13 ids it references are resolved
   through the *current* level's archives; a same-named but unrelated entry in a
   polluted level `.res` would put the wrong image on a correctly-selected part. See
   `source/renderer/model_texture_resolution.{h,cpp}` and
   `tests/test_model_texture_resolution.cpp` (which already documents
   "001_02_1's polluted level reses").
2. **The AI `_argb8888` substitution.** `source/renderer/renderer_objects_texture.cpp:523-525`
   replaces an `…_argb8888` id with its stripped id for AI models. Several character
   models have such entries (e.g. `709_01_2` → `600_30_1_argb8888`, `712_01_1` →
   `716_01_1_argb8888`). If the stripped id resolves to a different image, that part
   gets the wrong texture while every mapping above stays correct.
3. **Unproven, documented only — `sorted` vs `alpha`.** The editor reads the opacity
   field at +22 for formats 0/1 and +24 for format 3; the oracle's `alpha` is at +27
   (`rendertypes.h:27`) and +31 (`render.c:31`), while +22/+24 are `MeshGroup.sorted` /
   `LmGroup.sorted`. Changing this would alter blending on *working* models and is not
   justified by the reported symptom, so it is deliberately **not** changed here.

## 8. Verification status

| Claim | Status |
|---|---|
| `XTVC/ECFC/TAMC/HSPC` are collision data, filled as two parts | verified, oracle `d3drender.c:234-261` |
| Render partitions are the `REND`/`DNER` chain terminated by `next == 0xFFFF` | verified, oracle `d3drender.c:163-183`, `render.c:487-489,1110-1120` |
| Editor's `baseVertexCount`/`extraInfluenceCount` mapping | verified, oracle `dxinit/d3drender.c:226-313` |
| The editor's ASCII-export fields (`xtvcVerts`, `ecfcFaces`, `tamcRecords`) do not feed `renderBlocks` | verified by grep + `tests/test_mef_dual_set.cpp` |
| `mef_native.cpp` change is a no-op on 1578 retail MEFs | verified by differential run (§6) |
| `001_02_1` block→material mapping is correct | verified against the file's own collision mesh (§5) |
| `001_02_1` material→texture order equals the game's `MTP` order | verified (§4) |
| `tests/test_mef_native_lightmap.cpp` on real type-1 + type-3 assets | verified, 2/2 passed, identical on the pre-change parser (§6.1) |
| `tests/test_mef_dual_set.cpp` red→green | verified: 3/4 fail on the pre-change parser, 4/4 pass after |
| The change *fixes* the reported wrong-texture symptom | **not verified — and contradicted by §4-§6** |
| `sorted` vs `alpha` field mismatch | hypothesis only, not changed |
| Suspects 1-2 in §7 | not verified; outside this task's write scope |

## 9. Reproduction commands

```bash
# parse any retail MEF with the editor's parser
c++ -std=c++20 -O1 -Isource -Isource/renderer -Ithird_party/glm-1.0.1 dump.cpp \
    source/renderer/mef_native.cpp -o dump

# extract the model named in the issue from its level archive
#   RES_Extract LEVEL7.RES "LOCAL:models/001_02_1.mef"

# MTP texture order (game truth) vs LEVEL7.DAT order for 001_02_1
#   see §4; parser: VNAM {int count; int offs[count]; char names[]}, INST, GTT, TEXF
```
