# Deterministic Visual Integrity Gate

## Status

Approved concept; implementation pending review of this specification.

## Problem

The current native E2E gate proves that an object can be loaded, transformed,
assigned materials, and produce visible pixels. It does not prove that all
expected geometry is rendered. The Level 12 WinchHouse (`463_01_1`) report is a
false positive: the report passes while the screenshots show missing or
incorrectly rendered wing/glass geometry. A Level 12 Watchtower (`405_02_1`)
capture is the initial known-good comparison sample. These two captures are
explicit acceptance fixtures: the Watchtower must be accepted and the
WinchHouse must be rejected by the visual-integrity gate.

The replacement must be deterministic and evidence-based. It must not use an
AI classifier, query-specific rules, or model-specific pass/fail exceptions.

## Goals

- Detect missing, incomplete, incorrectly oriented, or unstable rendering of a
  selected object across still images and orbit video frames.
- Separate target-object quality from unrelated scene defects such as terrain,
  sky, or neighboring-object artifacts.
- Use authored scene data, MEF geometry, material data, camera position, and
  renderer diagnostic buffers as evidence.
- Preserve the existing loader/transform/texture checks and report them as a
  separate layer.
- Produce actionable per-view and per-material findings with reproducible
  thresholds and source evidence.

## Non-goals

- Reconstructing a missing mesh or repairing assets automatically.
- Declaring the entire level visually correct from one object capture.
- Comparing unrelated object shapes using raw image similarity.
- Hardcoding special handling for WinchHouse, Watchtower, glass, or any single
  user query.

## Design

### 1. Evidence bundle

The capture runner will emit a versioned evidence bundle for each object:

```text
object-evidence/
  manifest.json
  views/<view>.png
  views/<view>.bmp
  views/<view>.object-id.png
  views/<view>.depth.bin
  views/<view>.material-id.png
  views/<view>.normal.bin          (when available)
  video/orbit.mp4
  video/orbit-frames/<frame>.png   (sampled frames)
  visual-integrity.json
  overlays/<view>-diagnostic.png
```

`manifest.json` records level, task ID, model ID, authored transform, camera
transform, renderer/editor hash, source hashes, viewport, capture settings,
and the expected mesh/material inventory. Every derived result references the
manifest and source frame rather than relying on filenames alone.

### 2. Expected-geometry inventory

The analyzer will derive an expected inventory from the loaded MEF and DAT/MTP
lineage:

- submesh/part identity where the MEF exposes it;
- vertex and triangle counts;
- local bounds and transformed bounds;
- material/texture identity;
- transparency/alpha properties;
- projected bounds and conservative projected area for each camera;
- whether a part is back-face-only, double-sided, or expected to be visible
  only from a subset of orientations.

The inventory is geometry-derived and generic. It must not contain entries such
as `if modelId == 463_01_1`.

### 3. Renderer diagnostic capture

During each diagnostic render, the renderer will expose target-scoped buffers:

- object ID: confirms which pixels belong to the target object;
- material/submesh ID: confirms which expected part produced fragments;
- depth: detects holes, clipping, and contradictory occlusion;
- color: retained for human review and image overlays;
- normal: optional initially, useful for malformed winding or collapsed faces.

The diagnostic pass must use the same camera transform and object transform as
the visible capture. Scene-wide pixels are never treated as evidence for the
target unless their object/material ID identifies the target.

### 4. Deterministic checks

Each view receives independent checks:

1. **Transform agreement** — runtime object transform matches authored data.
2. **Target presence** — target ID pixels exist and are inside the projected
   target bounds.
3. **Part coverage** — each expected visible submesh/material reaches a
   conservative projected-area minimum in at least the orientations where it
   should be visible.
4. **Silhouette continuity** — target-ID pixels do not contain unexplained
   interior holes larger than the geometry/material mask permits.
5. **Depth consistency** — target depth agrees with the target-ID pass and does
   not disappear behind a contradictory self-occlusion pattern.
6. **Material coverage** — expected materials produce fragments; a loaded
   texture alone is insufficient.
7. **Transparency evidence** — transparent parts must produce valid fragments
   and depth/order evidence; invisible glass is not accepted merely because its
   texture loaded.
8. **Temporal stability** — sampled orbit frames maintain target presence,
   part coverage, and bounded centroid/area changes except where camera motion
   predicts them.

Checks return `PASS`, `FAIL`, or `INCONCLUSIVE` with numeric measurements and
the evidence path. Occlusion by another target-identified object is reported
as occluded rather than misclassified as missing geometry.

### 5. Baseline calibration

Known-good captures calibrate generic tolerances, not object-specific rules.
The Watchtower sample (`405_02_1`, task `570`) will establish the initial
accepted renderer/viewport baseline for noise, ID stability, and temporal
variance. The WinchHouse sample (`463_01_1`, task `-1#907`) is the required
negative fixture and must produce at least one actionable visual failure for
the missing/incomplete wing or glass rendering. Calibration records the rule,
measurement distribution, and threshold version. A new object is evaluated
against its own geometry-derived expectation plus the shared renderer baseline.

If calibration data is insufficient, the result is `INCONCLUSIVE`, never an
automatic pass.

### 6. Result contract

`visual-integrity.json` will contain:

```json
{
  "schemaVersion": 1,
  "status": "FAIL",
  "object": { "level": 12, "taskId": "-1#907", "modelId": "463_01_1" },
  "summary": {
    "viewsChecked": 10,
    "viewsPassed": 2,
    "viewsFailed": 8,
    "partsExpected": 12,
    "partsObserved": 9
  },
  "findings": [
    {
      "rule": "part-coverage",
      "severity": "error",
      "part": "463_06_1",
      "views": ["Ext_060", "Ext_120"],
      "observed": 0,
      "expectedMinimum": 1240,
      "reason": "projected part produced no target material fragments",
      "evidence": ["overlays/Ext_060-diagnostic.png"]
    }
  ]
}
```

The existing batch report will retain loader evidence and add a separate
`visualIntegrity` result. A batch is visually acceptable only when its
configured policy says all required checks passed; loader PASS alone will not
mask visual FAIL.

### 7. Human-review artifacts

For every failure, the analyzer creates a diagnostic overlay containing the
source image, target-ID mask, expected projected bounds, observed material IDs,
and the failing measurement. The report links directly to the frame, overlay,
camera orientation, and manifest entry. This makes a false positive or false
negative auditable without trusting a visual score alone.

## Implementation boundaries

- `source/renderer/`: diagnostic render targets and target-scoped buffer reads.
- `source/level/` or a new `source/visual_integrity/`: MEF/material expected
  inventory and pure deterministic checks.
- `tools/e2e/Invoke-SmartNativeCaptureSession.ps1`: collect and package the
  extra evidence without changing existing launch policy.
- `tools/e2e/Test-SmartCaptureArtifact.ps1`: invoke the analyzer and apply the
  configured acceptance policy.
- `tools/e2e/Generate-SmartDashboard.ps1` or report generation: display
  visual-integrity findings and overlays.
- `tests/`: pure projection/mask/threshold tests plus fixture-backed WinchHouse
  failure and Watchtower pass cases.

The analyzer should be callable independently against an evidence bundle so
that captured samples can be re-evaluated without relaunching the game.

## Verification plan

1. Unit-test projected bounds, area thresholds, mask holes, depth agreement,
   transparency evidence, and temporal stability using synthetic buffers.
2. Run the analyzer against the Watchtower baseline (`405_02_1`, task `570`) and
   require PASS for the target object.
3. Run it against the WinchHouse negative fixture (`463_01_1`, task `-1#907`)
   and require FAIL with named missing or under-covered parts.
4. Perturb camera orientation, remove a material-ID region, and occlude the
   target with another object to confirm the classifications are distinct.
5. Re-run Level 12 native capture and verify the report links, hashes, camera
   metadata, screenshots, and video frame evidence.
6. Preserve the existing loader/transform/texture E2E checks and verify that a
   visual failure cannot be hidden by a loader PASS.

## Open decisions for implementation

- Whether material/submesh IDs can be emitted directly by the current fixed-
  function/OpenGL path or require a second ID-only draw pass.
- The smallest reliable MEF part identity available across all model variants.
- Whether normal buffers are needed in the first implementation or can remain a
  follow-up diagnostic channel.
- The initial policy for `INCONCLUSIVE` views when a part is legitimately
  occluded by another authored scene object.
