# Declared-position fallback for task types the editor does not model

Fixes `docs/KNOWN_ISSUES.md` §2 "📍 Missing Position" — *"Certain objects or entities are
loaded without valid coordinates, defaulting to the map origin."*

All evidence in this document comes from the two sources named in the task: the retail
game data shipped with IGI (`MISSIONS/location*/LEVEL*/objects.qsc`) and the decompiled
engine in `/Users/haseeb-mir/Documents/Code/IGI-RE`. No external sources were used.

---

## 1. Symptom, measured

Measured over the 14 retail level scripts (`MISSIONS/location0/LEVEL1..14/objects.qsc`):

| Quantity | Value |
| --- | --- |
| `Task_New` lines in the corpus | **2944** |
| lines whose type has no explicit branch in `LoadRecursive` | **2438** |
| …of those, lines whose type really carries a world position at args 3/4/5 | **1757** |

Per type, the 1757 positioned lines are:

| type | lines | type | lines |
| --- | ---: | --- | ---: |
| `TerrainLightMap` | 813 | `EditVariable` | 15 |
| `EditRigidObj` | 582 | `AreaActivate` | 15 |
| `EditCamera` | 111 | `TextureModifier` | 13 |
| `StatusMessage` | 73 | `AmmoPickup` | 13 |
| `Building` | 57 | `Terminal` | 6 |
| `ComputerHilight` | 45 | `Smoke` | 6 |
| `LevelFlow` | 3 | `LevelTimer` | 3 |
| `HeightMap` | 2 | | |

Every one of those objects loaded at `(0,0,0)`.

## 2. Why this is data loss, not just a display bug

`source/app_level.cpp` runs `SnapObjectsToTerrain()` immediately after a level loads, and
the save path writes the object's current `pos` back to the QSC. An object that loaded at
the origin is therefore snapped to the terrain height **at the origin** and then persisted
there — the authored coordinates are destroyed the first time the level is saved. This is
the concrete reason the fix matters beyond "markers show in the wrong place".

## 3. Root cause

`LevelObjects::LoadRecursive()` (`source/level/level_objects.cpp`) dispatches on the task
type through a chain of `if`/`else if` blocks — `isSplineObj`, `isGraph`, `isHumanAI`,
`isWeaponChild`, `isMissingGeneric`, `isPickup`, … — and ends in a bare `else` that read
nothing but the task id. Any type outside the chain (all 1757 lines above) kept
`pos = (0,0,0)`.

The root cause recorded in `KNOWN_ISSUES.md` — "unmapped coordinate offsets in the parsed
binary QSC/QVM files" — is **refuted**: the parser is not the problem. `QSC::Load` parses
these lines correctly and `obj.argTokens` holds the real values (this is exactly what the
text parser `ParseTaskLine` and the property inspector already use). The loader simply had
no branch that read them.

## 4. The oracle: the level declares its own parameter layout

Every retail level script declares the layout of every task type it uses:

```
Task_DeclareParameters("TerrainLightMap", "Position", "ObjectPos", "Level", "Int32",
                       "Size", "Int32", "TextureIndex", "Int32",
                       "GeneratedWhenCreated", "bool8");
```

`Task_DeclareParameters` places the first declared field at QSC **arg 3** (args 0–2 are
id, type, note) — the same offset rule already implemented by
`TaskSchemaNS::ParseDeclaration` (`source/level/task_schema.cpp`), which starts `off = 3`.

Across all 14 scripts, the types that carry a world position are **exactly** the types
whose first declared parameter is `("Position","ObjectPos")`, and the types that merely
happen to have three numbers at args 3/4/5 declare something else there:

| type | first declared parameter | args 3/4/5 in the retail data |
| --- | --- | --- |
| `TerrainLightMap` | `Position` / `ObjectPos` | `22020098.0, -59768828.0, 174500512.0` |
| `LightmapInfo` | `Texture scale` / `Real32` | `1, 1, 450` |
| `PatrolPathCommand` | `Command` / `Int32` | `0, 240` |
| `AddAmmo` | `ID` / `EnumString32` | `"AMMO_ID_919", 49` |
| `Dirlight` | *(bool parameters)* | `TRUE, TRUE, 18.0` |
| `FlatSky` | `Fog Amount` / `Real32` | `0.941667, 4.0, 100.0` |
| `DirlightKeyframe` | `Beta` / `Angle` | `0.523599, -4.92183, 1` |

So a blanket "read args 3/4/5 when they are numbers" rule would have invented coordinates
for `LightmapInfo` (31 lines), `Dirlight` (3), `FlatSky` (3), `DirlightKeyframe` (3),
`LevelFlow` (3, genuinely `0,0,0`) and others. The declaration is what distinguishes them.

## 5. Fix

Two parts, both in `source/level/level_objects.cpp`:

1. **Candidate read.** The final `else` of `LoadRecursive` now reads args 3/4/5 when they
   are `DBL` tokens and marks the object
   (`LevelObject::declaredPositionCandidate`, declared in `source/level/level_objects.h`).
   Objects that took an explicit branch are never marked, so a type whose real position is
   elsewhere is untouched — `SplineObjWaypoint` (position at args 9/10/11) and `Door`
   are the important cases.

2. **Validation pass.** Immediately after the recursive walk, `Load()` collects the
   level's own `Task_DeclareParameters` objects into `type → TaskSchema`, then for every
   marked object calls `TaskDeclarationHasPositionAt3(schema)`:

   * declaration puts `ObjectPos` at arg 3 → keep the candidate as the position and mirror
     it into `original_pos`;
   * otherwise (no declaration, or a different first parameter) → revert `pos` and
     `original_pos` to `(0,0,0)`, i.e. exactly the pre-fix behaviour rather than inventing
     coordinates from unrelated parameters.

   A single `INFO` log line reports how many objects were placed and how many candidates
   were reverted.

`TaskDeclarationHasPositionAt3` matches on the **type** name (`ObjectPos`) rather than the
field name, so a differently-worded position field still counts.

## 6. Deliberate non-changes

* **`PatrolPath`** has no `Task_DeclareParameters` line in any retail level and its args
  3/4/5 are the first child's tokens (`Task_New(-1, "PatrolPathCommand", …`), not
  coordinates. It is left at the origin; its commands are separate objects.
* Types with an explicit branch keep that branch's mapping even when their declaration
  says `ObjectPos` at arg 3, because the explicit mapping is what the rest of the editor
  (save path, inspector, gizmos) is written against.
* `original_pos` is only set for objects the fallback actually placed, so change detection
  and fallback matching behave as before for everything else.

## 7. Verification

| Check | Result |
| --- | --- |
| `tests/test_declared_position_fallback.cpp` (new, 5 tests) | PASS — 15 positional declarations accepted, 15 non-positional rejected, undeclared type not guessed, renamed position field accepted, position-not-at-arg-3 rejected |
| Host lane | `PASS test_declared_position_fallback 5 passed 0 failed`, `OK: 5 tests passed in 1 suites`, exit 0 |
| Cross-compile of the changed TUs | `python3 tools/igi-re-parity/tu-check.py` — see the run log referenced in the task report |
| Retail declaration strings | every declaration embedded in the test is copied verbatim from the shipped scripts, not paraphrased |

**Not verified:** no editor session was run on Windows, so the visual result (markers and
lightmap tiles at their real coordinates) is not confirmed by eye, and the end-to-end
`Load()` path cannot execute in the host-native lane because `QSC::Load` and
`LevelObjects::Load` depend on `common.cpp` (`File_LoadText`/`MEM_ALLOC`) and `logger.cpp`,
which are Windows-only. The wiring is therefore compile-verified, and the decision it rests
on is what the new tests pin down against real retail declarations.
