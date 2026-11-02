# Project I.G.I. C++ Runtime Porting Specification

## 1. Purpose and scope

This document defines the controlled implementation of a dual-mode C++ application:

1. **Editor mode** — the existing `project-igi-editor` tooling for inspecting,
   editing, previewing, and writing I.G.I. data.
2. **Game mode** — a runtime simulation layer that uses the editor's existing
   asset and level infrastructure to run a playable I.G.I. mission.

The target is a native C++ runtime with behavior progressively validated against:

- the retail IGI1 executable and observed game behavior;
- IGI2 executable/symbol evidence where it is applicable;
- OpenIGI's C# implementation as a reference implementation and hypothesis source;
- existing level, QSC, QVM, terrain, model, and rendering tests.

This is not a claim that OpenIGI is the original Innerloop engine. OpenIGI is a
clean-room C# implementation. Its code may contain useful reverse-engineering
work, but every behavior used for high-fidelity emulation must be classified as
`verified`, `inferred`, or `placeholder` and tested accordingly.

The specification excludes reimplementing file formats that already work in the
C++ editor. It does include the runtime adapters needed to consume those formats.

## 2. Non-goals

The following are explicitly outside the first implementation milestone:

- rewriting the MEF, QSC, QVM, RES, MTP, terrain, texture, or model readers;
- deleting editor code or creating a second unrelated executable;
- claiming binary or frame-perfect parity without retail comparison evidence;
- copying OpenIGI's classes mechanically into C++ without adapting ownership,
  lifetime, coordinate systems, and existing editor data structures;
- implementing all fourteen missions before a single end-to-end mission works;
- allowing gameplay simulation to mutate the editor's source scene irreversibly.

## 3. Current baseline

The C++ editor already provides substantial infrastructure:

- level loading and writing;
- QSC parsing, editing, compiling, and writing workflows;
- QVM parsing/decompilation/compiler support;
- terrain loading, rendering, height queries, and editor ray operations;
- model, object, lightmap, and texture presentation;
- task/object hierarchy inspection and editing;
- editor camera movement and basic object/terrain collision helpers;
- automated format and regression tests.

The existing application is not yet a complete game runtime. In particular,
`edit_mode_` currently controls editor/view behavior; it does not by itself
provide a player entity, simulation clock, QVM runtime, AI runtime, weapon
runtime, mission flow, or gameplay state isolation.

The current editor collision helper is not the final player physics system. It
uses editor-camera movement, terrain grounding, and object extent checks. The
runtime must add stateful player movement, collision probes, wall sliding,
airborne movement, slopes, crouching, head clearance, interactions, and
animation/root-motion integration as evidence requires.

## 4. Evidence classification

Every ported behavior must carry one of these labels in code comments, design
notes, or tests:

| Classification | Meaning |
|---|---|
| `verified-retail` | Confirmed by repeatable IGI1 runtime observation, hook, trace, or binary comparison. |
| `verified-reference` | Confirmed in a reference implementation or format/tool behavior, but not yet proven in IGI1. |
| `inferred` | A reasoned interpretation from disassembly, symbols, scripts, or related versions. |
| `placeholder` | Temporary behavior used to make a vertical slice executable. |

IGI2 symbols are evidence, not automatic IGI1 truth. The IGI2 PDB must match its
executable by signature/age before it is used. Names and types from a matching
PDB are useful for locating systems, but function behavior, constants, layouts,
and script bindings must still be checked against the target executable.

## 5. Runtime architecture

The application is divided into three boundaries:

### 5.1 Shared asset boundary

This layer owns existing readers and immutable loaded data:

- `Level`, terrain, model, texture, animation, QSC, QVM, RES, and MTP data;
- coordinate conversion and unit conventions;
- render-resource preparation;
- read-only source-level metadata.

### 5.2 Simulation boundary

This layer owns mutable gameplay state:

- runtime entities and stable entity IDs;
- player body and camera state;
- AI state and perception snapshots;
- task tree instances and message queues;
- QVM execution contexts and native calls;
- weapons, projectiles, damage, sound events, and interactions;
- mission objectives, win/fail state, and restart snapshots.

Simulation state must not write directly into editor source objects unless an
explicit editor command requests that operation.

### 5.3 Presentation/tool boundary

This layer consumes simulation state and renders either:

- editor tools, selection, inspectors, gizmos, and terrain brushes; or
- gameplay HUD, player camera, tactical computer, crosshair, and menus.

The renderer must not become the owner of gameplay state.

## 6. Mode contract

| Concern | Editor mode | Game mode |
|---|---|---|
| Camera | Free/editor camera | Player first-person camera |
| Input | Selection, editing, tools | Movement, look, fire, interact |
| Simulation | Paused or preview-only | Fixed-step runtime simulation |
| Object state | Editable source representation | Runtime entity snapshot |
| UI | ImGui editor panels and gizmos | Gameplay HUD and menus |
| File writes | Explicit editor save commands | Disabled by default |
| Exit behavior | Normal editor state | Restore pre-game snapshot or reload |

Entering game mode must:

1. validate that a level is loaded;
2. capture the editor camera and mutable source state required for restoration;
3. construct a runtime world from the loaded level;
4. create the player and initial task/script state;
5. reset the simulation clock;
6. lock gameplay input and suppress editor picking.

Leaving game mode must destroy or reset runtime state, restore the editor camera
and source state, unlock the cursor, and restore editor UI state. A failed mode
transition must leave the editor usable.

## 7. Simulation timing

Implement a fixed simulation step of `1.0 / 30.0` seconds. Rendering may run at a
different frequency, but gameplay state changes only during simulation ticks.

Required behavior:

- monotonic time source;
- deterministic tick counter;
- pause and level-load exclusion scopes;
- bounded catch-up to prevent an unbounded loop after a stall;
- explicit handling of dropped/capped time;
- input sampled at the frame boundary and consumed by simulation ticks;
- tests for pause, long frame, clock drift, repeated timestamps, and mode reset.

The clock is a scheduling mechanism, not evidence that every other subsystem
matches the original game. Tick rate must not be used as a substitute for retail
physics validation.

## 8. QVM runtime and native registry

The existing C++ QVM parser/compiler/decompiler is not automatically a runtime
interpreter. Add a separate runtime component with:

- validated program loading;
- instruction pointer and operand stack;
- call frames and return handling;
- integer, float, string, and pointer/value semantics as required by the target;
- bounds and malformed-bytecode checks;
- deterministic native-function lookup;
- native call context with explicit argument evaluation rules;
- instruction limits and fault reporting;
- resettable script instances;
- trace logging that never exposes invalid memory or secrets.

Do not register natives by guessed names alone. Build a registry table from
verified QVM behavior and test each native against small scripts and retail
observations. A QVM that disassembles correctly is not necessarily a QVM that
executes correctly.

## 9. Task tree and messaging

Create runtime task objects separately from editor schema objects. The runtime
task system requires:

- stable IDs and parent/child ownership;
- explicit destruction order;
- per-tick update scheduling;
- queued messages with defined delivery timing;
- state transitions and cancellation;
- script argument/value conversion;
- safe restart and level teardown;
- cycle and duplicate-parent detection.

The initial implementation should support only the task types required by the
vertical slice. Additional task types are added when a real mission requires them,
with a regression test for each type.

## 10. Player movement and collision

The player controller must be implemented as a runtime subsystem, not as a
modified free camera. Its responsibilities include:

- input-frame consumption;
- yaw/pitch and player camera placement;
- grounded, airborne, crouched, falling, and dead states;
- velocity and gravity integration;
- ground probe and step-down policy;
- multi-height wall probes and wall sliding;
- head/overhang clearance;
- slope and material queries;
- interaction raycasts;
- ladders, ziplines, or other traversal only when required by the selected mission;
- animation/root-motion integration where the reference behavior depends on it.

Collision must use world geometry appropriate to the target. Object bounding-box
collision may be used as a clearly labelled placeholder for the first slice, but
cannot be presented as original IGI physics. Add deterministic tests for ground
contact, wall approach, wall departure, corners, steps, slopes, ceilings,
teleport/reset, and unloaded geometry.

## 11. Weapons and damage

Implement weapons as runtime state machines:

- weapon selection and ammunition;
- fire cadence and reload timing;
- recoil/spread policy;
- hitscan or projectile behavior;
- hit filtering and damage application;
- impact and sound events;
- death handling;
- deterministic tests using fixed random seeds where randomness is required.

Do not assume every weapon is hitscan. Classify weapon behavior from retail
evidence, scripts, or a temporary placeholder label.

## 12. AI and perception

AI must be driven by runtime state and task/script integration. The minimum
subsystems are:

- per-tick perception snapshot;
- view-cone and distance tests;
- line-of-sight/occlusion query;
- sound-event admission and propagation;
- patrol/idle/alert/combat transitions;
- target selection;
- alarm propagation;
- movement and weapon actions;
- reset on death, reload, and mission restart.

The two-cone model documented by OpenIGI is a useful reference hypothesis. It is
not sufficient evidence for complete retail AI behavior. Test visibility at cone
boundaries, behind obstacles, beyond range, after sound events, and after state
transitions.

## 13. Mission flow

Implement a runtime mission-flow service that evaluates objective state from
events and entity/task state. It must support:

- mission start and initialization;
- primary and secondary objectives;
- pending, completed, failed, and cancelled states;
- extraction/end conditions;
- death and failure;
- restart from a clean snapshot;
- success/failure presentation;
- campaign progression only after explicit success.

The first slice requires one mission objective path. Fourteen-level support is a
later compatibility gate, not a prerequisite for the first runtime build.

## 14. Three-day vertical-slice plan

The three-day run is an experiment with measurable gates, not a promise of a
complete engine port.

### Day 1 — runtime foundation

- create runtime-world ownership and entity IDs;
- implement fixed 30 Hz clock;
- implement mode transition and restoration;
- connect one player runtime entity;
- compile after each logical change;
- add clock, snapshot, and mode-transition tests.

**Day 1 gate:** editor remains functional; game mode starts and exits cleanly;
player state advances on deterministic ticks.

### Day 2 — script and gameplay slice

- connect the existing QVM parser output to a bounded interpreter;
- execute one verified or minimal test QVM;
- add one native binding;
- implement player ground/wall placeholder or verified query;
- add one guard entity and one perception transition;
- add one weapon action and one damage result.

**Day 2 gate:** a test mission can run one script, move the player, detect the
player, and produce a combat event without crashing.

### Day 3 — mission and validation

- add one objective and win/fail transitions;
- add gameplay HUD and input lock;
- test restart and editor restoration;
- run focused unit tests;
- run the existing editor regression suite;
- record token usage, cache-read percentage, failures, and unresolved behaviors;
- produce a playable vertical-slice build.

**Day 3 gate:** one mission path is playable end-to-end and all known limitations
are recorded. If this gate fails, the next action is debugging the failing
subsystem, not adding more mission content.

## 15. Validation requirements

Every subsystem must have tests before it is considered ported:

| Subsystem | Required evidence |
|---|---|
| Clock | deterministic tick, pause, exclusion, catch-up, reset tests |
| QVM | opcode, call-frame, native, malformed-program, limit, reset tests |
| Tasks | ownership, message order, cancellation, teardown, restart tests |
| Player | ground, wall, air, slope, ceiling, input, reset tests |
| Weapons | cadence, ammo, hit, damage, reload, death tests |
| AI | cone boundary, occlusion, sound, transition, reset tests |
| Mission | objective, success, failure, restart, progression tests |
| Modes | editor-to-game, game-to-editor, input lock, UI suppression tests |

Validation levels:

1. unit tests;
2. deterministic simulation tests;
3. focused level fixture tests;
4. editor regression suite;
5. interactive vertical-slice test;
6. retail comparison and replay evidence;
7. multi-level compatibility testing.

Local tests prove local correctness only. They do not prove retail parity.

## 16. AI-assisted development rules

AI may generate translations, boilerplate, tests, documentation, and candidate
implementations. The agent must not silently invent retail facts.

Each AI change must include:

- source/reference files consulted;
- evidence classification;
- files changed;
- build/test command;
- test result;
- known limitations;
- whether behavior is a placeholder or retail-derived.

Use small commits and keep the worktree clean enough to bisect failures. Preserve
the existing editor and unrelated user changes. Do not run an unbounded autonomous
loop without a spending limit, checkpoint, or recovery point.

## 17. Completion definition

The port is **not complete** when the C++ code compiles or when one demo loads.

### Vertical-slice complete

- one mission plays from start to success/failure;
- runtime state is isolated from editor state;
- QVM, player, AI, weapon, and objective paths execute;
- focused tests pass;
- known placeholders are documented.

### Runtime milestone complete

- required task/script natives are implemented;
- player collision and interactions cover the selected mission;
- AI and weapon behavior is validated across mission fixtures;
- restart and mode transitions are reliable;
- full editor regression suite passes.

### Fidelity milestone complete

- behavior is compared with IGI1 using repeatable scenarios;
- discrepancies are tracked by subsystem;
- IGI2 evidence is explicitly separated from IGI1 evidence;
- no unsupported claim of exact physics or engine parity remains.

## 18. Risk register

| Risk | Impact | Mitigation |
|---|---|---|
| IGI2/IGI1 differences | Wrong layouts, natives, or constants | Validate against IGI1; classify IGI2 evidence as inference until confirmed. |
| QVM native mismatch | Scripts fail or corrupt state | Registry tests, bounds checks, instruction limits, trace fixtures. |
| Editor/runtime coupling | Corrupt editor state | Runtime snapshot and separate mutable entity state. |
| Bounding-box placeholders | Incorrect physics | Label placeholders and replace with verified geometry queries incrementally. |
| AI context drift | Agent produces incompatible subsystems | Small commits, focused prompts, compile/test gates. |
| Three-day time pressure | Incomplete or speculative code | Require vertical-slice gates; stop expanding scope when a gate fails. |
| Token/cache changes | Unexpected cost | Record provider usage after each milestone and set a hard balance limit. |

## 19. Exact file-and-method implementation map

This section is intentionally spoon-fed. Every implementation task must identify
the exact reference file, exact reference symbol, exact C++ target file, exact
C++ target symbol, and exact behavior to add. Do not invent a reference path such
as `PlayerController.cs`, `WeaponSystem.cs`, or `LevelFlow.cs`; those names are
conceptual groupings, not current OpenIGI files.

### 19.1 Fixed clock

| Reference file | Reference methods | C++ target | Exact target change |
|---|---|---|---|
| `D:/Code/open-igi/src/OpenIGI.Engine/Time/GameClock.cs` | `GameClock.Decide(int nowMilliseconds)`, `IsTickDue(int nowMilliseconds)`, `CompleteTick()`, `CompleteRender()`, `CompleteUnscheduledFrame()`, `BeginExcludedTime()`, `EndExcludedTime()`, `ResetDrift()` | `D:/Code/project-igi-editor/source/game_clock.h` and `source/game_clock.cpp` | **CREATE** `GameClock` with equivalent state and explicit tests. Do not copy the 30 Hz claim without porting the due/catch-up/exclusion rules. |
| `D:/Code/open-igi/src/OpenIGI.Engine/Flow/GameLoop.cs` | `Boot(bool showIntro)`, `RunFrame()`, `RequestShutdown()` | `D:/Code/project-igi-editor/source/app.cpp`, `App::OnIdle()` | Call `game_clock_.Update(...)` from `App::OnIdle()`, consume pending ticks, and call a new `App::RunSimulationTick(uint32_t tick)`. Do not put gameplay updates directly into render code. |

### 19.2 QVM execution

| Reference file | Reference methods | C++ target | Exact target change |
|---|---|---|---|
| `D:/Code/open-igi/src/OpenIGI.Scripting/Runtime/QvmInterpreter.cs` | `QvmInterpreter.Run()`, private `Execute()`, private `Step()`, `ReadArgumentOffset(...)`, `TryEvaluateArgument(...)`, `Peek(...)` | `D:/Code/project-igi-editor/source/level/qvm_interpreter.h` and `qvm_interpreter.cpp` | **CREATE** a bounded interpreter. Port opcode semantics only after comparing them with `source/level/qvm_parser.cpp`. Add program-counter, stack, call-frame, illegal-opcode, stack-underflow, and instruction-limit tests. |
| `D:/Code/open-igi/src/OpenIGI.Scripting/Runtime/NativeRegistry.cs` | `RegisterFunction(...)`, `RegisterConstant(...)`, `RegisterRealConstant(...)`, `RegisterVariable(...)`, `TryResolve(...)`, `Unregister(...)`, `SetSymbolContext(...)` | `D:/Code/project-igi-editor/source/level/qvm_native_registry.h` and `qvm_native_registry.cpp` | **CREATE** a registry keyed by the target QVM symbol/index contract. Do not register arbitrary names or silently return zero for missing natives. |
| `D:/Code/project-igi-editor/source/level/qvm_parser.cpp` | `QVM_Parse(const std::string& filepath)` | `D:/Code/project-igi-editor/source/level/qvm_interpreter.cpp` | Consume the validated `QVMFile` representation. Do not make the decompiler an interpreter and do not alter parser validation to make runtime tests pass. |

### 19.3 Runtime task tree

| Reference file | Reference methods | C++ target | Exact target change |
|---|---|---|---|
| `D:/Code/open-igi/src/OpenIGI.Engine/Tasks/GameTask.cs` | `Receive(TaskMessage message)`, virtual `OnCreate()`, `OnUpdate()`, `OnRender()`, `OnPreDestroy()`, `OnDestroy()`, `OnParsed()`, `OnPostLoad()`, `OnMessage(TaskMessage)`, `AppendChild(...)`, `PrependChild(...)`, `Unlink()` | `D:/Code/project-igi-editor/source/level/task_tree.h` and `task_tree.cpp` | **CREATE** runtime `GameTask` ownership and lifecycle. Use `std::unique_ptr` for owned children unless a measured requirement proves otherwise. Add duplicate-parent, teardown, and message-order tests. |
| `D:/Code/open-igi/src/OpenIGI.Engine/Tasks/ContainerTask.cs` | `OnUpdate()` | `D:/Code/project-igi-editor/source/level/task_tree.cpp` | Forward the update message to runtime children at the defined tick boundary. Do not make `LevelObject` inherit from runtime tasks merely to reuse editor storage. |
| `D:/Code/project-igi-editor/source/level/level_objects.cpp` | `LevelObjects::Load(...)`, `LoadRecursive(...)` | `D:/Code/project-igi-editor/source/runtime/runtime_world.cpp` | **CREATE** an adapter that reads `LevelObject` data and creates runtime entities/tasks. Keep `LevelObjects` as editor/source data. |

### 19.4 Player movement and collision

| Reference file | Reference methods | C++ target | Exact target change |
|---|---|---|---|
| `D:/Code/open-igi/src/OpenIGI.Game/Player/PlayerBody.cs` | `Tick(in PlayerCommand, TerrainData, Vector3f rootDelta)`, `TickAnimated(...)`, `BeginLadderTraversal()`, `TickLadderSlide(...)`, `EndLadderTraversal(...)`, `PlaceOnGround(...)` | `D:/Code/project-igi-editor/source/player_controller.h` and `player_controller.cpp` | **CREATE** runtime player controller. First implement `Tick`, ground state, velocity, and camera eye position. Add traversal only when the selected mission requires it. |
| `D:/Code/open-igi/src/OpenIGI.Game/Player/HumanMotion.cs` | `Integrate(Vector3f velocity)`, `IntegrateLadderSlide(...)`, `AirControl(...)`, `ApplyRootMotion(...)` | `D:/Code/project-igi-editor/source/player_controller.cpp` | Port as named helper functions and add unit tests for gravity, air control, and root-motion composition. Record whether each behavior is `verified-reference` or `inferred`. |
| `D:/Code/open-igi/src/OpenIGI.Game/Player/HumanGroundQuery.cs` | `StepDownBudget(...)`, `ProbeOrigin(...)`, `IsStandingOn(...)`, `IsUnderRoof(...)` | `D:/Code/project-igi-editor/source/player_collision.h` and `player_collision.cpp` | **CREATE** ground/roof query helpers. Connect them to the existing terrain query through an adapter; do not replace the terrain reader. |
| `D:/Code/open-igi/src/OpenIGI.Game/Player/HumanWallSweep.cs` | `Sweep(...)` | `D:/Code/project-igi-editor/source/player_collision.cpp` | Port the multi-probe sweep and wall-slide result. The current editor `App::CheckCollision(...)` is not a substitute. |
| `D:/Code/project-igi-editor/source/app_view.cpp` | `App::ProcessInput(float delta_seconds)`, `App::CheckCollision(const glm::vec3&)`, `App::UpdateViewerVectors()` | `D:/Code/project-igi-editor/source/app_view.cpp` | In `ProcessInput`, branch to runtime input only when `edit_mode_ == false`. In game mode do not update `viewer_` as a free camera; copy the runtime player's eye pose into the view. Retain editor behavior unchanged. |
| `D:/Code/project-igi-editor/source/level/terrain_query.cpp` | `Terrain::GetZ(...)` | `D:/Code/project-igi-editor/source/player_collision.cpp` | Call the existing height query through a narrow collision adapter. Add tests for missing terrain and discarded/invalid terrain data. |

### 19.5 Weapons and damage

| Reference file | Reference methods | C++ target | Exact target change |
|---|---|---|---|
| `D:/Code/open-igi/src/OpenIGI.Game/Weapons/GunFiring.cs` | `Evaluate(...)`, `ConeAngle(...)`, `SampleCone(...)` | `D:/Code/project-igi-editor/source/weapon_system.h` and `weapon_system.cpp` | **CREATE** weapon state and fire evaluation. Preserve cooldown, burst, ammunition, and deterministic spread semantics. |
| `D:/Code/open-igi/src/OpenIGI.Game/Weapons/GunShotBallistics.cs` | `RangeInUnits(...)`, `Penetrates(...)`, `DeflectionChance(...)`, `CanDeflect(...)`, `ConsumeRange(...)` | `D:/Code/project-igi-editor/source/weapon_system.cpp` | Port as independent ballistics helpers. Do not call them “original” until retail comparison is complete. |
| `D:/Code/project-igi-editor/source/app_input_mouse.cpp` | `App::Input_OnMouse(...)` | `D:/Code/project-igi-editor/source/app_input_mouse.cpp` | In game mode, route the fire input to `WeaponSystem::Fire(...)`; in editor mode retain object picking. Add a testable input-action boundary rather than embedding all weapon logic in GLUT callbacks. |

### 19.6 AI

| Reference file | Reference methods | C++ target | Exact target change |
|---|---|---|---|
| `D:/Code/open-igi/src/OpenIGI.Game/Ai/AiVision.cs` | `Capture(...)`, `IsVisible(...)`, static `AngleFromForward(...)` | `D:/Code/project-igi-editor/source/ai_system.h` and `ai_system.cpp` | **CREATE** perception snapshots and cone tests. Inject a line-of-sight callback; do not hardwire visibility to renderer geometry. |
| `D:/Code/open-igi/src/OpenIGI.Game/Ai/AiGuardCombat.cs` | `Begin()`, `CancelChild()`, `Update(...)`, `ReportAimed(...)` | `D:/Code/project-igi-editor/source/ai_system.cpp` | Port the combat state machine as a runtime state object. Feed it player visibility, target, ammunition, and detection state from the runtime world. |
| `D:/Code/open-igi/src/OpenIGI.Game/Ai/AiEventQueues.cs` | `Post(...)`, `Pump(...)`, `PendingCount(...)`, `HeadOf(...)`, `SetEnabled(...)`, `SetPriority(...)` | `D:/Code/project-igi-editor/source/ai_events.h` and `ai_events.cpp` | **CREATE** event queues with explicit tick ordering and owner filtering. Add sound-event and reset tests. |
| `D:/Code/project-igi-editor/source/level/level_objects.cpp` | `LoadRecursive(...)` | `D:/Code/project-igi-editor/source/runtime/runtime_world.cpp` | Detect relevant loaded task/object records and create AI runtime entities. Do not execute AI inside the editor loader. |

### 19.7 Mission objectives

| Reference file | Reference methods | C++ target | Exact target change |
|---|---|---|---|
| `D:/Code/open-igi/src/OpenIGI.Game/World/MissionObjectiveRuntime.cs` | `MissionObjectiveRuntime` constructor and objective-state evaluation members; `MissionObjectiveState` | `D:/Code/project-igi-editor/source/level_flow.h` and `level_flow.cpp` | **CREATE** a runtime objective service using event/state inputs. Do not reference a nonexistent `LevelFlow.cs`. |
| `D:/Code/open-igi/src/OpenIGI.Game/World/MissionProgression.cs` | `UnlockNext(...)`, `Status(...)` | `D:/Code/project-igi-editor/source/level_flow.cpp` | Port campaign progression only after the single-mission win/fail path works. Test locked, active, completed, and invalid mission IDs. |
| `D:/Code/project-igi-editor/source/app.cpp` | `App::OnIdle()`, `App::Frame(float delta_seconds)`, `App::ToggleEditMode()`, `App::SetEditMode(bool)` | `D:/Code/project-igi-editor/source/app.cpp` | `OnIdle()` schedules ticks; `RunSimulationTick()` updates player, QVM, tasks, AI, weapons, and objectives in a documented order; `ToggleEditMode()` snapshots/restores state. |

### 19.8 UI and input separation

| Existing C++ file | Existing methods | Required game-mode behavior |
|---|---|---|
| `D:/Code/project-igi-editor/source/app.cpp` | `App::OnDisplay()`, `App::OnIdle()`, `App::Frame(...)` | Select editor or gameplay presentation path. Do not run editor mutation tools in game mode. |
| `D:/Code/project-igi-editor/source/app_ui.cpp` | `App::UpdateCursorMode()`, `App::DrawProgressOverlay(...)` | Add a separate gameplay HUD function; suppress editor panels/gizmos only while game mode is active. |
| `D:/Code/project-igi-editor/source/app_input_keyboard.cpp` | `App::Input_OnKeyboard(...)`, `App::Input_OnKeyboardUp(...)` | Add the mode-toggle action and gameplay input mapping without breaking existing editor bindings. |
| `D:/Code/project-igi-editor/source/app_input_mouse.cpp` | `App::Input_OnMouse(...)`, `App::Input_OnMotion(...)` | Lock and recenter gameplay mouse input; preserve editor picking and manipulation in editor mode. |
| `D:/Code/project-igi-editor/source/app.h` | `App::ToggleEditMode()`, `GetEditMode()`, `SetEditMode(bool)` | Add runtime services and snapshot ownership only after their headers exist. Avoid a header-only declaration that has no implementation. |

### 19.9 Required implementation order

An agent must execute these rows in order:

1. Create runtime state types and snapshot/reset tests.
2. Create `GameClock`; connect it only to a no-op simulation tick.
3. Create runtime world/entity ownership and adapt `LevelObjects::Load(...)` data.
4. Create QVM interpreter and registry tests before connecting level scripts.
5. Create player collision helpers and player tick tests.
6. Connect player input and camera in game mode.
7. Add one weapon and damage path.
8. Add one guard, perception, event queue, and combat transition.
9. Add one objective, success/failure, and restart path.
10. Add HUD, input lock, editor restoration, and the vertical-slice test.

For each row, the implementation record must state: reference path, reference
method, target path, target method, evidence classification, changed files, build
command, focused tests, result, and unresolved limitations.

## 20. Production twin-window architecture

The production application must not remove, redesign, or weaken the existing
editor. The editor remains the authoritative level-authoring tool and is hosted
in its own application window. Gameplay is hosted in a separate runtime window
inside the same process/application session.

The final product is therefore a **twin-window dual-mode application**:

| Window | Role | Ownership |
|---|---|---|
| Editor window | Existing `project-igi-editor` UI, inspectors, task tree, terrain tools, previews, and save operations | Editor/UI state |
| Gameplay window | First-person runtime, HUD, input, simulation, AI, weapons, QVM, mission flow, and pause/restart | Runtime/presentation state |

The phrase “dual mode” means that the user can move between these windows during
one application session. It does not mean that gameplay code is allowed to
replace the editor implementation or that editor controls are merely hidden in
the same viewport.

### 20.1 Production requirements

The production implementation must satisfy all of the following:

1. The existing editor code remains available and its current editor behavior is
   preserved.
2. The gameplay window is a separate native window or controlled child window
   with its own input focus, cursor policy, camera, HUD, and render/presentation
   state.
3. Both windows use the same loaded asset/session layer but do not share mutable
   gameplay state by accident.
4. The editor can remain visible while gameplay is running, unless the user
   explicitly chooses exclusive/fullscreen gameplay.
5. The user can switch focus between editor and gameplay without restarting the
   application or reloading the process.
6. An editor change can be explicitly applied to a stopped/reloaded runtime
   session; gameplay changes are never silently written back to source files.
7. Entering, pausing, restarting, and leaving gameplay do not corrupt editor
   selections, undo history, camera state, unsaved text, or terrain tools.
8. A runtime crash or failed mission load must return a useful error to the
   editor and must not destroy the editor window.

### 20.2 Required production components

Create these components as separate responsibilities:

| Component | Required location | Responsibility |
|---|---|---|
| `EditorHost` | Existing `source/app.cpp`/`source/app.h` integration | Preserve and host the current editor window and lifecycle. |
| `GameplayHost` | **CREATE** `source/runtime/gameplay_host.h/.cpp` | Create, show, hide, focus, pause, restart, and destroy the gameplay window. |
| `RuntimeSession` | **CREATE** `source/runtime/runtime_session.h/.cpp` | Own one isolated runtime world and its lifecycle state. |
| `AssetSession` | Existing `Level`, renderer/resource loaders, and adapters | Share immutable loaded asset data safely between windows. |
| `EditorSnapshot` | **CREATE** `source/runtime/editor_snapshot.h/.cpp` | Capture and restore only the editor state required for seamless switching. |
| `RuntimeRenderer` | **CREATE** or adapt existing renderer boundary | Render gameplay camera/HUD without editor selection/gizmo overlays. |
| `WindowInputRouter` | **CREATE** `source/runtime/window_input_router.h/.cpp` | Route keyboard/mouse input only to the focused window and active purpose. |
| `SimulationScheduler` | **CREATE** `source/runtime/simulation_scheduler.h/.cpp` | Run fixed simulation ticks independently of window render frequency. |

Do not implement this architecture by putting all runtime behavior into
`App::OnDisplay()` or by adding more branches to the existing editor renderer.
Those functions may be integration points, but ownership belongs in the new
runtime components.

### 20.3 Exact integration points

| Existing file | Existing method | Production change |
|---|---|---|
| `D:/Code/project-igi-editor/source/app.cpp` | `App::Init(int argc, char** argv)` | Initialize `EditorHost` and the `GameplayHost` controller without changing the existing editor initialization order. |
| `D:/Code/project-igi-editor/source/app.cpp` | `App::OnIdle()` | Pump window events, advance the independent simulation scheduler, and dispatch safe editor/runtime repaint requests. |
| `D:/Code/project-igi-editor/source/app.cpp` | `App::OnDisplay()` | Render the existing editor window only. Gameplay rendering belongs to `GameplayHost`. |
| `D:/Code/project-igi-editor/source/app.cpp` | `App::Shutdown()` | Stop simulation, destroy gameplay window/resources, then shut down editor resources in the existing order. |
| `D:/Code/project-igi-editor/source/app.cpp` | `App::ToggleEditMode()`, `App::SetEditMode(bool)` | Convert these APIs into session/window commands or preserve them as compatibility wrappers. They must not mean “delete editor UI.” |
| `D:/Code/project-igi-editor/source/app_input_keyboard.cpp` | `App::Input_OnKeyboard(...)` | Add commands for `Open Gameplay`, `Focus Editor`, `Focus Gameplay`, `Pause`, and `Restart`; preserve existing editor bindings. |
| `D:/Code/project-igi-editor/source/app_input_mouse.cpp` | `App::Input_OnMouse(...)`, `App::Input_OnMotion(...)` | Route events through `WindowInputRouter`; editor picking remains unchanged when editor focus is active. |
| `D:/Code/project-igi-editor/source/app_view.cpp` | `App::ProcessInput(float delta_seconds)` | Keep editor camera behavior for the editor window. Runtime player movement must be handled by `PlayerController::Tick(...)`. |
| `D:/Code/project-igi-editor/source/level/level.cpp` | `Level::Load(...)`, `Unload()`, `Update(...)` | Keep source-level load/update behavior stable; add runtime adapters rather than turning `Level` into a mixed editor/game object. |
| `D:/Code/project-igi-editor/source/level/level_objects.cpp` | `LevelObjects::Load(...)`, `LoadRecursive(...)` | Continue building editor objects; runtime conversion is performed by `RuntimeSession::BuildFromEditorLevel(...)`. |

### 20.4 Seamless switching contract

The user-visible workflow must be:

1. Open a level in the editor window.
2. Choose `Open Gameplay` or press the documented gameplay command.
3. The runtime session snapshots editor state and builds a gameplay world from
   the current loaded level.
4. The gameplay window opens and receives gameplay focus.
5. The editor remains available in its own window.
6. Pause or stop gameplay to return focus to the editor.
7. Edit the level if desired.
8. Choose `Apply and Restart Gameplay` to build a fresh runtime snapshot.
9. Choose `Close Gameplay` to destroy runtime state and return to editing.

There must be no implicit source-file write during step 3, 6, or 9. Applying
runtime state to editor source data, if ever supported, must be an explicit,
reviewable editor command with undo support.

### 20.5 Runtime state isolation

The runtime must not hold references that allow it to mutate editor-owned
containers, undo stacks, unsaved QSC text, or editor selection state. Use one of:

- immutable shared asset objects plus copied runtime components; or
- a documented copy-on-build representation with explicit ownership.

At minimum, snapshot and restore:

- editor camera position/orientation and camera mode;
- selected/hovered object and inspector state;
- terrain editing state and brush settings;
- unsaved editor text buffers and undo/redo state;
- loaded level identity and source paths;
- renderer/editor overlay flags;
- any mutable object transforms changed by preview operations.

Runtime restart must reconstruct runtime entities from the snapshot/source view,
not attempt to reverse every gameplay mutation one by one.

## 21. Reverse-engineering track for original physics

The C++ runtime port and the original-physics investigation are related but
separate workstreams. Porting OpenIGI's code creates a playable reference
implementation; it does not establish that the result is Innerloop's original
physics.

### 21.1 Required evidence pipeline

For every physics behavior, use this order:

1. Locate the candidate IGI1 function or data through Ghidra.
2. Load and verify the matching PDB only when executable signature and PDB
   signature/age match.
3. Use Ghidra MCP to extract function names, callers, callees, constants,
   structures, and cross-references.
4. Ask the AI to produce a hypothesis and list uncertainty; never ask it to
   present an inference as fact.
5. Confirm the hypothesis with debugger/hook/runtime observations where legal
   and technically available.
6. Compare against IGI2 symbols only as supporting evidence, explicitly marking
   version differences.
7. Implement the smallest C++ function that represents the observed behavior.
8. Add a deterministic unit test and a retail comparison fixture or observation
   record.
9. Replace placeholders only when the new evidence is stronger than the old
   implementation.

### 21.2 Physics investigation matrix

| Physics behavior | Ghidra/MCP task | C++ implementation target | Required proof |
|---|---|---|---|
| Tick integration | Identify movement update caller and time step | `source/player_controller.cpp` | Tick trace plus deterministic movement test |
| Gravity/air control | Identify velocity fields and update constants | `source/player_controller.cpp` | Jump/fall comparison |
| Ground probe | Identify downward query and snap/step threshold | `source/player_collision.cpp` | Ground, step, and walk-off tests |
| Wall sweep | Identify probe heights, normals, iteration, and slide projection | `source/player_collision.cpp` | Wall, corner, and slope comparison |
| Ceiling response | Identify high probe and vertical clamp behavior | `source/player_collision.cpp` | Jump-under-roof test |
| Root motion | Identify animation delta composition with velocity | `source/player_controller.cpp` | Animation movement comparison |
| Body dimensions | Identify collision offsets/radii from data and code | `source/player_collision.cpp` | Standing/crouching clearance tests |
| Ladders/ziplines | Identify task/script entry and movement state | `source/player_controller.cpp` and `source/runtime/runtime_world.cpp` | Mission fixture where required |

### 21.3 AI responsibilities

Ghidra MCP and AI are used for:

- decompiler explanation;
- call-graph and structure organization;
- candidate constant extraction;
- translation drafts;
- test generation;
- comparison-report generation.

They are not a replacement for runtime evidence. AI-generated physics must be
marked `inferred` until a repeatable test supports it. A compiler success, a
smooth-looking demo, or an OpenIGI comment is not sufficient proof of original
physics parity.

## 22. Production completion gates

The production twin-window feature is complete only when:

- the existing editor opens and behaves as before;
- gameplay opens in a separate window in the same application session;
- focus and input routing are deterministic;
- gameplay can be paused while the editor remains usable;
- editor changes can explicitly rebuild the runtime session;
- runtime changes never silently write editor source files;
- close/restart/crash paths preserve editor usability;
- one mission runs end-to-end;
- physics placeholders are listed and labelled;
- Ghidra/MCP evidence records exist for every claimed original-physics behavior;
- focused tests and the existing editor regression suite pass.

## 23. Current decision

Proceed with the C++ runtime project, but fund and evaluate it as a staged
reverse-engineering effort. The existing editor and file infrastructure make a
playable runtime practical. They do not make a complete original-engine port
automatic.

The immediate objective is the three-day vertical slice described above. Success
means the architecture is validated and worth continuing. It does not mean that
all fourteen missions, all original physics, or complete engine parity have been
achieved.
