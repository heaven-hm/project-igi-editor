# Live editor E2E runner

`editor-e2e.ps1` drives the installed editor as a user would. It launches with
WMI from `D:\IGI1`, verifies the window is responsive in interactive Session 1,
sends explicit mouse/keyboard actions, captures PNG checkpoints, and writes
JSON/Markdown reports. The runner contains only generic step execution;
scenario intent, coordinates, expected files, and log patterns live in JSON.

From PowerShell:

```powershell
Set-Location D:\IGI1
$runner = 'D:\Code\project-igi-editor\tools\e2e\editor-e2e.ps1'
$manifest = 'D:\Code\project-igi-editor\tools\e2e\scenarios\editor-regression.json'

pwsh -NoProfile -ExecutionPolicy Bypass -File $runner -ScenarioPath $manifest -ValidateOnly
pwsh -NoProfile -ExecutionPolicy Bypass -File $runner -ScenarioPath $manifest -ScenarioName 'level*'
pwsh -NoProfile -ExecutionPolicy Bypass -File $runner -ScenarioPath $manifest -AllowGameDataMutation
```

Use `-ScenarioName` for a focused run, `-KeepEditorOpen` for manual inspection,
and `-ArtifactsRoot` for a known evidence directory. Mutation scenarios refuse
to run without `-AllowGameDataMutation`. Assertions against editor logs are
bounded to the current run's byte offset and poll until `timeoutSeconds`; file
assertions use the same bounded polling behavior. A failure captures
`failure.png` before cleanup and records the failed step in `scenario.json`.
The generic action vocabulary also includes `key_hold`; screenshot assertions
include region metrics, tolerant color ratios, and frame-to-frame difference
ratios. These assertions are data-driven and can be reused by additional
level/model fixtures without adding production-specific logic to the runner.

## Deterministic native visual-integrity gate

The generic UI runner above and the smart native runner are separate E2E
surfaces. `Invoke-SmartNativeCaptureSession.ps1`, called by
`Run-SmartTest.ps1` and `e2e_live_test.cmd`, launches the editor through WMI in
interactive Session 1 and records loader evidence separately from visual
acceptance. Its default `-VisualIntegrityPolicy Required` is fail-closed: each
selected object must have visual-integrity `PASS`; loader transforms, DAT
texture loads, or assignment records cannot mask a visual failure.

The visual check is deterministic and target-scoped. It uses object/material ID
masks, depth evidence, projected submesh coverage, and strict coverage for
independently rendered MEF attachments. It has no image classifier and no
model-specific pass rule. Use the direct script when selecting an authored task
ID, because the convenience `.cmd` wrapper does not expose `-TaskIds` or
`-VisualIntegrityPolicy`:

```powershell
$native = 'D:\Code\project-igi-editor\tools\e2e\Invoke-SmartNativeCaptureSession.ps1'
$out = 'D:\Code\project-igi-editor\artifacts\visual-integrity-level12-' + (Get-Date -Format yyyyMMdd-HHmmss)
& pwsh -NoProfile -ExecutionPolicy Bypass -File $native `
  -GameRoot D:\IGI1 -EditorExePath D:\Code\project-igi-editor\bin\Release\igi1ed.exe `
  -Level 12 -Category Buildings -ModelIds '405_02_1,463_01_1' `
  -TaskIds '570,-1#907' -MaxObjects 0 -ViewCount 10 -Video `
  -VisualIntegrityPolicy Required -ArtifactsRoot $out
```

The explicit acceptance fixtures have opposite classifications: Watchtower
(`405_02_1`, task `570`) must pass, while WinchHouse (`463_01_1`, task
`-1#907`) must fail with actionable under-covered geometry findings. A
verified run is recorded at
`artifacts/visual-integrity-level12-strict-live-20260907/`.

The explicit artifact root contains `batch.json`. Per-object directories under
`screenshots/obj-<index>-task<id>-<model>/` contain the native stills,
`evidence.jsonl`, `visual-integrity.json`, object/material mask PNGs, depth
binary data, diagnostic overlays, and a hashed portable `manifest.json`.
Validate a completed root independently with
`Test-SmartCaptureArtifact.ps1 -ArtifactsRoot <root>`; it rejects missing or
unsafe evidence paths, hash mismatches, and required visual FAIL/INCONCLUSIVE
results. The batch records the inventory and editor hashes, source hash,
policy, task IDs, loader evidence, and requested versus captured view counts.
Generated screenshots, videos, and live artifacts remain local evidence and
are not source files to commit.

The focused manifest exercises Level 9 weather, Level 12 startup glass, pause
logging/severity, property-panel interaction, save/reopen persistence, and
model import with model-specific texture application evidence. The model
scenario also captures the property field before and after typing so a failed
import can be diagnosed from UI evidence rather than a passing unit test.

Generate the corpus matrix from the installed data and validate it without
launching a GUI:

```powershell
Set-Location D:\IGI1
$matrix = 'D:\Code\project-igi-editor\artifacts\e2e\corpus-matrix-manifest.json'
& 'D:\Code\project-igi-editor\tools\e2e\New-EditorCorpusManifest.ps1' -GameRoot D:\IGI1 -OutputPath $matrix
& 'D:\Code\project-igi-editor\tools\e2e\editor-e2e.ps1' -ValidateOnly -ScenarioPath $matrix
& 'D:\Code\project-igi-editor\tools\e2e\test-editor-corpus.ps1'
```

The generated matrix has two scenarios per installed Level 1–14. The smoke
scenario checks load completion, required level archives, authored weather
resolution, texture-miss diagnostics, viewport/pause rendering, native cursor
visibility while paused, custom-cursor resumption after pause, and graceful
process exit. The terrain scenario presses `T` and requires the terrain-edit
palette in a screenshot. Run it serially with an explicit artifacts directory:

```powershell
& 'D:\Code\project-igi-editor\tools\e2e\editor-e2e.ps1' `
  -GameRoot D:\IGI1 -ScenarioPath $matrix `
  -ArtifactsRoot 'D:\Code\project-igi-editor\artifacts\e2e\corpus-matrix-run'
```

The generated corpus scenarios are non-mutating. Keep the focused mutating
manifest behind `-AllowGameDataMutation`; do not run the two manifests in
parallel because both launch against the same installed editor state.

Run the serial regression pipeline after building/deploying the editor. It
validates both manifests, runs the existing focused workflows, then runs the
visual regressions that use real screenshots for terrain mode, indoor weather,
and distant building floors:

```powershell
& 'D:\Code\project-igi-editor\tools\e2e\test-editor-regression.ps1' `
  -GameRoot D:\IGI1 -AllowGameDataMutation
```

The generic pipeline may be red while a known UI/scene defect is present. A red
result is useful evidence only when its scenario directory contains the failed
step's screenshot and `scenario.json`. This pipeline is complementary to the
strict native visual-integrity result; do not turn either assertion surface
into permissive scene-wide pixel checks.
