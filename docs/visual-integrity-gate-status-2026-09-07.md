# Visual Integrity Gate Status

Date: 2026-09-07
Branch: `e2e-live-testing`

## What is implemented

The editor now emits deterministic, target-scoped visual evidence for native
model captures:

- a geometry-derived submesh/material inventory;
- local geometry bounds and authored texture identity for every inventory part;
- recursive MEF attachment inventory and transforms;
- object-ID, material-ID, and depth evidence buffers;
- projected-part coverage checks against the rendered scene;
- strict coverage for independently rendered attachment instances;
- diagnostic overlays and per-capture visual-integrity JSON;
- serialized projected-part measurements and an independent portable-bundle analyzer;
- task identity propagation, including anonymous IDs such as `-1#907`;
- a required visual-integrity policy in the native capture runner;
- dashboard display of visual-integrity results.

The analyzer is generic. It does not contain model-specific pass/fail rules or
query-specific fixtures. Root surfaces that are self-occluded by authored
target geometry are not treated as missing; independently projected attachment
parts remain strict requirements.

## Verified working

Release Win32 build:

```text
igi-editor.vcxproj -> bin/Release/igi1ed.exe
buildExit=0
```

Focused analyzer tests: 27/27 passed.

The mandated WMI `Win32_Process.Create` Session 1 capture was run against Level
12 with the Watchtower and WinchHouse fixtures:

| Fixture | Model | Result |
|---|---|---|
| Watchtower | `405_02_1`, task `570` | PASS |
| WinchHouse | `463_01_1`, task `-1#907` | FAIL with 20 attachment part-coverage findings |

The batch result is correctly `FAIL`; loader success does not mask visual
failure. The refreshed evidence is in
`artifacts/visual-integrity-current2-winch/batch.json` and
`artifacts/visual-integrity-current2-watch/batch.json`.

The independent `Analyze-VisualIntegrityBundle.ps1` re-evaluated the
Watchtower as `PASS` and the WinchHouse as `FAIL` (`52/116` observed parts
versus an `87`-part shared-calibration minimum).

A one-view WMI smoke capture also confirmed that generated evidence preserves
both task IDs:

`artifacts/visual-integrity-taskid-smoke-20260907/`

## Validation boundary

`git diff --check` and dashboard Python compilation pass. The full native test
binary contains 675 tests, but its all-level integration portion requires the
installed mission corpus under `bin/Release/missions`; that corpus is absent in
this worktree. The run therefore cannot be reported as a clean full-suite
release gate. Tests that require that external corpus remain an environment
setup gate, not evidence that the visual-integrity implementation passed.

The generated live captures and pre-existing scratch files are intentionally
not part of the source commit.
