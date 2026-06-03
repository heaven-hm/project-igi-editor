# Known Issues and Engine Limitations

This document tracks all known rendering bugs, engine limitations, and editor work-in-progress features for the Project IGI Editor.

---

## 🏔️ 1. Rendering and Model Issues

### 🎥 Camera Orientation
* **Symptom**: Camera orientations in certain views or cutscenes do not align correctly with the expected angles.
* **Root Cause**: Discrepancies in yaw/pitch/roll representations and matrix multiplication orders between the editor's camera viewport and the game's internal camera structures.

### 📏 Wire Width
* **Symptom**: Fence wires and secondary wire meshes render with incorrect thicknesses or fail to scale dynamically based on viewport distance.
* **Root Cause**: Platform line-width rendering limits and lack of shader-based variable wire width scaling.

---

## 🛤️ 2. Splines and Placements

### 📍 Missing Position
* **Symptom**: Certain objects or entities are loaded without valid coordinates, defaulting to the map origin or failing to render.
* **Root Cause**: Unmapped coordinate offsets in the parsed binary QSC/QVM level files for specific object classes.

### 🎬 Train Placements in Cutscenes
* **Symptom**: Trains or railway carriages appear misplaced, misaligned, or floating during cutscene playback.
* **Root Cause**: The engine uses hardcoded spline overrides and coordinate offsets specifically during cutscenes, which are not currently synchronized with the editor's standard object placement.

---

## 📢 3. Reporting Other Bugs and Issues

If you encounter any other bugs, crashes, or rendering issues that are not documented here, please feel free to report them to the dev team!

* **🎮 Discord**: Message us directly at `Jones_IGI#3954` or join the modding community on the [Project IGI Discord Server](https://discord.com/invite/QpbQrRFAER).
* **📧 Email**: [igiproz.hm@gmail.com](mailto:igiproz.hm@gmail.com)
* **🌟 GitHub**: Create a detailed issue on the [project-igi-editor GitHub Repository](https://github.com/Jones-HM/project-igi-editor/issues) with reproduction steps and logs.
