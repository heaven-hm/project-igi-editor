# Known Issues and Engine Limitations

This document tracks all known rendering bugs, engine limitations, and editor work-in-progress features for the Project IGI Editor.

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
* **x64 feasibility spike**: An x64 (`-A x64`) build is viable at the build-config level: `CMakeLists.txt` already selects `third_party/GL/lib/x64` (freeglut/glew32 `.lib`) and `assets/dlls/x64` runtime DLLs when `CMAKE_SIZEOF_VOID_P == 8`, and the freeglut+GLEW+OpenGL32 toolchain is architecture-agnostic. The remaining porting work is auditing pointer-width assumptions (file offsets cast to `int`, `DWORD`-sized `tellg` results, hand-packed struct reads) before flipping the default. Not implemented in this issue; tracked as a future structural fix.
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

## 📢 3. Reporting Other Bugs and Issues

If you encounter any other bugs, crashes, or rendering issues that are not documented here, please feel free to report them to the dev team!

* **🎮 Discord**: Message us directly at `Jones_IGI#3954` or join the modding community on the [Project IGI Discord Server](https://discord.com/invite/QpbQrRFAER).
* **📧 Email**: [DEFAULT_RECIPIENT_EMAIL](mailto:DEFAULT_RECIPIENT_EMAIL)
* **🌟 GitHub**: Create a detailed issue on the [project-igi-editor GitHub Repository](https://github.com/Jones-HM/project-igi-editor/issues) with reproduction steps and logs.
