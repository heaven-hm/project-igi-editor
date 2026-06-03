# IGI Editor — Test Suite

The test binary `igi_tests.exe` is a standalone **Google Test** runner. It must be co-located with `igi1ed.exe` and the game's `missions/` directory (e.g. `D:\IGI1\`). No source tree or build directory is needed at runtime.

---

## Quick Start

```powershell
# Run all tests (uses all 14 levels for verify-level — takes ~4 minutes)
.\igi_tests.exe

# Fast run — level 1 only for the slow verify-level integration tests (~18 seconds)
$env:IGI_TEST_LEVEL="1"; .\igi_tests.exe

# Skip the slow verify-level tests entirely (~2 seconds)
.\igi_tests.exe --gtest_filter="-AllLevels/VerifyLevelIntegration*"
```

---

## Controlling Which Levels Are Tested

The `IGI_TEST_LEVEL` environment variable restricts the **verify-level integration test** to specific levels. The QVM round-trip tests always run all 14 levels (they are fast).

| Command | Levels tested |
| --- | --- |
| `.\igi_tests.exe` | All 14 (default) |
| `$env:IGI_TEST_LEVEL="5"; .\igi_tests.exe` | Level 5 only |
| `$env:IGI_TEST_LEVEL="10"; .\igi_tests.exe` | Level 10 only |
| `$env:IGI_TEST_LEVEL="1,3,7"; .\igi_tests.exe` | Levels 1, 3 and 7 |

---

## Filtering by Test Suite

Use `--gtest_filter` to run a subset. The pattern supports `*` wildcards and `:` to combine multiple patterns.

```powershell
# List every registered test name
.\igi_tests.exe --gtest_list_tests

# Run only parser tests
.\igi_tests.exe --gtest_filter="DatParserTest*:ResParserTest*:TexParserTest*"

# Run a single suite
.\igi_tests.exe --gtest_filter="QscLexerTest*"

# Run verify-level for a specific level via filter (no env var needed for QVM tests)
$env:IGI_TEST_LEVEL="5"; .\igi_tests.exe --gtest_filter="AllLevels/VerifyLevelIntegration*"

# Run QVM round-trip for level 10 only
.\igi_tests.exe --gtest_filter="AllLevels/QvmGameRoundTripTest.ObjectsQvmDecompilesAndReparses/Level10"

# Run both verify-level and QVM round-trip for level 3
$env:IGI_TEST_LEVEL="3"; .\igi_tests.exe --gtest_filter="AllLevels/VerifyLevelIntegration*:AllLevels/QvmGameRoundTripTest.ObjectsQvmDecompilesAndReparses/Level3"
```

---

## Test Suites

### Unit Tests (no game files required)

| Suite | Tests | What it covers |
| --- | :---: | --- |
| `QscLexerTest` | 52 | All token types, keywords, operators, escape sequences, qualified identifiers, line/block comments, error recovery with position reporting |
| `QscParserTest` | 42 | AST node types, operator precedence, control flow (`if`/`else`/`while`), assignment associativity, parenthesised expressions, call/arg counters, error cases |
| `QvmRoundTripTest` | 19 | Synthetic compile→write→parse→decompile cycles; identifier and string pool integrity; structural re-parse of decompiled output; fixture-based round-trip with `level01_simple.qsc` |
| `ConfigTest` | 10 | Config defaults, field value ranges, singleton behaviour, multi-init safety, keybinding load |
| `UtilsTest` | 35 | `Trim`, `Split`, `TryParseInt/Float/Double`, `ToString` — all edge cases |
| `PosMatchTest` | 4 | `PosMatch()` exact-match and per-axis mismatch logic |
| `OriMatchTest` | 4 | `OriMatch()` epsilon tolerance (passes just below, fails at and above threshold) |
| `ParseLogTest` | 9 | `ParseLog()`: model ID/type extraction, position/orientation flags, tex/mesh flags, last-occurrence selection, level marker filtering; fixture: `verify_log_l1.txt` |
| `CrossRefTest` | 8 | `CrossRef()`: found/missing/pos-mismatch/ori-mismatch/tex-mismatch/mesh-mismatch categorisation; rail object matching |
| `ParseQscObjectsTest` | 2 | `ParseQscObjects()` with `level01_simple.qsc` fixture and missing-file guard |

### Parser Integration Tests (require game files in same directory as exe)

| Suite | Tests | Game file used |
| --- | :---: | --- |
| `DatParserTest` | 6 | `missions/location0/level1/level1.dat` — model count, names, textures, JSON output shape |
| `GraphParserTest` | 5 | `missions/location0/level1/graphs/graph1.dat` — node count, IDs, coordinates, material range |
| `ResParserTest` | 6 | `missions/location0/level1/models/level1.res` — entry count, names, data presence, callback |
| `TexParserTest` | 5 | `missions/location0/level1/textures/level1.res` — extracts first `.tex`, version, image dimensions, pixel data size |
| `MtpParserTest` | 2 | `common/common.mtp` — model/texture count |
| `FntParserTest` | 5 | `computer/computer/font1.fnt` — glyph count, atlas dimensions, pixel data size |

### Multi-Level Tests (require game files for levels 1–14)

| Suite | Tests | What it covers |
| --- | :---: | --- |
| `QvmGameRoundTripTest` | 14 | Parses real `objects.qvm` for each level, decompiles to QSC, asserts the output re-lexes and re-parses cleanly. Skips gracefully if a level file is missing. |
| `VerifyLevelIntegration` | 1–14 | Launches `igi1ed.exe --verify-level N`, waits up to 35 s (15 s inner editor timeout + overhead), asserts exit code 0. Controlled by `IGI_TEST_LEVEL`. |

---

## Test Counts

| Scope | Tests |
| --- | :---: |
| Unit tests only | 173 |
| Parser integration tests | 29 |
| QVM game round-trips (all 14 levels) | 14 |
| Verify-level (all 14 levels) | 14 |
| **Total — all levels** | **230** |
| **Total — `IGI_TEST_LEVEL=1`** | **229** |

---

## Fixtures

Fixture files are copied next to `igi_tests.exe` in a `fixtures/` subdirectory by the CMake post-build step. They are synthetic and committed to the repository.

| File | Used by | Purpose |
| --- | --- | --- |
| `fixtures/level01_simple.qsc` | `QvmRoundTripTest`, `ParseQscObjectsTest` | Minimal two-line QSC with a `SplineObjWaypoint` task call and model ID `322_01_1` |
| `fixtures/verify_log_l1.txt` | `ParseLogTest` | Synthetic editor log covering level 1 and level 2 sections; tests last-occurrence selection and cross-level filtering |

---

## Build

```powershell
# Configure (must be Win32 — game and editor are 32-bit)
cmake -B build -G "Visual Studio 17 2022" -A Win32

# Build test binary only
cmake --build build --config Release --target igi_tests -j 1

# Deploy to game directory
Copy-Item bin\Release\igi_tests.exe  D:\IGI1\igi_tests.exe  -Force
Copy-Item bin\Release\fixtures\*     D:\IGI1\fixtures\      -Force
```

> **Note:** Always deploy the `fixtures\` directory alongside `igi_tests.exe`. Without it, all fixture-dependent tests fail.
