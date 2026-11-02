# Level 12 WinchHouse rendering repair plan

Date: 2026-09-07  
Status: Planning only; the reported editor defect remains unresolved.  
Issue: IGIED-LEVEL12-WINCHHOUSE-NORMAL-VIEW-20260907

## Objective

Make FortressWinchHouse render correctly in the ordinary Level 12 editor viewport, including its walls, floors, roof, windows and attachments at valid interior and exterior viewpoints. Preserve authored placement and intentional interior camera views. An attractive isolated model capture is insufficient to accept the repair.

This document authorizes no implementation by itself. The current request is to write the plan only.

## Evidence and limits

- Target: model `463_01_1`, building `FortressWinchHouse`, capture identity `-1#907`. Confirm the identity against the current loaded inventory because an ordinal suffix can change.
- Loader logs from the ordinary editor run show the building at `(112284896, -42113812, 179532464)`, 12 root submeshes with textures assigned, and multiple loaded attachments. Loading does not prove that every part is drawn correctly.
- The authored HumanPlayer start is `(112255288, -42206448, 179544736)`. Starting inside a building or cable car can be intentional. Proximity to a bounding sphere does not establish a camera bug.
- `artifacts/e2e/level12-visual-repro-20260907-1735-elevated/` records a responsive Session 1 editor and a failed building-selection color check. Its screenshot shows terrain selected and incomplete-looking surrounding surfaces. Therefore that scenario did not reliably select the intended building and cannot establish the cause.
- `artifacts/e2e/level12-startup-current-20260907-1940/` passed the broad startup image metric while showing a cable-car interior. Image variety and brightness are weak acceptance criteria for this defect.
- `artifacts/level12-winchhouse-centered-fixed.png` shows the exterior from a different camera. It does not prove normal interior or distance-dependent rendering is correct.
- The file named `artifacts/level12-retail-window.png` visibly contains editor UI. Do not use its name as evidence of a retail reference capture.
- The disposable test executable differed from the latest workspace executable during the investigation. Record hashes for every future run.
- A fallback x64 build linked but failed to expose a window in the installed environment. The installed editor is x86; an x86 rebuild was started and interrupted. Its final outcome needs checking before reuse.

## Existing unverified changes requiring review

The preceding investigation added a fresh-load camera relocation block in `source/app_level.cpp`, `CameraInsideModelBounds` in `source/runtime/editor_camera_start.h`, and a corresponding test. These edits have not passed live acceptance.

Do not accept automatic relocation as the rendering repair. The predicate uses a sphere, can select an unrelated overlapping building, ignores rotation when computing its center in the fresh-load block, and can move an intentional interior start. Review and remove or revise only these investigation edits during implementation if the evidence does not support them. Preserve unrelated working-tree changes.

The existing focused navigation helper also caps its offset; verify its outside-bounds claim for very large models before relying on it. Keep explicit navigation behavior separate from authored startup behavior.

## Phase 1 — Establish a reproducible baseline

1. Record branch, commit, working diff, executable SHA-256, PE architecture, dependent DLL architecture and current game-data hashes. Check whether the interrupted build is still running before starting another build.
2. Use one x86 build directory and one retained process session. Await its exit code; never launch competing builds into the same output directory. Reuse the existing local GoogleTest source when configuring a new test tree.
3. Review relevant documentation: `game_data_types.md`, `game_model_naming.md`, and the MEF, DAT/MTP, RES, QSC and FNT sections of `game_file_formats.md`. `game_structure.md` describes IGI2 paths; verify actual IGI1 paths under `D:\IGI1\missions\location0`.
4. Launch the editor through WMI `Win32_Process.Create`, with working directory `D:\IGI1`. Verify the actual process path, SessionId 1, responsiveness and memory above 30 MB.
5. Capture the ordinary startup view, a deterministically selected WinchHouse exterior, and a valid interior view. Select by resolved task identity or exact name rather than fixed screen coordinates. Record camera position, orientation, selected task, viewport size and draw settings with every screenshot.
6. Repeat the same views once to establish reproducibility. Freeze camera movement and account for weather/animation when comparing frames.

Exit condition: a repeatable command captures the user's missing surfaces in the normal rendering path, with correct target identity and binary provenance.

## Phase 2 — Test ranked explanations

Change one variable per experiment, keeping the camera and target fixed.

| Hypothesis | Discriminating experiment | Evidence needed |
|---|---|---|
| Attachment LOD/portal culling removes structural geometry | Compare ordinary rendering with only attachment distance gating temporarily bypassed | Missing parts return at the same camera; record part IDs, distance, threshold and decision |
| Opaque and alpha submeshes use incorrect depth/pass state | Inspect missing structural submeshes in opaque and transparent passes | Draw submissions, material mode, depth test/write and blend state explain missing pixels |
| Camera/clip configuration exposes a real interior failure | Compare valid interior and exterior views with unchanged geometry; inspect near/far planes | Correct eye height/orientation and clipping measurements, not merely an outside screenshot |
| Root/attachment transforms or terrain depth are wrong | Compare authored transforms and composed runtime transforms; temporarily isolate terrain occlusion | A specific transform or depth mismatch accounts for displaced/hidden surfaces |
| Resource resolution supplies incomplete/wrong geometry | Compare loaded root and recursive attachments with the level archive and material metadata | Expected versus loaded meshes, indices, textures and resolved source paths |

Prior captures temporarily bypassed LOD/portal gating. That difference makes distance gating worth testing first, but it is not yet a confirmed cause. Temporary diagnostic overrides must be restored and excluded from final acceptance.

## Phase 3 — Define the smallest supported repair

1. Identify the exact decision or state transition responsible for the reproduced failure before editing behavior.
2. Fix the shared rule at that point. Candidate areas are `renderer_objects.cpp`, `renderer_objects_atta.cpp`, resource/metadata loading, or camera setup only if its own defect is demonstrated.
3. If culling is responsible, correct the distance units, threshold interpretation or attachment policy using authored metadata. Do not globally disable LOD or hardcode the WinchHouse model ID.
4. If material routing is responsible, preserve opaque depth writes and proper alpha behavior per submesh, including recursive attachments and subsequent draws.
5. If transforms are responsible, use the same scale, rotation order and parent composition as the normal renderer. Preserve authored level data.
6. Keep interior views usable. Moving the camera outside the house is not an acceptable substitute for restoring missing interior surfaces.

Exit condition: one measured cause, one scoped repair, and a regression test that exercises the failing decision at its actual call site.

## Phase 4 — Verification

- Demonstrate the regression fails before the repair and passes afterward. Avoid tests that merely restate a distance formula.
- Build the x86 editor and focused tests successfully, recording exit codes and exact executable hashes. Resolve build-environment failures separately from renderer defects.
- Repeat the original normal-scene camera poses with production settings restored. Capture exterior front/back/sides, valid interiors, near/far distances and relevant LOD transition points.
- Check structural coverage, occlusion and alpha behavior using part/depth evidence where available. Pair automated evidence with direct inspection of the ordinary viewport; do not accept loader success, screenshot brightness or isolated capture PASS alone.
- Include one other building with mixed opaque/glass attachments, one rotated building, and an intentional interior start to detect collateral changes.
- Verify selection, F11 and explicit model navigation independently. Ensure the test actually selects the requested building.
- Check game-data hashes remain unchanged and the editor exits cleanly. Never write QSC/QVM data as a side effect of visual verification.

## Acceptance and handoff

All of the following must be true before marking the issue fixed:

1. The user's normal editor failure is reproduced and then absent at the same valid viewpoints.
2. Expected walls, roof, floor and attachments render; windows retain appropriate transparency and depth ordering.
3. No model-specific workaround or automatic escape from intentional interiors conceals the defect.
4. The actual x86 executable used in live verification matches the tested build.
5. Focused regression and representative building checks pass, with original rendering settings restored.
6. A report links before/after screenshots, camera metadata, hashes, tests and the established root cause. Update earlier bug records to distinguish prior capture fixes from this normal-view repair.
7. Save the confirmed bug ID and resolution to mem0 as requested if that connector is available; otherwise report that the memory write remains unavailable. Do not record an unverified hypothesis as a fixed bug.

Suggested final evidence location: `artifacts/e2e/level12-winchhouse-normal-view-<timestamp>/`. Implementation and deployment should follow only after this planning request.
