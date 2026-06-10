/******************************************************************************
 * @file    imgui_panels.cpp
 * @brief   UI, input handling, cursor, Frame rendering — App methods extracted
 *          from app.cpp.
 *****************************************************************************/

#include "../pch.h"
#include <freeglut.h>
#include "../logger.h"
#include "../utils.h"
#include "../parsers/qsc_lexer.h"
#include "../parsers/qsc_parser.h"
#include "../parsers/qvm_compiler.h"
#include "../parsers/qvm_parser.h"
#include "../parsers/qvm_decompiler.h"
#include "../parsers/tex_parser.h"
#include "../renderer/gl_helper.h"
#include "../level/task_schema.h"
using namespace TaskSchemaNS;
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <unordered_map>

// BuildRotMatZXY, ExtractEulerZXY defined in camera/camera.cpp (global linkage)
glm::dmat3 BuildRotMatZXY(const glm::dvec3& euler);
glm::dvec3 ExtractEulerZXY(const glm::dmat3& M);

// Key-state flags (same values as app.cpp — internal linkage, no ODR violation)
constexpr int MK_FORWARD       = FLAG_BIT(0);
constexpr int MK_BACKWARD      = FLAG_BIT(1);
constexpr int MK_LEFT          = FLAG_BIT(2);
constexpr int MK_RIGHT         = FLAG_BIT(3);
constexpr int MK_STRAIGHT_UP   = FLAG_BIT(4);
constexpr int MK_STRAIGHT_DOWN = FLAG_BIT(5);
constexpr int MK_ROLL_INC      = FLAG_BIT(7);
constexpr int MK_ROLL_DEC      = FLAG_BIT(8);
constexpr int MK_MANIP_A       = FLAG_BIT(10);
constexpr int MK_MANIP_B       = FLAG_BIT(11);
constexpr int MK_MANIP_G       = FLAG_BIT(12);
constexpr int MK_MANIP_S       = FLAG_BIT(13);
constexpr int MK_MANIP_O       = FLAG_BIT(14);
constexpr int MK_MANIP_SPACE   = FLAG_BIT(15);
// Movement speed constants (same values as app.cpp — internal linkage, no ODR violation)
constexpr float MIN_JUMP_SPEED = 4.0f * WORLD_UNITS_PER_METER;
constexpr float MAX_JUMP_SPEED = 512.0f * WORLD_UNITS_PER_METER;

// ── C1: Custom SPR cursor — multi-mode ────────────────────────────────────────

static GLuint LoadOneSpr(const char* path, int& w, int& h) {
	TEXFile tex = TEX_Parse(path);
	if (!tex.valid || tex.images.empty()) return 0;
	const TEXImage& img = tex.images[0];

	std::vector<uint8_t> rgba;
	if (img.mode == 2) { // RGB565 — no alpha channel; chroma-key dark pixels as transparent
		rgba.reserve(img.width * img.height * 4);
		for (size_t i = 0; i + 1 < img.pixels.size(); i += 2) {
			uint16_t p = img.pixels[i] | ((uint16_t)img.pixels[i + 1] << 8);
			uint8_t r = ((p >> 11) & 0x1F) << 3;
			uint8_t g = ((p >> 5)  & 0x3F) << 2;
			uint8_t b =  (p        & 0x1F) << 3;
			// Chroma-key: near-black pixels become transparent background
			uint8_t a = (r < 30 && g < 30 && b < 30) ? 0 : 255;
			rgba.push_back(r);
			rgba.push_back(g);
			rgba.push_back(b);
			rgba.push_back(a);
		}
	} else { // ARGB8888 (modes 3, 67) — swizzle BGRA → RGBA
		rgba.resize(img.pixels.size());
		for (size_t i = 0; i + 3 < img.pixels.size(); i += 4) {
			rgba[i + 0] = img.pixels[i + 2]; // R ← B
			rgba[i + 1] = img.pixels[i + 1]; // G
			rgba[i + 2] = img.pixels[i + 0]; // B ← R
			rgba[i + 3] = img.pixels[i + 3]; // A
		}
	}

	pic_s pic;
	pic.width_  = (int)img.width;
	pic.height_ = (int)img.height;
	pic.pixels_ = rgba.data();
	w = (int)img.width;
	h = (int)img.height;
	return GL_RegisterTexture(&pic, GL_CLAMP_TO_EDGE, GL_LINEAR, GL_LINEAR, false);
}

void App::LoadAllCursors() {
	// Order must match CursorMode enum values (0..9)
	const char* paths[NUM_CURSORS] = {
		"content\\qed\\TerrainEditIcon_Pointer.spr",     // 0 Default
		"content\\qed\\highlighttool.spr",               // 1 Hover
		"content\\qed\\activetool.spr",                  // 2 Selected
		"content\\qed\\TerrainEditIcon_Lift.spr",        // 3 TerrainLift
		"content\\qed\\TerrainEditIcon_Lower.spr",       // 4 TerrainLower
		"content\\qed\\TerrainEditIcon_Flatten.spr",     // 5 TerrainFlatten
		"content\\qed\\TerrainEditIcon_FlattenLine.spr", // 6 TerrainFlattenLine
		"content\\qed\\TerrainEditIcon_Drop.spr",        // 7 TerrainDrop
		"content\\qed\\TerrainEditIcon_Soften.spr",      // 8 TerrainSoften
		"content\\qed\\inactivetool.spr",                // 9 Inactive
		"content\\qed\\editor_camera.spr",               // 10 Camera (ALT held)
		"content\\qed\\editor_move.spr",                 // 11 Move (ALT held + moving)
	};
	cursor_loaded_count_ = 0;
	for (int i = 0; i < NUM_CURSORS; ++i) {
		cursor_tex_ids_[i] = LoadOneSpr(paths[i], cursor_tex_ws_[i], cursor_tex_hs_[i]);
		if (cursor_tex_ids_[i]) cursor_loaded_count_++;
	}

	// Cache AITYPE_ model IDs from IGIModels.json
	ai_model_ids_.clear();
	{
		std::string jsonPath = Utils::GetExeDirectory() + "\\content\\tools\\IGIModels.json";
		std::ifstream jf(jsonPath);
		if (jf) {
			std::string content((std::istreambuf_iterator<char>(jf)), {});
			size_t pos = 0;
			while ((pos = content.find("\"AITYPE_", pos)) != std::string::npos) {
				size_t idPos = content.find("\"ModelId\"", pos);
				if (idPos != std::string::npos && idPos - pos < 200) {
					size_t q1 = content.find('"', idPos + 9);
					size_t q2 = (q1 != std::string::npos) ? content.find('"', q1 + 1) : std::string::npos;
					if (q1 != std::string::npos && q2 != std::string::npos)
						ai_model_ids_.insert(content.substr(q1 + 1, q2 - q1 - 1));
				}
				pos++;
			}
		}
		Logger::Get().Log(LogLevel::INFO, "[App] Loaded " + std::to_string(ai_model_ids_.size()) + " AITYPE_ model IDs");
	}
}

void App::LoadHelpEntries() {
	help_entries_.clear();
	std::vector<std::string> paths = {
		Utils::GetExeDirectory() + "\\content\\qed\\qedkeybindings.qsc",
		Utils::GetExeDirectory() + "\\qed\\qedkeybindings.qsc",
		Utils::GetIGIRootPath() + "\\content\\qed\\qedkeybindings.qsc",
		Utils::GetIGIRootPath() + "\\qed\\qedkeybindings.qsc",
		"content\\qed\\qedkeybindings.qsc",
		"qed\\qedkeybindings.qsc"
	};
	std::ifstream f;
	for (const auto& p : paths) {
		f.open(p);
		if (f.is_open()) {
			Logger::Get().Log(LogLevel::INFO, "[App] Loaded keybindings from: " + p);
			break;
		}
		f.clear();
	}
	if (!f.is_open()) {
		Logger::Get().Log(LogLevel::WARNING, "[App] Could not load help keybindings file: qedkeybindings.qsc");
		return;
	}
	std::string line;
	while (std::getline(f, line)) {
		// Strip leading whitespace
		size_t start = line.find_first_not_of(" \t\r\n");
		if (start == std::string::npos) continue;
		line = line.substr(start);
		if (line.empty() || line[0] == '/' ) continue; // skip blank/comment lines
		// Keep SetEventBinding lines; format them as "  Key  =>  Event"
		if (line.find("SetEventBinding") == 0 || line.find("QEDSetEventBinding") == 0) {
			// extract: SetEventBinding("EventName", "KeyCombo") or QEDSetEventBinding(...)
			size_t q1 = line.find('"');
			size_t q2 = (q1 != std::string::npos) ? line.find('"', q1 + 1) : std::string::npos;
			size_t q3 = (q2 != std::string::npos) ? line.find('"', q2 + 1) : std::string::npos;
			size_t q4 = (q3 != std::string::npos) ? line.find('"', q3 + 1) : std::string::npos;
			if (q1 != std::string::npos && q2 != std::string::npos &&
			    q3 != std::string::npos && q4 != std::string::npos) {
				std::string evtName = line.substr(q1 + 1, q2 - q1 - 1);
				std::string keyCombo = line.substr(q3 + 1, q4 - q3 - 1);
				// Replace angle-bracket modifiers for readability
				auto repl = [](std::string s, const std::string& from, const std::string& to) {
					size_t p = 0;
					while ((p = s.find(from, p)) != std::string::npos) { s.replace(p, from.size(), to); p += to.size(); }
					return s;
				};
				keyCombo = repl(keyCombo, "<Alt>", "Alt+");
				keyCombo = repl(keyCombo, "<Ctrl>", "Ctrl+");
				keyCombo = repl(keyCombo, "<Control>", "Ctrl+");
				keyCombo = repl(keyCombo, "<Shift>", "Shift+");
				keyCombo = repl(keyCombo, "<", "");
				keyCombo = repl(keyCombo, ">", "");
				help_entries_.push_back(keyCombo + "  =>  " + evtName);
			}
		}
	}
}

void App::LoadAutoCompleteKeywords() {
	autocomplete_keywords_.clear();
	std::string path = Utils::GetExeDirectory() + "\\content\\tools\\AutoCompleteKeywords.txt";
	std::ifstream f(path);
	if (!f.is_open()) {
		// try IGI root fallback
		path = Utils::GetIGIRootPath() + "\\content\\tools\\AutoCompleteKeywords.txt";
		f.open(path);
	}
	if (!f.is_open()) { Logger::Get().Log(LogLevel::WARNING, "[App] AutoCompleteKeywords.txt not found"); return; }
	std::string line;
	while (std::getline(f, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (!line.empty()) autocomplete_keywords_.push_back(line);
	}
	Logger::Get().Log(LogLevel::INFO, "[App] Loaded " + std::to_string(autocomplete_keywords_.size()) + " autocomplete keywords");
}

void App::SaveTaskSubtreeToFile(int idx, const std::string& path) {
	auto& objects = level_.GetLevelObjects().GetObjects();
	if (idx < 0 || idx >= (int)objects.size()) { status_message_ = "Save task: no task selected"; return; }
	// Serialize ONLY the selected task + its descendants as a proper nested QSC block.
	// (Previously this wrote each object's raw qscLine, but a parent's qscLine holds the
	//  whole original nested block, so it exported the entire object.)
	level_.GetLevelObjects().SaveSubtreeToQSC(idx, path);
	status_message_ = "Saved task to: " + path;
	Config::Get().taskFileName = path;
}

void App::ConfirmFileDialog() {
	std::string path = file_dialog_path_;
	FileDialogMode mode = file_dialog_mode_;
	file_dialog_mode_ = FileDialogMode::None;
	if (mode != FileDialogMode::SaveObjectFile)
		Config::Get().taskFileName = path; // remember last sub-task path only

	if (mode == FileDialogMode::SaveObjectFile) {
		// Ctrl+S: write the live objects QSC to the user-specified path.
		level_.GetLevelObjects().SaveToQSC(path);
		status_message_ = "Saved objects file: " + path;
	} else if (mode == FileDialogMode::SaveSubTask) {
		SaveTaskSubtreeToFile(selected_object_index_, path);
	} else if (mode == FileDialogMode::SaveSubTaskParent) {
		auto& objects = level_.GetLevelObjects().GetObjects();
		if (selected_object_index_ >= 0) {
			int par = objects[selected_object_index_].parentIndex;
			if (par >= 0) SaveTaskSubtreeToFile(par, path);
		}
	} else if (mode == FileDialogMode::LoadSubTask) {
		std::ifstream f(path);
		if (!f.is_open()) { status_message_ = "Error: cannot read " + path; return; }
		std::string block((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
		f.close();
		block = Utils::Trim(block);
		if (block.empty()) { status_message_ = "LoadSubTask: file is empty"; return; }

		PushUndoState();
		// Flush current live objects to the temp QSC, append the loaded task block as a
		// new top-level task, then reparse the whole level from the merged QSC. Using the
		// real parser keeps nested subtrees intact.
		std::string tempQsc = Utils::GetExeDirectory() + "\\content\\qed\\temp\\objects.qsc";
		level_.GetLevelObjects().SaveToQSC(tempQsc);
		{
			std::ofstream out(tempQsc, std::ios::app);
			if (!out.is_open()) { status_message_ = "LoadSubTask: cannot write temp QSC"; return; }
			out << "\n" << block;
			if (block.back() != ';') out << ";";
			out << "\n";
		}
		level_.ReloadObjectsFromFile(tempQsc);
		EvaluateTrainTrackPositions();
		SnapObjectsToTerrain();
		RebuildLevelModelIds();
		// Object list changed — keep selection valid.
		if (selected_object_index_ >= (int)level_.GetLevelObjects().GetObjects().size())
			selected_object_index_ = -1;
		status_message_ = "Loaded task from: " + path + " (added as top-level task)";
		Logger::Get().Log(LogLevel::INFO, "[App] LoadSubTask: loaded " + path);
	}
}

void App::UpdateCursorMode() {
	if (terrain_edit_enabled_) {
		// Drop is the neutral terrain cursor; Lift/Lower for raise/lower brush
		if (edit_brush_ == 0)
			current_cursor_mode_ = CursorMode::TerrainLift;  // BRUSH_RAISE
		else if (edit_brush_ == 1)
			current_cursor_mode_ = CursorMode::TerrainLower; // BRUSH_LOWER
		else
			current_cursor_mode_ = CursorMode::TerrainDrop;  // any other terrain mode
		return;
	}
	// Camera mode (ALT held)
	bool enableCameraMode = Utils::IsKeyBindingPressed(Config::Get().keyEnableCamera);
	if (enableCameraMode) {
		// ALT + left button held = camera look (editor_camera.spr)
		// ALT + any movement = lateral move (editor_move.spr)
		current_cursor_mode_ = mouse_state_.left_button_down_ ? CursorMode::Camera : CursorMode::Move;
		camera_mode_moved_ = false;
		return;
	}
	current_cursor_mode_ = CursorMode::Default;
}

void App::DrawCustomCursor() {
	UpdateCursorMode();
	int idx = (int)current_cursor_mode_;
	if (idx < 0 || idx >= NUM_CURSORS || !cursor_tex_ids_[idx]) {
		idx = 0; // fallback to Default
		if (!cursor_tex_ids_[0]) return;
	}
	int mx = mouse_state_.prior_x_;
	int my = mouse_state_.prior_y_;
	int vw = window_state_.viewport_width_;
	int vh = window_state_.viewport_height_;
	int w  = cursor_tex_ws_[idx];
	int h  = cursor_tex_hs_[idx];

	// Match the renderer's fixed-function HUD state: a shader/VAO left bound from
	// the 3D pass makes immediate-mode textured quads draw nothing.
	glUseProgram(0);
	glBindVertexArray(0);
	glBindBufferBase(GL_UNIFORM_BUFFER, 0, 0);
	glActiveTexture(GL_TEXTURE0);

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_LIGHTING);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
	glOrtho(0, vw, vh, 0, -1, 1);   // y=0 at top (top-down to match mouse coords)
	glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();

	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, cursor_tex_ids_[idx]);
	glColor4f(1.f, 1.f, 1.f, 1.f);
	glBegin(GL_QUADS);
	  glTexCoord2f(0, 0); glVertex2i(mx,     my);
	  glTexCoord2f(1, 0); glVertex2i(mx + w, my);
	  glTexCoord2f(1, 1); glVertex2i(mx + w, my + h);
	  glTexCoord2f(0, 1); glVertex2i(mx,     my + h);
	glEnd();
	glBindTexture(GL_TEXTURE_2D, 0);
	glDisable(GL_TEXTURE_2D);

	glMatrixMode(GL_MODELVIEW);  glPopMatrix();
	glMatrixMode(GL_PROJECTION); glPopMatrix();
	glEnable(GL_DEPTH_TEST);
}

// ─────────────────────────────────────────────────────────────────────────────

void App::DrawProgressOverlay(const char* title, int pct, const char* stage) {
	pct = std::max(0, std::min(100, pct));
	int vw = window_state_.viewport_width_;
	int vh = window_state_.viewport_height_;
	if (vw <= 0 || vh <= 0) return;
	glViewport(0, 0, vw, vh);
	glClearColor(0.02f, 0.06f, 0.02f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glDisable(GL_DEPTH_TEST);
	glMatrixMode(GL_PROJECTION);
	glPushMatrix(); glLoadIdentity();
	glOrtho(0, vw, 0, vh, -1, 1);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix(); glLoadIdentity();

	int pw = 340, ph = 84;
	int px = (vw - pw) / 2, py = (vh - ph) / 2;
	glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glColor4f(0.0f, 0.15f, 0.0f, 0.9f);
	glBegin(GL_QUADS);
	glVertex2i(px, py); glVertex2i(px+pw, py);
	glVertex2i(px+pw, py+ph); glVertex2i(px, py+ph);
	glEnd();
	glColor3f(0.0f, 0.8f, 0.0f);
	glBegin(GL_LINE_LOOP);
	glVertex2i(px, py); glVertex2i(px+pw, py);
	glVertex2i(px+pw, py+ph); glVertex2i(px, py+ph);
	glEnd();
	int bx = px+20, by = py+18, bw = pw-40, bh = 12;
	glColor3f(0.0f, 0.3f, 0.0f);
	glBegin(GL_QUADS);
	glVertex2i(bx, by); glVertex2i(bx+bw, by);
	glVertex2i(bx+bw, by+bh); glVertex2i(bx, by+bh);
	glEnd();
	int fw = bw * pct / 100;
	glColor3f(0.1f, 0.95f, 0.1f);
	glBegin(GL_QUADS);
	glVertex2i(bx, by); glVertex2i(bx+fw, by);
	glVertex2i(bx+fw, by+bh); glVertex2i(bx, by+bh);
	glEnd();
	glDisable(GL_BLEND);

	char msg[160];
	snprintf(msg, sizeof(msg), "%s  -  %d%%  (%s)", title ? title : "", pct, stage ? stage : "");
	glColor3f(0.0f, 0.9f, 0.0f);
	glRasterPos2i(px + 20, py + ph - 18);
	for (const char* c = msg; *c; ++c)
		glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *c);

	glMatrixMode(GL_PROJECTION); glPopMatrix();
	glMatrixMode(GL_MODELVIEW);  glPopMatrix();
	glEnable(GL_DEPTH_TEST);
	glutSwapBuffers();
}

// AI text editor helpers — must be defined before Input_OnMouse and Input_OnSpecial.

// Returns flat text offsets of each visual line start.
// Lines split on '\n'; lines longer than max_chars wrap to the next visual line.
static std::vector<int> AiTextLineStarts(const std::string& txt, int max_chars) {
	std::vector<int> s;
	s.push_back(0);
	for (int i = 0; i < (int)txt.size(); ) {
		if (txt[i] == '\n') { s.push_back(i + 1); i++; }
		else {
			int cnt = 0;
			while (i < (int)txt.size() && txt[i] != '\n' && cnt < max_chars) { i++; cnt++; }
			if (i < (int)txt.size() && txt[i] != '\n') s.push_back(i);
		}
	}
	return s;
}

static int AiScriptMaxChars() {
	return std::max(1, (PropPanel::kWidth - 2 * PropPanel::kPad - 6) / 7);
}

// input
void App::Input_OnMouseWheel(int wheel, int direction, int x, int y) {
	if (show_help_) {
		// Scroll keybindings help panel
		if (direction > 0) { if (help_scroll_offset_ > 0) help_scroll_offset_--; }
		else               { help_scroll_offset_++; }
		return;
	}
	// Over property panel: scroll it
	if (prop_editor_open_ &&
	    x >= PropPanel::kLeft && x <= PropPanel::kLeft + PropPanel::kWidth) {
		const int kScrollStep = PropPanel::kBoxH + 4;
		if (direction > 0) prop_panel_scroll_ = std::max(0, prop_panel_scroll_ - kScrollStep);
		else               prop_panel_scroll_ += kScrollStep;
		return;
	}
	if (show_hud_ && x < 350) { // Over TreeView
		if (direction > 0) {
			if (tree_scroll_offset_ > 0) tree_scroll_offset_--;
		} else {
			tree_scroll_offset_++;
		}
		Logger::Get().Log(LogLevel::INFO, "[App] Tree scroll offset: " + std::to_string(tree_scroll_offset_));
	}
}

void App::Input_OnMouse(int button, int state, int x, int y) {
	bool enableCameraMode = Utils::IsKeyBindingPressed(Config::Get().keyEnableCamera);

	// Update mouse position first so EditorProcessClick uses correct coords
	mouse_state_.prior_x_ = x;
	mouse_state_.prior_y_ = y;

	if (button == GLUT_LEFT_BUTTON) {
		if (GLUT_DOWN == state) {
			mouse_state_.left_button_down_ = true;
			
			if (enableCameraMode) {
				int cx = window_state_.viewport_width_ >> 1;
				int cy = window_state_.viewport_height_ >> 1;
				int targetIdx = selected_object_index_;
				if (targetIdx < 0) targetIdx = hover_object_index_;
				if (targetIdx < 0) targetIdx = PickObjectAtScreenPos(cx, cy);
				if (targetIdx >= Renderer::kAttaPickBase) targetIdx = -1; // ATTA: no orbit target

				if (targetIdx >= 0) {
					const auto& obj = level_.GetLevelObjects().GetObjects()[targetIdx];
					orbit_active_ = true;
					orbit_target_pos_ = glm::vec3(obj.pos);
					orbit_distance_ = glm::distance(viewer_.pos_, orbit_target_pos_);
					if (orbit_distance_ < 0.1f) orbit_distance_ = 1.0f;
					Logger::Get().Log(LogLevel::INFO, "[App] Orbit mode activated around object: " + obj.type);
					Logger::Get().Log(LogLevel::WARNING, "[App] Orbit target pos: " + std::to_string(orbit_target_pos_.x) + ", " + std::to_string(orbit_target_pos_.y) + ", " + std::to_string(orbit_target_pos_.z) + " | Dist: " + std::to_string(orbit_distance_));
				} else {
					orbit_active_ = false;
				}
				return;
			}
			
			// C2: Property editor click handling (IGI2-style left panel)
			if (prop_editor_open_ && selected_object_index_ >= 0) {
				auto& objects = level_.GetLevelObjects().GetObjects();
				if (selected_object_index_ < (int)objects.size()) {
					LevelObject& obj = objects[selected_object_index_];
					const TaskSchema* scp = GetSchema(obj.type);
					if (scp) {
						const TaskSchema& schema = *scp;
						bool is_ai = ai_model_ids_.count(obj.modelId) > 0;
						// Gather editable child task sections (same order as the renderer).
						std::vector<std::pair<int, const TaskSchema*>> children;
						for (int ci : obj.childrenIndices) {
							if (ci < 0 || ci >= (int)objects.size()) continue;
							if (objects[ci].deleted) continue;
							const TaskSchema* cscp = GetSchema(objects[ci].type);
							if (cscp && !cscp->empty()) children.push_back({ci, cscp});
						}
						PropPanel::Layout L = PropPanel::BuildLayout(schema, is_ai, children);
						// Apply the same vertical scroll the renderer uses so hit-tests align.
						if (prop_panel_scroll_ > 0)
							for (auto& w : L.widgets) { w.y1 -= prop_panel_scroll_; w.y2 -= prop_panel_scroll_; }
						// The property panel is a foreground overlay: ANY click anywhere in
						// the left panel strip is consumed so background 3D objects are never
						// selected/manipulated while the editor is open.
						if (x >= 0 && x <= L.panel_x + L.panel_w) {
							// +4px tolerance on all sides for pixel-perfect feel at any DPI
							auto inRect = [&](const PropPanel::Widget& w) {
								return x >= w.x1 - 4 && x <= w.x2 + 4 && y >= w.y1 - 4 && y <= w.y2 + 4;
							};
							// Commit any in-progress text edit before switching focus.
							CommitPropTextEdit();
							for (const auto& w : L.widgets) {
								if (!inRect(w)) continue;
								using K = PropPanel::WidgetKind;
								if (w.kind == K::ChildHeader) { return; } // separator: not interactive

								// Resolve the widget's target object: the selected parent, or
								// one of its child tasks (w.objIndex). Edits/drags route there so
								// children get the identical interface.
								int tIdx = (w.objIndex >= 0) ? w.objIndex : selected_object_index_;
								if (tIdx < 0 || tIdx >= (int)objects.size()) return;
								LevelObject& tobj = objects[tIdx];
								const TaskSchema* tscp = (tIdx == selected_object_index_) ? scp : GetSchema(tobj.type);
								if (!tscp) return;
								const TaskSchema& tsch = *tscp;
								auto fieldArg = [&](int fidx, int comp) -> int {
									return (fidx >= 0 && fidx < (int)tsch.size()) ? tsch[fidx].argOffset + comp : -1;
								};
								auto tTok = [&](int idx) -> float {
									if (idx >= 0 && idx < (int)tobj.argTokens.size()) {
										try { return std::stof(tobj.argTokens[idx]); } catch(...) {}
									}
									return 0.f;
								};

								if (w.kind == K::NoteBox) {
									prop_edit_obj_index_ = tIdx;
									prop_text_edit_field_ = -2; // sentinel: editing note
									prop_text_buf_ = tobj.name;
									prop_text_caret_ = (int)prop_text_buf_.size();
								} else if (w.kind == K::SnapGround || w.kind == K::SnapObject) {
									// Snap acts on the selected object's marker manipulation.
									if (tIdx == selected_object_index_) {
										int mk = (w.kind == K::SnapGround) ? MK_MANIP_S : MK_MANIP_O;
										PushUndoState();
										input_.keys_ |= mk;
										UpdateMarkerManipulation();
										input_.keys_ &= ~mk;
										mouse_state_.left_button_down_ = false; // prevent drag overwriting snap
									}
								} else if (w.kind == K::StringBox) {
									prop_edit_obj_index_ = tIdx;
									prop_text_edit_field_ = w.fieldIndex * 3 + w.comp;
									int argIdx = fieldArg(w.fieldIndex, w.comp);
									prop_text_buf_ = (argIdx >= 0 && argIdx < (int)tobj.argTokens.size()) ? StripQuotes(tobj.argTokens[argIdx]) : "";
									prop_text_caret_ = (int)prop_text_buf_.size();
								} else if (w.kind == K::NumBox) {
									// Click to type AND arm scrub-drag (motion will scrub).
									prop_edit_obj_index_ = tIdx;
									prop_text_edit_field_ = w.fieldIndex * 3 + w.comp;
									int argIdx = fieldArg(w.fieldIndex, w.comp);
									prop_text_buf_ = (argIdx >= 0 && argIdx < (int)tobj.argTokens.size()) ? tobj.argTokens[argIdx] : "";
									prop_text_caret_ = (int)prop_text_buf_.size();
									prop_drag_obj_index_ = tIdx;
									prop_field_index_  = w.fieldIndex * 3 + w.comp;
									prop_drag_start_x_ = x;
									prop_drag_start_y_ = y;
									PushUndoState();
									prop_drag_start_val_ = tTok(argIdx);
								} else if (w.kind == K::Checkbox) {
									int argIdx = fieldArg(w.fieldIndex, w.comp);
									if (argIdx >= 0 && argIdx < (int)tobj.argTokens.size()) {
										int cur = 0; try { cur = std::stoi(tobj.argTokens[argIdx]); } catch(...) {}
										tobj.argTokens[argIdx] = (cur == 0) ? "1" : "0";
										tobj.modified = true;
										level_.GetLevelObjects().UpdateCoordinatesInLine(tobj);
									}
								} else if (w.kind == K::PosPad) {
									// drag X and Y simultaneously — store field, use comp=0 marker
									prop_drag_obj_index_ = tIdx;
									prop_field_index_  = w.fieldIndex * 3 + 0;
									prop_drag_start_x_ = x;
									prop_drag_start_y_ = y;
									PushUndoState();
									int off = fieldArg(w.fieldIndex, 0);
									prop_drag_start_val_  = tTok(off + 0);
									prop_drag_start_val2_ = tTok(off + 1);
								} else if (w.kind == K::AIScriptPath) {
									prop_edit_obj_index_ = -1;
									prop_text_edit_field_ = PropPanel::kAIScriptPathField;
									prop_text_buf_ = ai_script_path_;
									{
										int cc = std::max(0, (x - w.x1 - 3) / 7);
										prop_text_caret_ = std::min((int)prop_text_buf_.size(),
										                            ai_script_path_hscroll_ + cc);
									}
								} else if (w.kind == K::AIScriptText) {
									prop_edit_obj_index_ = -1;
									prop_text_edit_field_ = PropPanel::kAIScriptTextField;
									prop_text_buf_ = ai_script_text_;
									{
										const int mc = AiScriptMaxChars();
										int click_row = std::max(0, (y - w.y1) / PropPanel::kBoxH);
										int click_col = std::max(0, (x - w.x1 - 3) / 7);
										int abs_line  = ai_script_vscroll_ + click_row;
										auto lstarts  = AiTextLineStarts(ai_script_text_, mc);
										abs_line = std::max(0, std::min(abs_line, (int)lstarts.size() - 1));
										int ls   = lstarts[abs_line];
										int next = (abs_line + 1 < (int)lstarts.size()) ? lstarts[abs_line + 1] : (int)ai_script_text_.size();
										int len  = next - ls;
										if (len > 0 && (ls + len) <= (int)ai_script_text_.size() &&
										    ai_script_text_[ls + len - 1] == '\n') --len;
										prop_text_caret_ = ls + std::min(click_col, len);
									}
								} else {
									// PosZSlider / OriSlider / RgbSlider / NumSlider — single-value drag
									prop_drag_obj_index_ = tIdx;
									prop_field_index_  = w.fieldIndex * 3 + w.comp;
									prop_drag_start_x_ = x;
									prop_drag_start_y_ = y;
									PushUndoState();
									prop_drag_start_val_ = tTok(fieldArg(w.fieldIndex, w.comp));
								}
								return;
							}
								return; // consume any click in the panel strip (foreground overlay)
						}
					}
				}
			}

			if (pause_mode_) {
				// *** MUST match renderer.cpp pause menu constants exactly ***
				const int menu_w = 460;
				const int menu_h = 480;
				const int menu_x = (window_state_.viewport_width_  - menu_w) / 2;
				const int screen_menu_top = (window_state_.viewport_height_ - menu_h) / 2;

				auto btn_hit = [&](int idx, int mouse_y) -> bool {
					int btn_y = screen_menu_top + 85 + idx * 35;
					return (mouse_y >= btn_y - 15 && mouse_y <= btn_y + 15);
				};

				if (x >= menu_x && x <= menu_x + menu_w &&
				    y >= screen_menu_top && y <= screen_menu_top + menu_h) {
					mouse_state_.left_button_down_ = false;
					// Deselect text boxes by default if we click anywhere in menu
					int clicked_input = -1;

					int btn_idx = 0;
					int RESUME_ROW = btn_idx++;
					int FONT_ROW = btn_idx++;
					int LEVEL_ROW = btn_idx++;
					int SEARCH_ROW = btn_idx++;
					int TERRAIN_HEADER_ROW = btn_idx++;
					int TERRAIN_TEX_ROW = -1, TERRAIN_HGT_ROW = -1, TERRAIN_DSC_ROW = -1;
					if (pause_terrain_expanded_) {
						TERRAIN_TEX_ROW = btn_idx++;
						TERRAIN_HGT_ROW = btn_idx++;
						TERRAIN_DSC_ROW = btn_idx++;
					}
					int RESET_ROW = btn_idx++;
					int SAVE_ROW = btn_idx++;
					int QUIT_ROW = btn_idx++;

					if      (btn_hit(RESUME_ROW, y)) { TogglePauseMenu(); }                    // Resume
					else if (btn_hit(FONT_ROW, y)) {                                         // Font row: toggle + [-] size [+]
						const int sz_box_w = 34, btn_w = 22, gap = 6, label_w = 96, label_gap = 16;
						const int controls_w = btn_w + gap + sz_box_w + gap + btn_w;
						const int group_w = label_w + label_gap + controls_w;
						int gx = menu_x + (menu_w - group_w) / 2;
						int minus_x = gx + label_w + label_gap;
						int box_x   = minus_x + btn_w + gap;
						int plus_x  = box_x + sz_box_w + gap;
						int& fs = Config::Get().systemFontSize;
						if (x >= minus_x && x < minus_x + 22) {
							fs = std::max(8, fs - 1); Config::Save();
						} else if (x >= plus_x && x < plus_x + 22) {
							fs = std::min(32, fs + 1); Config::Save();
						} else if (x < minus_x) {
							Config::Get().useEditorFont = !Config::Get().useEditorFont; Config::Save();
						}
					}
					else if (btn_hit(LEVEL_ROW, y)) { clicked_input = 0; }                    // Select Level Input
					else if (btn_hit(SEARCH_ROW, y)) { clicked_input = 1; }                    // Model Search Input
					else if (btn_hit(TERRAIN_HEADER_ROW, y)) { pause_terrain_expanded_ = !pause_terrain_expanded_; }
					else if (pause_terrain_expanded_ && btn_hit(TERRAIN_TEX_ROW, y)) { ToggleTerrainModOption(1); }            // Texture
					else if (pause_terrain_expanded_ && btn_hit(TERRAIN_HGT_ROW, y)) { ToggleTerrainModOption(2); }            // Height
					else if (pause_terrain_expanded_ && btn_hit(TERRAIN_DSC_ROW, y)) { ToggleTerrainModOption(4); }            // Discard
					else if (btn_hit(RESET_ROW, y)) { ResetLevel(); TogglePauseMenu(); }      // Reset Level
					else if (btn_hit(SAVE_ROW, y)) { SaveCurrentLevel(); }                   // Save Level
					else if (btn_hit(QUIT_ROW,y)) { exit(0); }                              // Quit

					pause_active_input_ = clicked_input;
				} else {
					pause_active_input_ = -1; // Clicked outside menu
				}
				return; // Block all other interactions while paused
			}

			// Priority: on-screen terrain brush palette (bottom-right). Consume click so
			// it does not also sculpt terrain / pick an object this frame.
			if (TerrainPaletteClick(x, y)) {
				mouse_state_.left_button_down_ = false; // stop Frame() from sculpting while held
				return;
			}

			// Task picker mouse click: left-click on an item selects it
			if (task_picker_open_) {
				const int picker_x = 20;
				const int picker_w = 520;
				const int picker_items_top_y = 50; // items start below header (screen coords)
				const int row_h = 16;

				if (x >= picker_x && x <= picker_x + picker_w && y >= picker_items_top_y) {
					int row = (y - picker_items_top_y) / row_h;
					int target_idx = task_picker_scroll_offset_ + row;

					// Build picker list (same logic as Input_OnSpecial picker handling)
					auto& objects = level_.GetLevelObjects().GetObjects();
					std::vector<int> picker_to_objects;
					std::string search_lower = task_picker_search_;
					std::transform(search_lower.begin(), search_lower.end(), search_lower.begin(),
					               [](unsigned char c) { return std::tolower(c); });
					std::set<std::string> seen_types;
					for (int i = 0; i < (int)objects.size(); ++i) {
						if (!objects[i].deleted) {
							const auto& obj = objects[i];
							if (obj.type == "Task_DeclareParameters") continue; // picker excludes declare params
							std::string type_lower = obj.type;
							std::transform(type_lower.begin(), type_lower.end(), type_lower.begin(),
							               [](unsigned char c) { return std::tolower(c); });
							if (seen_types.count(type_lower) > 0) continue;
							seen_types.insert(type_lower);
							std::string label = obj.type + "()";
							std::string label_lower = label;
							std::transform(label_lower.begin(), label_lower.end(), label_lower.begin(),
							               [](unsigned char c) { return std::tolower(c); });
							if (search_lower.empty() || label_lower.find(search_lower) != std::string::npos)
								picker_to_objects.push_back(i);
						}
					}

					if (target_idx >= 0 && target_idx < (int)picker_to_objects.size()) {
						task_picker_selected_idx_ = target_idx;
					}
					mouse_state_.left_button_down_ = false;
					return;
				}
			}

			// Priority: TreeView HUD interaction
			if (show_hud_ && x < 350 && !enableCameraMode) { // Tree is on the left
				ProcessTreeViewClick(x, y);

			}
			else if (edit_mode_ && !enableCameraMode) {
				if (terrain_edit_enabled_) {
					int picked = PickObjectAtScreenPos(x, y);
					if (picked >= 0) {
						SetTerrainEditEnabled(false);
					}
				}
				EditorProcessClick(); 
				// Update manipulation data AFTER selection, so we get the newly selected object
				marker_manip_.start_x_ = x;
				marker_manip_.start_y_ = y;
				if (selected_object_index_ >= 0) {
					auto& obj = level_.GetLevelObjects().GetObjects()[selected_object_index_];
					marker_manip_.start_pos_ = obj.pos;
					marker_manip_.start_rot_ = obj.rot;
				}
			}
		}
		else if (GLUT_UP == state) {
			mouse_state_.left_button_down_ = false;
			edit_dragging_ = false;
			orbit_active_ = false;
			prop_field_index_ = -1; // C2: stop dragging property field
			prop_drag_obj_index_ = -1;
			prop_drag_speed_ = 0.f;
			prop_last_drag_dx_ = 0;
			prop_last_drag_dy_ = 0;
			status_message_.clear(); // Clear movement telemetry status when mouse is released

			if (window_state_.cursor_visible_) {
				input_.mouse_delta_x_ = 0;
				input_.mouse_delta_y_ = 0;
			}
		}
	}

	// Right-click: open property editor for the object under cursor
	if (button == GLUT_RIGHT_BUTTON && state == GLUT_DOWN && !pause_mode_ && !enableCameraMode) {
		int target = hover_object_index_;
		if (target >= 0) {
			if (terrain_edit_enabled_) {
				SetTerrainEditEnabled(false);
			}
			selected_object_index_ = target;
			prop_editor_open_ = true; prop_panel_scroll_ = 0; prop_text_edit_field_ = -1; prop_edit_obj_index_ = -1;
			LoadAIScriptForSelected();
		} else {
			prop_editor_open_ = false;
			// Right Click on empty space toggles terrain editor
			if (!terrain_edit_enabled_) {
				SetTerrainEditEnabled(true);
			}
		}
	}

	// Update cursor instantly on click/release — keep NONE if SPR cursor is active
	if (window_state_.cursor_visible_ && !pause_mode_) {
		glutSetCursor(GLUT_CURSOR_NONE);
	}
}

void App::Input_OnMotion(int x, int y) {
	int dx = x - mouse_state_.prior_x_;
	int dy = y - mouse_state_.prior_y_;

	bool enableCameraMode = Utils::IsKeyBindingPressed(Config::Get().keyEnableCamera);
	if (enableCameraMode && (dx != 0 || dy != 0))
		camera_mode_moved_ = true;

	// Always update mouse coordinates for hover tooltip/interaction
	mouse_state_.prior_x_ = x;
	mouse_state_.prior_y_ = y;

	// Priority 1: TreeView Hover
	if (show_hud_ && x < 350 && !enableCameraMode) {
		hover_tree_index_ = -1; // Reset before check
		ProcessTreeViewHover(x, y);
		hover_object_index_ = -1; // Suppress 3D tooltips while over UI
	} else {
		hover_tree_index_ = -1;
		// Priority 2: 3D Object Hover — deferred to Frame() to avoid blocking GPU sync on every mouse event
	}

	if (window_state_.cursor_visible_) {
		glutSetCursor(GLUT_CURSOR_NONE);
	}

	if (window_state_.cursor_visible_ && !enableCameraMode) {
		if (mouse_state_.left_button_down_) {
			input_.mouse_delta_x_ = dx;
			input_.mouse_delta_y_ = dy;

			if (edit_mode_ && selected_object_index_ >= 0 && !terrain_edit_enabled_) {
				UpdateMarkerManipulation();
			}
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

	if (edit_dragging_) {
		int box_x = (window_state_.viewport_width_ - edit_box_w_) / 2;
		int rel_x = x - (box_x + 20);
		int char_w = 9;
		edit_cursor_pos_ = std::max(0, std::min((int)edit_string_.size(), edit_scroll_x_ + (rel_x / char_w)));
		edit_selection_end_ = edit_cursor_pos_;
	}

	// C2: Property editor drag (2D pad / Z slider / orientation+numeric sliders)
	if (prop_field_index_ >= 0 && !enableCameraMode && selected_object_index_ >= 0) {
		auto& objects = level_.GetLevelObjects().GetObjects();
		// The drag may target the selected parent or one of its child tasks.
		int dragIdx = (prop_drag_obj_index_ >= 0) ? prop_drag_obj_index_ : selected_object_index_;
		if (dragIdx >= 0 && dragIdx < (int)objects.size()) {
			auto& obj = objects[dragIdx];
			const TaskSchema* scp = GetSchema(obj.type);
			if (scp) {
				const TaskSchema& schema = *scp;
				int fi   = prop_field_index_ / 3;
				int comp = prop_field_index_ % 3;
				// A real drag cancels any pending NumBox text-edit (scrub wins).
				if ((std::abs(x - prop_drag_start_x_) > 2 || std::abs(y - prop_drag_start_y_) > 2) &&
				    prop_text_edit_field_ == prop_field_index_) {
					prop_text_edit_field_ = -1;
				}
				if (fi < (int)schema.size()) {
					const FieldDef& fd = schema[fi];
					const std::string& tn = fd.typeName;
					bool is_pos = (tn == "ObjectPos"); // only actual position type gets pad/camera-follow
					bool is_ori = (tn == "Real32x9");
						int dxp = x - prop_drag_start_x_;

						if (is_pos) {
							// Position pad / Z slider use a per-frame velocity model
							// (ApplyPropPositionDrag) so the object accelerates while held in a
							// direction and keeps moving even when the cursor is pinned at the
							// window edge. Raw motion events do nothing for position here.
						} else {
						glm::dvec3 oldPos = obj.pos;
						glm::dvec3 oldRot = obj.rot;
						// Orientation / RGB / numeric horizontal slider or NumBox scrub.
						bool isRgb = (tn == "RGB" || tn == "Colour");
						bool isInt = (tn == "Int16" || tn == "Int32" || tn == "EnumInt32");
						float sens = is_ori ? 0.01f : (isRgb ? 0.005f : (isInt ? 0.1f : 0.05f));
						float nv = prop_drag_start_val_ + dxp * sens;
						if (isRgb) nv = std::max(0.f, std::min(1.f, nv));
						int argIdx = fd.argOffset + comp;
						if (argIdx >= 0 && argIdx < (int)obj.argTokens.size()) {
							char buf[64];
							if (isInt) snprintf(buf, sizeof(buf), "%d", (int)std::lround(nv));
							else       snprintf(buf, sizeof(buf), "%.6f", nv);
							obj.argTokens[argIdx] = buf;
						}
						if (is_ori) { // mirror to obj.rot (Alpha/Beta/Gamma -> x/y/z)
							if      (comp == 0) obj.rot.x = nv;
							else if (comp == 1) obj.rot.y = nv;
							else                obj.rot.z = nv;
						}
						// Sync single-rotation Real32 fields (Gamma/Heading) to obj.rot.z so
						// UpdateCoordinatesInLine doesn't overwrite argTokens with stale obj.rot.
						// Gamma/Heading are stored as Angle/Degrees/Real32 — any of these
						// must drive obj.rot.z so the 3D object actually turns when dragged.
						bool isSingleRot = ((tn == "Real32" || tn == "Angle" || tn == "Degrees") &&
						                    (fd.name == "Gamma" || fd.name == "Heading"));
						if (isSingleRot) obj.rot.z = (double)nv;
						obj.modified = true;
						if (is_ori) {
							glm::dmat3 deltaWorld = BuildRotMatZXY(obj.rot) * glm::transpose(BuildRotMatZXY(oldRot));
							PropagateTransformToChildren(dragIdx, glm::dvec3(0), deltaWorld, oldPos);
						}
						level_.GetLevelObjects().UpdateCoordinatesInLine(obj);
					}
				}
			}
		}
	}
}

// Per-frame velocity-ramped position drag for the property editor's 2D pad and Z
// slider. Called once per frame while the field is held. The object accelerates
// the longer the cursor is held away from the drag-start point (mirrors the
// ALT+SHIFT camera boost) and keeps moving even when the cursor is pinned at the
// window edge. The camera is intentionally NOT moved, so the object visibly
// travels through the world.
void App::ApplyPropPositionDrag() {
	if (prop_field_index_ < 0) return;
	auto& objects = level_.GetLevelObjects().GetObjects();
	int dragIdx = (prop_drag_obj_index_ >= 0) ? prop_drag_obj_index_ : selected_object_index_;
	if (dragIdx < 0 || dragIdx >= (int)objects.size()) return;
	auto& obj = objects[dragIdx];
	const TaskSchema* scp = GetSchema(obj.type);
	if (!scp) return;
	const TaskSchema& schema = *scp;
	int fi = prop_field_index_ / 3;
	int comp = prop_field_index_ % 3;
	if (fi >= (int)schema.size()) return;
	const FieldDef& fd = schema[fi];
	if (fd.typeName != "ObjectPos") return;   // only the pad / Z slider use this model

	int dispx = mouse_state_.prior_x_ - prop_drag_start_x_;
	int dispy = mouse_state_.prior_y_ - prop_drag_start_y_;
	const int   kDead   = 5;        // cursor dead zone around the drag start
	const float kBase   = 25.0f;    // units/frame just past the dead zone
	const float kGrowth = 1.05f;    // per-frame acceleration multiplier
	const float kMax    = 8000.0f;  // speed cap (units/frame)

	auto curArg = [&](int idx) -> double {
		return (idx >= 0 && idx < (int)obj.argTokens.size()) ? std::atof(obj.argTokens[idx].c_str()) : 0.0;
	};
	auto writeArg = [&](int idx, double v) {
		if (idx >= 0 && idx < (int)obj.argTokens.size()) {
			char buf[64]; snprintf(buf, sizeof(buf), "%.6f", v);
			obj.argTokens[idx] = buf;
		}
	};

	glm::dvec3 oldPos = obj.pos;
	bool moved = false;

	if (comp == 0) {            // 2D pad → X / Y
		float len = std::sqrt((float)(dispx * dispx + dispy * dispy));
		if (len < (float)kDead) { prop_drag_speed_ = 0.f; return; }
		prop_drag_speed_ = (prop_drag_speed_ < kBase) ? kBase : std::min(kMax, prop_drag_speed_ * kGrowth);
		float ux = dispx / len, uy = dispy / len;
		double nx = curArg(fd.argOffset + 0) + ux * prop_drag_speed_;
		double ny = curArg(fd.argOffset + 1) - uy * prop_drag_speed_;  // screen-y down → world-y up
		writeArg(fd.argOffset + 0, nx);
		writeArg(fd.argOffset + 1, ny);
		obj.pos.x = nx; obj.pos.y = ny;
		moved = true;
	} else if (comp == 1) {     // Y numeric box → Y only
		if (std::abs(dispy) < kDead) { prop_drag_speed_ = 0.f; return; }
		prop_drag_speed_ = (prop_drag_speed_ < kBase) ? kBase : std::min(kMax, prop_drag_speed_ * kGrowth);
		float uy = (dispy < 0) ? 1.f : -1.f;   // drag up → +Y
		double ny = curArg(fd.argOffset + 1) + uy * prop_drag_speed_;
		writeArg(fd.argOffset + 1, ny);
		obj.pos.y = ny;
		moved = true;
	} else if (comp == 2) {     // Z slider
		if (std::abs(dispy) < kDead) { prop_drag_speed_ = 0.f; return; }
		prop_drag_speed_ = (prop_drag_speed_ < kBase) ? kBase : std::min(kMax, prop_drag_speed_ * kGrowth);
		float uy = (dispy < 0) ? 1.f : -1.f;   // drag up → +Z
		double nz = curArg(fd.argOffset + 2) + uy * prop_drag_speed_;
		writeArg(fd.argOffset + 2, nz);
		obj.pos.z = nz;
		moved = true;
	}

	if (moved) {
		obj.modified = true;
		glm::dvec3 deltaPos = obj.pos - oldPos;
		PropagateTransformToChildren(dragIdx, deltaPos, glm::dmat3(1.0), oldPos);
		level_.GetLevelObjects().UpdateCoordinatesInLine(obj);
		// Camera follows the object so it stays in view as it accelerates/travels.
		viewer_.pos_ += glm::vec3(deltaPos);
	}
}

void App::Input_OnSpecial(int key, int x, int y) {
	auto& config = Config::Get();

	// Autocomplete task picker navigation
	if (ac_task_picker_open_) {
		// Build filtered list size
		std::vector<std::string> filtered;
		std::string fl = ac_task_filter_;
		std::transform(fl.begin(), fl.end(), fl.begin(), [](unsigned char c){ return std::tolower(c); });
		for (const auto& item : ac_task_items_) {
			if (fl.empty()) { filtered.push_back(item); }
			else {
				std::string il = item;
				std::transform(il.begin(), il.end(), il.begin(), [](unsigned char c){ return std::tolower(c); });
				if (il.find(fl) != std::string::npos) filtered.push_back(item);
			}
		}
		int count = (int)filtered.size();
		if (count > 0) {
			if (key == GLUT_KEY_UP)        ac_task_selected_idx_ = std::max(0, ac_task_selected_idx_ - 1);
			else if (key == GLUT_KEY_DOWN) ac_task_selected_idx_ = std::min(count - 1, ac_task_selected_idx_ + 1);
			else if (key == GLUT_KEY_PAGE_UP)   ac_task_selected_idx_ = std::max(0, ac_task_selected_idx_ - 10);
			else if (key == GLUT_KEY_PAGE_DOWN) ac_task_selected_idx_ = std::min(count - 1, ac_task_selected_idx_ + 10);
			const int row_h = 16, panel_h = window_state_.viewport_height_ - 100;
			const int max_vis = std::max(1, panel_h / row_h);
			if (ac_task_selected_idx_ < ac_task_scroll_offset_)
				ac_task_scroll_offset_ = ac_task_selected_idx_;
			else if (ac_task_selected_idx_ >= ac_task_scroll_offset_ + max_vis)
				ac_task_scroll_offset_ = ac_task_selected_idx_ - max_vis + 1;
		}
		return;
	}

	// Model picker navigation
	if (model_picker_open_) {
		std::vector<std::string> filtered;
		std::string fl = model_picker_filter_;
		std::transform(fl.begin(), fl.end(), fl.begin(), [](unsigned char c){ return std::tolower(c); });
		for (const auto& id : level_model_ids_) {
			if (fl.empty()) { filtered.push_back(id); }
			else {
				std::string idl = id;
				std::transform(idl.begin(), idl.end(), idl.begin(), [](unsigned char c){ return std::tolower(c); });
				if (idl.find(fl) != std::string::npos) filtered.push_back(id);
			}
		}
		int count = (int)filtered.size();
		if (count > 0) {
			if (key == GLUT_KEY_UP)        model_picker_selected_ = std::max(0, model_picker_selected_ - 1);
			else if (key == GLUT_KEY_DOWN) model_picker_selected_ = std::min(count - 1, model_picker_selected_ + 1);
			else if (key == GLUT_KEY_PAGE_UP)   model_picker_selected_ = std::max(0, model_picker_selected_ - 10);
			else if (key == GLUT_KEY_PAGE_DOWN) model_picker_selected_ = std::min(count - 1, model_picker_selected_ + 10);
			const int row_h = 16, panel_h = window_state_.viewport_height_ - 100;
			const int max_vis = std::max(1, panel_h / row_h);
			if (model_picker_selected_ < model_picker_scroll_)
				model_picker_scroll_ = model_picker_selected_;
			else if (model_picker_selected_ >= model_picker_scroll_ + max_vis)
				model_picker_scroll_ = model_picker_selected_ - max_vis + 1;
		}
		return;
	}

	// Arrow key navigation inside AI script / path text boxes
	if (prop_text_edit_field_ == PropPanel::kAIScriptTextField ||
	    prop_text_edit_field_ == PropPanel::kAIScriptPathField) {
		bool isScript = (prop_text_edit_field_ == PropPanel::kAIScriptTextField);
		const int mc = AiScriptMaxChars();

		if (key == GLUT_KEY_LEFT) {
			if (prop_text_caret_ > 0) --prop_text_caret_;
			if (isScript) UpdateAIScriptScroll(); else UpdateAIScriptPathHScroll();
			return;
		}
		if (key == GLUT_KEY_RIGHT) {
			if (prop_text_caret_ < (int)prop_text_buf_.size()) ++prop_text_caret_;
			if (isScript) UpdateAIScriptScroll(); else UpdateAIScriptPathHScroll();
			return;
		}
		if (isScript) {
			if (key == GLUT_KEY_UP || key == GLUT_KEY_DOWN) {
				auto starts = AiTextLineStarts(prop_text_buf_, mc);
				int cl = (int)(std::upper_bound(starts.begin(), starts.end(), prop_text_caret_) - starts.begin()) - 1;
				cl = std::max(0, std::min(cl, (int)starts.size() - 1));
				int col = prop_text_caret_ - starts[cl];
				int nl = cl + (key == GLUT_KEY_DOWN ? 1 : -1);
				nl = std::max(0, std::min(nl, (int)starts.size() - 1));
				int next_end = (nl + 1 < (int)starts.size()) ? starts[nl + 1] : (int)prop_text_buf_.size();
				int line_len = next_end - starts[nl];
				if (line_len > 0 && (starts[nl] + line_len) <= (int)prop_text_buf_.size() &&
				    prop_text_buf_[starts[nl] + line_len - 1] == '\n') --line_len;
				prop_text_caret_ = starts[nl] + std::min(col, line_len);
				UpdateAIScriptScroll();
				return;
			}
			if (key == GLUT_KEY_PAGE_UP) {
				ai_script_vscroll_ = std::max(0, ai_script_vscroll_ - 12);
				return;
			}
			if (key == GLUT_KEY_PAGE_DOWN) {
				auto starts = AiTextLineStarts(prop_text_buf_, mc);
				ai_script_vscroll_ = std::min(std::max(0, (int)starts.size() - 12),
				                              ai_script_vscroll_ + 12);
				return;
			}
		}
	}

	if (task_picker_open_) {
		auto& objects = level_.GetLevelObjects().GetObjects();
		std::vector<int> picker_to_objects;
		
		std::string search_lower = task_picker_search_;
		std::transform(search_lower.begin(), search_lower.end(), search_lower.begin(), [](unsigned char c) { return std::tolower(c); });
		
		std::set<std::string> seen_types;
		for (int i = 0; i < (int)objects.size(); ++i) {
			if (!objects[i].deleted) {
				const auto& obj = objects[i];
				if (obj.type == "Task_DeclareParameters") continue; // picker excludes declare params
				std::string type_lower = obj.type;
				std::transform(type_lower.begin(), type_lower.end(), type_lower.begin(),
				               [](unsigned char c) { return std::tolower(c); });
				if (seen_types.count(type_lower) > 0) continue;
				seen_types.insert(type_lower);
				std::string label = obj.type + "()";

				std::string label_lower = label;
				std::transform(label_lower.begin(), label_lower.end(), label_lower.begin(), [](unsigned char c) { return std::tolower(c); });

				if (search_lower.empty() || label_lower.find(search_lower) != std::string::npos) {
					picker_to_objects.push_back(i);
				}
			}
		}

		if (!picker_to_objects.empty()) {
			int count = (int)picker_to_objects.size();
			if (key == GLUT_KEY_UP) {
				if (task_picker_selected_idx_ > 0) {
					task_picker_selected_idx_--;
				} else {
					task_picker_selected_idx_ = count - 1;
				}
			}
			else if (key == GLUT_KEY_DOWN) {
				if (task_picker_selected_idx_ < count - 1) {
					task_picker_selected_idx_++;
				} else {
					task_picker_selected_idx_ = 0;
				}
			}
			else if (key == GLUT_KEY_PAGE_UP) {
				task_picker_selected_idx_ = std::max(0, task_picker_selected_idx_ - 10);
			}
			else if (key == GLUT_KEY_PAGE_DOWN) {
				task_picker_selected_idx_ = std::min(count - 1, task_picker_selected_idx_ + 10);
			}
			else if (key == GLUT_KEY_HOME) {
				task_picker_selected_idx_ = 0;
			}
			else if (key == GLUT_KEY_END) {
				task_picker_selected_idx_ = count - 1;
			}

			int row_h = 16;
			int picker_h = window_state_.viewport_height_ - 100;
			int max_visible = std::max(1, picker_h / row_h);
			if (task_picker_selected_idx_ < task_picker_scroll_offset_) {
				task_picker_scroll_offset_ = task_picker_selected_idx_;
			}
			else if (task_picker_selected_idx_ >= task_picker_scroll_offset_ + max_visible) {
				task_picker_scroll_offset_ = task_picker_selected_idx_ - max_visible + 1;
			}
		}
		return;
	}

	// C2: caret movement while a property text/numeric box is being edited.
	if (prop_text_edit_field_ != -1) {
		int n = (int)prop_text_buf_.size();
		if (prop_text_caret_ < 0) prop_text_caret_ = 0;
		if (prop_text_caret_ > n) prop_text_caret_ = n;
		if (key == GLUT_KEY_LEFT)  prop_text_caret_ = std::max(0, prop_text_caret_ - 1);
		if (key == GLUT_KEY_RIGHT) prop_text_caret_ = std::min(n, prop_text_caret_ + 1);
		if (key == GLUT_KEY_HOME)  prop_text_caret_ = 0;
		if (key == GLUT_KEY_END)   prop_text_caret_ = n;
		return;
	}

	if (show_hud_ && window_state_.cursor_visible_) {
		if (key == GLUT_KEY_UP || key == GLUT_KEY_DOWN || key == GLUT_KEY_LEFT || key == GLUT_KEY_RIGHT) {
			auto visibleList = GetVisibleTreeNodes();
			if (!visibleList.empty()) {
				int current_row = -1;
				for (int i = 0; i < (int)visibleList.size(); ++i) {
					if (visibleList[i] == selected_object_index_) {
						current_row = i;
						break;
					}
				}

				if (key == GLUT_KEY_UP) {
					if (current_row > 0) {
						selected_object_index_ = visibleList[current_row - 1];
						current_row--;
					} else {
						selected_object_index_ = visibleList.back();
						current_row = (int)visibleList.size() - 1;
					}
				}
				else if (key == GLUT_KEY_DOWN) {
					if (current_row >= 0 && current_row < (int)visibleList.size() - 1) {
						selected_object_index_ = visibleList[current_row + 1];
						current_row++;
					} else {
						selected_object_index_ = visibleList.front();
						current_row = 0;
					}
				}
				else if (key == GLUT_KEY_LEFT) {
					if (selected_object_index_ == -2) {
						tree_decl_expanded_ = false;
						Logger::Get().Log(LogLevel::INFO, "[App] Collapsed Mission Declarations");
					} else if (selected_object_index_ >= 0) {
						auto& obj = level_.GetLevelObjects().GetObjects()[selected_object_index_];
						if (obj.isContainer && obj.expanded) {
							auto& nonConstObj = const_cast<LevelObject&>(obj);
							nonConstObj.expanded = false;
							Logger::Get().Log(LogLevel::INFO, "[App] Collapsed: " + obj.type);
						} else if (obj.parentIndex != -1) {
							selected_object_index_ = obj.parentIndex;
							for (int i = 0; i < (int)visibleList.size(); ++i) {
								if (visibleList[i] == selected_object_index_) {
									current_row = i;
									break;
								}
							}
						}
					}
				}
				else if (key == GLUT_KEY_RIGHT) {
					if (selected_object_index_ == -2) {
						tree_decl_expanded_ = true;
						Logger::Get().Log(LogLevel::INFO, "[App] Expanded Mission Declarations");
					} else if (selected_object_index_ >= 0) {
						auto& obj = level_.GetLevelObjects().GetObjects()[selected_object_index_];
						if (obj.isContainer) {
							if (!obj.expanded) {
								auto& nonConstObj = const_cast<LevelObject&>(obj);
								nonConstObj.expanded = true;
								Logger::Get().Log(LogLevel::INFO, "[App] Expanded: " + obj.type);
							} else if (!obj.childrenIndices.empty()) {
								int firstChild = -1;
								for (int childIdx : obj.childrenIndices) {
									if (childIdx >= 0 && childIdx < (int)level_.GetLevelObjects().GetObjects().size()) {
										if (!level_.GetLevelObjects().GetObjects()[childIdx].deleted) {
											firstChild = childIdx;
											break;
										}
									}
								}
								if (firstChild != -1) {
									selected_object_index_ = firstChild;
									auto newVisibleList = GetVisibleTreeNodes();
									for (int i = 0; i < (int)newVisibleList.size(); ++i) {
										if (newVisibleList[i] == selected_object_index_) {
											current_row = i;
											break;
										}
									}
								}
							}
						}
					}
				}

				// Auto scroll to keep selected item visible
				int row_h = 16;
				int start_y = 30;
				int max_rows = (window_state_.viewport_height_ - 50 - start_y) / row_h;
				if (max_rows > 0) {
					if (current_row < tree_scroll_offset_) {
						tree_scroll_offset_ = current_row;
					} else if (current_row >= tree_scroll_offset_ + max_rows) {
						tree_scroll_offset_ = current_row - max_rows + 1;
					}
				}
			}
			return;
		}
	}

	// F2 always toggles TaskTree — checked before any config key overrides
	if (key == GLUT_KEY_F2) {
		show_hud_ = !show_hud_;
		bridge_.SetEnabled(show_hud_);
		Logger::Get().Log(LogLevel::INFO, std::string("[App] TaskTree ") + (show_hud_ ? "shown" : "hidden"));
		return;
	}

	// All other F-key/special-key bindings go through DispatchEventBindings (qedkeybindings.qsc only)

	if (key == config.keyMoveForward) {
		input_.keys_ |= MK_FORWARD;
		return;
	}

	if (key == config.keyMoveBackward) {
		input_.keys_ |= MK_BACKWARD;
		return;
	}

	if (key == config.keyMoveLeft) {
		input_.keys_ |= MK_LEFT;
		return;
	}

	if (key == config.keyMoveRight) {
		input_.keys_ |= MK_RIGHT;
		return;
	}

	if (key == GLUT_KEY_PAGE_UP) {
		// Shift/Ctrl+PageUp = task tree operations → fall through to DispatchEventBindings
		if (!(glutGetModifiers() & (GLUT_ACTIVE_SHIFT | GLUT_ACTIVE_CTRL))) {
			viewer_.jump_speed_ *= 2.0f;
			if (viewer_.jump_speed_ > MAX_JUMP_SPEED) viewer_.jump_speed_ = MAX_JUMP_SPEED;
			printf("current jump speed set to %d\n", (int)viewer_.jump_speed_);
			return;
		}
	}

	if (key == GLUT_KEY_PAGE_DOWN) {
		// Shift/Ctrl+PageDown = task tree operations → fall through to DispatchEventBindings
		if (!(glutGetModifiers() & (GLUT_ACTIVE_SHIFT | GLUT_ACTIVE_CTRL))) {
			viewer_.jump_speed_ *= 0.5f;
			if (viewer_.jump_speed_ < MIN_JUMP_SPEED) viewer_.jump_speed_ = MIN_JUMP_SPEED;
			printf("current jump speed set to %d\n", (int)viewer_.jump_speed_);
			return;
		}
	}

	if (key == GLUT_KEY_LEFT) {
		input_.keys_ |= MK_ROLL_DEC;
		return;
	}

	if (key == GLUT_KEY_RIGHT) {
		input_.keys_ |= MK_ROLL_INC;
		return;
	}

	if (key == GLUT_KEY_F11) {
		if (selected_object_index_ >= 0) {
			auto& obj = level_.GetLevelObjects().GetObjects()[selected_object_index_];
			viewer_.pos_ = glm::vec3(obj.pos);
			UpdateViewerVectors();
			printf("Teleported to Object [%d]\n", selected_object_index_);
		}
		return;
	}

	DispatchEventBindings();
}

void App::Input_OnSpecialUp(int key, int x, int y) {
	auto& config = Config::Get();

	if (key == config.keyMoveForward) {
		input_.keys_ &= ~MK_FORWARD;
		return;
	}

	if (key == config.keyMoveBackward) {
		input_.keys_ &= ~MK_BACKWARD;
		return;
	}

	if (key == config.keyMoveLeft) {
		input_.keys_ &= ~MK_LEFT;
		return;
	}

	if (key == config.keyMoveRight) {
		input_.keys_ &= ~MK_RIGHT;
		return;
	}

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
	'q', 'Q', MK_STRAIGHT_UP,
	'z', 'Z', MK_STRAIGHT_DOWN
};

// Manip keys are now handled directly in Input_OnKeyboard using config values

// Inline autocomplete: complete the word left of the caret from autocomplete_keywords_.
// Repeated calls on the just-completed token cycle through ALL matches, so every keyword
// sharing a prefix is reachable. Triggered by Ctrl+Space AND Tab (Tab is reliably delivered
// by GLUT; Ctrl+Space is sometimes dropped). Returns true if it completed a token.
bool App::InlineAutocomplete() {
	Logger::Get().Log(LogLevel::INFO, "[Autocomplete] triggered: field=" +
		std::to_string(prop_text_edit_field_) + " keywords=" + std::to_string(autocomplete_keywords_.size()) +
		" buf='" + prop_text_buf_ + "' caret=" + std::to_string(prop_text_caret_));
	if (prop_text_edit_field_ == -1) {
		status_message_ = "Autocomplete: click a text box first";
		return false;
	}
	if (autocomplete_keywords_.empty()) {
		status_message_ = "Autocomplete: no keywords loaded (content/tools/AutoCompleteKeywords.txt)";
		return false;
	}
	if (prop_text_caret_ < 0) prop_text_caret_ = 0;
	if (prop_text_caret_ > (int)prop_text_buf_.size()) prop_text_caret_ = (int)prop_text_buf_.size();

	// Token immediately left of the caret (alphanumerics + underscore).
	int ws = prop_text_caret_;
	while (ws > 0 && (isalnum((unsigned char)prop_text_buf_[ws-1]) || prop_text_buf_[ws-1] == '_')) ws--;
	std::string word = prop_text_buf_.substr(ws, prop_text_caret_ - ws);
	if (word.empty()) {
		status_message_ = "Autocomplete: type a prefix first";
		return false;
	}

	auto lower = [](std::string s) {
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
		return s;
	};

	// Continuation = same token position and the token equals the keyword we just inserted;
	// then we cycle within the ORIGINAL prefix's match set instead of restarting from it.
	bool cycling = (ws == ac_inline_start_ && !ac_inline_last_kw_.empty() &&
	                !ac_inline_prefix_.empty() && lower(word) == lower(ac_inline_last_kw_));
	std::string activePrefix = cycling ? ac_inline_prefix_ : word;
	std::string ap = lower(activePrefix);

	std::vector<const std::string*> matches;
	for (const auto& kw : autocomplete_keywords_) {
		std::string kwl = lower(kw);
		if (kwl.size() >= ap.size() && kwl.compare(0, ap.size(), ap) == 0)
			matches.push_back(&kw);
	}
	if (matches.empty()) {
		status_message_ = "No keyword starts with '" + activePrefix + "'";
		ac_inline_start_ = -1; ac_inline_idx_ = -1; ac_inline_last_kw_.clear();
		return false;
	}

	int idx = cycling ? ((ac_inline_idx_ + 1) % (int)matches.size()) : 0;
	const std::string& chosen = *matches[idx];
	prop_text_buf_.replace(ws, word.size(), chosen);
	prop_text_caret_ = ws + (int)chosen.size();

	ac_inline_prefix_  = activePrefix;
	ac_inline_start_   = ws;
	ac_inline_idx_     = idx;
	ac_inline_last_kw_ = chosen;

	if (matches.size() > 1)
		status_message_ = "Autocompleted (" + std::to_string(idx + 1) + "/" +
		                  std::to_string(matches.size()) + "): " + chosen + "  [Tab/Ctrl+Space to cycle]";
	else
		status_message_ = "Autocompleted: " + chosen;

	if (prop_text_edit_field_ == PropPanel::kAIScriptTextField) {
		ai_script_text_  = prop_text_buf_;
		ai_script_dirty_ = true;
		UpdateAIScriptScroll();
	}
	return true;
}

void App::Input_OnKeyboard(unsigned char key, int x, int y) {
	auto& config = Config::Get();

	bool ctrlDown = (glutGetModifiers() & GLUT_ACTIVE_CTRL) != 0 ||
	                (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
	bool shiftDown = (glutGetModifiers() & GLUT_ACTIVE_SHIFT) != 0 ||
	                 (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

	if (ctrlDown && (key == 8 || key == 'h' || key == 'H')) { // CTRL+H
		ToggleOverlayWireframe();
		return;
	}

	if (pause_mode_) {
		if (pause_active_input_ == 0 || pause_active_input_ == 1) {
			std::string& buf = (pause_active_input_ == 0) ? pause_level_input_ : pause_search_input_;
			if (key == 27) { // ESC: clear focus
				pause_active_input_ = -1;
				return;
			}
			if (key == 13) { // Enter: submit
				if (pause_active_input_ == 0) {
					// Level input
					if (!buf.empty()) {
						int lvl = std::atoi(buf.c_str());
						if (lvl >= 1 && lvl <= 14) {
							LoadLevel(lvl);
							TogglePauseMenu(); // Close pause menu on load
						} else {
							Logger::Get().Log(LogLevel::ERR, "Level must be between 1 and 14.");
						}
					}
				} else if (pause_active_input_ == 1) {
					// Model Search input
					if (!buf.empty()) {
						bool isId = true;
						if (buf.length() != 8 || buf[3] != '_' || buf[6] != '_') isId = false;
						for (int i = 0; i < buf.length(); i++) {
							if (i != 3 && i != 6 && !isdigit(buf[i])) isId = false;
						}
						
						if (isId) {
							SearchModelById(buf);
						} else {
							SearchModelByName(buf);
						}
						TogglePauseMenu(); // Close pause menu to show result
					}
				}
				pause_active_input_ = -1;
				return;
			}
			if (key == 8) { // Backspace
				if (!buf.empty()) buf.pop_back();
				return;
			}
			if (key >= 32 && key < 127) {
				buf += (char)key;
				return;
			}
		}
		// If we are in pause mode, we shouldn't process other keys except maybe ESC to unpause (already handled by DispatchEventBindings or explicitly)
		// But don't block ESC so we can close menu.
		if (key != 27) return; 
	}

	// Autocomplete Ctrl combos — intercept before prop text editor so they work while editing.
	// Detect Ctrl via GLUT *and* GetAsyncKeyState: GLUT modifiers are occasionally not
	// reported for Ctrl+Space (key 0/space), which silently dropped inline autocomplete.
	if (ctrlDown) {
		if (key == 14 && !shiftDown) { // Ctrl+N only (not Ctrl+Shift+N — that's TaskFindByTaskNote)
			// Only open when a property text box is focused, so Enter knows which
			// field to insert the chosen item into.
			if (prop_text_edit_field_ == -1) {
				status_message_ = "Click a text box first, then Ctrl+N to pick a task type";
				return;
			}
			picker_target_field_  = prop_text_edit_field_;
			picker_target_obj_    = prop_edit_obj_index_;
			picker_target_caret_  = prop_text_caret_;
			ac_task_items_ = autocomplete_keywords_;
			ac_task_picker_open_ = true; ac_task_selected_idx_ = 0; ac_task_scroll_offset_ = 0; ac_task_filter_.clear();
			return;
		}
		if (key == 15) { // Ctrl+O → Model ID picker (only open when active text box has XXX_XX_X content)
			if (prop_text_edit_field_ == -1) {
				status_message_ = "Click a text box first, then Ctrl+O to pick a model";
				return;
			}
			// Check if current text looks like a model ID (digits and underscores)
			bool looksLikeModelId = true;
			for (char c : prop_text_buf_)
				if (!isdigit(c) && c != '_' && c != ' ') { looksLikeModelId = false; break; }
			if (!looksLikeModelId && !prop_text_buf_.empty()) {
				return; // Only open from model ID text boxes
			}
			picker_target_field_ = prop_text_edit_field_; picker_target_obj_ = prop_edit_obj_index_;
			model_picker_open_ = true; model_picker_selected_ = 0; model_picker_scroll_ = 0;
			model_picker_filter_.clear(); // show ALL model IDs; user types to filter
			return;
		}
		if (key == 0 || key == ' ') { // Ctrl+Space → AutoComplete inline
			InlineAutocomplete();
			return;
		}
	}

	// C2: Property text editor input (caret-based).
	// Skip while a picker or file dialog is open so their keystrokes (filter typing,
	// Enter to insert/confirm) reach their own handlers below instead of being eaten
	// here — otherwise typing edits the box and Enter commits+closes the field.
	if (prop_text_edit_field_ != -1 && !ac_task_picker_open_ && !model_picker_open_ &&
	    file_dialog_mode_ == FileDialogMode::None) {
		int& caret = prop_text_caret_;
		if (caret < 0) caret = 0;
		if (caret > (int)prop_text_buf_.size()) caret = (int)prop_text_buf_.size();
		bool multiline = IsPropFieldMultiline(prop_text_edit_field_);
		if (key == 27) { // ESC — cancel (revert)
			prop_text_edit_field_ = -1;
			return;
		}
		if (key == 13) { // Enter
			if (multiline) {              // VarString/String256: insert newline
				prop_text_buf_.insert(prop_text_buf_.begin() + caret, '\n');
				caret++;
				UpdateAIScriptScroll();
			} else {                      // single-line: commit
				CommitPropTextEdit();
			}
			return;
		}
		if (key == 8) { // Backspace — delete before caret
			if (caret > 0) {
				prop_text_buf_.erase(prop_text_buf_.begin() + (caret - 1));
				caret--;
				UpdateAIScriptScroll();
				UpdateAIScriptPathHScroll();
			}
			return;
		}
		if (key == 127) { // Delete — delete at caret
			if (caret < (int)prop_text_buf_.size())
				prop_text_buf_.erase(prop_text_buf_.begin() + caret);
			UpdateAIScriptScroll();
			return;
		}
		if (key == '\t') { // Tab → inline autocomplete (reliable trigger; GLUT may eat Ctrl+Space)
			InlineAutocomplete();
			return;
		}
		if (key >= 32 && key <= 126) { // printable: insert at caret
			prop_text_buf_.insert(prop_text_buf_.begin() + caret, (char)key);
			caret++;
			UpdateAIScriptScroll();
			UpdateAIScriptPathHScroll();
			return;
		}
		return;
	}

	// File dialog input (SaveSubTask / LoadSubTask)
	if (file_dialog_mode_ != FileDialogMode::None) {
		if (key == 27) { file_dialog_mode_ = FileDialogMode::None; return; }
		if (key == 13) { ConfirmFileDialog(); return; }
		if (key == 8) {
			if (file_dialog_caret_ > 0) {
				file_dialog_path_.erase(file_dialog_path_.begin() + file_dialog_caret_ - 1);
				file_dialog_caret_--;
			}
		} else if (key >= 32 && key <= 126) {
			file_dialog_path_.insert(file_dialog_path_.begin() + file_dialog_caret_, (char)key);
			file_dialog_caret_++;
		}
		return;
	}

	// Autocomplete task picker input (Ctrl+N panel)
	if (ac_task_picker_open_) {
		if (key == 27) { ac_task_picker_open_ = false; return; }
		if (key == 13) {
			// Build filtered list and get selected item
			std::vector<std::string> filtered;
			std::string fl = ac_task_filter_;
			std::transform(fl.begin(), fl.end(), fl.begin(), [](unsigned char c){ return std::tolower(c); });
			for (auto& item : ac_task_items_) {
				if (fl.empty()) { filtered.push_back(item); }
				else {
					std::string il = item; std::transform(il.begin(), il.end(), il.begin(), [](unsigned char c){ return std::tolower(c); });
					if (il.find(fl) != std::string::npos) filtered.push_back(item);
				}
			}
			ac_task_picker_open_ = false;
			// Restore field, obj, and caret position captured when the picker opened.
			if (picker_target_field_ != -1) {
				prop_text_edit_field_ = picker_target_field_;
				prop_edit_obj_index_  = picker_target_obj_;
			}
			if (prop_text_edit_field_ != -1 && ac_task_selected_idx_ < (int)filtered.size()) {
				const std::string& item = filtered[ac_task_selected_idx_];
				bool isAI = (prop_text_edit_field_ == PropPanel::kAIScriptTextField);
				if (isAI) {
					// For AI script: INSERT at saved caret position (don't replace entire script)
					int ins = std::max(0, std::min(picker_target_caret_, (int)prop_text_buf_.size()));
					prop_text_buf_.insert(ins, item);
					prop_text_caret_ = ins + (int)item.size();
					// Commit: store into ai_script_text_ and mark dirty, keep editing
					ai_script_text_  = prop_text_buf_;
					ai_script_dirty_ = true;
					UpdateAIScriptScroll();
				} else {
					prop_text_buf_  = item;       // REPLACE field content (existing behaviour)
					prop_text_caret_ = (int)prop_text_buf_.size();
					CommitPropTextEdit();
				}
			}
			picker_target_field_ = -1; picker_target_obj_ = -1; picker_target_caret_ = -1;
			return;
		}
		if (key == 8) {
			if (!ac_task_filter_.empty()) { ac_task_filter_.pop_back(); ac_task_selected_idx_ = 0; ac_task_scroll_offset_ = 0; }
		} else if (key >= 32 && key <= 126) {
			ac_task_filter_ += (char)key;
			ac_task_selected_idx_ = 0;
			ac_task_scroll_offset_ = 0;
		}
		return;
	}

	// Model picker input (Ctrl+O panel)
	if (model_picker_open_) {
		if (key == 27) { model_picker_open_ = false; return; }
		if (key == 13) {
			// Build filtered list from level_model_ids_
			std::vector<std::string> filtered;
			std::string fl = model_picker_filter_;
			std::transform(fl.begin(), fl.end(), fl.begin(), [](unsigned char c){ return std::tolower(c); });
			for (const auto& id : level_model_ids_) {
				if (fl.empty()) { filtered.push_back(id); }
				else {
					std::string idl = id;
					std::transform(idl.begin(), idl.end(), idl.begin(), [](unsigned char c){ return std::tolower(c); });
					if (idl.find(fl) != std::string::npos) filtered.push_back(id);
				}
			}
			model_picker_open_ = false;
			// Restore the field captured when the picker opened, so the choice lands in
			// the exact text box the cursor was in (even if focus changed meanwhile).
			if (picker_target_field_ >= 0) { prop_text_edit_field_ = picker_target_field_; prop_edit_obj_index_ = picker_target_obj_; }
			if (prop_text_edit_field_ >= 0 && model_picker_selected_ < (int)filtered.size()) {
				prop_text_buf_ = filtered[model_picker_selected_]; // CLEAR and insert
				prop_text_caret_ = (int)prop_text_buf_.size();
				CommitPropTextEdit();              // apply the chosen model to the field
			}
			picker_target_field_ = -1; picker_target_obj_ = -1;
			return;
		}
		if (key == 8) {
			if (!model_picker_filter_.empty()) { model_picker_filter_.pop_back(); model_picker_selected_ = 0; model_picker_scroll_ = 0; }
		} else if (key >= 32 && key <= 126) {
			model_picker_filter_ += (char)key;
			model_picker_selected_ = 0;
			model_picker_scroll_ = 0;
		}
		return;
	}

	// C3: Find bar input
	if (find_open_) {
		if (key == 27) { // ESC — close
			find_open_ = false;
			find_query_.clear();
			find_result_idx_ = -1;
			return;
		}
		if (key == 13) { // Enter — confirm
			if (find_mode_ == FindMode::SetId) {
				// Empty query → automatic unique ID; otherwise set the typed ID.
				std::string q = find_query_;
				while (!q.empty() && (q.front() == ' ' || q.front() == '\t')) q.erase(q.begin());
				while (!q.empty() && (q.back()  == ' ' || q.back()  == '\t')) q.pop_back();
				if (selected_object_index_ >= 0) {
					if (q.empty()) {
						AssignTaskID();
					} else {
						auto& objects = level_.GetLevelObjects().GetObjects();
						objects[selected_object_index_].taskId = q;
						objects[selected_object_index_].modified = true;
						level_.GetLevelObjects().UpdateCoordinatesInLine(objects[selected_object_index_]);
						SaveAndReloadObjects();
						auto& reloaded = level_.GetLevelObjects().GetObjects();
						if (!reloaded.empty()) selected_object_index_ = std::min(selected_object_index_, (int)reloaded.size() - 1);
						status_message_ = "Set Task ID: " + q;
					}
				}
				find_open_ = false; find_query_.clear(); find_result_idx_ = -1;
				return;
			}
			if (find_result_idx_ >= 0) {
				selected_object_index_ = find_result_idx_;
				// Expand all ancestor containers so the found item is visible in tree
				{
					auto& objects = level_.GetLevelObjects().GetObjects();
					int idx = find_result_idx_;
					while (idx >= 0 && idx < (int)objects.size()) {
						int parent = objects[idx].parentIndex;
						if (parent < 0 || parent >= (int)objects.size()) break;
						if (objects[parent].isContainer)
							objects[parent].expanded = true;
						idx = parent;
					}
				}
				// Scroll the tree to make the found item visible
				auto visibleList = GetVisibleTreeNodes();
				int current_row = -1;
				for (int i = 0; i < (int)visibleList.size(); ++i) {
					if (visibleList[i] == find_result_idx_) {
						current_row = i;
						break;
					}
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
			find_open_ = false;
			find_query_.clear();
			find_result_idx_ = -1;
			return;
		}
		if (key == 8) { // Backspace
			if (!find_query_.empty())
				find_query_.pop_back();
		} else if (key >= 32 && key <= 126) {
			find_query_ += (char)key;
		}
		// Search (respects find_mode_). SetId is input-only — no live search.
		if (!find_query_.empty() && find_mode_ != FindMode::SetId) {
			std::string q_lower = find_query_;
			std::transform(q_lower.begin(), q_lower.end(), q_lower.begin(), [](unsigned char c){ return std::tolower(c); });
			const auto& objects = level_.GetLevelObjects().GetObjects();
			find_result_idx_ = -1;
			for (int i = 0; i < (int)objects.size(); ++i) {
				if (objects[i].deleted) continue;
				if (objects[i].type == "Task_DeclareParameters") continue;
				std::string label;
				if (find_mode_ == FindMode::TextInTask) {
					for (auto& tok : objects[i].argTokens) label += tok + " ";
				} else if (find_mode_ == FindMode::ById) {
					label = objects[i].taskId;
				} else if (find_mode_ == FindMode::ByNote) {
					label = objects[i].name;
				} else {
					label = objects[i].type + " " + objects[i].name + " " + objects[i].taskId;
				}
				std::transform(label.begin(), label.end(), label.begin(), [](unsigned char c){ return std::tolower(c); });
				if (label.find(q_lower) != std::string::npos) {
					find_result_idx_ = i;
					break;
				}
			}
		} else {
			find_result_idx_ = -1;
		}
		return;
	}

	if (task_picker_open_) {
		if (key == 27) { // ESC - Cancel and close
			task_picker_open_ = false;
			return;
		}
		if (key == 13) { // Enter - Confirm selection
			auto& objects = level_.GetLevelObjects().GetObjects();
			std::vector<int> picker_to_objects;
			
			std::string search_lower = task_picker_search_;
			std::transform(search_lower.begin(), search_lower.end(), search_lower.begin(), [](unsigned char c) { return std::tolower(c); });
			
			std::set<std::string> seen_types;
			for (int i = 0; i < (int)objects.size(); ++i) {
				if (!objects[i].deleted) {
					const auto& obj = objects[i];
					if (obj.type == "Task_DeclareParameters") continue; // picker excludes declare params
					std::string type_lower = obj.type;
					std::transform(type_lower.begin(), type_lower.end(), type_lower.begin(),
					               [](unsigned char c) { return std::tolower(c); });
					if (seen_types.count(type_lower) > 0) continue;
					seen_types.insert(type_lower);
					std::string label = obj.type + "()";

					std::string label_lower = label;
					std::transform(label_lower.begin(), label_lower.end(), label_lower.begin(), [](unsigned char c) { return std::tolower(c); });

					if (search_lower.empty() || label_lower.find(search_lower) != std::string::npos) {
						picker_to_objects.push_back(i);
					}
				}
			}

			if (task_picker_selected_idx_ >= 0 && task_picker_selected_idx_ < (int)picker_to_objects.size()) {
				if (selected_object_index_ < 0 || selected_object_index_ >= (int)objects.size()) {
					status_message_ = "Error: Must select a valid parent task first.";
					Logger::Get().Log(LogLevel::WARNING, "[App] Validation failed: Parent index is invalid.");
					task_picker_open_ = false;
					return;
				}

				int sourceIdx = picker_to_objects[task_picker_selected_idx_];
				
				std::vector<LevelObject> temp_clipboard;
				std::function<void(int, int)> copy_recurse = [&](int idx, int newParentInTemp) {
					if (idx < 0 || idx >= (int)objects.size()) return;
					
					LevelObject copy = objects[idx];
					copy.childrenIndices.clear();
					copy.parentIndex = newParentInTemp;
					copy.modified = true;
					copy.qscLine.clear();

					// Clear IDs, model names, and notes (keeping position/rotation as default)
					copy.taskId = "-1";
					copy.name = "";
					copy.original_name = "";
					copy.has_original_name = false;
					copy.modelId = "";
					copy.aiId = "";
					copy.graphId = "";
					copy.graphName = "";
					copy.primaryWeapon = "";
					copy.primaryAmmo = "";
					copy.secondaryWeapon = "";
					copy.secondaryAmmo = "";
					copy.segmentModelId = "";
					copy.secondaryModelId = "";
					copy.lensModelId = "";
					copy.splineTaskId = "";
					copy.isAttaProxy = false;
					copy.attaRecordIndex = -1;
					copy.attaParentModelId = "";

					// Synchronize cleared fields into the argTokens
					level_.GetLevelObjects().UpdateCoordinatesInLine(copy);
					
					int tempIdx = (int)temp_clipboard.size();
					temp_clipboard.push_back(copy);
					
					if (newParentInTemp != -1) {
						temp_clipboard[newParentInTemp].childrenIndices.push_back(tempIdx);
					}
					
					// Do not recursively copy children for new tasks inserted from the picker
					/*
					for (int childIdx : objects[idx].childrenIndices) {
						copy_recurse(childIdx, tempIdx);
					}
					*/
				};
				
				copy_recurse(sourceIdx, -1);

				if (!ValidateParentChildCompatibility(objects[selected_object_index_], temp_clipboard)) {
					status_message_ = "Error: Cannot add Computer to a WaterTower.";
					Logger::Get().Log(LogLevel::WARNING, "[App] Validation failed: Cannot add Computer task to WaterTower parent.");
					task_picker_open_ = false;
					return;
				}
				
				int targetParent = selected_object_index_;
				int startIdxInObjects = (int)objects.size();

				// Collect all in-use task IDs for unique ID generation
				std::set<int> usedIds;
				for (const auto& obj : objects) {
					if (obj.deleted) continue;
					if (obj.taskId.empty() || obj.taskId == "-1") continue;
					try { usedIds.insert(std::stoi(obj.taskId)); } catch (...) {}
				}

				int levelNo = level_.GetLevelNo();
				std::string aiDir = Utils::GetIGIRootPath() + "\\missions\\location0\\level" + std::to_string(levelNo) + "\\ai";

				for (size_t i = 0; i < temp_clipboard.size(); ++i) {
					LevelObject pasted = temp_clipboard[i];

					if (pasted.parentIndex == -1) {
						pasted.parentIndex = targetParent;
						if (targetParent != -1) {
							if (task_picker_insert_first_)
								objects[targetParent].childrenIndices.insert(
									objects[targetParent].childrenIndices.begin(), (int)objects.size());
							else
								objects[targetParent].childrenIndices.push_back((int)objects.size());
							objects[targetParent].modified = true;
						}
					} else {
						pasted.parentIndex += startIdxInObjects;
					}
					
					for (size_t j = 0; j < pasted.childrenIndices.size(); ++j) {
						pasted.childrenIndices[j] += startIdxInObjects;
					}
					
					if (pasted.qscFuncName == "Task_New") {
						if (pasted.type == "HumanSoldier" || pasted.type == "HumanSoldierFemale" || pasted.type == "HumanAI") {
							std::string oldId = pasted.taskId;

							// Find next available unique ID (ensuring no file conflict in AI directory)
							int newId = 1;
							while (true) {
								if (usedIds.count(newId) == 0) {
									std::string idStr = std::to_string(newId);
									std::string qvmPath = aiDir + "\\" + idStr + ".qvm";
									std::string qscPath = aiDir + "\\" + idStr + ".qsc";
									if (!std::filesystem::exists(qvmPath) && !std::filesystem::exists(qscPath)) {
										break;
									}
								}
								newId++;
							}
							usedIds.insert(newId);

							std::string newIdStr = std::to_string(newId);
							pasted.taskId = newIdStr;
							if (!pasted.argTokens.empty()) {
								pasted.argTokens[0] = newIdStr;
							}
							pasted.qscLine.clear(); // Force regeneration from argTokens on save
							pasted.modified = true;

							// For HumanAI: create and compile a new AI script (.qsc -> .qvm)
							if (pasted.type == "HumanAI") {
								std::filesystem::create_directories(aiDir);
								std::string qscPath = aiDir + "\\" + newIdStr + ".qsc";
								std::string qvmPath = aiDir + "\\" + newIdStr + ".qvm";
								std::string qscContent = 
									"if (AIFunction_GetCurrentEventType() == AIEVENT_CREATE)\n"
									"{\n"
									"  AIFunction_DefaultHandler();\n"
									"}\n"
									"else\n"
									"{\n"
									"  AIFunction_DefaultHandler();\n"
									"}";

								std::ofstream qscFile(qscPath);
								if (qscFile) {
									qscFile << qscContent;
									qscFile.close();

									// Compile QSC to QVM
									auto lexResult  = qsc::Lex(qscContent);
									auto parseResult = lexResult.ok ? qsc::Parse(lexResult.tokens) : qsc::ParseResult{};
									std::string compileErr;
									bool success = lexResult.ok && parseResult.ok &&
									               qvm::CompileToFile(*parseResult.program, qvmPath, &compileErr);
									if (success) {
										Logger::Get().Log(LogLevel::INFO, "[App] Successfully compiled new AI script: " + qvmPath);
									} else {
										std::string detail = compileErr.empty() ? "(no detail)" : compileErr;
										Logger::Get().Log(LogLevel::ERR, "[App] Failed to compile new AI script: " + detail);
										status_message_ = "Warning: Failed to compile new AI script for Task " + newIdStr + ".";
									}

									// Remove temporary qsc file
									std::error_code ec;
									std::filesystem::remove(qscPath, ec);
									if (ec) {
										Logger::Get().Log(LogLevel::WARNING, "[App] Failed to remove temp QSC: " + qscPath + " (" + ec.message() + ")");
									}
								} else {
									Logger::Get().Log(LogLevel::ERR, "[App] Failed to create temp QSC file: " + qscPath);
									status_message_ = "Warning: Failed to create temp QSC file for Task " + newIdStr + ".";
								}
							}
							Logger::Get().Log(LogLevel::INFO, "[App] Assigned unique Task ID " + newIdStr + " to newly inserted " + pasted.type);
						} else {
							pasted.taskId = "-1";
							if (!pasted.argTokens.empty()) {
								pasted.argTokens[0] = "-1";
							}
						}
					}
					
					objects.push_back(pasted);
					level_.GetLevelObjects().UpdateCoordinatesInLine(objects.back());
				}
				
				// Override position with camera location if TaskNewCameraRelative
				if (task_new_at_camera_ && startIdxInObjects < (int)objects.size()) {
					objects[startIdxInObjects].pos = glm::dvec3(viewer_.pos_);
					objects[startIdxInObjects].modified = true;
					level_.GetLevelObjects().UpdateCoordinatesInLine(objects[startIdxInObjects]);
				}
				task_picker_insert_first_ = false;
				task_new_at_camera_       = false;

				selected_object_index_ = startIdxInObjects;
				SaveAndReloadObjects();

				auto& reloaded = level_.GetLevelObjects().GetObjects();
				if (!reloaded.empty()) {
					selected_object_index_ = std::min(selected_object_index_, (int)reloaded.size() - 1);
				}
				Logger::Get().Log(LogLevel::INFO, "[App] Successfully cloned task subtree into the tree hierarchy.");
			}
			task_picker_open_ = false;
			return;
		}
		if (key == 8) { // Backspace
			if (!task_picker_search_.empty()) {
				task_picker_search_.pop_back();
				task_picker_selected_idx_ = 0;
				task_picker_scroll_offset_ = 0;
			}
			return;
		}
		if (key >= 32 && key <= 126) { // Printable characters
			if (task_picker_search_.size() < 20) {
				task_picker_search_ += (char)key;
				task_picker_selected_idx_ = 0;
				task_picker_scroll_offset_ = 0;
			}
			return;
		}
		return; // Block other keyboard input while picker is open
	}

	// C3: Ctrl+F — toggle find bar (key 6 = Ctrl+F, hardcoded for convenience)
	// Don't intercept Ctrl+Shift+F — that goes to TaskFindAgain via event bindings.
	if (key == 6 && !(glutGetModifiers() & GLUT_ACTIVE_SHIFT)) {
		find_open_       = !find_open_;
		find_mode_       = FindMode::TaskNameTypeId;
		find_query_.clear();
		find_result_idx_ = -1;
		return;
	}

	// Enter: open property editor for the selected task, same as double-click.
	// For containers also toggle expanded/collapsed.
	if (key == 13 && !(glutGetModifiers() & GLUT_ACTIVE_ALT)) {
		if (selected_object_index_ >= 0) {
			auto& objects = level_.GetLevelObjects().GetObjects();
			if (selected_object_index_ < (int)objects.size()) {
				auto& obj = objects[selected_object_index_];
				prop_editor_open_ = true; prop_panel_scroll_ = 0; prop_text_edit_field_ = -1; prop_edit_obj_index_ = -1;
				LoadAIScriptForSelected();
				if (obj.isContainer) {
					obj.expanded = !obj.expanded;
					Logger::Get().Log(LogLevel::INFO, "[App] Enter opened props + toggled expand for " + obj.type);
				} else {
					Logger::Get().Log(LogLevel::INFO, "[App] Enter opened property panel for " + obj.type);
				}
				return;
			}
		}
	}

	// Check for modifier keys - if pressed, skip movement key checks
	int modifiers = glutGetModifiers();
	bool has_modifiers = (modifiers & (GLUT_ACTIVE_CTRL | GLUT_ACTIVE_SHIFT | GLUT_ACTIVE_ALT));

	// Check movement keys (regular keyboard characters) - case insensitive
	// Only check if the config key is a regular character (ASCII < 128 and not a special key code)
	// Special key codes (arrow keys) are >= 100 and should not match regular character keys
	if (!has_modifiers) {
		// Only check movement keys if they are regular characters, not special keys
		// Special keys have codes >= 100 (GLUT_KEY_UP=101, DOWN=103, LEFT=100, RIGHT=102)
		if (config.keyMoveForward < 100 && config.keyMoveForward > 0 && toupper(key) == toupper(config.keyMoveForward)) {
			input_.keys_ |= MK_FORWARD;
			return;
		}
		if (config.keyMoveBackward < 100 && config.keyMoveBackward > 0 && toupper(key) == toupper(config.keyMoveBackward)) {
			input_.keys_ |= MK_BACKWARD;
			return;
		}
		if (config.keyMoveLeft < 100 && config.keyMoveLeft > 0 && toupper(key) == toupper(config.keyMoveLeft)) {
			input_.keys_ |= MK_LEFT;
			return;
		}
		if (config.keyMoveRight < 100 && config.keyMoveRight > 0 && toupper(key) == toupper(config.keyMoveRight)) {
			input_.keys_ |= MK_RIGHT;
			return;
		}
	}

	if (key == 27) { // ESC
		if (show_help_) { show_help_ = false; return; }
		if (prop_editor_open_) { // close the property panel first
			prop_editor_open_ = false;
			prop_field_index_ = -1;
			prop_text_edit_field_ = -1;
			return;
		}
		TogglePauseMenu();
		return;
	}


	// DEL key: delete selected task (hardcoded for standard keyboard ergonomics)
	if (key == 127) { DeleteSelectedTask(); return; }

	if (pause_mode_) {
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

	// Object Manipulation from QED config
	if (toupper(key) == toupper(config.keyRotateAlpha)) { input_.keys_ |= MK_MANIP_A; return; }
	if (toupper(key) == toupper(config.keyRotateBeta))  { input_.keys_ |= MK_MANIP_B; return; }
	if (toupper(key) == toupper(config.keyRotateGamma)) { input_.keys_ |= MK_MANIP_G; return; }
	if (toupper(key) == toupper(config.keySnapGround))  {
        PushUndoState();
        input_.keys_ |= MK_MANIP_S;
        if (selected_object_index_ >= 0) UpdateMarkerManipulation();
        input_.keys_ &= ~MK_MANIP_S;
        return;
    }
	if (toupper(key) == toupper(config.keySnapObject))  {
        PushUndoState();
        input_.keys_ |= MK_MANIP_O;
        if (selected_object_index_ >= 0) UpdateMarkerManipulation();
        input_.keys_ &= ~MK_MANIP_O;
        return;
    }
	if (key == ' ' && !(glutGetModifiers() & GLUT_ACTIVE_ALT)) {
        PushUndoState();
        input_.keys_ |= MK_MANIP_SPACE;
        if (selected_object_index_ >= 0) UpdateMarkerManipulation();
        input_.keys_ &= ~MK_MANIP_SPACE;
        return;
    }

	if (key == 't' || key == 'T') {
		level_.TeleportToHMP(viewer_.pos_);
		viewer_.pitch_ = -30.0f; // Look down at the terrain
		UpdateViewerVectors();
		printf("Teleported to Height Map Zone\n");
		return;
	}

	// HUD toggle removed as per request



	if (key == 's' || key == 'S') {
		// Snap selected object to ground (works in any mode)
		if (selected_object_index_ >= 0) {
			auto& objects = level_.GetLevelObjects().GetObjects();
			if (selected_object_index_ < (int)objects.size()) {
				auto& obj = objects[selected_object_index_];
				if (!Utils::IsUndergroundModel(obj.name, obj.modelId)) {
					float terrainZ = 0.0f;
					if (level_.GetTerrainZ(obj.pos.x, obj.pos.y, terrainZ)) {
						float zOffset = renderer_.GetMeshZOffset(obj.modelId, obj.isBuilding);
						// Snap object bottom to terrain surface
						obj.pos.z = (double)terrainZ + (double)(zOffset * 40.96f * obj.scale);
						Logger::Get().Log(LogLevel::INFO, "[App] Snapped object to ground");
					}
				}
			}
		}
		return;
	}


	// Plain Tab cycles selection; Ctrl+I and Ctrl+Shift+I must reach DispatchEventBindings.
	if (key == '\t' && !(glutGetModifiers() & (GLUT_ACTIVE_CTRL | GLUT_ACTIVE_SHIFT | GLUT_ACTIVE_ALT))) {
		LevelObjects& lo = level_.GetLevelObjects();
		if (!lo.GetObjects().empty()) {
			selected_object_index_ = (selected_object_index_ + 1) % (int)lo.GetObjects().size();
			printf("Selected Object: %d / %d\n", selected_object_index_, (int)lo.GetObjects().size());
		}
		return;
	}

	// HandleMarkerInput(key);

	DispatchEventBindings();
}

void App::DispatchEventBindings() {
	// Guard: skip dispatch while any text-input modal is open
	if (task_picker_open_ || task_editor_open_ || find_open_ ||
	    file_dialog_mode_ != FileDialogMode::None ||
	    ac_task_picker_open_ || model_picker_open_) return;

	auto& eventBindings = Config::Get().eventBindings_;

	auto Check = [&](const std::string& name) -> bool {
		auto it = eventBindings.find(name);
		if (it == eventBindings.end()) return false;
		// Exact modifier match so e.g. Ctrl+C (TaskCopy) does not also fire while
		// Ctrl+Shift+C (TaskCopyRecursive) is pressed, F2 during Shift+F2, etc.
		return Utils::IsKeyBindingPressedExact(it->second);
	};

	// ---- Help ----
	if (Check("ToggleHelp")) {
		show_help_ = !show_help_;
		help_scroll_offset_ = 0;
		if (show_help_) LoadHelpEntries();
		return;
	}

	// ---- Camera ----
	if (Check("CameraEnable")) { /* handled by existing named binding */ }
	if (Check("CameraMoveForward")) { /* handled by existing named binding */ }
	if (Check("CameraMoveBackward")) { /* handled by existing named binding */ }
	if (Check("CameraAdjustRadius")) { /* handled by existing named binding */ }
	if (Check("CameraResetRadius")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] CameraResetRadius not implemented"); }
	if (Check("CameraLookDown")) { /* handled by existing named binding */ }
	if (Check("CameraCopyPosition")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] CameraCopyPosition not implemented"); }
	if (Check("CameraCopyOrientation")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] CameraCopyOrientation not implemented"); }
	if (Check("CameraSnapToObject")) {
		if (selected_object_index_ >= 0) {
			auto& objects = level_.GetLevelObjects().GetObjects();
			if (selected_object_index_ < (int)objects.size()) {
				PushUndoState();
				input_.keys_ |= MK_MANIP_O;
				if (selected_object_index_ >= 0) UpdateMarkerManipulation();
				input_.keys_ &= ~MK_MANIP_O;
			}
		}
	}
	if (Check("CameraSnapToObjectWithRadius")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] CameraSnapToObjectWithRadius not implemented"); }
	if (Check("CameraSnapToGround")) {
		if (selected_object_index_ >= 0) {
			auto& objects = level_.GetLevelObjects().GetObjects();
			if (selected_object_index_ < (int)objects.size()) {
				auto& obj = objects[selected_object_index_];
				if (!Utils::IsUndergroundModel(obj.name, obj.modelId)) {
					float terrainZ = 0.0f;
					if (level_.GetTerrainZ(obj.pos.x, obj.pos.y, terrainZ)) {
						float zOffset = renderer_.GetMeshZOffset(obj.modelId, obj.isBuilding);
						obj.pos.z = (double)terrainZ + (double)(zOffset * 40.96f * obj.scale);
						obj.modified = true;
						level_.GetLevelObjects().UpdateCoordinatesInLine(obj);
						Logger::Get().Log(LogLevel::INFO, "[App] CameraSnapToGround: Snapped object to ground");
					}
				}
			}
		}
	}
	if (Check("CameraStrafe")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] CameraStrafe not implemented"); }
	// CameraStrafeFree is handled at the end of this function (one-shot toggle)
	// CameraStrafeLeft/Right are handled in ProcessInput (continuous per-frame movement)

	// ---- Tasks ----
	if (Check("TaskMoveUp")) {
		if (selected_object_index_ >= 0) {
			auto& objects = level_.GetLevelObjects().GetObjects();
			int par = objects[selected_object_index_].parentIndex;
			if (par >= 0) {
				auto& kids = objects[par].childrenIndices;
				auto it = std::find(kids.begin(), kids.end(), selected_object_index_);
				if (it != kids.end() && it != kids.begin()) {
					PushUndoState();
					std::swap(*it, *(it - 1));
					objects[par].modified = true;
					SaveAndReloadObjects();
				}
			}
		}
		return;
	}
	if (Check("TaskMoveDown")) {
		if (selected_object_index_ >= 0) {
			auto& objects = level_.GetLevelObjects().GetObjects();
			int par = objects[selected_object_index_].parentIndex;
			if (par >= 0) {
				auto& kids = objects[par].childrenIndices;
				auto it = std::find(kids.begin(), kids.end(), selected_object_index_);
				if (it != kids.end() && (it + 1) != kids.end()) {
					PushUndoState();
					std::swap(*it, *(it + 1));
					objects[par].modified = true;
					SaveAndReloadObjects();
				}
			}
		}
		return;
	}
	if (Check("TaskMoveHigher")) {
		if (selected_object_index_ >= 0) {
			auto& objects = level_.GetLevelObjects().GetObjects();
			int par = objects[selected_object_index_].parentIndex;
			if (par >= 0 && objects[par].parentIndex >= 0) {
				int gp = objects[par].parentIndex;
				PushUndoState();
				// Remove selected from parent's children
				auto& pKids = objects[par].childrenIndices;
				pKids.erase(std::find(pKids.begin(), pKids.end(), selected_object_index_));
				// Insert selected before parent in grandparent's children
				auto& gpKids = objects[gp].childrenIndices;
				auto gpIt = std::find(gpKids.begin(), gpKids.end(), par);
				gpKids.insert(gpIt, selected_object_index_);
				objects[selected_object_index_].parentIndex = gp;
				objects[par].modified = true;
				objects[gp].modified = true;
				objects[selected_object_index_].modified = true;
				SaveAndReloadObjects();
			}
		}
		return;
	}
	if (Check("TaskMoveLower")) {
		if (selected_object_index_ >= 0) {
			auto& objects = level_.GetLevelObjects().GetObjects();
			int par = objects[selected_object_index_].parentIndex;
			if (par >= 0) {
				auto& pKids = objects[par].childrenIndices;
				auto it = std::find(pKids.begin(), pKids.end(), selected_object_index_);
				if (it != pKids.end() && it != pKids.begin()) {
					int prevSib = *(it - 1);
					PushUndoState();
					pKids.erase(it);
					objects[prevSib].childrenIndices.push_back(selected_object_index_);
					objects[selected_object_index_].parentIndex = prevSib;
					objects[par].modified = true;
					objects[prevSib].modified = true;
					objects[selected_object_index_].modified = true;
					SaveAndReloadObjects();
				}
			}
		}
		return;
	}
	if (Check("TaskNew")) { CreateNewTask(); }
	if (Check("TaskNewFirstChild")) {
		task_picker_insert_first_ = true;
		CreateNewTask();
	}
	if (Check("TaskNewCameraRelative")) {
		task_new_at_camera_ = true;
		CreateNewTask();
	}
	if (Check("TaskCopy")) { CopySelectedTask(false); }
	if (Check("TaskCopyRecursive")) { CopySelectedTask(true); }
	if (Check("TaskPaste")) { PasteTask(); }
	if (Check("TaskSendEvent")) {
		status_message_ = "TaskSendEvent: requires live game connection";
		return;
	}
	if (Check("TaskSendEventRecursive")) {
		status_message_ = "TaskSendEventRecursive: requires live game connection";
		return;
	}
	if (Check("TaskFindTextInTask")) {
		find_mode_  = FindMode::TextInTask;
		find_open_  = true;
		find_query_.clear();
		find_result_idx_ = -1;
		return;
	}
	if (Check("TaskFindByTaskID")) {
		find_mode_  = FindMode::ById;
		find_open_  = true;
		find_query_.clear();
		find_result_idx_ = -1;
		return;
	}
	if (Check("TaskFindByTaskNote")) {
		find_mode_  = FindMode::ByNote;
		find_open_  = true;
		find_query_.clear();
		find_result_idx_ = -1;
		return;
	}
	if (Check("TaskFindAgain")) {
		if (!find_query_.empty()) {
			const auto& objects = level_.GetLevelObjects().GetObjects();
			std::string q = find_query_;
			std::transform(q.begin(), q.end(), q.begin(), [](unsigned char c){ return std::tolower(c); });
			int start = (find_result_idx_ >= 0 ? find_result_idx_ + 1 : 0);
			bool found = false;
			for (int i = 0; i < (int)objects.size(); ++i) {
				int idx = (start + i) % (int)objects.size();
				if (objects[idx].deleted || objects[idx].type == "Task_DeclareParameters") continue;
				std::string label;
				if (find_mode_ == FindMode::TextInTask) {
					for (auto& tok : objects[idx].argTokens) label += tok + " ";
				} else if (find_mode_ == FindMode::ById) {
					label = objects[idx].taskId;
				} else if (find_mode_ == FindMode::ByNote) {
					label = objects[idx].name;
				} else {
					label = objects[idx].type + " " + objects[idx].name + " " + objects[idx].taskId;
				}
				std::transform(label.begin(), label.end(), label.begin(), [](unsigned char c){ return std::tolower(c); });
				if (label.find(q) != std::string::npos) {
					find_result_idx_ = idx;
					found = true;
					// Expand ancestors and scroll to result
					{
						int anc = idx;
						while (anc >= 0 && anc < (int)objects.size()) {
							int pp = objects[anc].parentIndex;
							if (pp < 0 || pp >= (int)objects.size()) break;
							if (objects[pp].isContainer)
								const_cast<LevelObject&>(objects[pp]).expanded = true;
							anc = pp;
						}
					}
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
		}
		return;
	}
	if (Check("TaskSetID")) {
		// Ctrl+I → input box for a Task ID. Empty + Enter = automatic unique assignment.
		if (selected_object_index_ < 0) { status_message_ = "Set Task ID: select a task first"; return; }
		find_mode_  = FindMode::SetId;
		find_open_  = true;
		find_query_.clear();
		find_result_idx_ = -1;
		return;
	}
	if (Check("TaskRebuildTree")) {
		SaveAndReloadObjects();
		status_message_ = "Tree rebuilt";
		return;
	}
	if (Check("TaskMakeTemplate")) {
		if (selected_object_index_ >= 0) {
			const auto& obj = level_.GetLevelObjects().GetObjects()[selected_object_index_];
			std::string dir = Utils::GetExeDirectory() + "\\content\\qed\\templates";
			std::filesystem::create_directories(dir);
			std::string path = dir + "\\" + obj.type + ".qsc";
			SaveTaskSubtreeToFile(selected_object_index_, path);
			status_message_ = "Template saved: " + path;
		}
		return;
	}
	if (Check("TaskSortChildren")) {
		if (selected_object_index_ >= 0) {
			PushUndoState();
			auto& objects = level_.GetLevelObjects().GetObjects();
			auto& obj = objects[selected_object_index_];
			std::sort(obj.childrenIndices.begin(), obj.childrenIndices.end(),
				[&](int a, int b){ return objects[a].type < objects[b].type; });
			obj.modified = true;
			SaveAndReloadObjects();
		}
		return;
	}
	if (Check("TaskDelete")) { DeleteSelectedTask(); }

	// ---- AnimTask ----
	if (Check("AnimTaskIncreaseKeyframeInterpolation")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] AnimTaskIncreaseKeyframeInterpolation not implemented"); }
	if (Check("AnimTaskDecreaseKeyframeInterpolation")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] AnimTaskDecreaseKeyframeInterpolation not implemented"); }
	if (Check("AnimTaskToggleCameraRelative")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] AnimTaskToggleCameraRelative not implemented"); }
	if (Check("AnimTaskGoToCursor")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] AnimTaskGoToCursor not implemented"); }
	if (Check("AnimTaskToggleWindowHidden")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] AnimTaskToggleWindowHidden not implemented"); }
	if (Check("AnimTaskStartRecording")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] AnimTaskStartRecording not implemented"); }
	if (Check("AnimTaskGoToTop")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] AnimTaskGoToTop not implemented"); }
	if (Check("AnimTaskToggleSyncPlayback")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] AnimTaskToggleSyncPlayback not implemented"); }

	// ---- Timer ----
	if (Check("TimerIncreaseSpeed")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] TimerIncreaseSpeed not implemented"); }
	if (Check("TimerDecreaseSpeed")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] TimerDecreaseSpeed not implemented"); }
	if (Check("TimerReset")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] TimerReset not implemented"); }
	if (Check("TimerStartStop")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] TimerStartStop not implemented"); }

	// ---- Manipulate ----
	if (Check("Manipulate")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] Manipulate not implemented"); }
	if (Check("ManipulatePositionXY")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] ManipulatePositionXY not implemented"); }
	if (Check("ManipulatePositionXZ")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] ManipulatePositionXZ not implemented"); }
	if (Check("ManipulatePositionSnapToGround")) {
		if (selected_object_index_ >= 0) {
			auto& objects = level_.GetLevelObjects().GetObjects();
			if (selected_object_index_ < (int)objects.size()) {
				auto& obj = objects[selected_object_index_];
				if (!Utils::IsUndergroundModel(obj.name, obj.modelId)) {
					float terrainZ = 0.0f;
					if (level_.GetTerrainZ(obj.pos.x, obj.pos.y, terrainZ)) {
						float zOffset = renderer_.GetMeshZOffset(obj.modelId, obj.isBuilding);
						obj.pos.z = (double)terrainZ + (double)(zOffset * 40.96f * obj.scale);
						obj.modified = true;
						level_.GetLevelObjects().UpdateCoordinatesInLine(obj);
						Logger::Get().Log(LogLevel::INFO, "[App] ManipulatePositionSnapToGround: Snapped object to ground");
					}
				}
			}
		}
	}
	if (Check("ManipulatePositionSnapToObject")) {
		if (selected_object_index_ >= 0) {
			auto& objects = level_.GetLevelObjects().GetObjects();
			if (selected_object_index_ < (int)objects.size()) {
				PushUndoState();
				input_.keys_ |= MK_MANIP_O;
				if (selected_object_index_ >= 0) UpdateMarkerManipulation();
				input_.keys_ &= ~MK_MANIP_O;
			}
		}
	}
	if (Check("ManipulateOrientationAlpha")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] ManipulateOrientationAlpha not implemented"); }
	if (Check("ManipulateOrientationBeta")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] ManipulateOrientationBeta not implemented"); }
	if (Check("ManipulateOrientationGamma")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] ManipulateOrientationGamma not implemented"); }
	if (Check("ManipulateOrientationReset")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] ManipulateOrientationReset not implemented"); }

	// ---- GraphNode ----
	if (Check("ScaleGraphNode")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] ScaleGraphNode not implemented"); }
	if (Check("ScaleGraphNodeHalfe")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] ScaleGraphNodeHalfe not implemented"); }
	if (Check("ScaleGraphNodeDouble")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] ScaleGraphNodeDouble not implemented"); }
	if (Check("CreateGraphNode")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] CreateGraphNode not implemented"); }
	if (Check("DeleteGraphNode")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] DeleteGraphNode not implemented"); }

	// ---- Other ----
	if (Check("ToggleDisplay")) { noclip_mode_ = !noclip_mode_; }
	if (Check("ToggleMouseInverted")) { Config::Get().invertMouse = !Config::Get().invertMouse; }
	if (Check("ToggleDebugText")) { show_debug_ = !show_debug_; }
	if (Check("ToggleTaskTypeView")) { show_task_type_ = !show_task_type_; }
	if (Check("ToggleGame")) { LaunchGame(); }
	if (Check("ToggleTaskNoteDisplay")) { Config::Get().displayTaskNote = !Config::Get().displayTaskNote; }
	if (Check("ToggleQEDRunEvent")) { Config::Get().runEvent = !Config::Get().runEvent; }
	if (Check("ToggleSaveStateOnExit")) {
		Config::Get().saveConfigOnExit = !Config::Get().saveConfigOnExit;
		status_message_ = Config::Get().saveConfigOnExit ? "Save on exit: ON" : "Save on exit: OFF";
	}
	if (Check("SaveState")) {
		SaveCurrentLevel();
		int lvl = level_.GetLevelNo();
		status_message_ = "Save level: LOCAL:missions/location" + std::to_string(lvl) +
		                  "/level" + std::to_string(lvl) + "/objects.qsc";
		return;
	}
	if (Check("SaveObjectFile")) {
		// Ctrl+S → open a path textbox and write the live objects QSC to that path.
		// (Whole-level save/compile lives in the pause menu.)
		int lvl = level_.GetLevelNo();
		file_dialog_mode_  = FileDialogMode::SaveObjectFile;
		file_dialog_path_  = "missions/location" + std::to_string(lvl) +
		                     "/level" + std::to_string(lvl) + "/objects.qsc";
		file_dialog_caret_ = (int)file_dialog_path_.size();
		return;
	}
	if (Check("SaveSubTaskObjectFile")) {
		if (selected_object_index_ >= 0) {
			file_dialog_mode_ = FileDialogMode::SaveSubTask;
			file_dialog_path_ = Config::Get().taskFileName.empty() ?
			    "content\\qed\\temp\\task.qsc" : Config::Get().taskFileName;
			file_dialog_caret_ = (int)file_dialog_path_.size();
		} else {
			status_message_ = "SaveSubTask: no task selected";
		}
		return;
	}
	if (Check("SaveSubTaskObjectFileParent")) {
		if (selected_object_index_ >= 0) {
			auto& objs = level_.GetLevelObjects().GetObjects();
			int par = objs[selected_object_index_].parentIndex;
			if (par >= 0) {
				file_dialog_mode_ = FileDialogMode::SaveSubTaskParent;
				file_dialog_path_ = Config::Get().taskFileName.empty() ?
				    "content\\qed\\temp\\task.qsc" : Config::Get().taskFileName;
				file_dialog_caret_ = (int)file_dialog_path_.size();
			} else {
				status_message_ = "SaveSubTaskParent: selected task has no parent";
			}
		} else {
			status_message_ = "SaveSubTaskParent: no task selected";
		}
		return;
	}
	if (Check("LoadSubTaskObjectFile")) {
		file_dialog_mode_ = FileDialogMode::LoadSubTask;
		file_dialog_path_ = Config::Get().taskFileName.empty() ?
		    "content\\qed\\temp\\task.qsc" : Config::Get().taskFileName;
		file_dialog_caret_ = (int)file_dialog_path_.size();
		return;
	}
	if (Check("SnapToGroundRecursive")) {
		if (selected_object_index_ >= 0) {
			PushUndoState();
			auto& objects = level_.GetLevelObjects().GetObjects();
			std::function<void(int)> snapRec = [&](int idx) {
				auto& obj = objects[idx];
				if (!Utils::IsUndergroundModel(obj.name, obj.modelId)) {
					float tz = 0.f;
					if (level_.GetTerrainZ(obj.pos.x, obj.pos.y, tz)) {
						float zOff = renderer_.GetMeshZOffset(obj.modelId, obj.isBuilding);
						obj.pos.z = (double)tz + (double)(zOff * 40.96f * obj.scale);
						obj.modified = true;
						level_.GetLevelObjects().UpdateCoordinatesInLine(obj);
					}
				}
				for (int ci : obj.childrenIndices)
					if (ci >= 0 && ci < (int)objects.size() && !objects[ci].deleted) snapRec(ci);
			};
			snapRec(selected_object_index_);
			SaveAndReloadObjects();
		}
		return;
	}
	if (Check("ToggleConsole")) { show_debug_ = !show_debug_; }
	if (Check("ConsoleIncreaseAutoActivateLevel")) {
		static const char* kLevels[] = {"DEBUG","INFO","WARNING","ERR","FATAL"};
		auto& lvl = Config::Get().consoleAutoActivate;
		lvl = std::min(4, lvl + 1);
		status_message_ = std::string("Console level: ") + kLevels[lvl];
		return;
	}
	if (Check("ConsoleDecreaseAutoActivateLevel")) {
		static const char* kLevels[] = {"DEBUG","INFO","WARNING","ERR","FATAL"};
		auto& lvl = Config::Get().consoleAutoActivate;
		lvl = std::max(0, lvl - 1);
		status_message_ = std::string("Console level: ") + kLevels[lvl];
		return;
	}
	if (Check("AutoComplete")) {
		if (prop_text_edit_field_ >= 0 && !autocomplete_keywords_.empty()) {
			int ws = prop_text_caret_;
			while (ws > 0 && (isalnum((unsigned char)prop_text_buf_[ws-1]) || prop_text_buf_[ws-1] == '_')) ws--;
			std::string prefix = prop_text_buf_.substr(ws, prop_text_caret_ - ws);
			std::string pl = prefix;
			std::transform(pl.begin(), pl.end(), pl.begin(), [](unsigned char c){ return std::tolower(c); });
			for (auto& kw : autocomplete_keywords_) {
				std::string kwl = kw;
				std::transform(kwl.begin(), kwl.end(), kwl.begin(), [](unsigned char c){ return std::tolower(c); });
				if (!pl.empty() && kwl.substr(0, pl.size()) == pl) {
					prop_text_buf_.replace(ws, prefix.size(), kw);
					prop_text_caret_ = ws + (int)kw.size();
					break;
				}
			}
		}
		return;
	}
	if (Check("AutoCompleteTaskName")) {
		if (prop_text_edit_field_ == -1) {
			status_message_ = "Click a text box first, then Ctrl+N to pick a task type";
			return;
		}
		picker_target_field_  = prop_text_edit_field_;
		picker_target_obj_    = prop_edit_obj_index_;
		picker_target_caret_  = prop_text_caret_;
		ac_task_items_ = autocomplete_keywords_;
		ac_task_picker_open_   = true;
		ac_task_selected_idx_  = 0;
		ac_task_scroll_offset_ = 0;
		ac_task_filter_.clear();
		return;
	}
	if (Check("AutoCompleteModelName")) {
		if (prop_text_edit_field_ == -1) {
			status_message_ = "Click a text box first, then Ctrl+O to pick a model";
			return;
		}
		picker_target_field_  = prop_text_edit_field_;
		picker_target_obj_    = prop_edit_obj_index_;
		picker_target_caret_  = prop_text_caret_;
		model_picker_open_    = true;
		model_picker_selected_ = 0;
		model_picker_scroll_   = 0;
		model_picker_filter_.clear();
		return;
	}
	if (Check("Undo")) { Undo(); }
	if (Check("Redo")) { Redo(); }
	if (Check("ReloadSettings")) { Config::Init(); LoadAutoCompleteKeywords(); Logger::Get().Log(LogLevel::INFO, "[App] Settings reloaded from QED config"); }
	if (Check("ToggleObjects")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] ToggleObjects not implemented"); }
	if (Check("TaskMagicObjToggle")) {
		show_magic_obj_spheres_ = !show_magic_obj_spheres_;
		status_message_ = show_magic_obj_spheres_ ? "Magic objects: ON" : "Magic objects: OFF";
	}
	if (Check("AddModelToRes")) {
		int oi = selected_object_index_;
		auto& objs = level_.GetLevelObjects().GetObjects();
		if (oi >= 0 && oi < (int)objs.size() && objs[oi].modelMissingInRes) {
			std::string addId = objs[oi].modelId;
			DrawProgressOverlay("Adding model to .res", 0, "starting");
			auto progressCb = [this, addId](size_t done, size_t total) {
				int pct = total ? (int)(done * 100 / total) : 0;
				DrawProgressOverlay(("Adding '" + addId + "' to .res").c_str(), pct, "packing textures");
			};
			if (renderer_.AddModelToLevelRes(addId, progressCb)) {
				level_res_models_.AddEntry("models\\" + objs[oi].modelId + ".mef");
				objs[oi].modelMissingInRes = false;
				std::string fam = addId.substr(0, addId.find('_'));
				status_message_ = "Added model family '" + fam + "' (+textures) to .res/.dat/.mtp (backups written). If the mtp_decoder console is still open in front, press M in it to finish writing the level .mtp.";
			} else {
				status_message_ = "Failed to add '" + objs[oi].modelId + "' to level .res (see log).";
			}
		} else {
			status_message_ = "Select an object whose model is flagged missing from the .res first.";
		}
	}
	if (Check("CameraStrafeFree")) {
		camera_strafe_free_ = !camera_strafe_free_;
		status_message_ = camera_strafe_free_ ? "Strafe lock: ON" : "Strafe lock: OFF";
	}
	if (Check("TerrainBrushCycle")) {
		edit_brush_ = (edit_brush_ + 1) % 4;
		static const char* kNames[] = {"Raise","Lower","Soften","Flatten"};
		status_message_ = std::string("Terrain brush: ") + kNames[edit_brush_];
	}
	if (Check("TerrainBrushRadiusDec"))   AdjustBrushRadius(0.8);
	if (Check("TerrainBrushRadiusInc"))   AdjustBrushRadius(1.25);
	if (Check("TerrainBrushStrengthDec")) AdjustBrushStrength(-1.0);
	if (Check("TerrainBrushStrengthInc")) AdjustBrushStrength(1.0);
}


void App::Input_OnKeyboardUp(unsigned char key, int x, int y) {
	auto& config = Config::Get();

	// Check for modifier keys - if pressed, skip movement key checks
	int modifiers = glutGetModifiers();
	bool has_modifiers = (modifiers & (GLUT_ACTIVE_CTRL | GLUT_ACTIVE_SHIFT | GLUT_ACTIVE_ALT));

	// Check movement keys (regular keyboard characters) - case insensitive
	// Only check if the config key is a regular character (ASCII < 128 and not a special key code)
	// Special key codes (arrow keys) are >= 100 and should not match regular character keys
	if (!has_modifiers) {
		// Only check movement keys if they are regular characters, not special keys
		// Special keys have codes >= 100 (GLUT_KEY_UP=101, DOWN=103, LEFT=100, RIGHT=102)
		if (config.keyMoveForward < 100 && config.keyMoveForward > 0 && toupper(key) == toupper(config.keyMoveForward)) {
			input_.keys_ &= ~MK_FORWARD;
			return;
		}
		if (config.keyMoveBackward < 100 && config.keyMoveBackward > 0 && toupper(key) == toupper(config.keyMoveBackward)) {
			input_.keys_ &= ~MK_BACKWARD;
			return;
		}
		if (config.keyMoveLeft < 100 && config.keyMoveLeft > 0 && toupper(key) == toupper(config.keyMoveLeft)) {
			input_.keys_ &= ~MK_LEFT;
			return;
		}
		if (config.keyMoveRight < 100 && config.keyMoveRight > 0 && toupper(key) == toupper(config.keyMoveRight)) {
			input_.keys_ &= ~MK_RIGHT;
			return;
		}
	}

	for (int i = 0; i < count_of(MOVEMENT_KEYS); ++i) {
		const movement_key_s& mk = MOVEMENT_KEYS[i];
		if (key == mk.lower_case_ || key == mk.upper_case_) {
			input_.keys_ &= ~mk.key_flag_;
			// Don't return, check manip keys too
		}
	}
	// Object Manipulation from QED config
	if (toupper(key) == toupper(config.keyRotateAlpha)) { input_.keys_ &= ~MK_MANIP_A; undo_state_pushed_for_manip_ = false; }
	if (toupper(key) == toupper(config.keyRotateBeta))  { input_.keys_ &= ~MK_MANIP_B; undo_state_pushed_for_manip_ = false; }
	if (toupper(key) == toupper(config.keyRotateGamma)) { input_.keys_ &= ~MK_MANIP_G; undo_state_pushed_for_manip_ = false; }
	if (toupper(key) == toupper(config.keySnapGround))  { input_.keys_ &= ~MK_MANIP_S; }
	if (toupper(key) == toupper(config.keySnapObject))  { input_.keys_ &= ~MK_MANIP_O; }
}

void App::Frame(float delta_seconds) {
	if (pause_mode_) {
		// Skip all updates when paused, just render
		UpdateViewDefine();
		if (mouse_state_.prior_x_ != last_pick_x_ || mouse_state_.prior_y_ != last_pick_y_) {
			hover_object_index_ = PickObjectAtScreenPos(mouse_state_.prior_x_, mouse_state_.prior_y_);
			if (hover_object_index_ >= Renderer::kAttaPickBase) hover_object_index_ = -1; // ATTA hovered (clickable; promote on click)
			last_pick_x_ = mouse_state_.prior_x_;
			last_pick_y_ = mouse_state_.prior_y_;
		}
		float ground_z = 0.0f;
		level_.GetTerrainZ(viewer_.pos_.x, viewer_.pos_.y, ground_z);
		Renderer::task_tree_view_params_s task_tree_view = {
			.show_hud_ = true,
			.status_msg_ = status_message_,
			.pause_mode_ = true,
			.pause_active_input_ = pause_active_input_,
			.pause_level_input_ = pause_level_input_,
			.pause_search_input_ = pause_search_input_,
			.pause_terrain_expanded_ = pause_terrain_expanded_,
			.show_debug_ = show_debug_,
			.show_help_ = show_help_,
			.edit_mode_ = edit_mode_,
			.terrain_edit_enabled_ = terrain_edit_enabled_,
			.terrain_mod_options_ = terrain_mod_options_,
			.selected_object_index_ = selected_object_index_,
			.hover_object_index_ = hover_object_index_,
			.hover_tree_index_ = hover_tree_index_,
			.mouse_x_ = mouse_state_.prior_x_,
			.mouse_y_ = mouse_state_.prior_y_,
			.tree_scroll_offset = tree_scroll_offset_,
			.tree_decl_expanded = tree_decl_expanded_,
			.level_objects_ = &level_.GetLevelObjects(),
			.task_picker_open_ = task_picker_open_,
			.task_picker_selected_idx_ = task_picker_selected_idx_,
			.task_picker_scroll_offset_ = task_picker_scroll_offset_,
			.task_picker_search_ = task_picker_search_,
			.enable_camera_mode_ = Utils::IsKeyBindingPressed(Config::Get().keyEnableCamera),
			.prop_editor_open_     = prop_editor_open_,
			.prop_field_index_     = prop_field_index_,
			.prop_text_edit_field_ = prop_text_edit_field_,
			.prop_edit_obj_index_  = prop_edit_obj_index_,
			.prop_drag_obj_index_  = prop_drag_obj_index_,
			.prop_text_buf_        = prop_text_buf_,
			.prop_text_caret_      = prop_text_caret_,
			.prop_panel_scroll_    = prop_panel_scroll_,
			.find_open_            = find_open_,
			.find_query_           = find_query_,
			.find_result_idx_      = find_result_idx_,
			.selected_obj_is_ai    = (selected_object_index_ >= 0 &&
				selected_object_index_ < (int)level_.GetLevelObjects().GetObjects().size() &&
				ai_model_ids_.count(level_.GetLevelObjects().GetObjects()[selected_object_index_].modelId) > 0),
			.help_scroll_offset_   = help_scroll_offset_,
			.help_entries_         = &help_entries_,
			.show_task_type_       = show_task_type_,
			.find_mode_            = (int)find_mode_,
			.file_dialog_mode_     = (int)file_dialog_mode_,
			.file_dialog_path_     = file_dialog_path_,
			.file_dialog_caret_    = file_dialog_caret_,
			.ac_task_picker_open_  = ac_task_picker_open_,
			.ac_task_selected_idx_ = ac_task_selected_idx_,
			.ac_task_scroll_offset_= ac_task_scroll_offset_,
			.ac_task_filter_       = ac_task_filter_,
			.ac_task_items_        = &ac_task_items_,
			.model_picker_open_    = model_picker_open_,
			.model_picker_selected_= model_picker_selected_,
			.model_picker_scroll_  = model_picker_scroll_,
			.model_picker_filter_  = model_picker_filter_,
			.model_ids_            = &level_model_ids_,
			.ai_script_path_       = ai_script_path_,
			.ai_script_text_       = ai_script_text_,
			.ai_script_dirty_      = ai_script_dirty_,
			.terrain_brush_          = edit_brush_,
			.terrain_brush_radius_   = edit_brush_radius_,
			.terrain_brush_strength_ = edit_brush_strength_,
		};
		draw_params_.level_objects_ = &level_.GetLevelObjects();
		draw_params_.selected_object_index_ = selected_object_index_;
		draw_params_.show_magic_obj_spheres_ = show_magic_obj_spheres_;
		draw_params_.terrain_id_at_world_xy_ =
			[this](double x, double y) { return level_.GetTerrainNodeId(x, y); };
		draw_params_.terrain_z_at_world_xy_ =
			[this](double x, double y, float& z) { return level_.GetTerrainZ(x, y, z); };
		renderer_.Draw(draw_params_, task_tree_view);

		DrawCustomCursor();
		glutSwapBuffers();
		return;
	}

	frame_++;
	frame_ %= 0xFFFFFFFF;	// reserve value 0xFFFFFFFF (-1) for INVALID_FRAME

	ProcessInput(delta_seconds);

	// Per-frame position-drag velocity: the pad / Z slider accelerate while held in
	// a direction and keep moving when the cursor is pinned at the window edge.
	if (mouse_state_.left_button_down_ && prop_field_index_ >= 0 && selected_object_index_ >= 0 &&
	    !Utils::IsKeyBindingPressed(Config::Get().keyEnableCamera)) {
		ApplyPropPositionDrag();
	}

	if (edit_mode_ && terrain_edit_enabled_ && mouse_state_.left_button_down_) {
		EditorProcessClick();
	}

	UpdateViewDefine();
	if (mouse_state_.prior_x_ != last_pick_x_ || mouse_state_.prior_y_ != last_pick_y_) {
		bool camMode    = Utils::IsKeyBindingPressed(Config::Get().keyEnableCamera);
		bool overPanel  = prop_editor_open_ &&
		                  mouse_state_.prior_x_ < (PropPanel::kLeft + PropPanel::kWidth);
		if (camMode || overPanel) {
			hover_object_index_ = -1;
		} else {
			hover_object_index_ = PickObjectAtScreenPos(mouse_state_.prior_x_, mouse_state_.prior_y_);
			if (hover_object_index_ >= Renderer::kAttaPickBase) hover_object_index_ = -1; // ATTA hovered (clickable; promote on click)
		}
		last_pick_x_ = mouse_state_.prior_x_;
		last_pick_y_ = mouse_state_.prior_y_;
	}

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
	draw_params_.show_magic_obj_spheres_ = show_magic_obj_spheres_;
	draw_params_.terrain_id_at_world_xy_ =
		[this](double x, double y) { return level_.GetTerrainNodeId(x, y); };


	float ground_z = 0.0f;
	bridge_.SetEnabled(show_hud_);
	IGIBridge::PositionData data = bridge_.GetLatestData();
	level_.GetTerrainZ(viewer_.pos_.x, viewer_.pos_.y, ground_z);

	Renderer::task_tree_view_params_s task_tree_view = {
		.show_hud_ = show_hud_,
		.status_msg_ = status_message_,
		.pause_mode_ = pause_mode_,
		.pause_active_input_ = pause_active_input_,
		.pause_level_input_ = pause_level_input_,
		.pause_search_input_ = pause_search_input_,
		.pause_terrain_expanded_ = pause_terrain_expanded_,
		.show_debug_ = show_debug_,
		.show_help_ = show_help_,
		.edit_mode_ = edit_mode_,
		.terrain_edit_enabled_ = terrain_edit_enabled_,
		.terrain_mod_options_ = terrain_mod_options_,
		.selected_object_index_ = selected_object_index_,
		.hover_object_index_ = hover_object_index_,
		.hover_tree_index_ = hover_tree_index_,
		.mouse_x_ = mouse_state_.prior_x_,
		.mouse_y_ = mouse_state_.prior_y_,
		.tree_scroll_offset = tree_scroll_offset_,
		.tree_decl_expanded = tree_decl_expanded_,
		.level_objects_ = &level_.GetLevelObjects(),
		.task_picker_open_ = task_picker_open_,
		.task_picker_selected_idx_ = task_picker_selected_idx_,
		.task_picker_scroll_offset_ = task_picker_scroll_offset_,
		.task_picker_search_ = task_picker_search_,
		.enable_camera_mode_ = Utils::IsKeyBindingPressed(Config::Get().keyEnableCamera),
		.prop_editor_open_     = prop_editor_open_,
		.prop_field_index_     = prop_field_index_,
		.prop_text_edit_field_ = prop_text_edit_field_,
		.prop_edit_obj_index_  = prop_edit_obj_index_,
		.prop_drag_obj_index_  = prop_drag_obj_index_,
		.prop_text_buf_        = prop_text_buf_,
		.prop_text_caret_      = prop_text_caret_,
		.prop_panel_scroll_    = prop_panel_scroll_,
		.find_open_            = find_open_,
		.find_query_           = find_query_,
		.find_result_idx_      = find_result_idx_,
		.selected_obj_is_ai    = (selected_object_index_ >= 0 &&
			selected_object_index_ < (int)level_.GetLevelObjects().GetObjects().size() &&
			ai_model_ids_.count(level_.GetLevelObjects().GetObjects()[selected_object_index_].modelId) > 0),
		.help_scroll_offset_   = help_scroll_offset_,
		.help_entries_         = &help_entries_,
		.show_task_type_       = show_task_type_,
		.find_mode_            = (int)find_mode_,
		.file_dialog_mode_     = (int)file_dialog_mode_,
		.file_dialog_path_     = file_dialog_path_,
		.file_dialog_caret_    = file_dialog_caret_,
		.ac_task_picker_open_  = ac_task_picker_open_,
		.ac_task_selected_idx_ = ac_task_selected_idx_,
		.ac_task_scroll_offset_= ac_task_scroll_offset_,
		.ac_task_filter_       = ac_task_filter_,
		.ac_task_items_        = &ac_task_items_,
		.model_picker_open_    = model_picker_open_,
		.model_picker_selected_= model_picker_selected_,
		.model_picker_scroll_  = model_picker_scroll_,
		.model_picker_filter_  = model_picker_filter_,
		.model_ids_            = &level_model_ids_,
		.ai_script_path_        = ai_script_path_,
		.ai_script_text_        = ai_script_text_,
		.ai_script_dirty_       = ai_script_dirty_,
		.ai_script_vscroll_     = ai_script_vscroll_,
		.ai_script_path_hscroll_= ai_script_path_hscroll_,
		.terrain_brush_          = edit_brush_,
		.terrain_brush_radius_   = edit_brush_radius_,
		.terrain_brush_strength_ = edit_brush_strength_,
	};

	renderer_.Draw(draw_params_, task_tree_view);

	DrawCustomCursor();
	glutSwapBuffers();
}


void App::ProcessTreeViewClick(int mx, int my) {
    if (!level_.GetLevelObjects().GetObjects().empty()) {
        auto& objects = level_.GetLevelObjects().GetObjects();
        int tree_x = 20;
        int row_h = 16;
        int start_y = 30;
        int current_row = 0;

        bool found = false;
        std::function<void(int, int)> check_node = [&](int idx, int depth) {
            if (found || idx < 0 || idx >= (int)objects.size()) return;
            const auto& obj = objects[idx];
            if (obj.deleted) return;
            
            int x = tree_x + (depth * 18);
            int y = start_y + (current_row - tree_scroll_offset_) * row_h;
            current_row++;

            if (y >= start_y && y < window_state_.viewport_height_ - 50) {
                // Check if interaction was on the node area (including [+] and label)
                if (mx >= x - 20 && mx <= x + 300 && my >= y && my <= y + row_h) {
                    found = true;
                    if (mx <= x + 5) { // Clicked on toggle area
                        if (obj.isContainer && !obj.childrenIndices.empty()) {
                            auto& nonConstObj = const_cast<LevelObject&>(obj);
                            nonConstObj.expanded = !nonConstObj.expanded;
                            Logger::Get().Log(LogLevel::INFO, "[App] Toggled tree node: " + obj.type);
                        }
                    } else { // Clicked on label area
                        selected_object_index_ = idx;
                        int currentTime = glutGet(GLUT_ELAPSED_TIME);
                        bool isDoubleClick = (idx == last_tree_click_index_ && (currentTime - last_tree_click_time_ms_ < 400));
                        last_tree_click_index_ = idx;
                        last_tree_click_time_ms_ = currentTime;

                        if (isDoubleClick) {
                            prop_editor_open_ = true; prop_panel_scroll_ = 0; prop_text_edit_field_ = -1; prop_edit_obj_index_ = -1;
                            LoadAIScriptForSelected();
                            Logger::Get().Log(LogLevel::INFO, "[App] Double clicked object from tree and opened property panel.");
                        } else {
                            Logger::Get().Log(LogLevel::INFO, "[App] Selected object from tree: " + obj.type);
                        }
                    }
                }
            }

            if (!found && obj.expanded) {
                for (int childIdx : obj.childrenIndices) {
                    check_node(childIdx, depth + 1);
                }
            }
        };

        std::vector<int> root_decls;
        std::vector<int> root_others;
        for (int i = 0; i < (int)objects.size(); ++i) {
            if (objects[i].parentIndex == -1 && !objects[i].deleted) {
                if (objects[i].type == "Task_DeclareParameters") root_decls.push_back(i);
                else root_others.push_back(i);
            }
        }

        if (!found && !root_decls.empty()) {
            int y = start_y + (current_row - tree_scroll_offset_) * row_h;
            current_row++;
            if (y >= start_y && y < window_state_.viewport_height_ - 50) {
                if (mx >= tree_x - 20 && mx <= tree_x + 300 && my >= y && my <= y + row_h) {
                    found = true;
                    if (mx <= tree_x + 5) {
                        tree_decl_expanded_ = !tree_decl_expanded_;
                        Logger::Get().Log(LogLevel::INFO, "[App] Toggled Mission Declarations");
                    } else {
                        selected_object_index_ = -2;
                    }
                }
            }
            if (!found && tree_decl_expanded_) {
                for (int idx : root_decls) check_node(idx, 1);
            }
        }

        if (!found) {
            for (int idx : root_others) {
                if (found) break;
                check_node(idx, 0);
            }
        }
    }
}

void App::ProcessTreeViewHover(int mx, int my) {
    int tree_x = 20;
    int start_y = 30;
    int row_h = 16;
    int current_row = 0;
    
    auto& objects = level_.GetLevelObjects().GetObjects();
    
    bool found = false;
    std::function<void(int, int)> check_node = [&](int idx, int depth) {
        if (found || idx < 0 || idx >= (int)objects.size()) return;
        auto& obj = objects[idx];
        if (obj.deleted) return;
        
        int x = tree_x + (depth * 18);
        int y = start_y + (current_row - tree_scroll_offset_) * row_h;
        current_row++;

        if (y >= start_y && y < window_state_.viewport_height_ - 50) {
            if (mx >= x - 20 && mx <= x + 300 && my >= y && my <= y + row_h) {
                hover_tree_index_ = idx;
                found = true;
            }
        }

        if (!found && obj.expanded) {
            for (int childIdx : obj.childrenIndices) {
                check_node(childIdx, depth + 1);
            }
        }
    };

    std::vector<int> root_decls;
    std::vector<int> root_others;
    for (int i = 0; i < (int)objects.size(); ++i) {
        if (objects[i].parentIndex == -1 && !objects[i].deleted) {
            if (objects[i].type == "Task_DeclareParameters") root_decls.push_back(i);
            else root_others.push_back(i);
        }
    }

    if (!found && !root_decls.empty()) {
        int y = start_y + (current_row - tree_scroll_offset_) * row_h;
        current_row++;
        if (y >= start_y && y < window_state_.viewport_height_ - 50) {
            if (mx >= tree_x - 20 && mx <= tree_x + 300 && my >= y && my <= y + row_h) {
                found = true;
                hover_tree_index_ = -1;
            }
        }
        if (!found && tree_decl_expanded_) {
            for (int idx : root_decls) check_node(idx, 1);
        }
    }

    if (!found) {
        for (int idx : root_others) {
            if (found) break;
            check_node(idx, 0);
        }
    }
}
