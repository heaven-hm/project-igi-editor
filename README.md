# IGI Editor

**IGI Editor** is a professional 3D world and object manipulation toolkit for Project IGI. Inspired by the original IGI and IGI 2 editors, it provides a modern interface for level research, object placement, and terrain modification.

**Current Status: BETA 0.0.2** - MVP Demo completed in just 3 days! Features full QSC/QVM compilation pipeline with proper IGIPath resolution and automated compiler folder cleaning.

This project is built upon the foundational work of the [Project-IGI-Terrain](https://github.com/hjcminus/Project-IGI-Terrain) repository. Special thanks to [hjcminus](https://github.com/hjcminus) for their research and for bringing this codebase to light. It is built using C++17 and OpenGL, and it is cross-platform, but it is mainly tested on Windows.

Written and maintained by **Jones-HM (Heaven)**.

---

## 📋 [Changelogs](CHANGELOGS.md)

See the [CHANGELOGS.md](CHANGELOGS.md) for version history and detailed change logs.

---

## 🚀 Features

- **3D Object Placement & Manipulation**: Load and render OBJ models with advanced 6-DOF controls.
- **IGI 2 Style Controls**: Seamless object manipulation using mouse-drags and keyboard modifiers (Shift, Ctrl, A, B, G).
- **Terrain Height Editing**: Integrated height-map brushes for real-time terrain sculpting.
- **Advanced Rendering**: OpenGL-based pipeline with full texture support, wireframe overlays, and skydome rendering.
- **Level Navigation**: Full support for loading and exploring all 13 original IGI levels.
- **Precision Snapping**: Instant ground-snapping logic to ensure objects sit perfectly on the terrain surface.

### Current Testing Status
- **Building Editor**: Working - tested with Building objects
- **Terrain Editor**: Working - height map editing functional
- **Model Format**: Currently tested with OBJ format only
- **Level Tested**: Level 1 (additional levels pending testing)

### Future Work
- **FBX Model Support**: Planned for future releases with full texture integration
- **Extended Level Testing**: Full testing across all 13 levels

---
 
## 📸 Screenshots
 
![IGI Editor Screenshot](assets/igi-editor.png)
*The editor in action showing terrain editing capabilities.*

![IGI Editor BETA Screenshot](assets/igi-editor-beta.png)
*Working BETA version - BETA 0.0.1*

![IGI Editor Debug Screenshot](assets/igi-editor-debug.png)
*Debug Console showing IGIPath resolution and QVM compilation pipeline - BETA 0.0.2*
 
---
 
## Folder Structure

### QEditor AppData Structure (`%APPDATA%/QEditor/`)

The editor requires QEditor to be installed in AppData for QSC/QVM compilation and decompilation:

- **`QFiles/IGI_QSC/`**: Original IGI level data (CTR, CMD, HMP, QSC scripts) organized by:
  - `missions/location0/level[1-14]/` - Level-specific scripts, AI, sounds, terrain
  - `ammo/`, `weapons/`, `common/` - Shared game data
- **`QFiles/IGI_QVM/`**: Compiled QVM files for all levels
- **`QCompiler/`**: Compilation tools (Compile, Decompile, DConv, GConv, TexConv, etc.)
- **`3DEditor/objects/level[1-14]/`**: Level-specific 3D model storage (`.obj`, `.mef`)
- **`3DEditor/textures/level[1-14]/`**: Texture storage for level assets (`.png`, `.tga`)
- **`3DEditor/buildings/level[1-14]/`**: Building model storage
- **`AIFiles/`**: AI data (AI-Json, AI-Path, AI-Script) per level
- **`QGraphs/`**: Area and graph data for levels
- **`QMissions/`**: Mission configuration files
- **`QWeapons/`**: Weapon group and modification data

### Local Repository Folders
- **`shaders/`**: Core OpenGL GLSL shader source files
- **`bin/`**: Pre-compiled binaries and required dynamic libraries (DLLs)
- **`assets/`**: Editor assets (screenshots, icons)

## 🛠️ Future Roadmap

This editor is focused on providing professional-grade tools for Project IGI modding. Upcoming milestones include:
- **AI & Waypoint Editing**: Define complex NPC behaviors and navigation paths.
- **Weapon & Item Management**: Integrated UI for modifying weapon statistics and battlefield placements.
- **Task Tree Editor**: A visual editor for mission objectives and task tree logic, inspired by IGI 2.
- **Native QSC/QVM Integration**: Real-time script debugging and interaction within the 3D viewport.

---

## 💻 Getting Started

### Prerequisites
- **OS**: Windows (x64)
- **Compiler**: MSVC (Visual Studio 2022 recommended)
- **Build System**: CMake
- **QEditor**: Required for QSC/QVM compilation and decompilation.
- **IGI Game**: Full installation of Project IGI required for level data and assets

### Build Instructions
1. Clone the repository.
2. Open the directory in a terminal.
3. Run the following commands:
   ```powershell
   cmake -B build -S .
   cmake --build build --config Release
   ```
4. Launch the editor:
   ```powershell
   .\bin\Release\igi-editor.exe -level 1 -draw_parts 49 -stick_to_ground
   ```

---

## 🔄 How It Works

### Editor Flow

* Editor first copies terrain files from QEditor folder to the executable directory
* Then it finds the latest objects file (QSC or QVM) by checking timestamps across multiple locations (editor, QEditor, IGI game)
* If QVM is newest, it decompiles it to QSC; if QSC is newest, it copies and compiles to QVM to keep everything in sync
* Editor loads the QSC file and parses level data including object positions, rotations, and model references
* Then it loads the terrain heightmap, textures, and lightmaps for rendering and editing
* Next, it loads all 3D models (buildings and props) with their textures and positions them according to QSC data
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

- **🎮 Discord**: Message me at `_Jones_IGI#3954` or join our [Discord Server](https://discord.com/invite/QpbQrRFAER).
- **📧 Email**: [igiproz.hm@gmail.com](mailto:igiproz.hm@gmail.com)
- **🌟 GitHub**: Follow the project on [Jones-HM GitHub](https://github.com/Jones-HM/).
- **📺 YouTube**: Subscribe to [IGI Research Devs](https://www.youtube.com/@igi-research-devs) for guides and walkthroughs.

---

## 🏆 Credits and Contributors

If you want to use this data, respect fellow researchers and give proper credits to people. (давать людям должные кредиты)

- **[Yoejin Light](https://vk.com/id436486682)** 🌟 - _MTP, Models structure_ and information.
- **[Dimon Krevedko](https://vk.com/dimonkrevedko)** 🌟 - **Graphs and Nodes** structure and information.
- **[Artiom Rotari](https://github.com/NEWME0)** 🌟 - _DConv Tools for Decompiler_ and **Scripts**.
- **[ORWA S](https://www.youtube.com/@totalwartimelapses6359)** 🌟 - **Graphs Area and Nodes** compilation of information.
- **[GM123](https://www.youtube.com/@gm1233)** 🌟 - **Detailed Models Information**.
- **[Dark](https://www.youtube.com/@CRONOQUILLOFFICIAL)** 🌟 - **Contributed on Various Projects and files (Resources, QVM, QSC etc) and UI/UX Designs**.
- **[Ferit Coder](https://www.youtube.com/channel/UCpn_gZMkFVBUAe9SJK9hYQA)** 🌟 - **Helped with IGI 2 ToolKit Maps/Models conversion to IGI 1**.
- **[Neo](https://next.nexusmods.com/profile/xaeroneo?gameId=5664)** 🌟 - **Helped with improvement of ToolKit overall and Texture ToolKit**.

---

### Acknowledgments
Special thanks to the original authors and researchers:
- [hjcminus](https://github.com/hjcminus) - 3D Terrain Editor Project, which this project is based on.
- [mrmaller1905](https://github.com/mrmaller1905) - For Requesting this feature.
