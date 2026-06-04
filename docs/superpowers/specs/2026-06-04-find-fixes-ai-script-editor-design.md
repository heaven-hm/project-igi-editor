# Design: Find Shortcut Fixes + AI Script Editor in Property Panel

**Date:** 2026-06-04  
**Branch:** develop  
**Scope:** Three keyboard-shortcut bug fixes + inline AI script editor in the prop panel

---

## 1. Bug Fix — TaskFindAgain tree scroll

**Problem:** `TaskFindAgain` (Ctrl+Shift+F) correctly advances `selected_object_index_` and expands ancestor containers, but never updates `tree_scroll_offset_`, so the tree does not scroll to the found item.

**Fix location:** `source/app.cpp` — `DispatchEventBindings()`, inside `if (Check("TaskFindAgain"))`, after `selected_object_index_ = idx`.

**Fix:** Add the identical scroll-to-row logic used by the find-bar Enter handler (lines ~2126–2143):
1. Call `GetVisibleTreeNodes()` to get the current visible flat list.
2. Find the row index of `find_result_idx_` in that list.
3. Clamp `tree_scroll_offset_` so the row is visible (scroll up if above, scroll down if below).

**Success criterion:** After Ctrl+Shift+F, the tree scrolls so the matched task is visible and highlighted.

---

## 2. Bug Fix — TaskFindByTaskNote (Ctrl+Shift+N)

**Problem:** User reports conflict or failure when pressing Ctrl+Shift+N. The binding in `qedkeybindings.qsc` is `<Ctrl><Shift><N>`. The event dispatch uses `GetAsyncKeyState()` with exact modifier matching, which should distinguish Ctrl+Shift+N from Ctrl+N.

**Investigation plan during implementation:**
- Trace whether ASCII key 14 (Ctrl+N) is consumed before `DispatchEventBindings()` (e.g. by the prop-text-edit handler or another early-return path).
- If consumed early: add a bypass comment/guard identical to the Ctrl+Shift+F note at line 2450.
- If the binding fires but the find bar doesn't open: fix the `Check("TaskFindByTaskNote")` handler (should open find bar in `ByNote` mode, identical to the `TaskFindByTaskID` pattern).

**Success criterion:** Ctrl+Shift+N opens the find bar in "Find by note / name" mode without conflicting with any other shortcut.

---

## 3. Bug Fix — TaskFindTextInTask (Ctrl+H conflict)

**Problem:** Ctrl+H = ASCII 8 = Backspace. The keyboard handler has explicit backspace handling that fires before `DispatchEventBindings()`, so `TaskFindTextInTask` never receives its key.

**Fix:** Rebind `TaskFindTextInTask` in `assets/content/qed/qedkeybindings.qsc` to an unused combo. `<Ctrl><Shift><T>` was the first candidate but is already bound to `SaveSubTaskObjectFile`. `<Ctrl><Shift><X>` is unused.

- Old: `SetEventBinding("TaskFindTextInTask", "<Ctrl><H>");`
- New: `SetEventBinding("TaskFindTextInTask", "<Ctrl><Shift><X>");`

**Success criterion:** Ctrl+Shift+T opens the find bar in "Find text in task parameters" mode.

---

## 4. Feature — Inline AI Script Editor in Property Panel

### 4.1 Overview

When an AI task (HumanSoldier, HumanSoldierFemale, HumanAI, or any type with an `ai_model_ids_` match) is selected and the property panel is open, two extra widgets appear below the standard parameter fields:

1. **AI Script Path** — single-line editable textbox showing the resolved `.qvm` file path.
2. **AI Script** — tall multiline editable textbox showing the decompiled QSC source.

Editing the script text marks a dirty flag. The dirty flag triggers recompilation only when the user saves the level (Save Level button or F3 `SaveAndCompile`).

### 4.2 State (`source/app.h`)

Add three fields to `App`:

```cpp
std::string  ai_script_path_;   // resolved .qvm path for selected AI task
std::string  ai_script_text_;   // decompiled QSC content (live-editable copy)
bool         ai_script_dirty_;  // true when text has been edited since last save
```

Two sentinel field IDs for `prop_text_edit_field_` (use values that don't collide with `fi*3+comp` range, which maxes out at ~300):

```cpp
static constexpr int kAIScriptPathField = -10;
static constexpr int kAIScriptTextField = -11;
```

### 4.3 Load on task selection

Wherever `prop_editor_open_ = true` is set (double-click, Enter key, right-click), add a call to a new helper `LoadAIScriptForSelected()`.

`LoadAIScriptForSelected()`:
1. Check `selected_obj_is_ai` — exit early if not an AI task.
2. Compute `aiDir = IGIRoot\missions\location0\levelN\ai\`.
3. Set `ai_script_path_ = aiDir + "\\" + obj.taskId + ".qvm"`.
4. If file exists: `QVM_Parse` + `QVM_DecompileToString` → `ai_script_text_`.
5. If file does not exist: `ai_script_text_ = "// .qvm not found: " + ai_script_path_`.
6. `ai_script_dirty_ = false`.

Also call `LoadAIScriptForSelected()` whenever `selected_object_index_` changes (on click/select), resetting state if the new selection is not an AI task.

### 4.4 Compile on save

In the Save Level / F3 handler (the existing `SaveObjectFile` / `SaveAndCompile` code path), before writing `objects.qvm`:

```
if (ai_script_dirty_) {
    lex+parse ai_script_text_ as QSC
    compile to ai_script_path_ via qvm::CompileToFile
    round-trip validate (QVM_Parse the written file)
    on success: ai_script_dirty_ = false, log to status bar
    on failure: log error to status bar, do NOT clear dirty flag
}
```

### 4.5 PropPanel layout

In `source/level/task_schema.cpp` (or wherever `PropPanel::BuildLayout` builds the widget list), when `is_ai == true`, append two widgets after all normal parameter widgets:

| Widget | Type | Field ID | Height |
|---|---|---|---|
| "AI Script Path" label + single-line box | single-line edit | `kAIScriptPathField` | 1 row (`kBoxH`) |
| "AI Script" label + multiline box | multiline edit | `kAIScriptTextField` | 12 rows (`12 * kBoxH`) |

### 4.6 Renderer (`source/renderer/renderer.cpp`)

In the prop panel draw loop, after rendering normal parameter widgets, check for `kAIScriptPathField` and `kAIScriptTextField` widget IDs and render them using the existing `draw_edit_box` lambda:

- Path box: `multiline = false`, live text = `task_tree_view.ai_script_path_`
- Script box: `multiline = true`, live text = `task_tree_view.ai_script_text_`

The renderer struct (`TaskTreeView` or equivalent) needs two new read-only string refs/copies for these values plus the dirty flag (for optional dirty-indicator rendering).

### 4.7 Path change behaviour

When the path textbox (`kAIScriptPathField`) is being edited and the user presses Enter:
1. Take the committed `prop_text_buf_` as the new `ai_script_path_`.
2. Call `LoadAIScriptForSelected()` with the override path (skipping the auto-compute step).
3. Reset `ai_script_dirty_ = false`.

When the script textbox (`kAIScriptTextField`) is edited:
1. On every commit of `prop_text_buf_` to the field, set `ai_script_dirty_ = true`.

### 4.8 Error handling

- Decompile failure (corrupt `.qvm`): show placeholder text `"// decompile failed"` in the script box, log to status bar.
- Compile failure (bad QSC syntax): log error to status bar, leave `ai_script_dirty_ = true`, do not write the `.qvm`.
- Round-trip validation failure: revert to backup (same pattern already used in the existing compile path at line ~4889).

---

## Files to Change

| File | Change |
|---|---|
| `assets/content/qed/qedkeybindings.qsc` | Rebind `TaskFindTextInTask` to `<Ctrl><Shift><T>` |
| `source/app.h` | Add `ai_script_path_`, `ai_script_text_`, `ai_script_dirty_`, two field ID constants |
| `source/app.cpp` | TaskFindAgain scroll fix; Ctrl+Shift+N trace/fix; `LoadAIScriptForSelected()`; save-time compile; path-box Enter handler; script-box dirty flag |
| `source/level/task_schema.cpp` | Append AI Script Path + AI Script widgets when `is_ai` |
| `source/renderer/renderer.cpp` | Render the two new AI script widgets; expose new fields via `TaskTreeView` struct |
| `source/renderer/renderer.h` | Add `ai_script_path_`, `ai_script_text_`, `ai_script_dirty_` to the render state struct |
