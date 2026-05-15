/******************************************************************************
 * @file    app.cpp
 * @brief   application class
 *****************************************************************************/

#include "pch.h"
#include <freeglut.h>
#include "logger.h"
#include "utils.h"
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
	edit_mode_(false),
	terrain_edit_enabled_(false),
	edit_brush_(0), // 0: raise, 1: lower
	selected_object_index_(0),
	hover_object_index_(-1),
	show_hud_(true),
	show_debug_(false),
	show_help_(false),


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
				obj.rot.x = 0.0;           // PITCH = 0
				obj.rot.y = 0.0;           // ROLL = 0
				// Preserve existing rotation if it's already set, otherwise default to a full circle
				if (obj.rot.z == 0.0) obj.rot.z = 6.28318;
				Logger::Get().Log(LogLevel::INFO, "[App] Applied AI rotation override (horizontal only) for " + obj.name + " (" + obj.type + ")");
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

void App::LoadAIModelsFromFolder(int level_no) {
	Logger::Get().Log(LogLevel::INFO, "[App] ==========================================");
	Logger::Get().Log(LogLevel::INFO, "[App] LoadAIModelsFromFolder() START for level " + std::to_string(level_no));
	Logger::Get().Log(LogLevel::INFO, "[App] ==========================================");

	char appDataPath[MAX_PATH];
	GetEnvironmentVariableA("APPDATA", appDataPath, MAX_PATH);
	std::string aiFolderPath = std::string(appDataPath) + "\\QEditor\\3DEditor\\ai\\level" + std::to_string(level_no);

	Logger::Get().Log(LogLevel::INFO, "[App] AI folder path: " + aiFolderPath);

	// Check if AI folder exists
	if (!std::filesystem::exists(aiFolderPath)) {
		Logger::Get().Log(LogLevel::WARNING, "[App] AI folder does not exist: " + aiFolderPath);
		return;
	}

	// Get all GLB files in the AI folder
	std::vector<std::string> aiModels;
	for (const auto& entry : std::filesystem::directory_iterator(aiFolderPath)) {
		if (entry.path().extension() == ".glb") {
			aiModels.push_back(entry.path().filename().string());
		}
	}

	Logger::Get().Log(LogLevel::INFO, "[App] Found " + std::to_string(aiModels.size()) + " AI GLB files");

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
	std::string jsonPath = std::string(appDataPath) + "\\QEditor\\IGIModelsAllLevel.json";
	
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

							// Validations
							bool isValid = true;
							if (aiData.type.empty()) isValid = false;
							else {
								// Type validation: ^[A-Z0-9_]+$
								for (char c : aiData.type) {
									if (!isalnum(c) && c != '_') { isValid = false; break; }
								}
							}
							
							// Model ID validation: ddd_dd_d (e.g. 000_01_1)
							if (aiData.modelId.length() != 8 || aiData.modelId[3] != '_' || aiData.modelId[6] != '_') {
								isValid = false;
							} else {
								for (int i = 0; i < 8; ++i) {
									if (i == 3 || i == 6) continue;
									if (!isdigit(aiData.modelId[i])) { isValid = false; break; }
								}
							}

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
void App::Input_OnMouse(int button, int state, int x, int y) {
	// Update mouse position first so EditorProcessClick uses correct coords
	mouse_state_.prior_x_ = x;
	mouse_state_.prior_y_ = y;

	if (button == GLUT_LEFT_BUTTON) {
		if (GLUT_DOWN == state) {
			mouse_state_.left_button_down_ = true;
			
			if (edit_mode_) {
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

			if (window_state_.cursor_visible_) {
				input_.mouse_delta_x_ = 0;
				input_.mouse_delta_y_ = 0;
			}
		}
	}
}

void App::Input_OnMotion(int x, int y) {
	// Always update hover detection
	hover_object_index_ = PickObjectAtScreenPos(x, y);

	// Always update mouse coordinates for hover tooltip
	mouse_state_.prior_x_ = x;
	mouse_state_.prior_y_ = y;

	if (window_state_.cursor_visible_) {
		if (mouse_state_.left_button_down_) {
			input_.mouse_delta_x_ = x - mouse_state_.prior_x_;
			input_.mouse_delta_y_ = y - mouse_state_.prior_y_;

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
}

void App::Input_OnSpecial(int key, int x, int y) {
	auto& config = Config::Get();

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
	else if (key == GLUT_KEY_F3) {
		viewer_.clip_to_z_ = !viewer_.clip_to_z_;
		return;
	}
	else if (key == GLUT_KEY_F4) {
		ToggleEditMode();
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
	'z', 'Z', MK_STRAIGHT_DOWN,
	' ', ' ', MK_JUMP
};

static constexpr movement_key_s MANIP_KEYS[] = {
	'a', 'A', MK_MANIP_A,
	'b', 'B', MK_MANIP_B,
	'g', 'G', MK_MANIP_G,
	's', 'S', MK_MANIP_S,
	'o', 'O', MK_MANIP_O,
	' ', ' ', MK_MANIP_SPACE
};

void App::Input_OnKeyboard(unsigned char key, int x, int y) {
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

	for (int i = 0; i < count_of(MANIP_KEYS); ++i) {
		const movement_key_s& mk = MANIP_KEYS[i];
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


void App::ResetLevel() {
	int levelNo = level_.GetLevelNo();

	printf("Resetting Level %d - copy objects.qsc, compile to QVM, copy to IGIPath\n", levelNo);

	char appData[1024];
	GetEnvironmentVariableA("APPDATA", appData, 1024);

	// Step 1: Copy objects.qsc from QEditor to exe directory
	char srcQsc[1024];
	Str_SPrintf(srcQsc, 1024, "%s\\QEditor\\QFiles\\IGI_QSC\\missions\\location0\\level%d\\objects.qsc", appData, levelNo);

	std::string exeDir = Utils::GetExeDirectory();
	char dstQsc[1024];
	Str_SPrintf(dstQsc, 1024, "%s\\objects.qsc", exeDir.c_str());

	printf("Step 1: Copying objects.qsc from %s to %s\n", srcQsc, dstQsc);

	try {
		if (std::filesystem::exists(srcQsc)) {
			std::filesystem::copy_file(srcQsc, dstQsc, std::filesystem::copy_options::overwrite_existing);
			printf("Objects.qsc copied successfully.\n");
		}
		else {
			printf("Error: Source file %s does not exist.\n", srcQsc);
			return;
		}
	}
	catch (const std::exception& e) {
		printf("ResetLevel step 1 error: %s\n", e.what());
		return;
	}

	// Step 2: Compile to QVM
	printf("Step 2: Compiling objects.qsc to objects.qvm\n");
	level_.CompileCurrentQSC(levelNo);

	// Step 3: Copy QVM to IGIPath
	ConfigData& cfg = Config::Get();
	char srcQvm[1024];
	Str_SPrintf(srcQvm, 1024, "%s\\QEditor\\QCompiler\\Compile\\output\\objects.qvm", appData);

	char dstQvm[1024];
	Str_SPrintf(dstQvm, 1024, "%s\\missions\\location0\\level%d\\objects.qvm", cfg.igiPath.c_str(), levelNo);

	printf("Step 3: Copying QVM from %s to %s\n", srcQvm, dstQvm);

	try {
		if (std::filesystem::exists(srcQvm)) {
			std::filesystem::create_directories(std::filesystem::path(dstQvm).parent_path());
			std::filesystem::copy_file(srcQvm, dstQvm, std::filesystem::copy_options::overwrite_existing);
			printf("QVM copied successfully to game path.\n");
		}
		else {
			printf("Error: Compiled QVM not found at %s\n", srcQvm);
		}
	}
	catch (const std::exception& e) {
		printf("ResetLevel step 3 error: %s\n", e.what());
	}

	// Reload level after reset
	LoadLevel(levelNo);
	// Snap objects to terrain after level reset
	SnapObjectsToTerrain();
}

void App::ResetScript() {
	int levelNo = level_.GetLevelNo();

	printf("Resetting Script for Level %d - copy objects.qsc from QEditor to exe directory\n", levelNo);

	char appData[1024];
	GetEnvironmentVariableA("APPDATA", appData, 1024);

	// Source: QEditor IGI_QSC
	char srcQsc[1024];
	Str_SPrintf(srcQsc, 1024, "%s\\QEditor\\QFiles\\IGI_QSC\\missions\\location0\\level%d\\objects.qsc", appData, levelNo);

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
	for (int i = 0; i < count_of(MANIP_KEYS); ++i) {
		const movement_key_s& mk = MANIP_KEYS[i];
		if (key == mk.lower_case_ || key == mk.upper_case_) {
			input_.keys_ &= ~mk.key_flag_;
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
		hover_object_index_ = PickObjectAtScreenPos(mouse_state_.prior_x_, mouse_state_.prior_y_);
		float ground_z = 0.0f;
		level_.GetTerrainZ(viewer_.pos_.x, viewer_.pos_.y, ground_z);
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
			.show_help_ = show_help_,
			.edit_mode_ = edit_mode_,
			.terrain_edit_enabled_ = terrain_edit_enabled_,
			.selected_object_index_ = selected_object_index_,
			.hover_object_index_ = hover_object_index_,
			.mouse_x_ = mouse_state_.prior_x_,
			.mouse_y_ = mouse_state_.prior_y_,
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
		.show_help_ = show_help_,
		.edit_mode_ = edit_mode_,
		.terrain_edit_enabled_ = terrain_edit_enabled_,
		.selected_object_index_ = selected_object_index_,
		.hover_object_index_ = hover_object_index_,
		.mouse_x_ = mouse_state_.prior_x_,
		.mouse_y_ = mouse_state_.prior_y_,
		.level_objects_ = &level_.GetLevelObjects()
	};


	renderer_.Draw(draw_params_, hud);


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
		glm::vec3 next_pos = viewer_.pos_ + move_delta;

        if (!CheckCollision(next_pos)) {
		    viewer_.pos_ = next_pos;
        } else {
            // Sliding logic (optional) - for now just stop
            viewer_.velocity_.x = 0.0f;
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

		// check movement - direct position update for free mode (NO collision)
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

	// Update cursor based on mode
	if (edit_mode_) {
		glutSetCursor(GLUT_CURSOR_CROSSHAIR);
		glutSetCursor(GLUT_CURSOR_INHERIT); // Show system cursor for better visibility
	} else {
		glutSetCursor(GLUT_CURSOR_NONE);
	}
	Logger::Get().Log(LogLevel::INFO, std::string("[App] Edit mode ") + (edit_mode_ ? "enabled" : "disabled"));
}

bool App::GetEditMode() const {
	return edit_mode_;
}

void App::SetEditMode(bool enabled) {
	edit_mode_ = enabled;
	window_state_.cursor_visible_ = enabled;
	
	// Update cursor based on mode
	if (edit_mode_) {
		glutSetCursor(GLUT_CURSOR_CROSSHAIR);
	} else {
		glutSetCursor(GLUT_CURSOR_NONE);
	}
	Logger::Get().Log(LogLevel::INFO, std::string("[App] Edit mode set to ") + (edit_mode_ ? "enabled" : "disabled"));
}

void App::SetTerrainEditEnabled(bool enabled) {
	terrain_edit_enabled_ = enabled;
}

bool App::GetTerrainEditEnabled() const {
	return terrain_edit_enabled_;
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
		Logger::Get().Log(LogLevel::INFO, "[App] Selected object index=" + std::to_string(pickedObject) + " model=" + obj.modelId + " type=" + (obj.isBuilding ? "building" : "object"));
		printf("SELECTED Object [%d]: %s (%s)\n", pickedObject, obj.name.c_str(), obj.modelId.c_str());
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
        // Skip snapping for AI Soldiers (they maintain original QSC height)
        if (obj.type == "HumanSoldier" || obj.type == "HumanSoldierFemale") {
            Logger::Get().Log(LogLevel::INFO, "[App] Skipping snap for AI: " + obj.modelId);
            obj.snap_z_offset = 0.0;
            skipped++;
            continue;
        }
        float terrainZ = 0.0f;
        bool isUnderground = Utils::IsUndergroundModel(obj.name, obj.modelId) || (obj.type == "Underground");
        if (level_.GetTerrainZ(obj.pos.x, obj.pos.y, terrainZ, isUnderground)) {
            float zOffset = renderer_.GetMeshZOffset(obj.modelId, obj.isBuilding);
            
            // For regular objects, snap bottom to terrain surface (offset by min_y)
            // For underground objects (tunnels, etc), keep origin at terrain surface (offset = 0)
            if (isUnderground) {
                obj.snap_z_offset = 0.0;
            } else {
                obj.snap_z_offset = (double)(zOffset * 40.96f * obj.scale);
            }
            
            obj.pos.z = (double)terrainZ + obj.snap_z_offset;
            
            // Critical debug for user-reported issues
            if (obj.modelId.find("471_") == 0 || obj.modelId.find("491_") == 0 || isUnderground) {
                Logger::Get().Log(LogLevel::INFO, "[SnapDebug] Model: " + obj.modelId + 
                    " (" + obj.type + ") | TerrainZ: " + std::to_string(terrainZ) + 
                    " | MeshZOffset: " + std::to_string(zOffset) + 
                    " | SnapOffset: " + std::to_string(obj.snap_z_offset) +
                    " | FinalZ: " + std::to_string(obj.pos.z));
            } else {
                Logger::Get().Log(LogLevel::INFO, "[App] Snapped " + obj.modelId + " to Z=" + std::to_string(obj.pos.z));
            }
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
	bool ctrl = (mods & GLUT_ACTIVE_CTRL);

	int dx = mouse_state_.prior_x_ - marker_manip_.start_x_;
	int dy = mouse_state_.prior_y_ - marker_manip_.start_y_;

	if (shift && ctrl) marker_manip_.mode_ = ManipulationMode::MoveXZ;
	else if (shift) marker_manip_.mode_ = ManipulationMode::MoveXY;
	else if (ctrl) marker_manip_.mode_ = ManipulationMode::MoveXZ;
	else if (input_.keys_ & MK_MANIP_A) marker_manip_.mode_ = ManipulationMode::RotateAlpha;
	else if (input_.keys_ & MK_MANIP_B) marker_manip_.mode_ = ManipulationMode::RotateBeta;
	else if (input_.keys_ & MK_MANIP_G) marker_manip_.mode_ = ManipulationMode::RotateGamma;
	else marker_manip_.mode_ = ManipulationMode::MoveXY;

	const float moveSensitivity = 200.0f;
	const float rotSensitivity = 0.01f;

	glm::dvec3 oldPos = obj.pos;
	glm::dvec3 oldRot = obj.rot;

	if (marker_manip_.mode_ == ManipulationMode::MoveXY) {
		glm::vec3 right = viewer_.right_;
		glm::vec3 forward = glm::normalize(glm::vec3(viewer_.forward_.x, viewer_.forward_.y, 0.0f));
		obj.pos = marker_manip_.start_pos_ + glm::dvec3(right * (float)dx * moveSensitivity + forward * (float)-dy * moveSensitivity);
	}
	else if (marker_manip_.mode_ == ManipulationMode::MoveXZ) {
		glm::vec3 right = viewer_.right_;
		glm::vec3 up = glm::vec3(0, 0, 1);
		obj.pos = marker_manip_.start_pos_ + glm::dvec3(right * (float)dx * moveSensitivity + up * (float)-dy * moveSensitivity);
	}
	else if (marker_manip_.mode_ == ManipulationMode::RotateAlpha) {
		obj.rot.x = marker_manip_.start_rot_.x + (float)dx * rotSensitivity;
	}
	else if (marker_manip_.mode_ == ManipulationMode::RotateBeta) {
		obj.rot.y = marker_manip_.start_rot_.y + (float)dx * rotSensitivity;
	}
	else if (marker_manip_.mode_ == ManipulationMode::RotateGamma) {
		obj.rot.z = marker_manip_.start_rot_.z + (float)dx * rotSensitivity;
	}

	// Propagate transform to children
	glm::dvec3 deltaPos = obj.pos - oldPos;
	glm::dvec3 deltaRot = obj.rot - oldRot;
	
	bool changed = (std::abs(deltaPos.x) > 1e-6 || std::abs(deltaPos.y) > 1e-6 || std::abs(deltaPos.z) > 1e-6 ||
	                std::abs(deltaRot.x) > 1e-6 || std::abs(deltaRot.y) > 1e-6 || std::abs(deltaRot.z) > 1e-6);

	if (changed) {
		PropagateTransformToChildren(selected_object_index_, deltaPos, deltaRot, oldPos);
		obj.modified = true;
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

	if (input_.keys_ & MK_MANIP_SPACE) {
		obj.rot = glm::vec3(0.0f);
		obj.modified = true;
	}

	if (dx != 0 || dy != 0) {
		obj.modified = true;
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

		glm::vec3 he = renderer_.GetMeshExtents(obj.modelId, obj.isBuilding);
		float max_he = glm::max(he.x, glm::max(he.y, he.z));
		if (max_he < 1.0f) {
			he = glm::vec3(200.0f);
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

		float margin = 2.0f;
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


