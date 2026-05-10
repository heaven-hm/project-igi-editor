/******************************************************************************
 * @file    app.h
 * @brief   application class
 *****************************************************************************/

#pragma once

#include "igi_bridge.h"
#include "renderer/model.h"
#include "config.h"


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

	// draw wireframe on top of solid mesh
	void					ToggleOverlayWireframe();
	void					ToggleDrawParts(int part);
	void					ToggleTerrainDrawOption(int opt);
	void					ToggleTerrainModOption(int opt);

	void					ToggleEditMode();
	bool					GetEditMode() const;
	void					TogglePauseMenu();
	bool					GetPauseMode() const;
	void					SetEditBrush(int brush);
	int						GetEditBrush() const;

	void					ToggleShowHUD();
	bool					GetShowHUD() const;

	bool					GetOverlayWireframe() const;
	int						GetDrawParts() const;
	int						GetTerrainDrawOptions() const;
	int						GetTerrainModOptions() const;
	void					SetSelectedObjectScale(float scale);
	float					GetSelectedObjectScale() const;

	// events
	void					OnWindowResize(int width, int height);
	void					OnDisplay();

	// input
	void					Input_OnMouse(int button, int state, int x, int y);
	void					Input_OnMotion(int x, int y);
	void					Input_OnSpecial(int key, int x, int y);
	void					Input_OnSpecialUp(int key, int x, int y);
	void					Input_OnKeyboard(unsigned char key, int x, int y);
	void					Input_OnKeyboardUp(unsigned char key, int x, int y);

	// idle
	void					OnIdle();

	void					HandleMarkerInput(unsigned char key);


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

	// editor
	bool					edit_mode_;
	bool					pause_mode_;
	int						edit_brush_;
	int						selected_object_index_;
	int						transform_flag_;	// 0-7 for cube transform flags
	bool					show_hud_;
	bool					show_debug_;

	bool					sync_from_game_once_;
	int						last_game_level_;


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

	void					Frame(float delta_seconds);

	void					ProcessInput(float delta_seconds);
	void					UpdateViewerVectors();
	void					UpdateViewDefine();
	void					EditorProcessClick();

};
