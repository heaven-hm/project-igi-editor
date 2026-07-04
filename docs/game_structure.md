[Back to README](../README.md)

# IGI Game File Organization

This page documents how files are organized in the IGI2 game directory and the three data sources available for research and conversion.

## Game Directory Layout

```
<game_install>/
├── config.qvm                         Game config (key bindings, settings)
├── lod.qvm                            LOD distances for all models
├── magicobjconfig.qvm                 Object config
├── ANIMTRIGGER/
│   └── animtrigger.qvm                Animation trigger definitions
├── COMMON/                            Shared assets across all missions
│   ├── common.dat + common.mtp        Material/texture properties (global)
│   ├── new.dat + new.mtp              Additional MTP data
│   ├── ai/                            Default AI behavior scripts
│   ├── fonts/                         .fnt files + font .res archives
│   ├── models/                        Common model .res archives
│   ├── sounds/                        Sounds (.res, .mp3, per-language)
│   ├── sprites/                       Sprite .res archives
│   └── textures/                      Common texture .res archives
├── COMPUTER/                          In-game computer terminal data
│   └── computer.res
├── HUMANPLAYER/
│   └── humanplayer.qvm                Player config (movement, physics)
├── LANGUAGE/                          Localization .res per language
│   ├── english/  french/  german/  italian/  spanish/
│   ├── danish/  finnish/  norwegian/  swedish/  USA/
│   └── (each has): computer.res, menusystem.res, messages.res,
│                    missions.res, objectives.res
├── MATERIAL/
│   └── material.qvm                   Material definitions
├── MENUSYSTEM/                        Menu UI assets
│   ├── ingamemenu.qvm + mainmenu.qvm
│   ├── menusystem.dat + menusystem.mtp
│   ├── ingamemenu.res  menusystem.res  loadingscreen.res  missionsprites.res
│   └── MODELS/  SOUND/  TEXTURES/     Menu-specific assets
├── MISSIONS/                          All game levels (see below)
├── PHYSICSOBJ/                        Vehicle definitions and scripts
│   ├── physicsobj.qvm
│   ├── cars/   (APC, Limo, buggy, cutscene_truck, t80)
│   ├── helis/  (bell, mil)
│   ├── missiles/  planes/  trains/  weapons/
├── SCREENS/                           Loading and intro screens
│   ├── game/status/status.res
│   └── intro/                         Intro images (.jpg)
└── WEAPONS/
    ├── weapon.qvm  ammo.qvm
    └── weapons.res                    Weapon sprite assets
```

## Mission Structure

### Mission Numbering

The game has 19 single-player missions across 3 locations plus 7 multiplayer maps:

| Mission IDs | Path | Description |
|-------------|------|-------------|
| 11–17 | `location1/level1–level7` | 7 missions (Location 1) |
| 21–26 | `location2/level1–level6` | 6 missions (Location 2) |
| 31–36 | `location3/level1–level6` | 6 missions (Location 3) |
| 1–5, 8 | `multiplayer/*` | 7 multiplayer maps |

Mission IDs are defined in `missions/igi2.qvm` → decompiles to `DefineMissionListItem(11..36)`.

### Multiplayer Maps

| ID | Map name |
|----|----------|
| 1 | redstone |
| 2 | forestraid |
| 3 | sandstorm |
| 4 | timberland |
| 5 | chinesetemple |
| 8 | jungle |

### Location Common Directory

Each location has a `common/` directory with shared assets used across all levels in that location:

```
missions/location1/common/
├── location1.dat + location1.mtp       Location-wide MTP data
├── models/location1.res                Shared models for all location1 levels
├── sounds/sounds.res                   Shared sound effects
└── textures/location1.res              Shared textures
```

### Level Directory Structure

Every level follows this layout:

```
missions/location1/level1/
├── objects.qvm         Level scene graph (largest file per level)
├── mission.qvm         DefineMission() — maps mission ID to paths
├── level1.dat          Level MTP data (material/texture properties)
├── level1.mtp          Level MTP companion
├── forest_<taskid>.dat  Vegetation placement (0–5 per level, named by task ID)
├── ai/                 AI scripts
│   ├── NNN.qvm         Individual soldier AI behaviors
│   └── Squad_NNN.qvm   Squad coordination scripts
├── envmaps/            Cubemap BMPs (6 faces per water object)
│   └── cubemap_NNNN_NNN_N.bmp
├── graphs/             AI navigation graphs (pathfinding meshes)
│   ├── graph<taskid>.dat      Primary nav graph — nodes, edges, routing table (named by AIGraph task ID)
│   └── graphcover<taskid>.dat Cover/visibility graph — subset of primary (same task ID)
├── heightmaps/
│   └── heightmaps.res  → extracts to heightmapNNN.thm/.tmm/.tlm
├── lightmaps/
│   └── lightmaps.res   → extracts to .olm baked lighting files
├── models/
│   └── levelN.res      → extracts to .mef and .olm model files
├── sounds/
│   ├── sounds.res      Sound effects
│   ├── *.mp3           Music and voice lines
│   ├── sounds.qvm      Sound definitions
│   └── ENGLISH/ FRENCH/ GERMAN/   Localized voice .res archives
└── textures/
    ├── levelN.res      → extracts to .tex texture files
    └── *.jpg *.tga     Loose texture files (sky, terrain, water)
```

## QSC Script Types

QSC files are decompiled from QVM bytecode. They serve different purposes depending on location:

### Global Scripts

| File | Purpose |
|------|---------|
| `config.qsc` | Game settings — version, key bindings, mouse sensitivity |
| `lod.qsc` | LOD distance thresholds per model (`ModelLODSettings`) |
| `humanplayer.qsc` | Player physics — movement speed, jump velocity, peek distance |
| `material.qsc` | Material definitions — physics properties, sound effects, visual effects |
| `weapon.qsc` | Weapon type definitions with tags and animation parameters |
| `ammo.qsc` | Ammo types — tracer colors, shop prices, casing models |
| `animtrigger.qsc` | Animation trigger definitions |
| `igi2.qsc` | Mission list — `DefineMissionListItem(11..36)` |

### Per-Level Scripts

| File | Purpose |
|------|---------|
| `objects.qsc` | **Main level scene graph** — declares all object types via `Task_DeclareParameters`, then instantiates the full scene via nested `Task_New` calls. Contains terrain, buildings, lights, soldiers, doors, forests, water, cameras, AI graphs, objectives, cutscenes. Largest file per level (up to 285 KB). |
| `mission.qsc` | One-liner: `DefineMission(id, ...)` mapping mission ID to file paths |
| `sounds/sounds.qsc` | Sound effect definitions for the level |

### AI Scripts (`ai/` directory)

| Pattern | Purpose | Example |
|---------|---------|---------|
| `NNN.qsc` | Individual soldier behavior — handles events (IDLE, CREATE) with actions like `AIAction_WalkToNode`, `AIAction_LookAtNode` | `500.qsc`: walk to node 100, look at node 69 |
| `Squad_NNN.qsc` | Squad coordination — checks squad state, area triggers, dispatches patrols | `Squad_700.qsc`: checks area activation, assigns patrol routes |

### objects.qsc Object Types

The `objects.qsc` file uses `Task_DeclareParameters` to define these object types (among others):

| Object type | Key fields |
|-------------|------------|
| `Terrain` | Position, world width/height, LOD parameters |
| `TerrainMap` | ID → `heightmapNNN.*`, grid dimensions, world coverage |
| `TerrainMaterial` | Material ID (0–7), texture paths, UV scale |
| `Forest` | Position, model, area size, tree count, scale ranges, LOD cutoff, wind LODs |
| `Water` | Position, size, UV scale, cubemap, texture paths |
| `AIGraph` | Graph position, Graphdata (node_count, capacity, edge_count), cover offsets, link params |
| `EditRigidObj` | Position, orientation, model reference |
| `Building` | Position, orientation, model, inside ambient color |
| `HumanSoldier` | Position, gamma, model, team, weapon script |
| `HumanAI` | AI type, animation type, graph ID |
| `AISquad` | Formation, squad type, alarm/gun links |
| `Door` | Start/stop positions, model, open time, pickable |
| `HumanPlayer` | Spawn position, gamma, model, team, weapons |
| `CutScene` | Camera, timing, viewport, expressions |
| `LevelFlow` | Start time, complete/failed expressions, timer |

## Task ID File Naming Convention

Binary data files generated by the editor use the `Task_New` ID from `objects.qsc` as their filename suffix. This convention links each file back to its parent task in the level's scene graph:

| File pattern | Task type | Example |
|-------------|-----------|---------|
| `forest_<id>.dat` | `Forest` | `forest_2540.dat` = `Task_New(2540, "Forest", ...)` |
| `graphs/graph<id>.dat` | `AIGraph` | `graph1.dat` = `Task_New(1, "AIGraph", "City", ...)` |
| `graphs/graphcover<id>.dat` | `AIGraph` | `graphcover1.dat` = same AIGraph task (subset) |
| `ai/<id>.qvm` | `HumanAI` | `ai/500.qvm` = `Task_New(500, "HumanAI", ...)` |
| `ai/Squad_<id>.qvm` | `AISquad` | `ai/Squad_700.qvm` = `Task_New(700, "AISquad", ...)` |

This means `objects.qsc` serves as the master index — it defines every task's parameters and implicitly names the associated data files via the task ID.

## Asset Pipeline

Understanding how game assets were originally authored helps when reverse-engineering the binary formats.

### Authoring → Compilation → Packaging

Game assets were created as human-readable source files, then compiled into optimized binary formats and packed into `.res` archives by a CLI build tool:

```
Source (authored)              Compiled (game format)         Packaged
─────────────────              ──────────────────────         ────────
model script (.qsc-like)  ──→  .mef (binary mesh)       ──→  models/levelN.res
texture script + .tga     ──→  .tex (ILFF texture)      ──→  textures/levelN.res
game logic script (.qsc)  ──→  .qvm (bytecode)               (loose files)
heightmap data            ──→  .thm/.tmm/.tlm           ──→  heightmaps/heightmaps.res
lightmap baking           ──→  .olm (object lightmaps)  ──→  lightmaps/lightmaps.res
```

- **Models** (`.mef`): Originally authored as script files (similar in concept to `.qsc`), describing geometry, materials, and bone hierarchies. The build tool compiled these into the binary `.mef` format.
- **Textures** (`.tex`): Originally `.tga` image files accompanied by a script defining texture properties (format, mipmaps, etc.). The build tool compiled the TGA + script into the binary `.tex` ILFF container.
- **Scripts** (`.qvm`): Source `.qsc` scripts compiled into QVM bytecode.

### MTP and DAT Files

Each scope level (global, location, level) has a paired `.mtp` and `.dat` file:

| Scope | MTP file | DAT file |
|-------|----------|----------|
| Global | `COMMON/common.mtp` | `COMMON/common.dat` |
| Global (new) | `COMMON/new.mtp` | `COMMON/new.dat` |
| Menu system | `MENUSYSTEM/menusystem.mtp` | `MENUSYSTEM/menusystem.dat` |
| Location | `missions/location1/common/location1.mtp` | `missions/location1/common/location1.dat` |
| Level | `missions/location1/level1/level1.mtp` | `missions/location1/level1/level1.dat` |

These files were generated as a summary output of the build tool's compilation process. They appear to contain material/texture property tables — an index of which models and textures were compiled and their associated metadata. The `.mtp` and `.dat` files always appear in pairs and are **not** packed inside `.res` archives; they sit as loose files alongside the `.res` archives they summarize.

## Data Sources

Three data sources are available in `.ignore/` for research:

| Source | Contents | File count |
|--------|----------|------------|
| `.ignore/game/` | Raw copy of game install directory — original `.res` archives and loose files exactly as installed | ~200 .res + loose files |
| `.ignore/igi2_collected.zip` | All `.res` archives exported (extracted) — files flattened from archives into their logical paths | 52,501 files |
| `.ignore/igi2_converted.zip` | Known formats converted to readable formats | 9,347 files |

### Conversion Mapping (collected → converted)

| Source format | Converted to | Count |
|---------------|-------------|-------|
| `.qvm` | `.qsc` (decompiled script) | 1,786 |
| `.tex` | `.tga` (image) | 5,394 → 5,904 .tga |
| `.spr` | `.tga` (image) | 257 → included in .tga count |
| `.pic` | `.tga` (image) | 3 → included in .tga count |
| `.fnt` | `.zip` (texture + BMFont) | 23 |
| `.wav` | `.wav` (standard PCM) | 1,634 |

### File Counts by Extension (from igi2_collected.zip)

| Extension | Count | Location | Description |
|-----------|-------|----------|-------------|
| `.olm` | 32,532 | lightmaps/, models/ | Object lightmaps |
| `.mef` | 7,609 | models/ | 3D mesh models |
| `.tex` | 5,394 | textures/ | Textures (ILFF container) |
| `.qvm` | 1,786 | ai/, root level | Compiled scripts |
| `.wav` | 1,634 | sounds/ | Audio (including ADPCM) |
| `.iff` | 1,244 | common/anims/ | Animation data |
| `.mp3` | 615 | sounds/ | Music and voice lines |
| `.dat` | 429 | level root, graphs/ | Forest (109), graph (182), graphcover (138) |
| `.syn` | 369 | sounds/per-language/ | Lip-sync envelopes |
| `.spr` | 257 | sprites/, weapons/ | Sprites (ILFF container) |
| `.bmp` | 192 | envmaps/ | Environment cubemap faces |
| `.jpg` | 119 | textures/, screens/ | Loose JPEG textures |
| `.tga` | 103 | textures/ | Loose TGA textures |
| `.thm` | 50 | heightmaps/ | Terrain height maps |
| `.tmm` | 50 | heightmaps/ | Terrain material maps |
| `.tlm` | 50 | heightmaps/ | Terrain light maps |
| `.json` | 42 | language/ | Translation strings (from .res) |
| `.fnt` | 23 | fonts/ | Bitmap font files |
| `.pic` | 3 | menusystem/ | Pictures (ILFF container) |

### The `_collision/` Folder

Both zip archives contain a `_collision/` directory with 19 files that have conflicting output paths when extracting from different `.res` archives. These are mostly:
- Duplicate fonts from `fonts_lo.res` vs `fonts_med.res` (10 files)
- Menu sprites from `menusystem.res` (9 files)

### `location0/` (Leftover Data)

The collected zip contains a `missions/location0/` with only cubemap BMP files for levels 2, 4, 5, and 7. This appears to be leftover or debug data — not a real playable location.

## See Also

- [File extensions overview](extensions.md) — conversion status per file type
- [QVM Format](format_qvm.md) — bytecode script format details
- [Terrain System](format_terrain.md) — terrain height, material, and light maps
