# Changelogs

## 3.3.0-pre — Auto-Save, Unified Undo/Redo & AI Script Hotkey Support

### ✨ New Features
- **Auto-Save System**: New pause-menu "Auto Save" row that displays current state ("Auto Save Enabled" / "Auto Save Disabled") and an interval spinner with `-`/`+` buttons (10s steps, 10s–3600s range). Toggle with `Ctrl+Shift+A`, increase/decrease interval with `Ctrl+Shift+]` / `Ctrl+Shift+[`. When enabled, the editor auto-saves the current level at the configured interval.
- **SaveState (Ctrl+W) & SaveObjectFile (Ctrl+S) read from qedkeybindings.qsc**: All save hotkeys are now dispatched via `DispatchEventBindings` using bindings loaded from `editor\qed\qedkeybindings.qsc` — no hardcoded keys.
- **AI Script Editor hotkey support**: `Ctrl+W` and `Ctrl+S` now commit the in-flight AI script edit and save the `.qvm` even when the AI script textbox is focused. The text editor pre-checks save bindings and commits the edit before letting the key reach the dispatcher.
- **Pause-menu Save button works while AI textbox is focused**: ESC closing the property panel now commits the AI script edit instead of discarding it.

### 🐛 Bug Fixes
- **Fixed: AI Script not saved via Ctrl+W / Ctrl+S**: The text editor block was intercepting all keys while the AI textbox was focused, so `Ctrl+W` was being inserted as a printable character instead of triggering `SaveState`. Now the editor checks for save hotkeys first, commits the AI edit, and lets the binding fire.
- **Fixed: ESC discarded AI script edits**: Pressing ESC while editing AI script text no longer discards the in-flight edit — it commits `prop_text_buf_` to `ai_script_text_` and sets `ai_script_dirty_=true`.
- **Fixed: Undo/Redo broke building ATTA positions**: After restoring the objects vector, ATTA proxy objects are now marked `modified=true` and `FlushAttaProxiesToMef()` rewrites the MEF binary. Previously the MEF kept post-edit local positions while the proxy's world pos reverted, causing the 3D view and saved level to disagree.
- **Fixed: Pause-menu AutoSave row not updating display**: The pause-mode `task_tree_view` struct was missing `auto_save_enabled_` and `auto_save_interval_seconds_` fields, so the renderer always showed defaults (`false`, `300s`) regardless of the actual state. Now both fields are passed in the pause-mode path.
- **Fixed: ResetLevel did not fully reset graphs/AI/textures**: ResetLevel now does a full folder replacement (`remove_all` + `copy recursive`) instead of in-place `overwrite_existing`, so deleted graph nodes, removed AI scripts, and any other stale files are all reverted. The backup is now always created on first level load (not gated on `enableBackup`) and captures the entire `missions\location0\levelX\` folder.
- **Fixed: AutoSave config not reloaded on startup**: `QEDAutoSaveEnabled` and `QEDAutoSaveInterval` were saved to `qedconfig.qsc` but never read back. The parser now restores both on launch.
- **Fixed: Terrain undo/redo didn't restore heightmap edits**: Added `SnapshotHMP()` / `RestoreHMP()` on `Terrain` (and a public pass-through on `Level`) that byte-copies the loaded HMP file body. Undo/redo now captures and restores the full heightmap buffer so brush edits can be rolled back.
- **Fixed: Editor created a `content` folder in the install path**: All asset cache stamps moved from `output_dir\content\cache` to `output_dir\editor\cache`; the terrain extract dir moved from `content\terrains` to `editor\terrains`. The editor no longer writes anywhere outside `editor\`.

### ⌨️ New Keybindings
| Event | Binding | Action |
|-------|---------|--------|
| `ToggleAutoSave` | `<Ctrl><Shift><A>` | Toggle auto-save on/off |
| `AutoSaveIntervalUp` | `<Ctrl><Shift><]>` | Increase auto-save interval by 10s |
| `AutoSaveIntervalDown` | `<Ctrl><Shift><[>` | Decrease auto-save interval by 10s |

### 🔧 Technical Changes
- **Unified `UndoState`**: Snapshots all editable state in one struct — objects (including ATTA proxy fields, spline data, lighting, scale), AI script (path/text/dirty), terrain HMP buffer, terrain mod options, and graph overlay (nodes/edges/visibility). One undo covers every edit type.
- **`Undo` / `Redo` re-flush ATTA proxies** after restoring the object list, so MEF binaries stay in sync with the proxy world positions.
- **`ResetLevel` always restores from full-folder backup** when one exists; the backup is created on first load of each level.
- **Cache and terrain extract dirs moved under `editor\`** so the install path stays clean.

---

## 3.2.0-pre — Graph Link Editing, Legacy Format & Edge Visibility

### ✨ New Features
- **Add/Remove Links (Two-Step Workflow)**: Navigation edges between graph nodes can now be added and removed interactively. Select node A, press `Alt++` to mark it as the link source (green double-ring indicator), select node B, press `Alt++` again to create the link. Use `Alt+-` to remove a link the same way.
- **Node Label Toggle**: On-screen node ID labels can now be toggled on/off with `Alt+L`. The title banner shows `[labels off]` when disabled.
- **Legacy Graph Format Support**: Added a parser, full writer, and position-patch saver for the undocumented alternate tagged graph format (1-byte type tags: `0x05`=int32, `0x06`=float, `0x08`=Vec3d, `0x09`=string, no magic number). This fixes the "bad magic" error that prevented level 8's `graph1.dat` and `graph7.dat` (and other legacy graphs) from loading and displaying.

### 🐛 Bug Fixes
- **Fixed: "Bad magic" error on level 8 graph1**: Graph files using the legacy tagged format (no `0xFFEEDDCC` magic) are now auto-detected and parsed transparently — both standard and legacy formats produce the same editable `GraphFile` and support full add/remove/edit/save.
- **Fixed: Links not visible / not above ground**: Edge lines were drawn at node Z (ground level), getting buried under terrain. Edges now render at the vertical centre of the node boxes (`z + H`) so they sit above ground and are clearly visible.

### ⌨️ New Keybindings
| Event | Binding | Action |
|-------|---------|--------|
| `AddGraphLink` | `<Alt><Plus>` | Two-step: mark source node, then link to target node |
| `RemoveGraphLink` | `<Alt><Minus>` | Two-step: mark source node, then unlink target node |
| `ToggleGraphNodeLabels` | `<Alt><L>` | Toggle on-screen node ID labels |

### 🧪 Tests
- Added 4 new tests for the legacy graph format: parse validity, node data sanity, write round-trip, and position-patch save.

---

## 3.1.0-pre — Visual 3D Graph Editor

- Added: Visual 3D Graph Editor (interactive 3D nodes, material color coding, and path/edge rendering via F3 overlay).
- Added: Binary parser, patch saver, and full writer serialization for AI navigation graph `.dat` files.
- Added: Config option `QGraphNodeSize` to control node box sizing.
- Fixed: Terrain sculpting vs object selection click conflicts, right-click triggers.
- Fixed: Syncing of level graph subdirectories to backup folders.

---

## 3.0.0 — igi1conv Integration, Qt Bundling, Module Refactor & Bug Fixes

This release is a major milestone: the standalone asset converter is now a first-class Qt application (`igi1conv`) living in its own repository, the editor codebase has been split into clearly separated modules, and a batch of code-review bugs has been resolved.

### 🔧 igi1conv — Standalone Asset Converter (Qt)
- **Dedicated repo**: `igi1conv` is developed at [project-igi-conv](https://github.com/jones-hm/project-igi-conv) and bundled prebuilt with each editor release.
- **Qt application**: Ships with a full graphical GUI mode and a headless CLI mode. The editor invokes only the CLI internally (`dat to-mtp`, etc.).
- **Full Qt package**: The entire runtime (exe + Qt5Core/Qt5Gui/Qt5Widgets/Qt5Svg DLLs, platform plugins, image formats) is bundled at `editor/tools/igi1conv/` — no separate Qt installation required.
- **v1.6.0**: Updated to `igi1conv` v1.6.0 with improved asset conversion accuracy.
- **Native MTP generation**: The editor delegates all `.mtp` generation to `igi1conv dat to-mtp`, which reproduces the original game `.mtp` byte-for-byte and fixes transparent/wrong-texture in-game issues.

### 🗂️ Module Refactor
- **renderer_objects** split into picking, mesh, visual, metadata, texture, and ATTA attachment subsystems.
- **app** split into `app_input` (mouse + keyboard + dispatch), `app_editor`, `app_view`, `app_lookup`, `app_level`, and `app_ui` modules.
- **terrain** implementation split into terrain_io, terrain_lod, terrain_mesh, and terrain_query.
- **LevelObjects** QSC serialization extracted into its own module.

### 🖥️ UI Improvements
- **Level Spinner**: Level number selector added to the pause menu for quick level switching.
- **Texture Panel Removed**: Texture list removed from the pause menu for a cleaner layout.
- **Editor File Verification on Launch**: Missing `editor/` directory files are detected and reported with a clear error dialog before the editor reaches the render loop.

### 🐛 Bug Fixes
- **Arrow Keys**: Arrow keys no longer move the camera unless `SHIFT+ALT` is held — fixes accidental camera drift during text input.
- **Weapon Orientation**: Corrected weapon/ammo model orientation so pickups display at the right angle in-game.
- **ESC Menu Re-open**: Fixed regression where pressing ESC a second time failed to reopen the pause menu.
- **Terrain Rings on Right-Click**: 3D brush rings now appear immediately when terrain is selected via right-click.
- **DAT Round-Trip Loss**: Fixed data loss when a DAT file was written back after read.
- **matCount Guard**: Added bounds check to prevent out-of-range material slot access.
- **`std::exit` replaced**: Replaced bare `std::exit` calls with proper cleanup paths.
- **VNAM Shadow**: Fixed variable shadowing bug in VNAM chunk parsing.
- **vert_info Overflow**: Corrected vertex info buffer overflow on large meshes.
- **JSON Parser**: Fixed edge-case crash in the embedded JSON parser on malformed input.
- **Boundary-Aware Texture Matching**: `009_01_1` can no longer accidentally grab `1009_01_1.tex`.

---

## 2.9.0 — New Terrain Editor, Foreign Models Support & UI Fixes
This release introduces a new Terrain editor, support for loading and adding foreign models from other levels, and critical bug fixes to the pause menu layout and viewport interaction.

### 🗺️ New Terrain Editor
- **Interactive Terrain Tools** — Enhanced level design capabilities with the new terrain editor interface and brush controls.

### 📦 Foreign Model Support
- **Cross-Level Assets** — Added the ability to import and add foreign models from different game levels directly into the current level.

### 🔧 Pause Menu & UI Bug Fixes
- **Terrain Options Expand/Collapse** — Fixed the list in Terrain Options in the Pause menu to properly expand and collapse when clicked.
- **Immediate 3D Rings Refresh** — When terrain is selected via right-click, the 3D brush rings now display immediately in the viewport without delay by forcing immediate frame redisplays.

---

## 2.8.0 — Inline AI Script Editor, Mini-Notepad, Autocomplete Overhaul & Find Fixes
This release delivers a full inline AI script editor embedded inside the property panel, a proper mini-notepad text editor with scrolling and arrow key navigation, a global autocomplete fix, and three keyboard shortcut corrections in the find system.

### 🤖 Inline AI Script Editor
- **Correct Task ID Resolution** — AI script path now uses the `HumanAI` child task's ID (e.g. `2203`) instead of the parent `HumanSoldier` ID, correctly pointing to `ai/2203.qvm`.
- **Path Textbox** — Editable single-line textbox showing the full `.qvm` path. Type a new path and press Enter to load and decompile that file. Horizontal scrolling keeps long paths visible.
- **Script Textbox** — Tall multiline textbox showing the decompiled QSC source. The orange "modified" label appears whenever unsaved edits exist.
- **Compile-on-Save** — AI script edits are compiled back to `.qvm` only when the user saves the level (F3 / Save), with round-trip validation. Errors are shown in the status bar; dirty flag stays set on failure.

### 📝 Mini-Notepad Text Editor
- **Vertical Scrolling** — Mouse wheel scrolls the script content. PageUp / PageDown jump a screen at a time.
- **Arrow Key Navigation** — Left/Right move the caret one character. Up/Down move by visual line, preserving the column position. Works correctly across `\n`-separated lines and wrapped lines.
- **Mouse Click Positioning** — Clicking inside the script box positions the caret at the character closest to the click, accounting for the current scroll offset.
- **Path Horizontal Scroll** — The path box scrolls horizontally as the caret moves past the visible area.
- **Proper `\n`-Aware Wrapping** — The `draw_edit_box` renderer now splits on `\n` characters AND wraps at the box width, showing each logical line correctly. Cursor is always on the right visual line.
- **Editable Appearance** — Both boxes now draw a dark background and a white/yellow border (yellow = active editing), matching the visual style of all other property fields.

### 🔡 Autocomplete Everywhere Fixed
- **Root Cause** — Field-ID sentinels for AI boxes are `-10`/`-11` (both `< 0`), which triggered a stale `prop_text_edit_field_ < 0` guard that blocked Ctrl+N, Ctrl+Space, and Ctrl+O in every field where they were hit. All guards now check `== -1`.
- **Ctrl+N / Ctrl+Space / Ctrl+O** — All autocomplete and picker paths now work in the AI script box, the AI path box, and all standard string fields.
- **Caret-Preserving Insert** — When Ctrl+N opens the keyword picker, the cursor position is captured (`picker_target_caret_`). Confirming a pick INSERTs the keyword at that exact position in the AI script box. Standard fields keep the existing full-replace behaviour.
- **DispatchEventBindings Paths Fixed** — `AutoCompleteTaskName` and `AutoCompleteModelName` event bindings also had the stale guard; both now fixed and save the caret on open.

### ⌨️ Find Shortcut Fixes
- **TaskFindTextInTask** — Rebound from `Ctrl+H` (ASCII 8 = Backspace, permanently blocked) to `Ctrl+Shift+X`.
- **TaskFindByTaskNote** — `Ctrl+Shift+N` was being silently swallowed by the Ctrl+N autocomplete intercept (both produce ASCII 14 in GLUT). Guard now checks `!shiftDown` so Ctrl+Shift+N correctly reaches `DispatchEventBindings`.
- **TaskFindAgain** — After finding the next match, `Ctrl+Shift+F` now scrolls the task tree so the found item is visible and highlighted.

---

## 2.7.0 - 3D Model Viewer, Autocomplete Task, Exact Keybindings & Task Tree Fixes
This release introduces an interactive rotating 3D Model Viewer for selected level assets, auto-complete for task inputs, and critical fixes for keybinding collisions and task subtree importing/exporting.

### 📐 3D Model Viewer
- **Real-time 3D Preview**: Renders a spinning 3D preview of the selected model inside the Model ID picker (`Ctrl+O`), loaded natively from MEF files.
- **Automatic Scaling and Orientation**: Preview models rotate automatically across dual axes and are scaled to fit the preview frame dynamically.

### 🔤 Task Autocomplete
- **Task Type Picker**: Added a dedicated sidebar autocomplete task type picker (`Ctrl+N` panel) with instant incremental search.
- **Inline Keyword Auto-complete**: Trigger keyword suggestions directly using `Ctrl+Space` for rapid task configuration.

### ⌨️ Exact Keybinding Validation
- **Modifier Exact Matching**: Dispatches event bindings only when key modifiers match exactly, preventing shortcuts like `Ctrl+C` from firing during `Ctrl+Shift+C` commands.
- **Autorotated Keybinding Config**: Preserves named configurations (`SaveSubTask`, `TaskMagicObjToggle`, etc.) directly from `qedkeybindings.qsc` instead of resetting them.

### 🔧 Task Tree & File Dialogs
- **Subtree Export & Import**: Save task subtrees to custom files and load them back onto target nodes dynamically.
- **Task Creation options**: Insert new tasks directly at the camera's coordinates or insert them at specific task tree locations.

---

## 2.6.0 - Properties Editor UI, Attachments, Splines & CLI Tools
This release introduces a new Properties Editor UI, support for editable Attachments, spline track fixes, and enhanced CLI tools for image and font atlas processing.

### 📋 Properties Editor & Attachments
- **Properties Editor UI**: Added a brand new User Interface for the Properties Editor.
- **Editable Attachments**: Added support for editing and configuring Attachments objects.

### 🛣️ Road Tracks & Assets
- **Road Tracks**: Fixed spline calculation and rendering issues for Road Tracks (`SplineObjs`).
- **Editor Fonts & Sprites**: Added custom Editor Fonts and Sprites assets.

### 🖼️ Image Conversion Utilities
- **Direct Format Conversion**: Added `--ToPng` and `--ToTga` arguments to convert individual `TGA` or `PNG` files instantly via the CLI.
- **PNG Encoding**: Integrated `stb_image_write.h` to natively write high-quality PNGs directly from the engine.

### 🔤 Font Output Improvements
- **FNT Atlas Export**: New `--export-png` flag for the `--fnt` parser extracts the internal bitmap texture of font files directly to PNG.
- **Glyph String Concatenation**: The `--fnt` info log now aggregates all active glyphs and prints them as a single string for quick character set verification.

### 🔧 Stability
- **Bug Fixes**: Fixed general bugs and improved overall editor stability.

---
## 2.5.0 - Property Panel Scrolling & Child Task Display
This release adds a scrollable property editor and displays weapon/ammo fields for soldier units.

### 📋 Enhanced Property Editor
- **Vertical Panel Scrolling**: Property panel now supports mouse-wheel scrolling for large task schemas, with a visual scrollbar indicator.
- **Child Task Fields Display**: Weapon, ammo, and AI sub-task fields now appear inline below parent task properties as read-only previews for quick reference.
- **Panel Scroll Reset**: Scroll position automatically resets when selecting a new object, preventing disorientation.

### 🎯 UI Improvements
- **Compact Scrollbar**: Minimal scrollbar thumb appears only when content overflows the visible panel area.
- **Keyboard-Free Navigation**: Intuitive mouse-wheel scrolling makes exploring large task hierarchies seamless without switching to the tree view.

---

## 2.4.0 - Position Editor Drag & All-Object Picking
This update fixes position editing when the cursor hits window edges and enables clicking collision-only meshes.

### 🖱️ Position Editor Edge Continuity
- **Stuck Cursor Handling**: When your mouse hits the window edge, the position editor now remembers your last drag direction and continues moving the object smoothly.
- **Per-Frame Delta Logic**: Switching from cumulative to per-frame drag delta prevents stalling when the cursor can't move further.
- **Seamless Manipulation**: No need to re-position your mouse—object keeps moving in the direction you were pushing.

### 🎯 Universal Object Picking
- **Collision-Mesh Fallback**: Vehicles, cargo containers, and other collision-only models are now clickable in the viewport, even without render geometry.
- **All Attachments Pickable**: MEF sub-models now serve as valid pick targets, allowing selection of child objects through any mesh surface.

---

## 2.3.0 - Font Toggle & Text Cursor Alignment
This release adds live font switching and precise text cursor positioning in property textboxes.

### 🔤 Font System Enhancements
- **Live Font Toggle**: New "Font: Editor / Font: System" button in pause menu switches between bitmap fonts at runtime without reloading.
- **Dynamic Font Switching**: All HUD text instantly updates when toggling fonts—no need to restart the editor.
- **Clean UI**: Removed unused Debug button and replaced with functional font control.

### ✏️ Text Editing Precision
- **Cursor-to-Glyph Alignment**: Text cursor now sits exactly where typed characters will appear, not offset to the right.
- **Per-Character Advances**: Cursor position calculated from actual glyph advance widths, supporting variable-width fonts.
- **Arrow Key Navigation**: Caret movement with arrow keys now aligns perfectly with the rendered text.

---

## 2.2.0 - Building Interior Occlusion & Door Selection
This release improves building visibility logic and allows selecting doors, lights, and cameras from outside.

### 🏢 Smart Building Occlusion
- **Selective Child Hiding**: Only interior sub-structures are hidden when viewing from outside; doors, lights, and security cameras remain clickable on exterior surfaces.
- **Exterior Attachment Support**: Building-attached objects (doors, alarms, cameras) are now properly selectable from outside without hiding them.
- **Ancestor Chain Traversal**: Complex nested building structures now correctly hide only true interior children, not surface attachments.

### 🔍 Improved Selection
- **Full Building Hierarchy Support**: Sub-children of buildings (nested MEF attachments) can now be selected from inside the building.
- **Occlusion Depth Testing**: Building hulls render first in the picking pass, using GPU depth testing to properly occlude interior objects from outside views.

---

## 2.1.0 - Windows DPI Scaling & Windowed-Mode Precision
This release fixes cursor precision in windowed mode on high-DPI displays.

### 💻 High-DPI Support
- **Per-Monitor DPI Awareness**: Editor now declares DPI awareness to Windows, ensuring GLUT delivers physical pixel coordinates.
- **Windowed Mode Precision**: Slider, button, and textbox hit-tests now respond exactly under your cursor at 100%, 125%, and 150% display scaling.
- **Runtime DPI Detection**: Fallback logic automatically detects display scaling if the OS manifest isn't detected.

### 🎨 Widget Accuracy
- **Pixel-Perfect Hit-Testing**: All interactive elements (sliders, text fields, buttons) respond with zero offset, even on scaled displays.
- **Cross-Display Compatibility**: Works seamlessly with external monitors at different scaling factors.

---

## 2.0.0 - Workspace Navigation & Map Selection
This release introduces direct 3D interaction and enhanced state restoration.

### 🖱️ Workspace & State Polish
- **Map View Selection**: Implemented click-to-select support on the main map rendering, enabling direct object interaction from the 3D viewport canvas.
- **Undo/Redo Stability**: Fixed state restoration where Train locations are correctly reserved and restored when performing Undo operations.

---

## 1.9.0 - Train Engine & Precision Logic
This update adds support for railway networks and addresses mission serialization limitations.

### 🚂 Train & Locomotive Support
- **Train Object Support**: Initial integration and rendering of Train models and locomotives.
- **Locomotive Orientation**: Resolved critical direction and alignment bugs when placing locomotive engines.
- **Track Spline Parsing**: Implemented robust rendering and Catmull-Rom smoothing for train track splines.

### 📐 Parser & Level Synchronization
- **Float/Double Precision Guard**: Solved precision truncation issues within QSC file serialization, preserving exact floating-point decimals.
- **Level 3 Integrity Fix**: Patched bracket and node compiler errors when saving mission data in Level 3.

---

## 1.8.0 - Level Cache, Textures, and CLI Standalone
This major update accelerates startup times and establishes a standalone utility toolchain.

### ⚡ Performance & Caching
- **Level Load Cache System**: Introduced an advanced caching layer for models and textures, decreasing startup load times across repeated sessions.
- **AI Texture Map Fixes**: Corrected texturing and mesh configurations for AI MEF models.
- **Render Distance Optimization**: Stabilized dynamic clip distance and global coordinate mappings.

### 🛠️ Headless Toolchain
- **CLI Independence**: Unlocked headless functionality from local executable path structures, allowing command-line tools to resolve game directories standalone.

---

## 1.7.0 - Robust Hierarchical Manipulation & Undo/Redo
This release focus on stabilizing complex object manipulation and providing essential safety features for level editing.

### 🔄 State Management & Safety
- **Multi-Step Undo/Redo**: Implemented a comprehensive Undo/Redo system for object manipulation. The editor now tracks level snapshots, allowing users to revert and redo changes with `CTRL+Z` and `CTRL+Y` (fully configurable via `qedkeybindings.qsc`).
- **Persistent State Fix**: Resolved a persistent bug where modifications to building objects were reset when selecting a different object.
- **Improved Change Tracking**: Updated `LevelObjects::SaveToQSC` to automatically clear stale cached lines for all modified objects, ensuring changes are always reflected in the underlying script.

### 📐 Precision Hierarchical Manipulation
- **Global Root Pivot Enforcement**: Fixed a critical bug in hierarchical rotation where child objects would drift or disappear. Nested descendants (furniture, items, AI) now rotate relative to the root building origin, ensuring perfect coordinate stability.
- **AI Graph Synchronization**: AI soldiers inside buildings now have their internal `graphPos` metadata automatically updated during building translation and rotation, maintaining pathing integrity.
- **Building Snap Preservation**: Refined hierarchical "Snap to Ground" and "Snap to Object" to ensure all child objects maintain their relative offsets after the parent snaps.
- **Snap Search Radius**: Added a 5000-unit search radius to "Snap to Object" for more predictable nearest-neighbor detection.

### 📜 Hierarchical Synchronization
- **Hierarchical Task Editor Sync**: The Task Editor now live-updates with the **full hierarchical script tree** when a container object is manipulated, preventing data loss in the script view.
- **Balanced Parenthesis Engine**: Completely refactored the QSC serialization engine to guarantee perfectly balanced parentheses and correct comma separation in saved mission files.

---

## 1.6.0 - Advanced QVM Configuration & In-Memory Logic
This update transitions the editor to a proprietary configuration system powered by the IGI bytecode engine.

### ⚙️ QSC/QVM Configuration System
- **Unified Config Standard**: Migrated all editor settings and keybindings from legacy `.txt` files to a robust `.qsc` (source) and `.qvm` (compiled) system.
- **In-Memory Decompilation**: Implemented `QVM_DecompileToString` in `qvm_decompiler.cpp`. The editor now parses compiled `.qvm` files and decompiles them directly into a RAM string for processing, eliminating temporary file overhead.
- **Automated Startup Compilation**: The editor now automatically scans the `content/qed/` directory and compiles any new or updated `.qsc` configuration scripts to `.qvm` bytecode at launch.
- **QED Prefix Enforcement**: Enforced a new project-wide convention where all configuration keys must be prefixed with `QED` (e.g., `QEDFontSize`, `QEDLevel`) and lines terminate with a semicolon (`;`).

### 🛠️ Advanced QED Parameters
- **New Engine Parameters**: Added support for 20+ advanced configuration settings including `QEDConsoleAutoActivate`, `QEDSearchType`, `QEDInvertMouse`, `QEDDisplayTaskNote`, and `QEDCameraLock`.
- **Dynamic File Overrides**: Implemented `QEDSetObjectFile("path")` with support for `LOCAL:` path resolution, allowing modders to load specific object scripts for rapid testing.
- **Camera State Persistence**: Added configuration support for initial camera orientation, radius, and matrices, allowing the viewport to restore its exact state between sessions.

---

## 1.5.0 - Asset Extraction & Advanced Native Fixes
This release introduces automated asset extraction and addresses critical rendering and caching bugs for complex native models.

### 📦 Automated Asset Management
- **Level Asset Extractor**: Implemented a robust `AssetExtractor` system that automatically unzips textures and models from `.res` archives into the editor's working directory on-demand (`--extract-level` CLI).
- **Timestamp Caching**: Added file-timestamp validation to ensure `.res` extraction only occurs when necessary, drastically improving level loading speeds across sessions.
- **Level Cache Isolation**: Fixed critical caching bugs where meshes and textures from a previously loaded level (e.g., Level 4) would bleed into the active level (e.g., Level 1). The engine now completely purges the GL mesh, texture, and DAT map caches when switching levels.

### 📐 Advanced Native Rendering Fixes
- **Bone Model Geometry (Type-1)**: Fixed an issue where character models (like `013_01_1` and `003_02_1`) failed to load faces. The `ParseSplitBoneTriangles` parser now dynamically scans all ECAF offsets across out-of-order blocks to accurately calculate missing triangle counts.
- **Building Floors & Collision Rendering (Type-3)**: Fixed a major rendering bug where multi-room buildings were missing their floors. The editor now correctly extracts `XTVC`/`ECFC` collision chunks (where the original game engine stores untextured structural elements like floors and ramps) and appends them as standalone untextured render blocks.
- **Precision Z-Offset Snapping**: Refined the terrain snapping system (`mainZOffset`) for buildings. The calculation now strictly utilizes the lowest Z-coordinate of *textured* submeshes, preventing invisible underground foundations from interfering with accurate floor-to-terrain alignment.

---

## 1.4.0 - Native Game Engine Parity & Toolchain Expansion
This major release marks the complete transition to the Project IGI native file ecosystem. The editor now directly parses and manages proprietary game assets, providing pixel-perfect parity with the original game engine and a robust headless toolset for modders.

### 🛠️ Native File Format Ecosystem
- **Native MEF (Model Engine Format)**: Complete replacement of the legacy OBJ/GLB pipeline with a high-performance proprietary MEF parser.
- **Bone & Rigging Support**: Full support for character bone hierarchies, rig mapping, and animation playback structures.
- **Native TEX Loader**: Direct support for IGI Texture formats (Version 7, Mode 67 BGRA8888) with mipmap and transparency handling.
- **RES Archive Parser**: Built-in ILFF/IRES archive management for dynamic asset extraction from original game resource files.
- **MTP Package Support**: Implementation of the FORM/IFF structured texture mapping parser for complex model-texture packaging.

### 📜 Integrated Scripting & VM Toolchain
- **QVM Decompiler**: Robust reverse-engineering of IGI bytecode (Version 0.5) back to human-readable QSC scripts.
- **QCompiler Pipeline**: Seamless integration of mission logic compilation, enabling direct saving of QSC changes into game-ready QVM binaries.
- **Bytecode Verification**: Automated integrity checks for compiled scripts to ensure stability in the game engine.

### 💻 Headless CLI & Automation
- **Unified Headless Toolset**: Added advanced command-line switches to `igi1ed.exe` for automated workflows:
  - `--extract-level <num>`: Batch extract all level resources (Models, Textures, Terrain).
  - `--mef <file>`: Detailed model geometry and bone structure analysis.
  - `--qsc/--qvm`: CLI-based compilation and decompilation routines.
  - `--res/--mtp`: Archive listing and specific block extraction.
  - `--terrain`: Raw heightmap and lightmap structure analysis.

### 🚀 Performance & Systems Optimization
- **AssetExtractor**: Intelligent management system for level data with file-timestamp caching to prevent redundant extractions.
- **Unified Logger**: Level-specific instrumentation with debug dump support and improved UI telemetry.
- **AppData Synchronization**: Automated migration and locking between local editor folders and `%APPDATA%\QEditor`.
- **Skeletal Rendering**: Improved character rendering with accurate bone-weight visualization and coordinate alignment.

---

## 1.0.0 - Official First Public Release
Welcome to the first public release of the **Project IGI 3D Editor**! This release marks a massive milestone, providing a state-of-the-art 3D modding suite with intuitive controls, visual script compilation pipelines, and precision world manipulation tools.

### 🌟 Core Public Release Features
* **Interactive 3D Terrain & Heightmap Editor**: Fully rendered real-time 3D terrain viewport with heightmap sculpting brushes.
* **Procedural Spline & Waypoint System**: Dynamic editing of track splines, waypoint structures, and repeated mesh fence/wire layouts.
* **Flight Camera & 3D Navigation**: Full 6-DOF fly-through camera with fine-grained speed controls, teleportation hotkeys, and real-time navigation grid.
* **Complete Task & Object Editor**: Just like the classic IGI 2 Editor, you can add new tasks (`Task_New`), duplicate node hierarchies, copy/paste selections, delete objects, and perform full multi-step **Undo/Redo** operations.
* **AI Behavior & Mission Intel Editor**: Full editing suite for soldier paths, movement node scripts, tactical AI weapon/ammo layouts, and mission logic constraints.
* **All 14 Levels Supported**: Robust support for decompiling, editing, and compiling all 14 missions of the original game (with the first 3 levels fully tested and verified).
* **Automatic Backup & Recovery**: Integrated restore systems that safeguard your pristine game files and allow instant reset to factory level state.

---

## BETA 0.0.9 - Global Database Search & UI Polish
### Global Model Search Database
- **Global Model Search Mappings**: Replaced local viewport-only model searches in `SearchModelById()` with a global database search inside `%APPDATA%\QEditor\IGIModels.json`.
- **Query Master JSON**: Replaced `SearchModelByName()` logic to query the global master JSON database and find matching model mappings.
- **Robust JSON Parsing**: Implemented `LoadAllModelsFromJson()` standalone C++ helper to dynamically parse JSON structures containing `ModelName` and `ModelId`.
- **Native Prompts**: Used standard, native **Windows InputBox** dialogue (`Utils::PromptForText`) to request user search queries.
- **MessageBox Integration**: Used **Windows MessageBox** (`MessageBoxA`) to display structured, multi-line database search matches.
- **Encoding Fix**: Prevented encoding glitches (such as the `â€¢` bullet character rendering bug) by implementing CP1252-safe ASCII hyphens (`- `) for item bulleting.
- **Output Truncation**: Capped maximum displayed search results to the first 25 matches with trailing count truncation (`... and X more matches`) to prevent message boxes from overflowing the screen.

### UI Telemetry & Rendering Stabilization
- **Right-Click Context Fix**: Fixed right-click context menu crash by ensuring safe pointer validation on selected tree index.
- **Tooltip Realignment**: Realigned context menu tooltip coordinates to track mouse cursor coordinates exactly.
- **HUD Active State Indicator**: Added conditional telemetry text displaying "Moving" or "Rotating" in the HUD only during active manipulation.
- **Natural Light Restore**: Removed bright red directional/ambient lighting overrides from `renderer_objects.cpp` and restored standard white natural lighting.
- **GLB Translation Alignment**: Corrected coordinate transformation matrices for GLB model rendering (reconciling Y-up to Z-up model coordinates).
- **Scale Glitch Resolution**: Resolved scaling mismatch glitches by removing redundant scaling multiplications in `renderer_objects.cpp`.

### QSC/QVM Pipeline Enhancements
- **Compiler Repairs**: Repaired template-arguments compiler errors and duplicate declarations at the end of `app.cpp`.
- **Scale Getter Signature**: Restructured `App::GetSelectedObjectScale()` function signature to return correct scale parameters.
- **Matrix Syntax Correction**: Restored proper rotation matrix index syntax in `app.cpp` line 2270 to ensure error-free builds.
- **Automated Backup Reset**: Added automatic level reset logic restoring `objects.qsc` and `objects.qvm` from read-only backups upon command.
- **Implicit Node Filtering**: Prevented duplicate-node serialization errors in QSC by refining implicit static node hierarchy filtering.

---

## BETA 0.0.8 - Cutscene Graph Area Protection & AppData Migration
### AppData & Migration Integration
- **Automatic AppData Sync**: Implemented automatic local `QEditor` to `%APPDATA%\QEditor` migration and synchronization logic in `utils.cpp` (`ValidateAndSetupQEditor`).
- **Pristine Backup Guard**: Created exclusions for read-only `QFiles` folder during `AppData` folder copying to protect pristine level configuration backups.
- **Config Path Resolution**: Replaced legacy config directory resolution to consistently target local executable directory using `GetExeDirectory()`.
- **Level Texture Resolution**: Resolved level texture resolution failures by mapping local GLB path references directly to `AppData` textures folders.
- **Altitude Override Guard**: Implemented dynamic coordinate Z-snapping bypass pipeline to keep specific critical actors at their defined altitudes.

### Cutscene Graph Area Protection
- **Graph Area JSON Mapping**: Implemented a custom JSON parser for mapping Graph Area details from `graph_area_levelX.json`.
- **Graph ID Conversions**: Added automatic parser logic that converts visual graph IDs (e.g., `Graph #1` -> integer `1`) for precise matching.
- **Parser Pipeline**: Created the `LoadCutsceneGraphIds` pipeline inside `app.cpp` to parse Graph Area mappings on load.
- **Static Actor Protection**: Flagged all AI models located in "Cutscene" graph zones to preserve author-defined orientations.
- **AI Rotation Bypasses**: Added a static rotation override guard to skip standard yaw adjustments (`0.0` -> `6.28318`) for cutscene actors.
- **Ground Snapping Bypasses**: Prevented terrain-snapping modifications for designated cutscene units in `SnapObjectsToTerrain()`.
- **AI Sync Bypasses**: Bypassed position synchronization and metadata overrides inside `LoadAIModelsFromFolder()` for cutscene actor IDs.

---

## BETA 0.0.6 - Level 14 & GLB Pipeline
### Level Support
- **Full Level 14 Support**: Completed terrain and object support for Level 14 (The Maivon Compound).
- **Pre-defined Object Library**: Integrated the 3DEditor's pre-defined objects collection for rapid level assembly.

### Asset Management
- **GLB Model Format**: Migrated to OpenGL-friendly GLB format, combining models and textures into single binary files for faster loading and better compatibility.

---

## BETA 0.0.5 - IGI 2 Controls & Splines
### User Interface
- **IGI 2 Controls System**: Fully implemented the IGI 2 style control scheme for object manipulation (drags, keyboard modifiers).
- **Enhanced Manipulation**: Refined 6-DOF controls for smoother object placement.

### Foundation
- **Spline System Foundation**: Introduced the foundational logic for spline paths used by tracks and fences.
- **Level Loading Fixes**: Resolved critical crashes when switching between different game levels.

---

## BETA 0.0.4 - Clean Persistence & QSC Integrity
### Object Serialization Stabilization
- **Intentional Change Tracking**: Introduced a `modified` flag for all level objects. The editor now only saves changes for objects you've actually interacted with (moved, rotated) or that were updated during an AI synchronization.
- **QSC Data Preservation**: Fixed "Sticky Snapping" by decoupling the visual terrain snap from the saved QSC coordinates. The original file position is now preserved unless explicitly moved by the user.
- **Minimal Diffs on Save**: By using the `modified` flag, the editor now performs minimal line rewrites in `objects.qsc`, keeping the rest of the file's formatting, indentation, and floating-point precision intact.

### Manual Manipulation & Snapping
- **Manual Snap Guard**: Integrated the underground check into the manual snap key ('S') and reset key ('Space') to prevent accidental displacement of structural components during editing.

---

## BETA 0.0.3 - AI Logic & Stabilization
### AI Logic Fixes
- **Fixed AI Rotation Over-match**: Resolved a critical issue where objects containing "ai" in their name (e.g., "Chair", "Container") were incorrectly identified as AI units and had their rotation forced to 360 degrees.
- **Improved Type Safety**: The editor now explicitly identifies object types (`Building`, `EditRigidObj`, `HumanSoldier`) during the load phase to ensure level-specific rules apply only to the correct units.

### Structural Persistence
- **Robust "Joint Fixer" Exclusion**: Enhanced `IsUndergroundModel` logic with broad keyword support and string normalization to prevent structural models (tunnels, junctions, joint fixers) from snapping to the terrain.
- **Refined Matching**: Implemented case-insensitive trimming and normalization for model names and IDs to ensure matches are found even with inconsistent QSC string formatting.

---

## BETA 0.0.2 - MVP Demo (3 Days Sprint)

### IGIPath Resolution Fix
- **Fixed `IGI_GAME_PATH` placeholder issue**: `GetLevelQVMPath()` now correctly reads `IGIPath` from the executable directory's `config.ini` instead of using a relative path that was picking up placeholder values from the project root
- **Added detailed path logging**: New log entry shows the resolved IGIPath and the config file path being used: `[App] GetLevelQVMPath using IGIPath: D:\IGI1 (from config: D:\...\config.ini)`
- **Root cause**: Was using `.\config.ini` (relative to current working directory) instead of exe directory path

### Compiler/Decompiler Folder Cleaning
- **Automatic input/output folder cleanup**: Both `Compiler::Compile()` and `Decompiler::Decompile()` now clean their respective input and output directories before operations
- **Prevents stale file errors**: Removes old QSC/QVM files that could interfere with new compilation/decompilation operations
- **Detailed cleanup logging**: Reports number of items removed from each directory

### Config Loading Improvements
- **Exe-directory config resolution**: All config operations now consistently use the executable directory path via `GetExeDirectory()`
- **Eliminated config path ambiguity**: No more confusion about which config.ini is being read (project root vs exe directory)

### Technical Changes
- Modified `GetLevelQVMPath()` in `app.cpp` to construct full config path using `exeDir + "\\config.ini"`
- Added comprehensive logging in `GetLevelQVMPath()` to track IGIPath resolution
- Added folder cleaning logic in `compiler.cpp` (lines 54-69) and `decompiler.cpp` (lines 43-58)
- All config paths now resolve to: `<exe_directory>\config.ini`

### Build System
- Both Debug and Release configurations build successfully with MSVC 2022
- No breaking changes to existing functionality

---

## BETA 0.0.1

### QSC Save Logic Improvements
- **Added `original_rot` tracking**: Store original rotation values from QSC during load to enable proper change detection
- **Change detection on save**: Only update objects whose position or rotation has genuinely changed compared to their originally loaded state
- **Terrain snap offset handling**: Added `snap_z_offset` field to track Z offset added by terrain snapping, subtracted during save to match game coordinates
- **Multi-line Building support**: Fixed `Task_New` line replacement for Buildings with child tasks (no closing paren on first line)
- **Snap-to-terrain sync**: After snapping objects to terrain, sync `original_pos.z` so snap-only changes don't count as user edits
- **Preserved formatting**: Maintain original line indentation and terminators (`;` or `,`) for multi-line blocks

### Key Fixes
- Fixed issue where all `EditRigidObj` entries were being rewritten after reset and save
- Fixed Z-axis position discrepancy between editor view and in-game (editor was saving snapped Z, game expects raw terrain Z)
- Fixed compiler errors caused by incorrect multi-line `Task_New` termination
- Improved float formatting with `%.10g` to preserve precision and avoid scientific notation

### Technical Details
- Modified `LevelObject` struct to include `original_rot` and `snap_z_offset` fields
- Updated `LevelObjects::Load()` to capture original rotation values from QSC arguments
- Updated `App::SnapObjectsToTerrain()` to store snap offset and sync original Z
- Updated `LevelObjects::SaveToQSC()` with comprehensive change detection logic
- Added detailed logging for skip/save operations during QSC save

### Compilation
- Successfully builds with MSVC 2022 (Visual Studio)
- No breaking changes to existing functionality
