# Project IGI Editor — Live End-to-End (E2E) Testing Guide

This guide provides a manual and automated reference for live, native,
end-to-end verification across the 14 game levels and object categories in
Project IGI Editor. It documents both generic UI workflows and the stricter
deterministic native visual-integrity gate; a loader PASS alone is not visual
acceptance.

---

## 🎯 What is Live Native Verification?

The Project IGI Editor test harness verifies real runtime 3D scene rendering, model loading, texture assignment, and transform parity directly inside the interactive OpenGL engine:
- **One-Session Interactive Execution**: Launches the editor on the user's interactive desktop (Session 1) using WMI `Win32_Process.Create`.
- **4 GB Large Address Aware (VAS)**: Validated for 64-bit aware 32-bit execution without memory truncation.
- **OpenGL Driver Crash Immunity**: Fully protects fixed-function rendering pipelines (`glBegin`/`glEnd`) from vertex buffer object (VBO) state leakage in modern GPU drivers (e.g. AMD `atioglxx.dll` offset `0x444` crash).
- **Deterministic target evidence**: Emits target object/material ID masks and
  depth data, then checks projected submesh coverage and independently rendered
  attachment parts. The check is generic and has no model-specific rules or
  image classifier.
- **Multi-Angle Camera Captures**: Emits a fixed 16-view capture set (12
  exterior poses and 4 interior views). `-ViewCount` controls the number of
  selected records copied into each object result; the default is 10.
- **Separate evidence layers**: `batch.json` records loader transforms, DAT
  material/texture evidence, screenshots, and visual-integrity status
  separately. The native runner defaults to `-VisualIntegrityPolicy Required`,
  so a visual `FAIL` or `INCONCLUSIVE` fails the selected object.

---

## 🚀 Quick Start Commands

The harness is driven by [`e2e_live_test.cmd`](../e2e_live_test.cmd) (or PowerShell [`Run-SmartTest.ps1`](../Run-SmartTest.ps1)) in the repository root:

```cmd
:: Quick test: 1 AI object on Level 5
e2e_live_test --level 5 --ai --maximum 1

:: Quick test: 1 Vehicle on Level 5
e2e_live_test --level 5 --vehicle --maximum 1

:: Quick test: 1 Building on Level 5
e2e_live_test --level 5 --building --maximum 1

:: Quick test: 1 Rigid Object on Level 5
e2e_live_test --level 5 --rigid --maximum 1

:: Cross-category trial: 1 object from EACH category (4 objects, 40 screenshots)
e2e_live_test --level 5 --distinct-categories --maximum 4
```

These convenience commands use `Run-SmartTest.ps1` and the required visual
policy in the underlying native runner. For the Level 12 strict fixtures, use
the native script directly so authored task IDs are selectable:

```powershell
$native = 'D:\Code\project-igi-editor\tools\e2e\Invoke-SmartNativeCaptureSession.ps1'
$out = 'D:\Code\project-igi-editor\artifacts\visual-integrity-level12-' + (Get-Date -Format yyyyMMdd-HHmmss)
& pwsh -NoProfile -ExecutionPolicy Bypass -File $native `
  -GameRoot D:\IGI1 -EditorExePath D:\Code\project-igi-editor\bin\Release\igi1ed.exe `
  -Level 12 -Category Buildings -ModelIds '405_02_1,463_01_1' `
  -TaskIds '570,-1#907' -MaxObjects 0 -ViewCount 10 -Video `
  -VisualIntegrityPolicy Required -ArtifactsRoot $out
```

Expected fixture classifications are intentionally split: Watchtower
(`405_02_1`, task `570`) must be visual `PASS`, while WinchHouse (`463_01_1`,
task `-1#907`) must be visual `FAIL` with named under-covered geometry. A
verified run is recorded at
`artifacts/visual-integrity-level12-strict-live-20260907/`.

---

## 🏷️ Testing by Object Category

The runner supports filtering by discrete game object categories. Each category can be specified with short shortcut flags (`--ai`, `--vehicle`, etc.) or with `--category <name>`.

### 1. 🤖 AI Category
Covers enemy soldiers, patrol units, and player avatars:
- **Types**: `HumanSoldier`, `HumanSoldierFemale`, `HumanSoldierRPG`, `HumanPlayer`, `HumanAI`
- **Supported Models**: `003_01_1` (Soldier), `000_01_1` (Jones), etc.
- **Rotation**: Automatically handles single-axis (yaw/gamma) and runtime horizontal rotation overrides.

**Commands:**
```cmd
:: Test 1 AI soldier on Level 5
e2e_live_test --level 5 --ai --maximum 1

:: Test 3 distinct AI models on Level 5
e2e_live_test --level 5 --category ai --distinct-types --maximum 3

:: Test AI on Level 1 (Trainyard)
e2e_live_test --level 1 --ai --maximum 1

:: Dry-run plan for AI on Level 12
e2e_live_test --level 12 --ai --maximum 2 --prepare-only
```

---

### 2. 🏢 Buildings & Structures Category
Covers architectural models, compound doors, operational switches, and terminals:
- **Types**: `Building`, `Door`, `Terminal`, `Switch`, `AlarmControl`, `Elevator`, `Fence`, `Cabinet`
- **Supported Models**: Watchtowers, barracks, sliding doors, power switches, computer consoles, security systems.

**Commands:**
```cmd
:: Test 1 building object on Level 5
e2e_live_test --level 5 --building --maximum 1

:: Test 3 distinct building types (e.g. Door, Switch, Building) on Level 5
e2e_live_test --level 5 --category buildings --distinct-types --maximum 3

:: Test doors and switches on Level 1
e2e_live_test --level 1 --category buildings --maximum 2

:: Dry-run verify building candidates on Level 8
e2e_live_test --level 8 --category buildings --maximum 3 --prepare-only
```

---

### 3. 📦 Rigid Objects Category
Covers props, military hardware, explosive containers, rotating machinery, and battlefield fixtures:
- **Types**: `EditRigidObj`, `Static`, `Dynamic`, `ExplodeObject`, `RotatingObject`, `StationaryGun`, `SCamera`, `AlarmLight`, `Siren`, `Generator`, `GenericPickup`, `GenericTBA`, `Radio`, `Wire`
- **Supported Models**: Crates, searchlights, barrels, surveillance cameras, radar dishes, weapon pickups.

**Commands:**
```cmd
:: Test 1 rigid object on Level 5
e2e_live_test --level 5 --rigid --maximum 1

:: Test 3 distinct rigid object types on Level 5
e2e_live_test --level 5 --category rigid --distinct-types --maximum 3

:: Test Level 1 rigid objects
e2e_live_test --level 1 --category rigid --maximum 2

:: Dry-run plan for Level 3 rigid objects
e2e_live_test --level 3 --category rigid --maximum 3 --prepare-only
```

---

### 4. 🚁 Vehicles Category
Covers operational and static vehicles in the game:
- **Types**: `Car`, `Heli`, `Train`, `Plane`, `CarAI`
- **Supported Models**: `709_01_1` (Mil Mi-24 Hind Helicopter), trains, transport trucks, fighter jets.

**Commands:**
```cmd
:: Test Helicopter on Level 5 (Radar Base)
e2e_live_test --level 5 --vehicle --maximum 1

:: Test Vehicles on Level 2 (SAM Base)
e2e_live_test --level 2 --category vehicles --maximum 1

:: Test Train / Rail Vehicles on Level 1 (Trainyard)
e2e_live_test --level 1 --category vehicles --maximum 1

:: Dry-run plan for Vehicles on Level 6
e2e_live_test --level 6 --category vehicles --maximum 1 --prepare-only
```

---

## 🎲 Cross-Category & Sampling Strategies

### Sampling 1 Object from Every Available Category (`--distinct-categories`)
Ensures a balanced test suite that checks AI, Buildings, Vehicles, and Rigid Objects in a single session:
```cmd
:: Test 1 AI + 1 Building + 1 Rigid + 1 Vehicle on Level 5
e2e_live_test --level 5 --distinct-categories --maximum 4

:: Test distinct categories on Level 1
e2e_live_test --level 1 --distinct-categories --maximum 3
```

### Sampling by Distinct Types (`--distinct-types`)
Guarantees no two tested objects share the exact same object type:
```cmd
:: Test 3 completely different types on Level 5 (e.g. Door, EditRigidObj, RotatingObject)
e2e_live_test --level 5 --distinct-types --maximum 3

:: Test 3 distinct types on Level 1
e2e_live_test --level 1 --distinct-types --maximum 3
```

### Adjusting Camera Angles per Object (`--views`)
Default is 10 views (6 exterior orbit views at 60° increments + 4 interior views). You can specify fewer for rapid checks:
```cmd
:: Rapid 4-angle check per object
e2e_live_test --level 5 --ai --maximum 1 --views 4

:: Standard 10-angle inspection
e2e_live_test --level 5 --vehicle --maximum 1 --views 10
```

---

## 🗺️ Testing by Level Type

Project IGI features 14 distinct levels with varying terrain profiles, asset densities, and object types. Here are the recommended testing commands for each level archetype:

### 1. Infiltration & Train Facilities (Levels 1 & 2)
- **Level 1 (Trainyard)**: Splines, tracks, rail containers, soldiers, alarm systems.
  ```cmd
  e2e_live_test --level 1 --category buildings --maximum 2
  e2e_live_test --level 1 --distinct-categories --maximum 3
  ```
- **Level 2 (SAM Base)**: Surface-to-air missile pads, military trucks, perimeter fences.
  ```cmd
  e2e_live_test --level 2 --distinct-categories --maximum 3
  ```

### 2. Mountain & Border Compounds (Levels 3 & 4)
- **Level 3 (Military Airbase)**: Hangars, fighter jets, security cameras, generators.
  ```cmd
  e2e_live_test --level 3 --category rigid --maximum 2
  ```
- **Level 4 (GOD Compound)**: Guard towers, high fences, floodlights.
  ```cmd
  e2e_live_test --level 4 --distinct-types --maximum 3
  ```

### 3. Radar & Heavy Industrial Installations (Levels 5, 8 & 10)
- **Level 5 (Radar Base)**: Helicopters (`709_01_1`), large radar dishes (`303_01_1`), power generators, elevator systems.
  ```cmd
  e2e_live_test --level 5 --distinct-categories --maximum 4
  ```
- **Level 8 (Re-supply)**: Dense warehouse buildings, switchgear, interior doors.
  ```cmd
  e2e_live_test --level 8 --category buildings --maximum 2
  ```
- **Level 10 (Get Priboi)**: High-density compound, offices, security terminals.
  ```cmd
  e2e_live_test --level 10 --category buildings --maximum 2
  ```

### 4. High-Altitude & Train Chase Maps (Levels 12, 13 & 14)
- **Level 12 (Train Ambush)**: Moving train cars, heavy terrain height variances.
  ```cmd
  e2e_live_test --level 12 --distinct-types --maximum 2
  ```
- **Level 13 (Nuclear Base)**: Heavily guarded subterranean and surface structures.
  ```cmd
  e2e_live_test --level 13 --category ai --maximum 2
  ```
- **Level 14 (The Bomb)**: Complex facility corridors, countdown triggers, terminal nodes.
  ```cmd
  e2e_live_test --level 14 --distinct-categories --maximum 3
  ```

### 5. Multi-Level Full Campaign Verification
Run a light sampling across all 14 levels sequentially:
```cmd
e2e_live_test --all-levels --category rigid --maximum 1
```

---

## ⚡ Instant Dry-Run Verification (`--prepare-only`)

When validating candidate manifests or checking rotation and texture bindings without launching the editor or OpenGL window:

```cmd
:: Dry-run Level 5 AI
e2e_live_test --level 5 --ai --maximum 1 --prepare-only

:: Dry-run Level 5 all categories
e2e_live_test --level 5 --distinct-categories --maximum 4 --prepare-only

:: Dry-run all 14 levels
e2e_live_test --all-levels --distinct-categories --maximum 4 --prepare-only
```

The tool output will confirm candidate resolution instantly:
```
Prepared native one-session level 5: 4 objects, 10 views each.
[Results Summary]
  Level 5: Category=All, Status=PREPARED, Objects=4, Launches=1, Closes=1
```

---

## 📊 Inspecting Artifacts & Evidence

The convenience wrapper defaults to a timestamped directory under
`artifacts/e2e/`:
`artifacts/e2e/run-lvl<level>-<category>-<timestamp>/`. The strict fixture
command above uses its explicit `-ArtifactsRoot`; its batch summary is
`<ArtifactsRoot>/batch.json`.

### 1. `batch.json` (Per-Level Evidence)
Contains the execution and acceptance summary, including the inventory/editor
hashes, `visualIntegrityPolicy`, per-object `taskId`, loader evidence, and
`visualIntegrityStatus`:
```json
{
  "level": 5,
  "category": "All",
  "status": "PASS",
  "editorExecutable": "D:\\Code\\project-igi-editor\\bin\\Release\\igi1ed.exe",
  "editorSha256": "343baabb9c4e90706ffbbb6bbbeea5df72f362f59bdf6465f5e3fe87effe07b4",
  "logPath": "D:\\Code\\project-igi-editor\\bin\\Release\\igi1ed.log",
  "launchCount": 1,
  "closeCount": 1,
  "objects": [
    {
      "passed": true,
      "matchingTransforms": 1,
      "matchingTransformVariants": 1,
      "assignmentRecords": 1,
      "requiredTextureLoads": [
        { "texture": "709_01_1", "loaded": true },
        { "texture": "709_02_1", "loaded": true }
      ],
      "taskId": "1234",
      "type": "Heli",
      "category": "Vehicles",
      "modelId": "709_01_1",
      "screenshotCount": 10,
      "visualIntegrityStatus": "PASS",
      "visualIntegrity": {
        "visualIntegrity": { "status": "PASS", "findings": [] }
      }
    }
  ]
}
```

### 2. Captured Screenshots
Captured images are neatly organized by object prefix in:
`artifacts/e2e/run-.../level<N>/screenshots/obj-<index>-task<id>-<model>/`

Files include the selected rendered stills, plus target-scoped evidence files:
- `Level05_Model709_01_1_Ext_000.png`
- `Level05_Model709_01_1_Ext_060.png`
- `Level05_Model709_01_1_Ext_120.png`
- `Level05_Model709_01_1_Ext_180.png`
- `Level05_Model709_01_1_Ext_240.png`
- `Level05_Model709_01_1_Ext_300.png`
- `Level05_Model709_01_1_Int_000.png`
- `Level05_Model709_01_1_Int_090.png`
- `Level05_Model709_01_1_Int_180.png`
- `Level05_Model709_01_1_Int_270.png`

For the strict native runner, each object directory also contains
`evidence.jsonl`, `visual-integrity.json`, per-view `.object-id.png` and
`.material-id.png` masks, `.depth.bin` data, and `-diagnostic.png` overlays.
Task identity is retained in the object directory name and JSON records; an
anonymous authored ID is represented literally, for example `-1#907`.

---

## 🛠️ CLI Options Reference Table

| Option | Shorthand | Default | Description |
| :--- | :--- | :--- | :--- |
| `--level <N>` | `-level <N>` | `1` | Target level number (`1` to `14`). |
| `--all-levels` | `-all-levels` | `false` | Execute test across all levels 1–14. |
| `--category <cat>` | `--categories` | `All` | Category filter: `AI`, `Buildings`, `RigidObjects`, `Vehicles`, `All`. |
| `--ai` | `-ai` | - | Shortcut for `--category AI`. |
| `--building` | `-building`, `--buildings` | - | Shortcut for `--category Buildings`. |
| `--rigid` | `-rigid`, `--rigidobjects` | - | Shortcut for `--category RigidObjects`. |
| `--vehicle` | `-vehicle`, `--vehicles` | - | Shortcut for `--category Vehicles`. |
| `--maximum <N>` | `--max-objects <N>`, `-max <N>` | `3` | Maximum number of objects to capture per level (`0` = all). |
| `--distinct-categories` | `-distinct-categories` | `false` | Picks 1 object per distinct category. |
| `--distinct-types` | `-distinct-types` | `false` | Picks 1 object per distinct object type. |
| `--views <N>` | - | `10` | Number of camera angles per object (`1` to `10`). |
| `--prepare-only` | - | `false` | Dry-run plan generation and validation without opening the game window. |
| `--artifacts <dir>` | - | auto | Custom output root directory for screenshots and evidence logs. |
| `--editor-exe <path>` | - | auto | Custom path to `igi1ed.exe` binary. |

The table above describes the `e2e_live_test.cmd` convenience surface. The
direct `Invoke-SmartNativeCaptureSession.ps1` surface additionally supports
`-GameRoot`, `-InventoryPath`, `-TaskIds`, `-VisualIntegrityPolicy` (`Required`
or `ReportOnly`), `-Video`, and `-NoDashboard`. Use the direct surface for
strict fixture selection and task-id preservation; `ReportOnly` must not be
used as a release gate.
