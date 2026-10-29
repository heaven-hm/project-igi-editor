# Contributing to the IGI Editor

Read [`AGENTS.md`](AGENTS.md) first. It is short, it is binding, and it is the source of
the order below. This file is the practical companion: the commands in the order they must
be run, and the traps that have actually cost this project time. Every claim here names the
script or file it comes from, so you can check it instead of trusting it. Line references
are pinned to commit `7b281355`; the script, target or symbol named beside each number is
the durable anchor, so re-find it by name if the number has drifted.

For how the code is put together once you are ready to change it, read
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md). For the test inventory, see
[`docs/TESTS.md`](docs/TESTS.md).

---

## 0. Which checkout you work in

`/Users/haseeb-mir/Documents/Code/project-igi-editor` is the original checkout and must not
be modified. Work happens in the worktree
`/Users/haseeb-mir/Documents/Code/project-igi-editor-igi-re` (`AGENTS.md:113-117`).

The retail game install is **read-only input**, never a place to write. `Config::Init`
recompiles `<IGI>/editor/qed/*.qsc` into `.qvm` on every editor start, so every lane that
runs the editor hands it a writable *copy* of the game instead
(`scripts/e2e-headless.sh:78-83`, `AGENTS.md:91-94`).

---

## 1. The one rule that is not negotiable: test → commit → push

`AGENTS.md:5-16`. Never the other order. Concretely:

- Do not `git commit` a source change that has not been compiled and tested since the last
  edit to it. "It is a one-line constant" is not an exemption.
- Do not `git push` until the full gate has passed **on the committed content**.
- If the gate fails after a commit, fix forward with another commit; do not amend published
  history on a shared branch.

### The gate is three lanes, and they are not interchangeable

| # | lane | command | what it proves |
| - | ---- | ------- | -------------- |
| 1 | build gate | `bash scripts/build-windows.sh` | every TU of the editor and the test aggregate compiles for MinGW; the x64 tree links; the i686 tree is internally consistent |
| 2 | host lane | `IGI_HOST_NO_CACHE=1 bash scripts/run-host-tests.sh` | the pure-logic suites build and **run** natively (macOS clang++) |
| 3 | Windows suite | i686 `igi_tests.exe` under Wine | the suites the host lane cannot build, plus the corpus-backed ones |

The host lane is **not** the full suite. It does not build
`tests/test_runtime_subsystems.cpp` (Windows-only) or the MCP tool tests
(`tests/test_mcp_*.cpp`); those exist only in the Windows `igi_tests` binary. Treating the
host lane as the last word once hid nine real failures, including a
dangling-`std::initializer_list` bug that is architecture-independent and therefore shipped
(`AGENTS.md:49-61`).

### The automatic gate is not "testing" — the user testing the binary is

Passing all three lanes is **not** permission to push. It proves the code compiles and the
suite passes; it cannot prove the editor works, because the GUI does not run on this Mac.
The editor's shader tiers are 21/41/45 and it wants OpenGL 4.1 compatibility; this host
offers GL 2.1 (`AGENTS.md:21-26`, `scripts/e2e-headless.sh:8-10`,
`scripts/perf-guard.sh:166-168`).

So the full order is (`AGENTS.md:28-47`):

1. run the gate (build gate → host lane → Windows suite);
2. commit locally;
3. build the i686 editor and copy it into the user's install so they can run it —
   `cmake --build build-mingw --target igi-editor --parallel`, binary lands in
   `bin/Release/igi1ed.exe`, **not** `bin/igi1ed.exe`, then copy it to `<IGI>/igi1ed.exe`
   after backing up the previous one;
4. wait for the user to confirm it works;
5. only then `git push`.

Corollary: when a change is ready but untested by the user, leave it **committed and
unpushed**, and say so explicitly ("committed, not pushed — awaiting your test").

---

## 2. Toolchain

| tool | needed by |
| ---- | --------- |
| `cmake` (>= 3.18), `ninja` | every lane (`scripts/build-windows.sh:124-132`) |
| `i686-w64-mingw32-{gcc,g++,windres,objdump}` | build gate, Wine lanes |
| `x86_64-w64-mingw32-{gcc,g++,windres}` | build gate, x64 link half |
| `clang++` (or `c++`) | host lane (`scripts/run-host-tests.sh:53-56`) |
| `python3` | host lane runner, e2e verdicts, perf guard |
| Wine | the Windows suite and the headless lanes (`scripts/e2e-headless.sh:110`) |

Two build trees, on purpose (`scripts/build-windows.sh:5-27`):

- `build-mingw/` — **i686**. This is the shipping target and the tree
  `tools/igi-re-parity/tu-check.py` replays compile commands from. Keep it 32-bit;
  reconfiguring it for another architecture silently breaks everyone else's check
  (`docs/igi-re-parity/BUILD_GATE.md:331-333`).
- `build-mingw-x64/` — x86_64. This is the only architecture the repo can always *link*
  for, because the only committed GL import libraries are MSVC x64 ones. It links but does
  not run: no x64 freeglut/glew runtime DLLs are committed
  (`scripts/build-windows.sh:7-13`).

You do not have to configure by hand: `scripts/build-windows.sh` configures both trees and
reuses an existing configure unless you pass `--reconfigure` or `--clean`
(`scripts/build-windows.sh:158-175`).

> **Never drive `build-mingw/` with a bare `ninja` from `PATH`.** This repo has two ninja
> installations (the one CMake cached, and Homebrew's). A build run by the *other* one
> discards the tree's dependency database ("build log version is too old; starting over"),
> which is how objects end up with no dependency record at all. Always go through
> `cmake --build build-mingw ...`, which uses the cached ninja; rule F of
> `scripts/check-build-integrity.sh:77-80,194-203` fails the gate if the two versions
> differ.

This host has no `timeout` and no `flock`. Long-running lanes implement their own bounded
kill by polling the child (`scripts/e2e-headless.sh:274-308`), and when several agents build
at once, serialise by process name — `pgrep -x cc1plus`, `pgrep -x ninja`. Do **not** use
`pgrep -f "cc1plus|ninja"`: the pattern matches the `pgrep` command line itself and
deadlocks.

---

## 3. The gate, in order

### 3.1 Build gate

```bash
bash scripts/build-windows.sh
```

Exit 0 means all of: x64 link OK, i686 compile OK, build integrity OK
(`scripts/build-windows.sh:356-371`). Useful variants:

```bash
bash scripts/build-windows.sh --arch i686        # the half that matters before a Wine run
bash scripts/build-windows.sh --keep-going       # ninja -k 0: collect every error
bash scripts/build-windows.sh --fresh-pch        # drop stale GCC PCHs first
bash scripts/build-windows.sh --clean            # wipe both trees, reconfigure
```

Transcripts: `build-mingw/build-windows.log` (combined),
`build-mingw/build-windows-i686.log`, `build-mingw-x64/build-windows.log`
(`scripts/build-windows.sh:47-50`).

The build-integrity check runs automatically at the end of the i686 half and cannot be
skipped (`scripts/build-windows.sh:28-35,324-354`). Run it on its own after any header
change, or any time you doubt the tree:

```bash
bash scripts/check-build-integrity.sh          # exit 0 = consistent, 1 = inconsistent, 2 = env error
bash scripts/check-build-integrity.sh --fix    # delete stale objects + PCHs, then rebuild
```

### 3.2 Host lane (the inner loop)

```bash
IGI_HOST_NO_CACHE=1 bash scripts/run-host-tests.sh
```

`IGI_HOST_NO_CACHE=1` disables the object cache; while headers are being edited
concurrently it is the safe choice, because the cache cannot save you from a stale object
compiled against an old struct layout (`docs/igi-re-parity/BUILD_GATE.md:136-148`).

Useful variants (`docs/igi-re-parity/BUILD_GATE.md:32-35`):

```bash
bash scripts/run-host-tests.sh --list                          # what will run
bash scripts/run-host-tests.sh --show-output                   # full gtest output
IGI_HOST_TESTS="tests/test_mef_dual_set.cpp" bash scripts/run-host-tests.sh
```

The transcript is always `build-mingw/host-tests.log`, which is a fixed path — two people
running the lane at once clobber each other's log. Use `IGI_HOST_LOG_DIR` to keep yours
(`docs/igi-re-parity/BUILD_GATE.md:340-342`).

### 3.3 Windows suite (the gate that finds what the host lane cannot)

From the repository root, with the game path set — several tests are CWD- and
asset-dependent and fail spuriously otherwise (`AGENTS.md:63-77`):

```bash
rm -f bin/igi_tests.exe && cmake --build build-mingw --target igi_tests --parallel
file bin/igi_tests.exe          # must say: PE32 executable (console) Intel 80386
WINEPATH='Z:\<abs-path-to>\scratch\wine-smoke\run' \
IGI_GAME_PATH='Z:\<abs-path-to>\IGI' \
  wine bin/igi_tests.exe '--gtest_filter=-*VerifyLevel*' --gtest_color=no
```

The i686 runtime DLLs must be staged next to the binary; `scratch/wine-smoke/run` is that
stage, populated once with `bash scripts/run-wine-smoke.sh --tiers help`
(`scripts/perf-guard.sh:153-157`). `--gtest_filter=-*VerifyLevel*` excludes the
verify-level integration tests, which are covered by the headless lane below.

The exact test counts drift as suites are registered — treat "exit 0 with 0 failures" as
the acceptance condition, not a particular number
(`docs/igi-re-parity/BUILD_GATE.md:166-172`).

### 3.4 Headless acceptance lane (all 14 levels, GL-free, under Wine)

This is the only way to see whether every level still loads on this host, because the GUI
cannot run here (`AGENTS.md:79-103`, `docs/TESTS.md:40-78`):

```bash
rm -f bin/Release/igi1ed.exe && cmake --build build-mingw --target igi-editor --parallel
IGI_E2E_STAGE=$PWD/scratch/e2e-50 bash scripts/e2e-headless.sh --fast   # smoke + --run-tests
IGI_E2E_STAGE=$PWD/scratch/e2e-50 bash scripts/e2e-headless.sh          # + 14 verify passes
```

Use your own `IGI_E2E_STAGE` while others are working: the default is
`scratch/e2e-headless` (`scripts/e2e-headless.sh:112-113`), and two runs sharing it will
fight over the staged copy.

It stages its own run directory and its own writable copy of the game, asserts the pinned
baseline, and exits non-zero on any change to it:

- levels 1..14 load, and every placement's model and every texture resolves;
- **level 7 fails on exactly one id, `s332_02_1`** — a known retail defect (the shipped
  `level7/objects.qvm` names a model the shipped data does not contain). Asserted as a
  defect, not tolerated: a second missing id, or level 7 passing, fails the run;
- zero missing textures anywhere.

What it does **not** prove: resolution, not rendering. `--smoke-level` never parses a mesh,
binds a texture or draws a frame; `--verify-level` never launches the editor. A green run
means "every level's objects still resolve", not "the viewport looks right"
(`scripts/e2e-headless.sh:71-76`).

### 3.5 Performance guard (opt-in)

```bash
bash scripts/perf-guard.sh                 # ~2 min: frame + headless lanes
bash scripts/perf-guard.sh --with-gameplay # + the gameplay anim lane
bash scripts/perf-guard.sh --parse-only    # re-judge the last run's logs
```

It budgets deterministic counters, not wall-clock frame totals, and refuses to measure a
binary older than the newest file under `source/` (`scripts/perf-guard.sh:1-180`). It needs
the populated `scratch/wine-smoke/run` stage. The e2e lane can also drive it with
`--perf-guard` (`scripts/e2e-headless.sh:546-567`).

---

## 4. Traps that will bite you

**The output slot is shared between architectures.** `bin/Release/igi1ed.exe` and
`bin/igi_tests.exe` are single output slots used by *both* build trees, and
`build-windows.sh` runs the x64 link half, so after a full gate those files are 64-bit. A
Wine run straight afterwards fails with `libgcc_s_seh-1.dll not found` / exit 53. Force an
i686 relink first, deleting the artifact so ninja cannot skip it
(`docs/igi-re-parity/TRANSLATION_CAMPAIGN.md:21-27`):

```bash
rm -f bin/Release/igi1ed.exe && cmake --build build-mingw --target igi-editor --parallel
rm -f bin/igi_tests.exe       && cmake --build build-mingw --target igi_tests   --parallel
```

**Check the image before you run it.** Both Wine lanes refuse a foreign binary rather than
running it; do the same by hand (`scripts/e2e-headless.sh:192-198`,
`scripts/perf-guard.sh:257-262`):

```bash
file bin/Release/igi1ed.exe   # PE32 executable (console) Intel 80386  ← i686, runnable
                              # PE32+ ... x86-64                     ← x64, not runnable here
```

**`IGI_GAME_PATH` must point at a real install.** The host lane defaults it to
`/Users/haseeb-mir/Downloads/IGI` and, when the directory is missing, corpus-backed suites
self-skip via `GTEST_SKIP` instead of failing — a green lane that tested less than you think
(`scripts/run-host-tests.sh:58-65`). The editor is a Windows binary, so the value handed to
*it* must be the Windows form of the path (`Z:\...` under Wine,
`scripts/e2e-headless.sh:287-291`). Suites that need extra fixture files name them
explicitly, e.g. `IGI_MEF_WATERTOWER` (`docs/igi-re-parity/BUILD_GATE.md:157-162`).

**The build-integrity check has an mtime blind spot.** It proves the artifacts are
consistent with the dependency information ninja holds — not that your edit is in the
binary. It cannot see a header that was edited and reverted within one mtime tick, and it
does not verify struct layouts directly (`docs/igi-re-parity/BUILD_GATE.md:486-488`). When
you need to prove a specific change reached the artifact, **verify by content**: put a
distinctive `const char*` literal in the code and grep the binary for it.

```bash
# e.g. renderer_draw.cpp:3630 emits this literal from Renderer::Draw's profile block
strings -a bin/Release/igi1ed.exe | grep 'RenderProfile] 30 frames: terrain='
```

A match proves the translation unit that carries the literal was compiled into that
artifact; no match proves it was not. mtimes prove neither.

**A stale PCH looks like a source bug.** A GCC PCH generated while headers were being
edited concurrently can be accepted by the compiler yet carry stale `#pragma once`
bookkeeping, surfacing as bogus "redefinition of <X>" errors in headers that plainly have
`#pragma once`. `scripts/build-windows.sh` self-heals by clearing the PCHs and retrying
once, and `--fresh-pch` forces it up front (`scripts/build-windows.sh:177-191`). Related:
`STALE_PCH` also fires when two ninja processes share one build tree — reported as a FAIL
on purpose, because that is exactly when the tree must not be trusted
(`docs/igi-re-parity/BUILD_GATE.md:480-485`).

**Do not put the exe name on a command line for the smoke launcher.**
`scripts/run-wine-smoke.sh` uses a launcher that begins with `pkill -9 -f "<the exe name>"`,
which kills any process whose command line contains that string — including the shell that
typed it. Always call it as `bash scripts/run-wine-smoke.sh` and let the script reference
the name from a variable (`scripts/run-wine-smoke.sh:26-30`).

**Never edit the build file and the file it names in separate commits.** `source/` is
globbed with `CONFIGURE_DEPENDS`, so a new source file under a globbed directory is picked
up without a CMake edit — but `tests/*.cpp` is an **explicit** list in
`add_executable(igi_tests ...)` (`CMakeLists.txt:347-470`). A commit once added
`tests/test_skinned_static_parity.cpp` to that list without committing the file
(`git show 0e0c3d9f:CMakeLists.txt` names it at line 423; `git cat-file -e
0e0c3d9f:tests/test_skinned_static_parity.cpp` fails), which left the branch unbuildable
until `5d277f21` fixed it forward. If you touch `CMakeLists.txt`, commit the files it names
in the same commit, and build before you push.

---

## 5. How to add a test

Tests are `tests/test_<area>.cpp`, Google Test, one suite per file. Two things decide
whether your test ever runs:

1. **Register it in `CMakeLists.txt`.** Add the file to the explicit
   `add_executable(igi_tests ...)` list (`CMakeLists.txt:347-470`). Both lanes read that
   list: the host runner takes "every `tests/*.cpp` referenced from `CMakeLists.txt`"
   (`tools/host-tests/host_test_runner.py:83-91`), and the Windows aggregate is the same
   target. An unregistered test file is invisible in both lanes and the gate stays green.
2. **If the test needs engine code that is not already in the host pool**, add that
   `source/*.cpp` to `target_sources(igi_tests PRIVATE ...)` (`CMakeLists.txt:497+`). The
   host runner compiles exactly that pool into its static library; `igi-editor` picks the
   same file up through the `source/<dir>/*.*` globs, so no second registration is needed
   (`CMakeLists.txt:67-84`).

Then:

```bash
IGI_HOST_TESTS="tests/test_your_area.cpp" IGI_HOST_NO_CACHE=1 bash scripts/run-host-tests.sh
```

### Keep pure-logic and corpus-backed tests in separate files

This is a convention with a reason. The host lane trial-compiles each suite and keeps only
what does not need `windows.h`; a suite that links is linked **strictly** first and only
falls back to `-Wl,-undefined,dynamic_lookup` if the strict link fails
(`tools/host-tests/host_test_runner.py:290-312`). A lazily linked suite that then calls an
unresolved symbol aborts inside `dyld`; the runner reports it as "not host-portable" and
names the first symbol `ld` reported (`docs/igi-re-parity/BUILD_GATE.md:131-134`).

So:

- a **pure-rule** test (arithmetic, parsing, geometry) links strictly and runs in the fast
  inner loop — put it in its own file;
- a **corpus-backed** test (needs `LevelObjects::Load` or the retail install) should live in
  a *separate* file that self-skips without `IGI_GAME_PATH`, so the pure-rule suite is not
  dragged out of the host lane with it. This is the reason
  `tests/test_cutscene_camera.cpp` and `tests/test_cutscene_camera_corpus.cpp` are two
  files (`CMakeLists.txt:457-462`).

Corpus-backed suites skip rather than fail when the corpus is absent, and the host summary
reports the skip count per suite — read that column before calling a run green.

Fixtures live in `tests/fixtures/` and are copied next to the test binary by the CMake
post-build step; they are synthetic and committed (`docs/TESTS.md:173-181`).

---

## 6. How to add an IGI-RE-derived rule

Engine behaviour is reconstructed from IGI-RE
(`/Users/haseeb-mir/Documents/Code/IGI-RE`), and every claim about it must cite
**function + address + file:line**. No web sources, and open-igi (C#) is not an authority
(`AGENTS.md:105-111`, `source/runtime/rain_rules.h:12-14`). The established shape is:

1. **A pure rule module** — `source/runtime/<feature>_rules.{h,cpp}` (or
   `source/level/<feature>_rules.h` for placement/geometry rules). Constants live here as
   data, each cited to its IGI-RE address; the header states whether the module is WIRED
   into the editor or an extracted-but-unwired audit. A new file under `source/runtime/` is
   picked up by the glob — no `CMakeLists.txt` edit for the editor
   (`CMakeLists.txt:75-84`).
2. **A unit test** — `tests/test_<feature>_rules.cpp`, registered as in §5. Pin the
   discriminating case, not just the happy path: e.g. the cutscene camera test pins that
   two keys 100 units apart give **37.5** under the engine's cubic Hermite and **50** under
   the linear blend it replaced (`git show 0e0c3d9f`).
3. **A parity document** — `docs/igi-re-parity/<SUBSYSTEM>.md`, opening with a `Status:`
   line that names the rule module and its test, and says plainly what is wired, what is
   audited only, and what was not verified. `docs/igi-re-parity/RAIN.md:1-25` is a good
   template; `docs/igi-re-parity/MENU_UI.md` shows the citation density expected.
4. **Wire it, or say you did not.** A rule module with no caller is a known failure mode in
   this repo: `cutscene_rules.*` was correct and unreachable until `0e0c3d9f` gave it a
   caller (`git show 0e0c3d9f`). If your change is extraction-only, say so in the doc's
   `Status:` line and in the commit message.

Cite the oracle as the tree already does — the address and the reconstructed file:line
together, so a disassembler and a reader can each check it
(`source/runtime/rain_rules.h:424-425`, `source/runtime/cutscene_rules.h:325`):

```
Rain_ClipCells  HOOK 0x499910 (src/dxinit/rain.c:493-554)
CutScene_Spline HOOK 0x4F6290 (src/cutscene/cutscene.c:631-651)
```

`HOOK <address>` is the retail address of the function in the decompilation; the
`src/...:line-line` is where it lives in the reconstructed source.

---

## 7. Where the rest of the documentation is

| document | what it is |
| -------- | ---------- |
| [`AGENTS.md`](AGENTS.md) | the binding project rules (gate order, evidence standard) |
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | the layers, and a map of `renderer_draw.cpp` |
| [`docs/TESTS.md`](docs/TESTS.md) | the test inventory and the suite table |
| [`docs/igi-re-parity/BUILD_GATE.md`](docs/igi-re-parity/BUILD_GATE.md) | build gate + host lane mechanics, build-integrity rules |
| [`docs/igi-re-parity/WINE_SMOKE.md`](docs/igi-re-parity/WINE_SMOKE.md) | the Wine lane |
| [`docs/igi-re-parity/`](docs/igi-re-parity/) | one audit per engine subsystem, with its IGI-RE citations |
| [`docs/KNOWN_ISSUES.md`](docs/KNOWN_ISSUES.md) | known defects and their status |

Two of the older lane documents predate the Wine lanes and still say this host cannot
execute a Windows binary (`docs/igi-re-parity/BUILD_GATE.md:6-9`); Wine is installed and
the headless lanes run under it. Where a document and a script disagree, the script is what
actually runs — check the script.
