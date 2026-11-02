# IGI Editor Testing Master Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Use superpowers:subagent-driven-development only when parallel agent work is authorized. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Establish reproducible, layered regression protection for the Windows IGI editor, from binary parsing and editing policies through persistence, native runtime behavior, visible user workflows, rendering, and release packaging.

**Architecture:** Extend the existing Google Test, PowerShell E2E, native capture, and visual-integrity infrastructure. Introduce small test seams only where real dependencies prevent reliable testing. Separate synthetic tests, private installed-corpus checks, GPU checks, and interactive-desktop checks so each result states what actually executed.

**Tech Stack:** C++20, MSVC Win32, CMake/CTest, Google Test 1.14.0 as currently declared, PowerShell, Python standard-library tests, OpenGL/FreeGLUT, WMI, optional MSVC AddressSanitizer and LLVM fuzzing lanes.

**Spec:** The user's September 8, 2026 request for an in-depth testing master plan, with exact files, methods, examples, implementation order, and execution timing. This document contains the acceptance specification as well as the implementation plan. Existing workflow specifications under `docs/superpowers/specs/` remain context; conflicts with observed code must be resolved explicitly.

## 1. Scope, evidence, and global constraints

This is an implementation document, not a claim that these changes or tests have run. Only this document is being added. Estimates, thresholds, proposed interfaces, and future commands below are recommendations, not measured results.

Inspected baseline: branch `e2e-live-testing`, commit `f4577d1cb5f585c060276b59289c3f110eba7ef7`, September 8, 2026. The worktree was clean at initial inspection. Concurrent changes to App/input/UI/camera tests and a new cursor policy header appeared during document preparation and were left untouched. Re-resolve symbols before applying patches, including changes made without a new commit; method names are more durable anchors than line numbers.

Global constraints:

- Preserve unrelated changes and installed game data. Default write tests to independent disposable directories; never hardlink mutable fixtures to retail files.
- Production compatibility remains Windows x86. x64 parser experiments do not establish x86 editor compatibility.
- Launch `D:\IGI1\igi.exe` only through WMI `Win32_Process.Create`, with working directory `D:\IGI1`; verify Session 1, responding state, and working set greater than 30 MB. Visible editor tests use the existing WMI runner.
- Read project format documentation before implementing parser changes. Current equivalents are `docs/game_data_types.md`, `docs/game_file_formats.md`, and `docs/game_model_naming.md`. The instruction paths `parameter_data_types.md`, `format_fnt.md`, `model_naming.md`, and `file-formats.md` are absent.
- `docs/game_structure.md` describes IGI2. IGI1 corpus jobs use `missions/location0/level1` through `level14`; do not import IGI2 location numbering into tests.
- FNT documentation and code disagree on `TRN2` versus `TRAN`. `source/renderer/fnt_parser.cpp` explicitly documents and parses `TRAN`. Fixtures must cite verified byte layouts; do not encode an unverified documentation diagram as the oracle.
- No model ID, level ID, user query, or test-case name may select production pass/fail behavior. Named regression assets belong in test data, with a general rule exercised by additional synthetic variants.
- Automated pass/fail must not depend on an LLM. If optional human-facing AI triage is added later, only OpenRouter free models may be used; it must remain optional and cannot waive a failure.
- No email integration is needed. If later added, use `DEFAULT_RECIPIENT_EMAIL`, never a hardcoded address, and require authorization before sending.
- Each implemented bug fix gets a stable bug ID and a mem0 summary of symptom, cause, resolution, and verification. If mem0 is unavailable, record the unsent summary in the bug record and report that persistence is pending. Do not describe a plan finding as a fixed bug.
- Account usage percentage is not exposed by the available tools. The requested stop below 25% requires a user/platform usage signal; context capacity is not a substitute for account usage.

## 2. Verified infrastructure and gaps

| Area | Existing implementation | Consequence for this plan |
|---|---|---|
| Build | Root `CMakeLists.txt`, aggregate `igi_tests`, separate `editor_camera_start_tests`, `igi_mcp` | Extend existing targets; gradually extract truly independent tests |
| Source inventory | 51 `tests/*.cpp` files | File count is not registered/executed test count |
| Unregistered files | `test_ai_behavior.cpp`, `test_debug_command_manager.cpp`, `test_res_stream_append.cpp` absent from root CMake source lists | Repair/classify before registration; fail future silent omissions |
| Debug-command test | Defines a different `class App`, writes `commands.txt`, sleeps 500 ms | Do not link this fake class against production `App`; replace with a real seam |
| Fixture isolation | `McpTransactionTest::SetUp` uses a fixed temp directory; malformed FNT uses a fixed temp filename | Parallel runs can collide; isolation precedes parallel CTest |
| Installed corpus | Several parser tests find assets through `Utils::GetIGIRootPath`; FNT chooses the first recursively discovered font | Replace nondeterministic discovery with declared fixtures |
| Mixed requirements | `test_runtime_subsystems.cpp` mixes policy and installed font tests | A filename-level label alone cannot correctly classify it |
| Test documentation | `docs/TESTS.md` contains historical 230 and 675 totals | Generate current results from built executables; preserve history separately |
| Visual status | Status document reports WinchHouse failure; `docs/TESTS.md` reports later success | Associate every claim with source hash, binary hash, corpus hash, and artifact path |
| CTest discovery | Camera test source included in aggregate and standalone executables, neither discovery call has a prefix | Disambiguate CTest names; eventually give each test one owner |
| Dependencies | FetchContent is forcibly disconnected; converter script downloads a pinned binary unless `IGI1CONV_NO_FETCH` exists | Fresh/offline setup must explicitly supply Google Test; avoid source-tree converter refresh in verification |
| E2E | `editor-e2e.ps1` validates manifests, sends real input, captures screenshots, snapshots/restores declared files | Harden and extend this runner; avoid a second GUI automation framework |
| Native visual | `DebugCommandManager::CaptureModel`, `EvaluateVisualIntegrity`, native PowerShell runner | Strengthen existing structural evidence with adversarial and appearance tests |
| View reporting | Runner builds a first-N view list but later selects top-coverage evidence; C++ evaluates all native views | Separate requested, captured, analyzed, and report-selected view identities |
| CI | No `.github/` directory found | Add CI only after local commands and capability boundaries are reliable |

No current full-suite baseline was executed during this planning task. Existing binaries and historical logs are not proof of the inspected source passing.

## 3. Test layers and when they execute

| Layer | Real question answered | Environment | Trigger | Initial runtime budget |
|---|---|---|---|---|
| Build/registration | Was intended source compiled and were intended tests discovered? | Clean Win32 toolchain | Every change | Measure; aim under 10 minutes warm |
| Unit/policy | Does one public behavior satisfy its contract? | Synthetic data; no game, GL context, or desktop | Edit loop and every PR | Under 60 seconds total after extraction |
| Component | Do parser/writer, filesystem, and process seams cooperate? | Unique temp files; fake external responses where appropriate | Every PR | Under 5 minutes |
| Corpus integration | Do supported real format variants and all levels work? | Private immutable corpus | Relevant PR, nightly, release | Establish baseline before timeout limits |
| Protocol/CLI | Do real clients observe valid framing, errors, exits, and persistence? | Child process, private loopback endpoint, disposable project | MCP/CLI PR and release | Under 3 minutes initially |
| GUI E2E | Can the user complete an operation via visible input? | Unlocked Session 1, serial | UI/persistence PR; nightly smoke | 5–15 minutes focused |
| Structural visual | Are expected geometry, attachments, depth, and transforms present? | Pinned GL machine, deterministic captures | Rendering/asset PR | Bounded representative batch |
| Appearance visual | Are text, materials, colors, transparency, and layout correct? | Pinned display/GPU/font baseline | UI/renderer PR | 5–15 minutes focused |
| Fuzz/sanitizer | Can malformed inputs corrupt memory or hang? | Isolated instrumented parser processes | Nightly; relevant PR short smoke | 60 s/target PR, 15 min/target nightly |
| Performance/soak | Does latency or resource use degrade over time? | Stable machine and workload | Weekly and release | 30 cycles/nightly; 200 weekly |
| Packaging | Does the assembled release work without developer files? | Clean staging directory and private game copy | Every release candidate | Dedicated gate |
| Exploratory/manual | Are workflows understandable and usable beyond scripted assertions? | Human interactive session | Major UI changes and release | Checklist, recorded outcome |

These are scheduling targets, not initial hard timeouts. First collect successful-run distributions; derive hard deadlines from measured p99 plus margin. A missing capability is `BLOCKED` for a required lane, not `PASS`.

## 4. Common acceptance and reporting contract

Every lane emits a machine-readable report with exactly one terminal status: `PASS`, `FAIL`, `BLOCKED`, or `NOT_RUN`. Individual cases may additionally be `SKIP` or `INCONCLUSIVE`. A required skip or inconclusive visual result prevents release acceptance. `PREPARED` manifests are inventory artifacts, never execution evidence.

Proposed `tools/testing/run-result.schema.json` shape:

```json
{
  "schemaVersion": 1,
  "runId": "unique-per-execution",
  "lane": "unit",
  "status": "PASS",
  "source": { "commit": "full-git-sha", "dirty": false, "diffSha256": null },
  "binary": { "path": "absolute-executable-path", "sha256": "64-hex-digits", "architecture": "x86" },
  "environment": { "osBuild": "captured-value", "compiler": "captured-value", "sessionId": 1 },
  "selection": { "expected": 1, "discovered": 1, "executed": 1 },
  "counts": { "passed": 1, "failed": 0, "skipped": 0, "inconclusive": 0 },
  "cases": [{ "id": "suite.case", "status": "PASS", "durationMs": 4, "artifacts": [] }],
  "cleanup": { "status": "PASS", "changedSourceFiles": [] }
}
```

Values above describe the schema, not a real result. Populate provenance before execution. Add corpus manifest hash, converter hash, GL vendor/renderer/version, client dimensions, DPI, configuration hash, RNG seed, and view identities to lanes that need them. Store case status before cleanup and cleanup status separately; cleanup failure must not erase the original error.

Coverage has separate denominators:

1. Registered tests versus test declarations eligible for the build.
2. Source line/branch coverage for selected production modules.
3. Format variants observed and tested versus variants inventoried.
4. Applicable workflow-action/level pairs executed versus declared.
5. Object/task/LOD/view combinations analyzed versus selected and applicable.
6. Negative visual faults detected versus intentionally injected faults.

Never report a percentage combining these denominators. A thousand screenshots do not establish a thousand successful behavioral assertions.

## 5. File ownership and implementation order

| New file/group | Responsibility | First task |
|---|---|---|
| `tools/testing/Invoke-TestLane.ps1` | Build provenance, lane execution, exit checking, reports | 1 |
| `tools/testing/test-source-disposition.json` | Explicit ownership or temporary exclusion of test sources | 2 |
| `tests/support/temp_directory.h` | Unique owned fixture directories | 3 |
| `tests/support/test_paths.h` | Explicit synthetic/corpus path resolution | 3 |
| `tests/test_editor_policy.cpp` | Extracted autosave/history policy tests | 4 |
| `tests/test_game_clock.cpp` | Pure scheduler-clock contract | 4, 8 |
| `tests/fixtures/formats/` | Authored minimal format cases with provenance | 5 |
| `tests/test_save_failure.cpp` | Editor save-chain fault behavior | 7 |
| `tests/protocol/` | Real MCP transport/client tests | 10 |
| `tools/e2e/visual-baselines/` | Versioned baseline metadata, masks, approved small reference images | 13 |
| `tools/testing/Test-VisualImage.py` | Deterministic image comparison | 13 |
| `tests/fuzz/` | Standalone bounded parser fuzz entrypoints | 15 |
| `tools/testing/Test-ReleaseLayout.ps1` | Staging/package invariants | 18 |
| `.github/workflows/tests.yml` | Hosted synthetic and separate private capability gates | 19 |
| `tests/regressions.json` | Bug-to-test-to-live-case traceability | 20 |

Each numbered task is a reviewable change. Within a task: add the failing test or deliberately failing harness fixture, prove the expected failure, implement the smallest change, run the focused command, inspect artifacts, and make one scoped commit. Do not commit another developer's files. When moving existing tests, preserve assertions before adding new behavior.

## 6. Task 1 — Establish reproducible build and baseline evidence

**When:** First; before interpreting failures or enabling CI. **Files:** `CMakeLists.txt`, `cmake/fetch_igi1conv.cmake`, proposed `tools/testing/Invoke-TestLane.ps1`, `docs/TESTS.md`.

**Consumes:** Current build targets. **Produces:** A verified executable path/hash and registration list for every baseline run.

- [ ] Capture `git rev-parse HEAD`, dirty diff hash, CMake version, compiler path/version, architecture, dependency paths, and converter SHA-256.
- [ ] Set `IGI1CONV_NO_FETCH=1` for test builds. Do not let a baseline build change a committed dependency.
- [ ] Use a new build directory. The inspected existing cache uses `NMake Makefiles` and an x86 compiler; do not reconfigure it with a Visual Studio generator.
- [ ] Change forced disconnected FetchContent into a user-settable default; supply a previously provisioned Google Test 1.14.0 source directory for offline CI. An unavailable dependency is a setup failure.
- [ ] Build explicitly, then list tests from that executable. Do not run a stale `bin/igi_tests.exe` merely because it exists.

Change the existing forced setting to:

```cmake
option(FETCHCONTENT_FULLY_DISCONNECTED "Use already provisioned dependencies" ON)
```

In `Invoke-TestLane.ps1`, require existing `-GoogleTestSource`, `-BuildRoot`, and `-ArtifactsRoot` paths/arguments. The first executable baseline implementation can use:

```powershell
param(
    [Parameter(Mandatory)][string]$GoogleTestSource,
    [Parameter(Mandatory)][string]$BuildRoot,
    [Parameter(Mandatory)][string]$ArtifactsRoot
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath (Join-Path $GoogleTestSource 'CMakeLists.txt'))) {
    throw 'Provision Google Test 1.14.0 source before configuring.'
}
$env:IGI1CONV_NO_FETCH = '1'
New-Item -ItemType Directory -Path $ArtifactsRoot -Force | Out-Null
& cmake -S . -B $BuildRoot -G 'Visual Studio 17 2022' -A Win32 `
    "-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=$GoogleTestSource"
if ($LASTEXITCODE -ne 0) { throw 'Configure failed.' }
& cmake --build $BuildRoot --config Release --target igi_tests editor_camera_start_tests -j 1
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
$testExe = [IO.Path]::GetFullPath('bin/Release/igi_tests.exe')
if (-not (Test-Path -LiteralPath $testExe)) { throw 'Expected binary missing.' }
$listing = & $testExe --gtest_list_tests 2>&1
if ($LASTEXITCODE -ne 0) { throw 'Test discovery failed.' }
$listing | Set-Content -LiteralPath (Join-Path $ArtifactsRoot 'registered-tests.txt')
Get-FileHash -LiteralPath $testExe -Algorithm SHA256 |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $ArtifactsRoot 'binary.json')
```

This script fragment assumes invocation from the repository root and current output layout. Later accept an explicit executable resolved from build metadata. Restore process-local environment overrides in a `finally` block when embedding the runner in a long-lived PowerShell process.

**Failure tests:** missing dependency source, compiler failure, nonexistent binary, nonzero discovery exit, zero discovered tests, stale binary whose provenance references another commit. Use small fixture executables/scripts to exercise the report wrapper; never reinterpret an exit failure as zero tests passed.

**Gate:** A clean machine can reproduce configuration with declared dependencies; build errors propagate; a registration artifact and binary hash exist. Only then refresh numerical counts in `docs/TESTS.md`. Historical results belong in dated records.

## 7. Task 2 — Close registration gaps and separate test identities

**When:** After Task 1; before publishing a test baseline. **Files:** `CMakeLists.txt`, the three unregistered test files, `tests/test_debug_cli.py`, proposed disposition manifest and `tools/testing/Test-TestRegistration.ps1`.

- [ ] Add `tests/test_ai_behavior.cpp` and `tests/test_res_stream_append.cpp` to `igi_tests` individually, compile each addition, and resolve actual API/link failures without weakening assertions.
- [ ] Keep the debug lifecycle file explicitly marked `repair-required` until Task 9 removes the substitute `App` definition.
- [ ] Register Python CLI tests only after Task 3 isolates their current-directory writes.
- [ ] Give CTest discoveries different prefixes immediately.

```cmake
# Replace the two existing discovery calls, retaining their working directory.
gtest_discover_tests(igi_tests
    TEST_PREFIX "aggregate."
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
gtest_discover_tests(editor_camera_start_tests
    TEST_PREFIX "camera."
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    PROPERTIES LABELS "unit;camera" TIMEOUT 10)
```

The final disposition manifest assigns every `test_*.cpp`/`test_*.py` file an owner target or an exclusion with reason, owner, and expiry. Initial exclusion:

```json
{
  "tests/test_debug_command_manager.cpp": {
    "status": "repair-required",
    "reason": "Conflicting App definition and nondeterministic file watcher test",
    "resolvesIn": "Task 9"
  }
}
```

Use CMake File API target source information after configuration as the registration source of truth. A simple CMake-text search is useful for initial auditing but is insufficient once `target_sources` and helper modules are involved. Compare source ownership to manifest entries; compare CTest discovery to executable discovery after accounting for prefixes and intentional parameterization.

**Verification:** Add a temporary unowned test file in a fixture project and assert the audit fails. Add two discovery targets with matching suite names and verify their prefixed CTest IDs remain distinct. Missing test binaries and `*_NOT_BUILT` results fail the lane. Final ownership should remove `test_editor_camera_start.cpp` from the aggregate once standalone execution is included everywhere.

## 8. Task 3 — Make test files and environment hermetic

**When:** Before parallelism, fuzzing, or repeated runs. **Files:** proposed `tests/support/temp_directory.h`, `tests/support/test_paths.h`; current `test_mcp_transaction.cpp`, `test_fnt_parser.cpp`, `test_debug_cli.py`, other fixed-temp-path tests found by audit.

**Consumes:** OS temp root. **Produces:** An exclusively created directory owned by one fixture instance. Do not add a production dependency for this test helper.

```cpp
// tests/support/temp_directory.h
#pragma once
#include <windows.h>
#include <atomic>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace test_support {
class TempDirectory {
public:
    TempDirectory() {
        static std::atomic<unsigned long long> sequence{0};
        const auto base = std::filesystem::temp_directory_path();
        for (unsigned attempt = 0; attempt < 128; ++attempt) {
            auto candidate = base / ("igi-test-" + std::to_string(GetCurrentProcessId()) +
                "-" + std::to_string(GetTickCount64()) + "-" +
                std::to_string(sequence.fetch_add(1)));
            std::error_code ec;
            if (std::filesystem::create_directory(candidate, ec)) {
                path_ = std::move(candidate);
                return;
            }
            if (ec && ec != std::errc::file_exists)
                throw std::runtime_error(ec.message());
        }
        throw std::runtime_error("Cannot allocate exclusive test directory");
    }
    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;
    const std::filesystem::path& path() const noexcept { return path_; }
    bool Cleanup(std::error_code& ec) noexcept {
        if (path_.empty()) return true;
        std::filesystem::remove_all(path_, ec);
        if (ec) return false;
        path_.clear();
        return true;
    }
    ~TempDirectory() {
        std::error_code ec;
        Cleanup(ec);
    }
private:
    std::filesystem::path path_;
};
}
```

Use `Cleanup` explicitly in fixture teardown and assert its result; the destructor is only best-effort fallback. The helper never accepts a caller-supplied deletion target. Tests containing junctions/symlinks require separate containment tests and must not redirect this owned root.

In `McpTransactionTest`, add `test_support::TempDirectory temp_;`, set `root_ = temp_.path();`, and remove the pre-test `remove_all` of the shared name. Replace the malformed FNT path with `temp.path() / "invalid-skip.fnt"`.

For Python CLI tests, replace current-directory file deletion with an owned temporary directory. Add `pathlib` and `tempfile` imports and use this fixture setup and representative test:

```python
def setUp(self):
    self.temp = tempfile.TemporaryDirectory(prefix="igi-cli-test-")
    self.addCleanup(self.temp.cleanup)
    self.script = pathlib.Path(__file__).resolve().parents[1] / "tools" / "debug_cli.py"

def test_cli_writes_goto_command(self):
    result = subprocess.run(
        [sys.executable, str(self.script), "goto", "--level", "5", "--model", "123_45_6"],
        cwd=self.temp.name, capture_output=True, text=True, timeout=10,
    )
    self.assertEqual(result.returncode, 0, result.stderr)
    command_file = pathlib.Path(self.temp.name) / "commands.txt"
    self.assertEqual(command_file.read_text(), "goto level=5 model=123_45_6\n")
```

Resolve every other command-file assertion under that directory; never delete the repository's command file.

Proposed path contract:

```cpp
// tests/support/test_paths.h
#pragma once
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
inline std::filesystem::path RequiredFixtureRoot() {
    const char* value = std::getenv("IGI_TEST_FIXTURE_ROOT");
    if (!value || !*value) throw std::runtime_error("IGI_TEST_FIXTURE_ROOT is required");
    return std::filesystem::path(value);
}
inline std::filesystem::path RequiredCorpusRoot() {
    const char* value = std::getenv("IGI_TEST_CORPUS_ROOT");
    if (!value || !*value) throw std::runtime_error("IGI_TEST_CORPUS_ROOT is required");
    return std::filesystem::path(value);
}
```

Set these variables in the lane runner. For production code still using `IGI_GAME_PATH`, set it to the same disposable corpus path in that lane. Keep legacy `IGI_WEATHER_CORPUS` wired until its test migrates. Do not globally change `Utils::GetIGIRootPath` just to satisfy tests.

**Gate:** Two independent test processes pass concurrently without sharing files; shuffled repetitions do not leak environment/config singleton state; the checkout and original corpus hash inventories remain unchanged. Initial tests that touch globals remain serial until proven isolated.

## 9. Task 4 — Extract small policy tests from the runtime aggregate

**When:** After isolation; this creates the fast edit loop. **Files:** `tests/test_runtime_subsystems.cpp`, new `tests/test_editor_policy.cpp`, new `tests/test_game_clock.cpp`, `CMakeLists.txt`.

Move existing `AutoSavePolicyTest` and `EditorHistoryTest` cases unchanged into `test_editor_policy.cpp`. They already exercise real inline production functions in `source/runtime/auto_save_policy.h` and `editor_history.h`; no replacement mocks or new policy layer is needed. Move pure clock cases separately if present; do not copy tests into both binaries.

```cmake
add_executable(editor_policy_tests tests/test_editor_policy.cpp)
target_include_directories(editor_policy_tests PRIVATE "${PROJECT_SOURCE_DIR}/source")
target_link_libraries(editor_policy_tests PRIVATE gtest_main)
gtest_discover_tests(editor_policy_tests
    TEST_PREFIX "policy."
    PROPERTIES LABELS "unit;policy" TIMEOUT 10)

add_executable(game_clock_tests tests/test_game_clock.cpp source/game_clock.cpp)
target_include_directories(game_clock_tests PRIVATE "${PROJECT_SOURCE_DIR}/source")
target_link_libraries(game_clock_tests PRIVATE gtest_main)
gtest_discover_tests(game_clock_tests
    TEST_PREFIX "clock."
    PROPERTIES LABELS "unit;clock" TIMEOUT 10)
```

Add an explicit truth-table test, including invalid and boundary level values:

```cpp
#include <gtest/gtest.h>
#include "runtime/auto_save_policy.h"
TEST(AutoSavePolicyTest, TimerRequiresEveryEligibilityCondition) {
    for (bool editor : {false, true})
    for (bool enabled : {false, true})
    for (bool paused : {false, true})
    for (int level : {-1, 0, 1, 14}) {
        const bool expected = editor && enabled && !paused && level > 0;
        EXPECT_EQ(igi::ShouldRunAutoSave(editor, enabled, paused, level), expected)
            << editor << ',' << enabled << ',' << paused << ',' << level;
    }
}
```

The current external-launch function accepts only editor/enabled, and exit accepts editor/enabled/level. Do not silently unify their signatures: characterize current behavior and separately decide whether pause is intended to prevent an explicit launch/quit save. The policy test is paired with disk-write E2E in Task 12 so an unused correct helper cannot hide a bad caller.

For history, exercise empty undo/redo, maximum 0/1/20, new edit after undo clearing redo, branching history, and switching document clearing both stacks. Verify restored state values and order, not just stack lengths.

**Run:** `cmake --build build-testing --config Release --target editor_policy_tests game_clock_tests -j 1`, then from that build directory `ctest -C Release -L unit --output-on-failure`.

**Gate:** These targets run without installed data or creating a GL context. Keep graphics-linked runtime tests in their existing target until each source dependency has been mapped; do not perform a broad production refactor just to shrink a test executable.

## 10. Task 5 — Synthetic parser contracts and format-variant fixtures

**When:** Before format changes, fuzzing, or compiler upgrades. **Files:** existing parser tests, `source/renderer/fnt_parser.cpp/.h`, new `tests/fixtures/formats/manifest.json` and minimal authored files/builders.

Every format needs four categories: minimal valid input, malformed input, supported variants, and serialization invariants where writing is supported. Real-corpus smoke complements these and cannot replace them.

| Format / current source | Add assertions | Fixture variations |
|---|---|---|
| QSC `qsc_lexer.cpp`, `qsc_parser.cpp` | Token positions, escaped strings, complete consumption, invalid syntax, bounded nesting | BOM, CRLF/LF, empty strings, numeric boundaries, comments within calls |
| QVM `qvm_parser.cpp`, `qvm_compiler.cpp`, `qvm_decompiler.cpp` | Header/offset bounds, pool references, preserved call operands, deterministic compile | Empty valid program, literals, nested tasks, invalid pool indices, truncated tables |
| RES `res_writer.cpp`, `res_compiler.cpp` | Entry bounds, duplicate-name policy, exact untouched bytes, append rollback | Unknown chunks, zero-length entries, invalid skip, mixed path separators |
| MEF `mef_native.cpp` | Vertex stride/type, index ranges, material slots, finite transforms, attachment graph | Types 0/1/3, 28/32-byte bone records, attachments, missing optional chunks |
| TEX `tex_writer.cpp` and parser headers | Dimensions versus decoded buffer, channel order, alpha, overflow rejection | Supported versions 2/7/9/11 only where code confirms support |
| FNT `fnt_parser.cpp` | Glyph bearings, advances, character map, atlas decode | `TRAN`, RGB565, ARGB8888, missing BODY, bad skips, large dimensions |
| MTP/DAT `mtp_writer.cpp`, `dat_writer.cpp` | Count/name/texture ordering, preserved opaque records | Empty tables, duplicate IDs, unknown content, mismatched paired metadata |
| Graph `graph_writer.cpp`, `graph_project.cpp` | Sparse IDs, destination-major routing, edge references, save/reload | Disconnected nodes, max ID, self-edge policy, primary/cover graph pairing |
| Terrain `terrain_files.cpp` | Tile dimensions, sample order, map pairing, boundary heights | Tiny synthetic grids, seams, truncated payloads |
| Audio `audio_asset_resolver.cpp` | ILSF versus RIFF identification, output validation, cache invalidation | Header mismatch, corrupt conversion, valid silence |

Start FNT with a buffer seam so fuzzing does not require one file per input. Proposed addition to `fnt_parser.h`:

```cpp
#include <span>
FntFont FNT_ParseBytes(std::span<const uint8_t> bytes);
```

Implementation instructions: keep filesystem open/read checks in `FNT_Parse`; move the existing section starting at the ILFF validation through the final return into `FNT_ParseBytes`. Replace `buf` with the read-only span and `fileSize` with `bytes.size()`. Make the byte parser reject a buffer shorter than 20 before any read. Keep format validation semantics; retain contextual file logging in the file adapter. Both callers use the same parser.

```cpp
TEST(FntParserMalformedTest, RejectsEveryShortHeaderWithoutReadingPastEnd) {
    for (size_t length = 0; length < 20; ++length) {
        const std::vector<uint8_t> bytes(length, 0);
        EXPECT_FALSE(FNT_ParseBytes(bytes).valid) << length;
    }
}
```

Use synthetic little-endian field writers that append fields rather than packed C++ structs. Each fixture manifest records format variant, expected fields, whether byte equality is required, generator version, and provenance. Avoid copying proprietary retail assets into public Git.

**Gate:** For each newly covered format, one valid and one deliberately malformed fixture run without `D:\IGI1`; every invalid input leaves no output; error assertions identify the failing category without demanding incidental log wording. Document unsupported format variants as explicit coverage gaps.

## 11. Task 6 — Property-based and semantic round-trip regression

**When:** After minimal deterministic fixtures; run every parser/writer PR. **Files:** `test_qvm_roundtrip.cpp`, writer tests, `test_graph_parser.cpp`, `test_res_model_set.cpp`, `test_mission_expression.cpp`.

Property-based here initially means deterministic generated cases using the standard library and Google Test, avoiding a new framework dependency. Print the seed and failing case. Add a minimized regression fixture for every discovered failure.

Example lexer/parser metamorphic test using current interfaces:

```cpp
#include "level/qsc_lexer.h"
#include "level/qsc_parser.h"
TEST(QscParserPropertyTest, WhitespaceAndCommentsPreserveCallShape) {
    for (const std::string gap : {" ", "\n", "\r\n", "/*gap*/"}) {
        const std::string source = "F" + gap + "(" + gap + "17," + gap + "TRUE);";
        const auto lexed = qsc::Lex(source);
        ASSERT_TRUE(lexed.ok) << source << ':' << lexed.error;
        const auto parsed = qsc::Parse(lexed.tokens);
        ASSERT_TRUE(parsed.ok) << source << ':' << parsed.error;
        ASSERT_NE(parsed.program, nullptr);
        ASSERT_EQ(parsed.program->children.size(), 1u);
        ASSERT_EQ(parsed.program->children[0]->kind, qsc::NodeKind::ExprStmt);
        const auto& call = *parsed.program->children[0]->children.at(0);
        ASSERT_EQ(call.kind, qsc::NodeKind::Call);
        EXPECT_EQ(call.s_val, "F");
        ASSERT_EQ(call.children.size(), 2u);
        EXPECT_EQ(call.children[0]->i_val, 17);
        EXPECT_TRUE(call.children[1]->b_val);
    }
}
```

For QVM, extend the existing compile/write/parse/decompile test pipeline to compare normalized ASTs recursively: node kind; operator/callee/name strings; literal values; ordered children. Ignore source line/column and formatting only. Check negative zero, float precision, escaped quotes, and task nesting explicitly rather than deleting them during normalization.

For archives, hash untouched entry bytes before and after append. Compare the edited entry's intended decoded fields. Do not require whole-archive byte equality if the format permits normalized padding or order changes; preserve unknown payloads byte-for-byte when the writer promises preservation.

For graph changes, verify translation leaves edge connectivity and route destinations unchanged; saving and reparsing preserves sparse node IDs. For object serialization, exercise `LevelObjects::GenerateTaskLine`, `UpdateCoordinatesInLine`, and `SaveSubtreeToQSC` with nested children and unrelated siblings. Assert sibling fields remain unchanged.

**Gate:** Fixed seed set runs in PR; a larger seed set runs nightly. Round-trip tests must verify meaning, not just that the output reparses. Differential comparison against the bundled converter is additional evidence, not an independent oracle when it shares the same parsing assumptions.

## 12. Task 7 — Save integrity, rollback, and crash recovery

**When:** Before making GUI save tests mutating or shipping persistence changes. **Files:** `source/app_level.cpp:App::SaveCurrentLevel` (currently around line 747), `source/app_editor.cpp:App::SaveAndCompile`, `source/level/level_objects_serialize.cpp`, `source/mcp/mcp_transaction.cpp`, `tests/test_mcp_transaction.cpp`, new `tests/test_save_failure.cpp`.

Start with the existing transaction seam. Add a two-file rollback test using its actual validator interface:

```cpp
TEST_F(McpTransactionTest, PairValidationFailureRestoresBothOriginalFiles) {
    const auto second = target_.parent_path() / "objects.qsc";
    std::ofstream(second, std::ios::binary) << "original-source";
    std::string error;
    mcp::Transaction transaction(*scope_, {});
    ASSERT_TRUE(transaction.Stage("missions/location0/level1/objects.qvm", Bytes("new-binary"), error));
    ASSERT_TRUE(transaction.Stage("missions/location0/level1/objects.qsc", Bytes("new-source"), error));
    transaction.SetPostValidator([](const fs::path& relative, const fs::path&, std::string& why) {
        if (relative.extension() == ".qsc") {
            why = "injected paired validation failure";
            return false;
        }
        return true;
    });
    EXPECT_FALSE(transaction.Commit(error));
    EXPECT_EQ(ReadText(target_), "original");
    EXPECT_EQ(ReadText(second), "original-source");
}
```

This tests logical rollback, not power-loss atomicity. NTFS replacement of individual files does not make a QSC/QVM pair atomic. Document that distinction in failure reports.

Implementation sequence for editor saves:

- [ ] Characterize actual outputs of `SaveCurrentLevel`: QSC/QVM, graphs, attachments, lightmaps, and any archive changes. Capture the complete changed-file set in a disposable level.
- [ ] Test failures before serialization, during compile, during reparse, and during replacement. For deterministic I/O failure injection, add a narrow file-operation adapter only after these cases cannot be exercised through existing validators/OS sharing locks.
- [ ] Stage all new content beside destination files, validate syntax and references, then publish. Do not directly truncate authoritative files before successful compile/reparse.
- [ ] Return a structured save result from an extracted save operation; let `App::SaveCurrentLevel` present status and preserve dirty state on failure. Avoid coupling the editor directly to the MCP service merely to reuse its transaction class; extract only a shared low-level writer if both callers need it.
- [ ] Add a transaction journal only if crash recovery across the multi-file set is required. States: prepared, publishing, committed; retain old hashes/backups until recovery validates the entire new set. On restart, detect incomplete publishing and restore the old coherent set or complete the validated new set.

Fault matrix: destination read-only; destination held open without delete-sharing; insufficient output space on a disposable test volume; second-file validation failure; converter nonzero exit; converter success with missing output; converter timeout; rollback file locked; test process killed after first replacement. Use a child-process fault harness and explicit phase acknowledgements for crash tests, never kill the user's editor.

**Gate:** Every failed save preserves a coherent reopenable level, reports failure, and retains recoverable backups if automatic rollback fails. Successful saves survive reopen and a second parse; unrelated files retain hashes. Recovery tests are required before claiming crash-safe multi-file saves.

## 13. Task 8 — Deterministic simulation, pause, AI, and interaction tests

**When:** After fast-target extraction; every runtime PR. **Files:** `game_clock.cpp`, `runtime/simulation_scheduler.cpp`, `runtime/window_input_router.cpp`, `ai_system.cpp`, `ai_script_host.cpp`, `player_*`, `runtime/door_state.cpp`, existing AI/runtime tests.

Use supplied times and existing input/query callbacks; do not add sleeps to simulation tests. Pin retail-compatible startup/exclusion behavior before changing clock arithmetic.

```cpp
#include <gtest/gtest.h>
#include "game_clock.h"
TEST(GameClockRegressionTest, ExcludedTimeDoesNotGeneratePausedTicks) {
    igi::GameClock clock;
    clock.Reset(1000);
    clock.Update(1000);
    clock.BeginExcludedTime(1000);
    clock.Update(11000);
    EXPECT_FALSE(clock.IsTickDue(11000));
    EXPECT_EQ(clock.GetTickCount(), 0u);
    clock.EndExcludedTime(11000);
    EXPECT_EQ(clock.GetExcludedMilliseconds(), 10000);
    EXPECT_FALSE(clock.IsTickDue(11000));
    EXPECT_TRUE(clock.IsTickDue(11001));
}
```

Add cases for nested exclusion scopes, focus loss while W is held, pause while firing, resumed first tick, catch-up cap, startup render guards, reset during level transition, and the documented signed 32-bit wrap behavior. Do not change wrap semantics to a more intuitive rule without a separate compatibility decision.

Exercise `WindowInputRouter::OnKeyboardKey`, `SetFocus`, `ResetInputState`, and `SimulationScheduler::Update` in a sequence test: focus gameplay → press movement → pause/reset → release key while unfocused → resume → verify no stale movement/fire command.

AI cases: authored patrol starts, pending action prevents repeated IDLE dispatch, route completion restarts appropriately, no authored patrol remains idle, sparse route nodes, unavailable graph, soldier versus civilian response, target occlusion, animation request progresses across fixed ticks. Extend `test_ai_patrol_port.cpp` and register/reconcile `test_ai_behavior.cpp`; verify existing model APIs before moving old expectations.

Interactions: initial collision overlap, ladder entry/exit, door closed/open/blocked transitions, weapon ammo decrement and cooldown, projectile hits exactly once, objective/event ordering, and level reload clearing old entities. Assert public state/results through the existing runtime interfaces. Rendering E2E separately establishes that updated poses are actually drawn.

**Gate:** Replaying identical initial state and inputs produces identical gameplay outcomes in the supported build. Float comparisons use justified tolerances; cross-compiler bitwise equality is not required unless a format contract demands it.

## 14. Task 9 — Lifecycle, process ownership, and debug command delivery

**When:** Before stress runs and enabling the excluded debug lifecycle test. **Files:** `source/app.cpp:App::Shutdown` (around line 276), `renderer/renderer.cpp:Renderer::Shutdown`, `debug_command_manager.h/.cpp`, `tests/test_debug_command_manager.cpp`.

Remove the locally defined substitute `App` class from the test. The watcher and queue need two real adapters: production file polling/dispatch and test-controlled delivery. Keep `App*` command execution in the production adapter; test the watcher/queue without a pretend editor object.

Proposed internal extraction, only for command delivery:

```cpp
// Proposed source/debug_command_queue.h
#pragma once
#include "debug_command_manager.h"
#include <mutex>
#include <queue>
class DebugCommandQueue {
public:
    void Push(DebugCommand command) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(command));
    }
    bool TryPop(DebugCommand& command) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return false;
        command = std::move(queue_.front());
        queue_.pop();
        return true;
    }
private:
    std::mutex mutex_;
    std::queue<DebugCommand> queue_;
};
```

Move `DebugCommand` into its own header if including the manager would create a cycle when integrating this queue. `WatcherThread` pushes; `Update` drains and calls the unchanged `ProcessCommand` on the main thread. Add tests for FIFO, empty queue, multiple producers with per-producer ordering, and exactly-once consumption.

For watcher lifecycle, accept a command-file path at construction while preserving the existing production default. Use a completion signal/condition variable with a deadline to observe consumption; replace the 500 ms sleep. Test Start/Stop twice, destruction after Start, partial writes, malformed commands, and stopping with pending input. Define whether unconsumed commands survive stop; test that documented policy.

For application shutdown, verify the game-monitor thread finishes before its handle closes, window subclass restoration happens before object destruction, and renderer cleanup occurs while its GL context is valid. Use a serial live test to close normally and require process exit code zero. A forced kill can clean up a failed test but cannot satisfy the graceful-quit assertion.

**Gate:** Repaired lifecycle test is registered, has no fake `App`, no shared command file, and no timing-only success condition. Repeat 100 start/stop cycles. App/renderer shutdown repeated or reached through destructor must not double-free resources.

## 15. Task 10 — Real MCP transport and CLI contracts

**When:** After filesystem isolation; every MCP/CLI change. **Files:** `source/mcp/mcp_server.cpp`, `mcp_transport_stdio.cpp`, `mcp_transport_http.cpp`, `mcp_transaction.cpp`, existing `test_mcp_*`, proposed `tests/protocol/test_mcp_process.py`.

Keep in-process tool tests and add a real process client. Launch `igi_mcp` with its actual documented command-line options from `docs/MCP.md`; these are server processes, not `igi.exe`. Use Python `subprocess` for server ownership, pipes, timeouts, and teardown. Avoid hardcoded live credentials or ports; allocate an available loopback port where supported.

Protocol sequence: initialize using a supported version → validate response ID/capabilities → send initialized notification → tools/list → call one read-only tool on a synthetic project → call a mutation in a disposable project → re-read state → shutdown/EOF → require bounded process exit. For HTTP retain the returned `Mcp-Session-Id` when the server provides/requires it; test the server's supported transport rather than assuming a different MCP transport revision.

Add a reusable assertion that rejects a superficially valid response with the wrong request ID:

```python
def require_response(message, request_id):
    if message.get("jsonrpc") != "2.0":
        raise AssertionError("invalid JSON-RPC version")
    if message.get("id") != request_id:
        raise AssertionError("response ID mismatch")
    if ("result" in message) == ("error" in message):
        raise AssertionError("response must contain exactly one result or error")
    return message
```

Transport cases: fragmented input, two messages, malformed JSON, unknown method, oversized payload, unsupported version, wrong content type, premature EOF, session mismatch, timeout, and server death. Expected errors follow the repository's supported protocol contract; do not invent unsupported notification/batch behavior.

Tool/path cases: traversal, absolute/UNC/device paths, drive-relative paths, ADS, case variants, trailing dots/spaces, forbidden backup directories, junction escape, stale snapshot, duplicate task ID, dry-run no writes, failed batch rollback, unmanaged game rejection. A symlink-privilege skip is acceptable in a developer lane but a required privileged security lane must execute it.

**Gate:** Socket existence or tools/list alone cannot pass. Preserve request/response transcripts with sensitive values redacted; require a real tool result, exit code, and output-state verification. No external messages or paid model calls are involved.

## 16. Task 11 — Corpus inventory and reproducible feature selection

**When:** Before increasing E2E breadth. **Files:** `tools/e2e/New-EditorCorpusManifest.ps1`, `New-EditorWorkflowManifest.ps1`, `test-editor-corpus.ps1`, `test-editor-workflow-manifest.ps1`, workflow catalogue.

Record every input file's relative path, size, and SHA-256. Inventory all 14 IGI1 levels, task types, renderable instances, model variants, LODs, attachments, textures, graphs, weather, audio and scripts. Source files remain read-only; extraction/conversion writes only below a unique artifact root. Reject artifact/input overlap in both directions and reject junction escapes before conversion.

Selection must be reproducible from the inventory hash and stable sort keys. Preserve anonymous IDs such as `-1#907` as strings. Key instances by level + authored task identity, not by model ID, since multiple objects share a model.

Proposed stable selection helper in the workflow generator:

```powershell
function Select-StableRepresentatives($Objects, [string]$FeatureProperty) {
    @($Objects | Sort-Object level, taskId, modelId |
        Group-Object -Property $FeatureProperty |
        ForEach-Object { $_.Group | Select-Object -First 1 })
}
```

Use feature metadata such as mesh type or attachment presence, not guessed numeric model prefixes. The helper selects representatives only; exhaustive jobs retain all objects. Every exclusion records its reason and supporting inventory fact. Missing/unresolved assets are findings, not silently removed denominator entries.

Create three profiles: smoke (all-level startup plus representatives), nightly (all-level workflows and one representative per supported feature combination), exhaustive (all applicable objects/LODs/views). Pairwise sampling reduces combinations but must not remove named historical regressions or high-risk combinations such as transparent attachments indoors.

**Gate:** Same inventory produces identical selection; removing an applicable case makes the completeness test fail; unsupported variants and missing assets appear separately from passing objects. Reports show planned, applicable, selected, executed, and passed counts.

## 17. Task 12 — Visible E2E with behavioral oracles and restoration

**When:** After save integrity and fixture isolation. **Files:** `tools/e2e/editor-e2e.ps1`, `test-editor-e2e.ps1`, `test-editor-regression.ps1`, existing scenario JSON files.

Relevant methods: `Validate-Step`, `Validate-Scenario`, `Assert-ObserverPair`, `Wait-ForEditor`, `Focus-Editor`, `Invoke-Scenario`, `Invoke-RestorePaths`, `Close-Editor`. Reuse their existing actions and mutation gates.

Strengthen the observer contract: a screenshot plus `assert_process` proves liveness, not the action's result. Each editing scenario needs an action-specific postcondition: changed parsed property, graph node coordinates, persisted option, expected UI selection, or before/after state. Snapshot/restore are cleanup evidence and cannot count as the success oracle for the edit itself.

Add general step fields `oracleRole: "action-result"` and `dependsOnStepId`, validate the dependency exists and precedes the assertion, and require at least one action-result oracle for non-smoke scenarios. Do not inspect scenario names to choose behavior.

```powershell
# Add inside Validate-Scenario after existing step validation.
$ids = @{}
foreach ($step in $Scenario.steps) {
    if ($step.oracleRole -eq 'action-result') {
        if (-not $ids.ContainsKey([string]$step.dependsOnStepId)) {
            Fail 'Action-result oracle must reference an earlier step.'
        }
    }
    $ids[[string]$step.id] = $true
}
```

Use `Get-Property` for optional fields in the final implementation, consistent with the runner's existing validation style. Add contract fixtures for missing dependencies, future dependencies, duplicate step IDs, screenshot-only edited workflows, and liveness-only workflows.

Required workflows:

| Workflow | Exact application anchors | Action and postcondition |
|---|---|---|
| Property save | `CommitPropTextEdit`, `Input_OnKeyboard`, `SaveCurrentLevel` | Edit without Enter, configured SaveState key, reopen and compare property |
| Autosave off | `App::Frame`, `LaunchGame`, `Shutdown` | Disable, wait beyond interval, transition/quit; QSC/QVM hashes unchanged where policy forbids save |
| Explicit save | `SaveCurrentLevel` | Autosave disabled; explicit save still persists |
| Undo/redo | `PushUndoState`, `Undo`, `Redo` | Transform parent/child, undo, redo, new edit; compare positions and hierarchy |
| Task editing | `CreateNewTask`, `DeleteSelectedTask`, `CopySelectedTask`, `PasteTask`, `AssignTaskID` | Unique IDs, subtree preservation, deletion references, save/reopen |
| Level switch | `LoadLevel`, `SetGameLevel` | A→B→A resets selection/history/weather and reloads correct authored data |
| Graph/F11 | graph camera policy and keyboard handler | Nested AI selection, no-selection fallback, stale overlay, cycle safety |
| Font/UI | pause/menu metrics and `CommitPropTextEdit` | Font sizes, caret placement, wrapping, scroll and click alignment |
| Import | model resolver and archive writer | Import previously uncached asset; all texture loads resolve after refresh; reopen |
| Weather | `ResolveLevelWeather`, `ShouldDrawWeatherForFrame` | Active outdoors, sheltered indoors, correct behavior when building display is hidden |
| Animation | `App::Frame`, `UpdateAnimations`, `GetSkinnedReplacementObjectIndices` | Editor pose advances over fixed ticks and draws changed silhouette |
| Quit | `App::Shutdown`, `Renderer::Shutdown` | Normal exit completes, process gone, no crash, permitted save behavior |

Replace fixed 12-second readiness sleeps with bounded observable conditions where logging is enabled. For logging-disabled cases, use a structured readiness/state output independent of `Logger` or visible stable-frame plus selected-level evidence. Do not enable logging just to test that disabled logging writes nothing.

Before each mutating scenario snapshot every possible output, including both QSC and QVM, generated graphs/archives/config pairs. Restore in `finally` after owned processes stop; verify bytes and original absence for files that did not exist. Do not restore over a concurrently modified user file: ownership is guaranteed by a disposable copy or an exclusive run lock. Preserve failure evidence before cleanup.

**Run existing validation now when implementing:** `pwsh -NoProfile -File tools/e2e/test-editor-regression.ps1 -ValidateOnly`. **Live gate later:** existing runner with explicit game root, editor path, manifest and artifact root; `-AllowGameDataMutation` only for the isolated mutation lane.

**Gate:** Missing foreground, wrong Session ID, locked desktop, stale process, timeout, cleanup failure, or forced quit prevents PASS. A screenshot checkpoint alone never proves persistence.

## 18. Task 13 — Visual structure, appearance, and trustworthy baselines

**When:** After corpus selection; required for renderer, attachment, weather, font, and UI changes. **Files:** `source/visual_integrity.cpp/.h`, `tests/test_visual_integrity.cpp`, `DebugCommandManager::CaptureModel`, native runner, proposed image comparator and baseline metadata.

### 18.1 Strengthen the structural oracle first

Existing `VisualIntegrityInput` already supports parts, strict attachment IDs, target/scene depth, transform agreement, and temporal grouping. Extend the current tests before adding a second analyzer. Add malformed-size, empty-input, duplicate/invalid ID, NaN/Inf depth, occlusion, transparent-material, and stale-frame cases. Expected behavior: invalid evidence cannot PASS; a known missing exposed part FAILS; insufficient visibility is INCONCLUSIVE.

Example using helpers already present in `test_visual_integrity.cpp`:

```cpp
TEST(VisualIntegrityTest, MissingMaskDataCannotPass) {
    auto view = MakeView(std::vector<int>(16, 1));
    view.targetMask.pop_back();
    const auto result = igi::EvaluateVisualIntegrity(MakeInput({1}, {view}));
    EXPECT_NE(result.status, igi::VisualIntegrityStatus::kPass);
}
TEST(VisualIntegrityTest, StrictAttachmentCannotBeSatisfiedByRootPixels) {
    auto view = MakeView(std::vector<int>(16, 1));
    auto input = MakeInput({1, 2}, {view});
    input.strictPartIds = {2};
    const auto result = igi::EvaluateVisualIntegrity(input);
    EXPECT_NE(result.status, igi::VisualIntegrityStatus::kPass);
}
```

Pair the second case with a projected-but-unrendered strict part test requiring FAIL, using the existing missing-fragments setup. Missing projection alone may legitimately be inconclusive.

Prevent a common-mode oracle bug: if expected parts are inventoried only from geometry that successfully loaded, a dropped attachment can disappear from both expected and actual evidence. Cross-check the capture inventory with independently parsed authored MEF attachment/material metadata. A target-only render of the same incomplete scene is not sufficient independence.

### 18.2 Fix the view reporting contract without reducing capture coverage

Current C++ captures/evaluates all native views. PowerShell retains full evidence but derives a report subset using top target coverage. Introduce explicit fields `requestedViews`, `capturedViews`, `analyzedViews`, and `reportViews`; compare sets and record each separately. Keep full native analysis in Required mode.

Proposed report-view default:

```powershell
$defaultReportViews = @(
    'Ext_000','Ext_060','Ext_120','Ext_180','Ext_240','Ext_300',
    'Int_000','Int_090','Int_180','Int_270'
)
```

Add `-ReportViews` while preserving/deprecating `-ViewCount` explicitly. Stop silently choosing easier high-coverage views as the requested view set. Insufficient interior visibility must be visible in results; do not substitute an exterior frame. Tests should shuffle evidence order and lower interior coverage, then assert requested view identities remain intact. Preserve native 16-view evidence and distinguish that from the ten report stills.

### 18.3 Add appearance comparison

Structural masks cannot prove correct texture content, glyph shape, color, or blending. Use deterministic image comparisons on semantic regions, alongside the structural analyzer. Baseline keys include renderer profile, resolution, DPI, font asset hash, render mode, scenario, task/LOD/view, and configuration. Store a baseline's approved commit/hash and rationale.

Core comparator, proposed `tools/testing/Test-VisualImage.py` (a pure function; decode images to RGB arrays in the command adapter):

```python
def compare_rgb(expected, actual, channel_tolerance=8):
    if len(expected) != len(actual) or not expected:
        raise ValueError("image region sizes differ or are empty")
    changed = 0
    total_error = 0
    for left, right in zip(expected, actual):
        if len(left) != 3 or len(right) != 3:
            raise ValueError("RGB triples required")
        error = [abs(int(a) - int(b)) for a, b in zip(left, right)]
        total_error += sum(error)
        changed += max(error) > channel_tolerance
    return {
        "changedRatio": changed / len(expected),
        "meanAbsoluteError": total_error / (3 * len(expected)),
    }
```

Before comparing, enforce equal dimensions and matching profile; do not resize one screenshot to fit another. Start with this auditable metric and heatmap, then add SSIM only if observed noise justifies it. Suggested provisional UI gate: changed ratio ≤0.5% and mean absolute error ≤1 inside a fixed stable region; calibrate on repeated unchanged runs before enforcement. Structural rule failures cannot be averaged away by low image error.

UI cases: text clipping, caret hit location, glyph baseline, pause spinner label overlap, task tree indentation, selected-row highlight, property scroll boundaries, and 100/125/150% DPI. Scene cases: wrong texture, missing texture, alpha cutout versus blend, lightmap modes, fog transition, floor retained while moving camera, weather inside/outside shelter, LOD transition, skeletal pose.

Mask only truly nondeterministic content, declare mask rationale and maximum area, and disallow masks covering the feature under test. For weather/animation, freeze seed/time for appearance checks and use separate temporal tests for motion. Auto-generated candidate baselines never auto-approve themselves.

**Gate:** Seeded faults—missing attachment, wrong material, invisible target, corrupt depth, clipped text, stale screenshot, and excessive mask—are detected. Failure bundle contains expected/actual/diff, masks, camera/projection, part inventory, frame identity, and thresholds.

## 19. Task 14 — Test the test runners and portable evidence

**When:** Alongside Tasks 11–13; every harness change. **Files:** existing `test-smart-*`, `Test-SmartCaptureArtifact.ps1`, `Test-SmartLivePilotEvidence.ps1`, `test-editor-e2e.ps1`, native runner.

Construct tiny evidence bundles in a unique temp directory; change one field/file per negative case. Cases: missing PNG, zero-byte depth, wrong dimensions, hash mismatch, duplicate view, missing required view, wrong task ID, anonymous ID altered, wrong binary hash, old run ID, FAIL child hidden by PASS batch, empty selection, ReportOnly in release mode, cleanup failure.

Use a simple generic status aggregation contract:

```powershell
function Get-RequiredLaneStatus([object[]]$Cases) {
    if ($Cases.Count -eq 0) { return 'BLOCKED' }
    if (@($Cases | Where-Object { $_.status -eq 'FAIL' }).Count) { return 'FAIL' }
    if (@($Cases | Where-Object { $_.status -ne 'PASS' }).Count) { return 'BLOCKED' }
    return 'PASS'
}
```

Only pass required cases to this function; optional diagnostics remain separately listed. Test both `@()` and a mix of PASS/SKIP so an empty or skipped lane cannot pass.

Validate that every file referenced from the portable JSON exists in the bundle and matches its manifest hash. Reject `..`, absolute paths, and reparse-point escape. Copy the bundle to another temp root and verify it there, without the installed screenshot directory. Log parsing must be scoped to this process/run and file offset; log truncation/rotation needs an explicit new segment.

**Gate:** Negative harness tests fail for the intended reason and positive bundles pass after relocation. SchemaVersion changes require compatibility tests for supported old artifacts or an explicit unsupported-version result.

## 20. Task 15 — Sanitizers, malformed-input fuzzing, and static checks

**When:** After byte seams and deterministic fixtures. **Files:** proposed `tests/fuzz/fuzz_fnt.cpp`, `tests/fuzz/fuzz_qsc.cpp`, CMake optional targets, parser files implicated by findings.

MSVC supports AddressSanitizer on Windows x86/x64; use an isolated instrumented configuration. Do not assume MSVC ThreadSanitizer or UndefinedBehaviorSanitizer is available. Disable incompatible incremental linking/runtime-check settings in that lane and prove an intentionally faulty fixture executable is detected before trusting the setup. [Microsoft AddressSanitizer documentation](https://learn.microsoft.com/en-us/cpp/sanitizers/asan?view=msvc-170).

```cmake
option(IGI_TEST_ASAN "Instrument test targets with AddressSanitizer" OFF)
if(IGI_TEST_ASAN AND MSVC)
    target_compile_options(igi_tests PRIVATE /fsanitize=address /Zi)
    target_link_options(igi_tests PRIVATE /INCREMENTAL:NO)
endif()
```

This fragment instruments sources compiled by that target. If sources later move into libraries, instrument those libraries too; linking only an instrumented test main does not cover uninstrumented parser code. Start with Release + symbols to avoid `/RTC` conflicts. Confirm actual compiler command lines and retain sanitizer logs/dumps.

After Task 5, the FNT fuzz entrypoint is small:

```cpp
#include "renderer/fnt_parser.h"
#include <cstddef>
#include <cstdint>
#include <span>
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > 1024 * 1024) return 0;
    const auto font = FNT_ParseBytes(std::span<const uint8_t>(data, size));
    if (font.valid) {
        const size_t expected = static_cast<size_t>(font.texWidth) *
                                static_cast<size_t>(font.texHeight) * 4;
        if (font.rgba.size() != expected) __builtin_trap();
    }
    return 0;
}
```

Compile this target with the installed LLVM fuzzing toolchain, not MSVC's ordinary compiler; check Windows runtime availability with a smoke target first. Bound input size, execution time, and process memory, seed from synthetic fixtures, retain crashes, and replay every retained crash in deterministic Google Tests. LLVM documents the entrypoint and Windows toolchain availability. [LLVM libFuzzer documentation](https://llvm.org/docs/LibFuzzer.html).

Fuzz QSC lexer/parser and MCP JSON directly through byte/string interfaces; add QVM/RES/MEF byte seams only as needed. Parser loops must demonstrate forward progress, overflow-safe range checking, and controlled resource use. Do not catch every exception and declare fuzz success; allocation exhaustion must be classified separately from invalid input.

Static checks: changed-file compiler warnings, narrowing/overflow-sensitive parser code, resource ownership, and suspicious iterator invalidation. Introduce warning ratchets for touched modules, not a repository-wide warning-as-error switch that initially hides the relevant failures behind unrelated backlog.

**Gate:** Instrumentation self-test proves detection; seeded malformed corpus has no crash/hang; every finding has a minimized reproducible case. Fuzzer elapsed time without retained logs and exit status is not a pass.

## 21. Task 16 — Performance, leaks, load, and soak regression

**When:** After functional lanes stabilize; nightly representative and weekly extended. **Files:** proposed `tools/testing/Invoke-EditorSoak.ps1`, E2E manifests, existing `App::LoadLevel`, `Shutdown`, model/cache and audio code only when measurements identify defects.

Measure level load ready-time, save duration, parser throughput, frame-time percentiles, working set/private bytes, handles, GDI objects, and thread count. Record hardware, power mode, binary/config/corpus hashes, viewport, and warm/cold cache state. Use monotonic timers and report p50/p95/p99; FPS averages alone hide stalls.

Resource sample in the runner:

```powershell
function Get-OwnedProcessSample([int]$ProcessId) {
    $process = Get-Process -Id $ProcessId -ErrorAction Stop
    [pscustomobject]@{
        utc = [DateTime]::UtcNow.ToString('o')
        privateBytes = $process.PrivateMemorySize64
        workingSetBytes = $process.WorkingSet64
        handles = $process.HandleCount
        threads = $process.Threads.Count
    }
}
```

Run load A→B→A, open/close property panel, font switch, graph overlay, animation toggle, import from working copy, save/reopen, pause/resume, and normal quit. Sample after a fixed settling point. Distinguish one-time cache growth from monotonic per-cycle leakage; compare stabilized last-half medians and slope, not just first/last working set.

Initial policy: alert on >10% median regression with a practical floor of 100 ms for load/save, reproduce three paired runs, and only enforce after noise calibration. Memory/handle budgets need measured baseline allowances; do not assert an arbitrary universal memory ceiling. Any crash, hang, unbounded growth, or retained owned process fails regardless of performance thresholds.

**Gate:** 30 cycles nightly and 200 weekly complete with cleanup evidence. Restore timers/configuration and owned processes. Run serially on a dedicated interactive machine; background workload invalidates performance comparison.

## 22. Task 17 — Coverage and mutation-based test quality

**When:** After stable targets; weekly and focused on high-risk PRs. **Files:** lane runner, report schema, `tests/regressions.json`, selected policy/parser/visual tests.

Measure source coverage with a Windows-compatible collector verified against a tiny fixture program; record collector version and compiler settings. Count production source only, excluding generated/vendor/test code. Start with observed baselines; propose 90% changed-line coverage for pure policy/parser code and no decrease in existing branch coverage, with reviewed exceptions for unreachable platform paths. This is a ratchet, not a claim of current coverage.

Validate assertion strength using controlled mutations in disposable source copies, never edits to the user's active worktree. Initial mutation catalogue:

| Mutation | Required detector |
|---|---|
| Remove `!paused` from autosave eligibility | Truth table plus disabled-write workflow |
| Do not clear redo on new edit | History branching test |
| Accept chunk skip smaller than header | Malformed parser test/fuzz corpus |
| Skip second-file validation | Pair rollback test |
| Treat INCONCLUSIVE as PASS | Required-lane aggregation test |
| Remove strict attachment check | Visual missing-attachment fixture |
| Ignore configured save binding | Commit-without-Enter GUI scenario |

Report killed, survived, invalid-build, timed-out, and equivalent mutations separately. Invalid-build is not killed. Require all hand-selected high-risk mutations to be killed before expanding to an automated mutation framework. Avoid a global mutation-score target until equivalent-mutant noise is understood.

**Gate:** Each historical bug maps to a test that fails with its causal defect reintroduced and passes with the fix. Coverage percentages cannot waive missing behavior or a failing live gate.

## 23. Task 18 — Package and deployment verification

**When:** Every release candidate, after all required lanes. **Files:** CMake post-build copy rules, `assets/editor/`, proposed `tools/testing/Test-ReleaseLayout.ps1`, `docs/TESTS.md`.

Build a unique package staging directory. Verify x86 PE architecture for editor/runtime DLLs and converter compatibility, required DLLs, shaders, font assets, QSC/QVM config pairs, relative directory layout, and converter hash/version. Run from a directory without repository source or build caches; use a private disposable game copy for asset-dependent checks.

Core required-file check:

```powershell
function Assert-PackageFiles([string]$Root, [string[]]$RelativePaths) {
    foreach ($relative in $RelativePaths) {
        $file = Join-Path $Root $relative
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
            throw "Package file missing: $relative"
        }
        if ((Get-Item -LiteralPath $file).Length -eq 0) {
            throw "Package file empty: $relative"
        }
    }
}
```

Populate the relative path list from the existing post-build packaging contract, not memory of an older package. Add a test that removes one DLL/font/config source at a time from a fixture package and checks an actionable failure. Compile the packaged config QSC and validate its sibling QVM equivalence; QSC is the authoring source. Verify no developer absolute paths are required at startup.

Packaging smoke: startup, level load, visible pause/font, explicit save/reopen in disposable data, graceful exit, and logging-disabled zero new log bytes. Hash the tested package. Deployment success requires the deployed files to match that package hash and the deployed smoke gate to pass; local tests alone do not establish deployment completion.

**Gate:** A release report names the exact package hash and required lane results. No publishing/installation is authorized by this planning document itself.

## 24. Task 19 — CI scheduling, required checks, and flake policy

**When:** After Tasks 1–4 and harness status tests; add more lanes as they become reliable. **Files:** new `.github/workflows/tests.yml`, lane runner, capability manifest.

Hosted Windows: clean dependency provisioning, Win32 build, registration audit, synthetic unit/component/CLI tests, manifest validation. Private runner: installed corpus. Dedicated unlocked desktop runner: serial visible E2E/visual. Never run untrusted fork code on a private runner with game files, credentials, or an interactive user session.

Use a single tested local command per lane; CI should invoke it and upload artifacts even on failure. Pin external actions and dependency artifacts to reviewed immutable revisions when implementing the workflow; do not paste an unverified action digest into this plan. Toolchain/dependency caches must be keyed by architecture, compiler, configuration, and dependency lock.

Required scheduling:

| Changed paths | Minimum additional required lanes |
|---|---|
| `source/level/qsc*`, `qvm*`, parser/writer sources | Format unit, round-trip, corpus variants, sanitizer smoke |
| `source/app_editor.cpp`, `app_level.cpp`, serialization | Save rollback, undo, GUI save/reopen, corpus integrity |
| `source/app_input*`, pause/font/layout | Policy, input routing, DPI visual, keyboard E2E |
| `source/renderer/`, capture/visual analyzer | Structural negatives, representative native capture, appearance |
| `source/runtime/`, AI/player | Deterministic simulation, runtime integration, relevant live interaction |
| `source/mcp/` | Tool/unit, transaction, transport process, path containment |
| `tools/e2e/` | Harness contracts, evidence negatives, representative live pilot |
| CMake/assets/dependency | Clean build, registration, package smoke |

Unknown source changes select the broader lane set. Scheduled nightly checks cover all subsystems even if path filters did not select them. Changes to tests/gates cannot approve themselves through path-filter omissions.

Flake policy: preserve the first failure; rerun once for diagnosis; report `failedThenPassed` rather than green. Quarantine needs a named owner, bug, expiry, and separate visible execution; a required historical regression cannot be waived by quarantine. Measure flake rate over runs with the same environment/profile. Do not retry deterministic assertion failures until they happen to pass.

**Gate:** A deliberately broken test makes CI red; missing desktop/corpus causes BLOCKED; every required job uploads its report; release aggregation rejects absent jobs. Never label a prepare-only or manifest-only job as live E2E.

## 25. Task 20 — Regression ledger, documentation, and manual acceptance

**When:** Every bug fix; complete before declaring the testing program operational. **Files:** new `tests/regressions.json`, `docs/bug-fixes/`, `docs/TESTS.md`, relevant scenario manifests.

Proposed ledger entry structure:

```json
{
  "id": "IGIED-SAVE-ACTIVE-EDIT",
  "symptom": "Configured save must persist the currently edited property",
  "sourceAnchors": ["App::CommitPropTextEdit", "App::Input_OnKeyboard", "App::SaveCurrentLevel"],
  "unitOrComponentCases": [],
  "e2eCases": ["save-active-property-reopen"],
  "requiredCapabilities": ["interactive-session", "disposable-corpus"],
  "status": "planned",
  "fixCommit": null,
  "evidenceRunIds": [],
  "mem0Status": "not-a-new-fix"
}
```

Do not invent test names as existing evidence: ledger entries remain planned until those cases register and run. Validate every referenced case against current test/scenario inventories. When a new bug is fixed, update the ID, cause, changed method, reproducer, fix commit, passing/failing evidence, and actual mem0 write result. Historical ad-hoc notes are useful leads but must not override current code; indoor weather policy has already changed since earlier notes.

Manual release checklist: keyboard-only task navigation; discoverable save errors; readable high-DPI fonts; focus recovery after Alt-Tab; drag/drop or import cancellation where supported; Unicode/space-containing project paths; minimized/restored window; multiple monitor placement; long editing session; recovery after failed save; normal quit. Mark unsupported accessibility features explicitly; a pixel-perfect image is not proof of keyboard usability.

Update documentation to link generated current reports, explain lane capabilities, list how to reproduce each gate, and distinguish old snapshots from current results. Fix documentation aliases only after confirming their replacement sections. Keep expensive all-level evidence private if it contains retail assets.

**Gate:** Every high-severity fixed bug has a reproducer, relevant automated test, live case when needed, and persistent summary status. No documentation claims a broader gate than the evidence supports.

## 26. Delivery sequence and realistic milestones

Work in the following dependency order; effort estimates assume one engineer familiar with the project and exclude repair of newly discovered production defects.

| Milestone | Tasks | Estimated effort | Reviewable outcome |
|---|---|---|---|
| A — Trust the baseline | 1, 2, 3 | 3–5 days | Reproducible build, no silent registrations, isolated fixtures |
| B — Fast feedback | 4, initial 5, initial 6 | 4–7 days | Small unit targets and meaningful synthetic format checks |
| C — Protect state/runtime | 7, 8, 9, 10 | 6–12 days | Save rollback, deterministic behavior, safe lifecycle, real protocol tests |
| D — Prove user workflows | 11, 12, 14 | 5–8 days | Explicit corpus coverage and reliable visible E2E |
| E — Prove rendering | 13 plus expanded 14 | 5–10 days | Structural/appearance negatives and approved baseline process |
| F — Long-term hardening | 15, 16, 17 | 4–8 days | Sanitizer/fuzz, soak, and assertion-strength evidence |
| G — Release operations | 18, 19, 20 | 3–5 days | Required CI gates, package proof, regression ledger |

CI for Milestone A/B can begin before all later work finishes; add each subsequent lane after its failure semantics are tested. Do not hold fast synthetic testing hostage to desktop availability. Do not promote an incomplete desktop lane to required release PASS.

Recommended first five implementation commits:

1. Build provenance and non-mutating dependency configuration, with failure propagation tests.
2. Test source ownership audit and prefixed discovery; explicitly record debug lifecycle exclusion.
3. Unique temp fixtures and Python working-directory isolation.
4. Extract policy/clock targets and preserve existing test ownership.
5. Repair/register omitted tests and add the first synthetic FNT malformed-input matrix.

Each milestone's exit report must state: implemented files, executed commands, expected/observed case counts, failures, skips, blocked capabilities, artifact paths, and outstanding work. There is no overall completion claim until all required milestone gates have evidence.

## 27. Implementation review checklist

- [ ] Every proposed test exercises production behavior or validates a real harness contract; it does not merely duplicate a helper's implementation.
- [ ] Every code change has a failing reproducer or characterization reason.
- [ ] New test sources are owned by targets; exclusions are explicit and temporary.
- [ ] Environment and file mutations are restored; data fixtures are not shared mutable retail files.
- [ ] Save tests check data after reopening and include failure outcomes.
- [ ] GUI tests use real input; their action-result oracle is stronger than process health.
- [ ] Visual tests cover both missing structure and incorrect appearance, with negative controls.
- [ ] Expected geometry is not derived solely from the renderer output it judges.
- [ ] Capture selection cannot silently replace a difficult requested view with an easier one.
- [ ] Empty, skipped, inconclusive, stale, or missing evidence cannot become required PASS.
- [ ] Crash-safe claims distinguish individual replacement from multi-file recovery.
- [ ] x86 build and package results are separate from any x64 parser experiments.
- [ ] Baseline updates are reviewed with images/metrics and immutable provenance.
- [ ] Account usage stopping follows a real user/platform signal, not an invented estimate.

## 28. Reference notes

Repository references are listed at the relevant tasks. External tooling details were checked against primary sources while preparing this plan:

- CMake supports prefixed Google Test discovery and per-test properties; respect the repository's minimum version when adding newer discovery features. [CMake GoogleTest module](https://cmake.org/cmake/help/latest/module/GoogleTest.html).
- The small standalone camera target is already a local example of the extraction approach; extending that pattern avoids immediate broad dependency restructuring.
- The currently inspected native capture keeps all analyzer evidence, even though the reporting subset is independently selected. Improve provenance and selection semantics while preserving that existing full-evidence behavior.

Document validation for this planning change covers file/symbol references, Markdown/code-fence structure, placeholder review, and diff hygiene. C++/PowerShell/Python examples are implementation guidance and have not been compiled or executed as new code in this planning-only task.
