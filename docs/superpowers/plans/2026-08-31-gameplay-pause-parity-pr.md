# Gameplay and Pause Menu Parity PR Plan

> **For Codex:** Execute each checkbox in order. Add behavior only when supported by retail IGI, open-igi, project documentation, or a reproducible defect.

**Goal:** Keep the Escape menu modal and usable in editor and gameplay modes, allow level selection in editor mode, and improve gameplay behavior using clean-room retail evidence.

**Architecture:** Preserve the existing six-commit gameplay runtime as the base. Route Escape-menu input before editor/gameplay input, centralize menu geometry shared by drawing and hit-testing, and keep level loading mode-independent where the menu exposes it.

**Tech Stack:** C++17, OpenGL/GLUT, GoogleTest, open-igi reference source, retail `igi.exe` and menu QVM evidence, Ghidra Headless MCP, r2mcp.

---

- [x] Record retail `igi.exe` identity and inspect relevant menu/input evidence through Ghidra Headless MCP, with r2mcp as independent validation.
- [x] Inspect open-igi pause, input, level-flow, and gameplay behavior and record exact provenance; do not copy uncertain layouts or addresses.
- [x] Add failing tests for Escape visibility, modal input, editor-mode level switching, gameplay preservation, and on-screen row geometry.
- [x] Port the smallest compatible behavior from commit `75dc9cb`, resolving against the six-commit runtime branch without logging/config changes.
- [x] Investigate and fix only additional major gameplay/pause defects with direct evidence and regression tests (pause clock catch-up and held-input reset).
- [x] Run focused tests, complete source-built tests from `D:\IGI1`, and all real-level verification cases (260 non-verify plus 14 verify).
- [x] Launch the editor visibly through WMI, verify Session 1 health, and close the exact launched PID. Interactive Escape clicks were covered by unit tests but not automated in the unavailable GUI controller.
- [ ] Run two independent reviews for runtime safety/retail parity and test/API scope; fix every blocker. Local static passes completed; external free-model agents were blocked by the source-sharing control.
- [ ] Commit, push, and open a PR targeting `develop`; do not merge it. Local commit is next; GitHub auth/network is currently blocked.
