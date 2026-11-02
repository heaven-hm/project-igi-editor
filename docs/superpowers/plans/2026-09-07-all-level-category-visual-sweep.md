# All-Level Category Visual Sweep Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Run strict native visual verification serially for every eligible RigidObjects, AI, and Buildings instance in Levels 1–14, retain compact tamper-evident HTML/JSON evidence for passing category runs, and remove only their bulky capture payloads before the next category begins.

**Architecture:** A new orchestration script invokes the existing `Invoke-SmartNativeCaptureSession.ps1` once for each `(level, category)` pair with the required visual-integrity policy and all eligible candidates. It validates the completed `batch.json`, copies a compact immutable summary into a report-only root, writes an HTML index from that summary, then deletes the run directory only after every required acceptance condition is true. A failed or incomplete run retains its raw artifact directory and stops the sweep.

**Tech Stack:** Windows PowerShell 5.1+, WMI `Win32_Process.Create` through the existing native runner, JSON, SHA-256, HTML5, existing x86 Release editor.

**Spec:** `docs/LIVE_E2E_TESTING.md`; `tools/e2e/README.md`; user request on 2026-09-07.

## Global Constraints

- Launch visible editor sessions only through the existing WMI-based native runner from `D:\IGI1`; run one session at a time.
- Use `VisualIntegrityPolicy Required`, `MaxObjects 0`, and `ViewCount 10`; `ReportOnly`, sampled runs, and failures never qualify for cleanup.
- Category mapping is explicit: Objects = `RigidObjects`, AI = `AI`, Buildings = `Buildings` (the native runner's `Building` task category).
- Process every level in numeric order and categories in Objects → AI → Buildings order.
- Keep full raw evidence on any `FAIL`, `NOT_RUN`, invalid JSON, hash mismatch, nonzero exit, or editor process left running; stop immediately.
- For a pass, preserve only compact report evidence: source/editor hashes, runner exit code, counts, per-object task/type/model/status, visual-integrity statuses, and SHA-256 of the original `batch.json`; do not embed screenshots in HTML.
- Never hardcode level-, model-, task-, or test-specific exceptions.

---

### Task 1: Add a fail-closed category-sweep orchestrator

**Files:**
- Create: `tools/e2e/Invoke-AllLevelCategoryVisualSweep.ps1`
- Test: `tools/e2e/test-all-level-category-visual-sweep.ps1`

**Interfaces:**
- Consumes: `Invoke-SmartNativeCaptureSession.ps1 -ArtifactsRoot -GameRoot -EditorExePath -Level -Category -MaxObjects -ViewCount -VisualIntegrityPolicy`.
- Produces: `<ReportsRoot>/summary.json`, `<ReportsRoot>/report.html`, and `<ReportsRoot>/levelNN-<category>.json`.

- [ ] **Step 1: Write a contract test for the sweep policy.**

```powershell
$script = Get-Content -LiteralPath "$PSScriptRoot/Invoke-AllLevelCategoryVisualSweep.ps1" -Raw
foreach ($needle in @("'RigidObjects','AI','Buildings'", 'MaxObjects', 'VisualIntegrityPolicy', 'Required', 'Remove-Item')) {
    if ($script -notmatch [regex]::Escape($needle)) { throw "Missing required policy: $needle" }
}
if ($script -match 'Remove-Item.*before.*status') { throw 'Cleanup must be gated by verified PASS.' }
```

- [ ] **Step 2: Run the contract test before the implementation exists.**

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File tools/e2e/test-all-level-category-visual-sweep.ps1`

Expected: FAIL because the orchestrator does not exist.

- [ ] **Step 3: Implement parameter validation and deterministic scheduling.**

```powershell
param(
  [string]$GameRoot = 'D:\IGI1',
  [string]$EditorExePath = 'bin\Release\igi1ed.exe',
  [string]$ReportsRoot = 'artifacts\e2e\category-sweep-reports',
  [ValidateRange(1,10)][int]$ViewCount = 10
)
$categories = @('RigidObjects','AI','Buildings')
$levels = 1..14
foreach ($level in $levels) { foreach ($category in $categories) { <# run one category #> } }
```

Resolve all roots to absolute paths, refuse an existing active `igi1ed` process, and make every category run root unique under a transient root. Pass `-MaxObjects 0`, `-ViewCount $ViewCount`, and `-VisualIntegrityPolicy Required` to the native runner.

- [ ] **Step 4: Implement acceptance and compact evidence extraction.**

```powershell
$batch = Get-Content -LiteralPath $batchPath -Raw | ConvertFrom-Json
$passing = $exitCode -eq 0 -and $batch.status -eq 'PASS' -and
  $batch.visualIntegrityPolicy -eq 'Required' -and
  $batch.evidenceFailed -eq 0 -and $batch.screenshotsCaptured -eq $batch.screenshotsExpected
if (-not $passing) { throw "Retaining failed evidence: $runRoot" }
```

Write a per-category JSON record containing only the acceptance fields in the global constraints, including `Get-FileHash $batchPath -Algorithm SHA256`; write the aggregate JSON and an escaped static HTML table. The report has one row per `(level, category)` and links only to compact JSON, not deleted images.

- [ ] **Step 5: Delete only verified passing raw artifacts and update the report.**

```powershell
Remove-Item -LiteralPath $runRoot -Recurse -Force
if (Test-Path -LiteralPath $runRoot) { throw "Raw artifact cleanup failed: $runRoot" }
```

Perform this after the compact JSON and HTML write succeeds. Never delete `ReportsRoot`, a failed run, or any directory outside the resolved transient root.

- [ ] **Step 6: Run the contract test after implementation.**

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File tools/e2e/test-all-level-category-visual-sweep.ps1`

Expected: PASS, proving exact category ordering, required policy, all-level scheduling, and cleanup gating are present.

### Task 2: Prove the workflow on Level 1, Objects

**Files:**
- Create: `artifacts/e2e/category-sweep-reports/level01-rigidobjects.json` (runtime evidence only)
- Create: `artifacts/e2e/category-sweep-reports/report.html` (runtime evidence only)

**Interfaces:**
- Consumes: Task 1 orchestrator with `-Levels 1 -Categories RigidObjects` test seam.
- Produces: a compact pass record or retains the failed raw capture root.

- [ ] **Step 1: Run the non-mutating preflight.**

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File tools/e2e/Invoke-AllLevelCategoryVisualSweep.ps1 -Levels 1 -Categories RigidObjects -PrepareOnly`

Expected: a generated candidate count and no WMI launch.

- [ ] **Step 2: Run Level 1 Objects live.**

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File tools/e2e/Invoke-AllLevelCategoryVisualSweep.ps1 -Levels 1 -Categories RigidObjects`

Expected: one WMI Session 1 lifecycle, all eligible Object candidates with ten views each, `batch.status=PASS`, `evidenceFailed=0`, and raw artifacts removed only after summary persistence.

- [ ] **Step 3: Independently verify compact evidence.**

Run: `Get-Content artifacts/e2e/category-sweep-reports/level01-rigidobjects.json -Raw | ConvertFrom-Json`

Expected: a `PASS` record with source/editor/batch SHA-256 values and no screenshot paths.

### Task 3: Execute all level/category runs serially

**Files:**
- Create: `artifacts/e2e/category-sweep-reports/levelNN-<category>.json` for each passing pair.
- Modify: `artifacts/e2e/category-sweep-reports/summary.json`, `artifacts/e2e/category-sweep-reports/report.html` after every passing pair.

**Interfaces:**
- Consumes: Task 1 orchestrator and an existing stable x86 Release editor.
- Produces: exactly 42 compact category rows if every category is available and passes; unavailable categories are explicit `SKIPPED` rows with candidate count zero, never implicit passes.

- [ ] **Step 1: Run the serial campaign.**

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File tools/e2e/Invoke-AllLevelCategoryVisualSweep.ps1`

Expected order: `L01/RigidObjects`, `L01/AI`, `L01/Buildings`, then the same order through Level 14. Each run uses a fresh transient root and does not overlap a game/editor process.

- [ ] **Step 2: Stop on the first non-pass.**

Expected: raw evidence remains available for the failed pair; later pairs do not run; aggregate HTML marks the exact failure rather than claiming campaign completion.

- [ ] **Step 3: Verify final campaign integrity only if all scheduled pairs pass or are explicit zero-candidate skips.**

Run: `Get-Content artifacts/e2e/category-sweep-reports/summary.json -Raw | ConvertFrom-Json`

Expected: every row has an explicit terminal state, every `PASS` has a matching compact JSON record and batch hash, and no transient raw directory exists for passed rows.

### Task 4: Record the outcome

**Files:**
- Modify: `docs/bug-fixes/IGIED-WEATHER-INDOOR-SHELTER-20260907.md` only if this campaign exposes a weather regression directly related to the current fix.

**Interfaces:**
- Consumes: Task 3 compact report and retained failure artifacts.
- Produces: factual scope/verification status; no release claim without a fully green summary.

- [ ] **Step 1: Add only evidence-backed results.**

For a green campaign, state exact PASS and SKIPPED counts and report location. For a failure, state the first failed `(level, category, taskId, modelId)` and retained artifact root.

- [ ] **Step 2: Do not alter unrelated source or installed game data.**

The native runner may use its existing temporary developer-command files and cleanup path; no level source/QSC/QVM edit is authorized by this verification campaign.

## Self-Review

- Spec coverage: Tasks 1–3 cover all 14 levels, serial category order, strict native evidence, HTML reporting, and pass-only raw cleanup. Task 4 prevents unsupported completion claims.
- Placeholder scan: no task relies on a model/task exception or unspecified acceptance rule.
- Type consistency: category strings exactly match `Invoke-SmartNativeCaptureSession.ps1`; compact report records are generated from its `batch.json` fields.

## Execution Handoff

Inline execution is appropriate because this is a long, externally visible serial validation job: implement and test Task 1, prove Level 1 Objects, then run the campaign with stop-on-first-failure semantics.
