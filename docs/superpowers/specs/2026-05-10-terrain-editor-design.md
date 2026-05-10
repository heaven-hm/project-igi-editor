# Terrain Editor: Height Sculpting Design

## Overview
This feature transforms the Project-IGI-Terrain viewer into a basic terrain editor. It introduces an "Edit Mode" that enables users to dynamically modify the terrain height (Z-axis) by clicking and dragging on the terrain in the 3D viewport.

## Architecture & Components

### 1. State Management (`App` class)
- Introduce a boolean state `edit_mode_` to track if the editor is active.
- Introduce an enum/state for `edit_brush_type_` (e.g., `BRUSH_RAISE`, `BRUSH_LOWER`).
- When `edit_mode_` is true, `Input_OnMouse` and `Input_OnMotion` will bypass camera rotation and instead trigger raycasting and terrain deformation.

### 2. Raycasting / Picking (`Level` and `Terrain` classes)
- A new function `App::ScreenToWorldRay(int mouse_x, int mouse_y, glm::vec3& ray_origin, glm::vec3& ray_dir)` will calculate a ray from the camera through the mouse cursor using `glm::unProject`.
- A new function `Terrain::Raycast(const glm::vec3& origin, const glm::vec3& dir, glm::vec3& hit_point)` will determine where the ray intersects the terrain geometry. Since doing a full per-polygon raycast on a 128km terrain is expensive, an approximated raymarch or a bounded box check followed by localized triangle intersection will be implemented.

### 3. Data Modification (`height_map_s`)
- The original engine loads height maps into a read-only buffer (`const int8_t* height_map_item_`).
- **Change:** We will allocate a mutable buffer for height map data upon loading, or cast away `const` if the underlying memory is already heap-allocated and writable.
- A new function `Terrain::ModifyHeight(const glm::vec3& hit_point, float radius, float delta)` will find the corresponding height map cell and modify its value.
- **Note:** If a level does not have a height map loaded, the editor will either refuse to modify it, or we will need to inject a blank height map at the location. Initially, we will restrict modification to areas covered by existing height maps (like Level 1 or 6) for simplicity.

### 4. UI / Menus (`main.cpp`)
- Add an "Editor Tools" sub-menu to the existing right-click context menu.
- Menu items:
  - Toggle Edit Mode
  - Brush: Raise Terrain
  - Brush: Lower Terrain

## Data Flow
1. User right-clicks -> Enables Edit Mode.
2. User left-clicks and drags -> `App::Input_OnMotion` calculates mouse coordinates.
3. `App` generates a 3D Ray and calls `Level::Raycast`.
4. `Terrain::Raycast` finds the 3D `hit_point`.
5. `Terrain::ModifyHeight` takes the `hit_point`, identifies the target height map, and increments/decrements the byte values in the height map buffer.
6. The next frame, `Terrain::Update` evaluates the updated height map data when generating vertex buffers, reflecting the changes visually.

## Error Handling & Edge Cases
- **Missing Height Map:** If the user clicks on a part of the terrain that does not have an active height map node, the edit will be ignored (no-op) to prevent out-of-bounds memory writes.
- **Buffer Overflows:** Height map values are stored as `int8_t` (signed 8-bit, -128 to 127). The modification logic will clamp values to prevent overflow/underflow wrap-around glitches.

## Testing Strategy
- Verify that toggling Edit Mode correctly disables camera look.
- Verify that Raycasting hits the ground accurately under the crosshair.
- Visually verify that clicking on the terrain in Level 6 modifies the mesh in real-time.
