# Coding Guidelines

These guidelines define the engineering bar for the Windows gameplay runtime.
They adapt Matt Pocock's emphasis on explicit contracts, narrow seams, and
readable code to this C++20 codebase.

## Design principles

- Keep runtime state separate from editor/source state. Runtime objects own
  mutable simulation data; editor objects are read-only inputs to a session.
- Prefer small, single-purpose types and functions with one reason to change.
  A class should expose the smallest public interface that its callers need.
- Use dependency injection at subsystem boundaries. Collision, time, input,
  visibility, and native script calls must be replaceable in headless tests.
- Use RAII and value semantics for ownership. `std::unique_ptr` expresses
  exclusive ownership; `std::shared_ptr` is reserved for an intentional tree
  or graph lifetime. Never encode ownership in a raw pointer.
- Make invalid states difficult to represent. Use scoped enums, named structs,
  explicit units, bounded counts, and checked operations at external inputs.
- Treat OpenIGI behavior as `verified-reference` evidence. Mark inferred or
  placeholder behavior in the code and tests; do not imply retail parity
  without repeatable IGI1 evidence.

## Naming and readability

- Use descriptive full names for variables, parameters, and public methods.
  Avoid abbreviations such as `gq`, `dt`, `obs`, or `tmp` in new code.
- Use `PascalCase` for types and `camelCase` for functions and variables. Keep
  existing public names only when changing them would break the branch API.
- Keep one concept at one abstraction level per function. Extract a named
  helper when a function needs a comment to explain a control-flow block.
- Prefer early returns for invalid input and guard conditions. Keep the happy
  path visually linear.
- Comments explain intent, invariants, evidence, or a non-obvious trade-off.
  They must not restate the code. Every reference-derived constant carries an
  evidence label when its provenance matters.

## C++20 rules

- Prefer `const`, references, `std::span`, and `std::string_view` where they
  make ownership and mutation explicit. Do not copy large runtime data without
  a reason.
- Initialize every member at its declaration or in the constructor. Avoid
  sentinel values when an enum or `std::optional` communicates the state.
- Check bounds, stack depth, instruction pointers, call depth, and resource
  counts at every untrusted boundary. A malformed level or script must fail
  safely and deterministically.
- Keep platform APIs behind Windows-specific adapters. Gameplay logic must not
  depend on OpenGL, Win32 window handles, or editor globals.
- Use `std::chrono` or the repository's monotonic clock boundary for time.
  Simulation code receives fixed-step data and never derives gameplay from
  render-frame delta time.
- Do not use exceptions for ordinary gameplay outcomes. Return a result,
  boolean, or explicit state for expected failure; reserve exceptions for
  programmer contract violations or unrecoverable setup errors.

## DRY and SOLID in practice

- Share domain rules, not incidental implementation. A helper is justified
  when the same rule has at least two real callers and a single source of truth
  improves correctness.
- Keep interfaces focused (Interface Segregation) and depend on abstractions
  at system boundaries (Dependency Inversion). Avoid speculative frameworks.
- Favor composition over inheritance. Inheritance is appropriate for the
  existing task lifecycle seam or a genuinely substitutable runtime type.
- Do not hide side effects behind getters or utility functions. Names should
  make mutation, allocation, I/O, and platform work apparent.

## TDD and verification

- Work in vertical red-green-refactor slices. Write one behavior test at a
  public seam, make it fail for the intended reason, implement the smallest
  change, then refactor only after it is green.
- Tests describe observable behavior, not private fields or implementation
  details. Use deterministic fakes for terrain, collision, visibility, input,
  and native calls.
- Every bug fix adds a regression test. Every placeholder replaced by runtime
  behavior gets a focused test and an evidence label.
- Before claiming completion, run `git diff --check`, the focused tests, the
  Windows CMake build, and the full test target when the environment permits.
  If Windows tooling is unavailable, report the exact verification gap.

The proprietary vanilla install is an external fixture, never a repository
dependency. Set `IGI_VANILLA_ROOT` to a vanilla Project IGI installation to
run `VanillaFixtureParityTest`; the test checks real level-one QVM gameplay
definitions and navigation data, and skips when the fixture is not mounted.
Fixture evidence supplements focused runtime tests; it does not replace
Windows interactive verification.

## Review checklist

- Does the change preserve editor state across open, tick, restart, and close?
- Are ownership, units, time boundaries, and failure behavior explicit?
- Can the behavior be tested without a renderer or native Windows window?
- Are names, comments, and error messages precise enough for a new maintainer?
- Is the implementation the smallest complete vertical slice, with no
  speculative abstraction or unverified parity claim?
