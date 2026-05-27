# IGI Editor v1.5.0 — CLI & GUI Reference Guide

The **IGI Editor** is a hybrid toolkit that operates in two primary modes:
1. **Graphical User Interface (GUI) Application**: A real-time 3D viewport editor for heightmaps, objects, Splines, AI, and level layouts.
2. **Headless Command Line Interface (CLI)**: A powerful, high-performance C++ utility toolchain for parsing, decompiling, compiling, and exporting proprietary Project IGI assets.

---

## 🖥️ 1. Graphical User Interface (GUI) Mode

To launch the graphical editor, run the compiled executable `igi1ed.exe` from your terminal or a batch script.

### Launch Options & Syntax
```powershell
.\bin\Release\igi1ed.exe -level <num> [options]
```

| Argument | Parameter Type | Description |
| :--- | :--- | :--- |
| `-level` | `integer (1-14)` | **Required.** Specifies which of the 14 original game levels to load. |
| `-w` | `integer` | Width of the editor window in pixels (e.g., `-w 1280`). |
| `-h` | `integer` | Height of the editor window in pixels (e.g., `-h 720`). |
| `-draw_parts` | `bitmask (0-127)` | Controls which elements of the level environment are parsed and rendered. |
| `-stick_to_ground` | *Flag* | Enforces automated ground-snapping/snapping-to-terrain on load. |

---

### 🎨 Selective Loading & Rendering (`-draw_parts` Bitmask)

The `-draw_parts` option uses a bitmask to optimize memory consumption and load times by selectively disabling subsystems. Add the values of the components you wish to render:

| Bit Value | Subsystem | Description |
| :---: | :--- | :--- |
| **1** | Terrain | Loads the 3D Quadtree Heightmap (`.ctr` / `.lmp` / `.hmp`). |
| **2** | Sky | Standard skybox rendering layers. |
| **4** | Objects | Core gameplay interactive objects. |
| **8** | Flat Sky | Secondary backdrop sky mapping. |
| **16** | Buildings | Primary buildings meshes (MEF model structures). |
| **32** | Props | Miscellaneous non-building object models (crates, fences, assets). |
| **64** | AI Units | AI patrol units, team structures, and soldier meshes. |

#### Commonly Used Bitmask Combinations
* **Minimal Terrain Only**:
  ```powershell
  .\bin\Release\igi1ed.exe -level 1 -draw_parts 1
  ```
* **Terrain & Buildings** (Value: `17` = `1 + 16`):
  ```powershell
  .\bin\Release\igi1ed.exe -level 1 -draw_parts 17
  ```
* **Terrain & Props** (Value: `33` = `1 + 32`):
  ```powershell
  .\bin\Release\igi1ed.exe -level 1 -draw_parts 33
  ```
* **Terrain & AI Patrols** (Value: `65` = `1 + 64`):
  ```powershell
  .\bin\Release\igi1ed.exe -level 1 -draw_parts 65
  ```
* **Full Level Editing Suite** (Value: `127` = all parts):
  ```powershell
  .\bin\Release\igi1ed.exe -level 1 -draw_parts 127
  ```

---

### ⌨️ GUI Keyboard & Mouse Controls

Once inside the graphical editor, use the following hotkeys to navigate the 3D space and modify entities:

| Control Category | Key | Action |
| :--- | :--- | :--- |
| **Camera Movement** | `W` / `S` / `A` / `D` | Fly forward, backward, left, right |
| | `Q` / `Z` | Adjust altitude (Up / Down) |
| | `PageUp` / `PageDown` | Increase / Decrease movement speed |
| | `Alt + Enter` | Toggle Fullscreen Mode |
| | `LeftArrow` / `RightArrow` | Roll camera rotation |
| **Editor Modes** | `F4` | **Toggle Edit Mode (Displays selection crosshairs & telemetry)** |
| | `F3` | Toggle Collision clipping (noclip) |
| | `F2` | Toggle Terrain Paint brush mode |
| **Object Selection** | `LMB Click` | Select an object or building under the cursor in Edit Mode |
| | `F11` | Teleport camera directly to the selected object |
| **IGI 2 Modifiers** | `LMB Drag + Shift` | Move selected object on XY Plane |
| | `LMB Drag + Ctrl` | Move selected object on XZ Plane |
| | `LMB Drag + A` / `B` / `G` | Rotate selected object on Alpha / Beta / Gamma axes |
| | `S` | Snap selected object directly to the terrain |
| | `Space` | Reset selected object's rotation |

---

## ⚙️ 2. Headless Command Line Interface (CLI)

The IGI Editor contains an in-process, headless compiler and parser suite written in native C++17. When any of the CLI flags are provided, the GUI is bypassed entirely, making it suitable for batch modding pipelines, decompiling/compiling automation, and unit testing.

### General Syntax
```powershell
.\bin\Release\igi1ed.exe [command_flag] [arguments]
```

### 🧪 Global Testing Command
* **Run All Parser C++ Unit Tests**:
  ```powershell
  .\bin\Release\igi1ed.exe --run-tests
  ```
  *Executes test coverage across all custom parser suites (MEF, QVM, RES, TEX, MTP, Graphs) to verify format parsing integrity.*

---

### 📦 3D Model Parsers (`--mef`)

Parses Project IGI proprietary MEF model geometries. The editor contains both binary MEF and ASCII MEF C++ parsing systems, automatically falling back to ASCII if the file is an open format.

* **Parse and Display MEF Structure**:
  ```powershell
  .\bin\Release\igi1ed.exe --mef models/001_01_1.mef
  ```
  *Outputs mesh layouts, vertices, normals, faces, bone matrices, textures, and attachment metadata.*

* **Export proprietary MEF to OBJ**:
  ```powershell
  .\bin\Release\igi1ed.exe --mef models/001_01_1.mef --export-obj models/001_01_1.obj
  ```
  *Converts the proprietary mesh layout into a standard Wavefront OBJ model for editing in Blender/Maya.*

* **Export MEF to ASCII MEF**:
  ```powershell
  .\bin\Release\igi1ed.exe --mef models/001_01_1.mef --export-mef models/001_01_1_ascii.mef
  ```
  *De-serializes a binary game model into human-readable ASCII format.*

---

### 📜 QSC Script & QVM Bytecode Suite (`--qsc`, `--qvm`)

Provides a complete, in-process C++ compiler and decompiler pipeline for mission scripts (`objects.qsc` / `objects.qvm`), bypassing the need for separate assembly binaries.

* **Lexical Analysis (QSC Tokens)**:
  ```powershell
  .\bin\Release\igi1ed.exe --qsc objects.qsc --lex
  ```
  *Tokenizes the QSC source and prints out the lexical structure with line and column identifiers.*

* **Syntax Tree Parsing (QSC AST)**:
  ```powershell
  .\bin\Release\igi1ed.exe --qsc objects.qsc --parse
  ```
  *Parses the tokens into an Abstract Syntax Tree (AST), displaying instruction call and argument hierarchies.*

* **Compile QSC to QVM (Bytecode)**:
  ```powershell
  .\bin\Release\igi1ed.exe --qsc objects.qsc --compile objects.qvm
  ```
  *Compiles the high-level QSC script into optimized, executable QVM bytecode for the game engine.*

* **Decompile QVM back to QSC**:
  ```powershell
  .\bin\Release\igi1ed.exe --qvm objects.qvm --decompile objects.qsc
  ```
  *Performs native, full reverse engineering of compiled QVM bytecode into standard QSC source code.*

* **Parse and Inspect QVM Headers**:
  ```powershell
  .\bin\Release\igi1ed.exe --qvm objects.qvm
  ```
  *Lists bytecode size, instruction metrics, string literals, and identifiers.*

---

### 🗃️ RES Resource Archives (`--res`)

Reads and extracts proprietary game assets stored within `.res` archives.

* **List RES Directory Contents**:
  ```powershell
  .\bin\Release\igi1ed.exe --res level1.res
  ```
  *Lists all files packed inside the archive along with their respective file sizes.*

* **Extract a Single Resource File**:
  ```powershell
  .\bin\Release\igi1ed.exe --res level1.res --extract LOCAL:models/tree.mef models/tree.mef
  ```
  *Unpacks a specified asset entry matching the given internal path.*

* **Extract All Packaged Resources**:
  ```powershell
  .\bin\Release\igi1ed.exe --res level1.res --extract-all output_directory/
  ```
  *Unpacks all archive structures recursively, recreating their folders into the destination directory.*

---

### 🎨 Texture Mappings & Formats (`--mtp`, `--tex`)

* **Parse MTP Texture Mapping Info**:
  ```powershell
  .\bin\Release\igi1ed.exe --mtp textures.mtp
  ```
  *Reads global mapping info, animations, shadow maps, texture configurations, and mappings.*

* **Parse TEX Textures**:
  ```powershell
  .\bin\Release\igi1ed.exe --tex sky_layer1.tex
  ```
  *Reads TEX texture file structures, listing dimensions and image modes for all mipmap layers.*

* **Export TEX to TGA Image Files**:
  ```powershell
  .\bin\Release\igi1ed.exe --tex sky_layer1.tex --export-tga output_images/
  ```
  *Extracts raw image assets from the TEX format and saves them as standard, uncompressed TGA files.*

---

### 🏔️ Terrain & Navigation Systems (`--terrain`, `--graph`)

* **Parse Terrain Quadtrees & Lightmaps**:
  ```powershell
  .\bin\Release\igi1ed.exe --terrain terrain.ctr
  .\bin\Release\igi1ed.exe --terrain terrain.lmp
  ```
  *Parses Quadtree nodes (`.ctr`) or lightmap layers (`.lmp`) to verify structural heightmap integrity.*

* **Parse Navigation Graphs (AI Paths)**:
  ```powershell
  .\bin\Release\igi1ed.exe --graph graph.dat
  ```
  *Reads navigation node networks (`.dat`) including positions, surface materials, and connectivity matrices for AI.*

---

### 📊 Game Data File Parser (`--dat`)

Parses Project IGI database and game object definition archives (`.dat`), outputting JSON or plaintext structures with advanced filtering.

* **Parse DAT and Output JSON to Console**:
  ```powershell
  .\bin\Release\igi1ed.exe --dat object_data.dat
  ```
  *Decodes the database and formats object records as JSON printed directly to stdout.*

* **Export DAT JSON to File**:
  ```powershell
  .\bin\Release\igi1ed.exe --dat object_data.dat --output out.json
  ```
  *Writes the structured JSON payload directly to the specified destination path.*

* **Filter by Model Name**:
  ```powershell
  .\bin\Release\igi1ed.exe --dat object_data.dat --filter MODEL_NAME
  ```
  *Extracts and returns only entries containing the specified model name prefix or query.*

* **Plain-text Report Mode**:
  ```powershell
  .\bin\Release\igi1ed.exe --dat object_data.dat --text [--output out.txt]
  ```
  *Outputs a clean, human-readable tabular plaintext report instead of JSON representation.*

---

### 🛠️ Resource Extraction Automation

To streamline modding setup across the 14 levels, the editor features an automated extraction utility.

* **Extract Level Assets**:
  ```powershell
  .\bin\Release\igi1ed.exe --extract-level <level_number> [<out_dir>]
  ```
  *Automates asset unpacking: extracts 3D meshes from the level's `.res` file, gathers texture mappings from `.tex`, copies Quadtree structures, and populates the models, textures, and terrain folders into the output directory (defaults to `levels/level<num>`).*

---

### 🧪 Automated Level Verification & Testing Suite (`--verify-level`)

Runs high-speed integration testing across game levels to compare compiled QVM instructions (ground truth) against the editor output logs (`igi1ed.log`) for verification.

* **Basic Verification**:
  ```powershell
  .\bin\Release\igi1ed.exe --verify-level --level 1 --level 2
  ```
  *Launches and executes comparison checks across the specified levels.*

* **Available Flags and Customization**:
  * `--skip-launch`: Bypasses launching the editor again and runs checks using existing logs.
  * `--timeout <seconds>`: Force-kills the editor after the specified runtime limit (default is `0` which waits indefinitely).
  * `--game-path <path>`: Specifies custom path to the original Project IGI installation directory.
  * `--log <path>`: Overrides the target editor log file path to check.
  * `--report-json <file>`: Writes an aggregated JSON report containing verification metrics.
  * `--report-md <file>`: Generates a beautiful markdown summary report for CI pipeline reporting.
  * `--report-dir <dir>`: Writes individual JSON and Markdown verification reports for each tested level to the specified directory.
  * `--delay <seconds>`: Time interval between level checks (defaults to `5` seconds).

