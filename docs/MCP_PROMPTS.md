# Project IGI Editor & Converter — MCP Prompt Playbook

> Comprehensive guide and prompt reference for controlling Project IGI through **Model Context Protocol (MCP)** across **Codex**, **Claude Code**, **Antigravity (AGY)**, **OpenCode**, and **Pi Agents**.

---

## 🌟 Quick Overview of the 3 IGI MCP Servers

| Server Name | Executable / Entry Point | Purpose |
| :--- | :--- | :--- |
| **`igi_editor_mcp`** | `D:\Code\project-igi-editor-mcp\bin\Release\igi_mcp.exe` | High-level 3D level editing, task trees, AI navigation, terrain & validation. |
| **`igi_live_mcp`** | `D:\IGI1\.mcp-live\igi_mcp.exe` | In-place live level editing directly inside active game installation. |
| **`igi_converter_mcp`** | `D:\Code\project-igi-converter\igi_converter_mcp.py` | Direct conversion of `.mef` (3D), `.tex` (2D), `.wav` (audio), `.res` (archives), and `.qsc/.qvm`. |

---

## 🏆 Top 5 Major Modding Prompts

### 1. 🏰 The Fortified Military Base & Horde Defense
* **Recommended Missions:** Level 1 (Trainyard), Level 5 (WaterTower Depot)
* **Prompt:**
  ```text
  Build a fortified defense base at Jones' spawn in Level 1 with 3 Watchtowers, M2HB heavy machine gun turrets, 2 friendly Dragunov snipers, and give me all weapons with 9999 ammo.
  ```
* **What it Does:** Injects 3 elevated watchtowers, mounts stationary M2HB turrets, deploys a friendly allied squad on team 0, and loads maximum ammunition reserves.

---

### 2. 🦸 Superhuman Juggernaut Physics Mode
* **Recommended Missions:** All Levels (Level 1 to 14)
* **Prompt:**
  ```text
  Enable Superhuman Juggernaut mode in Level 3: set jump velocity to 80, sprint speed to 15, health to 10,000 HP, and 9999 ammo for all weapons.
  ```
* **What it Does:** Recompiles `humanplayer.qsc` ➔ `humanplayer.qvm` with 15x movement speed, roof-leaping jump height, 10x god-mode health, and unlimited ammunition.

---

### 3. 🎯 Elite "Sniper Alley" Tactical Overhaul
* **Recommended Missions:** Level 2 (SAM Base), Level 8 (Defend Priboi)
* **Prompt:**
  ```text
  Turn Level 2 into Sniper Alley: place 5 elite Dragunov snipers on roof perches, arm all ground patrols with 50/50 SPAS-12 and Minimi weapons, and launch the level.
  ```
* **What it Does:** Re-arms base guards with long-range Dragunov SVDs and Minimi heavy machine guns, adjusts AI patrol waypoints, and updates the task tree.

---

### 4. 🚁 Instant Combat Insertion (Cutscene Bypass & Air-Drop)
* **Recommended Missions:** Level 7 (Border Crossing), Level 10 (Radar Base)
* **Prompt:**
  ```text
  Bypass the 2-minute intro cutscene in Level 7, spawn an attack helicopter next to Jones, and spawn an allied sniper squad with Dragunov rifles.
  ```
* **What it Does:** Sets intro cutscene container condition to `0`, spawns `HumanPlayer` immediately at tick 0, and injects vehicle + allied squad companions.

---

### 5. 🎨 3D Mesh & Texture Remaster Pipeline
* **Recommended Missions:** Global / Any Level
* **Prompt:**
  ```text
  Extract all 3D character models and textures from Level 1 to OBJ and PNG for Blender, and convert them to modern high-resolution formats.
  ```
* **What it Does:** Unpacks `level1.res` and `location0.res`, converts `.tex` bitmaps to `.png`, and exports `.mef` meshes to Wavefront `.obj` with 56-bone skeletal rigs.

---

## 🧰 Categorized Prompt Reference

### 🎮 Level & Object Placement
* `"Move the WaterTower in Level 5 next to the Checkpoint building and validate the level."`
* `"Add 4 floodlights and security camera posts around the main warehouse in Level 1."`
* `"List all buildings and their 3D coordinates in Level 8."`

### 🔫 Player Weapons & Ammo
* `"Equip Jones with all 18 weapons and 9999 starting ammo in Level 7."`
* `"Set player movement speed to 12.0 and jump to 70.0 in humanplayer.qsc."`

### 🤖 AI Guards & Patrol Routes
* `"Change the guard in Task 2004 from AK47 to Dragunov SVD sniper rifle."`
* `"Create an allied friendly guard on Team 0 standing next to Jones with an M16A2."`

### 📦 Asset Conversion (Blender, PNG, WAV)
* `"Export model 005_01_1.mef from location0.res to guard_mesh.obj."`
* `"Convert all dialogue audio files in Level 7 to playable Windows WAV files."`
* `"Decode all texture files in level5.res to PNG images."`

---

## ⚙️ Multi-Agent MCP Configuration

Add this JSON block to your AI client's configuration:

```json
{
  "mcpServers": {
    "igi_editor_mcp": {
      "command": "D:\\Code\\project-igi-editor-mcp\\bin\\Release\\igi_mcp.exe",
      "args": ["--stdio", "--project", "D:\\IGI1"],
      "env": { "IGI_GAME_PATH": "D:\\IGI1" }
    },
    "igi_live_mcp": {
      "command": "D:\\IGI1\\.mcp-live\\igi_mcp.exe",
      "args": ["--stdio", "--project", "D:\\IGI1"],
      "env": { "IGI_GAME_PATH": "D:\\IGI1" }
    },
    "igi_converter_mcp": {
      "command": "D:\\henv\\Scripts\\python.exe",
      "args": ["D:\\Code\\project-igi-converter\\igi_converter_mcp.py"],
      "env": { "IGI_GAME_PATH": "D:\\IGI1" }
    }
  }
}
```
