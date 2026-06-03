# IGI Editor

**IGI Editor** is a professional 3D world and object manipulation toolkit for Project IGI. Inspired by the official [IGI 2 Editor](https://www.nexusmods.com/igi2covertstrike/mods/1) created by the original IGI Developers, it provides a modern interface for level research, object placement, and terrain modification.

**Current Status: Version 2.6.0 (Properties Editor UI & Attachments Support)** - Professional 3D modding suite featuring click-to-select map selection, train and spline tools, a streamlined workspace menu, automated asset extraction, flawless native MEF model loading (including complex buildings and bone structures), integrated QVM decompilation, and a full headless CLI toolchain. Supports editing and compiling for all 14 original game levels with native asset parity.

This project is built upon the foundational work of the [Project-IGI-Terrain](https://github.com/hjcminus/Project-IGI-Terrain) repository. Special thanks to [hjcminus](https://github.com/hjcminus) for their research and for bringing this codebase to light. It is built using C++17 and OpenGL, and it is cross-platform, but it is mainly tested on Windows.

Written and maintained by **Heaven-HM**.

---

## 📋 [Changelogs](CHANGELOGS.md)

See the [CHANGELOGS.md](CHANGELOGS.md) for version history and detailed change logs.

---

## 🚀 Features

- **3D Terrain Rendering & Sculpting**: Fully rendered real-time 3D terrain with active snapping, grid drawing, and heightmap editing brushes.
- **Flight Camera & 3D Navigation**: Full 6-DOF fly cam with fine-grained pageup/pagedown speed controls and teleportation tools.
- **Visual Task Tree Editor**: Visual tree-view workspace for managing mission objectives, inserting new tasks (`Task_New`), duplicating nodes, copying/pasting selections, deleting nodes, and multi-step **Undo/Redo** support.
- **Advanced Splines & Waypoints**: Complete spline system for procedural railway paths, mesh repeats, linear/curved segment configuration, and pathing lines.
- **AI Behavior & Mission Layout**: Edit NPC soldier structures, patrol nodes, custom scripts, weapon loadouts, ammunition inventory, and team layouts.
- **Live Editor Real-Time Sync**: Direct communication between the editor and the IGI engine for instant visual and physical feedback.
- **3D Object Placement & Manipulation**: Advanced 6-DOF controls for placing buildings, props, terminals, doors, cameras, and actors.
- **IGI 2 Style Controls**: Seamless object translation and rotation using standard mouse-drag modifiers (Shift, Ctrl, A, B, G).
- **Automated Path & Sync Pipeline**: Automatically handles compiler syncing, path mapping, and safe directory cleaning.

### Current Testing Status
- **Building Editor**: Working - fully tested with Building objects.
- **Terrain Editor**: Working - 3D terrain heightmap rendering and snapping fully functional.
- **Task Tree & Objectives**: Working - interactive tree management, copy/paste, deletion, and insertion of new tasks fully operational.
- **AI & Waypoint System**: Working - full editing of NPC patrol nodes and properties.
- **Model Format**: Uses proprietary **Native MEF Models** natively loaded by the integrated MEF parser for optimal accuracy and parity with the game engine.
- **Level Tested**: Supports compiling/decompiling all 14 original game levels. Note that only the first few levels are fully tested and verified. Levels from Level 5 onwards may have bugs or issues; if you find any, please create an issue on GitHub and report them to us! Thank you!

### ⚠️ Known Issues
A comprehensive list of all known rendering, game, and engine issues can be found in our **[Known Issues Guide](docs/KNOWN_ISSUES.md)**. Key issues currently include:
- **Fence Wire**: Rendering and placement of secondary fence wiring is currently not solved.
- **Level 7 Cutscenes**: Model `003_01_1` (PATROL_AK) does not draw properly due to missing texture coordinates in cutscene context.
- **Metal Doors (L12-14)**: Slide-up doors fail to acquire standard orientations because the levels configure them as `EditRigidObj` instead of `Door`.
- **Train Tracks (L1, 9, 10)**: Spline railway lines appear jagged over bridges and lack smooth curve interpolation.
- **Cutscene Editing Crashes**: Editing `AnimationTask` or other timeline nodes can lead to game engine crashes due to bytecode script offset mismatches.

### Future Work
- **Expanded Sandbox Modding**: Expanding item drop coordinates, trigger boundary visualizers, and ammo boxes placements.
- **Full Campaign Testing**: Continuing rigorous end-to-end testing across levels 4 through 14.

## 📸 Screenshots

With the release of our premium modding features, we have expanded our workspace visualization with high-fidelity telemetry, dynamic objective tree views, and comprehensive level environment rendering. 

### 🖥️ Main Editor & Navigation

![IGI Editor Screenshot](assets/screenshots/igi-editor.png)
*3D viewport showing level models, objects, and real-time navigation.*

![IGI Editor Level 8](assets/screenshots/igi-editor-level8.png)
*Level 8 Harbor terrain, dynamic structures, and Flight Camera visualization.*

![IGI Editor Level 10](assets/screenshots/igi-editor-level10.png)
*Level 10 Research Facility rendering, building placement, and real-time snapping.*

### 🌳 Task & Objective Editor

![IGI Editor Task Tree](assets/screenshots/igi-editor-task-tree.png)
*Visual Task Tree Editor for mission objective management.*

![IGI Editor Task Tree Editor](assets/screenshots/igi-editor-task-edit.png)
*Interactive Task Objective Editor modal for inline task renaming, notes updates, and direct live save/reload functionality.*

![IGI Editor Copy & Paste Task](assets/screenshots/igi-editor-copy-task.png)
*Task Copy & Paste feature where you can copy and paste any task to replicate any objects with its object tree.*

![IGI Editor Add New Task](assets/screenshots/igi-editor-new-task.png)
*Adding a new task allows you to easily inject custom new Objects, Buildings, or AI units directly into the level.*

### 🏔️ Terrain Editor

![IGI Editor Terrain Editor](assets/screenshots/igi-editor-terrain.png)
*Interactive 3D Terrain Editor showing terrain sculpting, heightmap editing, and active wireframe brush.*

### 📦 Object & Controls Editor

![IGI Editor Controls](assets/screenshots/igi-editor-controls.png)
*HUD telemetry displaying precise translation, rotation, and selection info.*

### 🤖 AI Editor

![IGI Editor AI](assets/screenshots/igi-editor-ai.png)
*AI Unit identification and management interface.*

### ⚙️ Debugging & Compilation

![IGI Editor Debug Screenshot](assets/screenshots/igi-editor-debug.png)
*Debug Console showing IGIPath resolution and QVM compilation pipeline.*

## 🕹️ CLI & GUI Command-Line Options

The **IGI Editor** can be run as both a fully featured interactive 3D graphical suite and a high-performance, headless command-line asset tool:

*   **GUI Editor Mode**: Launch the graphical user interface to edit level data. Supports options like `-level <num>` (1-14), custom dimensions (`-w`, `-h`), ground snapping (`-stick_to_ground`), and selective rendering bitmasks (`-draw_parts`).
*   **Headless CLI Mode**: Perform high-speed operations directly from your terminal. Parsers are provided for 3D meshes (`--mef`), script compiles (`--qsc`), reverse engineering bytecode (`--qvm`), extracting resource libraries (`--res`), textures (`--mtp`, `--tex`), navigation systems (`--graph`), terrain geometries (`--terrain`), database archives (`--dat`), and automated level integrations (`--verify-level`).

For a comprehensive list of all CLI commands, export options, selective rendering bitmask combinations, keyboard hotkeys, and hands-on examples, please check our detailed guide:
👉 **[CLI & GUI Reference Guide](docs/CLI.md)**

And for detailed information about file formats of IGI game 👉 **[IGI File Formats](docs/file-formats.md)**
---
 
## Folder Structure

### QEditor AppData Structure (`%APPDATA%/QEditor/`)

The editor requires QEditor to be installed in AppData for QSC/QVM compilation and decompilation:

- **`QFiles/IGI_QSC/`**: Original IGI level data (CTR, CMD, HMP, QSC scripts) organized by:
  - `missions/location0/level[1-14]/` - Level-specific scripts, AI, sounds, terrain
  - `ammo/`, `weapons/`, `common/` - Shared game data
- **`QFiles/IGI_QVM/`**: Compiled QVM files for all levels
- **`QCompiler/`**: Compilation tools (Compile, Decompile, DConv, GConv, TexConv, etc.)
- **`3DEditor/objects/level[1-14]/`**: Level-specific 3D model storage (proprietary `.mef` models, `.obj` files)
- **`3DEditor/buildings/level[1-14]/`**: Building model storage (proprietary `.mef` models)
- **`AIFiles/`**: AI data (AI-Json, AI-Path, AI-Script) per level
- **`QGraphs/`**: Area and graph data for levels
- **`QMissions/`**: Mission configuration files
- **`QWeapons/`**: Weapon group and modification data

### Local Repository Folders
- **`shaders/`**: Core OpenGL GLSL shader source files
- **`bin/`**: Pre-compiled binaries and required dynamic libraries (DLLs)
- **`assets/`**: Editor assets (icons, screenshots in `assets/screenshots/`)

## 🛠️ Future Roadmap

With the successful release of **Version 2.0.0**, core features like the **Native MEF Parser**, **Asset Extractor**, **QVM Toolchain**, **Task Tree Editor**, **Train & Spline Engine**, **Click-to-Select Map View**, and **Headless CLI** have been fully realized. Future milestones include:
- **Visual 3D Graph Editor (Coming Soon)**: A full-featured Visual 3D Graph Editor displaying interactive nodes and visuals to seamlessly construct game logic, path routes, and area connections.
- **Weapon & Item Configurator**: Rich telemetry overlays and visual UI for modifying active gun parameters, ammunition slots, and dropping custom inventory directly onto the battlefield.
- **Full 14 Levels campaign run**: Complete, verified playthroughs of all custom compiled maps to guarantee total end-to-end stability.

---

## 💻 Getting Started

### Prerequisites
- **OS**: Windows (x86)
- **Compiler**: MSVC (Visual Studio 2022 recommended)
- **Build System**: CMake
- **QEditor**: Required for QSC/QVM compilation and decompilation.
- **IGI Game**: Full installation of Project IGI required for level data and assets

### Build Instructions
1. Clone the repository.
2. Open the directory in a terminal.
3. Run the following commands to build for **32-bit (Win32)**:
   ```powershell
   # Clean previous build if necessary
   if (Test-Path build) { Remove-Item build -Recurse -Force }

   # Configure for 32-bit (Win32) using a specific Visual Studio instance
   cmake -B build -G "Visual Studio 17 2022" -A Win32 -DCMAKE_GENERATOR_INSTANCE="C:/Program Files/Microsoft Visual Studio/2022/Community"

   # Build in Release mode
   cmake --build build --config Release
   ```
4. Launch the editor:
   ```powershell
   .\bin\Release\igi1ed.exe -level 1 -draw_parts 49 -stick_to_ground
   ```

   #### 🎨 Selective Loading and Drawing (`-draw_parts` Bitmask)
   You can customize what parts of the level to load and render using the `-draw_parts` bitmask argument:
   
   * **Only Buildings with Terrain** (Bitmask: `17` = `1` Terrain + `16` Buildings)
     ```powershell
     .igi1ed.exe -level 1 -draw_parts 17 -stick_to_ground
     ```
   * **Only Objects/Props with Terrain** (Bitmask: `33` = `1` Terrain + `32` Objects/Props)
     ```powershell
     .igi1ed.exe -level 1 -draw_parts 33 -stick_to_ground
     ```
   * **Only AI Units with Terrain** (Bitmask: `65` = `1` Terrain + `64` AI)
     ```powershell
     .igi1ed.exe -level 1 -draw_parts 65 -stick_to_ground
     ```
     *(Note: AI models are stored as non-building objects (props) inside the engine. To visually render the 3D meshes of the AI units, combine with props to get `-draw_parts 97` which is `1` + `32` + `64`)*


---

## 🔄 How It Works

### Editor Flow

* Editor first copies terrain files from QEditor folder to the executable directory
* Then it finds the latest objects file (QSC or QVM) by checking timestamps across multiple locations (editor, QEditor, IGI game)
* If QVM is newest, it decompiles it to QSC; if QSC is newest, it copies and compiles to QVM to keep everything in sync
* Editor loads the QSC file and parses level data including object positions, rotations, and model references
* Then it loads the terrain heightmap, textures, and lightmaps for rendering and editing
* Next, it loads all 3D models (buildings and props in native MEF format) and positions them according to QSC data
* Objects are automatically snapped to the terrain surface to ensure correct placement
* Camera is positioned at the level start coordinates and editor is ready for editing
* When you save changes, the editor writes to objects.qsc, compiles it to objects.qvm, and copies it to the IGI game path

---

## ⌨️ Controls

### Navigation
| Key | Action |
| :--- | :--- |
| **W/S/A/D** | Movement (Forward/Backward/Left/Right) |
| **Q/Z** | Vertical Movement (Up/Down) |
| **F4** | Toggle Edit Cursor (Global Edit Mode) |
| **F3** | Toggle Collision / Clipping |
| **F2** | Toggle Terrain Painting Mode |
| **PageUp/Dn** | Adjust Movement Speed |

### Object Manipulation (IGI 2 Style)
Select an object in **Edit Mode (F4)** and use **LMB Drag** + Modifiers:
- **Shift**: Move on XY Plane
- **Ctrl**: Move on XZ Plane
- **A / B / G**: Rotate Alpha / Beta / Gamma axes
- **S**: Snap to Ground
- **Space**: Reset Orientation
- **F11**: Teleport camera to selected object

---

## 📞 Connect with us

If you encounter any issues or have suggestions, feel free to reach out:

- **🎮 Discord**: Message me at `Jones_IGI#3954` or join our [Discord Server](https://discord.com/invite/QpbQrRFAER).
- **📧 Email**: [igiproz.hm@gmail.com](mailto:igiproz.hm@gmail.com)
- **🌟 GitHub**: Follow the project on [Jones-HM GitHub](https://github.com/Jones-HM/).
- **📺 YouTube**: Subscribe to [IGI Research Devs](https://www.youtube.com/@igi-research-devs) for guides and walkthroughs.

---

## 🏆 Credits and Contributors

If you want to use this data, respect fellow researchers and give proper credits to people. (давать людям должные кредиты)

- **[Yoejin Light](https://vk.com/id436486682)** 🌟 - _MTP, Models structure_ and information.
- **[Dimon Krevedko](https://vk.com/dimonkrevedko)** 🌟 - **Graphs and Nodes** structure and information.
- **[Artiom Rotari](https://github.com/NEWME0)** 🌟 - _DConv Tools for Decompiler_ and **Scripts** (For Native Game file formats QVM/MEF/TEX and more).
- **[ORWA S](https://www.youtube.com/@totalwartimelapses6359)** 🌟 - **Graphs Area and Nodes** compilation of information, and beta testing this out.
- **[GM123](https://www.youtube.com/@gm1233)** 🌟 - **Detailed Models Information** & Detailed documentation on 3D Models and Tools.
- **[Dark](https://www.youtube.com/@CRONOQUILLOFFICIAL)** 🌟 - **Contributed on Various Projects and files (Resources, QVM, QSC etc) and UI/UX Designs**.
- **[Ferit Coder](https://www.youtube.com/channel/UCpn_gZMkFVBUAe9SJK9hYQA)** 🌟 - **Helped with IGI 2 ToolKit Maps/Models conversion to IGI 1**.
- **[Neo](https://next.nexusmods.com/profile/xaeroneo?gameId=5664)** 🌟 - **Helped with all 3D Models/Textures of all Objects/AI without that would not have been possible to create this.** Beta testing and helped to improve this project more in IGI 2 ed style.

---

### Acknowledgments
Special thanks to the original authors and researchers:
- [hjcminus](https://github.com/hjcminus) - 3D Terrain Editor Project, which this project is based on.
- [mrmaller1905](https://github.com/mrmaller1905) - For Requesting this feature.
