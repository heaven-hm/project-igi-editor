# Changelogs

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
