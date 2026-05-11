# IGI Editor

**IGI Editor** is a professional 3D world and object manipulation toolkit for Project IGI. Inspired by the original IGI and IGI 2 editors, it provides a modern interface for level research, object placement, and terrain modification.

This project is built upon the foundational work of the [Project-IGI-Terrain](https://github.com/hjcminus/Project-IGI-Terrain) repository. Special thanks to [hjcminus](https://github.com/hjcminus) for their research and for bringing this codebase to light. It is built using C++17 and OpenGL, and it is cross-platform, but it is mainly tested on Windows.

Written and maintained by **Jones-HM (Heaven)**.

---

## 🚀 Features

- **3D Object Placement & Manipulation**: Load and render OBJ models with advanced 6-DOF controls.
- **IGI 2 Style Controls**: Seamless object manipulation using mouse-drags and keyboard modifiers (Shift, Ctrl, A, B, G).
- **Terrain Height Editing**: Integrated height-map brushes for real-time terrain sculpting.
- **Advanced Rendering**: OpenGL-based pipeline with full texture support, wireframe overlays, and skydome rendering.
- **Level Navigation**: Full support for loading and exploring all 13 original IGI levels.
- **Precision Snapping**: Instant ground-snapping logic to ensure objects sit perfectly on the terrain surface.

## 🛠️ Future Roadmap

The IGI Editor is under active development. Upcoming features include:
- **AI & Waypoint Editing**: Tools for defining NPC behaviors and paths.
- **Weapon & Item Management**: UI for modifying weapon stats and placement.
- **Task Tree Editor**: IGI 2 style mission objective and task tree visualization.
- **Native QSC/QVM Integration**: Deeper script-level interaction within the 3D view.

---

## 💻 Getting Started

### Prerequisites
- **OS**: Windows (x64)
- **Compiler**: MSVC (Visual Studio 2022 recommended)
- **Build System**: CMake

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
- **[Chat-GPT Pro](https://chat.openai.com/)** 🌟 - **Understanding complex game mechanics** with _Reverse Engineering_ and **Advanced Data Analysis** of Game files _QVM, QSC, .DAT and more_.

---

### Acknowledgments
Special thanks to the original authors and researchers:
- [hjcminus](https://github.com/hjcminus) - 3D Terrain Editor Project, which this project is based on.
- [mrmaller1905](https://github.com/mrmaller1905) - For Requesting this feature.
