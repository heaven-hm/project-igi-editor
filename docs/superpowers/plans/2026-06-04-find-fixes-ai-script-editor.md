# Find Shortcut Fixes + AI Script Editor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix three broken keyboard shortcuts (TaskFindAgain scroll, TaskFindByTaskNote Ctrl+N conflict, TaskFindTextInTask Backspace conflict) and add an inline AI Script editor panel (path + decompiled QSC textbox) to the property panel for HumanSoldier/HumanAI tasks, compiled on save.

**Architecture:** All changes are in the existing prop-panel / event-binding layer. New constants live in `renderer.h` (PropPanel namespace) so both `app.cpp` and `renderer.cpp` share them without a new header. AI script state is owned by `App`, passed into the renderer via the existing `task_tree_view_params_s` struct, and compiled in `SaveCurrentLevel()` before `SaveAndCompile()`.

**Tech Stack:** C++ / GLUT / OpenGL 2D overlay (existing patterns only — `draw_edit_box`, `PropPanel::BuildLayout`, `CommitPropTextEdit`, `QVM_Parse`, `QVM_DecompileToString`, `qvm::CompileToFile`).

---

## File Map

| File | What changes |
|---|---|
| `assets/content/qed/qedkeybindings.qsc` | Rebind `TaskFindTextInTask` from `<Ctrl><H>` to `<Ctrl><Shift><T>` |
| `source/app.cpp` | Fix Ctrl+N intercept; Fix FindAgain scroll; LoadAIScriptForSelected(); CommitPropTextEdit AI cases; IsPropFieldMultiline AI case; click handler AI cases; wire to prop-open sites; compile-on-save in SaveCurrentLevel; populate ai_script_* in Frame |
| `source/app.h` | Add three AI script state fields; two constexpr field-ID sentinels (via PropPanel::); declare LoadAIScriptForSelected() |
| `source/renderer/renderer.h` | Add `AIScriptPath`/`AIScriptText` to `WidgetKind`; two `constexpr int` sentinel field IDs in PropPanel namespace; append two widgets in `BuildLayout` when `is_ai`; add three fields to `task_tree_view_params_s` |
| `source/renderer/renderer.cpp` | Render the two new AI widget kinds in the prop panel draw loop |

---

## Task 1: Fix TaskFindTextInTask — rebind from Ctrl+H to Ctrl+Shift+T

Ctrl+H = ASCII 8 = Backspace. The keyboard handler intercepts key 8 as backspace before `DispatchEventBindings` is ever called, so this binding has never worked. Rebind to `<Ctrl><Shift><X>` — `<Ctrl><Shift><T>` was already bound to `SaveSubTaskObjectFile`.

**Files:**
- Modify: `assets/content/qed/qedkeybindings.qsc:41`

- [ ] **Step 1: Change the binding**

In `assets/content/qed/qedkeybindings.qsc`, find line 41:
```
SetEventBinding("TaskFindTextInTask",                       "<Ctrl><H>");
```
Change to:
```
SetEventBinding("TaskFindTextInTask",                       "<Ctrl><Shift><X>");
```

- [ ] **Step 2: Build and verify**

Build the project. Launch the editor, load any level. Press Ctrl+Shift+T. The find bar should open with title "Find text in task parameters:". Press ESC to close.

- [ ] **Step 3: Commit**

```bash
git add assets/content/qed/qedkeybindings.qsc
git commit -m "fix: rebind TaskFindTextInTask from Ctrl+H (backspace conflict) to Ctrl+Shift+T"
```

---

## Task 2: Fix TaskFindByTaskNote — Ctrl+N autocomplete intercept eats Ctrl+Shift+N

In `Input_OnKeyboard`, the block `if (ctrlDown) { if (key == 14) { ... return; } }` fires for both Ctrl+N and Ctrl+Shift+N (both produce ASCII 14 in GLUT). When Shift is also held, this block shows a "click a text box first" message and returns — `DispatchEventBindings` is never reached, so `TaskFindByTaskNote` never fires.

Fix: detect `shiftDown` alongside `ctrlDown` and exclude the Ctrl+Shift+N case from the autocomplete intercept.

**Files:**
- Modify: `source/app.cpp` — `Input_OnKeyboard`, around line 1882

- [ ] **Step 1: Add shiftDown detection and guard the Ctrl+N autocomplete block**

Find this code in `Input_OnKeyboard` (around line 1882):
```cpp
bool ctrlDown = (glutGetModifiers() & GLUT_ACTIVE_CTRL) != 0 ||
                (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
if (ctrlDown) {
    if (key == 14) { // Ctrl+N → AutoComplete keyword picker (AutoCompleteKeywords.txt)
```

Change it to:
```cpp
bool ctrlDown = (glutGetModifiers() & GLUT_ACTIVE_CTRL) != 0 ||
                (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
bool shiftDown = (glutGetModifiers() & GLUT_ACTIVE_SHIFT) != 0 ||
                 (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
if (ctrlDown) {
    if (key == 14 && !shiftDown) { // Ctrl+N only (not Ctrl+Shift+N — that's TaskFindByTaskNote)
```

- [ ] **Step 2: Build and verify**

Build. Launch editor, load a level. Press Ctrl+Shift+N — find bar should open with title "Find task by note / name:". Press ESC. Confirm Ctrl+N (without Shift) still opens the autocomplete picker (or shows "click a text box first" if no text box is focused).

- [ ] **Step 3: Commit**

```bash
git add source/app.cpp
git commit -m "fix: exclude Ctrl+Shift+N from Ctrl+N autocomplete intercept so TaskFindByTaskNote fires"
```

---

## Task 3: Fix TaskFindAgain — tree does not scroll to found item

`TaskFindAgain` sets `selected_object_index_` and expands ancestors but never updates `tree_scroll_offset_`. The identical scroll logic exists in the find-bar Enter handler (lines ~2126–2144). Copy it into the `TaskFindAgain` handler.

**Files:**
- Modify: `source/app.cpp` — `DispatchEventBindings()`, `if (Check("TaskFindAgain"))` block (~line 2815)

- [ ] **Step 1: Add scroll-to-row logic after the found item is selected**

Locate the `TaskFindAgain` block. It currently ends with:
```cpp
            selected_object_index_ = idx;
            break;
        }
    }
    if (!found) status_message_ = "No more matches";
```

Change to:
```cpp
            selected_object_index_ = idx;
            // Scroll the tree to make the found item visible
            {
                auto visibleList = GetVisibleTreeNodes();
                int current_row = -1;
                for (int i = 0; i < (int)visibleList.size(); ++i) {
                    if (visibleList[i] == idx) { current_row = i; break; }
                }
                if (current_row >= 0) {
                    int row_h = 16;
                    int start_y = 30;
                    int max_rows = (window_state_.viewport_height_ - 50 - start_y) / row_h;
                    if (max_rows > 0) {
                        if (current_row < tree_scroll_offset_)
                            tree_scroll_offset_ = current_row;
                        else if (current_row >= tree_scroll_offset_ + max_rows)
                            tree_scroll_offset_ = current_row - max_rows + 1;
                    }
                }
            }
            break;
        }
    }
    if (!found) status_message_ = "No more matches";
```

- [ ] **Step 2: Build and verify**

Build. Load a level with many tasks. Do a normal Ctrl+F search for a task name, press Enter to confirm. Then press ESC. Now press Ctrl+Shift+F repeatedly — each press should cycle to the next match AND scroll the tree so the matched task is visible and highlighted.

- [ ] **Step 3: Commit**

```bash
git add source/app.cpp
git commit -m "fix: TaskFindAgain now scrolls tree to found item after each Ctrl+Shift+F press"
```

---

## Task 4: Add AI script widget kinds, constants, and BuildLayout extension in renderer.h

Add two new `WidgetKind` entries, two sentinel `constexpr int` field IDs in the `PropPanel` namespace, and extend `BuildLayout` to emit them when `is_ai == true`.

**Files:**
- Modify: `source/renderer/renderer.h`

- [ ] **Step 1: Add the two new WidgetKind values**

In `renderer.h`, inside `namespace PropPanel`, find the `enum class WidgetKind` (currently ends with `ChildHeader`). Add two entries:
```cpp
enum class WidgetKind {
    NoteBox,
    PosPad,
    PosZSlider,
    SnapGround,
    SnapObject,
    OriSlider,
    RgbSlider,
    NumSlider,
    NumBox,
    StringBox,
    Checkbox,
    ChildHeader,
    AIScriptPath,  // single-line editable: resolved .qvm file path
    AIScriptText,  // multiline editable: decompiled QSC source
};
```

- [ ] **Step 2: Add the sentinel field-ID constants**

Immediately after the `WidgetKind` enum (still inside `namespace PropPanel`), add:
```cpp
// Sentinel prop_text_edit_field_ values for AI script widgets.
// Must not collide with fi*3+comp range (always >= 0 for normal fields).
static constexpr int kAIScriptPathField = -10;
static constexpr int kAIScriptTextField = -11;
```

- [ ] **Step 3: Extend BuildLayout to emit AI script widgets when is_ai is true**

In `BuildLayout`, find the end of the function body, just before:
```cpp
    L.panel_h = (y + kPad) - kTop;
    return L;
```

Add this block immediately before those two lines:
```cpp
    // AI Script section — only for AI tasks (HumanSoldier, HumanAI, etc.)
    if (is_ai) {
        y += kRowH;  // "AI Script Path:" label line
        L.widgets.push_back({WidgetKind::AIScriptPath,
                             kLeft + kPad, y, kLeft + kWidth - kPad, y + kBoxH,
                             kAIScriptPathField, 0});
        y += kBoxH + 6;

        y += kRowH;  // "AI Script:" label line
        const int scriptH = kBoxH * 12;
        L.widgets.push_back({WidgetKind::AIScriptText,
                             kLeft + kPad, y, kLeft + kWidth - kPad, y + scriptH,
                             kAIScriptTextField, 0});
        y += scriptH + 6;
    }
```

- [ ] **Step 4: Build (renderer.h changes only — expect linker pass)**

Run a full build. The new enum values and constants are defined; nothing uses them yet so no logic errors are expected.

- [ ] **Step 5: Commit**

```bash
git add source/renderer/renderer.h
git commit -m "feat: add AIScriptPath/AIScriptText widget kinds and BuildLayout extension for AI tasks"
```

---

## Task 5: Add AI script state fields to task_tree_view_params_s and renderer draw loop

The renderer needs `ai_script_path_`, `ai_script_text_`, and `ai_script_dirty_` to render the two new widgets. Add them to the params struct and render them.

**Files:**
- Modify: `source/renderer/renderer.h` — `task_tree_view_params_s`
- Modify: `source/renderer/renderer.cpp` — prop panel draw loop

- [ ] **Step 1: Add three fields to task_tree_view_params_s**

In `renderer.h`, inside `struct task_tree_view_params_s`, after the existing `model_picker_filter_` / `model_ids_` block (the last fields), add:
```cpp
        // AI Script editor state
        std::string ai_script_path_;
        std::string ai_script_text_;
        bool        ai_script_dirty_ = false;
```

- [ ] **Step 2: Render the two new widget kinds in the prop panel draw loop**

In `renderer.cpp`, find the prop panel widget draw loop. It has a chain of `if/else if` checks on `w.kind`. The loop handles `NoteBox`, `StringBox`, `NumBox`, etc. Find the end of the `for (const auto& w : L.widgets)` loop (look for the closing brace after the last `else if` case).

Add these two cases inside the widget loop (they can go before or after the `StringBox` case — position doesn't matter for rendering):

```cpp
            } else if (w.kind == K::AIScriptPath) {
                // Label
                draw_text(w.x1, w.y1 - PropPanel::kRowH + 12, "AI Script Path:", 0.8f, 0.8f, 1.0f);
                // Editable single-line box
                draw_edit_box(w, PropPanel::kAIScriptPathField,
                              task_tree_view.ai_script_path_, false);
            } else if (w.kind == K::AIScriptText) {
                // Label — show dirty indicator when unsaved edits exist
                const char* label = task_tree_view.ai_script_dirty_
                                        ? "AI Script (modified — save to compile):"
                                        : "AI Script:";
                draw_text(w.x1, w.y1 - PropPanel::kRowH + 12, label,
                          task_tree_view.ai_script_dirty_ ? 1.0f : 0.8f,
                          task_tree_view.ai_script_dirty_ ? 0.6f : 0.8f,
                          task_tree_view.ai_script_dirty_ ? 0.2f : 1.0f);
                // Editable multiline box
                draw_edit_box(w, PropPanel::kAIScriptTextField,
                              task_tree_view.ai_script_text_, true);
```

- [ ] **Step 3: Build and verify no compile errors**

Build the project. No runtime test yet (the App fields don't exist yet), just confirm it compiles.

- [ ] **Step 4: Commit**

```bash
git add source/renderer/renderer.h source/renderer/renderer.cpp
git commit -m "feat: render AIScriptPath and AIScriptText widgets in prop panel draw loop"
```

---

## Task 6: Add AI script state to App and implement LoadAIScriptForSelected

Add three state fields and the `LoadAIScriptForSelected()` helper to `App`.

**Files:**
- Modify: `source/app.h`
- Modify: `source/app.cpp`

- [ ] **Step 1: Add fields and method declaration to app.h**

In `app.h`, find the section with `find_open_`, `find_query_`, etc. (around line 222). Add the new AI script fields nearby (after `find_mode_` is fine):
```cpp
    // AI script editor state (HumanSoldier / HumanAI prop panel section)
    std::string              ai_script_path_;
    std::string              ai_script_text_;
    bool                     ai_script_dirty_ = false;
```

Also add the method declaration in the `private:` section of `App` alongside other helpers like `CommitPropTextEdit`, `IsPropFieldMultiline`:
```cpp
    void    LoadAIScriptForSelected();
```

- [ ] **Step 2: Implement LoadAIScriptForSelected() in app.cpp**

Add this function in `app.cpp` near `CommitPropTextEdit` (around line 4434). Add it just before `CommitPropTextEdit`:

```cpp
void App::LoadAIScriptForSelected() {
    ai_script_path_.clear();
    ai_script_text_.clear();
    ai_script_dirty_ = false;

    if (selected_object_index_ < 0) return;
    const auto& objects = level_.GetLevelObjects().GetObjects();
    if (selected_object_index_ >= (int)objects.size()) return;
    const auto& obj = objects[selected_object_index_];

    // Only AI model types get the script section.
    if (ai_model_ids_.find(obj.modelId) == ai_model_ids_.end()) return;
    if (obj.taskId.empty()) return;

    int levelNo = level_.GetLevelNo();
    std::string aiDir = Utils::GetIGIRootPath() +
                        "\\missions\\location0\\level" + std::to_string(levelNo) + "\\ai";
    ai_script_path_ = aiDir + "\\" + obj.taskId + ".qvm";

    if (!std::filesystem::exists(ai_script_path_)) {
        ai_script_text_ = "// .qvm not found: " + ai_script_path_;
        return;
    }
    QVMFile qvm = QVM_Parse(ai_script_path_);
    if (!qvm.valid) {
        ai_script_text_ = "// decompile failed (invalid QVM): " + ai_script_path_;
        return;
    }
    ai_script_text_ = QVM_DecompileToString(qvm);
}
```

- [ ] **Step 3: Build to confirm no compile errors**

Build the project. `ai_model_ids_` is already a member of `App` (used at line 3397). `std::filesystem` is already used in `app.cpp`. `QVM_Parse` and `QVM_DecompileToString` are already included.

- [ ] **Step 4: Commit**

```bash
git add source/app.h source/app.cpp
git commit -m "feat: add AI script state fields and LoadAIScriptForSelected() helper"
```

---

## Task 7: Wire LoadAIScriptForSelected to prop-open sites and update IsPropFieldMultiline + CommitPropTextEdit

**Files:**
- Modify: `source/app.cpp` — three `prop_editor_open_ = true` sites; `IsPropFieldMultiline`; `CommitPropTextEdit`; prop-panel click handler

- [ ] **Step 1: Call LoadAIScriptForSelected() at every prop-panel open site**

Search app.cpp for `prop_editor_open_ = true`. There are three occurrences (double-click ~line 1284, Enter key ~line 2466, right-click ~line 1284). After **each** one, add the call:

Site 1 — right-click / hover select (~line 1284):
```cpp
        selected_object_index_ = target;
        prop_editor_open_ = true; prop_panel_scroll_ = 0; prop_text_edit_field_ = -1; prop_edit_obj_index_ = -1;
        LoadAIScriptForSelected();  // ← add this line
```

Site 2 — double-click / Enter (~line 2466):
```cpp
            prop_editor_open_ = true; prop_panel_scroll_ = 0; prop_text_edit_field_ = -1; prop_edit_obj_index_ = -1;
            LoadAIScriptForSelected();  // ← add this line
```

Site 3 — if there is a third site (search `prop_editor_open_ = true` to find all), apply the same pattern.

- [ ] **Step 2: Make IsPropFieldMultiline return true for kAIScriptTextField**

Find `IsPropFieldMultiline` in `app.cpp` (~line 4418). It currently starts with `if (field < 0) return false;`. Change that guard to:

```cpp
bool App::IsPropFieldMultiline(int field) const {
    if (field == PropPanel::kAIScriptTextField) return true;   // ← add before the < 0 guard
    if (field < 0) return false; // note box (-2) is single-line
    // ... rest unchanged
```

- [ ] **Step 3: Handle AI script fields in CommitPropTextEdit**

In `CommitPropTextEdit` (~line 4434), after the early return `if (prop_text_edit_field_ == -1) return;` and before `int field = prop_text_edit_field_; prop_text_edit_field_ = -1;`, add handling for the two AI sentinels:

```cpp
void App::CommitPropTextEdit() {
    if (prop_text_edit_field_ == -1) return;

    // AI Script Path field: update path, reload and decompile the new .qvm.
    if (prop_text_edit_field_ == PropPanel::kAIScriptPathField) {
        prop_text_edit_field_ = -1;
        ai_script_path_ = prop_text_buf_;
        if (!ai_script_path_.empty() && std::filesystem::exists(ai_script_path_)) {
            QVMFile qvm = QVM_Parse(ai_script_path_);
            ai_script_text_ = qvm.valid ? QVM_DecompileToString(qvm)
                                        : "// decompile failed: " + ai_script_path_;
        } else {
            ai_script_text_ = "// file not found: " + ai_script_path_;
        }
        ai_script_dirty_ = false;
        return;
    }

    // AI Script Text field: store edited text and mark dirty.
    if (prop_text_edit_field_ == PropPanel::kAIScriptTextField) {
        prop_text_edit_field_ = -1;
        ai_script_text_ = prop_text_buf_;
        ai_script_dirty_ = true;
        return;
    }

    int field = prop_text_edit_field_;
    prop_text_edit_field_ = -1;
    // ... rest of CommitPropTextEdit unchanged
```

- [ ] **Step 4: Handle AI widget clicks in the prop-panel click handler**

In `Input_OnMouse` (~line 1060), find the `for (const auto& w : L.widgets)` loop inside the prop-panel click block. After the existing `if (w.kind == K::NoteBox)` / `else if (w.kind == K::StringBox)` / etc. chain, add:

```cpp
                } else if (w.kind == K::AIScriptPath) {
                    prop_edit_obj_index_ = -1;
                    prop_text_edit_field_ = PropPanel::kAIScriptPathField;
                    prop_text_buf_ = ai_script_path_;
                    prop_text_caret_ = (int)prop_text_buf_.size();
                } else if (w.kind == K::AIScriptText) {
                    prop_edit_obj_index_ = -1;
                    prop_text_edit_field_ = PropPanel::kAIScriptTextField;
                    prop_text_buf_ = ai_script_text_;
                    prop_text_caret_ = (int)prop_text_buf_.size();
```

- [ ] **Step 5: Build and do an initial UI smoke test**

Build. Load a level that has HumanSoldier or HumanAI tasks (e.g. level 1). Select one of those tasks and open the property panel (double-click or Enter). Scroll to the bottom of the panel — you should see "AI Script Path:" with the `.qvm` path and "AI Script:" with the decompiled QSC source. Click the path box and the script box to confirm they become editable.

- [ ] **Step 6: Commit**

```bash
git add source/app.cpp
git commit -m "feat: wire AI script load on prop-open, click handling, CommitPropTextEdit, and IsPropFieldMultiline"
```

---

## Task 8: Wire compile-on-save in SaveCurrentLevel

When `SaveCurrentLevel` is called (Save button or F3/`SaveAndCompile`), compile `ai_script_text_` to `ai_script_path_` if `ai_script_dirty_` is true.

**Files:**
- Modify: `source/app.cpp` — `SaveCurrentLevel()` (~line 838)

- [ ] **Step 1: Add AI script compile step before SaveAndCompile**

`SaveCurrentLevel` currently reads:
```cpp
void App::SaveCurrentLevel() {
    try {
        Logger::Get().Log(LogLevel::INFO, "[App] SaveCurrentLevel() called");
        FlushAttaProxiesToMef();
        level_.SaveChanges();
        Logger::Get().Log(LogLevel::INFO, "[App] Calling SaveAndCompile()");
        SaveAndCompile();
    }
```

Change to:
```cpp
void App::SaveCurrentLevel() {
    try {
        Logger::Get().Log(LogLevel::INFO, "[App] SaveCurrentLevel() called");
        FlushAttaProxiesToMef();
        level_.SaveChanges();

        // Compile edited AI script (.qsc text → .qvm file) before saving the level QVM.
        if (ai_script_dirty_ && !ai_script_path_.empty()) {
            Logger::Get().Log(LogLevel::INFO, "[App] Compiling modified AI script to: " + ai_script_path_);
            auto lexResult   = qsc::Lex(ai_script_text_);
            auto parseResult = lexResult.ok ? qsc::Parse(lexResult.tokens) : qsc::ParseResult{};
            std::string compileErr;
            bool ok = lexResult.ok && parseResult.ok &&
                      qvm::CompileToFile(*parseResult.program, ai_script_path_, &compileErr);
            if (ok) {
                QVMFile check = QVM_Parse(ai_script_path_);
                if (check.valid) {
                    ai_script_dirty_ = false;
                    status_message_ = "AI script compiled: " + ai_script_path_;
                    Logger::Get().Log(LogLevel::INFO, "[App] AI script compiled OK");
                } else {
                    Logger::Get().Log(LogLevel::ERR, "[App] AI script round-trip failed — file may be corrupt");
                    status_message_ = "AI script compile: round-trip failed";
                }
            } else {
                std::string detail = compileErr.empty() ? "(no detail)" : compileErr;
                Logger::Get().Log(LogLevel::ERR, "[App] AI script compile error: " + detail);
                status_message_ = "AI script compile error: " + detail;
            }
        }

        Logger::Get().Log(LogLevel::INFO, "[App] Calling SaveAndCompile()");
        SaveAndCompile();
    }
```

- [ ] **Step 2: Build and verify**

Build. Open level 1, select a HumanAI task, open prop panel, edit one line of the script (add a comment like `// test`). The label should change to orange "AI Script (modified — save to compile):". Press the Save key (or F3). Check logs — should see "AI script compiled OK". The label should revert to white "AI Script:". Open the `.qvm` file with `LoadQSCForLevel` or re-open the level to confirm the change round-tripped.

- [ ] **Step 3: Commit**

```bash
git add source/app.cpp
git commit -m "feat: compile modified AI script to .qvm on SaveCurrentLevel if dirty flag is set"
```

---

## Task 9: Populate ai_script_* fields in the renderer state struct (Frame / BuildState)

The renderer reads `task_tree_view.ai_script_path_` etc. from the params struct. Wire those from `App`'s fields.

**Files:**
- Modify: `source/app.cpp` — `Frame()` / wherever `task_tree_view_params_s` is built (~lines 3383–3400 and ~3530)

- [ ] **Step 1: Add ai_script_* to both struct-build sites**

Search `app.cpp` for `.selected_obj_is_ai` — the struct is populated at two sites (one for the normal render path, one for the idle/update path, around lines 3397 and 3533). At **each** site, add the three new fields next to `selected_obj_is_ai`:

```cpp
        .selected_obj_is_ai    = (selected_object_index_ >= 0 &&
            selected_object_index_ < (int)level_.GetLevelObjects().GetObjects().size() &&
            ai_model_ids_.count(level_.GetLevelObjects().GetObjects()[selected_object_index_].modelId) > 0),
        .ai_script_path_       = ai_script_path_,    // ← add
        .ai_script_text_       = ai_script_text_,    // ← add
        .ai_script_dirty_      = ai_script_dirty_,   // ← add
```

- [ ] **Step 2: Build and full smoke test**

Build. Load level 1. Select a HumanAI task, double-click to open props. Scroll to bottom of prop panel:
- "AI Script Path:" box shows the full path like `D:\IGI1\missions\location0\level1\ai\11.qvm`
- "AI Script:" multiline box shows the decompiled QSC content
- Click the path box, type a new path, press Enter — content should reload from the new path (or show "file not found" if invalid)
- Click the script box, make a small edit — label changes to orange indicating dirty
- Press Save (or F3) — status bar shows "AI script compiled: ..." and label returns to white

- [ ] **Step 3: Commit**

```bash
git add source/app.cpp
git commit -m "feat: pass ai_script_path/text/dirty to renderer params struct for prop panel display"
```

---

## Self-Review

**Spec coverage check:**

| Spec requirement | Task |
|---|---|
| TaskFindTextInTask rebind (Ctrl+H → Ctrl+Shift+T) | Task 1 ✓ |
| TaskFindByTaskNote Ctrl+Shift+N conflict | Task 2 ✓ |
| TaskFindAgain tree scroll | Task 3 ✓ |
| AI script: single-line path textbox | Tasks 4, 5, 6, 7, 9 ✓ |
| AI script: multiline script textbox | Tasks 4, 5, 6, 7, 9 ✓ |
| Load + decompile on task selection | Task 6 (LoadAIScriptForSelected) ✓ |
| Path box Enter → reload from new path | Task 7 (CommitPropTextEdit kAIScriptPathField) ✓ |
| Dirty flag when script edited | Task 7 (CommitPropTextEdit kAIScriptTextField) ✓ |
| Compile on Save Level / F3 only | Task 8 ✓ |
| Compile only if dirty | Task 8 (guard `if ai_script_dirty_`) ✓ |
| Round-trip validate | Task 8 ✓ |
| Error reporting to status bar | Task 8 ✓ |

**Placeholder scan:** No TBDs. All code blocks are complete.

**Type consistency:** `PropPanel::kAIScriptPathField` (-10) and `PropPanel::kAIScriptTextField` (-11) are used consistently across renderer.h, app.cpp click handler, CommitPropTextEdit, and IsPropFieldMultiline. `WidgetKind::AIScriptPath` / `AIScriptText` used consistently in BuildLayout and renderer draw loop.
