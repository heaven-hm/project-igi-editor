# Smart live testing status

Date: 2026-09-07
Branch: `e2e-live-testing`

## Current gate

`Invoke-SmartNativeCaptureSession.ps1` is the strict native capture path
behind `Run-SmartTest.ps1` and `e2e_live_test.cmd`. It launches one editor
process with WMI `Win32_Process.Create` in interactive Session 1, verifies the
process is responsive, and captures target-scoped visual evidence. The default
`-VisualIntegrityPolicy Required` requires both loader evidence and a visual
integrity `PASS` for each selected object. `ReportOnly` is diagnostic and is
not release acceptance.

The visual analyzer is generic and deterministic. It evaluates object/material
ID masks, depth evidence, projected submesh coverage, and independently
rendered MEF attachment coverage. It does not contain model-specific rules or
an image classifier. Loader transform/texture records are retained separately
so they cannot mask missing rendered parts.

The native capture emits a fixed set of 16 views (12 exterior poses and four
interior views). `-ViewCount` controls the number of selected records copied
into each object result; the default is 10. Completion waits for all emitted
evidence files before shared capture paths are restored.

## Level 12 strict fixtures

The two authored fixtures are deliberately run together:

| Fixture | Model | Authored task ID | Expected result |
|---|---|---|---|
| Watchtower | `405_02_1` | `570` | visual `PASS` |
| WinchHouse | `463_01_1` | `-1#907` | visual `FAIL` with actionable missing or under-covered geometry |

The recorded batch below predates the current visual-integrity result contract
and its required WinchHouse negative classification. It is historical evidence
only, not current acceptance evidence. A fresh capture must accept Watchtower
and reject WinchHouse under the configured `Required` policy.

Rerun the pair with explicit task identity and a fresh artifact directory:

```powershell
$native = 'D:\Code\project-igi-editor\tools\e2e\Invoke-SmartNativeCaptureSession.ps1'
$out = 'D:\Code\project-igi-editor\artifacts\visual-integrity-level12-' + (Get-Date -Format yyyyMMdd-HHmmss)
& pwsh -NoProfile -ExecutionPolicy Bypass -File $native `
  -GameRoot D:\IGI1 -EditorExePath D:\Code\project-igi-editor\bin\Release\igi1ed.exe `
  -Level 12 -Category Buildings -ModelIds '405_02_1,463_01_1' `
  -TaskIds '570,-1#907' -MaxObjects 0 -ViewCount 10 -Video `
  -VisualIntegrityPolicy Required -ArtifactsRoot $out
```

The convenience wrapper does not expose `-TaskIds` or
`-VisualIntegrityPolicy`; use the direct script for this fixture contract.
The runner passes `task=<id>` to the editor command watcher and preserves the
same value in `batch.json`, `evidence.jsonl`, visual-integrity records, and the
per-object directory name, including anonymous IDs such as `-1#907`.

## Historical evidence

The pre-contract live batch is retained at:

`artifacts/visual-integrity-level12-depthfix-20260907-155140/batch.json`

It records one WMI launch and close, 20 selected screenshots, the inventory and
editor SHA-256 values, loader evidence, and the older visual status for both fixtures.
Because it accepts WinchHouse and lacks the current nested summary contract, it
must not be used to approve the current gate.
The one-view task-ID smoke evidence is at:

`artifacts/visual-integrity-taskid-smoke-20260907/`

Per-object evidence directories contain the selected stills,
`visual-integrity.json`, `evidence.jsonl`, object/material masks, depth data,
and diagnostic overlays. Live images, videos, and generated JSON are local
artifacts; they are not source files to commit.

## Verification boundary

- The Release Win32 editor build was verified before this status update.
- `VisualIntegrityTest.*`: 6/6 passed locally.
- The full local binary registered 675 tests from 107 suites and ran 601
  passing, 33 skipped, and 41 failing. The failed parser/runtime-data and
  `VerifyLevelIntegration` cases are environment-gated because this checkout
  lacks the installed mission corpus under `bin\Release\missions`; they are
  not a clean full-suite release result.
- `git diff --check` and dashboard Python compilation passed for the existing
  implementation work. This documentation update does not change C++ or
  PowerShell implementation files.
