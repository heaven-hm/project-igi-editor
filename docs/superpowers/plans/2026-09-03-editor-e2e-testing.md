# Editor End-to-End Regression Testing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or **superpowers:executing-plans** (recommended). Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a data-driven PowerShell harness that drives the real editor from `D:\\IGI1`, captures screenshots, checks bounded logs and process health, and reports regression results for rendering, pause controls, persistence, and model/texture import.

**Architecture:** Keep the production editor unchanged. A manifest validator and a single generic runner interpret ordered JSON steps. The runner launches the editor through WMI, uses Win32 input and GDI screen capture for human-like interaction, scopes log assertions to the current process, and emits machine-readable and human-readable evidence.

**Tech Stack:** PowerShell 5+/Windows WMI `Win32_Process.Create`, Win32 `SendInput`/window APIs, `System.Drawing`, `System.Windows.Forms`, JSON manifests, existing GoogleTest/CMake regression suite.

**Spec:** `docs/superpowers/specs/2026-09-03-editor-e2e-testing-design.md`

## Global Constraints

- All editor/game runs use `D:\\IGI1` as the working directory and corpus root.
- Visible editor launch uses `Win32_Process.Create`; no `Start-Process`, `Popen`, Explorer, or hidden child launch.
- Scenarios remain data-driven; level/model-specific values live in JSON.
- Mutating scenarios require `-AllowGameDataMutation`.
- Do not stage existing screenshots, `config.ini`, `.opencode`, or unrelated artifacts.

---

### Task 1: Add a failing harness contract test

**Files:**
- Create: `tools/e2e/test-editor-e2e.ps1`
- Create: `tools/e2e/scenarios/invalid-missing-step-id.json`

**Interfaces:**
- Consumes: the future `tools/e2e/editor-e2e.ps1 -ValidateOnly -ScenarioPath <path>` command.
- Produces: a nonzero result for malformed manifests and a zero result for the committed valid manifest.

- [ ] **Step 1: Write the contract test**

The test script invokes the runner in validation-only mode and fails unless malformed JSON is rejected and the valid scenario manifest is accepted. It must never launch a GUI during validation.

- [ ] **Step 2: Run the contract test**

Run from `D:\\IGI1`:

```powershell
& 'D:\\Code\\project-igi-editor\\tools\\e2e\\test-editor-e2e.ps1'
```
Expected before implementation: FAIL because `editor-e2e.ps1` does not exist.

- [ ] **Step 3: Commit the test contract**

```powershell
git add -f tools/e2e/test-editor-e2e.ps1 tools/e2e/scenarios/invalid-missing-step-id.json
git commit -m "test: define editor e2e manifest contract"
```

### Task 2: Implement validation, WMI execution, and reporting

**Files:**
- Create: `tools/e2e/editor-e2e.ps1`
- Create: `tools/e2e/scenarios/editor-regression.json`
- Create: `tools/e2e/README.md`

**Interfaces:**
- Consumes: JSON scenario manifests with the vocabulary defined in the spec.
- Produces: `-ValidateOnly`, `run.json`, and `run.md`; validation errors identify scenario and step IDs.

- [ ] **Step 1: Implement validation only**

Validate required top-level fields, unique step IDs, supported step types, coordinate/region shapes, timeout ranges, and mutation permission. Return exit code 0 only when every scenario is valid.

- [ ] **Step 2: Run the contract test and confirm green**

```powershell
Set-Location D:\\IGI1
& 'D:\\Code\\project-igi-editor\\tools\\e2e\\test-editor-e2e.ps1'
```

Expected: malformed manifest is rejected and the valid manifest is accepted.

- [ ] **Step 3: Implement visible execution**

Use `Win32_Process.Create` with the editor path and game root under `D:\\IGI1`; verify SessionId 1, `Responding`, and working set. Support key, click, and text actions; focus the editor window; capture the desktop through GDI/System.Drawing.

- [ ] **Step 4: Implement bounded assertions and reports**

Capture the log byte offset at launch and search only appended bytes. Evaluate tolerant configurable screenshot-region metrics. Save a failure screenshot before cleanup and write step timings, screenshots, log offsets, command line, process ID, and failures to JSON and Markdown.

### Task 3: Add real regression scenarios

**Files:**
- Modify: `tools/e2e/scenarios/editor-regression.json`
- Modify: `tools/e2e/README.md`
- Modify: `docs/TESTS.md`

**Interfaces:**
- Consumes: the generic runner actions.
- Produces: data-driven scenarios for weather, Level 12 visibility, pause logging, persistence, and model/texture import.

- [ ] **Step 1: Add weather and startup visibility scenarios**

Use manifest-provided levels and log patterns; require the viewport to change from a blank/uninitialized region after load.

- [ ] **Step 2: Add pause-menu logging scenario**

Drive the actual pause controls, capture before/after screenshots, and assert the selected setting is reflected in bounded log/config evidence.

- [ ] **Step 3: Add persistence scenario**

Use manifest coordinates/actions to modify and save an object, close the launched editor, reopen it, and assert the post-reopen evidence. Mark this scenario mutating and require the explicit mutation flag.

- [ ] **Step 4: Add model/import texture scenario**

Use manifest-provided picker coordinates, model text, and expected model/texture log patterns. Assert no new texture-not-found line is emitted during the import/load window. Mark it mutating and require the explicit mutation flag.

### Task 4: Verify the installed corpus and commit

**Files:**
- Generated only: `artifacts/e2e/<run-id>/`

**Interfaces:**
- Consumes: built `D:\\IGI1\\igi1ed.exe`, installed levels 1–14, and the scenario manifest.
- Produces: screenshots/reports plus an explicit regression summary distinguishing pass, skip, and pre-existing failures.

- [ ] **Step 1: Build editor and tests**

```powershell
cmake --build build --config Release --target igi_tests igi-editor -j 1
```

- [ ] **Step 2: Deploy binaries and fixtures to `D:\\IGI1` and verify hashes.`

- [ ] **Step 3: Run focused and full non-GUI regression tests from `D:\\IGI1` with `IGI_GAME_PATH`, `IGI_WEATHER_CORPUS`, and `IGI_VANILLA_ROOT` set to `D:\\IGI1`. Record known unrelated failures without hiding them.

- [ ] **Step 4: Run the live E2E manifest from `D:\\IGI1`, inspect every screenshot/report, and verify editor cleanup unless `-KeepEditorOpen` is requested.

- [ ] **Step 5: Review and commit only the harness/docs changes.**

```powershell
git diff --check
git status --short
git add -f tools/e2e docs/superpowers/specs/2026-09-03-editor-e2e-testing-design.md docs/superpowers/plans/2026-09-03-editor-e2e-testing.md docs/TESTS.md
git commit -m "test: add live editor end to end harness"
```
