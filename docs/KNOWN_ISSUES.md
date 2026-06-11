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

### 🧍 Dual-vertex-set character models — wrong textures on parts (editor viewport)
* **Symptom**: A few AI/character models (notably `001_02_1`) render with textures on the wrong body parts in the **editor viewport** — e.g. a clothing/sign texture appears on the face/neck/hands. In-game these render correctly (editor-preview only). Most character models (`004_02_1`, `008_01_1`, `009_01_1`, `013_01_1`) are unaffected.
* **Root Cause**: The model→texture mapping is correct — the MEF's `TAMC` material chunk defines block *i* → texture *i*, and the editor honors that. The problem is **geometry parsing**: these models carry two render/vertex sets (`XTVC0/ECFC0/TAMC0` + `XTVC1/ECFC1/TAMC1`), and the binary-MEF parser splits triangles/vertices into render blocks differently than the model intends, so correct textures land on the wrong geometry. A safe fix needs a reference for the correct per-block geometry (e.g. the original IGI `dconv` OBJ export) to validate against the models that already render right.
* **Note**: Unrelated to the texture file-matching and `.mtp` import fixes — those resolved the other affected models and the in-game corruption.

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
