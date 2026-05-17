# Changelogs

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
