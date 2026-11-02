# Editor Human-Workflow E2E Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a corpus-generated visible-editor regression gate that exercises every human-facing editor workflow across all applicable levels, objects, assets, and settings with screenshot and state evidence.

**Architecture:** A checked-in workflow catalogue defines user intent and expected observables. A generator decompiles the installed corpus into an exhaustive inventory of levels, task instances, model/texture/LOD/sound references, graphs, AI, animation, weather, and lightmaps, then emits explicit JSON scenarios. The existing WMI screenshot runner gains reusable camera, state, hash, restore, and evidence actions; scenarios remain data-driven and run serially against `D:\IGI1`.

**Tech Stack:** PowerShell 5.1, WMI `Win32_Process.Create`, Win32 input/window APIs, GDI screenshots, `igi1conv qvm decompile`, JSON manifests, SHA-256 hashes, CMake/GoogleTest, and the existing x86 editor.

**Spec:** `docs/superpowers/specs/2026-09-03-editor-human-workflow-e2e-design.md`

## Global Constraints

- All editor/test runs use `D:\IGI1` as corpus and working directory.
- Visible editor launch uses WMI; `Start-Process`, `Popen`, and hidden launchers are invalid evidence.
- Scenario intent is JSON data; the runner contains no level-specific production logic.
- Mutating scenarios snapshot and restore exact bytes and run serially.
- A scenario cannot pass without both a screenshot/UI oracle and a state/data oracle.
- Generic scene image entropy is not a pass/fail oracle.
- Existing unrelated worktree modifications and generated artifacts remain unstaged.

---

### Task 1: Workflow and corpus inventory contract

**Files:**
- Create: `tools/e2e/editor-workflow-catalogue.json`
- Create: `tools/e2e/New-EditorWorkflowManifest.ps1`
- Create: `tools/e2e/test-editor-workflow-manifest.ps1`
- Test data: `D:\IGI1\MISSIONS\location0\level1..14`

**Interfaces:**
- Catalogue entry: `{ name, action, applicability, requiredSteps, uiOracle, stateOracle }`.
- Inventory entry: `{ level, taskId, type, modelId, authoredPosition, authoredRotation, textures, lods, sounds, sourceHash }`.
- Generator command: `New-EditorWorkflowManifest.ps1 -GameRoot D:\IGI1 -OutputPath <manifest>`.

- [ ] **Step 1: Write the failing manifest contract.** Require all catalogue actions to have an applicable scenario or explicit exclusion, require levels 1–14, require exhaustive task IDs, and reject duplicate `{level,taskId,workflow}` keys.

- [ ] **Step 2: Run the contract to verify it fails.**

  Run: `pwsh -NoProfile -ExecutionPolicy Bypass -File tools/e2e/test-editor-workflow-manifest.ps1`

  Expected: FAIL because `New-EditorWorkflowManifest.ps1` and the catalogue do not exist.

- [ ] **Step 3: Add the catalogue and generator.** Decompile each `objects.qvm` to a disposable output directory using the installed converter, parse `Task_New` instances and declarations, discover graphs/AI/animation/weather/lightmap/model/texture/sound references, record SHA-256 source hashes, and emit explicit exclusions for absent capabilities.

- [ ] **Step 4: Run the contract to verify it passes.**

  Run: `Set-Location D:\IGI1; pwsh -NoProfile -ExecutionPolicy Bypass -File D:\Code\project-igi-editor\tools\e2e\test-editor-workflow-manifest.ps1`

  Expected: PASS with 14 levels, no duplicate anchors, no silent exclusions, and an inventory hash in the manifest.

- [ ] **Step 5: Commit the inventory layer.**

  Run: `git add -f tools/e2e/editor-workflow-catalogue.json tools/e2e/New-EditorWorkflowManifest.ps1 tools/e2e/test-editor-workflow-manifest.ps1; git commit -m "test: generate exhaustive editor workflow inventory"`

### Task 2: Runner evidence and reversible state primitives

**Files:**
- Modify: `tools/e2e/editor-e2e.ps1`
- Test: `tools/e2e/test-editor-workflow-manifest.ps1`

**Interfaces:**
- `capture_window_state` records client bounds, Session 1, responsiveness, and working set.
- `orbit_camera` sends a deterministic modifier/mouse sequence and records angle/distance.
- `assert_file_hash` compares a SHA-256 digest after a bounded wait.
- `snapshot_paths` and `restore_paths` copy only manifest-declared files and verify hashes.
- `assert_log_count` counts matches only after the scenario launch offset.

- [ ] **Step 1: Add failing contract cases** for unsupported orbit angles, paths outside `D:\IGI1`, missing restore hashes, and a failed screenshot/state pair.
- [ ] **Step 2: Run `editor-e2e.ps1 -ValidateOnly`** against those cases and verify each is rejected before launch.
- [ ] **Step 3: Implement the generic actions** with explicit bounds, per-step JSON records, failure screenshots, and `finally` restoration.
- [ ] **Step 4: Re-run the contract** and verify all invalid manifests fail while the existing 28-scenario corpus manifest still validates.
- [ ] **Step 5: Commit** with `git add tools/e2e/editor-e2e.ps1 tools/e2e/test-editor-workflow-manifest.ps1; git commit -m "test: add reversible e2e evidence primitives"`.

### Task 3: All-level editor controls and persistence

**Files:**
- Create: `tools/e2e/scenarios/editor-workflows.json`
- Modify: `tools/e2e/New-EditorWorkflowManifest.ps1`
- Test: `tools/e2e/test-editor-workflow-manifest.ps1`

- [ ] **Step 1: Add failing scenario requirements** for every level: load/render, task tree, `T` terrain mode, pause/resume, native/custom cursor, font/font size, autosave/interval, logging/severity, music, clip, lightmap mode, terrain/fog controls, level change, save, reset, and graceful quit.
- [ ] **Step 2: Run manifest validation** and verify it reports the missing generated workflows.
- [ ] **Step 3: Generate the scenarios** with data-driven pause-row coordinates from `BuildPauseMenuLayout`, screenshot assertions for each control surface, QSC/config assertions, and exact file restoration for mutations.
- [ ] **Step 4: Run the non-mutating level/control matrix visibly** from `D:\IGI1`; inspect every level report and screenshot.
- [ ] **Step 5: Run serial mutating controls** with `-AllowGameDataMutation`; require restored hashes for QSC/QVM/config/terrain files.
- [ ] **Step 6: Commit** the control scenarios and documentation.

### Task 4: Exhaustive object transform and visual orbit coverage

**Files:**
- Modify: `tools/e2e/New-EditorWorkflowManifest.ps1`
- Modify: `tools/e2e/editor-e2e.ps1`
- Create: `tools/e2e/scenarios/object-visual-workflows.json`
- Test: `tools/e2e/test-editor-workflow-manifest.ps1`

- [ ] **Step 1: Add a failing inventory assertion** requiring every renderable task instance to have authored position, authored rotation, model ID, and at least one visual anchor.
- [ ] **Step 2: Run it** and verify incomplete inventory records fail with task IDs.
- [ ] **Step 3: Generate one scenario per object instance** using the editor find bar to locate the task ID, Enter to open properties, F11/orbit camera to center the object, and the ten deterministic views: front/back/left/right/top/bottom plus four diagonals.
- [ ] **Step 4: Assert each view** with projected bounds, non-empty object pixels, transform log/state, and model/task identity; repeat at each discovered LOD distance.
- [ ] **Step 5: Run a bounded object batch** serially and verify every failure includes `{level,taskId,modelId,angle,lod}` and a PNG.
- [ ] **Step 6: Commit** the object visual workflow layer.

### Task 5: Model, texture, sound, and object-creation workflows

**Files:**
- Create: `tools/e2e/scenarios/asset-workflows.json`
- Modify: `tools/e2e/New-EditorWorkflowManifest.ps1`
- Modify: `tools/e2e/editor-e2e.ps1`
- Test: `tests/test_res_model_set.cpp`, `tests/test_audio_asset_resolver.cpp`, `tools/e2e/test-editor-workflow-manifest.ps1`

- [ ] **Step 1: Add failing corpus checks** for every referenced model family, material texture, LOD file, and sound archive path, plus one live model-picker workflow per family and one creation workflow per supported task family.
- [ ] **Step 2: Run the checks** and verify missing texture/model/sound references are reported before any editor launch.
- [ ] **Step 3: Generate live asset scenarios** that select the property model field, use the model picker, commit the model, capture the rendered object from the orbit views, assert applied texture count, and check bounded resolver logs.
- [ ] **Step 4: Generate object-creation scenarios** that create/set model/material/transform, save/reload, verify task-tree and viewport state, delete the object, and compare all snapshot hashes.
- [ ] **Step 5: Run the complete asset workflow batch** serially and inspect model/texture screenshots, including sniper-family coverage.
- [ ] **Step 6: Commit** the asset workflow layer and its C++ resolver regressions.

### Task 6: Graph, AI, and animation workflows

**Files:**
- Create: `tools/e2e/scenarios/graph-ai-animation-workflows.json`
- Modify: `tools/e2e/New-EditorWorkflowManifest.ps1`
- Modify: `tools/e2e/editor-e2e.ps1`
- Test: `tests/test_graph_overlay.cpp`, `tests/test_graph_parser.cpp`, `tests/test_animation.cpp`, `tests/test_ai_patrol_port.cpp`

- [ ] **Step 1: Add failing generated-case requirements** for every graph file, graph node criterion class, animation-capable AI class, authored patrol, and no-patrol AI exclusion.
- [ ] **Step 2: Run the manifest contract** and verify each missing workflow is red.
- [ ] **Step 3: Generate graph scenarios** that open the graph overlay, select a discovered node, nudge position/criterion, save/reload, compare graph bytes outside the declared edit, and capture before/after screenshots.
- [ ] **Step 4: Generate animation scenarios** that select each applicable AI anchor, start playback, capture two changed frames, pause, capture stable frames, and verify graph target/patrol logs.
- [ ] **Step 5: Run the graph/AI/animation matrix** across all applicable levels and inspect every retained failure image.
- [ ] **Step 6: Commit** the graph/AI/animation workflow layer.

### Task 7: Weather, lightmaps, and distance visibility

**Files:**
- Create: `tools/e2e/scenarios/environment-workflows.json`
- Modify: `tools/e2e/New-EditorWorkflowManifest.ps1`
- Modify: `tools/e2e/editor-e2e.ps1`
- Test: `tests/test_level_weather.cpp`, `tests/test_igi1conv_lightmap.cpp`, `tests/test_runtime_subsystems.cpp`

- [ ] **Step 1: Add failing generated cases** for inactive weather, active exterior rain/snow, indoor occlusion where a building anchor exists, Baked/Hybrid/Dynamic lightmaps, transform-triggered recalculation, and far-camera building floors.
- [ ] **Step 2: Run the cases against the deployed editor** and retain red screenshots before changing runtime code.
- [ ] **Step 3: Implement environment scenarios** from authored QVM data, using separate exterior/interior camera anchors and before/after frame differences; require lightmap output/hash changes after transform and exact restore afterward.
- [ ] **Step 4: Run all environment scenarios** serially and inspect Level 9/12 plus every other applicable level.
- [ ] **Step 5: Commit** the environment regression layer.

### Task 8: Pipeline, audit, and release gate

**Files:**
- Create: `tools/e2e/Invoke-EditorHumanWorkflowRegression.ps1`
- Modify: `tools/e2e/test-editor-regression.ps1`
- Modify: `docs/TESTS.md`
- Modify: `tools/e2e/README.md`
- Test: `tools/e2e/test-editor-workflow-manifest.ps1`

- [ ] **Step 1: Add a failing pipeline contract** requiring catalogue validation, generated-manifest validation, C++ suite execution, all non-mutating scenarios, all mutating scenarios, evidence directories, exclusions, and restoration reports.
- [ ] **Step 2: Run the contract** and verify the pipeline refuses missing stages and unexplained exclusions.
- [ ] **Step 3: Implement serial orchestration** with `D:\IGI1` working directory, WMI-only launch, separate artifact directories, no parallel editor instances, and non-zero exit for any failed stage.
- [ ] **Step 4: Run the full pipeline** after a Release Win32 build/deploy, then inspect report counts, screenshots, logs, and hashes for all levels and applicable workflows.
- [ ] **Step 5: Run the existing regression suite** and verify previously passing parser, asset, weather, pause, autosave, quit, graph, AI, animation, and lightmap tests remain passing.
- [ ] **Step 6: Commit and push** only when the complete gate is green or every remaining red result is a documented runtime defect with retained evidence.

