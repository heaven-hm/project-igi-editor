# Audit: engine rule modules vs. what the editor actually calls

Method: for every header under `source/runtime/` and `source/level/`, list those
included by no other source file (tests excluded). Run after commit `4bba4839`.

| Module | Callers | Finding |
| --- | --- | --- |
| `sound_emitter.*` | `RuntimeWorld`, `mission_state_loader` | `RuntimeWorld::UpdateAuthoredConditionalSounds` had a private copy of the ConditionalSound rule that diverged from `ConditionalSound_Run` (`src/vehicle/condsound.c:121-136`): a OneShot loop was stopped when its condition fell, the engine returns first (`condsound.c:128-129`). **Fixed** in `4bba4839` — now calls `igi::TickConditionalSound`; regression test `RuntimeWorldTest.AuthoredOneShotLoopIsNeverStoppedByItsConditionFalling`. `AmbientArea` tasks (162 across the retail corpus) are now loaded in `mission_state_loader.cpp` and simulated via `RuntimeWorld::UpdateAuthoredAmbientAreas()` using `igi::TickAmbientArea` (`src/aiaction/ambientarea.c:125-179`); verified by `RuntimeWorldTest.AuthoredAmbientAreaFadesInWhenPlayerEntersAndStopsWhenExits`. **Fully wired.** |
| `magic_object_expand.*` | `renderer_objects_atta`, `renderer_objects_picking` | `MagicObjectTypeChildExpansion` and the portalised child Sector rule (`src/shadow/sector.c:673-676`, `:190`, `src/magicobj/rigidcoll.c:383-496`) are now wired into `DrawAttachmentsRecursive` and `DrawAttachmentsForPicking`. The previous sibling-blocking string key (`parent>child`) is replaced by an ancestor recursion call stack (`drawn.insert(spawnModelId)` / `drawn.erase(spawnModelId)`), preventing cycle hangs while allowing sibling attachments to expand their children. **Fully wired.** |

Everything else under those two directories has at least one non-test caller.
"Has a caller" is not "matches the engine": it only rules out dead duplicates.
