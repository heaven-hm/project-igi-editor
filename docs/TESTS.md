# IGI Editor — Test Suite

The test binary `igi_tests.exe` is a standalone **Google Test** runner. It must be co-located with `igi1ed.exe` and the game's `missions/` directory (e.g. `D:\IGI1\`). No source tree or build directory is needed at runtime.

---

## Quick Start

```powershell
# Run all tests (uses all 14 levels for verify-level — takes ~4 minutes)
.\igi_tests.exe

# Fast run — level 1 only for the slow verify-level integration tests (~18 seconds)
$env:IGI_TEST_LEVEL="1"; .\igi_tests.exe

# Skip the slow verify-level tests entirely (~2 seconds)
.\igi_tests.exe --gtest_filter="-AllLevels/VerifyLevelIntegration*"
```

## MCP Verification

The MCP additions are included in `igi_tests.exe` and can be focused with:

```powershell
$env:IGI_GAME_PATH="D:\IGI1"
$env:Path = (Join-Path (Get-Location) 'assets/dlls/x86') + ';' + $env:Path
.\igi_tests.exe --gtest_filter="Mcp*" --gtest_color=no
```

The focused MCP run contains 86 tests: 85 passed and one Windows symlink
containment test skipped because the test account lacks symlink privilege.
The current Release binary registers 675 tests across 107 suites. In the
latest local run, 601 passed, 33 were skipped, and 41 failed. The failures are
environment-gated: the checkout lacks the installed mission corpus under
`bin\Release\missions`, so parser, runtime-data, and `VerifyLevelIntegration`
checks cannot be treated as a source regression until the corpus is restored.

---

## Headless end-to-end lane (all 14 levels, under Wine)

`igi_tests.exe` covers the parsers and the runtime subsystems, but it never loads a
level's *objects* through the renderer's own resolution path. `scripts/e2e-headless.sh`
does that with the editor's GL-free modes, across every level, and asserts the pinned
baseline:

```bash
rm -f bin/Release/igi1ed.exe && cmake --build build-mingw --target igi-editor --parallel
bash scripts/e2e-headless.sh              # smoke + --run-tests + 14 verify passes (~1-2 min)
bash scripts/e2e-headless.sh --fast       # smoke + --run-tests only
bash scripts/e2e-headless.sh --levels "1 7"
```

It stages its own run directory and a writable **copy** of the game under
`scratch/e2e-headless/` (`Config::Init` recompiles `<IGI>/editor/qed/*.qsc` into `.qvm`
at every start, so the retail tree must never be the game path), then runs:

| Tier | Mode | What it proves |
| --- | --- | --- |
| smoke | `--smoke-level --level N ...` | every placement's model and every texture resolves through `Renderer_Objects`' own path |
| tests | `--run-tests` | the editor's native parser assertions |
| verify | `--verify-level --level N --skip-launch` | each level decompiles and its QSC parses to a non-empty object set |

Asserted baseline, and the script exits non-zero on any change to it:

* levels 1..14 load; **zero missing textures** anywhere;
* **level 7 fails on exactly one id, `s332_02_1`** — a KNOWN RETAIL DEFECT: the shipped
  `level7/objects.qvm` names a model that no shipped `.res` archive and no loose `.mef`
  provides. It is asserted as a defect rather than tolerated, so a *second* missing id
  (a new regression) is distinguishable from it, and level 7 starting to pass is also
  reported as a change to the documented baseline.

`--verify-level` without a game-run log cannot decide the AI comparison, so its per-level
`Result:` is always FAIL; the script asserts what that mode *can* decide on its own
(decompile + QSC parse counts) and says so in its output.

This lane needs Wine, the retail install and a staged tree, so it is an acceptance step,
not the inner loop — the inner loop stays `scripts/run-host-tests.sh`.

---

## Controlling Which Levels Are Tested

The `IGI_TEST_LEVEL` environment variable restricts the **verify-level integration test** to specific levels. The QVM round-trip tests always run all 14 levels (they are fast).

| Command | Levels tested |
| --- | --- |
| `.\igi_tests.exe` | All 14 (default) |
| `$env:IGI_TEST_LEVEL="5"; .\igi_tests.exe` | Level 5 only |
| `$env:IGI_TEST_LEVEL="10"; .\igi_tests.exe` | Level 10 only |
| `$env:IGI_TEST_LEVEL="1,3,7"; .\igi_tests.exe` | Levels 1, 3 and 7 |

---

## Filtering by Test Suite

Use `--gtest_filter` to run a subset. The pattern supports `*` wildcards and `:` to combine multiple patterns.

```powershell
# List every registered test name
.\igi_tests.exe --gtest_list_tests

# Run only parser tests
.\igi_tests.exe --gtest_filter="DatParserTest*:ResParserTest*:TexParserTest*"

# Run a single suite
.\igi_tests.exe --gtest_filter="QscLexerTest*"

# Run verify-level for a specific level via filter (no env var needed for QVM tests)
$env:IGI_TEST_LEVEL="5"; .\igi_tests.exe --gtest_filter="AllLevels/VerifyLevelIntegration*"

# Run QVM round-trip for level 10 only
.\igi_tests.exe --gtest_filter="AllLevels/QvmGameRoundTripTest.ObjectsQvmDecompilesAndReparses/Level10"

# Run both verify-level and QVM round-trip for level 3
$env:IGI_TEST_LEVEL="3"; .\igi_tests.exe --gtest_filter="AllLevels/VerifyLevelIntegration*:AllLevels/QvmGameRoundTripTest.ObjectsQvmDecompilesAndReparses/Level3"
```

---

## Test Suites

### Unit Tests (no game files required)

| Suite | Tests | What it covers |
| --- | :---: | --- |
| `QscLexerTest` | 53 | All token types, keywords, operators, escape sequences, qualified identifiers, line/block comments, error recovery with position reporting |
| `QscParserTest` | 42 | AST node types, operator precedence, control flow (`if`/`else`/`while`), assignment associativity, parenthesised expressions, call/arg counters, error cases |
| `QvmRoundTripTest` | 19 | Synthetic compile→write→parse→decompile cycles; identifier and string pool integrity; structural re-parse of decompiled output; fixture-based round-trip with `level01_simple.qsc` |
| `ConfigTest` | 10 | Config defaults, field value ranges, singleton behaviour, multi-init safety, keybinding load |
| `UtilsTest` | 35 | `Trim`, `Split`, `TryParseInt/Float/Double`, `ToString` — all edge cases |
| `PosMatchTest` | 4 | `PosMatch()` exact-match and per-axis mismatch logic |
| `OriMatchTest` | 4 | `OriMatch()` epsilon tolerance (passes just below, fails at and above threshold) |
| `ParseLogTest` | 9 | `ParseLog()`: model ID/type extraction, position/orientation flags, tex/mesh flags, last-occurrence selection, level marker filtering; fixture: `verify_log_l1.txt` |
| `CrossRefTest` | 8 | `CrossRef()`: found/missing/pos-mismatch/ori-mismatch/tex-mismatch/mesh-mismatch categorisation; rail object matching |
| `ParseQscObjectsTest` | 2 | `ParseQscObjects()` with `level01_simple.qsc` fixture and missing-file guard |

### Parser Integration Tests (require game files in same directory as exe)

| Suite | Tests | Game file used |
| --- | :---: | --- |
| `DatParserTest` | 6 | `missions/location0/level1/level1.dat` — model count, names, textures, JSON output shape |
| `GraphParserTest` | 5 | `missions/location0/level1/graphs/graph1.dat` — node count, IDs, coordinates, material range |
| `ResParserTest` | 6 | `missions/location0/level1/models/level1.res` — entry count, names, data presence, callback |
| `TexParserTest` | 5 | `missions/location0/level1/textures/level1.res` — extracts first `.tex`, version, image dimensions, pixel data size |
| `MtpParserTest` | 2 | `common/common.mtp` — model/texture count |
| `FntParserTest` | 5 | `computer/computer/font1.fnt` — glyph count, atlas dimensions, pixel data size |

### Multi-Level Tests (require game files for levels 1–14)

| Suite | Tests | What it covers |
| --- | :---: | --- |
| `QvmGameRoundTripTest` | 14 | Parses real `objects.qvm` for each level, decompiles to QSC, asserts the output re-lexes and re-parses cleanly. Skips gracefully if a level file is missing. |
| `VerifyLevelIntegration` | 1–14 | Launches `igi1ed.exe --verify-level N`, waits up to 35 s (15 s inner editor timeout + overhead), asserts exit code 0. Controlled by `IGI_TEST_LEVEL`. |

---

## Test Counts

| Scope | Tests |
| --- | :---: |
| Unit tests only | 173 |
| Parser integration tests | 29 |
| QVM game round-trips (all 14 levels) | 14 |
| Verify-level (all 14 levels) | 14 |
| **Total — all levels** | **230** |
| **Total — `IGI_TEST_LEVEL=1`** | **229** |
| Focused MCP tests | 86 |
| Current `igi_tests.exe` registration | **675 across 107 suites** |

---

## Fixtures

Fixture files are copied next to `igi_tests.exe` in a `fixtures/` subdirectory by the CMake post-build step. They are synthetic and committed to the repository.

| File | Used by | Purpose |
| --- | --- | --- |
| `fixtures/level01_simple.qsc` | `QvmRoundTripTest`, `ParseQscObjectsTest` | Minimal two-line QSC with a `SplineObjWaypoint` task call and model ID `322_01_1` |
| `fixtures/verify_log_l1.txt` | `ParseLogTest` | Synthetic editor log covering level 1 and level 2 sections; tests last-occurrence selection and cross-level filtering |

---

## Build

```powershell
# Configure (must be Win32 — game and editor are 32-bit)
cmake -B build -G "Visual Studio 17 2022" -A Win32

# Build test binary only
cmake --build build --config Release --target igi_tests -j 1

# Deploy to game directory
Copy-Item bin\Release\igi_tests.exe  D:\IGI1\igi_tests.exe  -Force
Copy-Item bin\Release\fixtures\*     D:\IGI1\fixtures\      -Force
```

> **Note:** Always deploy the `fixtures\` directory alongside `igi_tests.exe`. Without it, all fixture-dependent tests fail.

---

## Live editor end-to-end tests

Google Test validates parsers and runtime policies, but it cannot prove that a visible editor accepts mouse/keyboard input, renders a level, imports a model with its textures, or preserves a change after restart. The data-driven live runner covers those paths with WMI-launched editor processes, Session 1 health checks, screenshots, bounded log/file assertions, and JSON/Markdown reports.

Run from the installed game directory so the editor and all assets resolve consistently:

```powershell
Set-Location D:\IGI1
$runner = 'D:\Code\project-igi-editor\tools\e2e\editor-e2e.ps1'
$manifest = 'D:\Code\project-igi-editor\tools\e2e\scenarios\editor-regression.json'

# Validate scenario structure without launching the editor
& pwsh -NoProfile -ExecutionPolicy Bypass -File $runner -GameRoot D:\IGI1 -ScenarioPath $manifest -ValidateOnly

# Non-mutating smoke/regression scenarios
& pwsh -NoProfile -ExecutionPolicy Bypass -File $runner -GameRoot D:\IGI1 -ScenarioPath $manifest -ScenarioName 'level*'

# Persistence, pause settings, and model import; explicitly allows game-data writes
& pwsh -NoProfile -ExecutionPolicy Bypass -File $runner -GameRoot D:\IGI1 -ScenarioPath $manifest -AllowGameDataMutation
```

Each run writes `run.json`, `run.md`, the copied manifest, one JSON record per scenario, and checkpoint/failure PNGs below `artifacts\e2e\`. Coordinates and optional `inputScale` are scenario data because display scaling is a machine/runtime property. Do not run mutating scenarios in parallel; they intentionally exercise the same installed editor state and reset their test edits through the UI where the manifest provides cleanup steps.

### Corpus-wide live matrix

The focused manifest is deliberately small for deep workflows, but it must not
be mistaken for whole-corpus regression. Generate a second manifest from the
installed data:

```powershell
Set-Location D:\IGI1
$matrix = 'D:\Code\project-igi-editor\artifacts\e2e\corpus-matrix-manifest.json'
& 'D:\Code\project-igi-editor\tools\e2e\New-EditorCorpusManifest.ps1' -GameRoot D:\IGI1 -OutputPath $matrix
& 'D:\Code\project-igi-editor\tools\e2e\test-editor-corpus.ps1'
& 'D:\Code\project-igi-editor\tools\e2e\editor-e2e.ps1' -GameRoot D:\IGI1 -ScenarioPath $matrix -ArtifactsRoot 'D:\Code\project-igi-editor\artifacts\e2e\corpus-matrix-run'
```

This generates two visible-editor scenarios for each Level 1–14. The smoke
scenario records load completion, resource-file presence, weather resolution,
texture-miss diagnostics, viewport and pause screenshots, cursor-state
transitions, and graceful shutdown. The terrain scenario presses `T` and
requires the terrain-edit palette in its screenshot. It complements the
focused model-import, property-edit, save/reopen, and pause-setting scenarios
above.

### Editor control and persistence workflows

The workflow catalogue (`editor-workflow-catalogue.json`) declares the
editor-level controls, and `New-EditorWorkflowManifest.ps1` emits a generated
scenario anchor for each control on every applicable level. The always-on
controls (pause/resume, task tree, terrain shortcut, cursor state, font,
autosave, logging, music, collision/clip, save, reset, graceful quit) are
required on all 14 levels with no silent exclusions; the conditional controls
(lightmap mode, terrain fog, level change) require an explicit corpus
exclusion where the level lacks the feature. `test-editor-workflow-manifest.ps1`
enforces this contract.

The checked-in `tools/e2e/scenarios/editor-workflows.json` provides the
concrete runnable per-control scenarios (pause-row coordinates computed from
`BuildPauseMenuLayout` for the 1536x864 editor client). Validate them without
launching:

```powershell
& 'D:\Code\project-igi-editor\tools\e2e\editor-e2e.ps1' -ValidateOnly `
  -ScenarioPath 'D:\Code\project-igi-editor\tools\e2e\scenarios\editor-workflows.json'
```

Config-persisting controls (font, autosave, logging, music) snapshot
`qedconfig.qsc` and restore it; level save/reset snapshot the level
`objects.qvm` and restore it. Run them serially with mutation allowed only
against disposable copies or with byte-exact restoration:

```powershell
Set-Location D:\IGI1
& 'D:\Code\project-igi-editor\tools\e2e\editor-e2e.ps1' -GameRoot D:\IGI1 `
  -ScenarioPath 'D:\Code\project-igi-editor\tools\e2e\scenarios\editor-workflows.json' `
  -ArtifactsRoot 'D:\Code\project-igi-editor\artifacts\e2e\editor-workflows-run'
```

### Object visual orbit coverage

The workflow generator classifies every decompiled `Task_New` instance as
renderable when it carries an authored model reference and a 3-component
position (orientation is recorded but not required, since placed objects such
as soldiers and cameras carry yaw-only angle fields), and emits one
`object-visual-orbit` scenario anchor per renderable instance. The anchor
records the model ID, authored position/rotation, discovered LODs, and the ten
deterministic views (front/back/left/right/top/bottom and the four diagonals),
so a failure can identify `{level, taskId, modelId, angle, lod}`. The contract
in `test-editor-workflow-manifest.ps1` asserts the renderable classification is
internally consistent and every renderable instance has exactly one orbit
anchor carrying all ten views. Renderable instances number in the thousands
across the corpus, so the checked-in runnable batch is a bounded subset.

The checked-in `tools/e2e/scenarios/object-visual-workflows.json` batch runs
one named-ID renderable object per level where one exists, using the find bar
to locate the task ID and the ten `orbit_camera` views with per-view
screenshots. Validate it without launching:

```powershell
& 'D:\Code\project-igi-editor\tools\e2e\editor-e2e.ps1' -ValidateOnly `
  -ScenarioPath 'D:\Code\project-igi-editor\tools\e2e\scenarios\object-visual-workflows.json'
```

Live-run the bounded batch serially with:

```powershell
Set-Location D:\IGI1
& 'D:\Code\project-igi-editor\tools\e2e\editor-e2e.ps1' -GameRoot D:\IGI1 `
  -ScenarioPath 'D:\Code\project-igi-editor\tools\e2e\scenarios\object-visual-workflows.json' `
  -ArtifactsRoot 'D:\Code\project-igi-editor\artifacts\e2e\object-visual-run'
```

### Asset corpus resolution and model workflows

The workflow generator classifies every model reference. A real mesh is
resolved when at least one LOD file is discoverable in the importable archive
set (the level's own `.res`, every other level's `.res` — a level can reference
a model packed in a sibling level and the editor auto-imports it — and the
common `location0.res`). Helper models (collision boxes `colbox*`, spline
`waypoint`, `joint_fixer`, bare numeric spline indices) are intentional
non-mesh placeholders and are recorded but never treated as corpus misses. Any
remaining reference is a genuine corpus finding recorded in
`discovery.unresolvedModels` per level; the contract in
`test-editor-workflow-manifest.ps1` asserts the classification is consistent
and every unresolved model is listed. The current corpus has one genuine
finding: a level-7 `EditRigidObj` referencing `s332_02_1`, a model absent from
every mission archive — a candidate for the editor's missing-model/auto-import
path. Bare single-LOD meshes (`switch.mef`, `mapcomputer.mef`) and
cross-level references (a level-N object referencing a model packed in a
sibling level's archive) resolve correctly.

`tests/test_res_model_set.cpp` pins the resolver contract: authored
LOD-suffixed references resolve against archive entries exactly, base family
names do not match, and helper/bare-numeric names never match a mesh archive.

The checked-in `tools/e2e/scenarios/asset-workflows.json` batch selects one
named-ID resolved-model object per level (SCamera, HumanSoldier, Building,
Door, EditRigidObj families), opens its property panel, and orbits it through
the ten views. Validate it without launching:

```powershell
& 'D:\Code\project-igi-editor\tools\e2e\editor-e2e.ps1' -ValidateOnly `
  -ScenarioPath 'D:\Code\project-igi-editor\tools\e2e\scenarios\asset-workflows.json'
```

### Visual regression gate

The focused workflows do not cover every visual failure. The separate
`editor-visual-regressions.json` manifest exercises real input and screenshot
assertions for the terrain `T` workflow, weather inside a selected building,
and floor texture retention after moving away from a building. Run the serial
gate with:

```powershell
& 'D:\Code\project-igi-editor\tools\e2e\test-editor-regression.ps1' `
  -GameRoot D:\IGI1 -AllowGameDataMutation
```

The screenshot assertions are generic runner capabilities: color ratios test
stable UI/material regions and difference ratios test whether a scene region
changes between two captured frames. Each red failure retains both checkpoint
PNGs and the JSON step record for diagnosis.

### Deterministic native visual-integrity gate

The smart native runner adds a stricter, target-scoped gate to the loader
checks. Its default `-VisualIntegrityPolicy Required` requires a visual
integrity result of `PASS` for every selected object; `ReportOnly` is for
diagnostics and is not release acceptance. The gate evaluates deterministic
object/material ID masks, depth evidence, projected part coverage, and
independently rendered attachment coverage. It does not use an image
classifier or model-specific pass rules.

Run the focused runner contract test without launching a live capture:

```powershell
& pwsh -NoProfile -ExecutionPolicy Bypass -File `
  'D:\Code\project-igi-editor\tools\e2e\test-smart-native-capture-session.ps1'
```

For live fixture coverage, use `Invoke-SmartNativeCaptureSession.ps1` directly
when task identity must be explicit. `-TaskIds` preserves authored IDs,
including anonymous IDs such as `-1#907`; the `e2e_live_test.cmd` convenience
wrapper does not expose that parameter.

```powershell
$native = 'D:\Code\project-igi-editor\tools\e2e\Invoke-SmartNativeCaptureSession.ps1'
$out = 'D:\Code\project-igi-editor\artifacts\visual-integrity-level12-' + (Get-Date -Format yyyyMMdd-HHmmss)
& pwsh -NoProfile -ExecutionPolicy Bypass -File $native `
  -GameRoot D:\IGI1 -EditorExePath D:\Code\project-igi-editor\bin\Release\igi1ed.exe `
  -Level 12 -Category Buildings -ModelIds '405_02_1,463_01_1' `
  -TaskIds '570,-1#907' -MaxObjects 0 -ViewCount 10 -Video `
  -VisualIntegrityPolicy Required -ArtifactsRoot $out
```

The expected fixture result is a batch `PASS`: Watchtower (`405_02_1`, task
`570`) and WinchHouse (`463_01_1`, task `-1#907`) both pass the required visual
integrity policy with no findings. The verified run is recorded at
`artifacts/visual-integrity-level12-depthfix-20260907-155140/`.

Native artifacts are written below the explicit `-ArtifactsRoot`, with the
batch summary at `batch.json`. Each selected object has a directory under
`screenshots/obj-<index>-task<id>-<model>/` containing the copied stills,
`evidence.jsonl`, `visual-integrity.json`, object/material masks, depth data,
and diagnostic overlays. The batch also records the inventory path and SHA-256,
editor executable and SHA-256, source path and SHA-256, task IDs, policy, and
pass/fail status. Generated screenshots, videos, and live artifacts are local
evidence and are not source files to commit.
