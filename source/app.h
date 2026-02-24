/******************************************************************************
 * @file    app.h
 * @brief   application class
 *****************************************************************************/

#pragma once

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

	// level_no: 1 ~ 13
	void					LoadLevel(int level_no);
	int						GetCurLevelNo() const;

	// draw wireframe on top of solid mesh
	void					ToggleOverlayWireframe();
	void					ToggleDrawParts(int part);
	void					ToggleTerrainDrawOption(int opt);
	void					ToggleTerrainModOption(int opt);

	bool					GetOverlayWireframe() const;
	int						GetDrawParts() const;
	int						GetTerrainDrawOptions() const;
	int						GetTerrainModOptions() const;

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
	Renderer::draw_params_s	draw_params_;
	int						terrain_mod_options_;

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

};
