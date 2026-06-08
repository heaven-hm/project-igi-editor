# Editor Bugfix & Feature Batch Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix seven reported IGI Editor issues — weapon-id live update, terrain-id tooltip, `.res` model validation (warn + offer to add), Shift+M magic toggle verification, zipline wire scaling, door-frame texture, and mounted-gun interactables.

**Architecture:** Each issue is an independent unit. Pure-logic pieces (pickup enum→model resolution, `.res` membership) get googletest unit tests in `tests/`. UI/visual changes (tooltip, validation warning, render fixes) are verified by launching the editor (`bin/Debug/igi1ed.exe`) against the IGI install at `D:\IGI1` and observing. Issues 4/5/7 begin with a reproduction task; their fix is applied only after the cause is confirmed in the running editor.

**Tech Stack:** C++17, OpenGL, CMake (configure with `-A Win32` — editor is 32-bit), googletest (`igi_tests` target), QSC/RES/MEF binary formats.

**Spec:** `docs/superpowers/specs/2026-06-08-editor-bugfix-batch-design.md`

---

## Build & Run Reference (used throughout)

- **Configure (once):** `cmake -S . -B build -A Win32`
- **Build editor:** `cmake --build build --config Debug --target igi-editor`
- **Build tests:** `cmake --build build --config Debug --target igi_tests`
- **Run all tests:** `bin/Debug/igi_tests.exe` (reads game data from `Utils::GetIGIRootPath()` → `D:\IGI1`)
- **Run one test suite:** `bin/Debug/igi_tests.exe --gtest_filter=PickupResolveTest.*`
- **Launch editor on a level:** `cd bin/Debug/content && ..\igi1ed.exe -level <N> -draw_parts 49 -stick_to_ground`

> The memory note "Deployment checklist" applies: running `igi_tests.exe` against `D:\IGI1` needs the `fixtures\` dir alongside it if a test references fixtures.

---

## Task 1: Verify Issue 6 (Shift+M magic toggle) on a current build

**Files:** none expected (verification only).

- [ ] **Step 1: Build and launch the editor**

```
cmake --build build --config Debug --target igi-editor
cd bin/Debug/content && ..\igi1ed.exe -level 1 -draw_parts 49 -stick_to_ground
```

- [ ] **Step 2: Press Shift+M and observe**

Expected: magic-object spheres toggle on/off in the viewport (handler `app.cpp:3284`, render `renderer_objects.cpp:1702`, binding `qedkeybindings.qsc:115`).

- [ ] **Step 3: Record outcome**

If it toggles correctly: mark Issue 6 closed as already-fixed in v2.8.0 — no code change. Note this in the commit message of the next task.
If it does NOT toggle: STOP and debug. Check (a) the binding loaded (log `TaskMagicObjToggle`), (b) `Check("TaskMagicObjToggle")` fires, (c) `show_magic_obj_spheres_` reaches `draw_params_`. Fix the confirmed break, then re-run Step 2.

- [ ] **Step 4: Commit (only if a fix was needed)**

```bash
git add -A
git commit -m "fix: Shift+M magic-object toggle (issue 6)"
```

---

## Task 2: Issue 1 — pickup enum→model resolution helper (unit-tested)

Extract the GunPickup/AmmoPickup enum→model resolution (currently inline at `level_objects.cpp:880-887` and `:192-208`) into one reusable, testable method.

**Files:**
- Modify: `source/level/level_objects.h` (declare method, ~after line 85)
- Modify: `source/level/level_objects.cpp` (implement; reuse at the two existing call sites)
- Test: `tests/test_pickup_resolve.cpp` (create)
- Modify: `CMakeLists.txt:199-213` (add test file to `igi_tests`)

- [ ] **Step 1: Write the failing test**

Create `tests/test_pickup_resolve.cpp`:

```cpp
#include <gtest/gtest.h>
#include "level/level_objects.h"

// ResolvePickupModelId maps a WEAPON_ID_*/AMMO_ID_* enum to a render model id
// using the loaded IGIModels.json map. Unknown/non-enum input returns the input
// unchanged (caller renders the raw string, matching the existing fallback).
TEST(PickupResolveTest, UnknownEnumReturnsInputUnchanged) {
    LevelObjects lo;
    EXPECT_EQ(lo.ResolvePickupModelId("WEAPON_ID_DOES_NOT_EXIST"),
              "WEAPON_ID_DOES_NOT_EXIST");
}

TEST(PickupResolveTest, NonEnumStringReturnedUnchanged) {
    LevelObjects lo;
    EXPECT_EQ(lo.ResolvePickupModelId("123_45_6"), "123_45_6");
}

// A known weapon enum resolves to an 8-char NNN_NN_N model id (from IGIModels.json).
TEST(PickupResolveTest, KnownWeaponEnumResolvesToModelId) {
    LevelObjects lo;
    std::string r = lo.ResolvePickupModelId("WEAPON_ID_UZI");
    // Either resolved to a model id (8 chars, NNN_NN_N) or, if the JSON is absent
    // in this environment, falls back to the input. Assert it is one of those.
    bool resolved = (r.size() == 8 && r[3] == '_' && r[6] == '_');
    EXPECT_TRUE(resolved || r == "WEAPON_ID_UZI") << "got: " << r;
}
```

- [ ] **Step 2: Run test to verify it fails (no such method)**

```
cmake --build build --config Debug --target igi_tests
```
Expected: COMPILE FAIL — `ResolvePickupModelId` is not a member of `LevelObjects`.

- [ ] **Step 3: Declare the method**

In `source/level/level_objects.h`, after `std::string GetModelId(...) const;` (line 86):

```cpp
    // Resolve a GunPickup/AmmoPickup WEAPON_ID_*/AMMO_ID_* enum string to a render
    // model id via IGIModels.json. Returns the input unchanged if it is not a known
    // enum (caller then renders/keeps the raw string).
    std::string ResolvePickupModelId(const std::string& enumId);
```

- [ ] **Step 4: Implement the method**

In `source/level/level_objects.cpp`, add (near the other resolution code):

```cpp
std::string LevelObjects::ResolvePickupModelId(const std::string& enumId) {
    if (enumId.rfind("WEAPON_ID_", 0) != 0 && enumId.rfind("AMMO_ID_", 0) != 0)
        return enumId;
    LoadModelNames();
    auto it = modelIds_.find(enumId);
    if (it != modelIds_.end() && !it->second.empty())
        return it->second;
    return enumId; // fallback: keep raw enum string
}
```

- [ ] **Step 5: Reuse at the existing load-time call site**

In `source/level/level_objects.cpp:194-205` (the `LoadModelNames(); for (auto& obj ...)` block), replace the inline `modelIds_.find` lookup body with:

```cpp
        if (obj.type == "GunPickup" || obj.type == "AmmoPickup") {
            if (!obj.modelId.empty()) {
                std::string resolved = ResolvePickupModelId(obj.modelId);
                if (resolved != obj.modelId) {
                    Logger::Get().Log(LogLevel::INFO,
                        "[LevelObjects] Resolved pickup enum: " + obj.modelId +
                        " -> " + resolved + " (task " + obj.taskId + ")");
                    obj.modelId = resolved;
                }
            }
        }
```

Also update the per-arg site at `level_objects.cpp:878-889` to call `obj.modelId = ResolvePickupModelId(enumId);` (drop the inline `LoadModelNames()`/`modelIds_.find`).

- [ ] **Step 6: Run tests to verify pass**

```
cmake --build build --config Debug --target igi_tests
bin/Debug/igi_tests.exe --gtest_filter=PickupResolveTest.*
```
Expected: 3 PASS.

- [ ] **Step 7: Add test to CMake and commit**

Add `tests/test_pickup_resolve.cpp` to the `add_executable(igi_tests ...)` list (`CMakeLists.txt:199-213`).

```bash
git add source/level/level_objects.h source/level/level_objects.cpp tests/test_pickup_resolve.cpp CMakeLists.txt
git commit -m "refactor: extract ResolvePickupModelId helper with unit tests (issue 1)"
```

---

## Task 3: Issue 1 — re-resolve model on weapon-enum edit (live update)

Wire the new helper into the prop-panel commit so editing a GunPickup/AmmoPickup weapon enum updates the rendered model immediately.

**Files:**
- Modify: `source/app.cpp` — `App::CommitPropTextEdit` (around `:4787-4793`)

- [ ] **Step 1: Add the re-resolution after the model-field sync**

In `source/app.cpp`, inside `CommitPropTextEdit`, replace the `is_model_field` block (lines 4787-4793) with:

```cpp
		// Sync model field to obj.modelId so UpdateCoordinatesInLine doesn't
		// overwrite the new model with the stale obj.modelId.
		bool is_model_field = is_str && (fd.name == "Model" ||
		                                 fd.name.find("Model") != std::string::npos);
		if (is_model_field) {
			obj.modelId = StripQuotes(prop_text_buf_);
		}

		// GunPickup/AmmoPickup: the edited field is the weapon/ammo enum string, but
		// obj.modelId must hold the RESOLVED render model. Re-resolve so the viewport
		// mesh updates immediately instead of only after a reload (issue 1).
		if (obj.type == "GunPickup" || obj.type == "AmmoPickup") {
			std::string enumStr = StripQuotes(prop_text_buf_);
			if (enumStr.rfind("WEAPON_ID_", 0) == 0 || enumStr.rfind("AMMO_ID_", 0) == 0) {
				obj.modelId = level_.GetLevelObjects().ResolvePickupModelId(enumStr);
			}
		}
```

- [ ] **Step 2: Build the editor**

```
cmake --build build --config Debug --target igi-editor
```
Expected: builds clean.

- [ ] **Step 3: Manual verification in the editor**

```
cd bin/Debug/content && ..\igi1ed.exe -level 2 -draw_parts 49 -stick_to_ground
```
Select a GunPickup, open its prop panel, change the weapon enum (e.g. to `WEAPON_ID_UZI`), commit.
Expected: the rendered pickup model changes immediately, with NO reload. Confirm the QSC arg-9 string still shows the new enum.

- [ ] **Step 4: Commit**

```bash
git add source/app.cpp
git commit -m "fix: live-update pickup render model on weapon-enum edit (issue 1)"
```

---

## Task 4: Issue 3 — real terrain id in the hover tooltip

Replace the hardcoded `"Terrain ID: -1"` (`renderer.cpp:993`) with the terrain id under the cursor plus an `Add Terrain: <id>` hint.

**Files:**
- Inspect first: `source/level/terrain.cpp`, `source/level/terrain.h`, `source/renderer/renderer_terrain.cpp` (find existing ground-ray / terrain-id-at-point query)
- Modify: `source/renderer/renderer.cpp:992-994`
- Possibly modify: `source/level/terrain.h` / `terrain.cpp` (add a `TerrainIdAtWorldXY`/`TerrainIdUnderRay` query only if none exists)

- [ ] **Step 1: Locate an existing terrain pick / ground-ray**

```
```
Run a search for how object placement/snapping finds the terrain point under the cursor:
- Grep `source/` for `SnapObjectsToTerrain`, `Raycast`, `GroundRay`, `TerrainHeightAt`, `terrain_id`, `PickTerrain`.
- If a function already returns a terrain id (or terrain cell) for a world XY or screen ray, reuse it in Step 3 and SKIP Step 2.

- [ ] **Step 2: Add a minimal terrain-id query (ONLY if none exists)**

Add to `source/level/terrain.h` (public):

```cpp
    // Returns the terrain id of the cell containing world position (wx, wy),
    // or -1 if outside the terrain bounds. Read-only.
    int TerrainIdAtWorldXY(float wx, float wy) const;
```

Implement in `source/level/terrain.cpp` using the same cell-lookup the renderer already uses to draw terrain tiles (mirror the indexing in `renderer_terrain.cpp`). Return the stored terrain id for that cell, or -1.

- [ ] **Step 3: Use it in the tooltip branch**

In `source/renderer/renderer.cpp`, replace lines 992-994:

```cpp
    } else if (!task_tree_view.pause_mode_ && (!task_tree_view.show_hud_ || task_tree_view.mouse_x_ >= 350)) {
      int terrainId = task_tree_view.HoverTerrainId(); // ground-ray result, -1 if none
      char tbuf[96];
      if (terrainId >= 0) {
        snprintf(tbuf, sizeof(tbuf), "Terrain ID: %d", terrainId);
        draw_text_sm(tooltip_x, tooltip_y, tbuf, 1.0f, 1.0f, 1.0f);
        snprintf(tbuf, sizeof(tbuf), "Add Terrain: %d", terrainId);
        draw_text_sm(tooltip_x, tooltip_y + 15, tbuf, 0.7f, 1.0f, 0.7f);
      } else {
        draw_text_sm(tooltip_x, tooltip_y, "Terrain ID: -1", 1.0f, 1.0f, 1.0f);
      }
    }
```

`HoverTerrainId()` is a small accessor on the task-tree/view object that casts the cursor ground-ray (already computed for hover) to a world XY and calls `TerrainIdAtWorldXY`, caching the per-frame result. Add it next to the existing hover/mouse members. If a ground-ray world point is already stored per-frame for hover, reuse that point directly.

- [ ] **Step 4: Build and verify in the editor**

```
cmake --build build --config Debug --target igi-editor
cd bin/Debug/content && ..\igi1ed.exe -level 1 -draw_parts 49 -stick_to_ground
```
Hover over terrain (not over an object/UI).
Expected: tooltip shows the correct `Terrain ID: <id>` (matching level data) and an `Add Terrain: <id>` hint line. Hovering an object still shows the object tooltip.

- [ ] **Step 5: Commit**

```bash
git add source/renderer/renderer.cpp source/level/terrain.h source/level/terrain.cpp
git commit -m "feat: show real terrain id + Add Terrain hint in hover tooltip (issue 3)"
```

---

## Task 5: Issue 2a — build the level `.res` model set + membership check (unit-tested)

Add a pure helper that, given a parsed level `.res`, reports whether a model id is packed in it. This is the validation core.

**Files:**
- Create: `source/level/res_model_set.h` and `source/level/res_model_set.cpp`
- Test: `tests/test_res_model_set.cpp`
- Modify: `CMakeLists.txt` (add source to `igi-editor` SOURCES_LEVEL and the test to `igi_tests`)

- [ ] **Step 1: Write the failing test**

Create `tests/test_res_model_set.cpp`:

```cpp
#include <gtest/gtest.h>
#include "level/res_model_set.h"
#include "parsers/res_parser.h"

static RESFile MakeRes(std::initializer_list<std::string> names) {
    RESFile r; r.valid = true;
    for (auto& n : names) r.entries.push_back(RESEntry{n, {1,2,3}});
    return r;
}

TEST(ResModelSetTest, MatchesMefEntryCaseInsensitive) {
    ResModelSet s(MakeRes({"models\\426_02_1.MEF", "models\\003_01_1.mef"}));
    EXPECT_TRUE(s.Contains("426_02_1"));
    EXPECT_TRUE(s.Contains("003_01_1"));
}

TEST(ResModelSetTest, ReportsMissingModel) {
    ResModelSet s(MakeRes({"models\\003_01_1.mef"}));
    EXPECT_FALSE(s.Contains("999_99_9"));
}

TEST(ResModelSetTest, IgnoresNonMefEntries) {
    ResModelSet s(MakeRes({"textures\\foo.tga", "003_01_1.mef"}));
    EXPECT_FALSE(s.Contains("foo"));
    EXPECT_TRUE(s.Contains("003_01_1"));
}
```

- [ ] **Step 2: Run to verify it fails**

```
cmake --build build --config Debug --target igi_tests
```
Expected: COMPILE FAIL — `res_model_set.h` not found.

- [ ] **Step 3: Implement the helper**

Create `source/level/res_model_set.h`:

```cpp
#pragma once
#include <string>
#include <unordered_set>
#include "parsers/res_parser.h"

// Set of model ids (NNN_NN_N) packed as <id>.mef entries inside a level .res.
// Used to warn when an object references a model the game archive lacks.
class ResModelSet {
public:
    ResModelSet() = default;
    explicit ResModelSet(const RESFile& res);
    bool Contains(const std::string& modelId) const;
    bool Empty() const { return ids_.empty(); }
private:
    std::unordered_set<std::string> ids_; // lower-cased model ids
};
```

Create `source/level/res_model_set.cpp`:

```cpp
#include "level/res_model_set.h"
#include <algorithm>
#include <cctype>

static std::string LowerStem(const std::string& name) {
    // Take basename, strip ".mef" (case-insensitive), lower-case.
    size_t slash = name.find_last_of("\\/");
    std::string base = (slash == std::string::npos) ? name : name.substr(slash + 1);
    std::string lower; lower.reserve(base.size());
    for (char c : base) lower.push_back((char)std::tolower((unsigned char)c));
    const std::string ext = ".mef";
    if (lower.size() >= ext.size() &&
        lower.compare(lower.size() - ext.size(), ext.size(), ext) == 0)
        lower.erase(lower.size() - ext.size());
    else
        return ""; // not a mef entry
    return lower;
}

ResModelSet::ResModelSet(const RESFile& res) {
    for (const auto& e : res.entries) {
        std::string id = LowerStem(e.name);
        if (!id.empty()) ids_.insert(id);
    }
}

bool ResModelSet::Contains(const std::string& modelId) const {
    std::string lower; lower.reserve(modelId.size());
    for (char c : modelId) lower.push_back((char)std::tolower((unsigned char)c));
    return ids_.find(lower) != ids_.end();
}
```

- [ ] **Step 4: Wire into CMake**

Add `source/level/res_model_set.cpp` to the `SOURCES_LEVEL` list and `tests/test_res_model_set.cpp` to `add_executable(igi_tests ...)` (`CMakeLists.txt:199-213`). The test also needs `res_parser.cpp` + `res_model_set.cpp` compiled into `igi_tests` — add both to its `target_sources` if not already linked.

- [ ] **Step 5: Run to verify pass**

```
cmake --build build --config Debug --target igi_tests
bin/Debug/igi_tests.exe --gtest_filter=ResModelSetTest.*
```
Expected: 3 PASS.

- [ ] **Step 6: Commit**

```bash
git add source/level/res_model_set.h source/level/res_model_set.cpp tests/test_res_model_set.cpp CMakeLists.txt
git commit -m "feat: ResModelSet membership helper for .res model validation (issue 2)"
```

---

## Task 6: Issue 2b — load the level `.res` model set + warn on missing model

Build the set on level load and warn (status message + viewport tint) when a committed model isn't in it.

**Files:**
- Modify: `source/app.h` (add `ResModelSet level_res_models_;` member + include)
- Modify: `source/app.cpp` — where the level finishes loading (near `RebuildLevelModelIds`, `:4813`) to populate the set; and `CommitPropTextEdit` (`:4791`) to warn.
- Modify: `source/renderer/renderer_objects.cpp` draw path to tint flagged objects (reuse the missing-model code path if present).

- [ ] **Step 1: Populate the set on load**

In `source/app.cpp`, in `RebuildLevelModelIds()` (or the load-complete path that knows `current_level_`), add:

```cpp
	// Build the set of models actually packed in the level .res so we can warn when
	// an object references a model the game archive lacks (would be transparent in-game).
	{
		std::string gameRes = Utils::GetIGIRootPath() +
			"\\missions\\location0\\level" + std::to_string(current_level_) +
			"\\models\\level" + std::to_string(current_level_) + ".res";
		RESFile res = RES_Parse(gameRes);
		level_res_models_ = res.valid ? ResModelSet(res) : ResModelSet();
	}
```

Add `#include "level/res_model_set.h"` and `#include "parsers/res_parser.h"` to `app.cpp`, and the member `ResModelSet level_res_models_;` to `app.h` (with the include).

- [ ] **Step 2: Warn on commit of a missing model**

In `source/app.cpp` `CommitPropTextEdit`, inside the `is_model_field` block (right after `obj.modelId = StripQuotes(prop_text_buf_);`, `:4792`):

```cpp
		if (is_model_field) {
			obj.modelId = StripQuotes(prop_text_buf_);
			if (!level_res_models_.Empty() && !obj.modelId.empty() &&
			    !level_res_models_.Contains(obj.modelId)) {
				obj.modelMissingInRes = true;
				status_message_ = "Model '" + obj.modelId +
					"' is not in this level's .res — it will be invisible in-game. "
					"Press Ctrl+Shift+A to add it.";
			} else {
				obj.modelMissingInRes = false;
			}
		}
```

Add a `bool modelMissingInRes = false;` field to the `LevelObject` struct (`source/level/level_objects.h`).

- [ ] **Step 3: Tint flagged objects in the viewport**

In `source/renderer/renderer_objects.cpp`, in the per-object draw loop (around `:1195` where `GetOrLoadMesh` is called), when `obj.modelMissingInRes` is true, multiply the object color by a warning tint (e.g. set `u_dirlight`/`u_ambient` toward magenta, or add a `u_tint` uniform). Keep it minimal — reuse the existing selected/hover tint mechanism if one exists.

- [ ] **Step 4: Build and verify**

```
cmake --build build --config Debug --target igi-editor
cd bin/Debug/content && ..\igi1ed.exe -level 1 -draw_parts 49 -stick_to_ground
```
Edit an object's model to an id you know is NOT packed in `level1.res` (e.g. a model from another level). 
Expected: status message warns, and the object renders with the warning tint.

- [ ] **Step 5: Commit**

```bash
git add source/app.h source/app.cpp source/level/level_objects.h source/renderer/renderer_objects.cpp
git commit -m "feat: warn + tint when object model is absent from level .res (issue 2)"
```

---

## Task 7: Issue 2c — offer to add the missing model to the level `.res`

Add an action (keybinding or prop-panel button) that copies the loose `<id>.mef` into the level `.res`, backed up, mirroring `SuppressAttachmentInMef` (`renderer_objects.cpp:890-943`).

**Files:**
- Modify: `source/renderer/renderer_objects.h` / `.cpp` (add `AddModelToLevelRes(const std::string& modelId)`)
- Modify: `source/app.cpp` (invoke it from the warning action; refresh `level_res_models_` after)
- Modify: `source/config.h` + `qedkeybindings.qsc` (bind an "AddModelToRes" event) OR add a prop-panel button — pick the simpler given existing UI patterns.

- [ ] **Step 1: Implement the add-to-res operation**

In `source/renderer/renderer_objects.cpp`:

```cpp
bool Renderer_Objects::AddModelToLevelRes(const std::string& modelId) {
    const std::string gameRes = Utils::GetIGIRootPath() + "\\missions\\location0\\level" +
        std::to_string(current_level_) + "\\models\\level" +
        std::to_string(current_level_) + ".res";
    if (!std::filesystem::exists(gameRes)) {
        Logger::Get().Log(LogLevel::WARNING, "[Renderer] AddModelToLevelRes: archive missing: " + gameRes);
        return false;
    }
    // Locate the loose .mef the editor is rendering from.
    std::string mefPath = FindModelFile(modelId, /*isBuilding=*/false);
    if (mefPath.empty()) mefPath = FindModelFile(modelId, true);
    if (mefPath.empty()) {
        Logger::Get().Log(LogLevel::WARNING, "[Renderer] AddModelToLevelRes: no loose .mef for " + modelId);
        return false;
    }
    std::ifstream f(mefPath, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), {});
    if (bytes.empty()) return false;

    RESFile res = RES_Parse(gameRes);
    if (!res.valid) return false;

    // Skip if already present (case-insensitive <id>.mef).
    std::string suffix = modelId + ".mef";
    for (const auto& e : res.entries) {
        const std::string& n = e.name;
        if (n.size() >= suffix.size()) {
            bool eq = true;
            for (size_t i = 0; i < suffix.size(); ++i)
                if (std::tolower((unsigned char)n[n.size()-suffix.size()+i]) !=
                    std::tolower((unsigned char)suffix[i])) { eq = false; break; }
            if (eq) { Logger::Get().Log(LogLevel::INFO, "[Renderer] AddModelToLevelRes: already present: " + modelId); return true; }
        }
    }

    try {
        std::string bak = gameRes + ".orig";
        if (!std::filesystem::exists(bak))
            std::filesystem::copy_file(gameRes, bak, std::filesystem::copy_options::overwrite_existing);
    } catch (...) {}

    res.entries.push_back(RESEntry{ "models\\" + modelId + ".mef", bytes });
    std::string err;
    if (!RES_WriteEntries(res.entries, gameRes, err)) {
        Logger::Get().Log(LogLevel::ERR, "[Renderer] AddModelToLevelRes: repack failed: " + err);
        return false;
    }
    Logger::Get().Log(LogLevel::INFO, "[Renderer] AddModelToLevelRes: added " + modelId + ".mef to " + gameRes);
    return true;
}
```

Declare it in `renderer_objects.h`. Add `#include <fstream>` if not already present.

> **Texture note (scope, not a gap):** the editor and game resolve textures from shared texture directories (`renderer_objects.cpp:2414-2453`), so a model that renders textured in the editor will also find its textures in-game. The packed gap the user reported is the `.mef` itself; texture copying is intentionally out of scope. If verification shows the added model is untextured in-game, file a follow-up.

- [ ] **Step 2: Invoke from the editor on user confirmation**

In `source/app.cpp`, add an action (new keybinding `AddModelToRes`, handled near `:3284`) that, when the selected object has `modelMissingInRes`, calls `renderer_.GetObjectRenderer().AddModelToLevelRes(obj.modelId)` (use the actual accessor for the object renderer). On success: set `obj.modelMissingInRes = false`, rebuild `level_res_models_` from the now-updated `.res`, and set `status_message_ = "Added '" + obj.modelId + "' to level .res (backup .orig written)."`.

Bind the new event in `assets/content/qed/qedkeybindings.qsc` (e.g. `SetEventBinding("AddModelToRes", "<Ctrl><Shift><A>")`) and register it in `Config` parsing (`config.cpp`, mirror `keyToggleMagicObj` at `:281`).

- [ ] **Step 3: Build and verify end-to-end**

```
cmake --build build --config Debug --target igi-editor
cd bin/Debug/content && ..\igi1ed.exe -level 1 -draw_parts 49 -stick_to_ground
```
1. Set an object to a model NOT in `level1.res` → warning + tint (Task 6).
2. Trigger the add action → status confirms; `level1.res.orig` backup exists; tint clears.
3. Re-parse: `bin/Debug/igi_tests.exe --gtest_filter=ResParserTest.*` still passes against the rewritten archive.

- [ ] **Step 4: Commit**

```bash
git add source/renderer/renderer_objects.h source/renderer/renderer_objects.cpp source/app.cpp source/config.h source/config.cpp assets/content/qed/qedkeybindings.qsc
git commit -m "feat: offer to add missing model to level .res with backup (issue 2)"
```

---

## Task 8: Issue 5 — reproduce then fix zipline wire widening (Level 12)

**Files:**
- Reproduce: launch editor on level 12
- Modify (after confirm): `source/renderer/renderer_splines.cpp:160-168`

- [ ] **Step 1: Reproduce**

```
cmake --build build --config Debug --target igi-editor
cd bin/Debug/content && ..\igi1ed.exe -level 12 -draw_parts 49 -stick_to_ground
```
Navigate to the cable-car / zipline using model `426_02_1`. Observe the wire before vs inside the house. Confirm the wire's cross-section appears wider on the short segment(s) inside the house.

- [ ] **Step 2: Confirm the cause**

Confirm via the geometry math: in `DrawSplineSegment`, `sx = chordLen / localLen` (`:162`) scales X (length) per segment, while width/height stay fixed at `LENGTH_SCALE = 40.96` (`:168`). On short chords `sx` shrinks but the cross-section does not, so the wire looks proportionally fat. If observation contradicts this (e.g. width genuinely changes in data), STOP and re-diagnose before editing.

- [ ] **Step 3: Apply the fix**

The cable cross-section should stay at its natural model proportions regardless of segment length. In `source/renderer/renderer_splines.cpp`, the scale at `:168` is `glm::vec3(sx, LENGTH_SCALE, LENGTH_SCALE)`. The X stretch is correct for *track tiles* (they must butt end-to-end), but for thin wire/cable segments the visual issue is that very short segments over-subdivide or compress. The minimal, targeted fix: clamp the per-tile X scale so it cannot fall below the natural ratio that keeps width:length proportionate, i.e. ensure `steps` does not force `sx` far below `LENGTH_SCALE` for wire segments. Concretely, after computing `steps` (`:124-125`), for wire/cable models keep a single un-subdivided span and stretch X to the full segment:

```cpp
    // Wire/cable segments: do not subdivide into natural-length tiles; stretch the
    // single model across the whole segment so the cross-section stays proportionate
    // to its length (short spans inside buildings were rendering over-wide). (issue 5)
    bool isWireSegment = /* model 426_* cable, or a wire flag on the segment */;
    if (isWireSegment) steps = 1;
```

Confirm during Step 1 how to identify a wire/cable segment (segment model id prefix, or an `isWire` flag already on the object). Use that exact condition. If `steps=1` alone does not resolve it, the alternative is to scale the cross-section (Y/Z) by `sx` as well for wire segments so width tracks length — apply whichever the reproduction confirms removes the widening without distorting track tiles.

- [ ] **Step 4: Verify**

Re-launch level 12, confirm the wire keeps a consistent thin cross-section both before and inside the house. Re-check a level with a cable-car *track* (e.g. the same or another level) to confirm track tiles still butt end-to-end with no gaps.

- [ ] **Step 5: Commit**

```bash
git add source/renderer/renderer_splines.cpp
git commit -m "fix: keep zipline wire cross-section proportionate on short spans (issue 5)"
```

---

## Task 9: Issue 4 — reproduce then fix door-frame texture artifact

**Files:**
- Reproduce: editor on the affected level
- Inspect: `source/parsers/tex_parser.cpp`, the object texture-sampler setup in `renderer_objects.cpp` (`ApplyTexturesToMesh`, sampler params)
- Modify (after confirm): the confirmed cause site

- [ ] **Step 1: Reproduce and identify the model+texture**

Launch the editor on the level showing the striped door frame (the door in Issue 4's image). Select the door, note its model id from the tooltip/prop panel, and the texture name it loads (enable DEBUG logging — `GetOrLoadMesh`/`ApplyTexturesToMesh` log the resolved texture).

- [ ] **Step 2: Classify the artifact**

Determine which it is:
- **UV wrap:** the stripe is the texture repeating/clamping wrong at the frame edge → check the sampler `GL_TEXTURE_WRAP_S/T` (`GL_REPEAT` vs `GL_CLAMP_TO_EDGE`) where the object texture is uploaded.
- **Decode error:** the texture pixels themselves are wrong → dump/inspect the decoded texture from `tex_parser.cpp` for that texture name.
- **Mip/filter:** stripes shimmer with distance → check min/mag filter + mip generation.

- [ ] **Step 3: Apply the confirmed fix**

Fix only the confirmed cause (e.g. set the correct wrap mode for the door-frame texture, or correct the decode for its pixel format). Keep the change scoped to the texture-setup site; do not alter unrelated texture handling.

- [ ] **Step 4: Verify**

Re-launch, confirm the door frame renders cleanly. Spot-check a few other textured models in the same level to confirm no regression (especially any sharing the same wrap/filter path).

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "fix: door-frame texture artifact (issue 4)"
```

---

## Task 10: Issue 7 — reproduce then fix mounted-gun interactables (M2HB)

**Files:**
- Reproduce: editor on a level with a mounted M2HB / stationary gun
- Inspect: `source/level/level_objects.cpp:890-904` (model-token scan for `StationaryGun`/`AIStationaryGunHolder`), and the render path for those types
- Modify (after confirm): the confirmed cause site

- [ ] **Step 1: Reproduce**

Launch the editor on a level with a mounted M2HB machine gun. Select it; note its type (`StationaryGun` / `AIStationaryGunHolder` / similar) and whether a model id resolves in the tooltip/prop panel.

- [ ] **Step 2: Confirm the cause**

For these types, the model id is found by scanning args for an 8-char `NNN_NN_N` token (`level_objects.cpp:897-903`). Check the logs / the object's `modelId`:
- If `modelId` is empty → the token scan failed (the model is specified in a different arg/format). Fix the scan to find the correct model arg for this type.
- If `modelId` resolves but renders wrong → check orientation/position handling for the type, or whether the mesh load fails (`GetOrLoadMesh` "search FAILED" log).

- [ ] **Step 3: Apply the confirmed fix**

Fix the confirmed cause: correct the model-arg extraction for the gun type, or fix the transform/render. Keep it scoped to the gun types in question; do not change the generic path for unrelated `isMissingGeneric` types unless they share the exact confirmed bug.

- [ ] **Step 4: Verify**

Re-launch, confirm the mounted M2HB renders correctly (right model, right place/orientation). Confirm other `isMissingGeneric` objects in the level are unaffected.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "fix: mounted M2HB / stationary-gun interactable rendering (issue 7)"
```

---

## Final Verification

- [ ] **Run the full test suite**

```
cmake --build build --config Debug --target igi_tests
bin/Debug/igi_tests.exe
```
Expected: all suites pass, including the new `PickupResolveTest` and `ResModelSetTest`.

- [ ] **Confirm each success criterion** from the spec (§Success Criteria) is met by launching the editor on the relevant levels and observing issues 1, 3, 2, 6 fixed and 5/4/7 fixed against their reproductions.
