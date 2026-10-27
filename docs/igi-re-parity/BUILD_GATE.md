# Build & test gate — MinGW cross-build + host-native gtest lane

Owner: `build-gate` (shared task `task-1`).
Branch: `fix/igi-re-known-issues` (based on `origin/fix/editor-performance`, v3.6.11-pre).
Host: macOS (Homebrew clang 23, Homebrew mingw-w64 14 / GCC 16.2, CMake 3.26.3, Ninja).
There is **no Windows runtime and no wine on this host**, so nothing below *executes* a
Windows binary. The gate proves: (a) every translation unit of the editor and the test
aggregate compiles with MinGW-w64, (b) the editor and the test aggregate link, and
(c) the pure-logic test suites behave correctly when compiled and run natively.

---

## 1. Commands

```bash
# Full Windows cross-build gate (both trees). Exit 0 = green.
bash scripts/build-windows.sh

# Host-native pure-logic gtest lane (builds AND runs). Exit 0 = green.
bash scripts/run-host-tests.sh
```

Useful variants:

```bash
bash scripts/build-windows.sh --arch x64        # x86_64 full link gate only
bash scripts/build-windows.sh --arch i686       # i686 compile-only gate only
bash scripts/build-windows.sh --keep-going      # ninja -k 0: collect every error
bash scripts/build-windows.sh --fresh-pch       # drop stale GCC PCHs first (see §6)
bash scripts/build-windows.sh --clean           # wipe both build trees, reconfigure

bash scripts/run-host-tests.sh --list           # list the discovered suites
bash scripts/run-host-tests.sh --show-output    # full gtest output for every suite
IGI_HOST_TESTS="tests/test_mef_dual_set.cpp" bash scripts/run-host-tests.sh
IGI_GAME_PATH=/path/to/IGI bash scripts/run-host-tests.sh
```

Logs:

| file | contents |
| --- | --- |
| `build-mingw/build-windows.log` | combined transcript of both cross-build gates |
| `build-mingw/build-windows-i686.log` | raw i686 compile-only gate |
| `build-mingw-x64/build-windows.log` | raw x86_64 link gate |
| `build-mingw/host-tests.log` | host-native lane transcript |

---

## 2. Two build trees, and why

`build-windows.sh` runs two independent gates.

### Gate 1 — x86_64 full link gate (`build-mingw-x64/`)

`x86_64-w64-mingw32` configure + build of `igi-editor` and `igi_tests`. **This is the only
architecture this repository can actually link for.** The only GL import libraries that
are committed are MSVC x64 import libraries:

```
third_party/GL/lib/x64/freeglut.lib     (nm shows: __imp_glutInit, __imp_glutMainLoop, ...)
third_party/GL/lib/x64/glew32.lib       (nm shows: __imp___glewGenBuffers, ...)
```

MinGW-w64 consumes those COFF import libraries directly (verified with a standalone probe:
`x86_64-w64-mingw32-g++ t.cpp -L third_party/GL/lib/x64 -lfreeglut -lglew32 -lopengl32`
→ exit 0, `PE32+ executable (console) x86-64`). x86_64 also matches the project's real
MSVC x64/LLP64 target, so it produces fewer false portability diagnostics than a 32-bit
build would.

### Gate 2 — i686 compile-only gate (`build-mingw/`)

`i686-w64-mingw32` configure, then **compile every object target** of `igi-editor`,
`igi_tests` and `igi_mcp` — no link. `build-mingw/` stays 32-bit because the shipping
target is Win32 and because `tools/igi-re-parity/tu-check.py` replays the compile commands
from `build-mingw/compile_commands.json` for every teammate.

Why no i686 link: CMake selects `third_party/GL/lib/x86` for a 32-bit build, and

* `third_party/GL/lib/x86/` **does not exist** (only `lib/x64/` is committed),
* `assets/dlls/x86/` does not exist either (no `assets/dlls/` at all in this branch),
* the MinGW-w64 sysroot ships `libopengl32.a`, `libglu32.a`, `libglut.a`, `libgdiplus.a`,
  `libpsapi.a`, `libwbemuuid.a`, `libole32.a`, `libwinmm.a` — but **no `libfreeglut.a`
  and no `libglew32.a`**.

A synthetic i686 import library *can* be produced from the committed x64 export tables with
`dlltool`, but it is a fake gate (it resolves whatever the objects happen to reference) and
it was explicitly rejected by the Lead. Nothing in this repository depends on it.

---

## 3. MinGW / GCC portability edits

Each edit is minimal and behaviour-identical on MSVC. Files marked **(owned by another
teammate)** were touched only for the portability hunk and never reformatted.

| # | path:line | MinGW/GCC error | fix |
| --- | --- | --- | --- |
| 1 | `source/app_editor.cpp:1354` | `no known conversion from 'BOOL (*)(HWND, LPARAM)' … to 'WNDENUMPROC' {aka 'int (__attribute__((stdcall)) *)(HWND__*, long int)'}` — GCC rejects a `__cdecl` lambda where `__stdcall` is required | replaced the capture-less `EnumWindows` lambda with `static BOOL CALLBACK CloseCtx::EnumProc(HWND, LPARAM)` inside the existing local `CloseCtx` struct; `EnumWindows(&CloseCtx::EnumProc, …)`. No behaviour change (the lambda captured nothing) |
| 2 | `source/common.cpp:56` | `'aligned_alloc' was not declared in this scope; did you mean '_aligned_malloc'?` — MinGW-w64 has no C11 `aligned_alloc` | split the GCC branch: `#if defined(__GNUC__) && defined(_WIN32)` → `_aligned_malloc` / `_aligned_free`; the plain-GCC branch still uses `aligned_alloc` / `free`. MSVC branch untouched |
| 3 | `source/mcp/mcp_tools_runtime.cpp:377` | `no matching function for call to 'Gdiplus::Bitmap::Save(const wchar_t*, CLSID*)'` — MSVC's `gdiplus.h` defaults the 3rd parameter to `NULL`, MinGW's `gdiplusheaders.h` declares no default | pass `nullptr` explicitly as the `EncoderParameters*` |
| 4 | `source/mcp/mcp_tools_audio.cpp:53` | `'isfinite' is not a member of 'std'` / `'trunc' is not a member of 'std'` (pulled in transitively by MSVC) | added the missing `#include <cmath>` |
| 5 | `CMakeLists.txt` (before `include(GoogleTest)`) | build fails with `bin/editor_policy_tests.exe: cannot execute binary file` / `Result: 126`, `CMake Error at GoogleTestAddTests.cmake:112` — `gtest_discover_tests` *runs* each freshly linked Windows binary during the build | additive guard: `if(CMAKE_CROSSCOMPILING AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.18) set(CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE PRE_TEST) endif()` — discovery moves to `ctest` time |
| 6 | `CMakeLists.txt` (`igi-editor` link flags) | `x86_64-w64-mingw32-ld: cannot find /MANIFEST:EMBED: Invalid argument` (same for `/MANIFESTINPUT:`, `/MANIFESTUAC:`, `/SUBSYSTEM:CONSOLE`) — MSVC-only switches were passed to MinGW `ld` | wrapped the manifest `LINK_FLAGS` and `LINK_FLAGS_DEBUG/RELEASE` in `if (MSVC)`. MinGW's default subsystem is already console |
| 7 | `CMakeLists.txt` (`igi-editor` POST_BUILD) | `cmake -E copy_if_different` would fail on `assets/dlls/x64/freeglut.dll`, which is not tracked in this repository | copy only the runtime files that exist (`version`, plus each DLL if present). Behaviour on a machine that *does* have `assets/dlls/<arch>` is unchanged |

Portability edits 1–4 are in files outside the "owned by another teammate" list
(`source/renderer/renderer_objects*.cpp`, `source/renderer/mef_native.cpp`,
`source/level/level_objects*.cpp`, `source/renderer/spline_geometry.cpp`,
`source/renderer/renderer_splines.cpp`, `source/renderer/renderer_objects_atta.cpp`).
**No file owned by another teammate was modified by this task.**

---

## 4. Host-native gtest lane (`scripts/run-host-tests.sh`)

`tools/host-tests/host_test_runner.py` does the work:

1. reads every `tests/*.cpp` registered in `CMakeLists.txt` (so a newly registered suite
   is picked up automatically) and the source pool from
   `target_sources(igi_tests PRIVATE …)`;
2. trial-compiles every candidate with the host compiler and keeps only the translation
   units that do not need `windows.h`;
3. archives the portable engine sources into `libigi_host_sources.a` so the host linker
   pulls in exactly the objects each suite needs (this also resolves the
   `logger.cpp` vs `mcp_logger_stub.cpp` duplicate-definition pair without special cases);
4. links each surviving suite against that archive + a locally built `libgtest_host.a`
   (from `build-mingw/_deps/googletest-src`), preferring a strict link and falling back to
   `-Wl,-undefined,dynamic_lookup`;
5. runs each suite from the repository root and aggregates the real gtest counts.

Suites whose binary links lazily and then aborts in `dyld` with `missing symbol called` are
reported as "not host-portable (runtime unresolved symbol)", together with the first symbol
`ld` reported for that suite. They are an environment gap, not a regression, and the symbol
name says exactly what is missing.

### Object cache

The cache is keyed on the source file **and** on the compiler flags, **and** it is
invalidated whenever any header under `source/` or `tests/` (or `CMakeLists.txt`) is newer
than the cached object. The header term is a correctness requirement, not a convenience:
adding a member to `LevelObject` changes the struct layout, so a stale object compiled
against the old layout is an ODR violation that can corrupt the test binary rather than
merely produce a wrong result. `IGI_HOST_NO_CACHE=1` disables the cache entirely and is
still the safe choice while headers are being edited concurrently.

Environment overrides: `IGI_HOST_CXX`, `IGI_HOST_JOBS`, `IGI_HOST_TEST_TIMEOUT`,
`IGI_GTEST_SRC`, `IGI_HOST_TESTS` (explicit suite list, overrides discovery),
`IGI_HOST_TESTS_EXTRA`, `IGI_HOST_TESTS_SKIP`, `IGI_HOST_NO_CACHE`.

### Game corpus

Corpus-backed suites read the install root from the environment. `run-host-tests.sh`
exports `IGI_GAME_PATH` (default `/Users/haseeb-mir/Downloads/IGI`) and the runner also
defaults `IGI_VANILLA_ROOT` and `IGI_WEATHER_CORPUS` to it, so parser/verifier suites run
against the real retail data instead of skipping. `--corpus DIR` overrides it.

Additional fixture variables used by individual suites (not set by default):

```bash
export IGI_MEF_WATERTOWER=/Users/haseeb-mir/Downloads/IGI/MISSIONS/location0/LEVEL1/MODELS/models/435_01_1.mef
export IGI_MEF_SNIPER=/path/to/extracted/001_02_1.mef   # lives inside LEVEL7.RES / LEVEL8.RES
```

---

## 5. Baseline (measured)

> **Read this before quoting the numbers.** The working tree was being edited by six
> teammates throughout this task, and the numbers move every time a suite is registered or
> a header changes. Each figure below is tagged with the run it came from. Re-run the two
> commands to reproduce; a green cross-build gate plus a host lane with 0 failures is the
> acceptance condition, not a particular test count.

### 5.1 Cross-build gate — `bash scripts/build-windows.sh`

Run `2026-10-25T18:05Z`, full tree, both gates:

```
==> GATE 1 (x64 link) OK (exit 0)
==> GATE 2 (i686 compile) OK (exit 0)
   x64  link gate      : exit 0
   i686 compile gate   : exit 0
==> BUILD GATE OK (exit 0)
```

* GATE 1 links both executables: `bin/Release/igi1ed.exe` (5,684,542 B) and
  `bin/igi_tests.exe` (12,871,291 B), both `PE32+ executable (console) x86-64`.
* GATE 2 compiles **300 object targets** (every object of `igi-editor`, `igi_tests` and
  `igi_mcp` under i686-w64-mingw32). It is compile-only by design — see §2.
* That run exercised the stale-PCH self-heal: the first i686 attempt failed with a bogus
  `redefinition of 'struct LevelObject'`, the script cleared the PCHs and retried, and the
  retry reached `GATE 2 (i686 compile) OK (exit 0)`.

Re-run `2026-10-25T18:29Z`, `--arch x64`, on the tree that included the task-6 texture
changes (`source/renderer/renderer_objects_texture.cpp`,
`source/renderer/model_texture_resolution.cpp`, `tests/test_character_texture_resolution.cpp`):

```
  [30/252]  ... renderer_objects_texture.cpp.obj
  [97/252]  ... model_texture_resolution.cpp.obj
  [168/252] ... tests/test_character_texture_resolution.cpp.obj
  [111/252] Linking CXX executable .../bin/Release/igi1ed.exe
  [252/252] Linking CXX executable .../bin/igi_tests.exe
==> GATE 1 (x64 link) OK (exit 0)
    .../bin/Release/igi1ed.exe (5699239 bytes)
    .../bin/igi_tests.exe (13093015 bytes)
==> BUILD GATE OK (exit 0)
```

Zero `FAILED:` lines in that log. This is compilation + linking evidence only: no Windows
binary is executed on this host (§5.3).

### 5.1b Acceptance run — full tree, `2026-10-25T19:29Z`

`bash scripts/build-windows.sh` (no options), run on the tree as it stood after the whole
team's edits, zero `FAILED:` lines in the transcript:

```
[248/248] Linking CXX executable .../bin/igi_tests.exe
==> GATE 1 (x64 link) OK (exit 0)
    .../bin/Release/igi1ed.exe (5698263 bytes)
    .../bin/igi_tests.exe (13337136 bytes)
==> i686 compile-only build of 305 object targets
[254/254] Building CXX object CMakeFiles/igi_mcp.dir/source/mcp/game_data_service.cpp.obj
==> GATE 2 (i686 compile) OK (exit 0)
 build-windows.sh summary
   x64  link gate      : exit 0
   i686 compile gate   : exit 0
==> BUILD GATE OK (exit 0)
```

Both gates exit 0, both x64 executables link, and all 305 i686 object targets compile.

### 5.2 Host-native lane — `bash scripts/run-host-tests.sh`

Fully green run (`2026-10-25T18:22Z`, `IGI_HOST_NO_CACHE=1`):

```
   suites run            : 33
   suites failed         : 0
   tests run             : 347
   tests passed          : 344
   tests failed          : 0
   tests skipped         : 3 (in 2 suite(s))
   game corpus           : /Users/haseeb-mir/Downloads/IGI
   suites not host-portable (compile): 10
   suites not host-portable (link)   : 0
   suites not host-portable (runtime unresolved symbol): 19
   engine sources excluded (compile) : 13
OK: 347 tests passed in 33 suites
==> run-host-tests.sh exit=0
```

Latest run (`2026-10-25T19:06Z`, 58 candidate suites registered by the team at that point):

```
   suites attempted      : 58
   suites run            : 36
   suites failed         : 0
   tests run             : 365
   tests passed          : 363
   tests failed          : 0
   tests skipped         : 2 (in 1 suite(s))
   game corpus           : /Users/haseeb-mir/Downloads/IGI
   suites not host-portable (compile): 7
   suites not host-portable (link)   : 0
   suites not host-portable (runtime unresolved symbol): 22
   engine sources excluded (compile) : 13
OK: 365 tests passed in 36 suites
==> run-host-tests.sh exit=0
```

An intermediate run during the same session (`18:36Z`) also caught `test_train_rail`, a
suite a teammate created mid-run: the lane discovered and ran it automatically, reported
1 of its 9 tests failing, and exited 1. That suite was fixed by its owner and the 19:06Z run
above is green. The next run (`19:29Z`) picked up `test_keybindings`, another brand-new
suite, and reported 1 of its 7 tests failing (`tests/test_keybindings.cpp:308`) while the
other 36 suites passed. This is the intended behaviour: a newly registered suite needs no
runner change, and a real failure is never masked. A green lane therefore requires the
writers to be stopped; the integrator's final run on the frozen tree is the one that counts.

The 2 skips are fixture-gated: `test_mef_native_lightmap` skips unless `IGI_MEF_SNIPER` /
`IGI_MEF_WATERTOWER` are exported (see §4).

### 5.3 What is *not* covered by the host lane, and why

`suites not host-portable (compile)` and `(runtime unresolved symbol)` are the honest
limits of running Windows source on macOS:

* **13 of 94 engine sources do not compile on the host.** 11 of them fail on one cause:
  `source/app.h:473-486` declares `struct game_process_s` with `HANDLE hProcess/hThread/
  hMonitorThread` and `DWORD pid`. On the shipping target `pch.h` is force-included ahead
  of every TU, so those types always exist; the host lane has no PCH and no `windows.h`.
  The others need `<windows.h>` / `<tlhelp32.h>` directly (`source/utils.cpp`,
  `source/mcp/mcp_tools_runtime.cpp`, `source/level/qvm_native_registry.cpp`,
  `source/animation.cpp`).
  *Consequence:* any suite whose engine dependency lives in one of those TUs compiles but
  cannot resolve its symbols, so it is reported with the missing symbol (for example
  `Utils::GetIGIRootPath()`, `RES_WriteEntries(...)`, `Str_SPrintf(...)`,
  `Config::Get()`, `mcp::CallRuntimeTool(...)`).
  *Fix, if the team wants those suites:* guard the `game_process_s` block in `source/app.h`
  under `#if defined(_WIN32)` — that single change unblocks 11 of the 13 sources and most
  of the 23 blocked suites. It was deliberately **not** done here: `source/app.h` is a
  shared header outside this task's write scope, and a half-applied Windows type shim would
  be a fake gate.
* **7 suites do not compile on the host** — they need `<windows.h>`, `HANDLE`, `DWORD`, or
  a Windows-only member (`test_verify_level`, `test_editor_camera_start`,
  `test_runtime_subsystems`, `test_igi1conv_lightmap`, `test_visual_integrity`,
  `test_config`, `test_verify_unit`).
* **No Windows binary is ever executed.** There is no Windows runtime and no wine on this
  host, so the cross-build gate proves compilation and linking only. Behavioural evidence
  comes from the host lane, which runs the same source natively.

---

## 6. Gotchas worth knowing

1. **A GCC PCH that the compiler accepts can still be wrong.** If a header is edited while
   the PCH is being generated, GCC can accept the result with stale `#pragma once`
   bookkeeping; every TU that includes one of the PCH's headers then fails with a bogus
   `redefinition of <X>` in a header that plainly has `#pragma once`, hiding the real
   diagnostics. `--fresh-pch` clears it up front, and both gates self-heal by clearing the
   PCHs and retrying once when they see that symptom.
2. **The repository root must never be on the include path.** It contains a plain-text file
   named `version`, which shadows the C++ standard header `<version>` and breaks every TU
   that includes it (`version:1:1: error: expected unqualified-id`). This cost two
   teammates a debugging session.
3. **A build tree must be quiesced.** Concurrent header edits during a build produce both
   the stale-PCH symptom and genuinely torn intermediate states. Run the gate when writers
   are stopped; the integrator should run it last on the frozen tree.
4. **`build-mingw/` must stay i686.** `tools/igi-re-parity/tu-check.py` replays the compile
   commands from `build-mingw/compile_commands.json` for the whole team; reconfiguring that
   tree for another architecture silently breaks everyone else's check.
5. **`build-mingw*/` is not gitignored.** Both trees plus the logs are untracked build
   output. That is outside this task's write scope — flagged to the Lead.
6. **Both scripts log through a real `| tee` pipeline, never `exec > >(tee ...)`.** With
   process substitution the shell does not wait for `tee`, so the tail of the transcript is
   silently lost when the script exits; the first version of both scripts had this bug and
   `build-mingw/host-tests.log` ended up truncated to a 1.4 KB header.
7. **`build-mingw/host-tests.log` is a fixed path, so two people running the lane at once
   clobber each other's transcript.** Use `IGI_HOST_LOG_DIR` to send your run elsewhere if
   you need to keep it.

---

## 7. Evidence

| what | where |
| --- | --- |
| combined cross-build transcript | `build-mingw/build-windows.log` |
| raw i686 compile gate | `build-mingw/build-windows-i686.log` |
| raw x64 link gate | `build-mingw-x64/build-windows.log` |
| host lane transcript | `build-mingw/host-tests.log` |
| host lane objects / binaries | `build-mingw/host-tests/{obj,bin}/` |
| build-integrity verdict (in transcript) | `build-mingw/build-windows.log`, the `BUILD-INTEGRITY` block |

---

## 8. Build integrity — refusing to trust a stale tree

### 8.1 The failure this prevents

A binary shipped with a null write in `App::Input_OnMouse`. It was **not** a source bug.
`build-mingw/CMakeFiles/igi-editor.dir/source/app_input_mouse.cpp.obj` had been built
hours earlier and was never recompiled while `app.cpp.obj` and 33 others were rebuilt
against a different `App` layout, because ninja had no dependency record for it:

```
$ <cached-ninja> -C build-mingw -t deps CMakeFiles/igi-editor.dir/source/app_input_mouse.cpp.obj
...app_input_mouse.cpp.obj: #deps 0, deps mtime 1793108196720002110 (VALID)
$ <cached-ninja> -C build-mingw -t deps CMakeFiles/igi-editor.dir/source/app.cpp.obj
...app.cpp.obj: #deps 668, deps mtime 1793108207136698602 (VALID)
```

The stale object read a member at the old offset, landed in the zero-initialised SSO
buffer of an empty `std::string`, and wrote through NULL. It looks exactly like a source
bug, and it means **any gate run on such a tree may have validated a corrupted binary**.

`scripts/check-build-integrity.sh` makes that state impossible to miss. It is wired into
`scripts/build-windows.sh` immediately after gate 2 and before the summary, so it cannot
be skipped; a FAIL fails the whole gate.

### 8.2 Why header changes propagate through the PCH here

This is the measurement that makes rule A load-bearing rather than decorative:

```
ai_events.cpp.obj:        #deps 1     (just the .cpp)
animation_motion.cpp.obj: #deps 3
cmake_pch.hxx.gch:        #deps 623   (includes source/renderer/renderer_rain.h)
app_input_mouse.cpp.obj:  #deps 659   (this one is compiled WITHOUT the PCH)
```

An object compiled *through* the PCH never re-reads the headers, so its depfile lists only
the `.cpp`; the PCH's own depfile carries the whole header closure. A normal object
therefore has **no header in its dependency list at all**, and its only header-level
dependency is the implicit edge in `build-mingw/build.ninja`:

```
build CMakeFiles/igi-editor.dir/source/ai_events.cpp.obj: CXX_COMPILER__... \
    source/ai_events.cpp \
    | CMakeFiles/igi-editor.dir/cmake_pch.hxx CMakeFiles/igi-editor.dir/cmake_pch.hxx.gch \
    || cmake_object_order_depends_target_igi-editor
```

So a header change propagates as: header changes → `cmake_pch.hxx.gch` is rebuilt → the
gch is newer than every object → ninja rebuilds every object through that implicit edge.

**Not all objects use the PCH.** In this tree 69 of the 352 objects carry
`FLAGS = -O3 -DNDEBUG -std=gnu++20` with no `-Winvalid-pch -include .../cmake_pch.hxx`
and no `| cmake_pch.hxx.gch` implicit input, so they never read the PCH — and they carry
the full ~650-entry header depfile instead. The check reads `build.ninja` to decide which
is which. Without that distinction rule A reported 20 false positives (measured: every
object older than its gch was a no-PCH object).

### 8.3 The rules

| rule | fires when | why it is evidence |
| --- | --- | --- |
| `STALE_PCH` | a `.obj` is older than the `cmake_pch.hxx.gch` it is compiled through | it was not recompiled after the last PCH regeneration, i.e. the tree is not in a post-build state. Applied only to PCH-compiled objects. |
| `NO_RECORD` | a `.obj` has no entry in `ninja -t deps` | ninja knows nothing about its headers |
| `EMPTY_DEPS` | a `.obj` has `#deps 0` | **the exact signature of the shipped bug**: ninja believes it includes no headers, so nothing can ever invalidate it. Scoped to `.obj` — `generated/igi-editor.rc.res` legitimately has `#deps 0`. |
| `DEPS_DESYNC` | ninja prints `(STALE)`/`(MISSING)` instead of `(VALID)` | the dep log no longer matches the artifact |
| `STALE_DEP` | a file in the object's own dependency list is newer than the object | the precise per-object rule; it is what catches a stale `.cpp` and the no-PCH objects |
| `NINJA_VERSION` | `CMAKE_MAKE_PROGRAM` from `CMakeCache.txt` is a different **version** than `command -v ninja` | a build driven by the other ninja discards the dependency DB (`build log version is too old; starting over`), which is how objects end up with no dep record. Different paths with the *same* version are only a note. |

The check uses the ninja **CMake has cached**, never a bare `ninja` from PATH:

```bash
grep '^CMAKE_MAKE_PROGRAM:' build-mingw/CMakeCache.txt
# CMAKE_MAKE_PROGRAM:FILEPATH=/Library/Frameworks/Python.framework/Versions/3.12/bin/ninja
```

### 8.4 Running it

```bash
bash scripts/check-build-integrity.sh            # standalone; exit 0 = consistent
bash scripts/check-build-integrity.sh --quiet
bash scripts/check-build-integrity.sh --fix      # delete objects + PCHs, then rebuild
bash scripts/check-build-integrity.sh --build-dir DIR
```

Exit codes: `0` consistent (or nothing built yet), `1` inconsistent, `2` environment error
(no `CMakeCache.txt` / no cached ninja).

PASS looks like this:

```
   objects      : 352
   via PCH      : 283  (rule A applies to these only)
   no PCH       : 69  (rule E applies; their depfile lists every header)
   files stat'd : 1555
===================================================================
 BUILD INTEGRITY: PASS — all 352 objects are consistent with their
                  dependencies, their PCH, and the ninja dependency log.
```

### 8.5 Recovery

The check prints this on failure. Run it from the repository root:

```bash
find build-mingw/CMakeFiles/igi-editor.dir \
     build-mingw/CMakeFiles/igi_tests.dir -name '*.obj' -delete
rm -f build-mingw/CMakeFiles/igi-editor.dir/cmake_pch.hxx.gch \
      build-mingw/CMakeFiles/igi_tests.dir/cmake_pch.hxx.gch
cmake --build build-mingw --target igi_tests igi-editor --parallel
```

`bash scripts/check-build-integrity.sh --fix` performs exactly those three steps.

### 8.6 What could not be made reliable

* **"Older than the newest header under `source/`" is not usable as a global rule.** With
  1–3 deps per PCH object there is no per-object include set to compare against, and the
  coarse rule would flag all 352 objects the moment any single header is touched. It is
  used only as a fallback for objects whose dep record is already broken (`NO_RECORD` /
  `EMPTY_DEPS`), where the object is a FAIL regardless — there it is the strongest
  available statement.
* **`STALE_PCH` fires on interleaved builds, not only on real staleness.** In this build
  dir `.ninja_log` shows the `igi-editor` PCH compiling 9344→26565 ms in one build while
  `app_ui.cpp.obj` compiled 13530→26205 ms — overlapping work, which only happens when two
  ninja processes share one build dir. That is reported as a FAIL on purpose: it is
  exactly the condition under which the tree must not be trusted (and it is also how the
  PCH gets corrupted, per §6.3). A plain rebuild clears it.
* **The check cannot see a header that was edited and reverted** within one mtime tick,
  and it does not verify struct layouts directly — it proves the artifacts are consistent
  with the dependency information ninja holds, which is what the bug defeated.

