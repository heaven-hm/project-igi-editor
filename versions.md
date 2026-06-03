# Project IGI Editor - Version History

## Current Version: 2.7.0

---

## Version Timeline

| Version | Release Date | Status | Major Features |
|---------|--------------|--------|-----------------|
| **2.7.0** | 2026-06-04 | Latest | 3D Model Viewer, Autocomplete Task, exact keybinding match & task tree fixes |
| **2.6.0** | 2026-06-03 | Stable | Properties Editor UI, Attachments support, SplineObjs & Font/Sprite fixes |
| **2.5.0** | 2026-06-02 | Stable | Property panel scrolling, child task fields display, vertical scroll with scrollbar |
| **2.4.0** | 2026-05-28 | Stable | Position editor edge continuity, all-object picking (collision meshes), MEF attachment selection |
| **2.3.0** | 2026-05-24 | Stable | Live font toggle in pause menu, text cursor glyph alignment, variable-width font support |
| **2.2.0** | 2026-05-20 | Stable | Building interior occlusion, door/light/camera exterior selection, ancestor chain traversal |
| **2.1.0** | 2026-05-15 | Stable | Windows DPI awareness, high-DPI windowed-mode precision, pixel-perfect hit-testing |
| **2.0.0** | 2026-04-10 | Stable | Workspace navigation, map view selection, 3D viewport interaction |
| **1.9.0** | 2026-03-15 | Stable | Train engine support, locomotive orientation, track spline parsing |
| **1.8.0** | 2026-02-20 | Stable | Level cache system, texture optimization, headless CLI tools |
| **1.7.0** | 2026-01-28 | Stable | Multi-step undo/redo, hierarchical manipulation, global pivot enforcement |
| **1.6.0** | 2025-12-15 | Stable | QSC/QVM configuration system, QVM decompilation, automated compilation |
| **1.5.0** | 2025-11-10 | Stable | Asset extraction, timestamp caching, bone model geometry, collision rendering |
| **1.4.0** | 2025-09-25 | Stable | Native MEF parser, QVM decompiler, headless CLI expansion |
| **1.0.0** | 2025-08-01 | Stable | Official public release, terrain editor, task editor, AI behavior tools |

---

## Release Notes by Version

### 2.7.0 — 3D Model Viewer & Autocomplete Task
**Released:** June 4, 2026

**Overview:** This release introduces a rotating 3D Model Viewer, keyword auto-complete for task inputs, and critical keybinding and task tree fixes.

**Key Improvements:**
- 📐 **3D Model Viewer** — Added an interactive, dual-axis rotating 3D preview of the selected model directly within the model picker panel, loading assets natively from MEF.
- 🔤 **Task Autocomplete** — Added sidebar auto-complete task picker and inline keyword auto-complete suggestions for faster and safer task editing.
- ⌨️ **Exact Keybinding Match** — Refactored key binding validation to require exact modifier match, resolving keyboard shortcut collision bugs.
- 🔧 **Task Tree & Save/Load** — Added task tree file dialog for subtree export/import, task placement at camera position, and robust qedkeybindings configuration serialization.

**Files Modified:**
- `source/app.cpp` — key bindings, panels, sub-task save/load, autocomplete loading
- `source/config.cpp` — keybindings loading and saving robustness
- `source/renderer/renderer.cpp` — 3D model viewer render path, picker panels
- `source/renderer/renderer_objects.cpp` — implemented `DrawModelPreview`
- `source/utils.cpp` — exact keybinding modifiers match logic
- `tests/test_utils.cpp` — added keybinding exact matching regression tests
- `version` — bumped version string to 2.7.0

**Compatibility:** Backward compatible.

---

### 2.6.0 — Properties Editor UI, Attachments & CLI Tools
**Released:** June 3, 2026

**Overview:** This release introduces a new Properties Editor UI, support for editable Attachments, spline track fixes, and enhanced CLI tools for image and font atlas processing.

**Key Improvements:**
- 📋 **Properties Editor UI** — Added a brand new User Interface for the Properties Editor
- 🔗 **Editable Attachments** — Added support for editing and configuring Attachments objects
- 🛣️ **Road Tracks** — Fixed spline calculation and rendering issues for Road Tracks (`SplineObjs`)
- 🔤 **Editor Fonts & Sprites** — Added custom Editor Fonts and Sprites assets
- 🖼️ **CLI Image Conversion** — Direct format conversion via `--ToPng` and `--ToTga` with native PNG encoding
- 🎨 **Font Export & Concatenation** — FNT atlas extraction (`--export-png`) and active glyph string concatenation

**Files Modified:**
- `source/app.cpp` — integrated properties UI, attachments hook, key handlers
- `source/renderer/renderer_objects.cpp` — spline fixes, font/sprite rendering
- `version` — bumped version string to 2.6.0

**Compatibility:** Backward compatible.

---

### 2.5.0 — Property Panel Scrolling & Child Task Display
**Released:** June 2, 2026

**Overview:** This release adds a fully scrollable property editor with child task field display, making it easier to browse and reference large task schemas without switching views.

**Key Improvements:**
- 📋 **Vertical Panel Scrolling** — Mouse-wheel scrolling within the property panel with visual scrollbar
- 🔗 **Child Task Inline Display** — Weapon, ammo, and AI sub-task fields shown as read-only rows below parent
- 🎯 **Scroll State Persistence** — Scroll position managed per-object selection
- ✨ **GL Scissor Clipping** — Content properly clipped to panel bounds during scroll

**Files Modified:**
- `source/app.cpp` — scroll field wiring, mouse-wheel handler, scroll reset on selection
- `source/renderer/renderer.cpp` — property panel rendering with scissor test, child field display
- `source/renderer/renderer.h` — prop_panel_scroll_ field in task_tree_view_params_s

**Compatibility:** Fully backward compatible. Previous levels and configurations work without modification.

---

### 2.4.0 — Position Editor Drag & All-Object Picking
**Released:** May 28, 2026

**Overview:** This release fixes the frustrating "stuck cursor" issue when editing object positions near window edges, and enables clicking any model—even collision-only meshes.

**Key Improvements:**
- 🖱️ **Edge Continuity Logic** — Detects when cursor hits window edge (3px threshold) and substitutes last known drag delta
- ⏸️ **Per-Frame Delta Processing** — Prevents stalling when cumulative deltas hit zero at window boundary
- 🚗 **Collision-Mesh Picking** — Vehicles, cargo, and other collision-only geometry now serve as pick targets
- 🔧 **MEF Attachment Fallback** — All sub-models pickable via any mesh surface

**Files Modified:**
- `source/app.cpp` — edge-stuck detection, last-delta storage, delta reset on release
- `source/renderer/renderer_objects.cpp` — removed !fromRenderMesh guards in picking pass
- `source/renderer/renderer.h` — prop_last_drag_dx_, prop_last_drag_dy_ fields

**Behavior Change:** Objects now move continuously when cursor is stuck; clear on release.

---

### 2.3.0 — Font Toggle & Text Cursor Alignment
**Released:** May 24, 2026

**Overview:** Adds runtime font switching in the pause menu and fixes text cursor positioning to align with actual character advance widths.

**Key Improvements:**
- 🔤 **Live Font Toggle** — New pause menu button switches between Editor Font and System Font at runtime
- ✏️ **Glyph-Aware Cursor** — Text cursor calculated from per-character advance widths, not fixed pixel width
- 🧹 **Debug Button Removal** — Replaced unused Debug with functional font control
- 📐 **Variable-Width Support** — Supports both fixed and proportional fonts

**Files Modified:**
- `source/renderer/renderer.cpp` — font toggle label, text cursor calculation, measure_text_width helper
- `source/app.cpp` — font toggle click handler in pause menu
- `source/config.cpp/h` — systemFontSize configuration field

**Behavior Change:** Pause menu now has Font button; text cursors align perfectly.

---

### 2.2.0 — Building Interior Occlusion & Door Selection
**Released:** May 20, 2026

**Overview:** Improves the visibility culling system to hide only true interior sub-structures while keeping surface attachments (doors, lights, cameras) clickable from outside.

**Key Improvements:**
- 🏢 **Selective Child Occlusion** — Only hides children with isBuilding=true; doors/lights/cameras (non-building types) remain visible
- 🔍 **Ancestor Traversal** — Walks full parent chain to find nearest building ancestor instead of one-level check
- 🚪 **Exterior Attachment Support** — Building-attached objects properly selectable from outside
- 🌳 **Complex Nesting** — Sub-children of buildings now properly evaluated in picking pass

**Files Modified:**
- `source/renderer/renderer_objects.cpp` — ancestor traversal in DrawForPicking, building-interior occlusion logic

**Behavior Change:** Doors, lights, and cameras on building exteriors are now selectable from outside; interior sub-structures remain hidden.

---

### 2.1.0 — Windows DPI Scaling & Windowed-Mode Precision
**Released:** May 15, 2026

**Overview:** Fixes cursor precision issues in windowed mode on high-DPI displays (125%, 150%, and higher scaling).

**Key Improvements:**
- 💻 **DPI Awareness Declaration** — SetProcessDpiAwarenessContext for per-monitor DPI support
- 🎯 **Pixel-Perfect Hit-Testing** — Mouse coordinates match physical framebuffer pixels exactly
- 📱 **Multi-Monitor Support** — Works with external displays at different scaling factors
- 🔧 **Runtime Fallback** — Automatic DPI scale detection if OS manifest unavailable

**Files Modified:**
- `source/main.cpp` — SetProcessDpiAwarenessContext call before glutInit
- `source/app.cpp` — DPI scale member, runtime detection logic
- `source/app.h` — dpi_scale_ field

**Impact:** Sliders, textboxes, and buttons respond exactly under cursor on all DPI scaling levels.

---

## Feature Progression

### Property Editing Timeline
- **1.0.0** — Basic task property editor
- **1.7.0** — Multi-field editing, undo/redo
- **2.1.0** — DPI-aware precise hit-testing
- **2.3.0** — Text cursor alignment
- **2.4.0** — Edge-stuck drag fix
- **2.5.0** — Scrollable panel, child fields

### Picking & Selection Timeline
- **1.0.0** — Basic object selection
- **1.7.0** — Hierarchical selection support
- **2.2.0** — Building interior occlusion, door selection
- **2.4.0** — All-object picking (collision meshes)
- **2.5.0** — Child task reference display

### UI/UX Timeline
- **1.0.0** — Core HUD and task tree
- **2.1.0** — DPI precision
- **2.3.0** — Font system
- **2.5.0** — Scrollable panels

---

## Upgrading

### From 2.4.x to 2.5.0
Simply install the new version. All existing levels and configurations are fully compatible.

### From 2.0.x to 2.5.0
Full upgrade path is supported. No data migration needed. Property panel will automatically use scroll defaults.

### Legacy Versions (1.x)
Users on version 1.x should upgrade to 2.5.0 for the latest features and bug fixes.

---

## Support & Bug Reporting

For issues, bugs, or feature requests, please check the [CHANGELOGS.md](CHANGELOGS.md) file for details on what was fixed in your version.

## Development Schedule

- **Active Development:** 2.5.0 and beyond
- **Long-Term Support:** 2.0.0 and above
- **End of Life:** 1.x versions no longer receiving updates
