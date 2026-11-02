# TDD Seams for the Gameplay Runtime

The implementation tests public behavior at these seams. Tests should not
inspect private fields or couple themselves to the chosen data structure.

| Seam | Observable contract |
| --- | --- |
| `GameClock` | Absolute 30 Hz deadlines, bounded catch-up, pause/exclusion, render boundaries, and reset behavior. |
| `PlayerCollision` | Ground/roof probes, wall blocking and sliding, stance clearance, and finite obstacle resolution. |
| `PlayerMotion` / `PlayerController` | Verified-reference gravity, ladder-slide damping, air-control basis, root-motion transform order, input-to-motion behavior, stance transitions, and collision-safe position. |
| `PlayerAnimationDriver` | Vanilla locomotion-state selection, fixed 160 ms clip advancement, native-unit root motion, ladder event 8, and top-transition timing before the world tick. |
| `WeaponViewSway` / `RuntimeWorld` | Verified-reference 11-tick weapon lowering/raising, active-slot swap at the lowered boundary, and render-snapshot transition state. |
| `AudioAssetResolver` / `WeaponSystem` | Loose level sound precedence, case-insensitive packed `SOUNDS.RES` extraction/cache, active-level archive precedence, missing-sound behavior, and vanilla-QVM weapon sound names. |
| `RuntimeWorld` / `RuntimeRenderer` | Fixed-step firearm muzzle cue after a successful player shot, deterministic two-tick decay, and snapshot isolation. |
| `AiSystem` / `RuntimeRenderer` / `App` | Moving guard transforms, lifecycle state, and animation-request metadata cross the immutable render boundary before gameplay scene synchronization. |
| `AiSystem` | Clearing a runtime AI world resets the fixed-step simulation tick used by delayed patrol/QVM behavior. |
| `WeaponViewRecoil` / `RuntimeWorld` | Inferred three-tick visual recoil recovery, firearm classification, and render-snapshot transfer of the weapon kick. |
| `MissionObjectiveLoader` / `LevelFlow` | Vanilla six-slot objective parsing, resource-key/expression preservation, localized text fallback, last-valid-definition selection, and authored definition progression. |
| `LevelFlow` / `RuntimeRenderer` | Authored objective `LinkTaskId` resolution to copied task positions and immutable presentation transfer for the active primary objective. |
| `WindowInputRouter` / `RuntimeWorld` / `RuntimeRenderer` | Grounded map-computer rising-edge toggle, held-key suppression, vanilla Right Ctrl/C binding separation, player input freeze while open, six-row objective snapshot, and immutable HUD presentation data. |
| `RuntimeMapComputerProjection` / `RuntimeRenderer` | Deterministic top-down world-to-panel projection, centered player marker, and color-coded authored objective marker placement. |
| `App` interaction bridge / `RuntimeWorld` | Interactable events publish stable authored state keys and fixed-step objective evaluation consumes them without coupling mission expressions to renderer objects. |
| `MissionStateLoader` / `RuntimeWorld` | Authored AreaActivate geometry and EditVariable expressions are copied into runtime-owned state; area occupancy and add-before-sub latching occur before objective evaluation. |
| `RuntimeWorld` actor mission state | Authored player/guard death variables update after simulation actors tick; switch and terminal edge fields remain true only for their fixed simulation tick. |
| `LadderPlacement` / `LadderTraversal` | Magic-vertex mount geometry, activation selection, rung boundaries, top transitions, sliding interruption, and deterministic dismount. |
| `MagicObjectRegistry` | First-definition-wins parsing of `DefineMagicObj` rows and named `TASKTYPE_LADDER` resolution without hard-coded process-local IDs. |
| `TaskTree` | Registration, parent ownership, lifecycle order, targeted messages, and safe destruction. |
| `QvmInterpreter` | Malformed-program rejection, bounded execution, native calls, stack safety, and deterministic reset. |
| `GameplayHost` / `RuntimeWorld` | Isolated session start/tick/restart/close, mission state, and no mutation of editor snapshots. |
| `RuntimeSpawn` / `App` | Authored HumanPlayer task-id-zero selection, first-player fallback, and explicit editor-camera/level-start fallback boundary. |
| `RuntimeAssetTarget` | Gameplay simulation keeps consuming the mutable runtime snapshot while the editor window repaints. |
| `RuntimeWorld` ladder asset boundary | Gameplay setup owns a transformed placement copy; reset clears it so editor/source objects cannot become mutable runtime ladders. |
| `RuntimeWorld` ladder interaction | Nearest authored mount, deterministic rung input, top transition, slide-to-ground, dismount, and authored root-motion event completion. |
| `GameplayHost` / `WindowInputRouter` | F6/F7 focus handoff changes input ownership without restarting or resetting the active runtime session. |
| `RenderTarget` | Gameplay-window presentation remains distinct from render-only editor repaints. |
| `RuntimeMapComputerCamera` / `App` | Vanilla open/boot/open and close/shutdown/descend phases, live vantage/eye hand-off, geometric FOV zoom, deterministic display envelopes, and gameplay-viewer snapshot hand-off. |

The user-requested TDD workflow is applied as a vertical slice: each new test
is added before the smallest production change that makes that behavior pass.
