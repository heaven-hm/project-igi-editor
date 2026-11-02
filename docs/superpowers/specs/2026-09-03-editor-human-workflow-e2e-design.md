# Editor Human-Workflow End-to-End Testing Design

## Goal

Make regressions in the visible IGI editor reproducible before runtime fixes.
The gate must exercise user-facing workflows across every installed
`D:\IGI1\MISSIONS\location0\level1` through `level14`, retain visual evidence,
and reject a run when the editor does not produce the visible state a human
expects.

This is a coverage contract, not a claim that finite automation can prove the
absence of every future defect. Every implemented user-facing command, every
applicable level/object family, and every data mutation has a named live
scenario or an explicit recorded exclusion. Adding a user-facing feature
without adding it to the catalogue fails the contract test.

## Constraints

- The installed corpus and live working directory are `D:\IGI1`.
- The editor launches visibly only through WMI `Win32_Process.Create` and must
  be responsive in Session 1.
- Scenario intent is data, not level-specific conditional logic in the runner.
- Mutating scenarios run serially. They snapshot changed game files, restore
  exact bytes afterward, and verify restoration hashes.
- Each scenario records action timing, screenshots, logs bounded by launch byte
  offset, state/file assertions, and a failure screenshot.
- Existing C++/GoogleTest format and policy tests remain required; they do not
  replace visible-editor evidence.

## Coverage catalogue

`tools/e2e/New-EditorWorkflowManifest.ps1` builds a manifest from a checked-in
human-workflow catalogue and the installed corpus: level directories, task
types, graph files, animation-capable objects, weather declarations, model
families, textures, and lightmaps.

The generator emits only applicable combinations. Animation cases cover each
level containing animation-capable AI; graph cases cover each graph-bearing
level; weather cases distinguish inactive, active exterior, and selected
indoor-building views where the corpus provides them. The report lists every
exclusion with its corpus reason. Silent skips are forbidden.

The corpus inventory is exhaustive for object instances and referenced assets,
not a sample. Each object receives a stable level/task/model anchor, authored
position/orientation, referenced textures, available LODs, and sound references.
The inventory is hashed so changed level data produces a new manifest instead
of silently reusing old anchors.

## Workflow coverage

### Every level

Every Level 1–14 receives startup/render health, task-tree, pause open/resume,
native/custom cursor transition, terrain-shortcut activation, level switching,
and graceful-quit scenarios. The pause menu separately exercises font/size,
autosave/interval, logging/enabled severity, music, lightmap mode, clip,
terrain/fog, reset, save, and quit controls with visual and persisted-state
assertions.

### Terrain

Every level opens terrain edit through `T` and shows the palette. Disposable
scenarios select raise, lower, soften, and flatten; alter radius/strength;
make one bounded edit; verify the changed terrain artifact; and prove exact
restoration. Terrain-menu controls get before/after screenshots and setting
checks where their state is persisted.

### Objects and assets

For each task type present in a level, the driver selects a deterministic
catalogue anchor and verifies the property panel identifies that type.
Editable transform/model fields get a reversible edit, save, reopen, and
restore sequence. Corpus validation resolves every referenced model/texture.
The visible model-picker imports at least one member of every discovered model
family and asserts registration, applied textures, non-empty rendered geometry,
and no texture-miss diagnostic.

Every renderable object instance also receives a deterministic camera orbit:
front, back, left, right, top, bottom, and four diagonal views where geometry
permits. Each view asserts projected presence and stable authored transform.
Texture coverage cross-checks every material reference against the resolved
archive and requires a non-empty live assignment. Models with LODs repeat the
orbit at each available LOD distance. Failures retain task ID, model ID, angle,
screenshot, and resolver diagnostics.

Sound coverage inventories every referenced sound and resolved archive path.
The visible workflow triggers each sound-capable object or menu control when a
deterministic trigger exists, checks resolution and process health, and records
non-triggerable sounds as explicit inventory results. Object creation scenarios
use a disposable level copy: create, set transform/model/material, save,
reload, delete, and verify exact restoration of the original files.

### Graphs, AI, and animations

For every graph-bearing level, a scenario opens the overlay, focuses it,
selects a node, reversibly changes a supported criterion or position, saves,
reloads, and checks graph data plus a screenshot. For each animation-capable AI
class represented by the corpus, a scenario selects it, starts and pauses
playback, compares frames for motion while playing and stability while paused,
and checks its active graph target where applicable. Authored patrols must
advance without teleporting; AI with no authored patrol remains idle.

### Weather, lightmaps, and distance visibility

QVM is decompiled into disposable workspace output to classify authored
weather. Generated scenarios assert that inactive weather is absent, active
rain/snow moves in exterior views, and active weather is occluded inside a
selected building. For lightmapped editable objects, Baked/Hybrid/Dynamic are
captured, a reversible transform triggers recalculation, and reload preserves
the expected result. Anchored far-camera building cases verify floor geometry
remains visible at distance.

## Oracles and evidence

Every workflow requires at least two independent observations:

- a UI oracle: stable color/layout region, image-difference metric, cursor
  state, or tolerant control-surface baseline;
- a state oracle: bounded log, parser-visible value, hash change, process
  health, or restored-file hash.

Generic image entropy is prohibited as a pass/fail metric because valid snow,
sky, and close-up textures naturally vary. Golden images are used only for
stable controls; scene checks use anchored state and regional metrics. Reports
link scenario, level, task/model/graph anchor, screenshots, assertions, and
restoration status.

## Gates

The regression command runs: catalogue/corpus contract; fast C++ tests and
every-level headless verification; non-mutating visible workflows; serial
mutating workflows with byte-exact restoration; then evidence audit. Any failed
scenario, missing evidence, un-restored file, or unexplained catalogue
exclusion fails the gate.

When a user reports a defect, the first change is a red scenario on the
deployed editor with retained evidence. Runtime code changes follow only after
that scenario exists. A fix is accepted only when its focused scenario and
full applicable matrix pass.

## Exclusions

Free-form human aesthetic judgment, network services, and unsupported editor
features are not automated. Each must be an explicit catalogue exclusion with
a reason; exclusions are never treated as passing coverage.
