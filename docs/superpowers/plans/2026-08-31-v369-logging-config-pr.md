# v3.6.9 Logging Configuration PR Plan

> **For Codex:** Execute each checkbox in order and keep this branch scoped to logging, QSC/QVM configuration, and their verification.

**Goal:** Make `qedconfig.qsc` the authoritative logging configuration, compile it to `qedconfig.qvm`, and ensure disabled logging never creates or changes the editor log.

**Architecture:** Keep configuration parsing in `Config` and file creation in `Logger`. `Logger::Init` records a destination without opening it; the first permitted `Log` call opens it. Only the compiled `qedconfig.qvm` may set global logging flags, while invalid source fails closed.

**Tech Stack:** C++17, CMake, GoogleTest, QSC lexer/parser/compiler, QVM parser/decompiler, PowerShell live verification.

---

- [x] Capture the `origin/develop` baseline and reproduce disabled-log creation with a unit test.
- [x] Add unit coverage for lazy file creation, enabled logging, debug filtering, and repeat initialization.
- [x] Add configuration tests proving QSC-to-sibling-QVM conversion, authoritative config loading, unrelated-QVM isolation, and invalid-QSC fail-closed behavior.
- [x] Implement the minimum `Config` and `Logger` changes required by those tests.
- [x] Verify focused tests, then the complete source-built test suite with working directory `D:\IGI1` (251 non-verify tests plus 14 enabled-log level verifications).
- [x] Run live editor checks against disposable QSC/QVM/log snapshots for logging disabled, enabled, then disabled again; restore and hash-check every user file.
- [ ] Run two independent reviews for runtime safety and QSC/QVM persistence; fix every blocker. Local static passes completed; external free-model agents were blocked by the source-sharing control.
- [ ] Commit, push, and open a PR targeting `develop`; do not merge it. Local commit is next; GitHub auth/network is currently blocked.
