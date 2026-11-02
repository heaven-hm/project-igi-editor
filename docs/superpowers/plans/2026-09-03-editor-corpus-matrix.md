# Editor Corpus E2E Matrix Implementation Plan

> **For agentic workers:** Use the existing editor E2E runner and execute each task with a test-first checkpoint. Steps use checkbox syntax.

**Goal:** Prevent a Level 9/12-only false sense of regression safety by generating and validating a live E2E scenario for every installed Level 1–14.

**Architecture:** Keep scenario intent in JSON and make the generator discover corpus facts from `D:\\IGI1`. Extend the generic runner only with reusable assertions for path presence, cursor state, and graceful process exit. Keep mutating deep workflows separate from the non-mutating corpus matrix.

**Tech Stack:** PowerShell 5.1, WMI `Win32_Process.Create`, Win32 input/window APIs, GDI screenshots, JSON manifests, existing GoogleTest/CMake suite.

**Spec:** `docs/superpowers/specs/2026-09-03-editor-corpus-matrix-design.md`

## Global constraints

- All editor/test runs use `D:\\IGI1` as corpus and working directory.
- Visible editor launch uses WMI; no hidden process launcher is valid evidence.
- The generator must discover levels and resource paths; no Level 9/12 production special cases.
- Mutating scenarios require explicit `-AllowGameDataMutation` and serial execution.
- Existing untracked artifacts must not be staged.

### Task 1: Corpus contract

**Files:** `tools/e2e/test-editor-corpus.ps1`

- [x] Assert the generator emits the smoke and terrain-shortcut scenarios for levels 1–14.
- [x] Assert every generated scenario contains load, health, screenshot, pause/cursor, log, and close steps.
- [x] Assert the generated manifest is accepted by the runner in validation-only mode.

### Task 2: Generic runner assertions

**Files:** `tools/e2e/editor-e2e.ps1`

- [x] Add `assert_path` for root-contained files/directories and minimum byte sizes.
- [x] Add explicit native-cursor visible/hidden assertions using `GetCursorInfo`.
- [x] Capture the log offset before WMI launch so fast startup lines cannot be missed.
- [x] Check non-forced editor close exit codes and record them in the report.

### Task 3: Corpus generator

**Files:** `tools/e2e/New-EditorCorpusManifest.ps1`

- [x] Enumerate the 14 installed level directories.
- [x] Fail on missing or empty scene/DAT/model/texture/lightmap files and missing terrain directories.
- [x] Emit non-mutating smoke and terrain-shortcut scenarios per level with weather, texture, render, pause, cursor, terrain, and graceful-close assertions.

### Task 4: Verification and documentation

**Files:** `docs/TESTS.md`, `tools/e2e/README.md`, the corpus spec and plan

- [x] Document generation and contract validation.
- [x] Run the complete live 14-level smoke matrix from `D:\\IGI1` and inspect every report/screenshot.
- [x] Run the existing focused live workflows and installed-corpus GoogleTest regression gate.
- [ ] Commit the expanded matrix only after the active terrain and texture defects have their dedicated evidence retained.
