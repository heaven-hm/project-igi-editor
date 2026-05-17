/******************************************************************************
 * @file    app.h
 * @brief   application class
 *****************************************************************************/

#pragma once

#include <set>
#include "igi_bridge.h"
#include "renderer/model.h"
#include "config.h"
#include "compiler.h"
#include "decompiler.h"


/*
================================================================================
 App
================================================================================
*/
class App {
public:

	App();
	~App();

	bool					Init(int argc, char** argv);
	void					Shutdown();
	void					ResetLevel();
	void					ResetScript();



	// level_no: 1 ~ 13
	void					LoadLevel(int level_no);
	void					SetGameLevel(int level_no);
	void					SaveCurrentLevel();
	int						GetCurLevelNo() const;

	// Load AI models from ai\levelX folder
	void					LoadAIModelsFromFolder(int level_no);

	// draw wireframe on top of solid mesh
	void					ToggleOverlayWireframe();
	void					ToggleDrawParts(int part);
	void					SetDrawParts(int parts);
	void					ToggleTerrainDrawOption(int opt);
	void					ToggleTerrainModOption(int opt);

	void					ToggleEditMode();
	bool					GetEditMode() const;
	void					SetEditMode(bool enabled);
	void					SetTerrainEditEnabled(bool enabled);
	bool					GetTerrainEditEnabled() const;
	void					TogglePauseMenu();
	bool					GetPauseMode() const;
	void					SetEditBrush(int brush);
	int						GetEditBrush() const;
	void					ToggleShowHUD();
	bool					GetShowHUD() const;
	void					SetShowHUD(bool show);

	bool					GetOverlayWireframe() const;
	int						GetDrawParts() const;
	int						GetTerrainDrawOptions() const;
	int						GetTerrainModOptions() const;
	void					SetSelectedObjectScale(float scale);
	float					GetSelectedObjectScale() const;

	// Command line initial settings
	void					SetInitialDrawParts(int parts);
	void					SetInitialStickToGround(bool stick);

	// events
	void					OnWindowResize(int width, int height);
	void					OnDisplay();

	// input
	void					Input_OnMouse(int button, int state, int x, int y);
	void					Input_OnMotion(int x, int y);
	void					Input_OnMouseWheel(int wheel, int direction, int x, int y);
	void					Input_OnSpecial(int key, int x, int y);
	void					Input_OnSpecialUp(int key, int x, int y);
	void					Input_OnKeyboard(unsigned char key, int x, int y);
	void					Input_OnKeyboardUp(unsigned char key, int x, int y);

	// idle
	void					OnIdle();

	int						PickObjectAtScreenPos(int screen_x, int screen_y);
	std::vector<int>		GetVisibleTreeNodes();

	// void					HandleMarkerInput(unsigned char key); // Removed


private:

	struct window_state_s {
		bool				full_screen_;
		bool				cursor_visible_;
		int					viewport_width_;
		int					viewport_height_;
		int					old_viewport_width_;	// before switch to fullscreen
		int					old_viewport_height_;	// before switch to fullscreen
	};

	struct mouse_state_s {
		bool				left_button_down_;
		int					prior_x_;
		int					prior_y_;
	};

	struct input_s {
		int					keys_;
		int					mouse_delta_x_;
		int					mouse_delta_y_;
	};

	uint32_t				frame_;	// 0 ~ (0xFFFFFFFF - 1), reserve 0xFFFFFFFF as invalid frame number
	view_define_s			view_define_;

	Renderer				renderer_;
	Level					level_;
	IGIBridge				bridge_;
	Renderer::draw_params_s	draw_params_;
	int						terrain_mod_options_;
	Compiler				compiler_;
	Decompiler				decompiler_;

	// editor
	bool					edit_mode_;
	bool					terrain_edit_enabled_;
	bool					pause_mode_;
	int						edit_brush_;
	int						selected_object_index_;
	int						hover_object_index_;	// Object under mouse cursor
	bool					show_hud_;
	bool					show_debug_;
	bool					show_help_;
	int						tree_scroll_offset_;
	bool					tree_decl_expanded_;
	std::string				status_message_;

	bool					task_editor_open_ = false;
	std::string				edit_string_;
	int						edit_cursor_pos_ = 0;
	int						edit_selection_start_ = -1;
	int						edit_selection_end_ = -1;
	bool					edit_dragging_ = false;
	int						edit_box_w_ = 900;
	int						edit_box_h_ = 150;
	int						hover_tree_index_ = -1;
	int						edit_scroll_x_ = 0;
	int						last_tree_click_index_ = -1;
	int						last_tree_click_time_ms_ = 0;
	std::vector<std::string>	edit_undo_stack_;
	std::vector<std::string>	edit_redo_stack_;

	bool					sync_from_game_once_;
	int						last_game_level_;
	int						level_root_index_;
	std::vector<LevelObject> clipboard_;


	int64_t					prior_frame_time_;


	window_state_s			window_state_;
	mouse_state_s			mouse_state_;
	input_s					input_;

	bool					skip_input_on_motion_once_;

	// viewer
	struct viewer_s {
		glm::vec3			pos_;
		glm::vec3			forward_;
		glm::vec3			right_;
		glm::vec3			up_;
		glm::vec3			velocity_;
		float				yaw_;	// in degree
		float				pitch_;	// in degree
		float				roll_;	// in degree
		float				move_speed_;
		float				jump_speed_;
		bool				clip_to_z_;
		bool				on_ground_;
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
		ManipulationMode	mode_ = ManipulationMode::None;
		int					start_x_ = 0;
		int					start_y_ = 0;
		glm::dvec3			start_pos_;
		glm::vec3			start_rot_;
	} marker_manip_;
	bool                    manipulation_dirty_ = false;

	void					Frame(float delta_seconds);

	void					ProcessInput(float delta_seconds);
	bool					CheckCollision(const glm::vec3& next_pos);
	void					SnapObjectsToTerrain();

	bool					stick_to_ground_ = false;
	bool					noclip_mode_ = true; // By default true as requested by user
	void					UpdateViewerVectors();
	void					UpdateViewDefine();
	void					EditorProcessClick();
	void					UpdateMarkerManipulation();
	void					PropagateTransformToChildren(int parentIdx, const glm::dvec3& deltaPos, const glm::dvec3& deltaRot, const glm::dvec3& pivot);
	void					PushTaskEditorUndoState();
	void					UndoTaskEditorChange();
	void					RedoTaskEditorChange();
	void					ReplaceTaskEditorSelection(const std::string& text);
	void					SyncSelectedTaskToLiveQsc(bool keepEditorOpen);
	void					SaveTaskEditorChanges(bool keepEditorOpen);

public:
	// QSC/QVM workflow
	void					LoadQSCForLevel(int level_no);
	void					SaveAndCompile();
	void					DecompileFromGame(int level_no);
	void					ProcessTreeViewClick(int mx, int my);
	void					ProcessTreeViewHover(int mx, int my);
	void					CreateNewTask();
	void					DeleteSelectedTask();
	void					CopySelectedTask(bool includeSubtree);
	void					PasteTask();
	void					AssignTaskID();
	void					ModifyTaskParameters();
	void					LookupSelectedModelName();
	void					LookupSelectedModelId();
	void					CopySelectedModelName();
	void					CopySelectedModelId();
	void					SearchModelById();
	void					SearchModelByName();
	void					LookupHoveredModelName();
	void					LookupHoveredModelId();
	void					ClearStatusMessage();

};
