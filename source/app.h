/******************************************************************************
 * @file    app.h
 * @brief   application class
 *****************************************************************************/

#pragma once

#include "config.h"
#include "igi_bridge.h"
#include "level/res_model_set.h"
#include "renderer/model.h"
#include <atomic>
#include <optional>
#include <set>

/*
================================================================================
 App
================================================================================
*/
class App {
public:
  App();
  ~App();

  bool Init(int argc, char **argv);
  void Shutdown();
  void ResetLevel();
  void ResetScript();

  // level_no: 1 ~ 13
  void LoadLevel(int level_no);
  void DrawProgressOverlay(const char *title, int pct,
                           const char *stage); // reusable loading bar
  void SetGameLevel(int level_no);
  void SaveCurrentLevel();
  void FlushAttaProxiesToMef();
  void LaunchGame();
  void ExportTextureMap();
  int GetCurLevelNo() const;

  // draw wireframe on top of solid mesh
  void ToggleOverlayWireframe();
  void ToggleDrawParts(int part);
  void SetDrawParts(int parts);
  void ToggleTerrainDrawOption(int opt);
  void ToggleTerrainModOption(int opt);

  void ToggleEditMode();
  bool GetEditMode() const;
  void SetEditMode(bool enabled);
  void SetTerrainEditEnabled(bool enabled);
  bool GetTerrainEditEnabled() const;
  void TogglePauseMenu();
  bool GetPauseMode() const;
  void SetEditBrush(int brush);
  int GetEditBrush() const;
  // Returns true and consumes the click if (x,y) hit an on-screen terrain brush
  // palette button.
  bool TerrainPaletteClick(int x, int y);
  void AdjustBrushRadius(double factor);  // multiply brush radius, clamp+status
  void AdjustBrushStrength(double delta); // add to brush strength, clamp+status
  void ToggleShowHUD();
  bool GetShowHUD() const;
  void SetShowHUD(bool show);

  int GetHoverObjectIndex() const { return hover_object_index_; }
  bool GetPropEditorOpen() const { return prop_editor_open_; }

  bool GetOverlayWireframe() const;
  int GetDrawParts() const;
  int GetTerrainDrawOptions() const;
  int GetTerrainModOptions() const;
  void SetSelectedObjectScale(float scale);
  float GetSelectedObjectScale() const;

  // Command line initial settings
  void SetInitialFullscreen(int windowedW, int windowedH);
  void SetInitialDrawParts(int parts);
  void SetInitialStickToGround(bool stick);

  // events
  void OnWindowResize(int width, int height);
  void OnDisplay();

  // input
  void Input_OnMouse(int button, int state, int x, int y);
  void Input_OnMotion(int x, int y);
  void Input_OnMouseWheel(int wheel, int direction, int x, int y);
  void Input_OnSpecial(int key, int x, int y);
  void Input_OnSpecialUp(int key, int x, int y);
  void Input_OnKeyboard(unsigned char key, int x, int y);
  void Input_OnKeyboardUp(unsigned char key, int x, int y);

  // idle
  void OnIdle();

  int PickObjectAtScreenPos(int screen_x, int screen_y);
  // Promote a picked MEF ATTA sub-model (entry index from the pick pass) into a
  // real, editable EditRigidObj task at the same world transform. Selects it.
  void PromoteAttaToObject(int entry);
  std::vector<int> GetVisibleTreeNodes();

  // void					HandleMarkerInput(unsigned char
  // key); // Removed

private:
  struct window_state_s {
    bool full_screen_;
    bool cursor_visible_;
    int viewport_width_;
    int viewport_height_;
    int old_viewport_width_;  // before switch to fullscreen
    int old_viewport_height_; // before switch to fullscreen
  };

  struct mouse_state_s {
    bool left_button_down_;
    bool right_button_down_;
    int prior_x_;
    int prior_y_;
  };

  struct input_s {
    int keys_;
    int mouse_delta_x_;
    int mouse_delta_y_;
  };

  uint32_t frame_; // 0 ~ (0xFFFFFFFF - 1), reserve 0xFFFFFFFF as invalid frame
                   // number
  view_define_s view_define_;

  Renderer renderer_;
  Level level_;
  ResModelSet level_res_models_; // models packed in the current level's .res
  IGIBridge bridge_;
  Renderer::draw_params_s draw_params_;
  int terrain_mod_options_;
  // editor
  bool edit_mode_;
  bool terrain_edit_enabled_;
  bool pause_mode_;
  std::string pause_level_input_ = "";
  std::string pause_search_input_ = "";
  int pause_active_input_ = -1; // -1: none, 1: search
  bool pause_terrain_expanded_ = false;
  int edit_brush_;
  double edit_brush_radius_ = 5000.0;
  double edit_brush_strength_ = 7.0;
  int selected_object_index_;
  int hover_object_index_; // Object under mouse cursor
  int last_pick_x_ = -9999;
  int last_pick_y_ = -9999;
  bool show_hud_;
  bool show_debug_;
  bool show_help_;
  int help_scroll_offset_ = 0;
  std::vector<std::string>
      help_entries_; // keybinding lines from qedkeybindings.qsc
  bool show_magic_obj_spheres_;
  int tree_scroll_offset_;
  bool tree_decl_expanded_;
  std::string status_message_;

  bool task_editor_open_ = false;
  bool task_picker_open_ = false;
  int task_picker_selected_idx_ = 0;
  int task_picker_scroll_offset_ = 0;
  std::string task_picker_search_ = "";
  std::string edit_string_;
  int edit_cursor_pos_ = 0;
  int edit_selection_start_ = -1;
  int edit_selection_end_ = -1;
  bool edit_dragging_ = false;
  int edit_box_w_ = 900;
  int edit_box_h_ = 150;
  int hover_tree_index_ = -1;
  int edit_scroll_x_ = 0;
  int last_tree_click_index_ = -1;
  int last_tree_click_time_ms_ = 0;
  // Unified undo/redo state capturing all editable elements
  struct UndoState {
      std::vector<LevelObject> objects;
      std::string ai_script_path;
      std::string ai_script_text;
      bool        ai_script_dirty = false;
      int         terrain_mod_options = 0;
      std::vector<uint8_t> terrain_hmp;   // snapshot of terrain height-map pixel buffer
      GraphFile   graph_overlay;
      bool        graph_overlay_visible = false;
  };

  // C1: Custom SPR cursor — multi-mode
  enum class CursorMode {
    Default = 0,      // No selection, no terrain edit → Pointer
    Hover = 1,        // Hovering over an object → highlighttool
    Selected = 2,     // Object selected (edit mode) → activetool
    TerrainLift = 3,  // edit_brush_ == BRUSH_RAISE
    TerrainLower = 4, // edit_brush_ == BRUSH_LOWER
    TerrainFlatten = 5,
    TerrainFlattenLine = 6,
    TerrainDrop = 7,
    TerrainSoften = 8,
    Inactive = 9, // inactivetool.spr
    Camera = 10,  // ALT held, camera look mode  → editor_camera.spr
    Move = 11,    // ALT held + mouse moving      → editor_move.spr
  };
  static const int NUM_CURSORS = 12;
  GLuint cursor_tex_ids_[NUM_CURSORS] = {};
  int cursor_tex_ws_[NUM_CURSORS] = {};
  int cursor_tex_hs_[NUM_CURSORS] = {};
  int cursor_loaded_count_ = 0;
  CursorMode current_cursor_mode_ = CursorMode::Default;
  bool camera_mode_moved_ = false;
  void LoadAllCursors();
  void LoadHelpEntries(); // load keybinding lines from qedkeybindings.qsc
  void UpdateCursorMode();
  void DrawCustomCursor();

  std::set<std::string> ai_model_ids_; // AITYPE_ model IDs from IGIModels.json

  // C2: Typed task property editor
  bool prop_editor_open_ = false;
  int prop_field_index_ = -1;
  float prop_drag_start_val_ = 0.f;
  float prop_drag_start_val2_ = 0.f; // second axis (2D pad Y)
  int prop_drag_start_x_ = 0;
  int prop_drag_start_y_ = 0;
  int prop_text_edit_field_ = -1; // -2 = editing note
  int prop_edit_obj_index_ =
      -1; // LevelObject targeted by the active text edit (-1 = selected/parent)
  int prop_drag_obj_index_ = -1; // LevelObject targeted by the active
                                 // slider/pad drag (-1 = selected/parent)
  std::string prop_text_buf_;
  int prop_text_caret_ = 0; // caret index within prop_text_buf_
  // Text field captured when a model/autocomplete picker opens, so the chosen
  // item is inserted into the exact field the cursor was in — even if focus
  // changed.
  int picker_target_field_ = -1;
  int picker_target_obj_ = -1;
  int picker_target_caret_ = -1; // caret position when picker opened
  int prop_last_drag_dx_ =
      0; // last non-zero X delta (for edge-stuck continuity)
  int prop_last_drag_dy_ =
      0; // last non-zero Y delta (for edge-stuck continuity)
  float prop_drag_speed_ = 0.f; // ramping position-drag speed (units/frame)
                                // while held in a direction
  int prop_panel_scroll_ = 0; // vertical scroll offset in pixels for prop panel

  // C3: Ctrl+F find
  bool find_open_ = false;
  std::string find_query_;
  int find_result_idx_ = -1;
  enum class FindMode { TaskNameTypeId, TextInTask, ById, ByNote, SetId };
  FindMode find_mode_ = FindMode::TaskNameTypeId;

  // AI script editor state (HumanSoldier / HumanAI prop panel section)
  std::string ai_script_path_;
  std::string ai_script_text_;
  bool ai_script_dirty_ = false;
  int ai_script_vscroll_ = 0;      // first visible visual line
  int ai_script_path_hscroll_ = 0; // first visible char in path box

  // Task type view toggle
  bool show_task_type_ = false;

  // Task picker creation flags
  bool task_picker_insert_first_ = false;
  bool task_new_at_camera_ = false;

  // Camera strafe
  bool camera_strafe_free_ = false;

  // File dialog (SaveSubTask, LoadSubTask)
  enum class FileDialogMode {
    None,
    SaveSubTask,
    SaveSubTaskParent,
    LoadSubTask,
    SaveObjectFile
  };
  FileDialogMode file_dialog_mode_ = FileDialogMode::None;
  std::string file_dialog_path_;
  int file_dialog_caret_ = 0;

  // Autocomplete keyword picker (Ctrl+N — left panel)
  bool ac_task_picker_open_ = false;
  int ac_task_selected_idx_ = 0;
  int ac_task_scroll_offset_ = 0;
  std::string ac_task_filter_;
  std::vector<std::string> ac_task_items_;

  // Model ID picker (Ctrl+O — right panel)
  bool model_picker_open_ = false;
  int model_picker_selected_ = 0;
  int model_picker_scroll_ = 0;
  std::string model_picker_filter_;

  // AutoComplete inline keywords (Ctrl+Space)
  std::vector<std::string> autocomplete_keywords_;
  // Inline-autocomplete cycling state (Ctrl+Space / Tab): lets repeated presses
  // step through every keyword that shares the originally-typed prefix.
  std::string ac_inline_prefix_;  // original prefix being cycled
  std::string ac_inline_last_kw_; // keyword inserted by the last completion
  int ac_inline_start_ = -1;      // token start position of the last completion
  int ac_inline_idx_ = -1;        // index into the current match set

  // All modelIds from current level objects in XXX_XX_X format (for model
  // picker)
  std::set<std::string> level_model_ids_;

  bool sync_from_game_once_;
  int last_game_level_;
  int last_loaded_level_ =
      -1; // level currently loaded; -1 = none yet (first load == fresh process)
  int level_root_index_;
  std::vector<LevelObject> clipboard_;

  std::vector<UndoState> undo_stack_;
  std::vector<UndoState> redo_stack_;
  bool undo_state_pushed_for_manip_ = false;

  // Auto-save state
  bool    auto_save_enabled_ = false;
  int     auto_save_interval_seconds_ = 300;
  int64_t auto_save_last_time_ms_ = 0;

  int64_t prior_frame_time_;

  window_state_s window_state_;
  mouse_state_s mouse_state_;
  input_s input_;

  bool skip_input_on_motion_once_;

  // Game launch state
  struct game_process_s {
    HANDLE hProcess = NULL;
    HANDLE hThread = NULL;
    HANDLE hMonitorThread = NULL;
    DWORD pid = 0;
    bool running = false;
  } game_process_;
  std::atomic<bool> game_exited_{
      false}; // set by monitor thread when game process exits
  HWND editor_hwnd_ = NULL;

  bool orbit_active_ = false;
  glm::vec3 orbit_target_pos_ = glm::vec3(0.0f);
  float orbit_distance_ = 0.0f;

  // viewer
  struct viewer_s {
    glm::vec3 pos_;
    glm::vec3 forward_;
    glm::vec3 right_;
    glm::vec3 up_;
    glm::vec3 velocity_;
    float yaw_;   // in degree
    float pitch_; // in degree
    float roll_;  // in degree
    float move_speed_;
    float jump_speed_;
    bool clip_to_z_;
    bool on_ground_;
  } viewer_;

  enum class ManipulationMode {
    None,
    MoveXY,
    MoveXZ,
    RotateAlpha,
    RotateBeta,
    RotateGamma
  };

  struct marker_manip_s {
    ManipulationMode mode_ = ManipulationMode::None;
    int start_x_ = 0;
    int start_y_ = 0;
    glm::dvec3 start_pos_;
    glm::vec3 start_rot_;
  } marker_manip_;
  bool manipulation_dirty_ = false;

  // Graph-node drag state (parallel to marker_manip_, for AIGraph node editing).
  struct graph_node_manip_s {
    bool active_ = false;
    int  start_x_ = 0;
    int  start_y_ = 0;
    glm::dvec3 start_pos_;
  } graph_node_manip_;
  void UpdateGraphNodeManipulation(int x, int y);

  void Frame(float delta_seconds);
  void DispatchEventBindings();

  void ProcessInput(float delta_seconds);
  bool CheckCollision(const glm::vec3 &next_pos);
  void SnapObjectsToTerrain();
  void EvaluateTrainTrackPositions();

  bool stick_to_ground_ = false;
  bool noclip_mode_ = true; // By default true as requested by user
  void UpdateViewerVectors();
  void UpdateViewDefine();
  void EditorProcessClick();
  void ApplyPropPositionDrag(); // per-frame velocity-ramped position drag (pad
                                // / Z slider)
  void UpdateMarkerManipulation();
  void PropagateTransformToChildren(int parentIdx, const glm::dvec3 &deltaPos,
                                    const glm::dmat3 &deltaWorld,
                                    const glm::dvec3 &pivot);
  void PushUndoState();
  void ToggleAutoSave();
  void AdjustAutoSaveInterval(int delta_seconds);
  // C2: property-panel text-edit helpers.
  void
  CommitPropTextEdit(); // writes prop_text_buf_ to argTokens; clears edit state
  bool IsPropFieldMultiline(int field) const; // VarString/String256 box?
  static std::string StripQuotes(const std::string &s);
  void LoadAIScriptForSelected();
  void UpdateAIScriptScroll();      // keep vscroll so caret is visible
  void UpdateAIScriptPathHScroll(); // keep hscroll so caret is visible
  void SaveAndReloadObjects();
  void Undo();
  void Redo();
  void PushTaskEditorUndoState();
  void UndoTaskEditorChange();
  void RedoTaskEditorChange();
  void ReplaceTaskEditorSelection(const std::string &text);
  void SyncSelectedTaskToLiveQsc(bool keepEditorOpen);
  void SaveTaskEditorChanges(bool keepEditorOpen);

  bool IsComputer(const LevelObject &obj);
  bool IsWaterTower(const LevelObject &obj);
  bool ValidateParentChildCompatibility(
      const LevelObject &parent, const std::vector<LevelObject> &addedSubtree);
  void SaveTaskSubtreeToFile(int idx, const std::string &path);
  void ConfirmFileDialog();
  void LoadAutoCompleteKeywords();
  bool InlineAutocomplete(); // complete/cycle the token left of the caret
  void RebuildLevelModelIds();

public:
  // QSC/QVM workflow
  void LoadQSCForLevel(int level_no);
  void SaveAndCompile();
  void DecompileFromGame(int level_no);
  void ProcessTreeViewClick(int mx, int my);
  void ProcessTreeViewHover(int mx, int my);
  void CreateNewTask();
  void DeleteSelectedTask();
  void CopySelectedTask(bool includeSubtree);
  void PasteTask();
  void AssignTaskID();
  void ModifyTaskParameters();
  void LookupSelectedModelName();
  void LookupSelectedModelId();
  void CopySelectedModelName();
  void CopySelectedModelId();
  void SearchModelById(std::optional<std::string> query = std::nullopt);
  void SearchModelByName(std::optional<std::string> query = std::nullopt);
  void LookupHoveredModelName();
  void LookupHoveredModelId();
  void ClearStatusMessage();
};
