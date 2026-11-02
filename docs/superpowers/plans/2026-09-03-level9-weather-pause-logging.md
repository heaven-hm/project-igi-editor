# Level 9 Weather and Pause Logging Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make editor weather follow the authored RainEffect for Level 9, show only one cursor in the pause menu, and provide tested pause-menu logging controls.

**Architecture:** Keep RainEffect as the authoritative weather source and remove the editor-only visual-building AABB suppression that can hide valid rain. Put cursor and log-filter decisions in small testable runtime policies. Extend the existing configuration, logger, pause-menu layout, renderer labels, and mouse actions without changing unrelated editor behavior.

**Tech Stack:** C++17, CMake, GoogleTest, GLUT/OpenGL editor runtime, QSC/QVM configuration.

**Spec:**

- An active authored RainEffect, including Level 9's quoted newline boolean token, renders in the editor regardless of visual building-bound overlaps.
- The pause menu uses the native cursor only; the custom game cursor is not drawn while it is open.
- The pause menu can enable or disable logging and select DEBUG, INFO, WARNING, ERROR, or FATAL minimum severity.
- QEDLogs, QEDDebug, and the new QEDLogLevel configuration state round-trip safely, and disabled logging creates no output.

## Global Constraints

- Preserve unrelated dirty work and stay on the current branch.
- Do not modify level assets to special-case Level 9.
- Write regression tests before each corresponding implementation change.
- Build with the existing Visual Studio/CMake workaround and report any pre-existing full-suite failures separately.

---

## Tasks

- [ ] 1. Add failing runtime-policy tests for authored weather visibility and pause cursor visibility.
  - Verify: `igi_tests --gtest_filter=LevelWeatherTest.*:RuntimeSubsystemsTest.*` initially fails to compile or fails before implementation.

- [ ] 2. Remove visual-AABB rain suppression and guard custom cursor drawing while paused.
  - Verify: the focused weather/cursor tests pass and the Level 9 QSC token is exercised exactly.

- [ ] 3. Add failing configuration and log-policy tests for logging toggles, five severity thresholds, and QSC round-trip.
  - Verify: focused config/logger tests fail before the configuration/logger implementation.

- [ ] 4. Implement log-level persistence, filtering, pause-menu rows, labels, and mouse actions.
  - Verify: focused configuration, logger, runtime, and pause-layout tests pass.

- [ ] 5. Build the editor, run the relevant test set, and deploy/launch only after the currently running editor is closed.
  - Verify: Release editor builds; deployed editor launch is visible and responsive in Session 1 when deployment is possible.
