/******************************************************************************
 * @file    app.cpp
 * @brief   application class
 *****************************************************************************/

#include "pch.h"
#include <freeglut.h>
#include "logger.h"
#include "utils.h"
#include <filesystem>
#include <fstream>


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

// IGI 2 Style Manipulation Flags
constexpr int MK_MANIP_A		= FLAG_BIT(10);
constexpr int MK_MANIP_B		= FLAG_BIT(11);
constexpr int MK_MANIP_G		= FLAG_BIT(12);
constexpr int MK_MANIP_S		= FLAG_BIT(13);
constexpr int MK_MANIP_O		= FLAG_BIT(14);
constexpr int MK_MANIP_SPACE	= FLAG_BIT(15);

App::App():
	frame_(0),
	terrain_mod_options_(-1),
	edit_mode_(true), // Enable by default as requested
	terrain_edit_enabled_(false),
	pause_mode_(false),
	edit_brush_(0), // 0: raise, 1: lower
	selected_object_index_(0),
	hover_object_index_(-1),
	show_hud_(true),
	show_debug_(false),
	show_help_(false),
	tree_scroll_offset_(0),
	tree_decl_expanded_(false),
	status_message_(),
	noclip_mode_(true), // By default true as requested by user
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
	viewer_.clip_to_z_ = false;
	viewer_.move_speed_ = MIN_MOVE_SPEED;
	viewer_.jump_speed_ = MIN_JUMP_SPEED;
	window_state_.cursor_visible_ = true;
}

App::~App() {
	Shutdown();
}

bool App::Init(int argc, char** argv) {
	// Initialize logger with absolute path to exe directory
	std::string exeDir = Utils::GetExeDirectory();
	Logger::Get().Init(exeDir + "\\igi_editor.log");
	Logger::Get().Log(LogLevel::INFO, "IGI Editor Initializing...");

	// Check if running with admin privileges
	if (!Utils::IsElevatedProcess()) {
		std::string errorMsg = "WARNING: Application is not running with administrator privileges.\n\n"
			"Some features may not work correctly.\n"
			"Please right-click and select 'Run as administrator'.";
		Utils::ShowWarning(errorMsg, "IGI Editor - Warning");
		Logger::Get().Log(LogLevel::WARNING, "[App] Not running with admin privileges");
	}

	// Validate and setup QEditor folder structure first
	if (!Utils::ValidateAndSetupQEditor()) {
		return false;
	}

	char appDataPath[1024];
	GetEnvironmentVariableA("APPDATA", appDataPath, 1024);
	std::string qCompilerPath = std::string(appDataPath) + "\\QEditor\\QCompiler";
	std::string exeQCompilerPath = exeDir + "\\QEditor\\QCompiler";

	// Check if QCompiler exists in AppData
	if (!std::filesystem::exists(qCompilerPath)) {
		// Try to copy from exe directory if it exists there
		if (std::filesystem::exists(exeQCompilerPath)) {
			printf("[App] QCompiler not found in AppData, copying from exe directory...\n");
			Logger::Get().Log(LogLevel::INFO, "[App] QCompiler not found in AppData, copying from exe directory...");
			try {
				std::string appDataQEditor = std::string(appDataPath) + "\\QEditor";
				std::filesystem::create_directories(appDataQEditor);
				
				// Copy specific folders/files
				std::string exe3DEditor = exeDir + "\\QEditor\\3DEditor";
				std::string exeJSON = exeDir + "\\QEditor\\IGIModelsLevel.json";
				std::string exeQCompilerTools = exeDir + "\\QEditor\\QCompiler\\Tools";
				
				// Copy 3DEditor
				if (std::filesystem::exists(exe3DEditor)) {
					std::string appData3DEditor = appDataQEditor + "\\3DEditor";
					std::filesystem::create_directories(appData3DEditor);
					std::filesystem::copy(exe3DEditor, appData3DEditor,
						std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
					printf("[App] Copied 3DEditor\n");
				}
				
				// Copy IGIModelsLevel.json
				if (std::filesystem::exists(exeJSON)) {
					std::filesystem::copy_file(exeJSON, appDataQEditor + "\\IGIModelsLevel.json",
						std::filesystem::copy_options::overwrite_existing);
					printf("[App] Copied IGIModelsLevel.json\n");
				}
				
				// Copy QCompiler\Tools
				if (std::filesystem::exists(exeQCompilerTools)) {
					std::string appDataQCompilerTools = appDataQEditor + "\\QCompiler\\Tools";
					std::filesystem::create_directories(appDataQCompilerTools);
					std::filesystem::copy(exeQCompilerTools, appDataQCompilerTools,
						std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
					printf("[App] Copied QCompiler\\Tools\n");
				}
				
				printf("[App] Successfully copied required QEditor resources to AppData\n");
				Logger::Get().Log(LogLevel::INFO, "[App] Successfully copied required QEditor resources to AppData");
			}
			catch (const std::exception& e) {
				std::string errorMsg = "FATAL ERROR: Failed to copy QEditor resources from exe to AppData: " + std::string(e.what());
				Utils::LogAndShowError(errorMsg, "IGI Editor - Fatal Error");
				Log(log_type_t::LOG_FATAL, __FILE__, __LINE__, errorMsg.c_str());
				return false;
			}
		}
		else {
			std::string errorMsg = "FATAL ERROR: QCompiler directory not found at:\n" + qCompilerPath + "\n\nAnd not found in exe directory:\n" + exeQCompilerPath + "\n\nPlease make sure QEditor is correctly installed.";
			Utils::LogAndShowError(errorMsg, "IGI Editor - Fatal Error");
			Log(log_type_t::LOG_FATAL, __FILE__, __LINE__, errorMsg.c_str());
		}
	}


	if (!renderer_.Init()) {
		return false;
	}

	ConfigData& cfg = Config::Get();


	// read options from command line
	draw_params_.overlay_wireframe_ = Arg_OptionIdx(argc, argv, "-wireframe") > 0;
	draw_params_.draw_parts_ = Arg_ReadInt(argc, argv, "-draw_parts", -1);
	draw_params_.draw_terrain_options_ = Arg_ReadInt(argc, argv, "-draw_terrain_opts", -1);
	terrain_mod_options_ = Arg_ReadInt(argc, argv, "-terrain_mod_opts", terrain_mod_options_);
	stick_to_ground_ = Arg_OptionIdx(argc, argv, "-stick_to_ground") > 0;

	int start_level = Arg_ReadInt(argc, argv, "-level", cfg.level);
	if (start_level >= MIN_LEVEL_NO && start_level <= MAX_LEVEL_NO) {
		try {
			LoadLevel(start_level);
		}
		catch (const std::exception& e) {
			std::string errorMsg = "Failed to load level " + std::to_string(start_level) + ":\n" + std::string(e.what());
			Utils::LogAndShowError(errorMsg, "IGI Editor - Error");
			Logger::Get().Log(LogLevel::ERR, errorMsg);
		}
		catch (...) {
			std::string errorMsg = "Failed to load level " + std::to_string(start_level) + ":\nUnknown error occurred.";
			Utils::LogAndShowError(errorMsg, "IGI Editor - Error");
			Logger::Get().Log(LogLevel::ERR, errorMsg);
		}
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


	// Set initial cursor state
	glutSetCursor(GLUT_CURSOR_LEFT_ARROW);
	
	return true;
}

void App::Shutdown() {
	bridge_.Stop();
	level_.Unload();
	level_.FreeTerrainCubeDataPools();
	renderer_.Shutdown();

}

void App::LoadLevel(int level_no) {
	try {
		LoadCutsceneSoldierIDs();
		Logger::Get().Log(LogLevel::INFO, "[App] ==========================================");
		Logger::Get().Log(LogLevel::INFO, "[App] LoadLevel() START for level " + std::to_string(level_no));
		Logger::Get().Log(LogLevel::INFO, "[App] ==========================================");
		
		// Verify level number is valid
		if (level_no < MIN_LEVEL_NO || level_no > MAX_LEVEL_NO) {
			Logger::Get().Log(LogLevel::ERR, "[App] Invalid level number: " + std::to_string(level_no) + 
				" (valid range: " + std::to_string(MIN_LEVEL_NO) + "-" + std::to_string(MAX_LEVEL_NO) + ")");
			return;
		}
		
		renderer_.SetLevel(level_no);
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
			Logger::Get().Log(LogLevel::INFO, "[App] Level " + std::to_string(level_no) + " loaded. Viewer start=(" + std::to_string(viewer_.pos_.x) + "," + std::to_string(viewer_.pos_.y) + "," + std::to_string(viewer_.pos_.z) + ")");
		}
		else {
			std::string errorMsg = "Failed to load level " + std::to_string(level_no) + "\n\nPlease check if the terrain files exist in the correct location.";
			Utils::ShowError(errorMsg, "IGI Editor - Error");
			Logger::Get().Log(LogLevel::ERR, "[App] Failed to load level " + std::to_string(level_no));
		}

		// Load QSC file for this level
		Logger::Get().Log(LogLevel::INFO, "[App] Step 2: Loading QSC file for level " + std::to_string(level_no));
		LoadQSCForLevel(level_no);
		LoadCutsceneSoldierIDs();

		// Load AI models from JSON for this level
		Logger::Get().Log(LogLevel::INFO, "[App] Step 2.5: Loading AI models from IGIModelsAllLevel.json...");
		LoadAIModelsFromFolder(level_no);

		// Always snap objects to terrain after any level load
		Logger::Get().Log(LogLevel::INFO, "[App] Step 3: Snapping objects to terrain...");
		SnapObjectsToTerrain();

		// Level-specific rotation overrides for model 615 (Missile)
		if (level_no == 9 || level_no == 12 || level_no == 13) {
			auto& objects = level_.GetLevelObjects().GetObjects();
			for (auto& obj : objects) {
				if (obj.modelId.find("615") == 0) {
					if (level_no == 12) {
						obj.rot.x = 0.0;       // PITCH
						obj.rot.y = -1.54;     // ROLL
						obj.rot.z = 1.57;      // YAW
					} else if (level_no == 9) {
						obj.rot.x = 0.0;       // PITCH
						obj.rot.y = -1.58;     // ROLL
						obj.rot.z = 0.0;       // YAW
					} else if (level_no == 13) {
						obj.rot.x = 0.0;       // PITCH
						obj.rot.y = -1.57;     // ROLL
						obj.rot.z = 0.0;       // YAW
					}
					Logger::Get().Log(LogLevel::INFO, "[App] Applied missile rotation override for model " + obj.modelId + " in level " + std::to_string(level_no));
				}
			}
		}

		// AI rotation override: AI models (HumanSoldier, HumanAI) only have horizontal rotation
		auto& objects = level_.GetLevelObjects().GetObjects();
		for (auto& obj : objects) {
			if (obj.type == "HumanSoldier" || obj.type == "HumanAI" || obj.type.find("AITYPE") == 0) {
				if (cutscene_soldier_ids_.find(obj.taskId) == cutscene_soldier_ids_.end()) {
					obj.rot.x = 0.0;           // PITCH = 0
					obj.rot.y = 0.0;           // ROLL = 0
					// Preserve existing rotation if it's already set, otherwise default to a full circle
					if (obj.rot.z == 0.0) obj.rot.z = 6.28318;
					Logger::Get().Log(LogLevel::INFO, "[App] Applied AI rotation override (horizontal only) for " + obj.name + " (" + obj.type + ")");
				}
			}
		}
		
		Logger::Get().Log(LogLevel::INFO, "[App] ==========================================");
		Logger::Get().Log(LogLevel::INFO, "[App] LoadLevel() COMPLETE for level " + std::to_string(level_no));
		Logger::Get().Log(LogLevel::INFO, "[App] ==========================================");
	}
	catch (const std::exception& e) {
		std::string errorMsg = "Error loading level " + std::to_string(level_no) + ":\n" + std::string(e.what());
		Utils::LogAndShowError(errorMsg, "IGI Editor - Error");
		Logger::Get().Log(LogLevel::ERR, errorMsg);
	}
	catch (...) {
		std::string errorMsg = "Unknown error loading level " + std::to_string(level_no);
		Utils::LogAndShowError(errorMsg, "IGI Editor - Error");
		Logger::Get().Log(LogLevel::ERR, errorMsg);
	}
}

void App::SetGameLevel(int level_no) {
	bridge_.SetGameLevel(level_no);
}

#include <sstream>
#include <algorithm>

static bool containsIgnoreCase(const std::string& str, const std::string& substr) {
    if (substr.empty()) return true;
    auto it = std::search(
        str.begin(), str.end(),
        substr.begin(), substr.end(),
        [](char ch1, char ch2) { return std::tolower(ch1) == std::tolower(ch2); }
    );
    return it != str.end();
}

static std::set<std::string> LoadCutsceneGraphIds(int level_no) {
    std::set<std::string> cutscene_graph_ids;
    std::string graphsPath = Config::Get().graphsPath;
    std::string jsonPath = graphsPath + "\\Areas\\graph_area_level" + std::to_string(level_no) + ".json";
    
    Logger::Get().Log(LogLevel::INFO, "[App] Loading cutscene graphs from: " + jsonPath);
    
    std::ifstream file(jsonPath, std::ios::binary);
    if (!file) {
        Logger::Get().Log(LogLevel::WARNING, "[App] Could not open graph area file: " + jsonPath);
        return cutscene_graph_ids;
    }
    
    std::stringstream ss;
    ss << file.rdbuf();
    std::string content = ss.str();
    
    size_t pos = 0;
    while ((pos = content.find("{", pos)) != std::string::npos) {
        size_t end = content.find("}", pos);
        if (end == std::string::npos) break;
        
        std::string entry = content.substr(pos, end - pos + 1);
        pos = end + 1;
        
        // Extract Graph and Area
        auto extractValue = [](const std::string& str, const std::string& key) -> std::string {
            size_t kpos = str.find("\"" + key + "\"");
            if (kpos == std::string::npos) return "";
            size_t colon = str.find(":", kpos);
            if (colon == std::string::npos) return "";
            size_t qStart = str.find("\"", colon);
            if (qStart == std::string::npos) return "";
            size_t qEnd = str.find("\"", qStart + 1);
            if (qEnd == std::string::npos) return "";
            return str.substr(qStart + 1, qEnd - qStart - 1);
        };
        
        std::string graphVal = extractValue(entry, "Graph");
        std::string areaVal = extractValue(entry, "Area");
        
        if (containsIgnoreCase(areaVal, "Cutscene")) {
            // Extract number from "Graph #123"
            size_t hashPos = graphVal.find('#');
            if (hashPos != std::string::npos) {
                std::string graphId = graphVal.substr(hashPos + 1);
                graphId = Utils::Trim(graphId);
                if (!graphId.empty()) {
                    cutscene_graph_ids.insert(graphId);
                    Logger::Get().Log(LogLevel::INFO, "[App] Identified cutscene graph ID: " + graphId + " (Area: " + areaVal + ")");
                }
            }
        }
    }
    
    return cutscene_graph_ids;
}

void App::LoadAIModelsFromFolder(int level_no) {
	Logger::Get().Log(LogLevel::INFO, "[App] ==========================================");
	Logger::Get().Log(LogLevel::INFO, "[App] LoadAIModelsFromFolder() START for level " + std::to_string(level_no));
	Logger::Get().Log(LogLevel::INFO, "[App] ==========================================");

	std::string qeditor_path = Config::Get().qEditorPath;
	std::string aiFolderPath = qeditor_path + "\\AIFiles\\missions\\location0\\level" + std::to_string(level_no);

	Logger::Get().Log(LogLevel::INFO, "[App] AI folder path: " + aiFolderPath);

	// Get all GLB files in the AI folder if it exists
	std::vector<std::string> aiModels;
	if (std::filesystem::exists(aiFolderPath)) {
		for (const auto& entry : std::filesystem::directory_iterator(aiFolderPath)) {
			if (entry.path().extension() == ".glb") {
				aiModels.push_back(entry.path().filename().string());
			}
		}
		Logger::Get().Log(LogLevel::INFO, "[App] Found " + std::to_string(aiModels.size()) + " AI GLB files");
	} else {
		Logger::Get().Log(LogLevel::WARNING, "[App] AI folder does not exist: " + aiFolderPath + " (skipping GLB loading, but proceeding with JSON)");
	}

	// Read JSON file to get AI model positions
	struct AIData {
		glm::dvec3 pos;
		double rotation;
		std::string type;
		std::string name;
		std::string soldierId;
		std::string modelId;
		std::string aiId;
		std::string graphId;
		std::string graphName;
		glm::dvec3 graphPos;
		std::string primaryWeapon;
		std::string primaryAmmo;
		std::string secondaryWeapon;
		std::string secondaryAmmo;
		int team;
	};
	std::vector<AIData> aiDataList;
	std::string jsonPath = qeditor_path + "\\IGIModelsAllLevel.json";
	if (!std::filesystem::exists(jsonPath)) {
		jsonPath = "C:\\Users\\hasee\\AppData\\Roaming\\QEditor\\IGIModelsAllLevel.json";
	}
	
	if (std::filesystem::exists(jsonPath)) {
		FILE* f = fopen(jsonPath.c_str(), "rb");
		if (f) {
			fseek(f, 0, SEEK_END);
			long fileSize = ftell(f);
			fseek(f, 0, SEEK_SET);
			
			char* buf = new char[fileSize + 1];
			fread(buf, 1, fileSize, f);
			buf[fileSize] = '\0';
			fclose(f);
			
			std::string content(buf);
			delete[] buf;
			
			// Find AI array for this level
			std::string levelKey = "\"Level " + std::to_string(level_no) + "\"";
			size_t levelPos = content.find(levelKey);
			if (levelPos != std::string::npos) {
				size_t aiPos = content.find("\"AI\"", levelPos);
				if (aiPos != std::string::npos) {
					size_t arrayStart = content.find("[", aiPos);
					size_t arrayEnd = content.find("]", arrayStart);
					if (arrayStart != std::string::npos && arrayEnd != std::string::npos) {
						std::string aiArray = content.substr(arrayStart, arrayEnd - arrayStart + 1);
						
						// Parse each AI entry
						size_t entryPos = 0;
						while ((entryPos = aiArray.find("{", entryPos)) != std::string::npos) {
							size_t entryEnd = aiArray.find("}", aiArray.find("}", aiArray.find("}", aiArray.find("}", entryPos) + 1) + 1) + 1); // skip nested braces (approximate)
							if (entryEnd == std::string::npos) entryEnd = aiArray.length();
							
							// A better way to find the end of the JSON object matching the open brace
							int braceCount = 0;
							for (size_t i = entryPos; i < aiArray.length(); ++i) {
								if (aiArray[i] == '{') braceCount++;
								else if (aiArray[i] == '}') {
									braceCount--;
									if (braceCount == 0) {
										entryEnd = i;
										break;
									}
								}
							}

							std::string entry = aiArray.substr(entryPos, entryEnd - entryPos + 1);
							
							AIData aiData;
							aiData.pos = glm::dvec3(0,0,0);
							aiData.graphPos = glm::dvec3(0,0,0);
							aiData.rotation = 0.0;
							aiData.team = 0;
							
							// Helper lambda to extract string
							auto extractString = [](const std::string& str, const std::string& key) -> std::string {
								size_t pos = str.find("\"" + key + "\"");
								if (pos == std::string::npos) return "";
								size_t colon = str.find(":", pos);
								size_t qStart = str.find("\"", colon);
								size_t qEnd = str.find("\"", qStart + 1);
								if (qStart != std::string::npos && qEnd != std::string::npos) {
									return str.substr(qStart + 1, qEnd - qStart - 1);
								}
								return "";
							};
							
							// Helper lambda to extract number as string
							auto extractNumStr = [](const std::string& str, const std::string& key) -> std::string {
								size_t pos = str.find("\"" + key + "\"");
								if (pos == std::string::npos) return "";
								size_t colon = str.find(":", pos);
								size_t end = str.find_first_of(",}\n\r", colon + 1);
								if (colon != std::string::npos && end != std::string::npos) {
									std::string val = str.substr(colon + 1, end - colon - 1);
									val.erase(0, val.find_first_not_of(" \t")); // ltrim
									val.erase(val.find_last_not_of(" \t") + 1); // rtrim
									return val;
								}
								return "";
							};

							aiData.type = extractString(entry, "Type");
							aiData.name = extractString(entry, "Name");
							aiData.soldierId = extractNumStr(entry, "SoldierId");
							aiData.aiId = extractNumStr(entry, "AIId");
							
							// Extract Team
							std::string teamStr = extractNumStr(entry, "Team");
							if (!teamStr.empty()) aiData.team = std::stoi(teamStr);

							// Extract Rotation
							std::string rotStr = extractNumStr(entry, "Rotation");
							if (!rotStr.empty()) aiData.rotation = std::stod(rotStr);
							
							// Extract positions using blocks
							size_t posBlock = entry.find("\"Position\"");
							if (posBlock != std::string::npos) {
								size_t endBlock = entry.find("}", posBlock);
								std::string blk = entry.substr(posBlock, endBlock - posBlock);
								std::string xStr = extractNumStr(blk, "X");
								std::string yStr = extractNumStr(blk, "Y");
								std::string zStr = extractNumStr(blk, "Z");
								if (!xStr.empty()) aiData.pos.x = std::stod(xStr);
								if (!yStr.empty()) aiData.pos.y = std::stod(yStr);
								if (!zStr.empty()) aiData.pos.z = std::stod(zStr);
							}
							
							size_t modelBlock = entry.find("\"Model\"");
							if (modelBlock != std::string::npos) {
								size_t endBlock = entry.find("}", modelBlock);
								std::string blk = entry.substr(modelBlock, endBlock - modelBlock);
								aiData.modelId = extractString(blk, "ID");
							}
							
							size_t graphBlock = entry.find("\"Graph\"");
							if (graphBlock != std::string::npos) {
								size_t endBlock = entry.find("}", graphBlock); // wait, graph contains Position, so endBlock must be calculated carefully
								// actually let's just use the whole graph substring since keys are unique
								aiData.graphId = extractNumStr(entry.substr(graphBlock), "ID");
								aiData.graphName = extractString(entry.substr(graphBlock), "Name");
								
								size_t graphPosBlock = entry.find("\"Position\"", graphBlock);
								if (graphPosBlock != std::string::npos) {
									size_t gEndBlock = entry.find("}", graphPosBlock);
									std::string blk = entry.substr(graphPosBlock, gEndBlock - graphPosBlock);
									std::string xStr = extractNumStr(blk, "X");
									std::string yStr = extractNumStr(blk, "Y");
									std::string zStr = extractNumStr(blk, "Z");
									if (!xStr.empty()) aiData.graphPos.x = std::stod(xStr);
									if (!yStr.empty()) aiData.graphPos.y = std::stod(yStr);
									if (!zStr.empty()) aiData.graphPos.z = std::stod(zStr);
								}
							}
							
							size_t weaponBlock = entry.find("\"Weapons\"");
							if (weaponBlock != std::string::npos) {
								size_t priBlock = entry.find("\"Primary\"", weaponBlock);
								if (priBlock != std::string::npos) {
									size_t endBlock = entry.find("}", priBlock);
									std::string blk = entry.substr(priBlock, endBlock - priBlock);
									aiData.primaryWeapon = extractString(blk, "Name");
									aiData.primaryAmmo = extractNumStr(blk, "Ammo");
								}
								size_t secBlock = entry.find("\"Secondary\"", weaponBlock);
								if (secBlock != std::string::npos) {
									size_t endBlock = entry.find("}", secBlock);
									std::string blk = entry.substr(secBlock, endBlock - secBlock);
									aiData.secondaryWeapon = extractString(blk, "Name");
									aiData.secondaryAmmo = extractNumStr(blk, "Ammo");
								}
							}

							bool isValid = !aiData.soldierId.empty();

							if (isValid) {
								aiDataList.push_back(aiData);
							}
							
							entryPos = entryEnd + 1;
						}
					}
				}
			}
		}
		Logger::Get().Log(LogLevel::INFO, "[App] Found " + std::to_string(aiDataList.size()) + " valid AI entries in JSON file");
	}


	// Add or update AI models in level objects
	auto& objects = level_.GetLevelObjects().GetObjects();
	int addedCount = 0;
	int updatedCount = 0;

	// Load Cutscene Graph IDs and populate cutscene_soldier_ids_
	std::set<std::string> cutsceneGraphIds = LoadCutsceneGraphIds(level_no);
	for (const auto& aiData : aiDataList) {
		if (cutsceneGraphIds.count(aiData.graphId)) {
			cutscene_soldier_ids_.insert(aiData.soldierId);
			Logger::Get().Log(LogLevel::INFO, "[App] Added soldier taskId " + aiData.soldierId + " to cutscene list due to Graph ID " + aiData.graphId);
		}
	}

	for (const auto& aiData : aiDataList) {
		// Search for existing object with this taskId
		LevelObject* existingObj = nullptr;
		for (auto& obj : objects) {
			if (obj.taskId == aiData.soldierId && !obj.taskId.empty()) {
				existingObj = &obj;
				break;
			}
		}

		if (existingObj) {
			// Skip sync for cutscene soldiers to preserve authored QSC state
			if (cutscene_soldier_ids_.count(existingObj->taskId)) {
				Logger::Get().Log(LogLevel::INFO, "[App] Skipping AI sync for cutscene soldier: " + aiData.modelId + " taskId=" + aiData.soldierId);
				continue;
			}
			// Update existing object with AI metadata
			existingObj->aiId = aiData.aiId;
			existingObj->graphId = aiData.graphId;
			existingObj->graphName = aiData.graphName;
			existingObj->graphPos = aiData.graphPos;
			existingObj->team = aiData.team;
			existingObj->primaryWeapon = aiData.primaryWeapon;
			existingObj->primaryAmmo = aiData.primaryAmmo;
			existingObj->secondaryWeapon = aiData.secondaryWeapon;
			existingObj->secondaryAmmo = aiData.secondaryAmmo;
			
			// If the position in JSON differs from QSC, update it 
			// (JSON is usually more up-to-date for AI state)
			existingObj->pos = aiData.pos;
			existingObj->modified = true; // Mark as modified so it gets saved
			
			updatedCount++;
			Logger::Get().Log(LogLevel::INFO, "[App] Updated existing AI object: " + aiData.modelId + " taskId=" + aiData.soldierId);
		} else {
			// Create new LevelObject for this AI entry
			LevelObject newObj;
			newObj.name = aiData.name; // Use JSON name
			newObj.modelId = aiData.modelId;
			newObj.taskId = aiData.soldierId;
			newObj.aiId = aiData.aiId;
			newObj.graphId = aiData.graphId;
			newObj.type = aiData.type;
			newObj.graphName = aiData.graphName;
			newObj.graphPos = aiData.graphPos;
			newObj.team = aiData.team;
			newObj.modified = true; // New AI objects are definitely modified
			newObj.rot.z = aiData.rotation;
			newObj.primaryWeapon = aiData.primaryWeapon;
			newObj.primaryAmmo = aiData.primaryAmmo;
			newObj.secondaryWeapon = aiData.secondaryWeapon;
			newObj.secondaryAmmo = aiData.secondaryAmmo;
			newObj.pos = aiData.pos;
			newObj.original_pos = newObj.pos;
			newObj.rot = glm::dvec3(0.0, 0.0, aiData.rotation);
			newObj.original_rot = newObj.rot;
			newObj.isBuilding = false;
			newObj.snap_z_offset = 0.0;
			newObj.scale = 1.0f;

			objects.push_back(newObj);
			addedCount++;
			Logger::Get().Log(LogLevel::INFO, "[App] Added new AI model: " + aiData.modelId + " (" + aiData.type + ") at (" + 
				std::to_string(aiData.pos.x) + ", " + std::to_string(aiData.pos.y) + ", " + std::to_string(aiData.pos.z) + ")");
		}
	}

	Logger::Get().Log(LogLevel::INFO, "[App] AI sync: " + std::to_string(updatedCount) + " updated, " + std::to_string(addedCount) + " added.");

	Logger::Get().Log(LogLevel::INFO, "[App] Added " + std::to_string(addedCount) + " new AI models to level " + std::to_string(level_no));
	Logger::Get().Log(LogLevel::INFO, "[App] ==========================================");
	Logger::Get().Log(LogLevel::INFO, "[App] LoadAIModelsFromFolder() COMPLETE");
	Logger::Get().Log(LogLevel::INFO, "[App] ==========================================");
}

void App::SaveCurrentLevel() {
	try {
		Logger::Get().Log(LogLevel::INFO, "[App] SaveCurrentLevel() called");
		level_.SaveChanges();
		Logger::Get().Log(LogLevel::INFO, "[App] Calling SaveAndCompile()");
		SaveAndCompile();
	}
	catch (const std::exception& e) {
		std::string errorMsg = "Error saving level:\n" + std::string(e.what());
		Utils::LogAndShowError(errorMsg, "IGI Editor - Error");
		Logger::Get().Log(LogLevel::ERR, errorMsg);
	}
	catch (...) {
		std::string errorMsg = "Unknown error saving level";
		Utils::LogAndShowError(errorMsg, "IGI Editor - Error");
		Logger::Get().Log(LogLevel::ERR, errorMsg);
	}
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

void App::SetDrawParts(int parts) {
	draw_params_.draw_parts_ = parts;
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
void App::Input_OnMouseWheel(int wheel, int direction, int x, int y) {
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
	// Update mouse position first so EditorProcessClick uses correct coords
	mouse_state_.prior_x_ = x;
	mouse_state_.prior_y_ = y;

	if (button == GLUT_LEFT_BUTTON) {
		if (GLUT_DOWN == state) {
			mouse_state_.left_button_down_ = true;
			
			if (task_editor_open_) {
				int box_x = (window_state_.viewport_width_ - edit_box_w_) / 2;
				int box_y = (window_state_.viewport_height_ - edit_box_h_) / 2;
				
				// Check Save button click first!
				const int save_btn_w = 84;
				const int save_btn_h = 26;
				const int save_btn_x = box_x + edit_box_w_ - save_btn_w - 14;
				const int save_btn_y_opengl = box_y + edit_box_h_ - 40;
				int btn_y1 = window_state_.viewport_height_ - (save_btn_y_opengl + save_btn_h);
				int btn_y2 = window_state_.viewport_height_ - save_btn_y_opengl;

				if (x >= save_btn_x && x <= save_btn_x + save_btn_w && y >= btn_y1 && y <= btn_y2) {
					// Trigger Save!
					if (selected_object_index_ >= 0) {
						auto& obj = level_.GetLevelObjects().GetObjects()[selected_object_index_];
						std::string finalStr = edit_string_;
						finalStr.erase(std::remove(finalStr.begin(), finalStr.end(), '\r'), finalStr.end());
						finalStr.erase(std::remove(finalStr.begin(), finalStr.end(), '\n'), finalStr.end());
						
						obj.qscLine = finalStr;
						level_.GetLevelObjects().ParseTaskLine(obj.qscLine, obj);
						
						int savedIndex = selected_object_index_;
						level_.SaveAndReloadObjects();
						auto& objects = level_.GetLevelObjects().GetObjects();
						if (objects.empty()) selected_object_index_ = -1;
						else selected_object_index_ = std::min(savedIndex, (int)objects.size() - 1);
						Logger::Get().Log(LogLevel::INFO, "[App] Saved task changes via Save Button click.");
					}
					task_editor_open_ = false;
					edit_cursor_pos_ = 0;
					edit_scroll_x_ = 0;
					return;
				}

				if (x >= box_x && x <= box_x + edit_box_w_ && y >= box_y && y <= box_y + edit_box_h_) {
					int rel_x = x - (box_x + 20);
					int char_w = 9;
					edit_cursor_pos_ = std::max(0, std::min((int)edit_string_.size(), edit_scroll_x_ + std::max(0, rel_x) / char_w));
					if (!(glutGetModifiers() & GLUT_ACTIVE_SHIFT)) {
						edit_selection_start_ = edit_cursor_pos_;
					}
					edit_selection_end_ = edit_cursor_pos_;
					edit_dragging_ = true;
					return;
				}
			}

			if (pause_mode_) {
				// *** MUST match renderer.cpp pause menu constants exactly ***
				const int menu_w = 380;
				const int menu_h = 280;
				const int menu_x = (window_state_.viewport_width_  - menu_w) / 2;
				const int screen_menu_top = (window_state_.viewport_height_ - menu_h) / 2;

				// Buttons start at screen_menu_top + 80, spaced 35px
				auto btn_hit = [&](int idx, int mouse_y) -> bool {
					int btn_y = screen_menu_top + 80 + idx * 35;
					return (mouse_y >= btn_y - 15 && mouse_y <= btn_y + 15);
				};

				if (x >= menu_x && x <= menu_x + menu_w &&
				    y >= screen_menu_top && y <= screen_menu_top + menu_h) {
					mouse_state_.left_button_down_ = false;
					if      (btn_hit(0, y)) { TogglePauseMenu(); }                    // Resume
					else if (btn_hit(1, y)) { show_debug_ = !show_debug_; }           // Debug
					else if (btn_hit(2, y)) { ResetLevel(); TogglePauseMenu(); }      // Reset Level
					else if (btn_hit(3, y)) { SaveCurrentLevel(); }                   // Save Level
					else if (btn_hit(4, y)) { exit(0); }                              // Quit
				}
				return; // Block all other interactions while paused
			}

			// Priority: TreeView HUD interaction
			if (show_hud_ && x < 350) { // Tree is on the left
				ProcessTreeViewClick(x, y);

			}
			else if (edit_mode_) {
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

			if (window_state_.cursor_visible_) {
				input_.mouse_delta_x_ = 0;
				input_.mouse_delta_y_ = 0;
			}
		}
	}
}

void App::Input_OnMotion(int x, int y) {
	int dx = x - mouse_state_.prior_x_;
	int dy = y - mouse_state_.prior_y_;

	bool enableCameraMode = Utils::IsKeyBindingPressed(Config::Get().keyEnableCamera);

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
		// Priority 2: 3D Object Hover
		if (enableCameraMode) {
			int cx = window_state_.viewport_width_ >> 1;
			int cy = window_state_.viewport_height_ >> 1;
			hover_object_index_ = PickObjectAtScreenPos(cx, cy);
		} else {
			hover_object_index_ = PickObjectAtScreenPos(x, y);
		}
	}

	if (window_state_.cursor_visible_) {
		if (enableCameraMode) {
			glutSetCursor(GLUT_CURSOR_NONE);
		} else {
			glutSetCursor(GLUT_CURSOR_LEFT_ARROW);
		}
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
}

void App::Input_OnSpecial(int key, int x, int y) {
	auto& config = Config::Get();

	if (task_editor_open_) {
		// Task editor handles its own input or blocks others
		if (key == GLUT_KEY_LEFT) {
			edit_cursor_pos_ = std::max(0, edit_cursor_pos_ - 1);
		}
		if (key == GLUT_KEY_RIGHT) {
			edit_cursor_pos_ = std::min((int)edit_string_.size(), edit_cursor_pos_ + 1);
		}
		if (key == GLUT_KEY_HOME) {
			edit_cursor_pos_ = 0;
		}
		if (key == GLUT_KEY_END) {
			edit_cursor_pos_ = (int)edit_string_.size();
		}

		int visibleChars = std::max(1, (edit_box_w_ - 40) / 9);
		if (edit_cursor_pos_ < edit_scroll_x_) {
			edit_scroll_x_ = edit_cursor_pos_;
		} else if (edit_cursor_pos_ > edit_scroll_x_ + visibleChars) {
			edit_scroll_x_ = edit_cursor_pos_ - visibleChars;
		}
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

	// Check configurable keybindings for special keys (F-keys, etc.)
	// Save
	if (Utils::IsKeyBindingPressed(config.keySave)) {
		SaveCurrentLevel();
		return;
	}

	// Reset Level
	if (Utils::IsKeyBindingPressed(config.keyResetLevel)) {
		ResetLevel();
		return;
	}

	// Debug
	if (Utils::IsKeyBindingPressed(config.keyDebug)) {
		show_debug_ = !show_debug_;
		return;
	}

	// Quit
	if (Utils::IsKeyBindingPressed(config.keyQuit)) {
		exit(0);
		return;
	}

	// Clip Mode
	if (Utils::IsKeyBindingPressed(config.keyClipMode)) {
		noclip_mode_ = !noclip_mode_;
		Logger::Get().Log(LogLevel::INFO, std::string("[App] Clip Mode (NoCollision) set to ") + (noclip_mode_ ? "ENABLED" : "DISABLED"));
		printf("Clip Mode (NoCollision): %s\n", noclip_mode_ ? "ENABLED" : "DISABLED");
		return;
	}

	// Reset Script (pause menu only)
	if (pause_mode_ && Utils::IsKeyBindingPressed(config.keyResetScript)) {
		ResetScript();
		TogglePauseMenu();
		return;
	}

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

	if (key == GLUT_KEY_F2) {
		terrain_edit_enabled_ = !terrain_edit_enabled_;
		printf("Terrain Editing: %s\n", terrain_edit_enabled_ ? "ENABLED" : "DISABLED");
		return;
	}

	// Reload Settings
	if (Utils::IsKeyBindingPressed(config.keyReloadSettings)) {
		Config::Init(); // Re-read config.ini
		Logger::Get().Log(LogLevel::INFO, "[App] Settings reloaded from config.ini");
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

	if (key == GLUT_KEY_F11) {
		if (selected_object_index_ >= 0) {
			auto& obj = level_.GetLevelObjects().GetObjects()[selected_object_index_];
			viewer_.pos_ = glm::vec3(obj.pos);
			UpdateViewerVectors();
			printf("Teleported to Object [%d]\n", selected_object_index_);
		}
		return;
	}

	// Task Controls for special keys (INSERT, DELETE)
	if (Utils::IsKeyBindingPressed(config.keyCreateNewTask)) {
		CreateNewTask();
		return;
	}
	if (Utils::IsKeyBindingPressed(config.keyDeleteTask)) {
		DeleteSelectedTask();
		return;
	}
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

void App::Input_OnKeyboard(unsigned char key, int x, int y) {
	auto& config = Config::Get();

	if (task_editor_open_) {
		if (key == 27) { // ESC - Cancel and close (Discard edits!)
			task_editor_open_ = false;
			edit_cursor_pos_ = 0;
			edit_scroll_x_ = 0;
			return;
		}
		if (key == 8) { // Backspace
			int s = std::min(edit_selection_start_, edit_selection_end_);
			int e = std::max(edit_selection_start_, edit_selection_end_);
			if (s != e && s != -1) {
				edit_string_.erase(s, e - s);
				edit_cursor_pos_ = s;
				edit_selection_start_ = edit_selection_end_ = -1;
			} else if (edit_cursor_pos_ > 0 && !edit_string_.empty()) {
				edit_string_.erase(edit_cursor_pos_ - 1, 1);
				edit_cursor_pos_--;
			}
			return;
		}
		if (key == 13) { // Enter - Multi-line
			edit_string_.insert(edit_cursor_pos_, 1, '\n');
			edit_cursor_pos_++;
			return;
		}
		if (key == 1) { // Ctrl+A - Select All
			edit_selection_start_ = 0;
			edit_selection_end_ = (int)edit_string_.size();
			edit_cursor_pos_ = (int)edit_string_.size();
			return;
		}
		if (key == 3) { // Ctrl+C - Copy
			int s = std::min(edit_selection_start_, edit_selection_end_);
			int e = std::max(edit_selection_start_, edit_selection_end_);
			if (s != e && s != -1) {
				Utils::SetClipboardText(edit_string_.substr(s, e - s));
			} else if (!edit_string_.empty()) {
				Utils::SetClipboardText(edit_string_);
			}
			return;
		}
		if (key == 24) { // Ctrl+X - Cut
			int s = std::min(edit_selection_start_, edit_selection_end_);
			int e = std::max(edit_selection_start_, edit_selection_end_);
			if (s != e && s != -1) {
				Utils::SetClipboardText(edit_string_.substr(s, e - s));
				edit_string_.erase(s, e - s);
				edit_cursor_pos_ = s;
				edit_selection_start_ = edit_selection_end_ = -1;
			}
			return;
		}
		if (key == 22) { // Ctrl+V - Paste
			std::string pasteData = Utils::GetClipboardText();
			if (!pasteData.empty()) {
				int s = std::min(edit_selection_start_, edit_selection_end_);
				int e = std::max(edit_selection_start_, edit_selection_end_);
				if (s != e && s != -1) {
					edit_string_.erase(s, e - s);
					edit_cursor_pos_ = s;
				}
				edit_string_.insert(edit_cursor_pos_, pasteData);
				edit_cursor_pos_ += (int)pasteData.size();
				edit_selection_start_ = edit_selection_end_ = -1;
			}
			return;
		}
		if (key >= 32 && key <= 126) { // Printable characters
			int s = std::min(edit_selection_start_, edit_selection_end_);
			int e = std::max(edit_selection_start_, edit_selection_end_);
			if (s != e && s != -1) {
				edit_string_.erase(s, e - s);
				edit_cursor_pos_ = s;
				edit_selection_start_ = edit_selection_end_ = -1;
			}
			edit_string_.insert(edit_cursor_pos_, 1, key);
			edit_cursor_pos_++;

			// Live update from Task Editor to Object
			if (selected_object_index_ >= 0) {
				auto& obj = level_.GetLevelObjects().GetObjects()[selected_object_index_];
				obj.qscLine = edit_string_;
				level_.GetLevelObjects().ParseTaskLine(obj.qscLine, obj);
				obj.modified = true;
			}
			return;
		}

		// Update scroll to keep cursor visible - use 9px for mono font
		int visibleChars = std::max(1, (edit_box_w_ - 40) / 9);
		if (edit_cursor_pos_ < edit_scroll_x_) {
			edit_scroll_x_ = edit_cursor_pos_;
		} else if (edit_cursor_pos_ > edit_scroll_x_ + visibleChars) {
			edit_scroll_x_ = edit_cursor_pos_ - visibleChars;
		}

		return; // Consume all other keys while editor is open
	}

	// Task Controls (CTRL+C, CTRL+V, CTRL+I, etc.)
	if (Utils::IsKeyBindingPressed(config.keyCreateNewTask)) {
		CreateNewTask();
		return;
	}
	if (Utils::IsKeyBindingPressed(config.keyCopyTask)) {
		CopySelectedTask(true);
		return;
	}
	if (Utils::IsKeyBindingPressed(config.keyPasteTask)) {
		PasteTask();
		return;
	}
	if (Utils::IsKeyBindingPressed(config.keyDeleteTask)) {
		DeleteSelectedTask();
		return;
	}
	if (Utils::IsKeyBindingPressed(config.keyAssignTaskID)) {
		AssignTaskID();
		return;
	}

	// Open Task Editor on Enter if an object is selected
	if (key == 13 && !(glutGetModifiers() & GLUT_ACTIVE_ALT)) {
		if (selected_object_index_ >= 0) {
			auto& objects = level_.GetLevelObjects().GetObjects();
			if (selected_object_index_ < (int)objects.size()) {
				auto& obj = objects[selected_object_index_];
				task_editor_open_ = true;
				std::string line = obj.qscLine.empty() ? level_.GetLevelObjects().GenerateTaskLine(obj) : obj.qscLine;

				// Strip any newlines from the loaded line
				line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
				line.erase(std::remove(line.begin(), line.end(), '\n'), line.end());

				edit_string_ = line;
				edit_cursor_pos_ = (int)edit_string_.size();
				edit_selection_start_ = edit_selection_end_ = -1;
				edit_scroll_x_ = 0;
				Logger::Get().Log(LogLevel::INFO, "[App] Pressed Enter on task from tree and opened Task Editor.");
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
		TogglePauseMenu();
		return;
	}

	// Global shortcuts (work in both pause and normal mode)

	// Save
	if (Utils::IsKeyBindingPressed(config.keySave)) {
		SaveCurrentLevel();
	}

	// Reset Level
	if (Utils::IsKeyBindingPressed(config.keyResetLevel)) {
		ResetLevel();
	}

	// Debug
	if (Utils::IsKeyBindingPressed(config.keyDebug)) {
		show_debug_ = !show_debug_;
	}

	// Quit
	if (Utils::IsKeyBindingPressed(config.keyQuit)) {
		exit(0);
	}

	// Help
	if (Utils::IsKeyBindingPressed(config.keyHelp)) {
		show_help_ = !show_help_;
		if (show_help_) {
			if (!pause_mode_) {
				TogglePauseMenu();
			}
		} else {
			if (pause_mode_) {
				TogglePauseMenu();
			}
		}
	}

	// Task Controls (CTRL+C, CTRL+V, CTRL+I, etc.)
	if (Utils::IsKeyBindingPressed(config.keyCreateNewTask)) {
		CreateNewTask();
		return;
	}
	if (Utils::IsKeyBindingPressed(config.keyCopyTask)) {
		CopySelectedTask(true);
		return;
	}
	if (Utils::IsKeyBindingPressed(config.keyPasteTask)) {
		PasteTask();
		return;
	}
	if (Utils::IsKeyBindingPressed(config.keyDeleteTask)) {
		DeleteSelectedTask();
		return;
	}
	if (Utils::IsKeyBindingPressed(config.keyAssignTaskID)) {
		AssignTaskID();
		return;
	}

	if (pause_mode_) {
		// Reset Script (pause menu only)
		if (Utils::IsKeyBindingPressed(config.keyResetScript)) {
			ResetScript();
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

	// Object Manipulation from config.ini
	if (toupper(key) == toupper(config.keyRotateAlpha)) { input_.keys_ |= MK_MANIP_A; return; }
	if (toupper(key) == toupper(config.keyRotateBeta))  { input_.keys_ |= MK_MANIP_B; return; }
	if (toupper(key) == toupper(config.keyRotateGamma)) { input_.keys_ |= MK_MANIP_G; return; }
	if (toupper(key) == toupper(config.keySnapGround))  { 
        input_.keys_ |= MK_MANIP_S; 
        if (selected_object_index_ >= 0) UpdateMarkerManipulation();
        input_.keys_ &= ~MK_MANIP_S; 
        return; 
    }
	if (toupper(key) == toupper(config.keySnapObject))  { 
        input_.keys_ |= MK_MANIP_O; 
        if (selected_object_index_ >= 0) UpdateMarkerManipulation();
        input_.keys_ &= ~MK_MANIP_O; 
        return; 
    }
	if (key == ' ') { input_.keys_ |= MK_MANIP_SPACE; return; }

	if (key == 't' || key == 'T') {
		level_.TeleportToHMP(viewer_.pos_);
		viewer_.pitch_ = -30.0f; // Look down at the terrain
		UpdateViewerVectors();
		printf("Teleported to Height Map Zone\n");
		return;
	}

	// HUD toggle removed as per request

	if (key == 'h' || key == 'H') {
		int modifiers = glutGetModifiers();
		bool ctrl = (modifiers & GLUT_ACTIVE_CTRL);
		if (ctrl || !pause_mode_) {
			show_help_ = !show_help_;
			if (!pause_mode_) {
				// If not in pause mode, pause when showing help
				if (show_help_) {
					pause_mode_ = true;
				}
			}
		}
		return;
	}

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


	if (key == '\t') {
		LevelObjects& lo = level_.GetLevelObjects();
		if (!lo.GetObjects().empty()) {
			selected_object_index_ = (selected_object_index_ + 1) % (int)lo.GetObjects().size();
			printf("Selected Object: %d / %d\n", selected_object_index_, (int)lo.GetObjects().size());
		}
		return;
	}

	// HandleMarkerInput(key);

}


#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#endif

void App::ResetLevel() {
	int levelNo = level_.GetLevelNo();

	printf("Resetting Level %d - restore objects.qsc and objects.qvm from QFiles\n", levelNo);

	// Force kill any running game instance to release file locks on objects.qvm
#ifdef _WIN32
	{
		HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (hSnap != INVALID_HANDLE_VALUE) {
			PROCESSENTRY32 pe;
			pe.dwSize = sizeof(pe);
			if (Process32First(hSnap, &pe)) {
				do {
					if (_wcsicmp(pe.szExeFile, L"igi.exe") == 0) {
						HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
						if (hProc) {
							TerminateProcess(hProc, 0);
							CloseHandle(hProc);
							printf("[App] Terminated running game instance 'igi.exe' to unlock files.\n");
						}
					}
				} while (Process32Next(hSnap, &pe));
			}
			CloseHandle(hSnap);
		}
	}
#endif

	std::string baseQFiles = Config::Get().filesPath;
	if (baseQFiles.empty()) {
		baseQFiles = Config::Get().qEditorPath + "\\QFiles";
	}

	// Step 1: Restore objects.qsc from QFiles to exe directory
	char srcQsc[1024];
	Str_SPrintf(srcQsc, 1024, "%s\\IGI_QSC\\missions\\location0\\level%d\\objects.qsc", baseQFiles.c_str(), levelNo);

	std::string exeDir = Utils::GetExeDirectory();
	char dstQsc[1024];
	Str_SPrintf(dstQsc, 1024, "%s\\objects.qsc", exeDir.c_str());

	printf("Step 1: Copying objects.qsc from %s to %s\n", srcQsc, dstQsc);

	try {
		if (std::filesystem::exists(srcQsc)) {
			// Force permissions to allow overwrite/delete
			if (std::filesystem::exists(dstQsc)) {
				std::filesystem::permissions(dstQsc, 
					std::filesystem::perms::owner_all | std::filesystem::perms::group_all | std::filesystem::perms::others_all,
					std::filesystem::perm_options::replace);
				std::filesystem::remove(dstQsc);
			}
			std::filesystem::copy_file(srcQsc, dstQsc, std::filesystem::copy_options::overwrite_existing);
			printf("Objects.qsc copied successfully.\n");
		}
		else {
			printf("Error: Source file %s does not exist.\n", srcQsc);
		}
	}
	catch (const std::exception& e) {
		printf("ResetLevel step 1 error: %s\n", e.what());
	}

	// Step 2: Restore objects.qvm from QFiles to IGIPath
	ConfigData& cfg = Config::Get();
	char srcQvm[1024];
	Str_SPrintf(srcQvm, 1024, "%s\\IGI_QVM\\missions\\location0\\level%d\\objects.qvm", baseQFiles.c_str(), levelNo);

	char dstQvm[1024];
	Str_SPrintf(dstQvm, 1024, "%s\\missions\\location0\\level%d\\objects.qvm", cfg.igiPath.c_str(), levelNo);

	printf("Step 2: Copying objects.qvm from %s to %s\n", srcQvm, dstQvm);

	try {
		if (std::filesystem::exists(srcQvm)) {
			std::filesystem::create_directories(std::filesystem::path(dstQvm).parent_path());
			// Force permissions to allow overwrite/delete
			if (std::filesystem::exists(dstQvm)) {
				std::filesystem::permissions(dstQvm, 
					std::filesystem::perms::owner_all | std::filesystem::perms::group_all | std::filesystem::perms::others_all,
					std::filesystem::perm_options::replace);
				std::filesystem::remove(dstQvm);
			}
			std::filesystem::copy_file(srcQvm, dstQvm, std::filesystem::copy_options::overwrite_existing);
			printf("QVM copied successfully to game path.\n");
		}
		else {
			printf("Error: Source QVM not found at %s\n", srcQvm);
		}
	}
	catch (const std::exception& e) {
		printf("ResetLevel step 2 error: %s\n", e.what());
	}

	// Reload level after reset
	LoadLevel(levelNo);
	// Snap objects to terrain after level reset
	SnapObjectsToTerrain();
}

void App::ResetScript() {
	int levelNo = level_.GetLevelNo();

	printf("Resetting Script for Level %d - copy objects.qsc from QFiles to exe directory\n", levelNo);

	std::string baseQFiles = Config::Get().filesPath;
	if (baseQFiles.empty()) {
		baseQFiles = Config::Get().qEditorPath + "\\QFiles";
	}

	// Source: QEditor IGI_QSC
	char srcQsc[1024];
	Str_SPrintf(srcQsc, 1024, "%s\\IGI_QSC\\missions\\location0\\level%d\\objects.qsc", baseQFiles.c_str(), levelNo);

	// Destination: exe directory
	std::string exeDir = Utils::GetExeDirectory();
	char dstQsc[1024];
	Str_SPrintf(dstQsc, 1024, "%s\\objects.qsc", exeDir.c_str());

	printf("Resetting objects.qsc from %s to %s\n", srcQsc, dstQsc);

	try {
		if (std::filesystem::exists(srcQsc)) {
			std::filesystem::copy_file(srcQsc, dstQsc, std::filesystem::copy_options::overwrite_existing);
			printf("Script reset successful.\n");
		}
		else {
			printf("Error: Source file %s does not exist.\n", srcQsc);
		}
	}
	catch (const std::exception& e) {
		printf("ResetScript error: %s\n", e.what());
	}

	// Reload the level to apply changes
	LoadLevel(levelNo);
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
	// Object Manipulation from config.ini
	if (toupper(key) == toupper(config.keyRotateAlpha)) { input_.keys_ &= ~MK_MANIP_A; }
	if (toupper(key) == toupper(config.keyRotateBeta))  { input_.keys_ &= ~MK_MANIP_B; }
	if (toupper(key) == toupper(config.keyRotateGamma)) { input_.keys_ &= ~MK_MANIP_G; }
	if (toupper(key) == toupper(config.keySnapGround))  { input_.keys_ &= ~MK_MANIP_S; }
	if (toupper(key) == toupper(config.keySnapObject))  { input_.keys_ &= ~MK_MANIP_O; }
	if (key == ' ') { input_.keys_ &= ~MK_MANIP_SPACE; }
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
		hover_object_index_ = PickObjectAtScreenPos(mouse_state_.prior_x_, mouse_state_.prior_y_);
		float ground_z = 0.0f;
		level_.GetTerrainZ(viewer_.pos_.x, viewer_.pos_.y, ground_z);
		Renderer::task_tree_view_params_s task_tree_view = {
			.show_hud_ = true,
			.status_msg_ = status_message_,
			.pause_mode_ = true,
			.show_debug_ = show_debug_,
			.show_help_ = show_help_,
			.edit_mode_ = edit_mode_,
			.terrain_edit_enabled_ = terrain_edit_enabled_,
			.selected_object_index_ = selected_object_index_,
			.hover_object_index_ = hover_object_index_,
			.hover_tree_index_ = hover_tree_index_,
			.mouse_x_ = mouse_state_.prior_x_,
			.mouse_y_ = mouse_state_.prior_y_,
			.tree_scroll_offset = tree_scroll_offset_,
			.tree_decl_expanded = tree_decl_expanded_,
			.level_objects_ = &level_.GetLevelObjects(),
			.task_editor_open_ = task_editor_open_,
			.edit_string_ = edit_string_,
			.edit_cursor_pos_ = edit_cursor_pos_,
			.edit_selection_start_ = edit_selection_start_,
			.edit_selection_end_ = edit_selection_end_,
			.edit_box_w_ = edit_box_w_,
			.edit_box_h_ = edit_box_h_,
			.edit_scroll_x_ = edit_scroll_x_,
			.enable_camera_mode_ = Utils::IsKeyBindingPressed(Config::Get().keyEnableCamera)
		};
		draw_params_.level_objects_ = &level_.GetLevelObjects();
		draw_params_.selected_object_index_ = selected_object_index_;
		renderer_.Draw(draw_params_, task_tree_view);

		glutSwapBuffers();
		return;
	}

	frame_++;
	frame_ %= 0xFFFFFFFF;	// reserve value 0xFFFFFFFF (-1) for INVALID_FRAME

	ProcessInput(delta_seconds);

	if (edit_mode_ && terrain_edit_enabled_ && mouse_state_.left_button_down_) {
		EditorProcessClick();
	}

	UpdateViewDefine();
	hover_object_index_ = PickObjectAtScreenPos(mouse_state_.prior_x_, mouse_state_.prior_y_);

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
	level_.GetTerrainZ(viewer_.pos_.x, viewer_.pos_.y, ground_z);

	Renderer::task_tree_view_params_s task_tree_view = {
		.show_hud_ = show_hud_,
		.status_msg_ = status_message_,
		.pause_mode_ = pause_mode_,
		.show_debug_ = show_debug_,
		.show_help_ = show_help_,
		.edit_mode_ = edit_mode_,
		.terrain_edit_enabled_ = terrain_edit_enabled_,
		.selected_object_index_ = selected_object_index_,
		.hover_object_index_ = hover_object_index_,
		.hover_tree_index_ = hover_tree_index_,
		.mouse_x_ = mouse_state_.prior_x_,
		.mouse_y_ = mouse_state_.prior_y_,
		.tree_scroll_offset = tree_scroll_offset_,
		.tree_decl_expanded = tree_decl_expanded_,
		.level_objects_ = &level_.GetLevelObjects(),
		.task_editor_open_ = task_editor_open_,
		.edit_string_ = edit_string_,
		.edit_cursor_pos_ = edit_cursor_pos_,
		.edit_selection_start_ = edit_selection_start_,
		.edit_selection_end_ = edit_selection_end_,
		.edit_box_w_ = edit_box_w_,
		.edit_box_h_ = edit_box_h_,
		.edit_scroll_x_ = edit_scroll_x_,
		.enable_camera_mode_ = Utils::IsKeyBindingPressed(Config::Get().keyEnableCamera)
	};


	renderer_.Draw(draw_params_, task_tree_view);


	glutSwapBuffers();
}

void App::ProcessInput(float delta_seconds) {
	// Safety check: ensure level is loaded before processing movement
	if (level_.GetLevelNo() == 0) {
		// Level not loaded, skip input processing
		input_.mouse_delta_x_ = 0;
		input_.mouse_delta_y_ = 0;
		return;
	}
	
	bool enableCameraMode = Utils::IsKeyBindingPressed(Config::Get().keyEnableCamera);
	
	// Update cursor based on mode
	if (pause_mode_) {
		glutSetCursor(GLUT_CURSOR_LEFT_ARROW);
	} else {
		glutSetCursor(enableCameraMode ? GLUT_CURSOR_NONE : GLUT_CURSOR_LEFT_ARROW);
	}

	if (!edit_mode_ || enableCameraMode) {
		viewer_.yaw_ += -input_.mouse_delta_x_ * MOUSE_SENSITIVE;
		viewer_.pitch_ += -input_.mouse_delta_y_ * MOUSE_SENSITIVE;
	}

	input_.mouse_delta_x_ = 0;
	input_.mouse_delta_y_ = 0;

	UpdateViewerVectors();

	if (viewer_.clip_to_z_ && !enableCameraMode) {

		float z0 = viewer_.pos_.z - VIEW_HEIGHT;
		float friction = viewer_.move_speed_ * 2.0f;

		// Check configured bindings for camera movement (which override standard keys if pressed)
		bool is_fwd = (input_.keys_ & MK_FORWARD) || Utils::IsKeyBindingPressed(Config::Get().keyMoveCameraForward);
		bool is_bwd = (input_.keys_ & MK_BACKWARD) || Utils::IsKeyBindingPressed(Config::Get().keyMoveCameraBackward);
		bool is_left = (input_.keys_ & MK_LEFT);
		bool is_right = (input_.keys_ & MK_RIGHT);

		// check movement
		if (is_fwd) {
			viewer_.velocity_.x = viewer_.forward_.x * viewer_.move_speed_;
			viewer_.velocity_.y = viewer_.forward_.y * viewer_.move_speed_;
		}

		if (is_bwd) {
			viewer_.velocity_.x = viewer_.forward_.x * -viewer_.move_speed_;
			viewer_.velocity_.y = viewer_.forward_.y * -viewer_.move_speed_;
		}

		if (is_left) {
			viewer_.velocity_.x = viewer_.right_.x * -viewer_.move_speed_;
			viewer_.velocity_.y = viewer_.right_.y * -viewer_.move_speed_;
		}

		if (is_right) {
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
		glm::vec3 next_pos = viewer_.pos_ + move_delta;

        if (!CheckCollision(next_pos)) {
		    viewer_.pos_ = next_pos;
        } else {
            // Sliding logic (op0f;
            viewer_.velocity_.y = 0.0f;
        }

		if (viewer_.velocity_.z <= 0.0f) {

			float ret_z = 0.0f;
			bool ok = level_.GetTerrainZ(viewer_.pos_.x, viewer_.pos_.y, ret_z);

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

		bool is_fwd = (input_.keys_ & MK_FORWARD) || Utils::IsKeyBindingPressed(Config::Get().keyMoveCameraForward);
		bool is_bwd = (input_.keys_ & MK_BACKWARD) || Utils::IsKeyBindingPressed(Config::Get().keyMoveCameraBackward);
		bool is_left = (input_.keys_ & MK_LEFT);
		bool is_right = (input_.keys_ & MK_RIGHT);

		// check movement - direct position update for free mode (NO collision)
		if (is_fwd) {
			viewer_.pos_ += viewer_.forward_ * viewer_.move_speed_ * delta_seconds;
		}
		if (is_bwd) {
			viewer_.pos_ -= viewer_.forward_ * viewer_.move_speed_ * delta_seconds;
		}
		if (is_left) {
			viewer_.pos_ -= viewer_.right_ * viewer_.move_speed_ * delta_seconds;
		}
		if (is_right) {
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
	show_hud_ = true;
}

bool App::GetShowHUD() const {
	return show_hud_;
}

void App::SetShowHUD(bool show) {
	show_hud_ = true;
}

void App::ToggleEditMode() {
    // Logic removed as requested
}

bool App::GetEditMode() const {
	return true; // Always true
}

void App::SetEditMode(bool enabled) {
    // Logic removed as requested
}

void App::SetTerrainEditEnabled(bool enabled) {
	terrain_edit_enabled_ = enabled;
}

bool App::GetTerrainEditEnabled() const {
	return terrain_edit_enabled_;
}

void App::TogglePauseMenu() {
	pause_mode_ = !pause_mode_;
	// cursor_visible_ stays TRUE always — camera lock is handled dynamically in Input_OnMotion.
	// Hiding the cursor permanently caused the "mouse stuck" bug after resuming.
	window_state_.cursor_visible_ = true;
	if (pause_mode_) {
		// Opening pause menu: show arrow cursor and clear any accumulated deltas
		glutSetCursor(GLUT_CURSOR_LEFT_ARROW);
	} else {
		// Closing pause menu: reset mouse state so no stale drag occurs
		input_.mouse_delta_x_ = 0;
		input_.mouse_delta_y_ = 0;
		mouse_state_.left_button_down_ = false;
		skip_input_on_motion_once_ = false;
		// Cursor type will be set correctly on next Input_OnMotion call
		glutSetCursor(GLUT_CURSOR_LEFT_ARROW);
	}
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

	LevelObjects& lo = level_.GetLevelObjects();
	std::vector<LevelObject>& objects = lo.GetObjects();

	if (terrain_edit_enabled_) {
		// Terrain edit mode: build ray from camera through mouse and edit terrain
		glm::dmat4 proj_matrix = glm::perspective(
			(double)view_define_.fovy_,
			(double)window_state_.viewport_width_ / (double)window_state_.viewport_height_,
			(double)view_define_.render_z_near_,
			(double)view_define_.render_z_far_
		);
		// Use actual camera position in world units, no extra scale
		glm::dmat4 mat_view = glm::lookAt(
			glm::dvec3(view_define_.pos_),
			glm::dvec3(view_define_.pos_) + glm::dvec3(view_define_.forward_),
			glm::dvec3(view_define_.up_));
		glm::dvec4 viewport(0.0, 0.0, (double)window_state_.viewport_width_, (double)window_state_.viewport_height_);

		double winX = (double)mouse_state_.prior_x_;
		double winY = (double)window_state_.viewport_height_ - (double)mouse_state_.prior_y_;

		glm::dvec3 start_pos = glm::unProject(glm::dvec3(winX, winY, 0.0), mat_view, proj_matrix, viewport);
		glm::dvec3 end_pos = glm::unProject(glm::dvec3(winX, winY, 1.0), mat_view, proj_matrix, viewport);
		glm::vec3 ray_origin = (glm::vec3)start_pos;
		glm::vec3 ray_dir = glm::normalize((glm::vec3)(end_pos - start_pos));

		printf("EditorClick: Mouse(%.0f, %.0f), RayDir(%.2f, %.2f, %.2f)\n", winX, winY, ray_dir.x, ray_dir.y, ray_dir.z);
		level_.EditorRaycastAndModify(ray_origin, ray_dir, edit_brush_);
		return;
	}

	// Object edit mode: select the object under the mouse cursor
	int pickedObject = PickObjectAtScreenPos(mouse_state_.prior_x_, mouse_state_.prior_y_);
	if (pickedObject >= 0 && pickedObject < (int)objects.size()) {
		selected_object_index_ = pickedObject;
		const LevelObject& obj = objects[pickedObject];
		
		// Auto-expand tree for the selected object
		int currentIdx = pickedObject;
		while (currentIdx != -1) {
			int parentIdx = objects[currentIdx].parentIndex;
			if (parentIdx != -1) {
				objects[parentIdx].expanded = true;
			}
			currentIdx = parentIdx;
		}

		Logger::Get().Log(LogLevel::INFO, "[App] Selected object index=" + std::to_string(pickedObject) + " model=" + obj.modelId + " type=" + (obj.isBuilding ? "building" : "object"));
		printf("Selected Object [%d]: %s (%s)\n", selected_object_index_, 
			objects[selected_object_index_].name.c_str(), objects[selected_object_index_].modelId.c_str());
		printf("  Pos: (%.0f, %.0f, %.0f)\n", (double)obj.pos.x, (double)obj.pos.y, (double)obj.pos.z);
		printf("  Rot (Alpha/Beta/Gamma): (%.2f, %.2f, %.2f)\n", (double)obj.rot.x, (double)obj.rot.y, (double)obj.rot.z);
		printf("  Scale: %.2f\n", obj.scale);
		marker_manip_.start_x_ = mouse_state_.prior_x_;
		marker_manip_.start_y_ = mouse_state_.prior_y_;
		marker_manip_.start_pos_ = obj.pos;
		marker_manip_.start_rot_ = obj.rot;
	}
	else {
		selected_object_index_ = -1;
		marker_manip_.mode_ = ManipulationMode::None;
		Logger::Get().Log(LogLevel::INFO, "[App] Object selection cleared");
	}
}

bool App::CheckCollision(const glm::vec3& nextPos) {
    if (noclip_mode_) return false; // Bypass collision

    // Safety check: ensure level is loaded
    if (level_.GetLevelNo() == 0) {
        return false; // No collision when level not loaded
    }
    
    auto& objects = level_.GetLevelObjects().GetObjects();
    
    float playerRadius = 400.0f; 
    constexpr float BASE_SCALE = 40.96f;
    constexpr float FALLBACK_RADIUS_MODEL = 200.0f; // fallback collision radius in model units (tight)
    
    for (const auto& obj : objects) {
        float dist = glm::distance(nextPos, glm::vec3(obj.pos));
        if (dist > 150000.0f) continue;

        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(obj.pos.x, obj.pos.y, obj.pos.z));
        model = glm::rotate(model, (float)obj.rot.z, glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::rotate(model, (float)obj.rot.x, glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, (float)obj.rot.y, glm::vec3(0.0f, 1.0f, 0.0f));

        model = glm::scale(model, glm::vec3(BASE_SCALE * obj.scale));
        model = glm::rotate(model, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));

        glm::vec4 localPos = glm::inverse(model) * glm::vec4(nextPos, 1.0f);
        glm::vec3 extents = renderer_.GetMeshExtents(obj.modelId, obj.isBuilding);

        // Fallback: if mesh failed to load, use a minimum collision radius
        float ex = extents.x;
        float ey = extents.y;
        float ez = extents.z;
        if (ex < 1.0f && ey < 1.0f && ez < 1.0f) {
            ex = ey = ez = FALLBACK_RADIUS_MODEL;
        }

        if (std::abs(localPos.x) < (ex + playerRadius/BASE_SCALE) &&
            std::abs(localPos.y) < (ey + playerRadius/BASE_SCALE) &&
            std::abs(localPos.z) < (ez + playerRadius/BASE_SCALE)) 
        {
            static int collisionLogCount = 0;
            if (collisionLogCount < 50) {
                Logger::Get().Log(LogLevel::INFO, "[App] Collision with model=" + obj.modelId + " type=" + (obj.isBuilding ? "building" : "object"));
                collisionLogCount++;
            }
            return true;
        }
    }
    return false;
}

void App::SnapObjectsToTerrain() {
    auto& objects = level_.GetLevelObjects().GetObjects();
    Logger::Get().Log(LogLevel::INFO, "[App] Snapping " + std::to_string(objects.size()) + " objects to terrain...");

    int snapped = 0;
    int skipped = 0;
    int failed = 0;
    for (auto& obj : objects) {
        // Skip snapping for cutscene soldiers
        if (cutscene_soldier_ids_.count(obj.taskId)) {
            skipped++;
            continue;
        }
        // Skip snapping for AI Soldiers, Cameras, Terminals, and Spline Waypoints
        // Terminals sit on interior floors at their exact QSC Z, not outdoor terrain.
        if (obj.type == "HumanSoldier" || obj.type == "HumanSoldierFemale" ||
            obj.type == "SCamera" || obj.type == "Terminal" || obj.type == "SplineObjWaypoint" ||
            obj.type == "AmbientArea" || obj.type == "Elevator" || obj.isWire) {
            
            // Only restore original Z if the object has NOT been moved/modified by the user
            // or by its parent building. This allows hierarchical movement to stick.
            if (!obj.modified) {
                Logger::Get().Log(LogLevel::INFO, "[App] Snapping " + obj.type + " to original QSC Z: " + obj.modelId);
                obj.pos.z = obj.original_pos.z;
            } else {
                Logger::Get().Log(LogLevel::INFO, "[App] Preserving modified Z for " + obj.type + ": " + obj.modelId);
            }
            
            obj.snap_z_offset = 0.0;
            skipped++;
            continue;
        }

        // Underground objects (Tunnels, ElevatorTunnels, UndergroundRooms) have their
        // Z defined precisely in the QSC and must go below terrain. Preserve original Z.
        // We also check parents: if a child (like a Door or Terminal) is inside an underground
        // parent, it must also skip snapping.
        bool isUnderground = Utils::IsUndergroundModel(obj.name, obj.modelId) || (obj.type == "Underground");
        
        if (!isUnderground && obj.parentIndex != -1 && obj.parentIndex < (int)objects.size()) {
            int pIdx = obj.parentIndex;
            while (pIdx != -1 && pIdx < (int)objects.size()) {
                const auto& parent = objects[pIdx];
                if (Utils::IsUndergroundModel(parent.name, parent.modelId) || (parent.type == "Underground")) {
                    isUnderground = true;
                    break;
                }
                pIdx = parent.parentIndex;
            }
        }

        if (isUnderground) {
            obj.snap_z_offset = 0.0;
            obj.pos.z = obj.original_pos.z;
            Logger::Get().Log(LogLevel::INFO, "[App] Underground context, preserving QSC Z for " + obj.modelId + " (" + obj.name + ") Z=" + std::to_string(obj.pos.z));
            skipped++;
            continue;
        }

        float terrainZ = 0.0f;
        if (level_.GetTerrainZ(obj.pos.x, obj.pos.y, terrainZ, false)) {
            float zOffset = renderer_.GetMeshZOffset(obj.modelId, obj.isBuilding);
            obj.snap_z_offset = (double)(zOffset * 40.96f * obj.scale);
            obj.pos.z = (double)terrainZ + obj.snap_z_offset;
            Logger::Get().Log(LogLevel::INFO, "[App] Snapped " + obj.modelId + " to Z=" + std::to_string(obj.pos.z));
            snapped++;
        } else {
            Logger::Get().Log(LogLevel::WARNING, "[App] Snap FAILED for " + obj.modelId + " at (" + std::to_string(obj.pos.x) + ", " + std::to_string(obj.pos.y) + "). Outside terrain?");
            failed++;
        }
    }
    Logger::Get().Log(LogLevel::INFO, "[App] Snap complete. snapped=" + std::to_string(snapped) + " skipped=" + std::to_string(skipped) + " failed=" + std::to_string(failed));
}
void App::UpdateMarkerManipulation() {
	if (selected_object_index_ < 0) return;
	auto& objects = level_.GetLevelObjects().GetObjects();
	if (selected_object_index_ >= (int)objects.size()) return;
	LevelObject& obj = objects[selected_object_index_];

	int mods = glutGetModifiers();
	bool shift = (mods & GLUT_ACTIVE_SHIFT);
	bool ctrl  = (mods & GLUT_ACTIVE_CTRL);

	// Cumulative displacement from mouse-down origin (for translation)
	int dx = mouse_state_.prior_x_ - marker_manip_.start_x_;
	int dy = mouse_state_.prior_y_ - marker_manip_.start_y_;

	// Per-frame delta (for rotation, so it accumulates smoothly each frame)
	int fdx = input_.mouse_delta_x_;
	int fdy = input_.mouse_delta_y_;

	if (shift && ctrl) marker_manip_.mode_ = ManipulationMode::MoveXZ;
	else if (shift)    marker_manip_.mode_ = ManipulationMode::MoveXY;
	else if (ctrl)     marker_manip_.mode_ = ManipulationMode::MoveXZ;
	else if (input_.keys_ & MK_MANIP_A) marker_manip_.mode_ = ManipulationMode::RotateAlpha;
	else if (input_.keys_ & MK_MANIP_B) marker_manip_.mode_ = ManipulationMode::RotateBeta;
	else if (input_.keys_ & MK_MANIP_G) marker_manip_.mode_ = ManipulationMode::RotateGamma;
	else               marker_manip_.mode_ = ManipulationMode::None;

	const float moveSensitivity = 200.0f;
	const float rotSensitivity  = 0.008f; // radians per pixel

	glm::dvec3 oldPos = obj.pos;
	glm::dvec3 oldRot = obj.rot;

	if (marker_manip_.mode_ == ManipulationMode::MoveXY) {
		glm::vec3 right   = viewer_.right_;
		glm::vec3 forward = glm::normalize(glm::vec3(viewer_.forward_.x, viewer_.forward_.y, 0.0f));
		obj.pos = marker_manip_.start_pos_ +
		          glm::dvec3(right   * (float)dx * moveSensitivity +
		                     forward * (float)-dy * moveSensitivity);
	}
	else if (marker_manip_.mode_ == ManipulationMode::MoveXZ) {
		glm::vec3 right = viewer_.right_;
		glm::vec3 up    = glm::vec3(0, 0, 1);
		obj.pos = marker_manip_.start_pos_ +
		          glm::dvec3(right * (float)dx  * moveSensitivity +
		                     up   * (float)-dy  * moveSensitivity);
	}
	// Rotation modes use per-frame delta so they accumulate correctly on obj.rot
	else if (marker_manip_.mode_ == ManipulationMode::RotateAlpha) {
		obj.rot.x += (float)fdx * rotSensitivity;
		marker_manip_.start_rot_.x = obj.rot.x; // keep start_rot in sync
	}
	else if (marker_manip_.mode_ == ManipulationMode::RotateBeta) {
		obj.rot.y += (float)fdx * rotSensitivity;
		marker_manip_.start_rot_.y = obj.rot.y;
	}
	else if (marker_manip_.mode_ == ManipulationMode::RotateGamma) {
		obj.rot.z += (float)fdx * rotSensitivity;
		marker_manip_.start_rot_.z = obj.rot.z;
	}

	// Propagate transform to children
	glm::dvec3 deltaPos = obj.pos - oldPos;
	glm::dvec3 deltaRot = obj.rot - oldRot;
	
	bool changed = (std::abs(deltaPos.x) > 1e-6 || std::abs(deltaPos.y) > 1e-6 || std::abs(deltaPos.z) > 1e-6 ||
	                std::abs(deltaRot.x) > 1e-6 || std::abs(deltaRot.y) > 1e-6 || std::abs(deltaRot.z) > 1e-6);

	if (marker_manip_.mode_ != ManipulationMode::None) {
		char buf[128];
		if (marker_manip_.mode_ == ManipulationMode::MoveXY) {
			snprintf(buf, sizeof(buf), "Moving to XY Plane with X: %.2f Y: %.2f Z: %.2f", obj.pos.x, obj.pos.y, obj.pos.z);
			status_message_ = buf;
		} else if (marker_manip_.mode_ == ManipulationMode::MoveXZ) {
			snprintf(buf, sizeof(buf), "Moving to XZ Plane with X: %.2f Y: %.2f Z: %.2f", obj.pos.x, obj.pos.y, obj.pos.z);
			status_message_ = buf;
		} else if (marker_manip_.mode_ == ManipulationMode::RotateAlpha) {
			snprintf(buf, sizeof(buf), "Rotation Alpha: %.6f", obj.rot.x);
			status_message_ = buf;
		} else if (marker_manip_.mode_ == ManipulationMode::RotateBeta) {
			snprintf(buf, sizeof(buf), "Rotation Beta: %.6f", obj.rot.y);
			status_message_ = buf;
		} else if (marker_manip_.mode_ == ManipulationMode::RotateGamma) {
			snprintf(buf, sizeof(buf), "Rotation Gamma: %.6f", obj.rot.z);
			status_message_ = buf;
		}
	} else {
		status_message_.clear();
	}

	if (changed || (input_.keys_ & MK_MANIP_S) || (input_.keys_ & MK_MANIP_O)) {
		PropagateTransformToChildren(selected_object_index_, deltaPos, deltaRot, oldPos);
		obj.modified = true;

		// Live update from Object to Task Editor
		level_.GetLevelObjects().UpdateCoordinatesInLine(obj);
		if (task_editor_open_) {
			edit_string_ = obj.qscLine;
		}
	}

	if (input_.keys_ & MK_MANIP_S) {
		bool isUnderground = Utils::IsUndergroundModel(obj.name, obj.modelId) || (obj.type == "Underground");
		float terrainZ = 0.0f;
		if (level_.GetTerrainZ(obj.pos.x, obj.pos.y, terrainZ, isUnderground)) {
			float zOffset = renderer_.GetMeshZOffset(obj.modelId, obj.isBuilding);
			if (isUnderground) {
				obj.snap_z_offset = 0.0;
			} else {
				obj.snap_z_offset = (double)(zOffset * 40.96f * obj.scale);
			}
			obj.pos.z = (double)terrainZ + obj.snap_z_offset;
			obj.modified = true;
		}
	}

	if (input_.keys_ & MK_MANIP_O) {
		// Snap to nearest object
		int nearestIdx = -1;
		double minDist = 1e10;
		const auto& objects = level_.GetLevelObjects().GetObjects();
		for (int i = 0; i < (int)objects.size(); ++i) {
			if (i == selected_object_index_ || objects[i].deleted) continue;
			double d = glm::distance(obj.pos, objects[i].pos);
			if (d < minDist) {
				minDist = d;
				nearestIdx = i;
			}
		}
		if (nearestIdx >= 0) {
			obj.pos = objects[nearestIdx].pos;
			obj.modified = true;
			Logger::Get().Log(LogLevel::INFO, "[App] Snapped object to nearest object: " + objects[nearestIdx].type);
		}
	}

	if (input_.keys_ & MK_MANIP_SPACE) {
		obj.rot = glm::vec3(0.0f);
		obj.modified = true;
	}

	if (dx != 0 || dy != 0) {
		obj.modified = true;
	}

	if (obj.modified) {
		level_.GetLevelObjects().UpdateCoordinatesInLine(obj);
	}
}

void App::PropagateTransformToChildren(int parentIdx, const glm::dvec3& deltaPos, const glm::dvec3& deltaRot, const glm::dvec3& pivot) {
	auto& objects = level_.GetLevelObjects().GetObjects();
	std::vector<int> children = objects[parentIdx].childrenIndices;

	for (int childIdx : children) {
		LevelObject& child = objects[childIdx];
		child.pos += deltaPos;
		// Apply rotation delta (if any)
		bool hasRot = (std::abs(deltaRot.x) > 0.0001 || std::abs(deltaRot.y) > 0.0001 || std::abs(deltaRot.z) > 0.0001);
		if (hasRot) {
			// Position change due to rotation around pivot (parent's old position)
			glm::dvec3 relativePos = child.pos - pivot;
			if (std::abs(deltaRot.z) > 0.0001) {
				double cosZ = cos(deltaRot.z), sinZ = sin(deltaRot.z);
				double nx = relativePos.x * cosZ - relativePos.y * sinZ;
				double ny = relativePos.x * sinZ + relativePos.y * cosZ;
				relativePos.x = nx; relativePos.y = ny;
			}
			if (std::abs(deltaRot.x) > 0.0001) {
				double cosX = cos(deltaRot.x), sinX = sin(deltaRot.x);
				double ny = relativePos.y * cosX - relativePos.z * sinX;
				double nz = relativePos.y * sinX + relativePos.z * cosX;
				relativePos.y = ny; relativePos.z = nz;
			}
			if (std::abs(deltaRot.y) > 0.0001) {
				double cosY = cos(deltaRot.y), sinY = sin(deltaRot.y);
				double nx = relativePos.x * cosY + relativePos.z * sinY;
				double nz = -relativePos.x * sinY + relativePos.z * cosY;
				relativePos.x = nx; relativePos.z = nz;
			}
			child.pos = pivot + relativePos;
			child.rot += deltaRot;

			// If it's a soldier, update its internal AI graph position too
			if (child.type == "HumanSoldier" || child.type == "HumanSoldierFemale") {
				child.graphPos += deltaPos;
			}
		} else {
			// Just a translation move
			// If it's a soldier, update its internal AI graph position too
			if (child.type == "HumanSoldier" || child.type == "HumanSoldierFemale") {
				child.graphPos += deltaPos;
			}
		}
		child.modified = true;
		PropagateTransformToChildren(childIdx, deltaPos, deltaRot, pivot);
	}
}

int App::PickObjectAtScreenPos(int screen_x, int screen_y) {
	const auto& objects = level_.GetLevelObjects().GetObjects();
	if (objects.empty()) return -1;

	int width = window_state_.viewport_width_;
	int height = window_state_.viewport_height_;
	if (width == 0 || height == 0) return -1;

	glm::mat4 proj = glm::perspective(view_define_.fovy_,
		(float)width / (float)height,
		view_define_.render_z_near_, view_define_.render_z_far_);
	glm::vec3 scaled_down_pos = view_define_.pos_ * RENDERER_MODEL_SCALE_DOWN;
	glm::mat4 view = glm::lookAt(
		scaled_down_pos,
		scaled_down_pos + view_define_.forward_ * WORLD_UNITS_PER_METER,
		view_define_.up_);
	glm::mat4 mvp_base = proj * view * glm::scale(glm::mat4(1.0f), glm::vec3(RENDERER_MODEL_SCALE_DOWN));
	constexpr float BASE_SCALE = 40.96f;

	int closest_index = -1;
	float closest_depth = FLT_MAX;

	for (size_t i = 0; i < objects.size(); ++i) {
		const auto& obj = objects[i];
		if (obj.deleted || obj.modelId.empty()) continue;

		glm::vec3 he = renderer_.GetMeshExtents(obj.modelId, obj.isBuilding);
		float max_he = glm::max(he.x, glm::max(he.y, he.z));
		if (max_he < 1.0f) {
			he = glm::vec3(60.0f);
		}

		glm::mat4 model = glm::mat4(1.0f);
		model = glm::translate(model, glm::vec3(obj.pos));
		model = glm::rotate(model, (float)obj.rot.z, glm::vec3(0, 0, 1));
		model = glm::rotate(model, (float)obj.rot.x, glm::vec3(1, 0, 0));
		model = glm::rotate(model, (float)obj.rot.y, glm::vec3(0, 1, 0));
		model = glm::scale(model, glm::vec3(BASE_SCALE * obj.scale));
		model = glm::rotate(model, glm::radians(90.0f), glm::vec3(1, 0, 0));
		glm::mat4 mvp = mvp_base * model;

		float min_px = FLT_MAX, max_px = -FLT_MAX;
		float min_py = FLT_MAX, max_py = -FLT_MAX;
		float min_depth = FLT_MAX;
		bool any_front = false;

		for (int cx = -1; cx <= 1; cx += 2) {
			for (int cy = -1; cy <= 1; cy += 2) {
				for (int cz = -1; cz <= 1; cz += 2) {
					glm::vec4 clip = mvp * glm::vec4(he.x * cx, he.y * cy, he.z * cz, 1.0f);
					if (clip.w <= 0.0f) continue;
					any_front = true;
					float ndc_x = clip.x / clip.w;
					float ndc_y = clip.y / clip.w;
					float px = (ndc_x * 0.5f + 0.5f) * (float)width;
					float py = (1.0f - (ndc_y * 0.5f + 0.5f)) * (float)height;
					min_px = std::min(min_px, px);
					max_px = std::max(max_px, px);
					min_py = std::min(min_py, py);
					max_py = std::max(max_py, py);
					min_depth = std::min(min_depth, clip.w);
				}
			}
		}

		if (!any_front) continue;

		float margin = 6.0f;
		if ((float)screen_x >= min_px - margin && (float)screen_x <= max_px + margin &&
			(float)screen_y >= min_py - margin && (float)screen_y <= max_py + margin) {
			if (closest_index == -1 || min_depth < closest_depth) {
				closest_depth = min_depth;
				closest_index = (int)i;
			}
		}
	}

	return closest_index;
}

void App::LoadQSCForLevel(int level_no) {
	try {
		namespace fs = std::filesystem;

		std::string qsc_source = Utils::GetLevelQSCPath(level_no);
		std::string qsc_dest = Utils::GetExeDirectory() + "\\objects.qsc";
		
		Logger::Get().Log(LogLevel::INFO, "[App] [LoadQSCForLevel] Source: " + qsc_source);
		Logger::Get().Log(LogLevel::INFO, "[App] [LoadQSCForLevel] Destination: " + qsc_dest);

		// Set up compiler output callback
		compiler_.SetOutputCallback([](const std::string& msg) {
			Logger::Get().Log(LogLevel::INFO, msg);
			printf("%s\n", msg.c_str());
		});

		// Check if source exists
		if (!fs::exists(qsc_source)) {
			Logger::Get().Log(LogLevel::WARNING, "[App] [LoadQSCForLevel] QSC file not found at: " + qsc_source);
			Logger::Get().Log(LogLevel::INFO, "[App] [LoadQSCForLevel] Attempting to decompile from game QVM...");
			DecompileFromGame(level_no);
			return;
		}

		// Copy to current directory
		Logger::Get().Log(LogLevel::INFO, "[App] [LoadQSCForLevel] Copying QSC to exe directory...");
		if (fs::exists(qsc_dest)) fs::remove(qsc_dest);
		fs::copy_file(qsc_source, qsc_dest, fs::copy_options::overwrite_existing);
		Logger::Get().Log(LogLevel::INFO, "[App] [LoadQSCForLevel] SUCCESS: Loaded QSC from: " + qsc_source + " to: " + qsc_dest);
	}
	catch (const std::exception& e) {
		std::string errorMsg = "Error loading QSC file for level " + std::to_string(level_no) + ":\n" + std::string(e.what());
		Utils::LogAndShowError(errorMsg, "IGI Editor - Error");
		Logger::Get().Log(LogLevel::ERR, errorMsg);
	}
	catch (...) {
		std::string errorMsg = "Unknown error loading QSC file for level " + std::to_string(level_no);
		Utils::LogAndShowError(errorMsg, "IGI Editor - Error");
		Logger::Get().Log(LogLevel::ERR, errorMsg);
	}
}

void App::DecompileFromGame(int level_no) {
	try {
		namespace fs = std::filesystem;

		std::string qvm_source = Utils::GetLevelQVMPath(level_no);
		std::string qsc_dest = Utils::GetExeDirectory() + "\\objects.qsc";

		if (!fs::exists(qvm_source)) {
			std::string errorMsg = "Game QVM not found at:\n" + qvm_source + "\n\nPlease check your IGI game path in config.ini";
			Utils::LogAndShowError(errorMsg, "IGI Editor - Error");
			Logger::Get().Log(LogLevel::ERR, "[App] Game QVM not found at: " + qvm_source);
			return;
		}

		// Set up decompiler output callback
		decompiler_.SetOutputCallback([](const std::string& msg) {
			Logger::Get().Log(LogLevel::INFO, msg);
			printf("%s\n", msg.c_str());
		});

		bool success = decompiler_.Decompile(qvm_source, qsc_dest);
		if (!success) {
			std::string errorMsg = "Failed to decompile QVM from:\n" + qvm_source;
			Utils::LogAndShowError(errorMsg, "IGI Editor - Error");
			Logger::Get().Log(LogLevel::ERR, "[App] Failed to decompile from game QVM");
		}
	}
	catch (const std::exception& e) {
		std::string errorMsg = "Error decompiling QVM for level " + std::to_string(level_no) + ":\n" + std::string(e.what());
		Utils::LogAndShowError(errorMsg, "IGI Editor - Error");
		Logger::Get().Log(LogLevel::ERR, errorMsg);
	}
	catch (...) {
		std::string errorMsg = "Unknown error decompiling QVM for level " + std::to_string(level_no);
		Utils::LogAndShowError(errorMsg, "IGI Editor - Error");
		Logger::Get().Log(LogLevel::ERR, errorMsg);
	}
}

void App::SaveAndCompile() {
	namespace fs = std::filesystem;

	Logger::Get().Log(LogLevel::INFO, "[App] SaveAndCompile() starting");

	std::string qsc_source = Utils::GetExeDirectory() + "\\objects.qsc";
	std::string qvm_dest = Utils::GetLevelQVMPath(level_.GetLevelNo());

	Logger::Get().Log(LogLevel::INFO, "[App] Full QSC path: " + qsc_source);
	Logger::Get().Log(LogLevel::INFO, "[App] QVM destination: " + qvm_dest);

	if (!fs::exists(qsc_source)) {
		Logger::Get().Log(LogLevel::ERR, "[App] QSC file not found at: " + qsc_source);
		return;
	}

	// Set up compiler output callback
	compiler_.SetOutputCallback([](const std::string& msg) {
		Logger::Get().Log(LogLevel::INFO, msg);
		printf("%s\n", msg.c_str());
	});

	Logger::Get().Log(LogLevel::INFO, "[App] Calling compiler.Compile()");
	bool success = compiler_.Compile(qsc_source, qvm_dest);
	if (success) {
		Logger::Get().Log(LogLevel::INFO, "[App] Successfully compiled and deployed QVM to: " + qvm_dest);
	} else {
		Logger::Get().Log(LogLevel::ERR, "[App] Failed to compile QSC");
	}
}

void App::SetInitialDrawParts(int parts) {
	if (parts != 0) {
		draw_params_.draw_parts_ = parts;
		Logger::Get().Log(LogLevel::INFO, "[App] Set initial draw_parts to: " + std::to_string(parts));
	}
}

void App::SetInitialStickToGround(bool stick) {
	stick_to_ground_ = stick;
	if (stick) {
		SnapObjectsToTerrain();
		Logger::Get().Log(LogLevel::INFO, "[App] Enabled stick_to_ground mode");
	}
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
                            task_editor_open_ = true;
                            std::string line = obj.qscLine.empty() ? level_.GetLevelObjects().GenerateTaskLine(obj) : obj.qscLine;
                            
                            // Strip any newlines from the loaded line
                            line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
                            line.erase(std::remove(line.begin(), line.end(), '\n'), line.end());
                            
                            edit_string_ = line;
                            edit_cursor_pos_ = (int)edit_string_.size();
                            edit_selection_start_ = edit_selection_end_ = -1;
                            edit_scroll_x_ = 0;
                            Logger::Get().Log(LogLevel::INFO, "[App] Double clicked object from tree and opened Task Editor.");
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

void App::CreateNewTask() {
    auto& objects = level_.GetLevelObjects().GetObjects();
    LevelObject newObj;
    newObj.qscFuncName = "Task_New";
    newObj.type = "Container";
    newObj.name = "NewTask_" + std::to_string(objects.size());
    newObj.pos = glm::dvec3(viewer_.pos_);
    newObj.rot = glm::vec3(0.0f);
    newObj.scale = 1.0f;
    newObj.isContainer = true;
    newObj.expanded = true;
    newObj.modified = true;
    newObj.taskId = "-1"; // Auto-assign or manual
    
    // Add to current selection as parent if applicable
    if (selected_object_index_ >= 0 && selected_object_index_ < (int)objects.size()) {
        if (objects[selected_object_index_].isContainer) {
            newObj.parentIndex = selected_object_index_;
            objects[selected_object_index_].childrenIndices.push_back((int)objects.size());
        }
    }
    
    objects.push_back(newObj);
    selected_object_index_ = (int)objects.size() - 1;
    level_.GetLevelObjects().UpdateCoordinatesInLine(objects.back());
    level_.SaveAndReloadObjects();
    auto& reloaded = level_.GetLevelObjects().GetObjects();
    if (!reloaded.empty()) selected_object_index_ = std::min(selected_object_index_, (int)reloaded.size() - 1);
    
    Logger::Get().Log(LogLevel::INFO, "[App] Created new task at viewer position");
    printf("[App] Created new task at viewer position\n");
}

void App::DeleteSelectedTask() {
    if (selected_object_index_ < 0) return;
    auto& objects = level_.GetLevelObjects().GetObjects();
    if (selected_object_index_ >= (int)objects.size()) return;
    int parentIndex = objects[selected_object_index_].parentIndex;

    std::function<void(int)> delete_recurse = [&](int idx) {
        if (idx < 0 || idx >= (int)objects.size()) return;
        objects[idx].deleted = true;
        for (int childIdx : objects[idx].childrenIndices) {
            delete_recurse(childIdx);
        }
    };

    delete_recurse(selected_object_index_);
    level_.SaveAndReloadObjects();
    auto& reloaded = level_.GetLevelObjects().GetObjects();
    if (reloaded.empty()) selected_object_index_ = -1;
    else if (parentIndex >= 0 && parentIndex < (int)reloaded.size()) selected_object_index_ = parentIndex;
    else selected_object_index_ = std::min(selected_object_index_, (int)reloaded.size() - 1);
    Logger::Get().Log(LogLevel::INFO, "[App] Deleted task and its subtree");
}

void App::CopySelectedTask(bool includeSubtree) {
    if (selected_object_index_ < 0) return;
    auto& objects = level_.GetLevelObjects().GetObjects();
    clipboard_.clear();

    std::function<void(int, int)> copy_recurse = [&](int idx, int newParentInClipboard) {
        if (idx < 0 || idx >= (int)objects.size()) return;
        
        LevelObject copy = objects[idx];
        copy.childrenIndices.clear();
        copy.parentIndex = newParentInClipboard;
        
        int clipboardIdx = (int)clipboard_.size();
        clipboard_.push_back(copy);
        
        if (newParentInClipboard != -1) {
            clipboard_[newParentInClipboard].childrenIndices.push_back(clipboardIdx);
        }

        if (includeSubtree) {
            for (int childIdx : objects[idx].childrenIndices) {
                copy_recurse(childIdx, clipboardIdx);
            }
        }
    };

    copy_recurse(selected_object_index_, -1);
    Logger::Get().Log(LogLevel::INFO, "[App] Copied task to clipboard (subtree: " + std::string(includeSubtree ? "yes" : "no") + ")");
}

void App::PasteTask() {
    if (clipboard_.empty()) return;
    int targetParent = selected_object_index_;
    auto& objects = level_.GetLevelObjects().GetObjects();

    int startIdxInObjects = (int)objects.size();
    
    // Copy all from clipboard to objects
    for (size_t i = 0; i < clipboard_.size(); ++i) {
        LevelObject pasted = clipboard_[i];
        
        // Update indices to point into objects_ vector
        if (pasted.parentIndex == -1) {
            pasted.parentIndex = targetParent;
            if (targetParent != -1) {
                objects[targetParent].childrenIndices.push_back((int)objects.size());
            }
        } else {
            pasted.parentIndex += startIdxInObjects;
        }

        for (size_t j = 0; j < pasted.childrenIndices.size(); ++j) {
            pasted.childrenIndices[j] += startIdxInObjects;
        }

        pasted.modified = true;
        objects.push_back(pasted);
    }

    selected_object_index_ = startIdxInObjects;
    level_.SaveAndReloadObjects();
    auto& reloaded = level_.GetLevelObjects().GetObjects();
    if (!reloaded.empty()) selected_object_index_ = std::min(selected_object_index_, (int)reloaded.size() - 1);
    
    Logger::Get().Log(LogLevel::INFO, "[App] Pasted task(s) from clipboard");
}

void App::AssignTaskID() {
    if (selected_object_index_ < 0) return;
    auto& objects = level_.GetLevelObjects().GetObjects();
    
    // Find max task ID
    int maxId = 0;
    for (const auto& obj : objects) {
        if (!obj.taskId.empty()) {
            try {
                int id = std::stoi(obj.taskId);
                if (id > maxId) maxId = id;
            } catch (...) {}
        }
    }
    
    objects[selected_object_index_].taskId = std::to_string(maxId + 1);
    objects[selected_object_index_].modified = true;
    level_.GetLevelObjects().UpdateCoordinatesInLine(objects[selected_object_index_]);
    level_.SaveAndReloadObjects();
    auto& reloaded = level_.GetLevelObjects().GetObjects();
    if (!reloaded.empty()) selected_object_index_ = std::min(selected_object_index_, (int)reloaded.size() - 1);
    
    Logger::Get().Log(LogLevel::INFO, "[App] Assigned new Task ID: " + std::to_string(maxId + 1));
    printf("[App] Assigned new Task ID: %d\n", maxId + 1);
}

void App::ModifyTaskParameters() {
	Logger::Get().Log(LogLevel::INFO, "[App] ModifyTaskParameters (Stub - parameter UI needed)");
}

void App::ClearStatusMessage() {
	status_message_.clear();
}

static int GetLookupObjectIndex(int hoverIdx, int selectedIdx) {
	if (hoverIdx >= 0) return hoverIdx;
	if (selectedIdx >= 0) return selectedIdx;
	return -1;
}

static void SetLookupStatus(std::string& status_message, const std::string& msg) {
	status_message = msg;
	Logger::Get().Log(LogLevel::INFO, msg);
	printf("%s\n", msg.c_str());
}

void App::LookupSelectedModelName() {
	auto& objects = level_.GetLevelObjects().GetObjects();
	int idx = GetLookupObjectIndex(hover_object_index_, selected_object_index_);
	if (idx < 0 || idx >= (int)objects.size()) {
		SetLookupStatus(status_message_, "[App] Model lookup: no hovered/selected object");
		return;
	}

	const auto& obj = objects[idx];
	std::string name = level_.GetLevelObjects().GetModelName(obj.modelId);
	if (name.empty()) {
		SetLookupStatus(status_message_, "[App] Model lookup: no friendly name for model ID " + obj.modelId);
		return;
	}

	SetLookupStatus(status_message_, "[App] Model lookup: " + obj.modelId + " -> " + name);
}

void App::LookupSelectedModelId() {
	auto& objects = level_.GetLevelObjects().GetObjects();
	int idx = GetLookupObjectIndex(hover_object_index_, selected_object_index_);
	if (idx < 0 || idx >= (int)objects.size()) {
		SetLookupStatus(status_message_, "[App] Model lookup: no hovered/selected object");
		return;
	}

	const auto& obj = objects[idx];
	std::string name = level_.GetLevelObjects().GetModelName(obj.modelId);
	if (name.empty()) {
		name = obj.name;
	}
	if (name.empty()) {
		SetLookupStatus(status_message_, "[App] Model lookup: object has no readable model name");
		return;
	}

	std::string modelId = level_.GetLevelObjects().GetModelId(name);
	if (modelId.empty()) {
		SetLookupStatus(status_message_, "[App] Model lookup: no model id for name \"" + name + "\"");
		return;
	}

	SetLookupStatus(status_message_, "[App] Model lookup: " + name + " -> " + modelId);
}

void App::CopySelectedModelName() {
	auto& objects = level_.GetLevelObjects().GetObjects();
	int idx = GetLookupObjectIndex(hover_object_index_, selected_object_index_);
	if (idx < 0 || idx >= (int)objects.size()) {
		SetLookupStatus(status_message_, "[App] Model copy: no hovered/selected object");
		return;
	}

	const auto& obj = objects[idx];
	std::string name = level_.GetLevelObjects().GetModelName(obj.modelId);
	if (name.empty()) {
		SetLookupStatus(status_message_, "[App] Model copy: no friendly name for model ID " + obj.modelId);
		return;
	}

	Utils::SetClipboardText(name);
	SetLookupStatus(status_message_, "[App] Copied model name: " + name);
}

void App::CopySelectedModelId() {
	auto& objects = level_.GetLevelObjects().GetObjects();
	int idx = GetLookupObjectIndex(hover_object_index_, selected_object_index_);
	if (idx < 0 || idx >= (int)objects.size()) {
		SetLookupStatus(status_message_, "[App] Model copy: no hovered/selected object");
		return;
	}

	const auto& obj = objects[idx];
	std::string name = level_.GetLevelObjects().GetModelName(obj.modelId);
	if (name.empty()) {
		name = obj.name;
	}
	if (name.empty()) {
		SetLookupStatus(status_message_, "[App] Model copy: object has no readable model name");
		return;
	}

	std::string modelId = level_.GetLevelObjects().GetModelId(name);
	if (modelId.empty()) {
		SetLookupStatus(status_message_, "[App] Model copy: no model id for name \"" + name + "\"");
		return;
	}

	Utils::SetClipboardText(modelId);
	SetLookupStatus(status_message_, "[App] Copied model id: " + modelId);
}

void App::LookupHoveredModelName() { LookupSelectedModelName(); }
void App::LookupHoveredModelId() { LookupSelectedModelId(); }

void App::SearchModelById() {
	auto prompt = Utils::PromptForText("Search Model by ID", "Enter Model ID (e.g. 1506 or AITYPE_PATROL_AK):", "");
	if (!prompt.has_value()) return;

	std::string searchId = prompt.value();
	searchId = Utils::Trim(searchId);
	if (searchId.empty()) return;

	std::string searchIdLower = searchId;
	std::transform(searchIdLower.begin(), searchIdLower.end(), searchIdLower.begin(), [](unsigned char c) { return std::tolower(c); });

	auto& objects = level_.GetLevelObjects().GetObjects();
	int foundIdx = -1;
	for (int i = 0; i < (int)objects.size(); ++i) {
		if (objects[i].deleted) continue;

		std::string taskIdLower = objects[i].taskId;
		std::transform(taskIdLower.begin(), taskIdLower.end(), taskIdLower.begin(), [](unsigned char c) { return std::tolower(c); });

		std::string modelIdLower = objects[i].modelId;
		std::transform(modelIdLower.begin(), modelIdLower.end(), modelIdLower.begin(), [](unsigned char c) { return std::tolower(c); });

		if (taskIdLower == searchIdLower || modelIdLower == searchIdLower) {
			foundIdx = i;
			break;
		}
	}

	if (foundIdx != -1) {
		selected_object_index_ = foundIdx;
		const auto& obj = objects[foundIdx];
		viewer_.pos_ = glm::vec3(obj.pos.x, obj.pos.y, obj.pos.z + 1500.0f);
		viewer_.pitch_ = -45.0f; 
		SetLookupStatus(status_message_, "[App] Search: Found and selected model ID \"" + obj.modelId + "\" at index " + std::to_string(foundIdx));
	} else {
		SetLookupStatus(status_message_, "[App] Search: No object matches ID \"" + searchId + "\"");
	}
}

void App::SearchModelByName() {
	auto prompt = Utils::PromptForText("Search Model by Name", "Enter Model Name/Type (e.g. Heli or Soldier or Fence):", "");
	if (!prompt.has_value()) return;

	std::string searchName = prompt.value();
	searchName = Utils::Trim(searchName);
	if (searchName.empty()) return;

	std::string searchNameLower = searchName;
	std::transform(searchNameLower.begin(), searchNameLower.end(), searchNameLower.begin(), [](unsigned char c) { return std::tolower(c); });

	auto& objects = level_.GetLevelObjects().GetObjects();
	int foundIdx = -1;
	
	int startIdx = (selected_object_index_ >= 0) ? (selected_object_index_ + 1) % objects.size() : 0;
	
	for (size_t step = 0; step < objects.size(); ++step) {
		int i = (startIdx + step) % objects.size();
		if (objects[i].deleted) continue;

		std::string friendlyName = level_.GetLevelObjects().GetModelName(objects[i].modelId);
		if (friendlyName.empty()) {
			friendlyName = objects[i].name;
		}
		std::string typeName = objects[i].type;

		std::transform(friendlyName.begin(), friendlyName.end(), friendlyName.begin(), [](unsigned char c) { return std::tolower(c); });
		std::transform(typeName.begin(), typeName.end(), typeName.begin(), [](unsigned char c) { return std::tolower(c); });

		if (friendlyName.find(searchNameLower) != std::string::npos || typeName.find(searchNameLower) != std::string::npos) {
			foundIdx = i;
			break;
		}
	}

	if (foundIdx != -1) {
		selected_object_index_ = foundIdx;
		const auto& obj = objects[foundIdx];
		viewer_.pos_ = glm::vec3(obj.pos.x, obj.pos.y, obj.pos.z + 1500.0f);
		viewer_.pitch_ = -45.0f;
		std::string displayName = level_.GetLevelObjects().GetModelName(obj.modelId);
		if (displayName.empty()) displayName = obj.name;
		SetLookupStatus(status_message_, "[App] Search: Found \"" + displayName + "\" (Type: " + obj.type + ") at index " + std::to_string(foundIdx));
	} else {
		SetLookupStatus(status_message_, "[App] Search: No object matches name \"" + searchName + "\"");
	}
}

std::vector<int> App::GetVisibleTreeNodes() {
    std::vector<int> visibleIndices;
    if (level_.GetLevelObjects().GetObjects().empty()) return visibleIndices;
    
    auto& objects = level_.GetLevelObjects().GetObjects();
    
    std::function<void(int)> traverse = [&](int idx) {
        if (idx < 0 || idx >= (int)objects.size()) return;
        const auto& obj = objects[idx];
        if (obj.deleted) return;
        
        visibleIndices.push_back(idx);
        
        if (obj.expanded) {
            for (int childIdx : obj.childrenIndices) {
                traverse(childIdx);
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
    
    if (!root_decls.empty()) {
        visibleIndices.push_back(-2); // Virtual "Mission Declarations" folder
        if (tree_decl_expanded_) {
            for (int idx : root_decls) {
                traverse(idx);
            }
        }
    }
    
    for (int idx : root_others) {
        traverse(idx);
    }
    
    return visibleIndices;
}

void App::LoadCutsceneSoldierIDs() {
	cutscene_soldier_ids_.clear();
	auto& objects = level_.GetLevelObjects().GetObjects();
	for (const auto& obj : objects) {
		if (obj.type == "AnimTask" && obj.argTokens.size() > 9) {
			std::string targetId = Utils::Trim(obj.argTokens[9]);
			if (!targetId.empty() && targetId.front() == '"' && targetId.back() == '"') {
				targetId = targetId.substr(1, targetId.size() - 2);
			}
			if (!targetId.empty()) {
				cutscene_soldier_ids_.insert(targetId);
				Logger::Get().Log(LogLevel::INFO, "[App] Registered cutscene soldier task ID: " + targetId);
			}
		}
	}
}


