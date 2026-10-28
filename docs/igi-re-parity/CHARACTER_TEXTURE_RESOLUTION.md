# Character-model texture resolution precedence

Issue #47 / `docs/KNOWN_ISSUES.md` item (3): *"A few AI/character models (notably
`001_02_1`) render with textures on the wrong body parts in the editor viewport —
e.g. a clothing/sign texture appears on the face/neck/hands. In-game these render
correctly (editor-preview only)."*

**Result: root cause found, fixed, and quantified on retail data.** It is not
geometry and not the MEF material mapping. The editor resolved a model's textures
through whatever archives the session had indexed, so a same-named image from an
unrelated archive won over the image in the archive the game actually uses. In a
level-2 or level-4 preview of `001_02_1`, **11 of 26 material-slot lookups bound a
different image than the game does** (after the fix: 0 of 26).

Companion work: `docs/igi-re-parity/MEF_DUAL_SET.md` (task-2) disproved the
earlier "two render/vertex sets" theory — the render-partition → material-slot
mapping is correct, which is exactly why the symptom had to be in resolution.

---

## 1. Oracle: the game resolves textures from exactly one archive

Every claim below is from `/Users/haseeb-mir/Documents/Code/IGI-RE` (faithful C
decompilation of retail `IGI.exe`; every function carries `HOOK(0xADDR, Name)`).

| Step | IGI-RE location | What it does |
| --- | --- | --- |
| 1 | `src/model/model.c:233-235` | `ModelFile_Load`: `sprintf("%s%s.mtp", gModelDir, name)` then `ModelFile_Setup(f, name, 1)` |
| 2 | `src/model/model.c:251`, `HOOK(0x4CF080)` at `:317` | `ModelFile_Setup` |
| 3 | `src/model/model.c:261-265` | `sprintf("%stextures/%s.res", gModelDir, name)` → `Resource_Load(path, 0)` → `ModelFile_LoadTextures(f)` → `Resource_Unload(path)` |
| 4 | `src/model/model.c:450-461` | `ModelFile_LoadTextures` reads `MTP_TEXF` from **that same resource** into `f->textures` (`n * 0x24` bytes) |
| 5 | `src/model/model.c:300-306` | Per material: `t = gtt[g * 2]`; `t < 0x2000` → `tex = f->textures + t * 0x24`, else `first->textures + (t - 0x2000) * 0x24` |

Consequences:

* The archive is chosen by **the level that owns the model file** (`gModelDir` +
  `name`), not by the level being played and not by a search.
* There is **no cross-level search and no name-based fallback**. A texture the
  level's own archive does not contain simply is not there.
* `0x2000+` indices address the **first-loaded** model file's table
  (`model.c:281-284`), which is the shared/first bundle, not an arbitrary level.

So the correct precedence for one model is: *its own level's archive*, then
nothing else. The shared common archive is a substitute only because the editor
must preview models whose level is not loaded.

`001_02_1` is owned by level 7 (`LEVEL7.DAT` / `LEVEL7.MTP` list it; `LEVEL8`
also lists it). Its GTT indices are all `< 0x2000`, so the game reads all 13 of
its textures from `missions/location0/level7/textures/level7.res`.

## 2. Measured: the same texture id is a different image in different archives

This is what makes the precedence observable. Extracted from the retail install
at `/Users/haseeb-mir/Downloads/IGI`; `bytes` is the stored `.tex` size, `WxH` is
read from the `.tex` header (`LOOP` + 32-byte header, 16-bit pixels) and
cross-checked against `size == 32 + W*H*2`.

| texture id | level7 archive | shared common archive | level4 archive |
| --- | --- | --- | --- |
| `001_02_1` | 32800 B, 128×128 | **8224 B, 128×32** | — |
| `001_04_1` | 32800 B, 128×128 | **4128 B, 128×16** | — |
| `001_01_1` | 8224 B, 64×64 | **32800 B, 128×128** | — |
| `001_03_1` | 2080 B, 32×32 | **16416 B, 64×128** | — |
| `001_05_1` | 8224 B, 64×64 | **4128 B, 64×32** | — |
| `018_03_1` | 65568 B, 128×256 | — | **4128 B, 32×64** |
| `007_06_1`, `001_06_1`, `001_07_1`, `004_13_1`, `000_10_1` | present | — | — |
| `015_13_1`, `000_09_1` | present, identical in all 14 level archives | | |

`001_02_1` is the model's *own* material slot 4 texture and the common copy is a
128×32 strip — the "clothing/sign texture" of the report, on a part UV-mapped for
a 128×128 sheet.

## 3. The defect and the fix

`source/renderer/renderer_objects_texture.cpp`, `GetOrLoadTexture`:

* **Before**: step 1 called `FindTextureData(textureId)`, which searched the
  session's `res_tex_indexes_` in indexing order.
  `FindPreviewTextureEntry` searches non-common archives before the common
  archive, but *within* the non-common group it takes the first hit in list
  order. `LoadResCache(current_level_)` always indexes the current level **and**
  the shared common archive, so for a foreign model the common copy was found
  first and step 1b (the lazy owner-level load, which only runs on a *miss*)
  never executed.
* **After**: the owning level's archive is derived from `texture_level_map_`
  (built from all 14 `LEVEL*.DAT` + `common/location0.dat`), indexed if needed,
  and hoisted in front of the session order with
  `OrderTextureArchivesByOwner` before the search.
* `ApplyTexturesToMesh` additionally indexes the archive of the level that owns
  the model (`model_level_map_`), so ids the owning level defines are reachable
  even when the DAT-derived owner guess points elsewhere. Both paths are needed:
  see `001_01_1` in the table below.

No public API changed; `FindTextureData` is unchanged and still used elsewhere.

### Old vs new, on retail data

`tools`-style harness (kept out of the repo; commands in §7). OLD models the
pre-fix path faithfully: session order first, then the step-1b lazy owner load on
a miss.

```
model 001_02_1 (owned by level 7) previewed in a level-2 session
texture    | OLD (session, then owner)      | NEW (owner first)
018_03_1   | level7 65568B 128x256          | level7 65568B 128x256
007_06_1   | lazy-owner level7 8224B 64x64  | level7 8224B 64x64
001_06_1   | lazy-owner level7 8224B 64x64  | level7 8224B 64x64
001_07_1   | lazy-owner level7 32800B 128x128 | level7 32800B 128x128
001_02_1   | common 8224B 128x32            | level7 32800B 128x128           <-- FIXED
015_13_1   | level7 160B 8x8                | level7 160B 8x8
001_05_1   | common 4128B 64x32             | level7 8224B 64x64              <-- FIXED
004_13_1   | lazy-owner level7 16416B 128x64 | level7 16416B 128x64
000_09_1   | level7 4128B 32x64             | level7 4128B 32x64
000_10_1   | lazy-owner level7 544B 16x16   | level7 544B 16x16
001_04_1   | common 4128B 128x16            | level7 32800B 128x128           <-- FIXED
001_01_1   | common 32800B 128x128          | level7 8224B 64x64              <-- FIXED
001_03_1   | common 16416B 64x128           | level7 2080B 32x32              <-- FIXED

model 001_02_1 (owned by level 7) previewed in a level-4 session
018_03_1   | level4 4128B 32x64             | level7 65568B 128x256           <-- FIXED
001_02_1   | common 8224B 128x32            | level7 32800B 128x128           <-- FIXED
001_05_1   | common 4128B 64x32             | level7 8224B 64x64              <-- FIXED
001_04_1   | common 4128B 128x16            | level7 32800B 128x128           <-- FIXED
001_01_1   | common 32800B 128x128          | level7 8224B 64x64              <-- FIXED
001_03_1   | common 16416B 64x128           | level7 2080B 32x32              <-- FIXED

wrong-image resolutions across both sessions (of 13 ids each): OLD 11   NEW 0
```

`{model → DAT ids → resolved archive}` for `001_02_1`, in MTP/DAT order (the DAT
order was verified equal to `LEVEL7.MTP` in `MEF_DUAL_SET.md` §4):

| slot | texture id | game's archive | pre-fix editor (level-2 session) | post-fix editor |
| --- | --- | --- | --- | --- |
| 0 | `018_03_1` | level7 | level7 | level7 |
| 1 | `007_06_1` | level7 | level7 (lazy) | level7 |
| 2 | `001_06_1` | level7 | level7 (lazy) | level7 |
| 3 | `001_07_1` | level7 | level7 (lazy) | level7 |
| 4 | `001_02_1` | level7 | **common 128×32** | level7 128×128 |
| 5 | `015_13_1` | level7 | level7 | level7 |
| 6 | `001_05_1` | level7 | **common 64×32** | level7 64×64 |
| 7 | `004_13_1` | level7 | level7 (lazy) | level7 |
| 8 | `000_09_1` | level7 | level7 | level7 |
| 9 | `000_10_1` | level7 | level7 (lazy) | level7 |
| 10 | `001_04_1` | level7 | **common 128×16** | level7 128×128 |
| 11 | `001_01_1` | level7 | **common 128×128** | level7 64×64 |
| 12 | `001_03_1` | level7 | **common 64×128** | level7 32×32 |

This also explains why the *import* path already looked correct
(`docs/ISSUE_FIXES.md` → `IGIED-FOREIGN-MODEL-TEXTURES-001`, E2E 13/13
byte-identical): imports use `FindModelSourceEntry`, whose bundle is
`[selected source level, common]` and therefore already had the right archive.
Only the viewport preview went through the unqualified search — consistent with
the report's "editor-preview only".

## 4. `_argb8888` ids with no direct match (task-6 ask b)

* Game side: there is no name lookup at all. `_argb8888` is just another entry in
  the level's `MTP_TEXF` table (`model.c:450-461`) and is addressed by index. If
  the level's archive has no such entry the game has no fallback — it is a
  property of the shipped data, not of a resolution algorithm.
* Editor side: `FindPreviewTextureEntry` tries the exact id first and, on a miss,
  the `StripTextureFormatSuffix` id **within the same search order**. Because the
  owner archive is now searched first, a stripped id can no longer be satisfied by
  an unrelated archive's same-named base entry: measured, `004_13_1` exists in
  level1 and level7 with different bytes, so
  `004_13_1_argb8888` used to resolve to level1's image and now resolves to
  level7's.
* Where the exact `_argb8888` entry does exist it still wins (the editor's
  deliberate "exact first" preference for non-AI models is untouched); the
  AI-model stripping at `renderer_objects_texture.cpp:523-525` (now
  `:552-554`) is unchanged.
* Whether the *game* actually binds the `_argb8888` variant for a given model is
  decided by the level's `MTP_TEXF`/`GTT` indices, which the editor reproduces
  through the DAT; that mapping was not modified here.

## 5. Documented, not changed: the opacity offsets

`MEF_DUAL_SET.md` §7 and the task-6 brief both record that the editor reads
per-record opacity at `+22` (format 0/1) and `+24` (format 3), while IGI-RE says
those fields are `MeshGroup.sorted` (`src/render/renderertypes.h:27`) and
`LmGroup.sorted` (`src/render/render.c:31`), and the real `alpha` is at `+27` and
`+31`. **Not changed here**: it alters blending on models that currently render
correctly, it is not implicated in the wrong-texture symptom, and it needs its own
red/green evidence on a model whose blend mode is visible. Tracked as a separate
item.

## 6. Verification status

| Claim | Status | Evidence |
| --- | --- | --- |
| Game resolves a model's textures from its own level's archive only | **Verified** | `src/model/model.c:233-235,251,261-265,450-461,300-306` |
| Same texture id is a different image in different archives | **Verified** | `.tex` header + size of the extracted entries (§2) |
| Pre-fix editor bound the common copy for `001_02_1` in a level-2 session | **Verified** | old-vs-new table (§3), `test_character_texture_resolution` pins it |
| Pre-fix editor bound level4's `018_03_1` in a level-4 session | **Verified** | same table |
| Fix resolves all 13 slots from the owning level | **Verified** | 13/13 in the table; `EXPECT_EQ(resolvedInLevel7, ownedByLevel7)` on real archives |
| 11 of 26 lookups were wrong before, 0 after | **Verified** | harness total (§3) |
| Changed TUs compile | **Verified** | `tu-check.py`: 4 TUs checked, 0 failed |
| Changed TUs compile and link in the Windows cross-build | **Verified** (build gate) | `bash scripts/build-windows.sh --arch x64`: `renderer_objects_texture.cpp.obj`, `model_texture_resolution.cpp.obj`, `test_character_texture_resolution.cpp.obj` all built, `igi1ed.exe` + `igi_tests.exe` linked, `BUILD GATE OK (exit 0)`, 0 `FAILED:` lines |
| New suite passes | **Verified** | `run-host-tests.sh`: 4 tests, 4 passed, exit 0 (retail case included) |
| The on-screen symptom is gone | **Not verified** | needs a Windows viewport run; no host renderer |
| `GetOrLoadTexture` / `ApplyTexturesToMesh` execute under test | **Not verified** | `renderer_objects_texture.cpp` is one of the 13 engine sources the host lane cannot compile (Windows-only `game_process_s` in `source/app.h:473-486`); the cross-build proves compile+link only. Behavioural evidence for the ordering itself is the red-before/green-after probe |
| Opacity offsets `+22/+24` vs `+27/+31` | **Open, unchanged** | §5 |

## 7. Reproduction

```bash
# Host suite, synthetic + retail cases. IGI_GAME_PATH is what the runner exports
# for the detected install, so no extra variable is needed when the corpus is
# present; the retail case self-skips otherwise.
IGI_HOST_TESTS="tests/test_character_texture_resolution.cpp" bash scripts/run-host-tests.sh

# Syntax check of every changed TU
python3 tools/igi-re-parity/tu-check.py \
  source/renderer/renderer_objects_texture.cpp \
  source/renderer/model_texture_resolution.cpp \
  tests/test_character_texture_resolution.cpp

# Compile + link gate (the only check that covers renderer_objects_texture.cpp)
bash scripts/build-windows.sh --arch x64
```

The old-vs-new table in §3 comes from a scratch harness that links
`source/renderer/model_texture_resolution.cpp` + `source/renderer/res_writer.cpp`,
indexes `common`, `level2`, `level4` and `level7` texture archives, and compares
`FindPreviewTextureEntry(session)` (old) against
`FindPreviewTextureEntry(OrderTextureArchivesByOwner(session + owner, owner))`
(new) for the 13 ids. It is intentionally not committed: it duplicates the
committed test's assertions on live game data.

Red-before / green-after: `tests/test_character_texture_resolution.cpp`
`OwnerLevelArchiveBeatsEarlierSessionArchives` asserts *both* directions — that
the plain session order returns the unrelated common image (the defect, which is
what the pre-fix code produced) and that the owner-first order returns the
owning level's image. Measured: with an identity-ordering probe in
`OrderTextureArchivesByOwner` the suite reported
`1 passed 3 failed (4 tests, exit 1)` and `run-host-tests.sh exit=1`; the probe
was reverted and the file confirmed byte-identical to the pre-probe copy, after
which the suite reported `4 passed 0 failed`.

## 8. Related paths checked, and what remains

**The ATTA path is already oracle-correct — checked, no change needed.**
`Renderer_Objects::FindTextureDataFromLevel` (`renderer_objects.cpp:444-481`)
passes `bundle = {levelRes, commonRes}` to `FindModelSourceEntry`, which searches
`bundle[0]` first (`model_texture_resolution.cpp:206-250`). That is the level
before common, i.e. the precedence IGI-RE implies, and the pre-existing test
`SelectedLevelTextureOverridesSharedCommonTexture` pins it. Note the comment at
`renderer_objects.cpp:466-467` ("Texture lookups search common first so polluted
same-name level copies lose") states the **opposite** of what the code does — it
is a stale comment, not a bug. That file is outside this task's write scope.

**Remaining gap (known, not fixed, not needed for the measured symptom).** The
owner archive is hoisted using `texture_level_map_`, the first level whose DAT
lists the id. When the *current* level also defines that id and the DAT-derived
owner happens to be the current level, the hoist is a no-op and the current
level's copy still wins over the model's real owner. For all 13 ids of
`001_02_1` the DAT-derived owner is level 1 or level 7 (never the level being
previewed), so the measured table is unaffected: 11 wrong lookups → 0. Closing
the general case needs the model's own level archive to precede the current
level for the duration of one model's texture application, which means either a
per-model lookup context or a signature change in `renderer_objects.h` — both
outside this task's write scope. Recorded here so it is not lost.

> **Closed later — see §9.** The general case was fixed by hoisting the archive the
> *model's bytes were actually read from* rather than any DAT-derived owner. That is
> strictly better than both rules above: it is 100% correct on the ids each level's
> own archive defines (67,749 of 67,749 across all 14 levels), where this task's
> texture-id-owner rule was still wrong on 398 of them. `OrderTextureArchivesByOwner`
> is unchanged; what changed is which archive path is passed to it.

## 9. The archive the model came from decides (the general case)

### 9.1 The engine rule, again

A model file and its textures always come from **one level directory**, and that
directory is set by `Model_SetDir`:

| Step | IGI-RE location | What it says |
| --- | --- | --- |
| 1 | `src/game/game.c:411-418`, `HOOK(0x4CFBD0)` at `src/lod/modelfile.c:153-157` | `Model_SetDir("LOCAL:<mission dir>/")` before the game model is loaded, then `Model_SetDir(path2)` before the level's models: `gModelDir` is the level's own directory |
| 2 | `src/model/model.c:233` `ModelFile_Load`, `:251` `ModelFile_Setup` `HOOK(0x4CF080)` | the `.mtp`, `models/<name>.res` (`:267`) and `textures/<name>.res` (`:261`) are all `gModelDir + …` |
| 3 | `src/model/model.c:450-461`, `:300-306` | the `MTP_TEXF` table of that one texture resource is the model's texture table, addressed by the `GTT` index |
| 4 | `src/game/game.c:414`, `:418` | the dir changes **twice in one level load** and never because of a name lookup — there is no cross-level search to model |

So the question "which archive are this model's textures in?" is answered by "which
archive did this model's bytes come from?" — and nothing else. That is a fact the
renderer already has: `FindMeshData(modelId, &sourceRes)` returns the archive path it
read the `.mef` from, and the ATTA path does the same for every sub-model.

### 9.2 What changed

* `source/renderer/model_texture_resolution.{h,cpp}`: `ModelArchiveOwnerLevel(resPath)`
  reads the level back out of `…\level<N>\models\level<N>.res` (0 for the shared
  `common` archive or anything unrecognised), and `TextureOwnerLevelForModel(meshLevel,
  textureIdOwnerLevel)` states the rule: the model's archive wins when it names a level,
  and the texture id's DAT owner — this task's rule — is only the fallback for a mesh
  read from `common`.
* `source/renderer/renderer_objects_texture.cpp`: `GetOrLoadTexture` takes the level,
  uses it for the owner-first ordering *and* in the texture cache key (one id is a
  different image in different levels' archives, so two models from two levels must not
  share one cached texture). `ApplyTexturesToMesh` takes `meshOwnerLevel` and passes it
  to every lookup.
* `source/renderer/renderer_objects_mesh.cpp`: `ApplyTexturesToMesh(…,
  ModelArchiveOwnerLevel(meshRes))` — the archive `FindMeshData` answered from.
* `source/renderer/renderer_objects_atta.cpp`: every ATTA sub-model gets
  `ModelArchiveOwnerLevel` of *its own* resolved archive (5 call sites); the two
  loose-file call sites pass 0, because a `.mef` on disk is not in any level archive.

### 9.3 Measured on the retail install

Per level, over the (model, texture-id) pairs that **that level's own texture archive
defines** — i.e. the pairs where the engine has an answer — counting the lookups whose
bytes are the level's own copy:

| preview level | ids defined | this task's rule (texture-id owner) | new rule (model's archive) |
| --- | --- | --- | --- |
| 1 | 4428 | 4428 | 4428 |
| 2 | 3237 | 3237 | 3237 |
| 3 | 5114 | 5075 | 5114 |
| 4 | 4231 | 4143 | 4231 |
| 5 | 4080 | 4080 | 4080 |
| 6 | 5580 | 5525 | 5580 |
| 7 | 5469 | 5469 | 5469 |
| 8 | 5925 | 5925 | 5925 |
| 9 | 4767 | 4767 | 4767 |
| 10 | 5113 | 5052 | 5113 |
| 11 | 5638 | 5638 | 5638 |
| 12 | 5294 | 5200 | 5294 |
| 13 | 5002 | 4941 | 5002 |
| 14 | 3871 | 3871 | 3871 |
| **total** | **67749** | **67351 (398 wrong)** | **67749 (0 wrong)** |

Foreign models (a model the preview level's archive does not contain, e.g. the issue's
`001_02_1` in a level-2 session — measured separately): the mesh is read from its home
level's archive, so the new rule hoists that level for every one of its ids instead of
the first level whose DAT lists each id. 3,693 (model, id) pairs across the retail
corpus resolve from a different level under the new rule than under the old one; 22 of
them bind *different bytes* in a level-2 session, and in each of those the old rule
bound another level's copy (e.g. `020_01_1`'s `018_03_1`: level 4's 4128 B image under
the new rule, level 1/7's 65568 B sheet under the old one).

A discarded intermediate rule — hoisting the level that the *DAT* first lists the
**model** in — is recorded here because it was measured and rejected: it was wrong on
22 of 4428 ids at level 1, 13 at level 2, 13 at level 7 and 72 at level 12, because a
model listed by an earlier level's DAT is often read out of the current level's
archive. Only the archive the bytes actually came from is correct.

### 9.4 Verification status

| Claim | Status |
| --- | --- |
| The rule and the path parser | **Verified** — `tests/test_character_texture_resolution.cpp`, 3 new tests (`TheModelsOwnArchiveDecidesTheTextureArchive`, `ModelArchiveOwnerLevelReadsTheArchivePath`, `RetailModelArchiveBeatsAnotherArchivesCopy`), 7/7 in the suite |
| Retail numbers in §9.3 | **Verified** by read-only probes over `/Users/haseeb-mir/Downloads/IGI` (15 DATs, 15 texture archives, 14 model archives); the probes are scratch tools, the assertions they support are in the committed suite |
| The pre-existing retail counter in `RetailArchivesCarryDifferentImagesUnderOneId` | **Fixed while here** — it looked up `"<id>.tex"` in an index keyed by `"LOCAL:textures/<id>.tex"`, so it counted 0 of 13 and the `EXPECT_EQ` was vacuous (0 == 0). With the correct key it reports 13 of 13 and passes |
| The wiring (`renderer_objects_*.cpp`) | **Compile-verified only on this host** — those TUs are not host-portable; the Windows cross-build is the compile+link proof, and no frame was rendered |
| "The on-screen symptom is gone" | **Not verified** — needs a Windows viewport run, as in §6 |

