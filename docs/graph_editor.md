# Navigation Graph Editor

Documentation for the IGI 1 AI navigation graph (`.dat`) file format, the in-editor 3D graph overlay, and all graph-editing controls implemented in the editor.

---

## Table of Contents

1. [File Location and Naming](#1-file-location-and-naming)
2. [Binary Format (`.dat`)](#2-binary-format-dat)
3. [Node Fields and Criteria](#3-node-fields-and-criteria)
4. [Edge Records](#4-edge-records)
5. [Coordinate System](#5-coordinate-system)
6. [Graph Overlay (Editor)](#6-graph-overlay-editor)
7. [3D Rendering](#7-3d-rendering)
8. [Node Selection and Hit Detection](#8-node-selection-and-hit-detection)
9. [Node Properties Panel](#9-node-properties-panel)
10. [Keybindings](#10-keybindings)
11. [Config: QGraphNodeSize](#11-config-qgraphnodesize)
12. [Saving Graph Changes](#12-saving-graph-changes)
13. [Backup and Reset](#13-backup-and-reset)
14. [Code Reference](#14-code-reference)

---

## 1. File Location and Naming

Navigation graph files live inside each level directory under a `graphs/` subfolder:

```
<igi_root>/missions/location0/level<N>/graphs/graph<taskId>.dat
```

Where `<taskId>` matches the ID of the `AIGraph` task in the level's `objects.qsc`. A typical level contains many graph files:

- Most are sparse: `graph1.dat` through `graph<N>.dat`, many with only 1 node.
- The richest graph file (e.g. `graph4019.dat` on level 1 with 266 nodes) is the most useful for editing and testing.

The graph file name is derived from the `AIGraph` task's task ID argument (`obj.taskId`). The editor builds the path as:

```
Utils::GetIGIRootPath() + "\\missions\\location0\\level" + levelNo + "\\graphs\\graph" + taskId + ".dat"
```

---

## 2. Binary Format (`.dat`)

Confirmed by reverse engineering of the IGI 1 engine binary. All multi-byte integers are **little-endian** unless noted.

### 2.1 File Header

| Offset | Size | Type     | Value / Description                                  |
|--------|------|----------|------------------------------------------------------|
| 0x00   | 4    | uint32   | Magic: `0xFFEEDDCC` (bytes `CC DD EE FF` in file)    |
| 0x04   | 12   | record   | MaxNodes record — signature `0x04E6` (big-endian)    |
| 0x10   | 12   | record   | Second header record — signature `0x040D` (unused)   |
| 0x1C   | 2    | padding  | Two zero bytes                                       |
| 0x1E   | —    | table    | Adjacency (APSP) table — `MaxNodes × MaxNodes × 8` bytes |

### 2.2 Tagged Record Header (8 bytes each)

Every field in a node or edge is preceded by an 8-byte tagged record header:

| Offset | Size | Description                                  |
|--------|------|----------------------------------------------|
| 0x00   | 2    | Signature (big-endian uint16) — identifies field type |
| 0x02   | 2    | Sub-tag (not used by the editor)             |
| 0x04   | 4    | Unknown (not used by the editor)             |

Data payload begins at `header_offset + 8`.

### 2.3 Signature Table

| Signature (BE) | Constant             | Payload type       | Field              |
|----------------|----------------------|--------------------|--------------------|
| `0x04E6`       | `SIG_MAX_NODES`      | int32              | MaxNodes value     |
| `0x04CE`       | `SIG_NODE_ID`        | int32              | Node ID            |
| `0x0495`       | `SIG_NODE_POS`       | double × 3 (24 B)  | Node X, Y, Z       |
| `0x049C`       | `SIG_NODE_GAMMA`     | float32            | Node gamma (angle) |
| `0x0423`       | `SIG_NODE_RADIUS`    | float32            | Node radius / LinkType |
| `0x0429`       | `SIG_NODE_MAT`       | int32              | Material index     |
| `0x04E5`       | `SIG_NODE_CRIT`      | pascal string      | Criteria string    |
| `0x044A`       | `SIG_EDGE_LINK1`     | int32              | Edge: node 1 ID    |
| `0x04F6`       | `SIG_EDGE_LINK2`     | int32              | Edge: node 2 ID    |

> Note: LinkType (edge) reuses the `0x0423` signature but with a different sub-tag.

### 2.4 Adjacency Table (APSP)

Located at byte `0x1E`, immediately after the two header records and padding.

- **Size**: `MaxNodes × MaxNodes × 8` bytes
- **Entry format**: `(int32 nodeRef, float32 dist)` — each entry 8 bytes
- `nodeRef == -1` and `dist == -1.0f` indicate no connection
- This is a pre-computed All-Pairs Shortest Path (Floyd–Warshall) routing table used by the AI

The editor **preserves this table verbatim** on save — it never recomputes it.

### 2.5 Node Records

After the adjacency table, one group of tagged records per active node (in order):

1. `SIG_NODE_ID` — int32 node identifier (not necessarily sequential)
2. `SIG_NODE_POS` — three doubles: x, y, z (local coordinates)
3. `SIG_NODE_GAMMA` — float32 orientation angle
4. `SIG_NODE_RADIUS` — float32 radius (visual / influence size)
5. `SIG_NODE_MAT` — int32 surface material index (0–23)
6. `SIG_NODE_CRIT` — pascal string: 1 length byte + `<length>` bytes (last byte is null; empty criteria has length=1, byte=0x00)

### 2.6 Edge Records

After all node records, one group of tagged records per edge:

1. `SIG_EDGE_LINK1` — int32 (first node ID)
2. `SIG_EDGE_LINK2` — int32 (second node ID)
3. `0x0423` sub-tag variant — int32 link type

### 2.7 Legacy (Alternate Tagged) Format

Some graph files (e.g. level 8 `graph1.dat`, `graph7.dat`) use an older **tagged** format with **no magic number**. The editor auto-detects this format (first byte `0x05`) and parses it transparently — both formats produce the same `GraphFile` struct and support full editing.

**1-byte type tags:**

| Tag  | Type    | Size (incl. tag) |
|------|---------|-------------------|
| `0x05` | int32   | 5 bytes |
| `0x06` | float32 | 5 bytes |
| `0x08` | vec3d   | 25 bytes (3 × double) |
| `0x09` | bstring | 2 + len (1 length byte + data, last byte null) |

**Layout:**

```
header:  nodeCount(i32)  maxNodes(i32)  edgeCount(i32)
nodes:   ID  pos(v3d)  gamma  radius  f3  material  criteria
         numLinks  numLinks×targetId  numLinks×linkType
         hasGraphLink  [graphLinkTarget  graphLinkType]   (last 2 only if hasGraphLink≠0)
edges:   edgeCount × (link1  link2  linkType)
adjacency table: maxNodes × maxNodes × 8 bytes   (at END of file)
```

Key differences from the standard format: no magic, 1-byte tags instead of 8-byte record headers, and the adjacency table is at the **end** of the file rather than the start.

---

## 3. Node Fields and Criteria

```cpp
struct GraphNode {
    int    id;        // node identifier
    double x, y, z;  // position, LOCAL to the AIGraph task's world origin
    float  gamma;     // orientation angle (radians)
    float  radius;    // influence radius; also scales the 3D visual box
    int    material;  // surface material index 0–23
    std::string criteria;  // e.g. "NODECRITERIA_VIEW", "NODECRITERIA_DOOR"
    int    link1, link2;   // connected node IDs (-1 = none)
};
```

### Criteria Flags

Stored as a string in the file. Multiple flags may be combined in the string.

| Flag constant           | String substring | Visual colour in editor | Meaning               |
|-------------------------|-----------------|-------------------------|-----------------------|
| `NODECRITERIA_DOOR = 1` | `"DOOR"`        | Yellow                  | Node is near a door   |
| `NODECRITERIA_VIEW = 2` | `"VIEW"`        | Cyan                    | Node is a view point  |
| `NODECRITERIA_STAIR = 4`| `"STAIR"`       | Magenta                 | Node is on stairs     |
| (none)                  | —               | Red (default)           | Standard nav node     |

Classification precedence (when multiple match): **Door > Stair > View > Default**.

---

## 4. Edge Records

```cpp
struct GraphEdge {
    int node1;      // first node ID
    int node2;      // second node ID
    int link_type;  // engine-defined connection type
};
```

Edges are rendered in the 3D overlay as grey lines between node centres. When a node is selected, its connected edges are highlighted in orange.

---

## 5. Coordinate System

**Node coordinates are LOCAL to the AIGraph task's world position**, not absolute world coordinates.

To convert to world space for rendering:

```cpp
glm::dvec3 worldPos = graph_overlay_offset_ + glm::dvec3(n.x, n.y, n.z);
```

Where `graph_overlay_offset_` is the `obj.pos` of the `AIGraph` task object in the level scene (read from `objects.qsc`).

The editor uses `RENDERER_MODEL_SCALE_DOWN = 0.001f` to convert IGI world units to GL render units. Node positions in IGI units can exceed 7,500,000. After the 0.001 scale, 1 IGI unit = 0.001 GL units.

---

## 6. Graph Overlay (Editor)

### Activation

1. Select an `AIGraph` task in the task tree (left-click).
2. Press **F3** (`ShowGraphNodes` binding) to load and display the graph overlay.
3. Press **F3** again to hide it.

The overlay loads `graph<taskId>.dat` from the game's `graphs/` folder using the selected task's ID.

### State Fields (Renderer)

| Field                    | Type       | Description                                              |
|--------------------------|------------|----------------------------------------------------------|
| `graph_overlay_`         | `GraphFile`| Parsed graph data (nodes + edges)                        |
| `graph_overlay_offset_`  | `glm::dvec3`| World origin of the AIGraph task (node coords relative to this) |
| `graph_overlay_path_`    | `string`   | Full path to the loaded `.dat` file                      |
| `graph_overlay_taskid_`  | `string`   | Task ID (graph file stem, e.g. `"4019"`)                 |
| `graph_overlay_area_`    | `string`   | Area name from `graph_level<N>.json` lookup              |
| `graph_overlay_visible_` | `bool`     | Whether the overlay is drawn                             |
| `graph_overlay_dirty_`   | `bool`     | True if nodes have been edited but not saved             |
| `graph_overlay_selected_`| `int`      | ID of the currently selected node (-1 = none)            |

---

## 7. 3D Rendering

### Node Boxes

Each node is rendered as a **solid 3D cube** with per-face brightness shading using fixed-function OpenGL (`GL_QUADS`). The boxes are drawn in the scene pass (before the HUD) with full depth testing, so they are properly occluded by buildings.

**Box half-extent formula:**

```
H (IGI units) = QGraphNodeSize × 100 × max(1.0, node.radius)
```

Default `QGraphNodeSize = 14` → H = 1400 IGI units at radius 1.0.

The box occupies `[-H, +H]` on each axis centred at the node world position. After the `× 0.001` GL scale matrix this is `[-H×0.001, +H×0.001]` GL units.

### Face Brightness

| Face       | Multiplier | Description              |
|------------|------------|--------------------------|
| Top (Z+)   | 1.0        | Brightest                |
| Front/Back | 0.72       | Medium                   |
| Left/Right | 0.58       | Slightly darker          |
| Bottom (Z-)| 0.38       | Darkest                  |

### Node Colours

| Node kind     | RGB (approx.)       |
|---------------|---------------------|
| Selected      | Orange `(1.0, 0.6, 0.0)` |
| Door          | Yellow `(1.0, 1.0, 0.0)` |
| Stair         | Magenta `(1.0, 0.0, 1.0)` |
| View          | Cyan `(0.0, 1.0, 1.0)` |
| Default       | Red `(0.85, 0.12, 0.12)` |

### Selected Node Wireframe

The selected node also gets a yellow wireframe outline at `H × 1.08` (8% oversized) drawn with `glLineWidth(2.5)` so it stands out clearly.

### Edge Lines

Edges are drawn as 3D lines between node-box **centres** (at height `z + H`, i.e. the vertical middle of each node box) at `glLineWidth(1.5)`. This lifts them above ground level so they are always visible. Edges connected to the selected node are drawn in orange `(1.0, 0.6, 0.0)`, all others in translucent grey `(0.72, 0.72, 0.72, 0.55)`.

### Screen-space Overlays (HUD pass)

On top of the 3D boxes, the 2D HUD pass draws:
- **Rings**: small screen-space circles at the projected node centre for selected (yellow) and hovered (white) nodes
- **Node ID labels**: the numeric ID drawn next to each node's projected screen position
- **Title banner**: area name + node count at the top of the viewport
- **Hover tooltip**: node id, world position, gamma, radius, material, criteria, and edge links — shown when the cursor is within the pick radius of a node

---

## 8. Node Selection and Hit Detection

Left-clicking the 3D viewport while the graph overlay is visible picks the nearest node. The hit test uses `Renderer::PickGraphNodeAtScreen()`.

### Algorithm

For each node, the centre is projected to screen space. The screen-space half-extent of the node box is computed by also projecting `centre + (H, 0, 0)` and taking the pixel distance — this gives a pick threshold that scales with the visual box size at any camera distance. The minimum threshold is always 14 pixels (for very distant nodes).

```
screenRadius = distance( project(centre), project(centre + (H,0,0)) )
thresh       = max(14px, screenRadius)
```

The node with the smallest screen distance to the cursor that is within its own threshold is selected.

### Task Tree Toggle

Selecting a graph node closes the task tree (`show_hud_ = false`) and closes any open property panel. Clicking empty space to deselect a node re-opens the task tree (`show_hud_ = true`).

---

## 9. Node Properties Panel

Opens automatically when a node is selected (left-click). Located on the left side of the screen.

**Theme**: dark semi-transparent background, yellow `(1.0, 0.9, 0.2)` borders and labels, matching the editor's PropPanel style.

| Row       | Buttons     | Action                                             |
|-----------|-------------|---------------------------------------------------|
| Position  | X−/X+       | Nudge node ±256 IGI units along X                 |
| Position  | Y−/Y+       | Nudge node ±256 IGI units along Y                 |
| Position  | Z−/Z+       | Nudge node ±256 IGI units along Z                 |
| Gamma     | G−/G+       | Adjust orientation angle ±0.1                     |
| Radius    | R−/R+       | Adjust radius ±0.25 (also scales visual box size) |
| Material  | M−/M+       | Cycle surface material index (0–23)               |
| Criteria  | DOOR toggle | Toggle `NODECRITERIA_DOOR` flag                   |
| Criteria  | VIEW toggle | Toggle `NODECRITERIA_VIEW` flag                   |
| Criteria  | STAIR toggle| Toggle `NODECRITERIA_STAIR` flag                  |
| —         | Delete Node | Remove this node from the graph                   |
| —         | Save Graph  | Write all changes to disk immediately             |

---

## 10. Keybindings

Set in `editor/qed/qedkeybindings.qsc` via `SetEventBinding()`.

| Event name              | Default binding                       | Action                                    |
|-------------------------|---------------------------------------|-------------------------------------------|
| `ShowGraphNodes`        | `<F7>`                                | Toggle graph overlay on/off               |
| `ScaleGraphNode`        | `<LeftMouseButton><Decimal>`          | Reset selected node radius to 1.0         |
| `ScaleGraphNodeHalfe`   | `<LeftMouseButton><Divide>`           | Halve selected node radius (× 0.5)        |
| `ScaleGraphNodeDouble`  | `<LeftMouseButton><Multiply>`         | Double selected node radius (× 2.0)       |
| `CreateGraphNode`       | `<LeftMouseButton><Plus>`             | Create a new node at the current position |
| `DeleteGraphNode`       | `<LeftMouseButton><Minus>`            | Delete the selected node                  |
| `AddGraphLink`          | `<Alt><Plus>`                         | Two-step: mark source, then link to target |
| `RemoveGraphLink`       | `<Alt><Minus>`                        | Two-step: mark source, then unlink target |
| `ToggleGraphNodeLabels` | `<Alt><L>`                            | Toggle on-screen node ID labels           |

### Adding / Removing Links (Two-Step)

Links (edges) between nodes are edited in a two-step workflow:

1. **Left-click** the first node to select it (turns orange).
2. Press **Alt + +** (AddGraphLink) — the node is marked as the link source (green double ring). The status bar confirms: *"Link source: node N — select target, press Alt++ to link"*.
3. **Left-click** the second node to select it.
4. Press **Alt + +** again — a navigation edge is created between the two nodes.

To **remove** a link, use the same steps but press **Alt + −** (RemoveGraphLink) instead. If no link exists between the two nodes, the status bar reports it.

The link source is cleared automatically after the second step, or when the graph overlay is hidden/reloaded.

The `ScaleGraphNode*` bindings change `node.radius`, which also directly scales the 3D visual box because the box half-extent is `QGraphNodeSize × 100 × max(1.0, radius)`.

---

## 11. Config: QGraphNodeSize

Controls the base visual size of all graph node boxes.

**Location**: `editor/qed/qedconfig.qsc`

```
QGraphNodeSize(14);
```

- Parsed by `Config::Load()` as key `"QGraphNodeSize"` → `Config::Get().graphNodeSize` (int, min 1)
- Written by `Config::Save()` as `QGraphNodeSize(<N>);`
- Default: **14** → base box half-extent = 1400 IGI units
- Per-node effective half-extent: `graphNodeSize × 100 × max(1.0, node.radius)` IGI units

Note: this key does **not** use the `QED` prefix (unlike most config keys) because it applies specifically to the graph system, not the general editor config.

---

## 12. Saving Graph Changes

### Auto-save on Ctrl+S (`SaveObjectFile`)

While the graph overlay is visible and dirty, `Ctrl+S` saves the graph back to disk before saving the level objects file.

### Manual save from panel

The "Save Graph" button in the node properties panel also writes to disk immediately.

### Save functions

| Function       | Behaviour                                                                      |
|----------------|--------------------------------------------------------------------------------|
| `GRAPH_Save()` | Patch positions only — reads original bytes, overwrites X/Y/Z of matched node IDs, writes result. File size never changes. Fastest for position-only edits. |
| `GRAPH_Write()`| Full serializer — preserves original header + adjacency table, regenerates all node and edge tagged records from scratch. Supports add/remove/edit of nodes, criteria, radius, material, gamma, links. |

The editor currently uses `GRAPH_Save()` (position-patch) for the overlay save. `GRAPH_Write()` is available for full structural edits.

---

## 13. Backup and Reset

### Backup creation

On first level load (when the backup directory does not yet exist), the editor copies the entire `missions/location0/level<N>/` directory recursively — including the `graphs/` subfolder — to:

```
editor/backup/level<N>/
```

Controlled by `QEDBackup(TRUE)` in `qedconfig.qsc`.

If the backup already exists but the `graphs/` subfolder is missing (e.g. from an older backup created before graph editing was implemented), the editor automatically syncs `graphs/` into the backup on the next level load.

### Reset level

When the user resets the level (`ResetLevel()`), the editor:

1. Copies `editor/backup/level<N>/` back to `missions/location0/level<N>/` (recursive, overwrite), restoring all files including `graphs/*.dat`.
2. Clears the in-memory graph overlay completely (`graph_overlay_` is reset, `graph_overlay_visible_` set false).
3. Reloads the level from disk.

The in-memory overlay is also cleared on any level switch via `Renderer::BeginLoadLevel()`.

---

## 14. Code Reference

| File                                   | Responsibility                                            |
|----------------------------------------|-----------------------------------------------------------|
| `source/renderer/graph_writer.h/.cpp`   | Binary `.dat` parse (read: `igi1conv graph export`), `GRAPH_Save`, `GRAPH_Write` (full CRUD serializer) |
| `source/renderer/graph_overlay.h`      | `GraphWorldToScreen`, `GRAPH_PickNode` (pure math, unit-tested) |
| `source/renderer/graph_project.cpp`    | Implementation of `GraphWorldToScreen` and `GRAPH_PickNode` |
| `source/renderer/renderer.h`           | Graph overlay state members, public API declarations      |
| `source/renderer/renderer_draw.cpp`    | `DrawGraphNodes3D`, `DrawGraphOverlayInternal`, `DrawGraphNodePanel`, `PickGraphNodeAtScreen` |
| `source/renderer/renderer.cpp`         | `BeginLoadLevel` (clears overlay on level switch/reset)   |
| `source/app_input.cpp`                 | `ShowGraphNodes` F3 handler, `ScaleGraphNode*` handlers, Ctrl+S save |
| `source/app_input_mouse.cpp`           | Left-click node pick, task tree toggle, node panel button dispatch |
| `source/app_level.cpp`                 | Backup creation + graphs/ sync, `ResetLevel` restore      |
| `source/config.h`                      | `ConfigData::graphNodeSize` field                         |
| `source/config.cpp`                    | Parse `QGraphNodeSize`, write in `Config::Save()`         |
| `assets/editor/qed/qedconfig.qsc`      | `QGraphNodeSize(14)` default value                        |
| `tests/test_graph_overlay.cpp`         | Unit tests for `GraphWorldToScreen` and `GRAPH_PickNode`  |
| `tests/test_graph_parser.cpp`          | Round-trip parse + write tests for `.dat` format          |
