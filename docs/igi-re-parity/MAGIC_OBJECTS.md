# Magic-object expansion from `ATTA` records (issue #67)

Parity record for `#67 magic-object table drives attachment spawns`: the
expansion rule that turns a placed model's `ATTA` records into the tasks retail
IGI creates, the recursion rule that decides how far the walk goes, and the
measured behaviour on the shipped level-1 data.

Every rule and offset below is cited to IGI-RE, the verified decompilation of
the retail `IGI.exe`; every editor claim is cited to a file and line in this
repository. Companion record: `docs/igi-re-parity/ATTA_CHILDREN_ROTORS.md`
(issue #40 — the `ATTA` record layout and the parent → child transform).

Oracle: `/Users/haseeb-mir/Documents/Code/IGI-RE` (read-only).
Editor: this repository, branch `fix/igi-re-known-issues`.

> Status legend — **verified** = confirmed against IGI-RE source and/or the
> shipped game data with a command whose output is quoted; **unverified** =
> cannot be confirmed in this environment (there is no runnable retail target
> and the installed `IGI.exe` is not the IGI-RE-supported build, so no
> differential harness exists).

---

## 0. TL;DR

* A placed object expands its model's `ATTA` records through
  `Sector_CreateMagicObjs` (IGI-RE `src/shadow/sector.c:538-687`). Each record's
  **name** is looked up in the `DefineMagicObj` table, and the task that is
  created uses the **definition's model**, not the record's name
  (`src/shadow/sector.c:667`, `src/magicobj/rigidcoll.c:484`).
* The task is created as a child **Sector** when the spawned model's mesh has
  portals — the engine's test is the mesh block's `portals` pointer at `+0x98`
  (`src/shadow/sector.c:673-676`, layout in `src/sector/sector.h:33-45`).
  Otherwise it is created as the definition's own task type.
* Recursion is **not** a property of the portal test. Both branches expand the
  child's own `ATTA` list exactly once, and whether a spawned task expands
  further depends on its **task type**: 13 of the 28 types in the retail table
  have a create handler that calls `RigidDynCubeObj_CreateMagicObjs`
  (§3). The deepest walk observed on level 1 is 2 spawn levels.
* Retail data, measured with the corpus test in this repository:
  `MAGICOBJ.QVM` yields **374** definitions (its string table holds 387
  entries), all 374 use their own name as their model, and the level-1 garage
  models expand to **26** (`410_01_1`) and **34** (`404_01_1`) tasks.
* **The issue's "64 pieces" does not reproduce.** No level-1 model produces 64
  under the engine rule; the largest is `400_01_1` at 90 (§6).

---

## 1. The spawn pipeline

`Sector_CreateMagicObjs` (HOOK `0x4DFA50`, `src/shadow/sector.c:538-687`) is the
top-level walk. It has exactly one caller: `SectorObject_Update`
(`src/shadow/sector.c:336`), on the first update of a placed sector object, and
it runs once per object (`so->built`, `src/shadow/sector.c:327-340`).

| Step | Engine code | Behaviour |
| --- | --- | --- |
| Attachment count | `src/shadow/sector.c:544` | `ModelLod_GetAttachCount(s->o.model[0])` |
| Spawn position | `src/shadow/sector.c:562-567` | `arg.pos = M * att->pos + s->o.pos` |
| Local frame | `src/shadow/sector.c:569-605` | `a[0], a[3], a[6]` plus two cross products, each vector orthonormalized |
| World frame | `src/shadow/sector.c:607-653` | `M * local`, re-orthonormalized |
| Definition lookup | `src/shadow/sector.c:656-658` | first `strcmp(att->name, gMagicObjDefs[j].name) == 0` |
| Undefined name | `src/shadow/sector.c:659-663` | `Warning_Show("Magic object type \"%s\" not defined (Created from model \"%s\")")` then `continue` — **no task** |
| Model | `src/shadow/sector.c:667` | `arg.model = def->model` |
| Model must resolve | `src/shadow/sector.c:668-672` | `VirModel_Find(def->model)`; miss → `Warning_Show("Magic object model \"%s\" not found")` then `continue` — **no task** |
| Container / type | `src/shadow/sector.c:673-676` | `if (*(int *)(model_info(model) + 0x98))` → child **Sector** (`gSectorType`) in `so->sectors`; else `def->type` in `s->o.attached` |
| Event 3 | `src/shadow/sector.c:682-684` | `fn = HANDLER(RigidDynCubeObj_GetEvent3(), t->type); if (fn) fn(t, &arg2);` — see §3 |

`DefineMagicObj` itself is `src/magicobj/magicobj.c:189-220`; the record is
`struct MagicObjDef { char name[16]; char model[16]; uint16_t type; ... }`
(`src/magicobj/magicobjtypes.h:59-65`), so a definition carries a name and a
model that are *separate* 16-byte fields.

The editor already had the lookup (issue #40) and the registry
(`source/runtime/magic_object_registry.*`); what was missing was a consumer of
`def->model` and the recursive walk. That is what
`source/runtime/magic_object_expand.*` now provides.

---

## 2. What `model_info(model) + 0x98` actually is

The `+0x98` test is a **pointer test on the mesh's portal array**, not a
"has sectors" integer flag.

| Fact | Evidence |
| --- | --- |
| `model_info` is the LOD-0 mesh block | `src/shadow/sector.c:387-392` — `*(char**)(*(char**)((char*)model[0] + 0xC) + 0xC)` |
| `model_bounds` returns `model_info(model) + 0x50` | `src/shadow/sector.c:395-398` |
| The same block has `Vec3f center` at `0x50` and `float radius` at `0x5C` | `src/sector/sector.h:30-31` |
| The same block has `short portalCount` at `0x84` and `Portal *portals` at `0x98` | `src/sector/sector.h:33,37` |
| `lod_mesh(VirModel *m, int i)` computes the identical address | `src/sector/sector.h:39-42` — `*(SectorMesh **)(*(char **)((char *)m->lods[i] + 0xC) + 0xC)` |
| The offset is asserted by the decompilation itself | `src/sector/sector.h:45` — `_Static_assert(offsetof(SectorMesh, portals) == 0x98, "SectorMesh")` |

So `*(int *)(model_info(model) + 0x98) != 0` reads the low word of the
`portals` pointer: the branch is taken when the spawned model **has portals**,
i.e. when it is a portalised interior. `model_bounds` independently confirms
that `model_info` returns this struct, because `+0x50` is exactly `center`.

In MEF terms the portal array is filled from the `TROP` chunk: every retail
`ATTA`-carrying model ships a `TROP` chunk, and the record count is
`size / 20`. The corpus test therefore treats "has sectors" as "the parsed
geometry has at least one portal record" — the editor's own MEF parser exposes
it as `ParsedGeometry::portals` (`source/renderer/mef_native.h:104,140`,
parsed at `source/renderer/mef_native.cpp:827`).

Measured on level 1: `410_01_1` has `TROP size=60` (3 portals) and `404_01_1`
has `TROP size=20` (1 portal), while the props they spawn (`318_01_1`,
`219_01_1`, `300_01_1`, `410_03_1`, …) ship `TROP size=0` — a present but empty
chunk, which is why "the chunk exists" is **not** a usable predicate and the
record count is.

---

## 3. The recursion rule

There are two walkers, and they do not have the same rules.

| Walker | Entry | Rules |
| --- | --- | --- |
| `Sector_CreateMagicObjs` (`src/shadow/sector.c:538-687`) | placed object | undefined name → warn + skip; model must resolve; `+0x98` portal test chooses Sector vs own type |
| `RigidDynCubeObj_CreateMagicObjs` (`src/magicobj/rigidcoll.c:383-496`) | every spawned Sector, and the create handler of expanding types | **no** `VirModel_Find` check (`:485` always creates the task); **no** portal branch, so it never creates a Sector; undefined name → `Error_Show` then `for (;;) ;` (`:475-480`) |

A spawned **Sector** expands through `Sector_Create`
(`src/shadow/sector.c:183-195`):

```c
s->o.model = VirModel_Find(arg->model);
s->o.attached = MagicObjContainer_New(NULL, s);
RigidDynCubeObj_CreateMagicObjs(s->o.attached, &s->o, &s->o);
```

A spawned **magic object** expands through its own create handler. That is the
per-type property the editor models (`igi::MagicObjectTypeChildExpansion`):

| Type | Create handler | Expands |
| --- | --- | --- |
| `TASKTYPE_AISTATIONARYGUN` | `binocular/stationarygun.c:194` → `:149` | always |
| `TASKTYPE_ALARMLIGHTREFLECTOR` | `fxobj/alarm.c:117` → `:148` | always |
| `TASKTYPE_CABARREL`, `TASKTYPE_CACLIP` | `statgun/gunca.c:67`, `:74` → `:100` | always |
| `TASKTYPE_EXPLODEMAGICOBJ` | `elevator/explodemagicobj.c:97` → `:84` | always |
| `TASKTYPE_GLASS` | `glass/glass.c:103` → `:84` | always |
| `TASKTYPE_GUNCLIP`, `TASKTYPE_GUNMOVINGPART` | `guncasing/gunpart.c:79`, `:92` → `:226` | always |
| `TASKTYPE_GUNX2` | `comprun/gunx2.c:48` → `:71` | always |
| `TASKTYPE_LADDER` | `props/ladder.c:109` → `:85` | always |
| `TASKTYPE_RADARDISH` | `glass/radardish.c:111` → `:90` | always |
| `TASKTYPE_ROTOR` | `rotor/rotor.c:196` → `:105` | always |
| `TASKTYPE_MAGICOBJ` | `haze/magic.c:85` → `:108-109` | **only if the model resolved** (`if (model)`, else warns at `:99-100`) |
| the other 15 types in the table | — | never |

The table above was extracted mechanically: for every `TASKTYPE_*` the retail
table uses, `Script_RegisterConstant("TASKTYPE_X", gXType)` gives the task type,
`TaskType_SetHandler(gXType, 0, Handler)` gives the create handler (slot 0), and
the handler expands when its body calls `RigidDynCubeObj_CreateMagicObjs`
(HOOK `0x4C4950`). `TASKTYPE_SCAMERALENSVIEWCONE` has no create handler in the
decompilation and is treated as non-expanding.

Two consequences worth stating plainly:

1. **The portal test adds expansions only for non-expanding types.** A
   portalised model spawned as a Sector expands through `Sector_Create` even
   when its own type would not.
2. **The walk is shallow.** `RigidDynCubeObj_CreateMagicObjs` has no portal
   branch, so a Sector child can only be created at the top level; and the
   event-3 call at `src/shadow/sector.c:682-684` spawns nothing, because
   `TASKTYPE_MAGICOBJ` registers `TASK_NOTHING` for that slot
   (`src/haze/magic.c:88`) and `gSectorType` registers no event-3 handler at all
   (`src/sector/sector.c:895-901`). On level 1 the ATTA graph is two levels
   deep, which is why every measured expansion reports `max_depth=2`.

The engine has no cycle guard, so a cyclic `ATTA` graph hangs retail IGI. The
editor's walk keeps a path set and stops instead.

---

## 4. What the editor implements

`source/runtime/magic_object_expand.h` / `.cpp` (pure, host-portable; no GL, no
filesystem — model data arrives through `MagicObjectExpansionSource` callbacks):

| Rule | Implementation |
| --- | --- |
| name → `def->model` | `spawn.model_id = definition->model_id` |
| undefined name | counted, listed, **no spawn** |
| top-level missing model | counted, listed, **no spawn** |
| nested missing model | spawn recorded with `model_available=false`; expansion suppressed |
| portal test | `spawn.child_is_sector`, top level only |
| recursion | `child_is_sector` **or** the per-type table above |
| cycle / depth | path set + `kMagicObjectMaxDepth = 16` |
| placement | `spawn.pos` / `spawn.m` carry the `ATTA` record verbatim; the caller places with the existing #37 helper `igi::IgiAttaChildWorld` (`source/renderer/atta_children.h:73-88`) — the frame maths is **not** re-derived |

Deliberate divergences from retail, all on paths where the engine cannot
continue anyway:

* An undefined name inside a nested walk hard-errors and spins in retail
  (`src/magicobj/rigidcoll.c:475-480`). The editor counts and skips it.
* A missing model for `TASKTYPE_GLASS`, `TASKTYPE_EXPLODEMAGICOBJ`,
  `TASKTYPE_ROTOR` or `TASKTYPE_CABARREL`/`CACLIP` makes retail `Error_Show` and
  spin (`glass/glass.c:66-70`, `elevator/explodemagicobj.c:68-72`,
  `rotor/rotor.c:92-96`, `statgun/gunca.c:89-93`). The editor records the spawn
  and stops there.

---

## 5. Retail measurements

Command (acceptance #2 of this task):

```sh
IGI_HOST_NO_CACHE=1 IGI_HOST_TESTS=tests/test_magic_object_expand.cpp \
  bash scripts/run-host-tests.sh
```

The corpus tests print, on the retail install at
`/Users/haseeb-mir/Downloads/IGI`:

```
[magic-objects] definitions=374 stringTable=387 identifiers=33 distinctTypes=28
[magic-objects] string-table entries that are not a definition name: 13
[magic-objects]   123_01_1
[magic-objects]   300_02_1
[magic-objects]   456_13_1
[magic-objects]   458_15_1
[magic-objects]   601_05_1
[magic-objects]   603_03_1
[magic-objects]   614_03_1
[magic-objects]   700_12_1
[magic-objects]   explo_02_m
[magic-objects]   explosion0
[magic-objects]   radardishsounds
[magic-objects]   tank_turret
[magic-objects]   woodcrash_01
[magic-objects] definitions with model != name: 0 of 374
[magic-objects] garage models named by the level script: 404_01_1 410_01_1
[magic-objects] === garage model 404_01_1 ===
definitions=374 models_walked=2 attachments_seen=34 spawns=34 undefined_names=0 missing_models=0 max_depth=2
spawns_by_type: TASKTYPE_EXPLODEMAGICOBJ=2 TASKTYPE_MAGICOBJ=32
spawns_by_model: 207_01_1=10 208_01_1=3 219_01_1=10 300_01_1=2 315_01_1=1 316_01_1=3 317_01_1=3 318_01_1=1 404_02_1=1
[magic-objects] === garage model 410_01_1 ===
definitions=374 models_walked=2 attachments_seen=26 spawns=26 undefined_names=0 missing_models=0 max_depth=2
spawns_by_type: TASKTYPE_EXPLODEMAGICOBJ=4 TASKTYPE_GLASS=2 TASKTYPE_MAGICOBJ=20
spawns_by_model: 207_01_1=4 210_01_1=2 219_01_1=8 300_01_1=4 301_01_1=1 315_01_1=1 318_01_1=3 410_02_1=1 410_03_1=2
[magic-objects] level1 scan: models_with_spawns=43 attachments=429 spawns=428 undefined=0 missing=1 models_at_64=0
[magic-objects]   400_01_1 -> 90 spawns
[magic-objects]   400_20_1 -> 45 spawns
[magic-objects]   404_01_1 -> 34 spawns
[magic-objects]   410_01_1 -> 26 spawns
[magic-objects]   409_01_1 -> 21 spawns
[magic-objects]   definition model not in the level models dir: 313_04_1
```

Notes on each number:

* **374 definitions, 387 string-table entries.** The issue's "387" is the QVM
  string-table size, not the definition count. The test enumerates the
  difference: 374 entries are definition names, and the other 13 are
  `123_01_1`, `300_02_1`, `456_13_1`, `458_15_1`, `601_05_1`, `603_03_1`,
  `614_03_1`, `700_12_1`, `explo_02_m`, `explosion0`, `radardishsounds`,
  `tank_turret`, `woodcrash_01` — model and sound literals used elsewhere in the
  same QVM's scripts, not definition names. Both counts are asserted in
  `tests/test_magic_object_expand.cpp`
  (`RetailTableMatchesTheMeasuredCounts`) so the distinction cannot drift.
* **`model == name` for all 374.** The "spawn `def->model`" rule is therefore
  observationally identical to the editor's previous name-based load *on this
  corpus*; the wiring is a correctness fix for the rule, not a visible change on
  shipped data. This is stated as a measurement, not as a fix.
* **The garage models come from the level's own script.** `objects.qsc` places
  four `"SmallGarage"` buildings: three with model `410_01_1` and one with
  `404_01_1`; the corpus test reads those model ids out of the script rather
  than hard-coding them.
* **`410_01_1` = 26 spawns** = 9 direct `ATTA` records + the 17 records of its
  one portalised child `410_02_1`. **`404_01_1` = 34** = 21 + 13
  (`404_02_1`). Everything else they spawn is a leaf.
* **One dangling definition.** An `ATTA` record of `313_02_1` names `313_04_1`,
  which is not a loose `.mef` in the level's models directory or in
  `MISSIONS/location0/COMMON/MODELS/models`, and `LEVEL1.RES` carries 415
  `LOCAL:models/*.mef` entries but no `LOCAL:models/313_04_1.mef`. Retail's
  `VirModel_Find` fails, warns and skips that attachment
  (`src/shadow/sector.c:668-672`) — the counter records the one skipped record.

---

## 6. The issue's "64 pieces"

The issue's headline claim is that a level-1 garage with no script children
still ends up with 64 pieces. **That number does not reproduce on this corpus**,
under any rule that was tried:

| Rule | `410_01_1` | `404_01_1` | Level-1 maximum |
| --- | --- | --- | --- |
| Engine rule as implemented (portal test + per-type expansion) | **26** | **34** | `400_01_1` = 90 |
| Unconditional recursion at every level (naive) | 26 | 34 | 90 |
| The editor's unbounded `DrawAttachmentsRecursive` walk over every `ATTA` child | 26 | 34 | 90 |
| Models whose expansion is exactly 64 | — | — | **0** |

The level-1 `ATTA` graph is only two levels deep — the models the garage spawns
(`318_01_1`, `219_01_1`, `300_01_1`, `410_03_1`, …) have no `ATTA` records of
their own — so the portal test and even unconditional recursion give the same
answer here. The difference between the two rules is real but only shows up on
data that nests three levels; `tests/test_magic_object_expand.cpp`
(`PortalisedChildBecomesASectorAndExpandsOnce`, `DepthCapStopsTheWalk`) pins it
with a synthetic graph so the shallow corpus cannot hide it.

The measured numbers to carry forward are therefore **26** and **34**, with the
arithmetic above; 64 is not reproducible from level 1 and nothing was tuned to
reach it.

---

## 7. What changed in the editor

| File | Change |
| --- | --- |
| `source/runtime/magic_object_expand.h` / `.cpp` | new: the expansion rule, the per-type table, the walk, and an `Inventory()` printer |
| `tests/test_magic_object_expand.cpp` | new: 18 tests — synthetic branch coverage plus the corpus cross-checks above |
| `source/renderer/renderer_objects_atta.cpp` | additive wiring: `MagicObjectTable()` (cached registry), `MagicObjectModelId()`, and the resolved model used where a spawned sub-model is loaded, drawn, recursed into, or packed as a `.res` dependency |
| `CMakeLists.txt` | the test added to the `igi_tests` list and the module to its source pool (additive; the `source/runtime/*.*` glob already feeds `igi-editor`) |

What deliberately did **not** change:

* `AttachInfo::modelId` stays the `ATTA` **name**. The editor's name-keyed
  paths — rotor preview (`RotorAttachmentIds()`), death-zone filtering,
  promoted `ATTA` records, `AddModelToLevelRes` logging — keep using the name,
  which is what the level data references and what the engine uses for the
  lookup.
* Placement and rotation are untouched: the spawn still goes through
  `igi::IgiAttaChildWorld` with the `ATTA` record's own `pos`/`m`
  (`source/renderer/renderer_objects_atta.cpp:1257-1261`).
* The registry (`source/runtime/magic_object_registry.*`) was not modified.

Because `model == name` for all 374 retail definitions, the wiring is expected
to be a no-op on shipped data. It is still the documented rule, and the module's
corpus test proves the resolution path is exercised (the synthetic test
`SpawnUsesTheDefinitionsModelNotTheAttachmentName` pins the renamed case).

---

## 8. Verification status

**Verified**

* The expansion rule, per-type table and recursion depth, against IGI-RE with
  the citations in §1–§3.
* The `+0x98` identification, by the `center`@`0x50` agreement between
  `model_bounds` and `SectorMesh` plus the decompilation's own `_Static_assert`
  (`src/sector/sector.h:45`).
* The pure module and its corpus test: `IGI_HOST_NO_CACHE=1
  IGI_HOST_TESTS=tests/test_magic_object_expand.cpp bash scripts/run-host-tests.sh`
  → `18 passed, 0 failed`, exit 0, linked strictly (no lazy-link fallback, no
  excluded engine source).
* The retail numbers in §5, printed by that run.
* The changed renderer TU compiles under the cross-build flags:
  `python3 tools/igi-re-parity/tu-check.py source/renderer/renderer_objects_atta.cpp`
  → `checked 1 translation unit(s), 0 failed`.

**Unverified**

* **The renderer path is compile-verified only.** The host has no usable GL
  context (`docs/KNOWN_ISSUES.md` §1c) and cannot run the installed
  `IGI.exe` (hash `dea34892…` ≠ the IGI-RE-supported EU build `ae93669a…`), so
  nothing here proves that the editor now *draws* the 26/34 pieces. What is
  proven is that the rule is implemented, that the corpus test drives it with
  real data, and that the wiring compiles.
* **The level `.res` archives are not read by the corpus source.** The test's
  `model_available` callback looks at loose `.mef` files in the level models
  directory and in `COMMON`; the editor's real resolver also reads the level's
  packed models. The residual counter above (`missing=1`, `313_04_1`) was
  cross-checked by hand against the archive's entry list, but the test itself
  does not parse `.RES` files.
* **`tu-check.py` cannot yet see the two new files.** They are not in
  `build-mingw/compile_commands.json`, which is regenerated by a CMake
  configure; the build-gate stream owns that tree, so the new files were
  checked instead by replaying the exact DB flags of an equivalent TU
  (`magic_object_registry.cpp` for the module, `test_menu_labels.cpp` for the
  test) with `-fsyntax-only` — both exit 0. One reconfigure makes
  `tu-check.py source/runtime/magic_object_expand.cpp
  tests/test_magic_object_expand.cpp` work directly.
* **No differential trace against retail.** No hook-level comparison of task
  counts is possible in this environment; the counts in §5 are computed by the
  rule, cross-checked against the raw `ATTA`/`TROP` record counts read straight
  from the MEF files.
