# Gameplay Runtime TODO

- [ ] Windows CMake configure/build (authoritative environment still required).
- [x] Headless focused runtime test harness and isolated runtime smokes for the
      current gameplay slices; the authoritative GoogleTest/Windows run still
      requires the Windows dependency environment.
- [x] Final static-geometry line-of-sight smoke: wall-occluded player and
      guard damage was blocked; open combat and mission extraction passed.
- [x] Absolute-deadline 30 Hz clock and edge-case tests.
- [x] Runtime task ownership and lifecycle safety.
- [x] Runtime session/world ownership and editor snapshot isolation.
- [x] Ground/roof queries and multi-height wall sweep.
- [x] Player controller integration, authored tuning, and collision regression tests.
- [x] Weapon cadence/ammunition, world-occluded damage, AI patrol/combat, bounded
      QVM execution, and mission vertical-slice wiring.
- [x] Vanilla thrown-weapon categories with deterministic projectile collision,
      bounce/fuse/impact detonation, blast line-of-sight, flash exposure, and
      live Play-mode projectile presentation.
- [x] Authored ExplodeObject loading, delayed/stateful destruction, blast damage,
      projectile-triggered chain reactions, and transient Play-mode fireball
      presentation.
- [x] Authored ConditionalSound rising/falling edges with Windows per-task
      looping channels and deterministic stop/close lifecycle.
- [x] Authored ConditionalContainer gates publish fixed-step visibility
      snapshots for descendant render/collision/interaction state; initial
      hidden guards are excluded from AI registration and runtime visibility
      restoration preserves pickup, death, and explosion deletions.
- [x] Load vanilla `GuardGenerator` condition/max-spawn fields and gate
      pre-authored soldier children in AI, collision, combat, and Play-mode
      render visibility; dynamic soldier allocation remains pending.
- [x] Impact rockets collide with living guard volumes before static geometry;
      fixed-step detonation and blast damage are covered by a runtime smoke.
- [x] Authored guard weapon selection, scripted/patrol animation request
      delivery, and fixed-step weapon zoom state.
- [x] Port vanilla first-person weapon-change lowering/raising timing and
      expose the fixed-step view angles to Play-mode weapon presentation.
- [x] Resolve authored vanilla weapon and gameplay sounds from loose WAVs or
      packed `COMMON/SOUNDS/SOUNDS.RES` entries with a session cache; keep the
      catalog's weapon sound names aligned with the vanilla QVM evidence.
- [x] Route fixed-step sound intents through `GameplayHost` so `RuntimeWorld`
      remains platform-independent while Windows still plays authored/fallback
      one-shots and conditional loops.
- [x] Expose fixed-step firearm muzzle flash state through the presentation
      snapshot and render a labeled fallback cue when authored sprites are absent.
- [x] Expose fixed-step guard muzzle flashes and player incoming-damage feedback
      through the render snapshot; render world-space guard cues and a readable
      translucent hit overlay when authored effects are unavailable.
- [x] Apply the weapon system's recoil result to the first-person rig through a
      deterministic three-tick presentation recovery.
- [x] Parse vanilla `DefineComputerObjective` rows from the copied level task
      snapshot and resolve their English `objectives.res` text for the Play-mode
      HUD; preserve authored map links and state expressions.
- [x] Emit fixed-step mission-state events for authored door, terminal, switch,
      generator, vehicle, and generic-pickup interactions; evaluate the
      preserved completion/failure expressions after each interaction tick.
- [x] Port the verified-reference `AreaActivate` oriented-volume query and
      `EditVariable` add-before-sub fixed-step latch for player-authored
      mission progression; keep unsupported criteria/entities explicit.
- [x] Select the last authored `DefineComputerObjective` definition whose
      `Objectives Valid` expression evaluates true, matching vanilla task
      update order while preserving objective-set progression.
- [x] Resolve authored objective `LinkTaskId` values to copied level-object
      positions and expose the active target through `RuntimeRenderSnapshot`.
- [x] Add the grounded vanilla map-computer action, held-key edge suppression,
      six-row objective snapshot, phosphor-style gameplay HUD fallback, and
      player movement/look/interaction input freeze while it is open;
      default input now matches OpenIGI's Right Ctrl crouch / C map binding;
- [x] Port the OpenIGI map-computer camera transition as a renderer-free seam:
      authored ascend/boot/open and shutdown/descend timing, live eye/vantage
      hand-off, geometric FOV zoom, and display envelopes are fixed-step tested.
- [x] Attach the map-camera transition to the Windows gameplay viewer and
      `RuntimeRenderSnapshot.camera`; the first-person weapon is suppressed
      during the tactical transition and the live eye is restored on close.
- [x] Add a renderer-free tactical map projection and draw a top-down grid,
      player marker, and color-coded authored objective markers from the
      immutable `RuntimeRenderSnapshot`.
- [ ] Add tactical map pan/zoom interaction; the current projection uses the
      authored camera framing and the phosphor HUD remains the safe fallback
      for map rows.
- [x] Publish moving guard transforms and animation-request metadata through
      `RuntimeRenderSnapshot`; gameplay scene synchronization consumes that
      immutable copy instead of iterating the mutable AI vector directly.
- [x] Reset the AI fixed-step simulation tick with guard/event state so F5 and
      session restart replay delayed patrol behavior deterministically.
- [x] Prefer the authored vanilla `HumanPlayer` task (script id zero, then the
      first authored player) for Play-mode spawn placement; retain editor-camera
      and level-start positions only as incomplete-level fallbacks.
- [x] Add an opt-in vanilla fixture parity test for authored level-one QVM
      gameplay definitions and graph navigation data; proprietary assets remain
      external through `IGI_VANILLA_ROOT`.
- [x] Publish authored player/guard death variables and one-tick interaction
      pulses; preserve concrete and generic pickup state aliases used by the
      vanilla objective expressions.
- [x] Replace the inferred landing-impact rule with the verified OpenIGI
      vanilla speed-to-health formula, direct-health damage path, authored fall
      sound selection, guard hearing-radius event, and regression coverage.
- [x] Port the verified-reference HumanMotion airborne gravity, ladder-slide
      gravity/drag, air-control basis, and root-motion scale/rotation order;
      expose animation-local deltas at the fixed-step player boundary.
- [x] Port the verified-reference ladder climb line, mount offsets, activation
      geometry, and four-phase traversal state machine as a renderer-free seam.
- [x] Replace the renderer's ad-hoc magic-object scan with a shared parser that
      resolves `TASKTYPE_LADDER` by name rather than by a process-local integer.
- [x] Discover attached ladder models during gameplay setup and copy their
      transformed magic-vertex placement data into RuntimeWorld.
- [x] Bind ladder mount, climb, top transition, slide, and dismount input to
      RuntimeWorld; the no-animation path uses a deterministic `inferred`
      fixed-step fallback so Play mode remains traversable.
- [x] Connect authored vanilla player locomotion and ladder animation
      root-motion/completion events to `PlayerInputCmd`; ladder sliding keeps
      its authored clock but uses the collision-safe physics integrator until
      the slide track has verified collision-safe root motion.
- [x] Gameplay host/input focus, pause, restart, and editor restore integration.
- [x] Keep gameplay collision, interaction, and ladder discovery on the runtime
      object snapshot even while the editor window owns repaint/focus.
- [x] Extract and test an explicit `RuntimeSession` state machine with
      editor-snapshot isolation and deterministic restart/close behavior.
- [ ] Finish the separate native gameplay window boundary. `GameplayWindowHost`
      now creates the Windows/FreeGLUT window, routes gameplay callbacks,
      focus, viewport, and close recovery; Windows interactive verification and
      independent renderer ownership remain pending.
- [ ] Move gameplay presentation/HUD/camera ownership behind the gameplay host;
      `GameplayHost` now owns an OpenGL-free `RuntimeRenderer` snapshot and the
      gameplay HUD/weapon/projectile paths consume it, while the actual scene
      draw and GL asset cache still run through `App::Frame()`. The editor now
      repaints its authoring scene through a render-only target while gameplay
      remains active.
- [x] Add explicit `F5` apply/restart semantics for a live gameplay session;
      the session captures the replacement snapshot, rebuilds copied runtime
      objects, resets mission/AI state, and never silently mutates authoring
      data. `F6` focuses the editor for authoring changes and `F7` returns to
      gameplay; switching focus does not restart simulation. Auto-save,
      terrain editing, and level switching stay gated during an active run
      until immutable terrain/asset snapshots are complete.
- [ ] Bind/verify selected-vanilla-fixture traversal (animation events and
      player input), authored mission patrol routes, and non-demo extraction
      conditions from OpenIGI/retail evidence. Authored LevelFlow now disables
      the synthetic extraction fallback; the fallback remains only for levels
      without a LevelFlow task. The fixture parity suite now executes retail AI
      2205 and verifies its authored patrol-path and alarm-control bindings.
- [ ] Final Windows verification and evidence/limitations report.

## Current evidence boundary

- `verified-reference`: fixed-step scheduling and player collision constants
  plus the landing-impact speed, damage, sound-boundary, and direct-health
  rules traced to OpenIGI source; the branch preserves the evidence labels in
  code.
- `implemented-slice`: player movement/jump/crouch/health, weapon states,
  world-occluded hits, guard patrol/perception/combat, authored weapon
  selection, projectile simulation, flash exposure, muzzle flash, guard muzzle
  flashes, incoming-damage feedback, recoil, zoom, landing audio and lazy
  packed/loose vanilla sound resolution, authored objective text,
  objective-set progression, authored conditional-container visibility,
  authored GuardGenerator pre-authored-child gating,
  health/armor HUD, authored objective link positions, and objective/extraction
  flow. The map-computer input/state, six-row objective HUD fallback, animated
  tactical camera, and objective marker projection now cross the gameplay
  render boundary; tactical map pan/zoom interaction remains open.
- `verified-reference` motion seam: OpenIGI HumanMotion's airborne gravity,
  ladder-slide integrator, movement-slot air control, and root-motion transform
  are implemented and covered; the fixed-step player animation driver now feeds
  imported vanilla locomotion and ladder clips, with keyboard/physics fallback
  retained when a requested clip is unavailable.
- `verified-reference` ladder seam: placement, activation, traversal phases,
  ladder-slide integration, mount geometry, and authored climb/top event edges
  are covered; interactive RuntimeWorld input remains deterministic when an
  authored clip is unavailable, and slide collision remains physics-owned.
- `architecture-gap`: a controlled gameplay window now owns gameplay focus,
  input callbacks, relative mouse recentering, viewport, and close recovery,
  and `GameplayHost` owns an OpenGL-free presentation snapshot. The actual
  scene draw and GL asset cache still run through `App::Frame()`. The editor
  repaints its authoring scene through a render-only target during active
  gameplay; a fully independent GL `RuntimeRenderer` remains open.
- `inferred` or `placeholder`: some root-motion speeds, fallback guard patrol,
  fall-impact camera/hearing presentation, fallback extraction placement, and
  the normalized QVM seam are not proof of full retail IGI1 behavior. Actual
  Windows execution against the supplied vanilla assets remains a required
  verification step.
