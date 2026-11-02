# Deterministic Visual Integrity Gate Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make native E2E reject an object whose expected rendered parts are absent, unstable, or incomplete, while accepting the Level 12 Watchtower fixture.

**Architecture:** The renderer writes target-scoped object, submesh/material, and depth evidence for every native capture view. A pure C++ analyzer compares those buffers with the model's expected submeshes. The capture runner, audit, and dashboard present visual acceptance separately from loader acceptance.

**Tech Stack:** C++17, OpenGL, GoogleTest, PowerShell, JSON, native framebuffer images/video.

**Spec:** `docs/superpowers/specs/2026-09-06-visual-integrity-gate-design.md`

## Global Constraints

- Use deterministic geometry, camera, material, depth, and ID evidence only; no AI classifier or model-specific rules.
- Keep loader/transform/texture evidence distinct from visual-integrity acceptance.
- Ignore terrain, sky, and unrelated objects when deciding target quality.
- Watchtower `405_02_1` task `570` must pass; WinchHouse `463_01_1` task `-1#907` must fail.
- Retain the existing WMI Session 1 native-launch policy.

---

### Task 1: Pure analyzer

**Files:**
- Create: `source/visual_integrity.h`
- Create: `source/visual_integrity.cpp`
- Create: `tests/test_visual_integrity.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `igi::VisualIntegrityResult EvaluateVisualIntegrity(const VisualIntegrityInput&)`.
- Input: expected part IDs plus per-view target/material masks, depth data, and sampled frame area.
- Result: `PASS`, `FAIL`, or `INCONCLUSIVE` and structured rule findings.

- [ ] **Step 1: Write a failing expected-part test**

```cpp
TEST(VisualIntegrityTest, FailsWhenExpectedPartHasNoFragments) {
  const auto result = igi::EvaluateVisualIntegrity(MakeInput({4}, {{}}));
  EXPECT_EQ(result.status, igi::VisualIntegrityStatus::kFail);
}
```

- [ ] **Step 2: Run it and observe the missing analyzer failure**

Run: `bin\\Debug\\igi_tests.exe --gtest_filter=VisualIntegrityTest.*`

Expected: compile/test failure because the analyzer does not exist.

- [ ] **Step 3: Implement the minimal pure data model and rules**

```cpp
enum class VisualIntegrityStatus { kPass, kFail, kInconclusive };
struct VisualIntegrityFinding { std::string rule; std::string part; int viewIndex; };
VisualIntegrityResult EvaluateVisualIntegrity(const VisualIntegrityInput& input);
```

Implement part presence, minimum projected area, interior target-mask holes, depth agreement, and frame-area stability without OpenGL dependencies.

- [ ] **Step 4: Re-run the focused tests**

Run: `bin\\Debug\\igi_tests.exe --gtest_filter=VisualIntegrityTest.*`

Expected: PASS.

### Task 2: Diagnostic renderer evidence

**Files:**
- Modify: `source/renderer/renderer_objects.h`
- Modify: `source/renderer/renderer_objects_picking.cpp`
- Modify: `source/renderer/renderer.h`
- Modify: `source/renderer/renderer_objects.cpp`
- Modify: `source/debug_command_manager.cpp`

**Interfaces:**
- Produces: `Renderer::CaptureObjectVisualEvidence(...)`, containing target-object pixels, one stable material/submesh ID per target submesh, and matching depth values.

- [ ] **Step 1: Write a failing test that other-object pixels cannot satisfy a missing target part**

```cpp
TEST(VisualIntegrityTest, IgnoresOtherObjectPixelsWhenTargetPartIsMissing) {
  EXPECT_TRUE(HasFinding(igi::EvaluateVisualIntegrity(MakeTargetScopedInput()), "part-coverage"));
}
```

- [ ] **Step 2: Run it and observe failure before target-scoped material evidence is present**

Run: `bin\\Debug\\igi_tests.exe --gtest_filter=VisualIntegrityTest.IgnoresOtherObjectPixelsWhenTargetPartIsMissing`

Expected: FAIL.

- [ ] **Step 3: Implement the off-screen target submesh-ID/depth pass**

Reuse the picking FBO lifecycle. Draw only the selected target's submeshes with stable one-based IDs, read material IDs and depth, and restore OpenGL state before returning. Populate expected IDs from `Mesh::subMeshes` rather than model IDs.

- [ ] **Step 4: Build the Release editor and re-run focused tests**

Run: `cmake --build build --config Release --target igi-editor --parallel 2`

Expected: successful build with no compiler errors.

### Task 3: Evidence bundle and analyzer output

**Files:**
- Modify: `source/debug_command_manager.cpp`
- Modify: `source/debug_command_manager.h`
- Modify: `source/visual_integrity.cpp`
- Modify: `tests/test_visual_integrity.cpp`

**Interfaces:**
- Produces: `screenshots/visual-integrity.json`, target/material masks, and failure overlays.

- [ ] **Step 1: Write a failing serialization test**

```cpp
TEST(VisualIntegrityTest, SerializesActionablePartCoverageFinding) {
  EXPECT_NE(igi::VisualIntegrityJson(MakeFailedResult()).find("part-coverage"), std::string::npos);
}
```

- [ ] **Step 2: Run it and observe missing JSON writer failure**

Run: `bin\\Debug\\igi_tests.exe --gtest_filter=VisualIntegrityTest.SerializesActionablePartCoverageFinding`

Expected: FAIL.

- [ ] **Step 3: Write per-view evidence and final object result**

Write each target/material mask beside the still, evaluate all views after capture, emit `visual-integrity.json`, and render overlays with source image, target mask, material IDs, rule, observed pixels, and threshold.

- [ ] **Step 4: Run tests and a one-object native smoke capture**

Run: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools\\e2e\\Invoke-SmartNativeCaptureSession.ps1 -ArtifactsRoot artifacts\\e2e\\visual-integrity-smoke -Level 12 -Category Buildings -ModelIds 405_02_1 -MaxObjects 1 -ViewCount 10 -NoDashboard`

Expected: evidence bundle includes `visual-integrity.json` and overlays.

### Task 4: Runner, audit, dashboard, and temporal policy

**Files:**
- Modify: `tools/e2e/Invoke-SmartNativeCaptureSession.ps1`
- Modify: `tools/e2e/Test-SmartCaptureArtifact.ps1`
- Modify: `tools/e2e/Generate-SmartDashboard.ps1`
- Modify: `tools/e2e/generate_dashboard.py` if it owns report markup
- Modify: `tools/e2e/test-smart-native-capture-session.ps1`

**Interfaces:**
- Consumes: per-object `visual-integrity.json` from Task 3.
- Produces: batch `visualIntegrity` entries, visual acceptance policy, report links, and sampled video-frame status.

- [ ] **Step 1: Write a failing audit fixture for loader PASS plus visual FAIL**

```powershell
$batch = @{ status='PASS'; objects=@(@{ visualIntegrity=@{ status='FAIL' } }) } | ConvertTo-Json -Depth 8
Set-Content $fixturePath $batch
& $artifactTest -ArtifactsRoot $fixtureRoot
$LASTEXITCODE | Should -Be 1
```

- [ ] **Step 2: Run it and observe current false pass**

Run: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools\\e2e\\test-smart-native-capture-session.ps1`

Expected: the new assertion fails before policy integration.

- [ ] **Step 3: Enforce visual acceptance and report diagnostics**

Copy visual evidence to the artifact directory, reject required visual FAIL results despite loader PASS, preserve INCONCLUSIVE explicitly, sample orbit video frames, and add a Visual Integrity report tab with score, findings, and overlay links.

- [ ] **Step 4: Re-run runner tests and regenerate a report**

Run: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools\\e2e\\test-smart-native-capture-session.ps1`

Expected: PASS, with report visual-integrity fields present.

### Task 5: Level 12 fixtures and release evidence

**Files:**
- Modify: `tests/test_visual_integrity.cpp`
- Modify: `tools/e2e/SMART-LIVE-STATUS.md`

**Interfaces:**
- Consumes: completed evidence bundles for the two Level 12 fixture captures.
- Produces: automated fixture assertions and recorded native validation paths/hashes.

- [ ] **Step 1: Write failing fixture tests**

```cpp
TEST(VisualIntegrityFixtureTest, WatchtowerEvidencePasses);
TEST(VisualIntegrityFixtureTest, WinchHouseEvidenceFails);
```

- [ ] **Step 2: Capture Watchtower and WinchHouse with ten views and video**

Run the native E2E runner for `405_02_1` then `463_01_1`. Keep generated media uncommitted and record exact artifact paths and binary hashes in the status document.

- [ ] **Step 3: Verify required classifications**

Run: `bin\\Debug\\igi_tests.exe --gtest_filter=VisualIntegrityTest.*:VisualIntegrityFixtureTest.*`

Expected: Watchtower PASS, WinchHouse FAIL with named actionable findings.

- [ ] **Step 4: Commit each completed task and the final fixture evidence status**

Commit only source, tests, scripts, docs, and status files; never commit generated images, videos, or live artifacts.

## Plan self-review

- Spec coverage: Tasks cover deterministic checks, target-scoped ID/depth evidence, material/submesh inventory, screenshots, video sampling, overlays, runner policy, report integration, and the required accepted/rejected fixtures.
- Placeholder scan: no deferred implementation steps or ambiguous interfaces remain.
- Type consistency: `VisualIntegrityInput`, `VisualIntegrityResult`, and `CaptureObjectVisualEvidence` are defined before later tasks consume them.
