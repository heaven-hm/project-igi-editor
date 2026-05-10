/******************************************************************************
 * @file    app.cpp
 * @brief   application class
 *****************************************************************************/

#include "pch.h"
#include <freeglut.h>
#include "logger.h"
#include <filesystem>


/*
================================================================================
 App
================================================================================
*/
constexpr float	MOUSE_SENSITIVE = 0.2f;

// movement
constexpr float		VIEW_HEIGHT = 7000.0f;
constexpr float		GRAVITE = 10.0f * WORLD_UNITS_PER_METER;
constexpr float		MIN_MOVE_SPEED = 8.0f * WORLD_UNITS_PER_METER;
constexpr float		MAX_MOVE_SPEED = 8192.0f * WORLD_UNITS_PER_METER;
constexpr float		MIN_JUMP_SPEED = 4.0f * WORLD_UNITS_PER_METER;
constexpr float		MAX_JUMP_SPEED = 512.0f * WORLD_UNITS_PER_METER;


// movement key down flags
constexpr int MK_FORWARD		= FLAG_BIT(0);
constexpr int MK_BACKWARD		= FLAG_BIT(1);
constexpr int MK_LEFT			= FLAG_BIT(2);
constexpr int MK_RIGHT			= FLAG_BIT(3);
constexpr int MK_STRAIGHT_UP	= FLAG_BIT(4);
constexpr int MK_STRAIGHT_DOWN	= FLAG_BIT(5);
constexpr int MK_JUMP			= FLAG_BIT(6);
constexpr int MK_ROLL_INC		= FLAG_BIT(7);
constexpr int MK_ROLL_DEC		= FLAG_BIT(8);

App::App():
	frame_(0),
	terrain_mod_options_(-1),
	edit_mode_(false),
	edit_brush_(0), // 0: raise, 1: lower
	selected_object_index_(0),
	show_hud_(true),
	show_debug_(false),


	prior_frame_time_(0),
	skip_input_on_motion_once_(false)
{
	view_define_.pos_ = glm::vec3(0.0f);
	view_define_.forward_ = VEC3_Y_DIR;
	view_define_.right_ = VEC3_X_DIR;
	view_define_.up_ = VEC3_Z_DIR;
	view_define_.fovx_ = glm::radians(FOVY_IN_DEGREE);
	view_define_.fovy_ = glm::radians(FOVY_IN_DEGREE);
	view_define_.render_z_near_ = RENDER_Z_NEAR;
	view_define_.render_z_far_ = RENDER_Z_FAR;
	view_define_.render_min_depth_ = RENDER_DEPTH_MIN;
	view_define_.render_max_depth_ = RENDER_DEPTH_MAX;
	view_define_.viewport_width_ = 1;
	view_define_.viewport_height_ = 1;

	draw_params_.view_define_ = &view_define_;
	draw_params_.overlay_wireframe_ = false;
	draw_params_.draw_parts_ = -1;
	draw_params_.draw_terrain_options_ = -1;
	draw_params_.flat_sky_layer_is_visible_ = true;
	draw_params_.num_terrain_render_chunk_ = 0;
	draw_params_.selected_object_index_ = -1;

	memset(&window_state_, 0, sizeof(window_state_));
	memset(&mouse_state_, 0, sizeof(mouse_state_));
	memset(&input_, 0, sizeof(input_));

	window_state_.cursor_visible_ = true;

	memset(&viewer_, 0, sizeof(viewer_));
	viewer_.clip_to_z_ = true;
	viewer_.move_speed_ = MIN_MOVE_SPEED;
	viewer_.jump_speed_ = MIN_JUMP_SPEED;
}

App::~App() {
	Shutdown();
}

bool App::Init(int argc, char** argv) {
	Logger::Get().Init("igi_terrain_editor.log");
	Logger::Get().Log(LogLevel::INFO, "Project IGI 3D Editor Initializing...");

	char appDataPath[1024];
	GetEnvironmentVariableA("APPDATA", appDataPath, 1024);
	std::string qCompilerPath = std::string(appDataPath) + "\\QEditor\\QCompiler";
	if (!std::filesystem::exists(qCompilerPath)) {
		Log(log_type_t::LOG_FATAL, __FILE__, __LINE__, "FATAL ERROR: QCompiler directory not found at: %s\nPlease make sure QEditor is correctly installed in AppData.", qCompilerPath.c_str());
	}


	if (!renderer_.Init()) {
		return false;
	}

	Config::Init();
	ConfigData& cfg = Config::Get();


	// read options from command line
	draw_params_.overlay_wireframe_ = Arg_OptionIdx(argc, argv, "-wireframe") > 0;
	draw_params_.draw_parts_ = Arg_ReadInt(argc, argv, "-draw_parts", -1);
	draw_params_.draw_terrain_options_ = Arg_ReadInt(argc, argv, "-draw_terrain_opts", -1);
	terrain_mod_options_ = Arg_ReadInt(argc, argv, "-terrain_mod_opts", terrain_mod_options_);

	int start_level = Arg_ReadInt(argc, argv, "-level", cfg.level);
	if (start_level >= MIN_LEVEL_NO && start_level <= MAX_LEVEL_NO) {
		LoadLevel(start_level);
	}


	if (Arg_OptionIdx(argc, argv, "-yaw") > -1) {
		// override yaw
		viewer_.yaw_ = Arg_ReadFloat(argc, argv, "-yaw", 0.0f);
		UpdateViewerVectors();
	}

	if (Arg_OptionIdx(argc, argv, "-pitch") > -1) {
		// override pitch
		viewer_.pitch_ = Arg_ReadFloat(argc, argv, "-pitch", 0.0f);
		UpdateViewerVectors();
	}

	int wnd_w = Arg_ReadInt(argc, argv, "-w", 800);
	int wnd_h = Arg_ReadInt(argc, argv, "-h", 600);
	OnWindowResize(wnd_w, wnd_h);

	prior_frame_time_ = Sys_Milliseconds();

	bridge_.SetEnabled(show_hud_);
	bridge_.Start();


	return true;
}

void App::Shutdown() {
	bridge_.Stop();
	level_.Unload();
	level_.FreeTerrainCubeDataPools();
	renderer_.Shutdown();

}

void App::LoadLevel(int level_no) {
	renderer_.BeginLoadLevel();

	Level::load_params_s level_load_params_s = {
		.level_no_ = level_no,
		.render_res_loader_ = &renderer_
	};

	glm::vec3 start_pos;
	float start_yaw;
	if (level_.Load(level_load_params_s, start_pos, start_yaw)) {
		viewer_.pos_ = start_pos;
		viewer_.yaw_ = 13.0f; // Manually updated to requested start angle
		viewer_.pitch_ = 10.0f; // Manually updated to requested start angle
		viewer_.roll_ = 0.0f;

		UpdateViewerVectors();
	}

}

void App::SetGameLevel(int level_no) {
	bridge_.SetGameLevel(level_no);
}

void App::SaveCurrentLevel() {
	level_.SaveChanges();
}

int App::GetCurLevelNo() const {
	return level_.GetLevelNo();
}

void App::ToggleOverlayWireframe() {
	draw_params_.overlay_wireframe_ = !draw_params_.overlay_wireframe_;
}

void App::ToggleDrawParts(int part) {
	if (draw_params_.draw_parts_ & part) {
		draw_params_.draw_parts_ &= ~part;
	}
	else {
		draw_params_.draw_parts_ |= part;
	}
}

void App::ToggleTerrainDrawOption(int opt) {
	if (draw_params_.draw_terrain_options_ & opt) {
		draw_params_.draw_terrain_options_ &= ~opt;
	}
	else {
		draw_params_.draw_terrain_options_ |= opt;
	}
}

void App::ToggleTerrainModOption(int opt) {
	if (terrain_mod_options_ & opt) {
		terrain_mod_options_ &= ~opt;
	}
	else {
		terrain_mod_options_ |= opt;
	}
}

bool App::GetOverlayWireframe() const {
	return draw_params_.overlay_wireframe_;
}

int	App::GetDrawParts() const {
	return draw_params_.draw_parts_;
}

int	App::GetTerrainDrawOptions() const {
	return draw_params_.draw_terrain_options_;
}

int	App::GetTerrainModOptions() const {
	return terrain_mod_options_;
}

// events
void App::OnWindowResize(int width, int height) {
	window_state_.viewport_width_ = std::max(1, width);
	window_state_.viewport_height_ = std::max(1, height);

	view_define_.viewport_width_ = window_state_.viewport_width_;
	view_define_.viewport_height_ = window_state_.viewport_height_;

	glViewport(0, 0, width, height);

	// update fovx_
	float h = std::tan(view_define_.fovy_ * 0.5f);
	float w = h * width / height;
	view_define_.fovx_ = std::atan(w) * 2.0f;

	float tan_half_fovx = (float)std::tan(view_define_.fovx_ * 0.5);
	float tan_half_fovy = (float)std::tan(view_define_.fovy_ * 0.5);

	view_define_.tan_half_fovx_ = tan_half_fovx;
	view_define_.tan_half_fovy_ = tan_half_fovy;

	view_define_.half_viewport_width_div_tan_half_fovx_ = window_state_.viewport_width_ * 0.5f / tan_half_fovx;
	view_define_.half_viewport_height_div_tan_half_fovy_ = window_state_.viewport_height_ * 0.5f / tan_half_fovy;

}

void App::OnDisplay() {
	// ignore
}

// input
void App::Input_OnMouse(int button, int state, int x, int y) {
	if (button == GLUT_LEFT_BUTTON) {
		if (GLUT_DOWN == state) {
			mouse_state_.left_button_down_ = true;
			
		}
		else if (GLUT_UP == state) {
			mouse_state_.left_button_down_ = false;

			if (window_state_.cursor_visible_) {
				input_.mouse_delta_x_ = 0;
				input_.mouse_delta_y_ = 0;
			}
		}
	}

	mouse_state_.prior_x_ = x;
	mouse_state_.prior_y_ = y;
}

void App::Input_OnMotion(int x, int y) {
	if (window_state_.cursor_visible_) {
		if (mouse_state_.left_button_down_) {
			input_.mouse_delta_x_ = x - mouse_state_.prior_x_;
			input_.mouse_delta_y_ = y - mouse_state_.prior_y_;

			mouse_state_.prior_x_ = x;
			mouse_state_.prior_y_ = y;
		}
	}
	else {
		if (skip_input_on_motion_once_) {
			skip_input_on_motion_once_ = false;
			return;
		}

		int center_x = window_state_.viewport_width_ >> 1;
		int center_y = window_state_.viewport_height_ >> 1;

		input_.mouse_delta_x_ += x - center_x;
		input_.mouse_delta_y_ += y - center_y;

		mouse_state_.prior_x_ = x;
		mouse_state_.prior_y_ = y;

		glutWarpPointer(center_x, center_y); // move cursor to view center
		skip_input_on_motion_once_ = true;   // skip next cursor motion event caused by glutWarpPointer
	}
}

void App::Input_OnSpecial(int key, int x, int y) {
	if (key == GLUT_KEY_F3) {
		viewer_.clip_to_z_ = !viewer_.clip_to_z_;
		return;
	}
	else if (key == GLUT_KEY_F4) {
		window_state_.cursor_visible_ = !window_state_.cursor_visible_;

		glutSetCursor(window_state_.cursor_visible_ ? GLUT_CURSOR_LEFT_ARROW : GLUT_CURSOR_NONE);

		if (!window_state_.cursor_visible_) {
			glutWarpPointer(window_state_.viewport_width_ >> 1, window_state_.viewport_height_ >> 1);
			input_.mouse_delta_x_ = input_.mouse_delta_y_ = 0;
		}

		return;
	}

	if (key == GLUT_KEY_PAGE_UP) {
		viewer_.move_speed_ *= 2.0f;
		viewer_.jump_speed_ *= 2.0f;

		if (viewer_.move_speed_ > MAX_MOVE_SPEED) {
			viewer_.move_speed_ = MAX_MOVE_SPEED;
		}

		if (viewer_.jump_speed_ > MAX_JUMP_SPEED) {
			viewer_.jump_speed_ = MAX_JUMP_SPEED;
		}

		printf("current move speed set to %d, jump speed set to %d\n",
			(int)viewer_.move_speed_, (int)viewer_.jump_speed_);

		return;
	}

	if (key == GLUT_KEY_PAGE_DOWN) {
		viewer_.move_speed_ *= 0.5f;
		viewer_.jump_speed_ *= 0.5f;

		if (viewer_.move_speed_ < MIN_MOVE_SPEED) {
			viewer_.move_speed_ = MIN_MOVE_SPEED;
		}

		if (viewer_.jump_speed_ < MIN_JUMP_SPEED) {
			viewer_.jump_speed_ = MIN_JUMP_SPEED;
		}

		printf("current move speed set to %d, jump speed set to %d\n",
			(int)viewer_.move_speed_, (int)viewer_.jump_speed_);

		return;
	}

	if (key == GLUT_KEY_LEFT) {
		input_.keys_ |= MK_ROLL_DEC;
		return;
	}

	if (key == GLUT_KEY_RIGHT) {
		input_.keys_ |= MK_ROLL_INC;
		return;
	}
}

void App::Input_OnSpecialUp(int key, int x, int y) {
	if (key == GLUT_KEY_LEFT) {
		input_.keys_ &= ~MK_ROLL_DEC;
		return;
	}

	if (key == GLUT_KEY_RIGHT) {
		input_.keys_ &= ~MK_ROLL_INC;
		return;
	}
}

struct movement_key_s {
	char	lower_case_;
	char	upper_case_;
	int		key_flag_;
};

static constexpr movement_key_s MOVEMENT_KEYS[] = {
	'w', 'W', MK_FORWARD,
	's', 'S', MK_BACKWARD,
	'a', 'A', MK_LEFT,
	'd', 'D', MK_RIGHT,
	'q', 'Q', MK_STRAIGHT_UP,
	'z', 'Z', MK_STRAIGHT_DOWN,
	' ', ' ', MK_JUMP
};

void App::Input_OnKeyboard(unsigned char key, int x, int y) {
	if (key == 27) { // ESC
		TogglePauseMenu();
		return;
	}

	if (pause_mode_) {
		if (key == 'q' || key == 'Q') {
			exit(0);
		}
		if (key == 's' || key == 'S') {
			level_.SaveChanges();
			printf("Level changes saved.\n");
			TogglePauseMenu(); // Close menu after save
		}
		if (key == '=') {
			ResetLevel();
			TogglePauseMenu();
		}
		if (key == 'r' || key == 'R') {
			ResetScript();
			TogglePauseMenu();
		}
		if (key == 'd' || key == 'D') {
			show_debug_ = !show_debug_;
			TogglePauseMenu();
		}
		return;

	}


	if ((key == 13) && (glutGetModifiers() & GLUT_ACTIVE_ALT)) { // ALT + ENTER toggle full screen mode
		window_state_.full_screen_ = !window_state_.full_screen_;

		if (window_state_.full_screen_) {

			window_state_.old_viewport_width_ = window_state_.viewport_width_;
			window_state_.old_viewport_height_ = window_state_.viewport_height_;

			glutFullScreen();
		}
		else {
			glutReshapeWindow(window_state_.old_viewport_width_, window_state_.old_viewport_height_);
		}

		return;
	}

	for (int i = 0; i < count_of(MOVEMENT_KEYS); ++i) {
		const movement_key_s& mk = MOVEMENT_KEYS[i];
		if (key == mk.lower_case_ || key == mk.upper_case_) {
			input_.keys_ |= mk.key_flag_;
			return;
		}
	}

	if (key == 't' || key == 'T') {
		level_.TeleportToHMP(viewer_.pos_);
		viewer_.pitch_ = -30.0f; // Look down at the terrain
		UpdateViewerVectors();
		printf("Teleported to Height Map Zone\n");
		return;
	}

	if (key == 'l' || key == 'L') {
		show_hud_ = !show_hud_;
		return;
	}


	if (key == '\t') {
		LevelObjects& lo = level_.GetLevelObjects();
		if (!lo.GetObjects().empty()) {
			selected_object_index_ = (selected_object_index_ + 1) % (int)lo.GetObjects().size();
			printf("Selected Object: %d / %d\n", selected_object_index_, (int)lo.GetObjects().size());
		}
		return;
	}

	HandleMarkerInput(key);

}

void App::HandleMarkerInput(unsigned char key) {
	LevelObjects& lo = level_.GetLevelObjects();
	std::vector<LevelObject>& objects = lo.GetObjects();
	if (objects.empty()) return;

	if (selected_object_index_ < 0 || selected_object_index_ >= (int)objects.size()) {
		selected_object_index_ = 0;
	}

	LevelObject& obj = objects[selected_object_index_];
	ConfigData& cfg = Config::Get();
	char k = (char)toupper(key);

	// Movement steps
	const float moveStep = 1024.0f; // ~0.25m
	const float rotStep = 0.05f;  // ~3 degrees

	bool changed = false;

	if (k == cfg.moveForward) { obj.pos.z -= moveStep; changed = true; }
	else if (k == cfg.moveBackward) { obj.pos.z += moveStep; changed = true; }
	else if (k == cfg.moveLeft) { obj.pos.x -= moveStep; changed = true; }
	else if (k == cfg.moveRight) { obj.pos.x += moveStep; changed = true; }
	else if (k == cfg.moveUp) { obj.pos.y += moveStep; changed = true; }
	else if (k == cfg.moveDown) { obj.pos.y -= moveStep; changed = true; }

	else if (k == cfg.rotateYawCCW) { obj.rot.z -= rotStep; changed = true; }
	else if (k == cfg.rotateYawCW) { obj.rot.z += rotStep; changed = true; }
	else if (k == cfg.rotatePitchUp) { obj.rot.x += rotStep; changed = true; }
	else if (k == cfg.rotatePitchDown) { obj.rot.x -= rotStep; changed = true; }
	else if (k == cfg.rotateRollLeft) { obj.rot.y -= rotStep; changed = true; }
	else if (k == cfg.rotateRollRight) { obj.rot.y += rotStep; changed = true; }

	else if (k == cfg.teleportToMarker) {
		viewer_.pos_ = obj.pos;
		UpdateViewerVectors();
	}
	else if (k == cfg.resetMarkerToPlayer) {
		obj.pos = viewer_.pos_;
		changed = true;
	}

	if (changed) {
		printf("Marker [%d] Manipulated: Pos(%.0f, %.0f, %.0f) Rot(%.2f, %.2f, %.2f)\n",
			selected_object_index_, obj.pos.x, obj.pos.y, obj.pos.z, obj.rot.x, obj.rot.y, obj.rot.z);
	}
}

void App::ResetLevel() {
	char appData[1024];
	GetEnvironmentVariableA("APPDATA", appData, 1024);

	int levelNo = level_.GetLevelNo();
	ConfigData& cfg = Config::Get();

	char srcDir[1024];
	Str_SPrintf(srcDir, 1024, "%s\\QEditor\\QFiles\\IGI_QVM\\missions\\location0\\level%d", appData, levelNo);

	char dstDir[1024];
	Str_SPrintf(dstDir, 1024, "%s\\missions\\location0\\level%d", cfg.igiPath.c_str(), levelNo);

	printf("Resetting Level %d from %s to %s\n", levelNo, srcDir, dstDir);

	try {
		if (std::filesystem::exists(srcDir)) {
			std::filesystem::copy(srcDir, dstDir, std::filesystem::copy_options::overwrite_existing | std::filesystem::copy_options::recursive);
			printf("Level reset successful.\n");
		}
		else {
			printf("Error: Source directory %s does not exist.\n", srcDir);
		}
	}
	catch (const std::exception& e) {
		printf("ResetLevel error: %s\n", e.what());
	}
}

void App::ResetScript() {
	char appData[1024];
	GetEnvironmentVariableA("APPDATA", appData, 1024);

	int levelNo = level_.GetLevelNo();

	char srcPath[1024];
	Str_SPrintf(srcPath, 1024, "%s\\QEditor\\QFiles\\IGI_QSC\\missions\\location0\\level%d\\objects.qsc", appData, levelNo);

	char dstPath[1024];
	Str_SPrintf(dstPath, 1024, "%s/missions/location0/level%d/objects.qsc", g_folders.res_folder_, levelNo);

	printf("Resetting Script for Level %d from %s to %s\n", levelNo, srcPath, dstPath);

	try {
		if (std::filesystem::exists(srcPath)) {
			std::filesystem::copy_file(srcPath, dstPath, std::filesystem::copy_options::overwrite_existing);
			printf("Script reset successful.\n");

			// Reload the level to apply changes
			LoadLevel(levelNo);
		}
		else {
			printf("Error: Source file %s does not exist.\n", srcPath);
		}
	}
	catch (const std::exception& e) {
		printf("ResetScript error: %s\n", e.what());
	}
}



void App::Input_OnKeyboardUp(unsigned char key, int x, int y) {
	for (int i = 0; i < count_of(MOVEMENT_KEYS); ++i) {
		const movement_key_s& mk = MOVEMENT_KEYS[i];
		if (key == mk.lower_case_ || key == mk.upper_case_) {
			input_.keys_ &= ~mk.key_flag_;
			return;
		}
	}
}

// idle
void App::OnIdle() {
	int64_t cur_time = Sys_Milliseconds();
	int64_t delta_time = cur_time - prior_frame_time_;
	if (delta_time < 16) {
		return;
	}

	Frame(delta_time * 0.001f);	// convert to seconds

	prior_frame_time_ = cur_time;
}

void App::Frame(float delta_seconds) {
	if (pause_mode_) {
		// Skip all updates when paused, just render
		UpdateViewDefine();
		float ground_z = 0.0f;
		level_.GetTerrainZ(viewer_.pos_, ground_z);
		Renderer::hud_params_s hud = {
			.show_hud_ = true,
			.status_msg_ = "IGI LINK: NATIVE MODE",
			.raw_pos_ = viewer_.pos_,
			.meters_pos_ = viewer_.pos_ / 4096.0f,
			.ground_offset_ = viewer_.pos_.z - ground_z,
			.human_addr_ = 0,
			.game_level_ = level_.GetLevelNo(),
			.view_h_ = viewer_.yaw_,
			.view_v_ = viewer_.pitch_,
			.cam_pitch_ = viewer_.pitch_,
			.cam_yaw_ = viewer_.yaw_,
			.cam_roll_ = viewer_.roll_,
			.cam_fov_ = 60.0f,
			.pause_mode_ = true,
			.show_debug_ = show_debug_,
			.selected_object_index_ = selected_object_index_,
			.level_objects_ = &level_.GetLevelObjects()
		};
		draw_params_.level_objects_ = &level_.GetLevelObjects();
		draw_params_.selected_object_index_ = selected_object_index_;
		renderer_.Draw(draw_params_, hud);

		glutSwapBuffers();
		return;
	}

	frame_++;
	frame_ %= 0xFFFFFFFF;	// reserve value 0xFFFFFFFF (-1) for INVALID_FRAME

	ProcessInput(delta_seconds);

	if (edit_mode_ && mouse_state_.left_button_down_) {
		EditorProcessClick();
	}

	UpdateViewDefine();

	vert_flat_sky_layer_s * fsl_vb = renderer_.MapFlatSkyLayersVB();
	vert_pos_a_uv_s* terrain_vb = renderer_.MapTerrainVB();
	uint32_t* terrain_ib = renderer_.MapTerrainIB();
	render_chunk_s* render_chunks = renderer_.GetTerrainRenderChunckBuffer();

	update_params_s update_params = {
		.frame_ = frame_,
		.delta_seconds_ = delta_seconds,
		.view_define_ = &view_define_,
		.flat_sky_layer_vb_ = fsl_vb,
		.terrain_mod_options_ = terrain_mod_options_,
		.terrain_vb_ = terrain_vb,
		.terrain_ib_ = terrain_ib,
		.terrain_render_chunks_ = render_chunks
	};

	level_.Update(update_params);

	if (terrain_ib) {
		renderer_.UnmapTerrainIB();
	}

	if (terrain_vb) {
		renderer_.UnmapTerrainVB();
	}

	if (fsl_vb) {
		renderer_.UnmapFlatSkyLayersVB();
	}

	draw_params_.flat_sky_layer_is_visible_ = update_params.flat_sky_layer_is_visible_;
	draw_params_.num_terrain_render_chunk_ = update_params.num_terrain_render_chunk_;
	draw_params_.level_objects_ = &level_.GetLevelObjects();
	draw_params_.selected_object_index_ = selected_object_index_;


	float ground_z = 0.0f;
	bridge_.SetEnabled(show_hud_);
	IGIBridge::PositionData data = bridge_.GetLatestData();
	level_.GetTerrainZ(viewer_.pos_, ground_z);

	Renderer::hud_params_s hud = {
		.show_hud_ = show_hud_,
		.status_msg_ = data.status_msg,
		.raw_pos_ = viewer_.pos_,
		.meters_pos_ = viewer_.pos_ / 4096.0f,
		.ground_offset_ = viewer_.pos_.z - ground_z,
		.human_addr_ = 0, // No longer reading memory
		.game_level_ = level_.GetLevelNo(),
		.view_h_ = viewer_.yaw_,
		.view_v_ = viewer_.pitch_,
		.cam_pitch_ = viewer_.pitch_,
		.cam_yaw_ = viewer_.yaw_,
		.cam_roll_ = viewer_.roll_,
		.cam_fov_ = 60.0f, // Placeholder
		.pause_mode_ = pause_mode_,
		.show_debug_ = show_debug_,
		.selected_object_index_ = selected_object_index_,
		.level_objects_ = &level_.GetLevelObjects()
	};


	renderer_.Draw(draw_params_, hud);


	glutSwapBuffers();
}

void App::ProcessInput(float delta_seconds) {
	if (!edit_mode_) {
		viewer_.yaw_ += -input_.mouse_delta_x_ * MOUSE_SENSITIVE;
		viewer_.pitch_ += -input_.mouse_delta_y_ * MOUSE_SENSITIVE;
	}

	input_.mouse_delta_x_ = 0;
	input_.mouse_delta_y_ = 0;

	UpdateViewerVectors();

	if (viewer_.clip_to_z_) {

		float z0 = viewer_.pos_.z - VIEW_HEIGHT;
		float friction = viewer_.move_speed_ * 2.0f;

		// check movement
		if (input_.keys_ & MK_FORWARD) {
			viewer_.velocity_.x = viewer_.forward_.x * viewer_.move_speed_;
			viewer_.velocity_.y = viewer_.forward_.y * viewer_.move_speed_;
		}

		if (input_.keys_ & MK_BACKWARD) {
			viewer_.velocity_.x = viewer_.forward_.x * -viewer_.move_speed_;
			viewer_.velocity_.y = viewer_.forward_.y * -viewer_.move_speed_;
		}

		if (input_.keys_ & MK_LEFT) {
			viewer_.velocity_.x = viewer_.right_.x * -viewer_.move_speed_;
			viewer_.velocity_.y = viewer_.right_.y * -viewer_.move_speed_;
		}

		if (input_.keys_ & MK_RIGHT) {
			viewer_.velocity_.x = viewer_.right_.x * viewer_.move_speed_;
			viewer_.velocity_.y = viewer_.right_.y * viewer_.move_speed_;
		}

		if (input_.keys_ & MK_JUMP && viewer_.clip_to_z_) {
			if (viewer_.velocity_.z <= 0.0f && viewer_.on_ground_) {
				viewer_.velocity_.z = viewer_.jump_speed_;
			}
		}

		if (!input_.keys_ && viewer_.on_ground_) {

			float speed = (float)std::sqrt(viewer_.velocity_.x * viewer_.velocity_.x + viewer_.velocity_.y * viewer_.velocity_.y);
			if (speed > 1.0f) {
				float one_over_speed = 1.0f / speed;
				float dir_x = viewer_.velocity_.x * one_over_speed;
				float dir_y = viewer_.velocity_.y * one_over_speed;

				float speed_drop = friction * delta_seconds;
				speed -= speed_drop;

				if (speed <= 1.0f) {
					speed = 0.0f;
				}

				viewer_.velocity_.x = dir_x * speed;
				viewer_.velocity_.y = dir_y * speed;
			}
			else {
				viewer_.velocity_.x = 0.0f;
				viewer_.velocity_.y = 0.0f;
			}
		}

		glm::vec3 move_delta = viewer_.velocity_ * delta_seconds;
		viewer_.pos_ += move_delta;

		if (viewer_.velocity_.z <= 0.0f) {

			float ret_z = 0.0f;
			glm::vec3 get_z_pos = viewer_.pos_;
			get_z_pos.z = viewer_.pos_.z - VIEW_HEIGHT;
			bool ok = level_.GetTerrainZ(get_z_pos, ret_z);

			if (ok) {
				float view_z = ret_z + VIEW_HEIGHT;

				if (viewer_.pos_.z < view_z) {
					viewer_.pos_.z = view_z;
					viewer_.velocity_.z = 0.0f;
					viewer_.on_ground_ = true;
				}
				else {
					viewer_.on_ground_ = false;
				}

			}
			else {
				viewer_.on_ground_ = false;
			}

		}
		else {
			viewer_.on_ground_ = false;	// moving upward
		}

		if (!viewer_.on_ground_) {
			viewer_.velocity_.z -= GRAVITE * delta_seconds;
		}

	}
	else {

		// check movement
		if (input_.keys_ & MK_FORWARD) {
			viewer_.pos_ += viewer_.forward_ * viewer_.move_speed_ * delta_seconds;
		}

		if (input_.keys_ & MK_BACKWARD) {
			viewer_.pos_ -= viewer_.forward_ * viewer_.move_speed_ * delta_seconds;
		}

		if (input_.keys_ & MK_LEFT) {
			viewer_.pos_ -= viewer_.right_ * viewer_.move_speed_ * delta_seconds;
		}

		if (input_.keys_ & MK_RIGHT) {
			viewer_.pos_ += viewer_.right_ * viewer_.move_speed_ * delta_seconds;
		}

		if (input_.keys_ & MK_STRAIGHT_UP && !viewer_.clip_to_z_) {
			viewer_.pos_ += VEC3_Z_DIR * viewer_.move_speed_ * delta_seconds;
		}

		if (input_.keys_ & MK_STRAIGHT_DOWN && !viewer_.clip_to_z_) {
			viewer_.pos_ -= VEC3_Z_DIR * viewer_.move_speed_ * delta_seconds;
		}

	}

	// check rotation
	bool update_orientation = false;

	if (input_.keys_ & MK_ROLL_INC) {
		update_orientation = true;
		viewer_.roll_ += 1.0f;
	}

	if (input_.keys_ & MK_ROLL_DEC) {
		update_orientation = true;
		viewer_.roll_ -= 1.0f;
	}

	if (update_orientation) {
		UpdateViewerVectors();
	}
}

void App::UpdateViewerVectors() {
	// clamp yaw, pitch & roll

	while (viewer_.yaw_ < 0.0f) {
		viewer_.yaw_ += 360.0f;
	}

	while (viewer_.yaw_ > 360.0f) {
		viewer_.yaw_ -= 360.0f;
	}

	if (viewer_.pitch_ < -89.0f) {
		viewer_.pitch_ = -89.0f;
	}

	if (viewer_.pitch_ > 89.0f) {
		viewer_.pitch_ = 89.0f;
	}

	while (viewer_.roll_ < 0.0f) {
		viewer_.roll_ += 360.0f;
	}

	while (viewer_.roll_ > 360.0f) {
		viewer_.roll_ -= 360.0f;
	}

	AngleToVectors(viewer_.yaw_, viewer_.pitch_, viewer_.roll_,
		viewer_.forward_, viewer_.right_, viewer_.up_);
}

void App::UpdateViewDefine() {
	view_define_.pos_ = viewer_.pos_;
	view_define_.forward_ = viewer_.forward_;
	view_define_.right_ = viewer_.right_;
	view_define_.up_ = viewer_.up_;

	/* rotate to coordinate:

	  Z
	  /
	 /
	/________X
	|
	|
	|
	Y

	 */

	// rotation only, with out translate
	view_define_.mat_rot_[0][0] = view_define_.right_.x;
	view_define_.mat_rot_[1][0] = view_define_.right_.y;
	view_define_.mat_rot_[2][0] = view_define_.right_.z;

	view_define_.mat_rot_[0][1] = -view_define_.up_.x;
	view_define_.mat_rot_[1][1] = -view_define_.up_.y;
	view_define_.mat_rot_[2][1] = -view_define_.up_.z;

	view_define_.mat_rot_[0][2] = view_define_.forward_.x;
	view_define_.mat_rot_[1][2] = view_define_.forward_.y;
	view_define_.mat_rot_[2][2] = view_define_.forward_.z;
}

void App::ToggleShowHUD() {
	show_hud_ = !show_hud_;
}

bool App::GetShowHUD() const {
	return show_hud_;
}

void App::ToggleEditMode() {
	edit_mode_ = !edit_mode_;
	
	window_state_.cursor_visible_ = edit_mode_;
	glutSetCursor(window_state_.cursor_visible_ ? GLUT_CURSOR_CROSSHAIR : GLUT_CURSOR_NONE);

	if (!window_state_.cursor_visible_) {
		glutWarpPointer(window_state_.viewport_width_ >> 1, window_state_.viewport_height_ >> 1);
		input_.mouse_delta_x_ = input_.mouse_delta_y_ = 0;
	}
}

bool App::GetEditMode() const {
	return edit_mode_;
}

void App::TogglePauseMenu() {
	pause_mode_ = !pause_mode_;
	window_state_.cursor_visible_ = pause_mode_;
	glutSetCursor(window_state_.cursor_visible_ ? GLUT_CURSOR_LEFT_ARROW : GLUT_CURSOR_NONE);
}

bool App::GetPauseMode() const {
	return pause_mode_;
}

void App::SetEditBrush(int brush) {
	edit_brush_ = brush;
}

int App::GetEditBrush() const {
	return edit_brush_;
}

void App::SetSelectedObjectScale(float scale) {
	if (selected_object_index_ >= 0 && selected_object_index_ < (int)level_.GetLevelObjects().GetObjects().size()) {
		level_.GetLevelObjects().GetObjects()[selected_object_index_].scale = scale;
		Logger::Get().Log(LogLevel::INFO, "[App] Scale changed to " + std::to_string(scale) + " for object " + std::to_string(selected_object_index_));
	}
}

float App::GetSelectedObjectScale() const {
	if (selected_object_index_ >= 0 && selected_object_index_ < (int)level_.GetLevelObjects().GetObjects().size()) {
		return level_.GetLevelObjects().GetObjects()[selected_object_index_].scale;
	}
	return 1.0f;
}

#include <glm/ext/matrix_projection.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

void App::EditorProcessClick() {
	if (!window_state_.viewport_width_ || !window_state_.viewport_height_) return;

	// Build matrices exactly as Renderer does
	glm::dmat4 proj_matrix = glm::perspective(
		(double)view_define_.fovy_,
		(double)window_state_.viewport_width_ / (double)window_state_.viewport_height_,
		(double)view_define_.render_z_near_,
		(double)view_define_.render_z_far_
	);

	glm::dvec3 scaled_down_pos = glm::dvec3(view_define_.pos_) * 0.001; // RENDERER_MODEL_SCALE_DOWN
	glm::dmat4 mat_view = glm::lookAt(scaled_down_pos, scaled_down_pos + glm::dvec3(view_define_.forward_), glm::dvec3(view_define_.up_));
	glm::dmat4 mat_scale = glm::scale(glm::dmat4(1.0), glm::dvec3(0.001));
	glm::dmat4 view_matrix = mat_view * mat_scale;

	glm::dvec4 viewport(0.0, 0.0, (double)window_state_.viewport_width_, (double)window_state_.viewport_height_);
	
	double winX = (double)mouse_state_.prior_x_;
	double winY = (double)window_state_.viewport_height_ - (double)mouse_state_.prior_y_;
	
	glm::dvec3 start_pos = glm::unProject(glm::dvec3(winX, winY, 0.0), view_matrix, proj_matrix, viewport);
	glm::dvec3 end_pos = glm::unProject(glm::dvec3(winX, winY, 1.0), view_matrix, proj_matrix, viewport);
	
	glm::vec3 ray_origin = (glm::vec3)start_pos;
	glm::vec3 ray_dir = glm::normalize((glm::vec3)(end_pos - start_pos));

	// First, try to select an object (object selection takes priority)
	LevelObjects& lo = level_.GetLevelObjects();
	std::vector<LevelObject>& objects = lo.GetObjects();
	
	float closest_dist = FLT_MAX;
	int closest_object = -1;
	
	// Simple sphere intersection test for object selection
	constexpr float SELECTION_RADIUS = 5000.0f; // Hit radius in world units
	
	for (size_t i = 0; i < objects.size(); ++i) {
		glm::vec3 obj_pos = objects[i].pos;
		
		// Ray-sphere intersection
		glm::vec3 to_obj = obj_pos - ray_origin;
		float projection = glm::dot(to_obj, ray_dir);
		
		if (projection > 0) {
			glm::vec3 closest_point = ray_origin + ray_dir * projection;
			float dist_to_center = glm::length(closest_point - obj_pos);
			
			if (dist_to_center < SELECTION_RADIUS) {
				float dist_to_camera = glm::length(obj_pos - ray_origin);
				if (dist_to_camera < closest_dist) {
					closest_dist = dist_to_camera;
					closest_object = (int)i;
				}
			}
		}
	}
	
	if (closest_object != -1) {
		selected_object_index_ = closest_object;
		const LevelObject& obj = objects[closest_object];
		printf("SELECTED Object [%d]: %s (%s)\n", closest_object, obj.name.c_str(), obj.modelId.c_str());
		printf("  Pos: (%.0f, %.0f, %.0f)\n", obj.pos.x, obj.pos.y, obj.pos.z);
		printf("  Rot (Alpha/Beta/Gamma): (%.2f, %.2f, %.2f)\n", obj.rot.x, obj.rot.y, obj.rot.z);
		printf("  Scale: %.2f\n", obj.scale);
		return; // Don't do terrain editing if we selected an object
	}

	printf("EditorClick: Mouse(%.0f, %.0f), RayDir(%.2f, %.2f, %.2f)\n", winX, winY, ray_dir.x, ray_dir.y, ray_dir.z);

	level_.EditorRaycastAndModify(ray_origin, ray_dir, edit_brush_);
}
