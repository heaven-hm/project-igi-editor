# Changelogs

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
