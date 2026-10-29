# Architecture

This is the map of the **code**: the layers, what owns what, and how a frame is produced.
Its companion is [`docs/igi-re-parity/`](igi-re-parity/), which is the map of the **engine
behaviour** the code reconstructs — one audit per subsystem, each citing IGI-RE as
function + address + file:line.

Scope of the line numbers: they are pinned to commit `7b281355`, where
`source/renderer/renderer_draw.cpp` is 4258 lines. Your working tree may be ahead of that
commit; line numbers drift, and every range below names the symbol it belongs to, so
re-derive the map rather than trusting a range once the file has changed. The same pin
applies to the `CMakeLists.txt` line references below — the target or symbol named beside
each is the durable anchor:

```bash
grep -n '^[A-Za-z].*(' source/renderer/renderer_draw.cpp   # top-level definitions
grep -n '// ──' source/renderer/renderer_draw.cpp           # the section banners
```

---

## 1. The layers at a glance

```
main.cpp                      CLIHandler::IsCLICommand / Process   g_app.Init
   │                                   │                              │
   │ headless modes                    │                              ▼
   │  --smoke-level  ──► cli/smoke_level.cpp                     ┌──────────┐
   │  --verify-level ──► cli/verify_level.cpp                    │   App    │
   │  --run-tests    ──► cli/cli_tests.cpp                       └────┬─────┘
   │  --extract-level──► cli/asset_extractor.cpp                      │ owns
   │                                                                  │
   │                        ┌─────────────────────────────────────────┼───────────────┐
   │                        ▼                     ▼                   ▼               ▼
   │                   Renderer              Level              IGIBridge      runtime/
   │                        │                 │                    │          (rules +
   │                        │                 │                    │           session)
   │                        │                 ▼                    ▼
   │                        │            LevelObjects ────► utils_igi1conv ──► igi1conv.exe
   │                        │            Terrain                              (all file
   │                        │            FlatSkyLayer                          conversion)
   │                        ▼
   │                 per-pass renderer modules
   │                 (objects / terrain / splines / rain / smoke / skydome / …)
   ▼
 igi_mcp  (source/mcp/, separate binary)
```

| layer | lives in | responsibility |
| ----- | -------- | -------------- |
| entry / modes | `source/main.cpp`, `source/cli/` | parse argv, dispatch the GL-free modes before any GL init, otherwise hand control to `App` |
| application | `source/app*.cpp` (11 files) | window, input, frame loop, editor state, and the wiring of every subsystem |
| level data | `source/level/` | QVM/QSC parsing, the object model, terrain, save/load |
| rendering | `source/renderer/` | GL resources and the per-frame passes |
| engine rules | `source/runtime/` | the reconstructed engine behaviour, as pure tested arithmetic, plus the runtime world/session |
| menu | `source/menu/` | the retail in-game menu reader/model and its writer |
| tooling | `source/mcp/`, `source/cli/` | the MCP server binary and the headless commands |
| tests | `tests/` | Google Test suites (`igi_tests` on Windows, a host subset natively) |

Build targets: `igi-editor` (`CMakeLists.txt:145`), `igi_tests` (`:347`), `igi_mcp`
(`:726`), plus four small single-suite targets (`:676-725`).

---

## 2. Entry points and the GL-free modes

`source/main.cpp` sets up GLUT and then, **before** any GUI or folder setup, intercepts the
headless commands (`source/main.cpp:582-585`):

```cpp
if (CLIHandler::IsCLICommand(argc, argv)) {
  g_isCLIMode = true;
  int result = CLIHandler::Process(argc, argv);
```

The dispatch table is `source/cli/cli_handler.cpp:18-20,87-160`: `--help`, `--run-tests`,
`--extract-level`, `--verify-level`, `--smoke-level`. Everything else falls through to
`g_app.Init(argc, argv)` (`source/main.cpp:775`).

These modes matter beyond convenience: they are the only way to exercise the loader and the
parsers on a host whose GL is 2.1, because the GUI needs OpenGL 4.1 compatibility
(`scripts/e2e-headless.sh:8-10`). What each proves, and what it does not, is in
[`docs/TESTS.md`](TESTS.md) and `scripts/e2e-headless.sh:14-29`:

| mode | what it does | what it does **not** do |
| ---- | ------------ | ----------------------- |
| `--smoke-level --level N` | loads `objects.qvm` → QSC → `LevelObjects`, resolves every placement's model and every texture through the renderer's own resolution path | never parses a mesh, binds a texture or draws a frame |
| `--verify-level --level N --skip-launch` | decompiles `objects.qvm`, reports the QSC parse counts | never launches the editor; without a game-run log the AI comparison cannot run |
| `--run-tests` | the native parser assertions | — |

---

## 3. App — what it owns

`App` (`source/app.h:34`) is the composition root. Its members are the ownership graph
(`source/app.h:206-265`):

```cpp
Renderer renderer_;            // app.h:210  — every GL resource and pass
Level level_;                  // app.h:211  — the loaded level (objects + terrain)
ResModelSet level_res_models_; // app.h:212  — models packed in the level's .res
IGIBridge bridge_;             // app.h:213
Renderer::draw_params_s draw_params_; // app.h:214 — the per-frame render request
igi::GameplayHost gameplay_host_;     // app.h:219
std::optional<LevelObjects> runtime_level_objects_; // app.h:221 — gameplay's copy
igi::app_menu::EditorMenu pause_menu_;              // app.h:232
```

Two things are worth internalising:

**`App` hands the renderer a request, not the world.** `Renderer::draw_params_s`
(`source/renderer/renderer.h:418-442`) carries the view, the draw-part mask, the selected
and hovered indices, the level objects pointer, and two `std::function` callbacks that let
the renderer ask the terrain a question without depending on `Level`:

```cpp
std::function<int(double,double)>        terrain_id_at_world_xy_;  // renderer.h:437
std::function<bool(double,double,float&)> terrain_z_at_world_xy_;  // renderer.h:441
```

`App::Frame` fills those callbacks before drawing (`source/app.cpp:1257-1261`). This is the
seam that keeps `Renderer` from including `level.h`.

**One frame is `App::Frame` → `Renderer::Draw`.** The loop is GLUT idle-driven; `App::Frame`
(`source/app.cpp:1047`) updates the view and the level, then calls
`renderer_.Draw(draw_params_, task_tree_view)` at `source/app.cpp:1264` (editor path) or
`source/app.cpp:1743` (gameplay path), and finishes with the custom cursor and
`glutSwapBuffers()`.

`App`'s implementation is split by concern — `app.cpp` (init, window, frame loop),
`app_input*.cpp` (binding dispatch, keyboard, mouse), `app_view.cpp` (camera, snapping,
markers), `app_editor.cpp` (property/AI-script editing, undo/redo), `app_level.cpp`
(load/save, ATTA flush, export), `app_menu.cpp` / `app_pause_menu.cpp` (the menu model),
`app_lookup.cpp`, `app_ui.cpp`. Most of those files open with an `@brief` naming their
scope; `app.cpp` is the core and has no header comment. The list is a `file(GLOB)` result,
so a new `app_*.cpp` needs no build-file edit (`CMakeLists.txt:75`).

---

## 4. Level, LevelObjects and the loaders

The load path is a straight line, and it always starts from the shipped binary data, never
from a cached QSC (`source/level/level.cpp:112-166`):

```
<IGI>/missions/location0/levelN/objects.qvm        (retail binary)
        │  Level::DecompileObjects            level.cpp:175
        ▼
<exe>/editor/qed/temp/objects.qsc                  (decompiled text)
        │  QSC::Load                          level.cpp:137
        ▼
QSC* qsc_objects  ──►  Level::LoadStartPosInfo   level.cpp:140
                       Level::LoadFogInfo        level.cpp:143
                       Level::LoadSkydomeInfo    level.cpp:144
                       Level::LoadFlatSkyLayersInfo  level.cpp:145
                       LevelObjects::Load(this, qsc_objects)  level.cpp:157
```

- **`Level`** (`source/level/level.h:15`) owns the terrain (`Terrain terrain_`), the
  objects (`LevelObjects level_objects_`), the flat-sky layers and the level number, and
  exposes terrain queries (`GetTerrainZ`, `GetTerrainNodeId`) plus the editor's terrain
  brush (`EditorRaycastAndModify`).
- **`LevelObjects`** (`source/level/level_objects.h:211`) owns the flat
  `std::vector<LevelObject>` the whole editor passes around by reference — the tree view,
  the renderer, the picker and the savers all read the same vector. `Load` (:216) parses the
  QSC tree into it, `LoadRecursive` (:252) walks the task hierarchy, `SaveToQSC` (:231) and
  `SerializeObjectRecursive` (:253) write it back.
- **`LevelObject`** (`source/level/level_objects.h:34`) is one placement: identity
  (`name`, `modelId`, `taskId`, `type`), transform (`pos`, `rot`, plus the `original_*`
  copies used for change detection), and the per-task extras (spline waypoint parameters,
  SCamera lens/holder ids, ATTA proxy fields, …). Its comments cite the engine's own
  parameter tables at the point of each field — e.g. the spline args at
  `level_objects.h:91-105` (`SplineObj_Open` HOOK 0x502250).

Two consequences worth knowing before you change anything here:

1. **Its layout is baked into every object file.** `App` even holds a second copy for
   gameplay (`std::optional<LevelObjects> runtime_level_objects_`, `source/app.h:221`), so a
   change to this struct is the exact case `scripts/check-build-integrity.sh` exists for —
   see `docs/igi-re-parity/BUILD_GATE.md` §8.
2. **The parser is not the only loader.** Everything the CLI converter can do is delegated
   to the bundled `igi1conv.exe` through one shared spawner
   (`source/utils_igi1conv.h:1-10`, path built at `source/utils_igi1conv.cpp:23-25`); only
   the loaders that must run in-process every frame (MEF meshes, FNT fonts, QSC/QVM, terrain
   files) stay in the editor.

---

## 5. The renderer and its passes

`Renderer` (`source/renderer/renderer.h`) owns one member per pass
(`source/renderer/renderer.h:922-928`):

```cpp
Renderer_Skydome        skydome_;
Renderer_FlatSkyLayers  flat_sky_layers_;
Renderer_Terrain        terrain_;
Renderer_Objects        objects_;
Renderer_Splines        splines_;
Renderer_Rain           rain_;
Renderer_Smoke          smoke_;
```

and `Renderer::Draw` (`source/renderer/renderer_draw.cpp:395`) sequences them:

| order | pass | code |
| ----- | ---- | ---- |
| 1 | skydome | `skydome_.Draw` — `renderer_draw.cpp:435` |
| 2 | flat sky layers | `flat_sky_layers_.Draw` — `renderer_draw.cpp:440` |
| 3 | terrain | `terrain_.Draw` — `renderer_draw.cpp:445` |
| 4 | objects, buildings, props | `objects_.Draw` — `renderer_draw.cpp:458` |
| 5 | splines | `splines_.Draw` — `renderer_draw.cpp:467` |
| 6 | rain / snow | `rain_.Draw` — `renderer_draw.cpp:496`, rules in `runtime/rain_rules.h` |
| 7 | smoke particles | `smoke_.Draw` — `renderer_draw.cpp:525` |
| 8 | 3D graph nodes | `DrawGraphNodes3D` — `renderer_draw.cpp:537` |
| 9 | the 2D HUD overlay | `renderer_draw.cpp:539-3642` |

The pass files are split by concern, not by layer:

| file | responsibility |
| ---- | -------------- |
| `renderer.cpp` | GL init, shader tiers, uniform blocks, the `Renderer` shell |
| `renderer_draw.cpp` | the frame: the 3D pass sequence above **and** the entire immediate-mode 2D UI |
| `renderer_objects*.cpp` | one file per concern: `_mesh` (load/cache/lookup), `_texture` (DAT mapping), `_visual` (spheres, selection box, preview), `_atta` (building attachments), `_picking` (colour-id FBO), `_smoke` (the headless resolution probe), `_metadata` (JSON/QVM metadata) |
| `renderer_terrain.cpp`, `renderer_splines.cpp`, `renderer_skydome.cpp`, `renderer_flat_sky_layers.cpp`, `renderer_rain.cpp`, `renderer_smoke.cpp` | one pass each |
| `model.cpp`, `mef_native.cpp`, `fnt_parser.cpp`, `res_writer.cpp`, `tex_writer.cpp`, `dat_writer.cpp`, `graph_*.cpp`, `spline_geometry.cpp` | asset readers/writers |

Picking is **not** in `renderer_draw.cpp`; it is `Renderer_Objects::PickObjectAtScreen`
(`source/renderer/renderer_objects.h:148`), implemented over the colour-id FBO in
`source/renderer/renderer_objects_picking.cpp`. `renderer_draw.cpp` contains exactly one
pick — `Renderer::PickGraphNodeAtScreen` (:3902) for the navigation-graph overlay.

`Renderer::Draw` assumes the state `Renderer::Init()` created once at startup
(`source/app.cpp:147`): the shader program, the uniform blocks and the pass objects. The
GL-free `--smoke-level` path never enters `Draw` at all — it calls the pass objects'
resolution entry points directly (`source/cli/smoke_level.cpp:10-20`,
`source/renderer/renderer_objects_smoke.cpp`).

---

## 6. The runtime layer: the engine rules

`source/runtime/` holds the reconstructed engine behaviour and is the layer to look in
before you write engine logic of your own. It has two kinds of file:

- **rule modules** — `<feature>_rules.{h,cpp}`, pure arithmetic with every constant cited
  to IGI-RE. In `source/runtime/`: `rain_rules.*`, `anim_rules.*`, `spline_rules.*`,
  `heli_rules.*`, `lightmap_rules.*`, `cutscene_rules.*`, `elevator_rules.*`,
  `pickup_rules.*`. Placement/geometry rules live next to the data they describe instead:
  `source/level/collision_drop_rules.h`, `source/level/terrain_snap_rules.h`. The
  `*_policy.h` headers in `source/runtime/` are the same idea for a single decision rule
  (`auto_save_policy.h`, `editor_hover_policy.h`, `log_policy.h`, …). Rule modules are
  deliberately free of GL and of `windows.h`, which is what lets the host lane test them.
- **the runtime world/session** — `runtime_world.cpp` (2976 lines: the gameplay simulation
  the editor hosts), `runtime_session.*`, `runtime_renderer.*`, `simulation_scheduler.*`,
  and the feature systems built on the rules (`anim_player.*`, `smoke_sim.*`,
  `projectile_system.*`, `gameplay_host.*`, `audio_system.*`, …).

The convention is that a rule module is *either* wired into a caller *or* explicitly marked
as an audit that is not. Both states exist in the tree and the module header says which
(`source/runtime/rain_rules.h:4-10` says WIRED; `docs/igi-re-parity/RAIN.md:3` records the
audit state of the document). A correct module with no caller is a known failure mode here:
`cutscene_rules.*` was correct and unreachable until `0e0c3d9f` gave it a caller
(`git show 0e0c3d9f`).

How to add one: [`CONTRIBUTING.md`](../CONTRIBUTING.md) §6.

---

## 7. The menu layer

The pause menu's rows are **not** hard-coded. `source/menu/` reads the retail
`<IGI>/menusystem/ingamemenu.qvm` — the compiled script the engine's own in-game menu is —
into a definition, then into a presentation model (`menu_ui.*`), and the draw layer renders
the model's current screen while `App`'s `pause_menu_*` methods are the row actions
(`source/app.h:79-113`). The chain is:

```
<IGI>/menusystem/ingamemenu.qvm
   │ igi::MenuDefinition::FromQvmFile      source/menu/qvm_image.h:134
   │   (container parsing is QVM_Parse, source/level/qvm_parser.h — not duplicated)
   ▼
igi::MenuDefinition                        source/menu/qvm_image.h:122
   │
   ▼
igi::app_menu::EditorMenu                  source/menu/menu_ui.h:504
   │ igi::MenuUiBuildOptions / PauseMenuLayout / PauseMenuRowState
   ▼
Renderer::Draw's pause-menu block          renderer_draw.cpp:1742-2221
   ▲
   │ clicks/keys
App::HandlePauseMenuClick / MovePauseMenuFocus / ActivateFocusedPauseMenuRow
```

`menu_qvm_writer.*` is the save-back half: it turns the model back into the byte image the
engine's own menu scripts have (`source/menu/menu_qvm_writer.h:1-14`).

The retail frame, palette and geometry rules are pinned in
[`docs/igi-re-parity/PAUSE_MENU_STYLE.md`](igi-re-parity/PAUSE_MENU_STYLE.md) and
[`MENU_UI.md`](igi-re-parity/MENU_UI.md), and the values themselves live in
`source/runtime/pause_menu_*.h` (`pause_menu_layout.h`, `pause_menu_style.h`,
`pause_menu_rows.h`, `pause_menu_font.*`).

Note the deliberate asymmetry: the **rows** come from the retail model, the **strings** are
the editor's own. `App::LogPauseMenuDrawnText` (`source/app.h:89-93`) logs exactly the
strings the draw layer will put on screen, so a host that cannot show the window can still
prove what the menu shows.

---

## 8. MCP and the CLI

- **`igi_mcp`** (`CMakeLists.txt:726`, `source/mcp/`) is a separate binary: a JSON-RPC
  server (`mcp_json_rpc.*`, stdio and HTTP transports) exposing the editor's data model as
  tools (`mcp_tools_*.cpp`, one file per domain: objects, ai, player, runtime, audio,
  strings, cutscene, mission, graph, assets). It does not need the GUI. See
  [`docs/MCP.md`](MCP.md).
- **`source/cli/`** is the headless half of the editor: `cli_handler.cpp` (dispatch),
  `smoke_level*.cpp`, `verify_level*.cpp`, `cli_tests.cpp`, `asset_extractor.cpp`. These
  are the modes the acceptance lane drives.

Both are thin: they call the same loaders and rule modules the GUI does, which is why they
are usable as evidence about the loader.

---

## 9. How the parity work is organised

[`docs/igi-re-parity/`](igi-re-parity/) is the project's knowledge base about the retail
engine, and it is organised by subsystem, not by issue:

- **one document per engine subsystem**, named after the subsystem (`RAIN.md`, `SNOW.md`,
  `MENU_UI.md`, `PAUSE_MENU_STYLE.md`, `CUTSCENES.md`, `HELICOPTER.md`, `PICKUPS.md`,
  `SOUND.md`, `PLACEMENT_SPLINES.md`, `LIGHTMAP_BAKE.md`, `KEYBINDINGS.md`, …; 37 documents
  at `7b281355`).
- **a `Status:` line at the top** that says what is implemented, which rule module and test
  carry it, and what was *not* verified — e.g. `RAIN.md:3` "audit + extracted rule module,
  not wired", `HELICOPTER.md:3` "implemented in `source/runtime/heli_rules.{h,cpp}` and
  tested in …". Read the status line before trusting the body.
- **citations in one form**: `Function HOOK 0xADDRESS (src/path/file.c:line-line)`. The
  address is what a disassembler can check; the file:line is what a reader can read. The
  rule modules repeat the citation next to the constant
  (`source/runtime/rain_rules.h:424-425`).
- **two lane documents** among the subsystem docs: `BUILD_GATE.md` (build gate, host lane,
  build integrity) and `WINE_SMOKE.md` (the Wine lane). These describe *how to check*, not
  engine behaviour.
- **triage documents**: `ISSUE_TRIAGE_FULL.md` and `TRIAGE_REMAINING_ISSUES.md` map issues
  to subsystems and record what is still open.

The authority rule is in `AGENTS.md:105-111`: IGI-RE
(`/Users/haseeb-mir/Documents/Code/IGI-RE`) is the only authority for engine behaviour, and
a claim must cite function + address + line. open-igi (C#) is not an authority
(`source/runtime/rain_rules.h:12-14`). A build is not a test, and a test passing is not
behavioural equivalence — say which one was proved.

---

## 10. `renderer_draw.cpp` — the map

`source/renderer/renderer_draw.cpp` is 4258 lines and it is a god-file: it contains the 3D
pass sequence, the entire immediate-mode 2D UI, and the navigation-graph overlay. **A map is
what makes a god-file survivable** — you do not have to hold 4258 lines in your head, you
have to know which of the ranges below to open. Splitting the file is a separate question
with a separate cost (§11).

The file has two halves: `Renderer::Draw` (one enormous function, lines 395-3643) and the
navigation-graph overlay (3650-4258).

### 10.1 File-scope helpers (1-393)

| lines | symbol | what it does |
| ----- | ------ | ------------ |
| 1-23 | file header + includes | — |
| 24-32 | `HudSprite`, `g_hudSprites` | the retail `.spr` sprite cache |
| 34-91 | `GetOrLoadHudSprite` | `.spr` → `TEX_Parse` → `GL_RegisterTexture` |
| 93-110 | `DrawHudFrame` | draw one sprite frame |
| 112-128 | `DrawHudBarFill` | the health/ammo bar fill |
| 130-164 | `GetWeaponSpriteName` | weapon name → sprite name |
| 166-202 | `EnsureEditorFont`, `EnsureEditorSmFont` | load the two editor bitmap fonts |
| 204-219 | `EnsureRetailPauseFontTexture` | the retail menu font |
| 221-259 | `DrawRetailPauseText` | text in the retail menu font |
| 261-321 | `DrawFontText` | the main HUD text path |
| 323-372 | `DrawFontTextSm` | small variant (tooltips) |
| 374-393 | `UnprojectCursorToWorld` | screen → world ray for the terrain brush |

### 10.2 `Renderer::Draw` (395-3643)

| lines | section | what it draws |
| ----- | ------- | ------------- |
| 395-427 | prologue | profile clocks; unpacks `draw_params_` |
| 428-471 | **3D passes** | clear, viewport, skydome, flat sky, terrain, objects, splines |
| 473-507 | rain / snow | precipitation, suppressed inside building footprints |
| 508-529 | smoke | `Renderer_Smoke` emitters (#43) |
| 530-538 | overlay tail begins | the 3D navigation-graph node pass |
| 539-791 | **2D HUD prologue** | fixed-function state reset, font scale, the `draw_text` / `draw_text_sm` / `draw_text_sys` helpers, keybinding→string, font colour |
| 792-1080 | **task tree panel** | the TreeView HUD: recursion, folder icons, expansion toggles, scrollbar, selection highlight |
| 1081-1237 | hover tooltip | object under the mouse: model name lookup, team, world→clip placement of the label |
| 1238-1293 | terrain-edit overlay | 3D brush-radius rings + brush label |
| 1294-1302 | graph overlay hook | calls `DrawGraphOverlayInternal` |
| 1303-1446 | **terrain palette** | the 5 brush buttons + 2×2 radius/strength grid |
| 1447-1457 | watermark | centred version string |
| 1458-1498 | animation panel | the bottom-left animation status card |
| 1499-1739 | **insert-task picker** | the modal task list opened from the tree |
| 1740-1741 | note | SPR cursors are drawn by `App::DrawCustomCursor()` |
| 1742-2221 | **live pause menu** | the retail menu tree's current screen: frame, title band, palette, rows, spinners, glow (#74) |
| 2222-2853 | **property panel** | C2: the left-hand property editor for the selected object |
| 2854-2953 | find bar | C3: `Ctrl+F` |
| 2954-2992 | file dialog | save/load sub-task |
| 2993-3058 | autocomplete picker | `Ctrl+N`, right panel |
| 3059-3154 | model-ID picker | `Ctrl+O`, right panel |
| 3155-3599 | **in-game HUD** | reticle (3163), health bar (3183), weapon/ammo (3257), objective card (3315), status messages (3336), muzzle cue (3393), incoming-fire cue (3430), flashbang (3446), map computer (3461) |
| 3600-3642 | state restore | re-enables depth test, cull, lighting, texture 2D |
| 3643 | end of `Draw` | — |

### 10.3 Navigation-graph overlay (3650-4258)

| lines | symbol |
| ----- | ------ |
| 3650-3669 | `LoadGraphOverlayFile` |
| 3670-3677 | `GetGraphNodePos` |
| 3678-3686 | `SetGraphNodePos` |
| 3687-3698 | `SaveGraphOverlay` |
| 3699-3707 | `ScaleSelectedGraphNode` |
| 3708-3725 | `CreateGraphNode` |
| 3726-3739 | `DeleteSelectedGraphNode` |
| 3740-3768 | `AddGraphLinkStep` |
| 3769-3794 | `RemoveGraphLinkStep` |
| 3795-3800 | `NudgeSelectedGraphNode` |
| 3801-3806 | `AdjustSelectedGraphGamma` |
| 3807-3813 | `AdjustSelectedGraphRadius` |
| 3814-3821 | `AdjustSelectedGraphMaterial` |
| 3822-3828 | `ToggleSelectedGraphCriteria` |
| 3829-3835 | `GetSelectedGraphNode` |
| 3836-3901 | `DrawGraphNodePanel` (the left-side node properties) |
| 3902-3937 | `PickGraphNodeAtScreen` |
| 3938-3963 | `BuildGraphNodeInfo` (static) |
| 3964-4123 | `DrawGraphOverlayInternal` (nodes, edges, labels, tooltip) |
| 4124-4258 | `DrawGraphNodes3D` (the depth-tested 3D pass) |

### 10.4 Reading it without splitting it

- Looking for a **3D** behaviour? It is in 428-538 or in the per-pass file §5 names.
- Looking for a **panel, menu or overlay**? Use the table in §10.2 — the sections are in
  draw order, so the HUD block you see on screen is the one further down the file.
- Looking for **picking**? It is not here (`renderer_objects_picking.cpp`), except the
  graph-node pick at 3902.
- The draw-order comment at 530-532 explains why the overlay tail is inside the same
  profile window as the 3D passes: the `[RenderProfile]` numbers would otherwise not add up.
  If you move code across that boundary, you move the attribution with it.

---

## 11. Why the map, and not (yet) a split

Issue #50 proposes splitting this file into `renderer_ui.cpp` / `renderer_picking.cpp` /
3D passes. The map above is the part that was done; the split is deliberately deferred, and
the reasons are worth recording so the next person can revisit them rather than re-derive
them:

1. **The file is entangled by shared state, not by accident.** Everything after line 539
   runs inside one function body with one set of locals — the font metrics, the scaled text
   helpers, the viewport dimensions, the hover/selection indices and the GL state pushed at
   539 and restored at 3600. A split means turning those locals into parameters or a struct
   *before* any file moves, which is a behaviour-affecting refactor, not a mechanical move.
2. **The acceptance criterion for the issue is navigability**, and a map with a
   responsibility per range is what delivers that. "The file is smaller" is not the goal.
3. **`CMakeLists.txt` is a shared, contended file.** Adding new translation units to it
   while other work is in flight is how this branch was once left unbuildable
   (`CONTRIBUTING.md` §4). The `source/renderer/*.*` glob means the editor would pick new
   files up, but the *test* target's explicit list would still need edits, and the risk is
   not worth paying for a move that changes no behaviour.

If you do take the split on, do it as its own change: first extract the shared locals into
an explicit context struct with the drawing code unchanged, verify with
`bash scripts/e2e-headless.sh --fast` plus the Windows suite, and only then move text
between files.
