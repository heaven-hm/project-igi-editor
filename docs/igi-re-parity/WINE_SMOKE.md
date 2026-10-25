# WINE_SMOKE — running the Win32 editor under Wine (task-15)

**Status: the binary RUNS, but the editor cannot finish starting under Wine on
this macOS host.** The cause is identified and reproduced: Wine hands the
editor a legacy **OpenGL 2.1** context, while every shader in
`editor/shaders/41/` begins with `#version 410 core`. Shader compilation fails,
`Renderer::Init()` returns false, `App::Init()` returns false, and `main`
returns **2** (`source/main.cpp:770-772`).

A probe (below) proves this is **not** a Wine driver limit: Wine can give a
GL **4.1 / GLSL 4.10** core context on this same host. The editor simply never
asks for one. That is a source-level question, so it is reported rather than
fixed here (task-15 forbids source edits).

| gate | result | evidence |
|---|---|---|
| i686 link (`cmake --build build-mingw --target igi-editor`) | **exit 0** | `logs/i686-build.log` |
| tier `help` (`--help` under Wine) | **PASS, exit 0** | `logs/tier-help.log` |
| tier `tests` (`--run-tests`, retail data) | exit 1 — 67/72 assertions | `logs/tier-tests.log` |
| tier `gui` (real GLUT/OpenGL editor) | **exit 2** — window created, GL 2.1, `#version 410` rejected | `logs/tier-gui.log` |
| GL context probe | legacy → 2.1 (fixed-function OK); core 3.2/4.1 → 4.1 (all fixed-function `GL_INVALID_OPERATION`); compatibility 3.2/4.1 → **creation refused** | `scratch/wine-smoke/glprobe/` |

The single command below (build + all three tiers) was run end to end:

```
$ bash scripts/run-wine-smoke.sh
==> 3. build: cmake --build …/build-mingw --target igi-editor
   backed up existing …/bin/Release/igi1ed.exe -> …/logs/previous-igi1ed.exe
   existing artifact is not a 32-bit image — removing it so ninja relinks
[1/1] Linking CXX executable …/bin/Release/igi1ed.exe
   cmake --build exit=0 (full log: …/logs/i686-build.log)
…
 run-wine-smoke.sh summary
   tier help   : PASS  (exit 0, 10s)
   tier tests  : FAIL(exit=1)  (exit 1, 3s)
   tier gui    : FAIL(exit=2)  (exit 2, 3s)
…
==> SMOKE FAILED (exit 1)
```

(`exit 1` is the script correctly refusing to call this a pass.) The very first
cold build of the target compiled and linked all 105 steps; later runs relink
only. Full transcript: `scratch/wine-smoke/logs/full-run.out`.

---

## 1. How to reproduce

All of it is one script. Never put the literal executable name on a command
line — the user's launcher starts with `pkill -9 -f <that name>`, which kills
any process whose command line contains it, **including the shell that typed
it**. Call the script; the script handles the name internally.

```bash
cd /Users/haseeb-mir/Documents/Code/project-igi-editor-igi-re

# full: link i686, stage, run all three tiers  (this is the run in §1)
bash scripts/run-wine-smoke.sh

# re-run the tiers against a saved artifact without touching bin/Release
IGI_WINE_EXE="$PWD/scratch/wine-smoke/artifacts/i686-igi1ed.exe" \
  bash scripts/run-wine-smoke.sh --no-build
# same, plus the editor's own log (this is the run in §5)
IGI_WINE_EXE="$PWD/scratch/wine-smoke/artifacts/i686-igi1ed.exe" \
  bash scripts/run-wine-smoke.sh --no-build --diag-log --tiers gui
```

`--diag-log` exists because the shipped `editor/qed/qedconfig.qsc` contains
`QEDLogs(FALSE)`, so a failed startup is otherwise completely silent.
`Config::Init()` recompiles that `.qsc` to `.qvm` at every start
(`source/config.cpp:224-238`), so flipping the **staged copy** turns the
editor's own log on. The source tree is never touched.

### Staging

`scripts/run-wine-smoke.sh` builds a run directory at
`scratch/wine-smoke/run/` containing:

* the freshly linked i686 `igi1ed.exe` plus `version` and the built `editor/`
  tree (from `bin/Release/`, which is what CMake's POST_BUILD step produces);
* the DLLs the binary actually imports, resolved from disk
  (`libgcc_s_sjlj-1.dll`, `libstdc++-6.dll`, `libwinpthread-1.dll` from the
  MinGW i686 toolchain; `freeglut.dll`, `glew32.dll` — both PE32 i386 — from
  the retail install);
* symlinks to every top-level entry of the retail install
  (`/Users/haseeb-mir/Downloads/IGI`), including the `igi.exe` the editor
  insists on finding (`source/main.cpp:597-609`);
* a **verbatim copy of the user's launcher**, so the real launch path
  (including its `pkill`) is what gets exercised, and the user's own
  `Downloads/IGI/igi1ed.exe` is never overwritten.

The i686 link needs `third_party/GL/lib/x86/{libfreeglut.a,libglew32.a}`.
They are gitignored (`.gitignore:79` has a blanket `x86/` rule), so they are
copied in from
`/Users/haseeb-mir/Documents/Code/project-igi-editor/third_party/GL/lib/x86/`
by the script. **No `CMakeLists.txt` change was needed** — `CMakeLists.txt:209-217`
already selects `lib/x86` by pointer size and links `freeglut glew32 OpenGL32
winmm`.

> Note on the shared output slot: `CMakeLists.txt:227` and `:254-256` pin the
> editor's output to `bin/Release/igi1ed.exe` for **both** architectures, so an
> i686 link overwrites the x64 artifact. The script backs the existing file up
> before building and restores it afterwards, and it removes a foreign 64-bit
> artifact first so ninja does not consider the link up to date. `bin/Release`
> was verified byte-identical (`sha256 7a0a7d98…4f5`) to its pre-run state
> after all runs below.

---

## 2. The i686 link

```
$ cmake --build build-mingw --target igi-editor -j 10     # run as the script's step 3
[1/1] Linking CXX executable …/bin/Release/igi1ed.exe
   cmake --build exit=0
```

The first (cold) invocation of the same target built all 105 steps:

```
[104/105] Building CXX object CMakeFiles/igi-editor.dir/source/cli/verify_level_core.cpp.obj
[105/105] Linking CXX executable …/bin/Release/igi1ed.exe
EXIT=0
```

The linked binary:

```
PE32 executable (console) Intel 80386, for MS Windows
6470235 bytes, sha256 7365d8c005dd8782b16a4513951c0de5f436de1b5d5782f164fabcfe0d799b59
```

Imports (23, all resolved for staging):

```
ADVAPI32.dll KERNEL32.dll USER32.dll ole32.dll OPENGL32.DLL WINMM.DLL
api-ms-win-crt-{convert,environment,filesystem,heap,locale,math,private,runtime,stdio,string,time,utility}-l1-1-0.dll
libgcc_s_sjlj-1.dll  libwinpthread-1.dll  libstdc++-6.dll
freeglut.dll  glew32.dll
```

Only the Win32 DLLs and the `api-ms-win-crt-*` group are Wine builtins; the
MinGW runtime DLLs and `freeglut`/`glew32` were staged from disk.

---

## 3. Tier `help` — PASS

```
$ IGI_WINE_EXE=… bash scripts/run-wine-smoke.sh --no-build
==> 6a. TIER help — launcher with --help (GL-free)
   exit=0  elapsed=10s  (limit 60s)
   output contains the editor banner: yes
```

Verbatim (Wine noise retained, output trimmed to the editor's own lines):

```
…fixme:keyboard:NtUserActivateKeyboardLayout Changing user locale is not supported
IGI Editor v3.4.1-pre - Game Editor

GUI Editor Mode Options:
  -level <num>            Load specific level (1-14)
  -w <width> -h <height>  Set window dimensions
  -draw_parts <bitmask>   Selective loading (1=terrain, 2=sky, 4=objects, 8=flat_sky, 16=buildings, 32=props, 64=AI)
  -stick_to_ground        Enable ground sticking on load

Editor CLI Commands:
  --help                  Show this message
  --run-tests             Run all native C++ parser unit tests
  --verify-level --level N [--level N ...]  Verify level output
  …
```

**Proves:** the 32-bit PE loads under Wine, the MinGW runtime DLLs resolve, the
C++ runtime and `std::cout` work, `CLIHandler::PrintHelp()` runs, and the
process exits 0. **Does not prove:** anything about rendering or the editor's
behaviour.

---

## 4. Tier `tests` — exit 1, 67/72

`--run-tests` runs the editor's own native parser tests **inside the Windows
binary**, against the retail data. This is the strongest runtime evidence in
this document: it is real Windows code, under Wine, reading the real game
archives.

```
$ … bash scripts/run-wine-smoke.sh --no-build
==> 6b. TIER tests — launcher with --run-tests (native parser tests, GL-free)
   exit=1  elapsed=3s  (limit 600s)
   | [CONFIG] Loaded dynamic IGIGamePath: Z:\…\scratch\wine-smoke\run
   |   Total Assertions Checked: 72
   |   Passed Assertions       : 67
   |   Failed Assertions       : 5
   |   [RESULT] FAILED - some assertions did not pass.
```

Passing, verbatim highlights (`logs/tier-tests.log`):

```
[SUITE] RES Archive Parser Tests ...
  [PASS] RES parser successfully parsed archive
  [PASS] Successfully extracted MEF files from models RES
[SUITE] MTP Animation Parser Tests ...
  [PASS] MTP contains texture models
[SUITE] TEX Texture Parser Tests ...
  [PASS] TEX contains texture images
[SUITE] QVM Bytecode Parser & Decompiler Tests ...
  [PASS] Decompiled mission.qvm to QSC successfully
  [PASS] Recompiled objects.qsc back to objects.qvm successfully
[SUITE] Level load end-to-end (levels 1-3) Verification ...
  [PASS] level1: Parse mission.qvm
  [PASS] level1: Parse terrain.tex
  [PASS] level1: Parse terrain.lmp (Lightmaps)
  … level2 and level3 the same …
[ExtractLevel] Level 1: models=415 textures=393 terrain=8
```

### The five failures, with causes

**Three are a test/implementation mismatch, not a runtime problem.**
`--extract-level` writes into `<outdir>\models\level<N>\` and
`<outdir>\textures\level<N>\` (`source/cli/cli_handler.cpp:155-157,183,205`),
while the test counts only the *immediate children* of `<outdir>\models` and
`<outdir>\textures` (`source/cli/cli_tests.cpp:359,369`). It therefore counts
the single `level1` subdirectory:

```
  [FAIL] Extracted more than 100 models (Assertion 'modelCount > 100' failed at …/cli_tests.cpp:374)
  [FAIL] Extracted at least one MEF model file (Assertion 'mefCount > 0' failed at …/cli_tests.cpp:375)
  [FAIL] Extracted more than 100 textures (Assertion 'textureCount > 100' failed at …/cli_tests.cpp:376)
```

The extraction itself **succeeded** — verified on disk in the staging dir:

```
models/level1   : 415 files   (all 415 are .mef)
textures/level1 : 393 files
terrain         : TERRAIN.BIT .CMD .CTR .HMP .LMP .QVM .TEX + terrain.qsc
```

**Two are caused by the working directory.** The FNT suite opens
`assets\editor\qed\editor.fnt` and `assets\editor\qed\editorsm.fnt`
(`source/cli/cli_tests.cpp:447`) relative to the process CWD. The launcher
`cd`s to the executable's directory, which in any deployment contains
`editor/`, not `assets/editor/`. The renderer itself loads the very same font
from the *deployment* path — `Utils::GetExeDirectory() +
"\\editor\\qed\\editor.fnt"` (`source/renderer/renderer_draw.cpp:170`) — which
is what makes the test's path the wrong one, not the staging layout:

```
  [FAIL] editor: file exists (assets\editor\qed\editor.fnt) (Assertion 'fs::exists(path)' failed at …/cli_tests.cpp:447)
  [FAIL] editorsm: file exists (assets\editor\qed\editorsm.fnt) (Assertion 'fs::exists(path)' failed at …/cli_tests.cpp:447)
```

**Differential experiment** (`IGI_WINE_EXTRA_LINKS="assets=$PWD/assets"` adds
an `assets` symlink to the stage dir, nothing else changes):

```
Total Assertions Checked: 268      (was 72 — the suite only runs its deeper
Passed Assertions       : 263       assertions once the files are found)
Failed Assertions       : 5
```

The two "file exists" failures disappear, which confirms they were layout-only.
Three "extracted N" failures remain (as above), and the now-reachable FNT
assertions surface a **new, genuine** failure:

```
  [FAIL] editor: all advances within [width, width+4] (84 out of band) (Assertion 'badAdvance == 0' failed at …/cli_tests.cpp:475)
  [FAIL] editorsm: all advances within [width, width+4] (85 out of band) (Assertion 'badAdvance == 0' failed at …/cli_tests.cpp:475)
```

84 of the editor font's glyphs and 85 of `editorsm`'s have an advance outside
`[width, width+4]`. **Whether the assertion or the FNT parser is wrong is not
determined here** — this is recorded as an observation for whoever owns
`source/renderer/fnt_*`/`cli_tests.cpp`, not as a finding of this task. It is
the only thing in this document that looks like a real product defect, and it
was only reachable because of the differential staging.

---

## 5. Tier `gui` — exit 2, and why

```
$ … bash scripts/run-wine-smoke.sh --no-build --diag-log --tiers gui
==> 6c. TIER gui — launcher with no CLI args (the real GLUT/OpenGL editor)
    bounded to 25s; liveness sampled once per second
   process DIED ON ITS OWN after 6s with exit 2
```

Verbatim stdout/stderr (`logs/tier-gui.log`):

```
…fixme:keyboard:NtUserActivateKeyboardLayout Changing user locale is not supported
2026-10-26 04:44:13.075 wine[33663:13915424] +[IMKClient subclass]: chose IMKClient_Modern
05c4:fixme:win:RegisterTouchWindow hwnd 0072009C, flags 0x3 stub!
GL_VENDOR:                   Apple
GL_RENDERER:                 Apple M2
GL_VERSION:                  2.1 Metal - 89.3
GL_SHADING_LANGUAGE_VERSION: 1.20
Alt+Enter:  toggle fullscreen mode
CTRL+H / CTRL+F2: toggle overlay wireframe
F3:         toggle clip
F4:         show/hide cursor
PageUp:     increase move speed
PageDown:   decrease move speed
Left/Right: change roll
2026-10-26 04:44:14 [ERROR] shader "…/editor/shaders/41/skydome.vert" compile error: ERROR: 0:6: '' :  version '410' is not supported
ERROR: 0:6: '' : syntax error: #version
ERROR: 0:14: 'layout' : syntax error: syntax error
```

The editor's own log (`scratch/wine-smoke/run/igi1ed.log`, produced only
because of `--diag-log`) shows the full ordering:

```
[INFO] [Config] Loaded from memory-decompiled: qedconfig.qvm
[INFO] [Config] Loaded event bindings from qedkeybindings.qsc (154 lines)
[INFO] [Main] Config initialized. Logging enabled: TRUE, Debug: TRUE
[INFO] IGI Editor Initializing...
[ERROR] shader "…/editor/shaders/41/skydome.vert" compile error: ERROR: 0:6: '' :  version '410' is not supported
```

### What this establishes

* **A window was created.** `fixme:win:RegisterTouchWindow hwnd 0072009C` reports
  a non-null Win32 HWND being registered; the GL strings below it prove a GL
  context was created and made current.
* **It got as far as the renderer.** `main` printed the key-binding banner
  (`source/main.cpp:757-765`), which is *after* `GL_Init()` succeeded and
  *before* `g_app.Init()`.
* **It failed in `App::Init()`**, whose first action is `renderer_.Init()`
  (`source/app.cpp:140-142`) — returning `false` there is exactly the
  `return 2` at `source/main.cpp:770-772`. The app's own log puts the shader
  failure inside `IGI Editor Initializing...`, i.e. inside that call.
* **The trigger is the GL context version.** Wine reported
  `GL_VERSION: 2.1 Metal - 89.3`, `GLSL 1.20`. `editor/shaders/41/skydome.vert`
  starts with `#version 410 core`. A 2.1 context cannot compile it.
* **No `igi1ed.log` in the shipped configuration** — `QEDLogs(FALSE)` in
  `editor/qed/qedconfig.qsc:8`; this is why a failed GUI start looks like a
  silent death. Anyone debugging this on Windows should turn logging on first.

### Is it Wine's fault? No.

`scratch/wine-smoke/gl-probe.c` is a ~60-line Win32/freeglut probe that creates
one window in a requested mode and prints the driver's GL strings. Built with
the same i686 toolchain and staged with the same DLLs:

```
$ cd scratch/wine-smoke/glprobe
$ env -u DISPLAY "/Applications/Wine Stable.app/…/bin/wine" ./glprobe.exe legacy
PROBE mode=legacy
PROBE window=1
PROBE GL_VENDOR=Apple
PROBE GL_RENDERER=Apple M2
PROBE GL_VERSION=2.1 Metal - 89.3
PROBE GLSL=1.20
```

```
$ env -u DISPLAY "…/bin/wine" ./glprobe.exe core32      # glutInitContextVersion(3,2) + CORE
PROBE GL_VERSION=4.1 Metal - 89.3
PROBE GLSL=4.10

$ env -u DISPLAY "…/bin/wine" ./glprobe.exe core41      # glutInitContextVersion(4,1) + CORE
PROBE GL_VERSION=4.1 Metal - 89.3
PROBE GLSL=4.10
```

All three exited 0. So Wine **can** provide GL 4.1 here; the legacy context is
what is capped at 2.1 (Apple's legacy profile on Apple Silicon). The editor
never asks for anything else: `grep -rn
"glutInitContextVersion|glutInitContextProfile|GLUT_CORE_PROFILE" source/`
returns **nothing**, and `source/main.cpp:686-697` calls only
`glutInitDisplayMode(GLUT_DOUBLE|GLUT_RGBA|GLUT_DEPTH|GLUT_STENCIL)` before
`glutCreateWindow`. On Windows with a modern driver the legacy path usually
yields a high compatibility context, which is why this does not bite there.

### Follow-up: what about a COMPATIBILITY profile? (the deciding experiment)

Wine **can** give GL 4.1 — but only as a *core* profile, and a core profile
forbids fixed-function GL. That matters because the editor needs **both**: its
shaders are `#version 410 core` / `#version 450 core`, and it makes **208
fixed-function calls** (`glBegin`, `glMatrixMode`, `glLoadIdentity`,
`glVertex3f`, `glOrtho`, `glEnableClientState`, `glTexCoord2f` …) in
`source/renderer/renderer_draw.cpp`, `renderer_rain.cpp`, `renderer.cpp` and
`app_ui.cpp`. The probe was extended to settle it. Five modes, same harness,
one run each (`scratch/wine-smoke/glprobe/prof-*.log`):

| mode | requested | context created? | GL_VERSION | GLSL | fixed-function `glGetError` |
|---|---|---|---|---|---|
| `legacy` | none | **yes** (`create_window_returned=1`) | `2.1 Metal - 89.3` | `1.20` | **all `GL_NO_ERROR`** |
| `compat32` | 3.2 + `GLUT_COMPATIBILITY_PROFILE` | **NO — freeglut aborts, exit 1** | — | — | not reached |
| `compat41` | 4.1 + `GLUT_COMPATIBILITY_PROFILE` | **NO — freeglut aborts, exit 1** | — | — | not reached |
| `core41` | 4.1 + `GLUT_CORE_PROFILE` | yes | `4.1 Metal - 89.3` | `4.10` | **all `GL_INVALID_OPERATION` (0x0502)** |
| `core32` | 3.2 + `GLUT_CORE_PROFILE` | yes | `4.1 Metal - 89.3` | `4.10` | **all `GL_INVALID_OPERATION` (0x0502)** |

Raw output for the two that decide it:

```
########## compat41: exit=1 ##########
PROBE mode=compat41 requested=version+COMPATIBILITY
freeglut (Z:\…\glprobe.exe): Unable to create OpenGL 4.1 context (flags 0, profile 2)

########## compat32: exit=1 ##########
PROBE mode=compat32 requested=version+COMPATIBILITY
freeglut (Z:\…\glprobe.exe): Unable to create OpenGL 3.2 context (flags 0, profile 2)
```

```
########## legacy: exit=0 ##########
PROBE create_window_returned=1
PROBE GL_VERSION=2.1 Metal - 89.3
PROBE GLSL=1.20
PROBE GL_MAJOR/MINOR=2.1
PROBE FF glMatrixMode(GL_PROJECTION)      -> 0x0000 GL_NO_ERROR
PROBE FF glLoadIdentity()                 -> 0x0000 GL_NO_ERROR
PROBE FF glBegin/glVertex3f/glEnd         -> 0x0000 GL_NO_ERROR
PROBE FF glEnableClientState(VERTEX_ARRAY)-> 0x0000 GL_NO_ERROR
PROBE FF glOrtho(0,1,0,1,-1,1)            -> 0x0000 GL_NO_ERROR
PROBE FF glTexCoord2f(0,0)                -> 0x0000 GL_NO_ERROR

########## core41: exit=0 ##########
PROBE create_window_returned=1
PROBE GL_VERSION=4.1 Metal - 89.3
PROBE GLSL=4.10
PROBE GL_MAJOR/MINOR=4.1
PROBE FF glMatrixMode(GL_PROJECTION)      -> 0x0502 GL_INVALID_OPERATION
PROBE FF glLoadIdentity()                 -> 0x0502 GL_INVALID_OPERATION
PROBE FF glBegin/glVertex3f/glEnd         -> 0x0502 GL_INVALID_OPERATION
PROBE FF glEnableClientState(VERTEX_ARRAY)-> 0x0502 GL_INVALID_OPERATION
PROBE FF glOrtho(0,1,0,1,-1,1)            -> 0x0502 GL_INVALID_OPERATION
PROBE FF glTexCoord2f(0,0)                -> 0x0502 GL_INVALID_OPERATION
```

**Conclusion: no usable context exists for this editor under Wine on this
host.**

* A **compatibility** profile at 3.2 or 4.1 cannot be created at all. Note the
  `profile 2` in freeglut's message — that is `GLUT_COMPATIBILITY_PROFILE`
  (0x0002), i.e. the request is being made correctly and the driver refuses it.
* A **legacy** context gives working fixed-function but only GL 2.1 / GLSL 1.20,
  so `#version 410 core` and `#version 450 core` cannot compile.
* A **core** context gives GL 4.1 / GLSL 4.10 but rejects every one of the
  editor's 208 fixed-function calls with `GL_INVALID_OPERATION`.

There is no overlap, so no context configuration makes the editor's renderer
work here. Most likely cause: Apple's OpenGL implementation offers only a 2.1
legacy profile and 3.2/4.1 core profiles — it has no compatibility profile —
and Wine passes the request through. (Interpretation, not directly measured.)

### The previously proposed core-profile change is WITHDRAWN

Requesting `GLUT_CORE_PROFILE` in `source/main.cpp` would trade a shader-version
failure for a total renderer failure: all 208 fixed-function calls become
`GL_INVALID_OPERATION`. Do not apply it.

A "request 4.1 compatibility, fall back to legacy if it fails" guard does not
work either, for a mechanical reason: with an unsatisfiable request freeglut
calls `fgError()` and **terminates the process** (exit 1) — it does not return
0 from `glutCreateWindow`. The probe never reached its
`create_window_returned=` line in either compatibility case. Such a fallback
would have to test driver capability *before* asking freeglut, or in a child
process.

**GUI verification is therefore impossible on this machine.** That is the
ceiling of what can be verified here; it says nothing about Windows with a
real driver, where the legacy path normally yields a high compatibility
context.

---

## 6. Failure modes hit, with causes

| # | Symptom | Cause | Status |
|---|---|---|---|
| 1 | `--run-tests`: 3 × "Extracted more than N" | Test counts direct children of `models/`; extractor nests `models/level1/` (`cli_handler.cpp:155-157,183,205` vs `cli_tests.cpp:359,369`). Files really are there (415/393). | diagnosed, not fixed (test bug) |
| 2 | `--run-tests`: 2 × `assets\editor\qed\*.fnt` missing | Paths are CWD-relative; the launcher `cd`s to the exe dir which has `editor/`, not `assets/editor/`. Proven by the `assets`-symlink differential. | diagnosed, not fixed (test bug) |
| 3 | FNT `badAdvance` 84/85 out of band | Only reachable after #2 is worked around. Advance outside `[width, width+4]` (`cli_tests.cpp:475`). Parser or assertion is wrong; undetermined. | **open observation** |
| 4 | GUI: silent death, exit 2, no log | `QEDLogs(FALSE)` in the shipped `qedconfig.qsc`; renderer init failure is logged at INFO-gated level. | diagnosed; `--diag-log` works around it |
| 5 | GUI: shader `version '410' is not supported` | Editor gets a legacy GL 2.1 context; shaders need 4.1. A 4.1 *core* context is obtainable but rejects all 208 fixed-function calls; a *compatibility* context cannot be created at all. No usable context exists here. | **root cause; no fix available on this host (see §5)** |
| 6 | i686 link overwrites the x64 artifact | `CMakeLists.txt:227,254-256` pin both architectures to `bin/Release/igi1ed.exe`. | handled by backup/restore in the script |
| 7 | `cmake --build` fails on `source/menu/menu_ui.cpp` | A concurrent teammate's in-flight edit (`BuildContext` had an uninitialised reference member) during the first canonical build attempt. | transient, not mine; resolved by its owner, and worked around meanwhile with `IGI_WINE_EXE` + a saved artifact |

---

## 7. What this run does and does not prove

**Proves**

* The i686 (Win32) target **links** on this host with the machine-local
  `third_party/GL/lib/x86` import libraries, and produces a PE32 i386 image.
* That image **loads and executes under Wine**: process start, MinGW runtime
  DLL resolution, C++ runtime, stdio, `Config::Init()`, GLUT window creation,
  a current GL context, and `GL_Init()`.
* The editor's own parser/extractor code runs on Windows under Wine against
  the retail install: 67 of 72 assertions pass, including full QVM
  decompile→recompile round-trips and level 1–3 end-to-end loads.
* The GUI start **fails at renderer initialisation because the GL context is
  2.1** and the shaders require 4.1 — evidenced by the app's own log.
* **No context configuration can fix that on this host** (§5): 4.1 is available
  only as a core profile, which rejects all 208 fixed-function calls with
  `GL_INVALID_OPERATION`, and a compatibility profile cannot be created at all
  (freeglut: `Unable to create OpenGL 4.1 context (flags 0, profile 2)`).

**Does not prove**

* That the editor works. **It does not start.** No level was ever rendered,
  no frame was drawn, no editor feature was exercised.
* That the x64 build behaves the same — only the i686 binary was run.
* That any context configuration would make the editor start here — §5 shows
  none exists.
* Anything about Windows with a real GPU driver. Everything here is
  Wine-on-macOS-on-Apple-Silicon.
* That the three non-GL assertion failures (§6 #1, #2, #3) reproduce on
  Windows; #1 and #2 are CWD/layout-dependent, #3 is unexamined.

---

## 8. Artifacts

```
scripts/run-wine-smoke.sh                     the reproducible gate
scratch/wine-smoke/artifacts/i686-igi1ed.exe  the smoke-tested binary
                                              sha256 7365d8c0…99b59
scratch/wine-smoke/logs/wine-smoke.log        combined transcript of the last run
scratch/wine-smoke/logs/full-run.out          full run, build + help,tests,gui
scratch/wine-smoke/logs/canonical-run.out     tiers-only run (earlier artifact)
scratch/wine-smoke/logs/diag-run.out          diagnostic run (gui, --diag-log)
scratch/wine-smoke/logs/tier-help.log         tier 1 verbatim
scratch/wine-smoke/logs/tier-tests.log        tier 2 verbatim
scratch/wine-smoke/logs/tier-gui.log          tier 3 verbatim
scratch/wine-smoke/logs/i686-build.log        the link
scratch/wine-smoke/glprobe/                   GL context probe + its output
scratch/wine-smoke/glprobe/prof-{legacy,compat32,compat41,core41,core32}.log
                                              raw per-mode probe output (§5)
scratch/wine-smoke/gl-probe.c                 probe source
scratch/wine-smoke/backup/x64-igi1ed.exe.bak  pre-existing x64 artifact, restored
```

Nothing under `source/`, `tests/`, `assets/` or `CMakeLists.txt` was modified
by this task; the only tracked-tree addition is
`scripts/run-wine-smoke.sh`. `bin/Release/igi1ed.exe` was restored to its
pre-run content (`sha256 7a0a7d98…4f5`).
