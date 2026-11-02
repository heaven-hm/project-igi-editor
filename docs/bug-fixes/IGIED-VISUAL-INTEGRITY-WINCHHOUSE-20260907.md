# IGIED-VISUAL-INTEGRITY-WINCHHOUSE-20260907

## Status

Fixed in commit `2ab165f`; manual Level 12 inspection remains available from
the editor launch documented below.

## Issue

The Level 12 authored building capture for WinchHouse (`463_01_1`, task
`-1#907`) could report loader success while omitting authored attachment
geometry from the rendered evidence. The deterministic visual gate correctly
reported missing projected attachment coverage, but the native editor view
was incomplete.

## Root causes

1. Model inspection reused the gameplay LOD/portal-distance gate. Some authored
   buildings have a very small portal distance, while the inspection orbit must
   frame the complete model. The capture now disables that gate only for the
   duration of the authored model capture and restores the user setting on exit.
2. A window attachment can contain both opaque structural blocks and genuinely
   alpha-blended panes. Routing the whole model to the transparent pass left
   opaque blocks without the normal depth behavior. Attachment rendering now
   routes each submesh by its alpha mode and applies the window blend settings
   only to alpha submeshes.
3. The visual-evidence picking pass did not use the same depth ordering as the
   normal attachment draw. It now uses polygon offset for the root mesh and
   recursive attachments, then restores the GL state before returning.

These are generic renderer/capture rules; there is no model-specific
`463_01_1` exception or image-classifier pass rule.

## Changed code

- `source/debug_command_manager.cpp` — scoped LOD override for authored model
  capture.
- `source/renderer/renderer_objects_atta.cpp` — per-submesh opaque/alpha
  attachment pass routing and state handling.
- `source/renderer/renderer_objects_picking.cpp` — depth-aligned visual
  evidence capture.

## Verification

- Release editor build: passed with CMake/MSBuild.
- Focused native visual tests: 6/6 passed.
- Native-session PowerShell contract test: passed.
- Dashboard Python syntax check: passed.
- Live WMI Session 1 capture: one launch/close, 20/20 screenshots, two
  evidence passes, zero evidence failures, `Required` visual policy.
- Watchtower (`405_02_1`, task `570`): visual `PASS`.
- WinchHouse (`463_01_1`, task `-1#907`): visual `PASS` with no findings.
- Live evidence: `artifacts/visual-integrity-level12-depthfix-20260907-155140/`

The full local test binary registered 675 tests: 601 passed, 33 skipped, and
41 failed because this checkout lacks the mission corpus and related fixture
data under `bin\\Release\\missions`. Those failures are an environment gate,
not a clean full-suite release result.

## Manual Level 12 check

The retail game must be launched from `D:\IGI1` in interactive Session 1:

```powershell
$wmi = [wmiclass]"\\.\root\cimv2:Win32_Process"
$result = $wmi.Create("D:\IGI1\igi.exe window level12", "D:\IGI1")
Write-Host "PID: $($result.ProcessId)"
```

Confirm the `igi` process has `SessionId = 1`, `Responding = True`, and more
than 30 MB working memory before inspecting Level 12 manually. Do not modify
the installed game data during this check.
