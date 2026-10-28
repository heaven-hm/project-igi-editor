# Known Issues and Engine Limitations

This document tracks all known rendering bugs, engine limitations, and editor work-in-progress features for the Project IGI Editor.

---

## 0. Status as of 4.0.0-pre — the IGI-RE parity campaign

This release is **major** because the editor now reproduces the **original `igi.exe`'s own
behaviour**, read out of the decompiled game (IGI-RE), instead of approximating it. Credits for
that are in `CHANGELOGS.md` (4.0.0-pre): **SlickDaddy** and **BlankName** for the game natives,
and the decomp project.

**Closed by this campaign** — each with the engine citation in the code and in its commit:

| Was | Now |
|---|---|
| Mission status text read from the wrong archive (44 warnings/session) | `LANGUAGE:messages.res`, the engine's flat lookup (`StatusMessages_Load` HOOK `0x4868A0`) |
| Invented texture ids (`waypoint`; the AI `_argb8888` strip) | the engine's own ids; a zero-texture model binds nothing (`0xFFFF`) |
| ATTA sub-models borrowing the parent hull's textures | removed — the engine never copies texture lists between models |
| `.olm` reader dropping lightmap pages 1..n on 13 shipped files | the engine's real page layout, and each group binds **its own** page |
| The `.olm` accept test never called | applied on the live load path (`Lightmap_BuildLod` HOOK `0x523D40`) |
| Collision proxies fed to the terrain snap; off-terrain reported as a failure | proxies skipped; the engine never clamps (`Game_DropToGround` HOOK `0x4149B0`) |
| The game's own menu reachable from the pause menu | editor-only; `OpenGameMenu` refuses to push a retail screen |
| The startup loading bar never painting | fixed (the reshape callback did not exist yet during `App::Init`) |
| Fullscreen leaving half the screen black | fixed (the `-w`/`-h` re-apply was undoing the borderless switch) |
| 2534–5399 ms/frame | **~40 ms/frame** (memoised `.res` reads, pick cull, animation pose/clock split) |

**Still open, and stated rather than implied:**

* **§1b x64** — still links-only: no x64 `freeglut.dll`/`glew32.dll` exists to stage, so the
  runnable build remains the i686 one. Unchanged by this campaign.
* **Audio, cutscene UI and vehicle simulation** — the editor has **no such subsystems at all**.
  Those are new construction, not parity fixes.
* **No in-process lightmap baker** — a recorded decision, not a gap: the engine has **no `.olm`
  writer** (`.olm` appears three times in the decompilation, all read-side; `gLightmapBuilt` is
  never read), so "bake in-process and save" has no engine behaviour to be faithful to. See
  `docs/igi-re-parity/LIGHTMAP_BAKE.md` §9.
* **On-screen verification** — the GUI needs OpenGL 4.1 and the build host offers 2.1, so the
  rendering fixes above are proven by engine citations, unit tests, corpus measurements and the
  editor's own log lines, **not by looking at the screen**. A Windows viewport run is still the
  only confirmation for the visual ones.

---

## 🏔️ 1. Rendering and Model Issues

### 🎥 Camera Orientation
* **Symptom**: Camera orientations in certain views or cutscenes do not align correctly with the expected angles.
* **Root Cause**: Discrepancies in yaw/pitch/roll representations and matrix multiplication orders between the editor's camera viewport and the game's internal camera structures.
* **Status**: ✅ **Fixed** — the engine's Euler convention was recovered from the decompiled `Matrix_SetEuler` (`src/math/matrix.c:99-110`, `HOOK 0x4B38E0`): `R = Rz(az) · Ry(ay) · Rx(ax)`, row-major, with `Mul(a,b)` computing `a·b`. A tree-wide sweep found **ten** sites that disagreed with it, not just the viewport: six in the render path, plus the runtime ladder-parent orientation (`app.cpp`), the door slide offset (`app.cpp`, oracle `Door_Place` `src/door/door.c:121-138`), the promoted-ATTA MEF write-back (`app_level.cpp`), the "centre on object" mesh-centre offset (`debug_command_manager.cpp`), and an `Rz·Rx·Ry` **decomposition** in `app_editor.cpp`. All now use `source/renderer/igi_transform.h`, which carries both directions of the conversion (`IgiEulerRotationMatrix`, `IgiMatrixToEuler` — the latter a transcription of the engine's own `Matrix_GetEuler`, `HOOK 0x4B3C50`). `tests/test_igi_transform.cpp` (9 tests) transcribes the engine's matrix code and checks the helpers against it, including a case that fails on the pre-fix extraction. Evidence: `docs/igi-re-parity/TRANSFORM_PIPELINE.md`.

### 📏 Wire Width
* **Symptom**: Fence wires and secondary wire meshes render with incorrect thicknesses or fail to scale dynamically based on viewport distance.
* **Root Cause**: ~~Platform line-width rendering limits and lack of shader-based variable wire width scaling.~~ **Refuted.** There is no distance-dependent wire width and no `glLineWidth` anywhere in the wire path — a wire is a *stretched rigid model*: `Wire_ParamsChanged` (`src/props/wire.c:160-191`) places it at the anchor midpoint, sets local +Y along `end − start`, and writes only `obj.scale[1] = length · kWireScale` (`kWireScale = 1/4096`, `wire.c:320-321`). The editor drew the unstretched model at the start anchor with a uniform scale.
* **Status**: ✅ **Fixed** — the wire stretch is implemented, and the zipline is now exposed as what it actually is (`Wire` with slope > 2°, `Wire_IsSloped` `wire.c:304-317`), not a separate task type. See `docs/igi-re-parity/PROPS_WIRE_INTERACTABLES.md`.

### 🧍 Character-model material precedence — fixed
* **Symptom**: A few AI/character models (notably `001_02_1`) render with textures on the wrong body parts in the **editor viewport** — e.g. a clothing/sign texture appears on the face/neck/hands. In-game these render correctly (editor-preview only). Most character models (`004_02_1`, `008_01_1`, `009_01_1`, `013_01_1`) are unaffected.
* **Root Cause**: The editor resolved a model's textures through every archive the session had indexed, so a same-named image in an unrelated archive (the shared `common` archive, or another level's) won over the archive the game uses. IGI-RE resolves a model's textures from exactly **one** archive — the level that owns the model file (`src/model/model.c:233-235` `.mtp`, `:251-261` `textures/<name>.res`, `:450-461` `MTP_TEXF`, `:300-306` the `0x2000` table rule); there is no cross-level search and no name-based fallback. Measured on the retail install: `001_02_1.tex` is 128×32 in `common` and 128×128 in `level7`, and pre-fix a level-2 or level-4 preview bound a different image for **11 of 26** material-slot lookups. The "two render/vertex sets" theory previously recorded here was disproved by the task-2 parity audit (`docs/igi-re-parity/MEF_DUAL_SET.md`).
* **Status**: ✅ **Fixed** in `source/renderer/renderer_objects_texture.cpp` and `source/renderer/model_texture_resolution.{h,cpp}` (owner-first archive ordering). See `docs/igi-re-parity/CHARACTER_TEXTURE_RESOLUTION.md`; regression suite `tests/test_character_texture_resolution.cpp` (red before, green after).
* **Note**: Unrelated to the `001_02_1` *import* texture bug
  (`docs/ISSUE_FIXES.md` → `IGIED-FOREIGN-MODEL-TEXTURES-001`, fixed and
  verified: common-archive source bundle + session-staleness + RES replace;
  E2E `001_02_1` into levels 1–5 all PASS with 13/13 byte-identical textures).
  The import path already used the owner-qualified lookup, which is why only the
  viewport preview showed the symptom. On-screen confirmation still requires a
  Windows viewport run and has not been done.

---

## 1b. 32-bit address-space ceiling and x64 feasibility

* **Symptom**: Lightmap auto-load and other large contiguous allocations (200+ MB `.res` archives, many `.olm` lightmap textures) can exhaust the 32-bit process address space, especially after in-session level switches fragment it. Level 3+ with lightmaps on historically crashed here, which is why `QEDLightmaps` defaulted to OFF.
* **Root Cause**: The editor builds only as a Win32 (`-A Win32`) process, capping usable contiguous memory near ~2 GB. The `.res` streaming reader (`RES_ForEachEntry`) and the `.olm` lightmap loader (`olm_texture.cpp`) already read chunk-by-chunk to keep peak memory to one entry/texture, which removes the crash without changing the build target.
* **x64 feasibility spike**: An x64 (`-A x64`) build is viable at the build-config level: `CMakeLists.txt` already selects `third_party/GL/lib/x64` (freeglut/glew32 `.lib`) when `CMAKE_SIZEOF_VOID_P == 8`, and the freeglut+GLEW+OpenGL32 toolchain is architecture-agnostic. The remaining porting work is auditing pointer-width assumptions (file offsets cast to `int`, `DWORD`-sized `tellg` results, hand-packed struct reads) before flipping the default. Not implemented in this issue; tracked as a future structural fix.
* **Correction — x64 links but does not run here**: the paragraph above previously also claimed `assets/dlls/x64` runtime DLLs exist. They do not: `assets/dlls/` is absent from this repository entirely (`ls assets/dlls` -> no such directory; `CMakeLists.txt:265-273` guards each copy with `if(EXISTS ...)` for exactly that reason), and the only freeglut/glew DLLs anywhere on the build host are **PE32 i386** — `assets/dlls/x86/{freeglut,glew32}.dll` in the sibling checkout, and `/Users/haseeb-mir/Downloads/IGI/{freeglut,glew32}.dll`. So the x64 gate proves *compilation and linking*, not runnability: launching the x64 `igi1ed.exe` under Wine fails at loader time with `err:module:import_dll Library freeglut.dll ... failed (error c000007b)` and `status c0000135` (bad image format / module not found), because no x64 build of those two DLLs exists to stage. A runnable build therefore means the **i686** target, whose matching DLLs do exist — the only blocker there was that the x86 import libraries `third_party/GL/lib/x86/{libfreeglut,libglew32}.a` are untracked (the original repository's `.gitignore` has a blanket `x86/` rule). See `docs/igi-re-parity/WINE_SMOKE.md`.
* **Note**: With the streaming `.olm` loader, `QEDLightmaps` no longer needs to stay OFF purely to avoid crashing level 3; it remains OFF by default as a conservative opt-in, and the Escape-menu checkbox turns it on without crashing.

## 🛤️ 2. Splines and Placements

### 📍 Missing Position
* **Symptom**: Certain objects or entities are loaded without valid coordinates, defaulting to the map origin or failing to render.
* **Root Cause**: ~~Unmapped coordinate offsets in the parsed binary QSC/QVM level files for specific object classes.~~ **Refuted.** The parser is not at fault: `LevelObjects::LoadRecursive()` simply had no dispatch branch for those task types, so its final `else` read nothing but the task id. Measured over the 14 retail scripts: 2438 of 2944 `Task_New` lines take that path, and 1757 of them belong to types that genuinely carry a world position at args 3/4/5 (`TerrainLightMap` 813, `EditRigidObj` 582, `EditCamera` 111, `StatusMessage` 73, `Building` 57, …). Because `SnapObjectsToTerrain()` runs right after load, those objects were then snapped to the terrain height **at the origin** and written back that way on save — silent level corruption, not just a display bug.
* **Status**: ✅ **Fixed** — the fallback reads args 3/4/5 as a candidate and validates it against the level's own `Task_DeclareParameters` (`("Position","ObjectPos")` as the first parameter), reverting types that merely have numbers there (`LightmapInfo`, `Dirlight`, `FlatSky`, `PatrolPathCommand`, …). See `docs/igi-re-parity/DECLARED_POSITION_FALLBACK.md`.

### 🎬 Train Placements in Cutscenes
* **Symptom**: Trains or railway carriages appear misplaced, misaligned, or floating during cutscene playback.
* **Root Cause**: ~~The engine uses hardcoded spline overrides and coordinate offsets specifically during cutscenes, which are not currently synchronized with the editor's standard object placement.~~ **Partly refuted.** There is no cutscene-specific override: the train is placed by the same spline evaluation as the editor's, and the decompiled `SplineObj_GetPoints` (`src/tagitem/spline.c:242-255`) *loops* the authored distance rather than clamping it. The real defects were in the editor's own placement maths: a single-shot wrap at `source/app_lookup.cpp:448`, polyline sampling instead of the engine's Hermite curve (`:415-422`), and a hardcoded `WAGON_SPACING = 52756.0` (`:466`).
* **Status**: ✅ **Fixed** — all three sites now run on the IGI-RE-derived helpers (`WrapRailDistance`, `PlaceTrainOnRail`, consist chaining), with red/green proof: swapping `WrapRailDistance` back to the old single-add-and-clamp arithmetic fails 3 of 12 tests. See `docs/igi-re-parity/PLACEMENT_SPLINES.md` §11-15.
* **Measured impact (corrected)**: an earlier estimate of "95 m" in §3.2 of that doc was wrong — it came from a mis-counted waypoint list and a chord-sum length. Rail 1500 has **11** waypoints and an engine length of **3,928,793.21** units; the retail `start_dist = -4045627.5` is more than one rail length back, so the engine's wrap loop runs **twice** and the old code left the train clamped at arc 0 — a **394 m** error. The other three retail trains sit inside one rail length, which is why the bug hid. Pinned by `RetailLevel1Train1499NeedsMoreThanOneWrap`.
* **Open approximation (documented, not hidden)**: the car length is a named constant (`kDefaultCarLength = 52756.0`) because the editor does not parse the model's local points 0/1 that the engine uses (`src/missile/train.c:142-167`); a waypoint's own `NumAreas` (arg 11) is likewise unloaded, measured at 0.008% length difference on rail 1500.
* **Align / Flip on waypoints — behavioural but unreachable in shipped data**: `Align` forces the rigid branch (`src/tagitem/spline.c:769,80-81`) *and* `Waypoint_Straighten` (`:686-746`) moves intermediate waypoints and overwrites their orientation; `Flip` mirrors the span (`src/sound/segsetup.c:230`, `src/shadow/spline.c:48-51`). Scanned across the installed corpus: LEVEL1 has 19 `SplineObjWaypoint`, all `(Align, Flip, autoOrient) = (FALSE, FALSE, FALSE)`, and LEVEL2/LEVEL4 have no spline objects at all — so no retail level can be drawn wrong. It becomes a real gap only when the editor is used to *author* rails (the inspector can already toggle both). Logged, not implemented; `PLACEMENT_SPLINES.md` §15.

---

## 1c. The GUI cannot start on a legacy OpenGL context — a host ceiling, not a Windows defect

* **Symptom**: On a host that hands the process a legacy context, the editor creates its window and GL context, prints its banner, then exits with status 2 before drawing anything. Its own log names the cause:
  `[ERROR] shader ".../editor/shaders/41/skydome.vert" compile error: ERROR: 0:6: '' : version '410' is not supported`.
* **Root Cause**: `source/main.cpp:686-687` calls only `glutInit` + `glutInitDisplayMode`, so it gets whatever context the driver defaults to. The shaders under `assets/editor/shaders/{41,45}` are `#version 410 core` (13 files) and `#version 450 core` (13 files). Where the default is a legacy context (`GL_VERSION 2.1 / GLSL 1.20`), shader compilation fails, `Renderer::Init()` returns false (`source/app.cpp:140-142`), `App::Init()` returns false, and `main()` returns 2 (`source/main.cpp:770-772`). On real Windows a legacy context normally resolves to a high compatibility context, which is why this does not bite there and the app is Windows-only in practice.
* **Measured: no *context configuration* can satisfy the renderer on this host — but that is not the same as "no source change can".** The editor needs **both** GL >= 4.1 (for the shaders) **and** fixed-function (it issues **208** fixed-function calls — `grep -rn "glBegin\|glMatrixMode\|glLoadIdentity\|glVertex3f\|glOrtho\|glEnableClientState\|glTexCoord2f" source/ --include=*.cpp` -> 208, in `renderer_draw.cpp`, `renderer_rain.cpp`, `renderer.cpp`, `app_ui.cpp`), i.e. a **compatibility** profile at 4.1+. Probing all five modes with the same i686 toolchain and freeglut on this host:

  | requested | context created | GL_VERSION | GLSL | fixed-function `glGetError` |
  |---|---|---|---|---|
  | none (legacy) | yes | 2.1 Metal - 89.3 | 1.20 | all `GL_NO_ERROR` |
  | 3.2 + COMPATIBILITY | **no — freeglut `fgError`, exit 1** | — | — | not reached |
  | 4.1 + COMPATIBILITY | **no — freeglut `fgError`, exit 1** | — | — | not reached |
  | 4.1 + CORE | yes | 4.1 Metal - 89.3 | 4.10 | all `GL_INVALID_OPERATION` |
  | 3.2 + CORE | yes | 4.1 Metal - 89.3 | 4.10 | all `GL_INVALID_OPERATION` |

  The two requirements have no overlap **in any single context request**, and `GLUT_CORE_PROFILE` is indeed the wrong fix: it does obtain GL 4.1, but every one of the 208 fixed-function call sites then fails. A guarded context request is also still unwritable — with an unsatisfiable request freeglut calls `fgError()` and **terminates the process**, so `glutCreateWindow` never returns a failure to test.
* **~~"No source change can make the renderer run on this machine."~~ REFUTED — see §1d.** That conclusion only followed while the shader `#version` was treated as fixed. The legacy 2.1 context *does* satisfy the fixed-function half (all `GL_NO_ERROR`), so the 4.1 requirement was entirely the shaders'; porting them to GLSL 1.20 removes it. The editor now gets past the `skydome` failure on a 2.1 context. `shaders/21` is selected automatically when `GL_Init()` sees no GL 3.0; see `docs/igi-re-parity/GL21_FALLBACK.md`.
* **Status**: ⚠️ **Superseded by the 2.1 fallback (§1d) on hosts that only offer a legacy profile.** The underlying observation stands: Apple's OpenGL offers a 2.1 legacy profile and 3.2/4.1 core profiles, with no compatibility profile, and the request is passed through. On Windows with a real driver the legacy path yields a high compatibility context, so the context request is deliberately left alone there. Reproduction, raw probe output and the per-mode logs are in `docs/igi-re-parity/WINE_SMOKE.md`; `scripts/run-wine-smoke.sh` reruns the whole thing.
* **What this costs the project**: GUI verification was believed impossible on such a host, so the ceiling for verifying editor *behaviour* was the compile gates plus the host-native suites plus the Windows binary's own parser tests (`--run-tests`: 67 of 72 assertions against the real retail install). With the 2.1 fallback the GUI can start here, but a fallback that *starts* is not a fallback that *renders identically*: the `noperspective` drop in the flat-sky layer is a known divergence, and any frame-dependent claim still needs to be checked rather than inferred.

---

## 1d. The OpenGL 2.1 fallback — how the GUI now starts on a legacy context

* **What it is**: when `GL_Init()` finds no GL 3.0, `g_gl_info.legacy_gl21_` is set, `source/main.cpp` selects the `shaders/21` GLSL 1.20 tier, and `source/renderer/gl_helper.cpp` repairs what a 2.1 context lacks. Full detail, including the probe results and the proof that the port is arithmetic-preserving, is in `docs/igi-re-parity/GL21_FALLBACK.md`.
* **Measured surface**: a probe compiled every GL entry point this source calls (110 names) against the real 2.1 context. **97 usable, 8 NULL**, and all 8 are handled: three VAO entry points repointed to their `APPLE_vertex_array_object` equivalents, three uniform-block entry points replaced by an emulation that re-uploads the block as plain uniforms, `glDepthRangef` widened to `glDepthRange`, and `glBindTextureUnit` already guarded by `support_version_45_`. Immediate mode needed nothing — it exists in 2.1 and was only removed in core 3.2+.
* **Cost**: one GLSL 1.20 tier of 13 files, twelve inline shader variants, and the compat block in `gl_helper.cpp`. The ~103 VAO call sites and 26 `glBindBufferBase` call sites needed **no edits**, because GLEW exposes both as function pointers that can be repointed once at init.
* **Status**: ✅ implemented; the shader tiers are proven to compile and link on the real 2.1 context. Whether the editor then starts **end to end** is a separate, separately-reported claim — do not read "the fallback works" as "the editor runs".

---

## 📢 3. Reporting Other Bugs and Issues

If you encounter any other bugs, crashes, or rendering issues that are not documented here, please feel free to report them to the dev team!

* **🎮 Discord**: Message us directly at `Jones_IGI#3954` or join the modding community on the [Project IGI Discord Server](https://discord.com/invite/QpbQrRFAER).
* **📧 Email**: [DEFAULT_RECIPIENT_EMAIL](mailto:DEFAULT_RECIPIENT_EMAIL)
* **🌟 GitHub**: Create a detailed issue on the [project-igi-editor GitHub Repository](https://github.com/Jones-HM/project-igi-editor/issues) with reproduction steps and logs.
