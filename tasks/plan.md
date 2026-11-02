# Implementation Plan: IGI Gameplay Runtime and Twin-Window Port

## Overview

Bring the `editor-gameplay-mode` C++ branch from its current scaffold toward a
deterministic, isolated Windows gameplay runtime that consumes the existing
vanilla editor level data. The immediate target is a convincing playable
vertical slice: fixed-step simulation, real player collision, weapons/projectiles,
moving enemies, sound, HUD, and one mission path. The production boundary must
also keep the editor alive in its own window while gameplay is focused. Retail
parity claims remain gated on IGI1 evidence.

## Architecture Decisions

- Keep editor/source level objects immutable to gameplay; runtime state is built
  from copied/adapted data and is destroyed on restart/close.
- Keep the simulation at 30 Hz with explicit catch-up, pause, exclusion, and
  reset semantics; rendering/window integration consumes simulation state.
- Treat the runtime session as the owner of mutable world state. The editor host
  owns transition policy; the gameplay host owns presentation/window policy; no
  gameplay renderer may mutate the authoring level representation.
- Follow OpenIGI's `GameLoop`/`IGameWindow` split: the host pumps window events,
  the fixed-step session advances simulation, and presentation consumes a
  snapshot. Do not hide gameplay rendering inside the editor display callback.
- Treat OpenIGI as `verified-reference` evidence, not proof of IGI1 parity.
  Any behavior without retail evidence is labelled `inferred` or `placeholder`.
- Prefer narrow injected collision and visibility queries so gameplay tests do
  not depend on an OpenGL window or renderer globals.

## Task List

### Phase 1: Audit and baseline

- [x] Confirm the exact remote branch, specification, OpenIGI reference, and
  current runtime files.
- [x] Build a headless runtime harness and run the focused tests; the baseline
  slice passed 28/28 tests on the development host, and the final line-of-sight
  change passed a fresh runtime smoke harness.
- [x] Record the current placeholders and missing production components in
  `coding_guidelines.md` and the evidence boundary in `tasks/todo.md`.

### Phase 2: Runtime foundation

- [x] Replace the accumulator-only clock with the documented absolute-deadline
  schedule, pause/exclusion/reset handling, bounded catch-up, and tests.
- [x] Harden runtime task ownership, duplicate IDs/parents, lifecycle ordering,
  queued message timing, and teardown tests.
- [x] Add an explicit runtime session/world boundary and editor snapshot reset
  contract without mutating editor source objects.

### Checkpoint: Foundation

- [x] Focused runtime tests pass.
- [ ] Windows C++ build succeeds (not runnable on the current non-Windows host).
- [x] Runtime can start, tick deterministically, reset, and close without
  changing the editor/source representation.

### Phase 3: Gameplay systems

- [x] Port the OpenIGI-referenced ground/roof constants and multi-probe wall
  sweep into the C++ collision adapter; add ground, step, wall, corner, slope,
  ceiling, and missing-geometry tests.
- [x] Integrate player input, stance, gravity, air control, camera pose, and
  collision resolution into the runtime session.
- [x] Harden weapon cadence/ammunition/damage and world-occluded deterministic
  ballistics.
- [x] Connect AI perception/event queues/patrol/combat to runtime entities.
- [x] Execute one validated bounded normalized QVM program through an explicit native
  registry and connect task/message updates at the fixed tick boundary.

### Phase 4: Mission and presentation integration

- [x] Add one objective success/failure/restart path from runtime events.
- [x] Separate gameplay input focus/pause/restart from editor input and expose a
  gameplay host boundary; preserve editor rendering and save behavior.
- [x] Add the vertical-slice integration test and document unresolved retail
  differences and placeholders.

### Phase 5: Production runtime boundary (current)

- [x] Extract an explicit `RuntimeSession` lifecycle (`Created`, `Running`,
  `Paused`, `Stopped`, `Failed`) from `GameplayHost`, with direct lifecycle
  tests and no editor/OpenGL dependency.
- [ ] Add a controlled gameplay window owned by `GameplayHost`; the current
  slice creates the Windows/FreeGLUT window, routes callbacks, focus, cursor,
  relative mouse look, viewport, and close recovery. The editor now repaints
  its authoring scene through a render-only target while gameplay remains
  active. Full independent camera/HUD/render ownership remains pending because
  scene presentation still passes through the shared `App::Frame()` renderer.
- [ ] Move gameplay drawing out of `App::Frame()` into a runtime presentation
  path. `GameplayHost` now owns the OpenGL-free `RuntimeRenderer` snapshot used
  by HUD/weapon/projectile presentation; keep shared asset caches read-only and
  make restart/close destroy the mutable runtime session before moving the GL
  draw/resource ownership.
- [x] Add an explicit `F5` apply/restart boundary: the host replaces the
  captured editor snapshot, rebuilds mutable runtime adapters from copied
  level objects, and never silently writes source files or mutates the running
  world.
- [x] Keep simulation-side collision, interaction, and ladder discovery bound
  to the mutable runtime snapshot while the editor window repaints.
- [x] Add deterministic `F6` editor / `F7` gameplay focus routing. The editor
  can receive authoring input and repaint its source view while the active
  runtime continues through the fixed-step scheduler; focus changes do not
  restart the session. Terrain editing and level switching remain blocked
  while gameplay is active because their immutable asset snapshot boundary is
  not yet complete.

### Phase 6: Fidelity and fixture coverage

- [x] Replace the inferred landing-impact threshold with the verified OpenIGI
  vanilla speed-to-health formula, direct-health damage path, authored fall
  sound selection, and regression coverage.
- [x] Port the OpenIGI-referenced airborne and ladder-slide motion integrators,
  root-motion transform order, and the fixed-step player-controller seam.
- [x] Port the OpenIGI-referenced ladder placement offsets, activation geometry,
  rung discretization, and traversal state transitions as a headless seam.
- [x] Parse the runtime-allocated `magicobj.qvm` definitions through a shared
  registry and expose named ladder attachment lookup to the renderer bridge.
- [x] Discover attached ladder models at gameplay setup, transform their magic
  vertices into runtime units, and store the immutable placement list in the
  session-owned world.
- [x] Bind ladder traversal to player input with a deterministic fixed-step
  fallback so the selected fixture can mount, climb, slide, and dismount.
- [x] Feed the selected fixture's authored player locomotion and ladder
  animation root-motion/completion events into the fixed-step command stream;
  the slide animation clock remains presentation-only while the verified
  ladder-slide physics path owns collision-safe descent.
- [x] Port the vanilla first-person weapon-change view sway: lower the rig for
  eleven fixed ticks, change the active slot at the settled boundary, then
  raise the new weapon through the immutable render snapshot.
- [x] Resolve authored vanilla WAV names from loose mission files and packed
  `COMMON/SOUNDS/SOUNDS.RES` entries, lazily materialize them in a cache, and
  keep the packed archive index warm so repeated fire events stay fixed-step
  friendly; align the weapon catalog with names present in the vanilla QVMs.
- [x] Emit typed fixed-step audio intents from `RuntimeWorld` and dispatch them
  from `GameplayHost`, keeping Windows WAV/RES playback out of simulation code
  while preserving one-shot and conditional-loop sound timing.
- [x] Carry a fixed-step firearm muzzle cue through `RuntimeRenderSnapshot` and
  draw a deterministic screen-space fallback when the authored muzzle sprite is
  unavailable.
- [x] Carry fixed-step guard muzzle flashes and incoming player damage feedback
  through the presentation snapshot, with translucent HUD fallback cues when
  authored combat sprites are unavailable.
- [x] Carry weapon-system recoil into a short fixed-step first-person rig kick
  and recovery, without changing the simulation-owned aim or ballistics path.
- [x] Load authored `DefineComputerObjective` rows from the runtime level
  snapshot and resolve their exact English `objectives.res` text for the HUD;
  preserve links and completion/failure expressions as runtime metadata.
- [x] Bind the first authored interaction events (doors, terminals, switches,
  generators, vehicles, and generic pickups) to fixed-step mission-state
  expressions while retaining the legacy fallback completion path.
- [x] Port the verified-reference player `AreaActivate` volume and
  `EditVariable` add-before-sub update order into the runtime mission-state
  boundary, including authored orientation/dimensions and criteria filtering.
- [x] Evaluate authored `DefineComputerObjective` validity expressions in
  task order and publish the last valid definition to the runtime HUD.
- [x] Resolve each authored objective `LinkTaskId` to its level-object
  position and carry that immutable location through the runtime render
  snapshot, matching OpenIGI's map-computer data contract.
- [x] Route the vanilla map-computer rising-edge action through the fixed-step
  runtime, freeze player input while the state is open, and publish the active
  six-row objective set to a phosphor-style gameplay HUD overlay; align the
  default Right Ctrl crouch / C map bindings; the animated tactical camera now
  owns the gameplay-viewer transition. Add a renderer-free top-down projection
  so authored objective locations render as a grid/player marker/objective
  marker layer; tactical pan/zoom remains a later presentation-fidelity slice.
- [x] Copy fixed-step guard transforms and animation-request metadata into the
  immutable render snapshot before synchronizing the gameplay scene copy.
- [x] Reset the AI simulation clock when clearing a runtime world so restart
  does not retain stale delayed patrol/QVM timing.
- [x] Resolve Play-mode spawn from the authored HumanPlayer task before using
  editor-camera or level-start fallbacks, matching OpenIGI PlayerSpawn selection.
- [x] Publish fixed-step actor death state, transient switch/terminal pulses,
  and concrete/generic pickup aliases for authored mission expressions.
- [x] Port authored ConditionalContainer conditions and descendant visibility
  snapshots; apply them to the copied scene before AI registration and during
  fixed-step rendering without resurrecting gameplay-deleted objects.
- [x] Load vanilla `GuardGenerator` conditions and maximum-spawn metadata,
  gate its pre-authored soldier children in the fixed-step AI/render seam, and
  evaluate door-dependent conditions after authored doors publish state;
  dynamic soldier allocation remains an explicit follow-up.
- [ ] Replace remaining fallback patrol behavior with authored mission data in
  the selected fixture and add repeatable traversal/combat captures. Authored
  LevelFlow extraction is now authoritative; the synthetic zone is retained
  only for levels without that task. The selected retail AI 2205 QVM now runs
  through the bounded interpreter and verifies its patrol/alarm bindings.
- [ ] Add repeatable vanilla fixture captures for player traversal, AI combat,
  weapon/projectile timing, objective progression, audio, and restart.
- [ ] Run the authoritative Windows configure/build/test/play checks and record
  every remaining `inferred` or `placeholder` behavior.

### Checkpoint: Vertical slice

- [x] One mission fixture reaches success and failure deterministically.
- [x] Player, AI, weapon, task/QVM, objective, and restart paths execute without
  crashing.
- [ ] Existing editor regression tests remain green in the authoritative Windows
  build.

## Checkpoints and acceptance criteria

### Checkpoint: Runtime session

- [x] A session can be created, opened, paused, restarted, and closed
  without changing the editor snapshot or source-level object data.
- [x] Duplicate open/close and failed transition paths are deterministic and
  covered by focused tests.

### Checkpoint: Twin-window gameplay

- The editor and gameplay windows have separate focus/input/camera/HUD/render
  paths; switching focus does not restart simulation.
- Pausing or closing gameplay leaves the editor visible and interactive.
- A gameplay-window failure tears down only the runtime presentation and returns
  the editor to a usable state.

### Checkpoint: Playable vanilla fixture

- One selected vanilla mission can be entered, played for multiple seconds at
  the fixed simulation rate, fought through, interacted with, completed or
  failed, and restarted without process termination.
- Windows build/tests and an interactive smoke are recorded; local headless
  tests do not count as Windows proof.

## Risks and Mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| Current branch contains plausible but incomplete stubs | High | Replace stubs only behind focused tests; search for no-op paths before claiming behavior. |
| OpenIGI behavior differs from IGI1 | High | Mark evidence class in code/tests and require retail comparison before parity claims. |
| Editor/runtime coupling corrupts authoring state | High | Runtime copies/adapters, explicit snapshots, no implicit source writes. |
| Windows-only C++/OpenGL build is platform-sensitive | Medium | Run headless subsystem tests here, then require the Windows CMake target/CI as the authoritative build. |
| FreeGLUT/OpenGL context ownership can couple two windows accidentally | High | Keep window creation behind a host adapter, use FreeGLUT's current-context option so the renderer's loaded resources remain visible, verify current-window routing on Windows, and test session state independently before wiring presentation. |
| Scope is larger than one implementation turn | High | Keep this plan active, deliver vertical slices, and preserve a runnable checkpoint. |

## Evidence Sources

- `PORTING_SPECIFICATION.md` in this branch.
- `/Users/haseeb-mir/Documents/Code/open-igi/Agents.md`.
- OpenIGI reference symbols under `src/OpenIGI.Engine`, `src/OpenIGI.Game`, and
  `src/OpenIGI.Scripting`.
- OpenIGI window/loop contracts: `src/OpenIGI.Platform/IGameWindow.cs`,
  `src/OpenIGI.Platform.Desktop/SilkGameWindow.cs`,
  `src/OpenIGI.Engine/Flow/GameLoop.cs`, and
  `src/OpenIGI.Engine/Time/GameClock.cs`.
- Existing C++ parser, terrain, renderer, and runtime tests in this branch.
