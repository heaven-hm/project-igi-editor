/******************************************************************************
 * @file    level.cpp
 * @brief   level
 *****************************************************************************/

#include "pch.h"
#include <filesystem>
#include "logger.h"



/*
================================================================================
 Level
================================================================================
*/
constexpr int DYNAMIC_CUBE_POOL_CAPACITY = 3500;

Level::Level():
	cur_level_no_(0),
	flat_sky_fog_amount_(0.0),
	flat_sky_z_pos_(0.0),
	flat_sky_distance_(1.0f),
	root_dyn_cube_(nullptr),
	loaded_(false)
{
	// do nothing
}

Level::~Level() {
	// do nothing
}

static std::string GetExeDirectory() {
	char exePath[MAX_PATH];
	GetModuleFileNameA(NULL, exePath, MAX_PATH);
	std::string exeDir(exePath);
	size_t lastSlash = exeDir.find_last_of("\\/");
	if (lastSlash != std::string::npos) {
		exeDir = exeDir.substr(0, lastSlash);
	}
	return exeDir;
}

static void CleanDirectory(const std::string& dirPath) {
	try {
		if (std::filesystem::exists(dirPath)) {
			for (const auto& entry : std::filesystem::directory_iterator(dirPath)) {
				if (entry.is_regular_file()) {
					std::filesystem::remove(entry.path());
				}
			}
			Logger::Get().Log(LogLevel::INFO, "[Clean] Cleaned directory: " + dirPath);
		}
	}
	catch (const std::exception& e) {
		Logger::Get().Log(LogLevel::WARNING, "[Clean] Could not clean directory " + dirPath + ": " + e.what());
	}
}

void Level::FreeTerrainCubeDataPools() {
	terrain_.FreeCubeDataPools();
}

bool Level::Load(load_params_s& params, glm::vec3& start_pos, float& start_yaw) {
	Unload();

	dyn_cube_item_pool_.Init((uint32_t)sizeof(dyn_cube_s), DYNAMIC_CUBE_POOL_CAPACITY, 4);
	root_dyn_cube_ = (dyn_cube_s*)dyn_cube_item_pool_.Alloc();

	memset(root_dyn_cube_, 0, sizeof(dyn_cube_s));

	root_dyn_cube_->cube_half_size_ = ROOT_CUBE_HALF_SIZE;
	root_dyn_cube_->idx_in_parent_children_array_ = 0;
	root_dyn_cube_->parent_ = nullptr;
	root_dyn_cube_->qtask_link_chain_ = nullptr;
	root_dyn_cube_->children_mask_ = 0;
	root_dyn_cube_->flags_ = 0;

	ConfigData& cfg = Config::Get();
	char filename[1024];
	std::string exeDir = GetExeDirectory();

	// Copy terrain from QEditor to executable directory on load
	try {
		CopyTerrainFromQEditor(params.level_no_);

		// Check if terrain folder exists in executable directory
		std::string terrainPath = exeDir + "\\terrains\\level" + std::to_string(params.level_no_) + "\\terrain";
		if (!std::filesystem::exists(terrainPath)) {
			Logger::Get().Log(LogLevel::ERR, "[Level] FATAL: ERROR Missing terrain folder at: " + std::string(terrainPath));
			return false;
		}
	}
	catch (const std::exception& e) {
		Logger::Get().Log(LogLevel::ERR, "[Level] FATAL: ERROR " + std::string(e.what()));
		return false;
	}

	// Priority 1: Executable directory (where igi-editor.exe is)
	Str_SPrintf(filename, 1024, "%s\\objects.qsc", exeDir.c_str());

	if (!File_Exists(filename)) {
		// Priority 2: Mission-specific folder in AppData/QFiles/IGI_QSC
		Str_SPrintf(filename, 1024, "%s/missions/location0/level%d/objects.qsc", g_folders.res_folder_, params.level_no_);

		if (!File_Exists(filename)) {
			// Priority 3: IGI Game Directory
			Str_SPrintf(filename, 1024, "%s\\missions\\location0\\level%d\\objects.qsc", cfg.igiPath.c_str(), params.level_no_);

			if (!File_Exists(filename)) {
				// Priority 4: Decompile into executable directory
				DecompileObjects(params.level_no_);
				Str_SPrintf(filename, 1024, "%s\\objects.qsc", exeDir.c_str());
			}
		}
	}

	qsc_path_ = filename;

	if (!File_Exists(filename)) {
		Logger::Get().Log(LogLevel::ERR, "[Level] FATAL: ERROR Missing 'objects.qsc' file at: " + qsc_path_);
		return false;
	}

	// Check if current objects.qsc differs from QEditor IGI_QSC version
	char appData[1024];
	GetEnvironmentVariableA("APPDATA", appData, 1024);
	char qeditorQsc[1024];
	Str_SPrintf(qeditorQsc, 1024, "%s\\QEditor\\QFiles\\IGI_QSC\\missions\\location0\\level%d\\objects.qsc", appData, params.level_no_);

	if (File_Exists(qeditorQsc) && FilesDiffer(filename, qeditorQsc)) {
		Logger::Get().Log(LogLevel::INFO, "[Level] objects.qsc has changes, compiling before loading...");
		CompileCurrentQSC(params.level_no_);
	}

	QSC* qsc_objects = new QSC();
	if (!qsc_objects) {
		return false;
	}

	qsc_objects->Load(filename);

	try {
		LoadStartPosInfo(qsc_objects, start_pos, start_yaw);
		LoadFogInfo(qsc_objects, params.render_res_loader_);
		LoadSkydomeInfo(qsc_objects, params.render_res_loader_);
		LoadFlatSkyLayersInfo(qsc_objects, params.render_res_loader_);

		Terrain::load_params_s terrain_load_params = {
			.level_no_ = params.level_no_,
			.level_dyn_cube_ = this,
			.render_res_loader_ = params.render_res_loader_,
			.qsc_objects_ = qsc_objects
		};


		terrain_.Load(terrain_load_params);

		level_objects_.Load(this, qsc_objects);


	}
	catch (const std::exception&) {
		delete qsc_objects;
		throw;	// re throw 
	}

	delete qsc_objects;

	loaded_ = true;

	cur_level_no_ = params.level_no_;
	
	return true;
}

void Level::DecompileObjects(int levelNo) {
	ConfigData& cfg = Config::Get();
	std::string igiPath = cfg.igiPath;

	char qvmPath[1024];
	Str_SPrintf(qvmPath, 1024, "%s\\missions\\location0\\level%d\\objects.qvm", igiPath.c_str(), levelNo);

	if (!File_Exists(qvmPath)) {
		Logger::Get().Log(LogLevel::ERR, "[Decompile] QVM file not found at: " + std::string(qvmPath));
		return;
	}


	char appData[1024];
	GetEnvironmentVariableA("APPDATA", appData, 1024);

	char decompileDir[1024];
	Str_SPrintf(decompileDir, 1024, "%s\\QEditor\\QCompiler\\Decompile", appData);

	char inputPath[1024];
	Str_SPrintf(inputPath, 1024, "%s\\input\\objects.qvm", decompileDir);

	// Clean input and output directories before decompiling
	char decompileInputDir[1024];
	Str_SPrintf(decompileInputDir, 1024, "%s\\input", decompileDir);
	char decompileOutputDir[1024];
	Str_SPrintf(decompileOutputDir, 1024, "%s\\output", decompileDir);
	CleanDirectory(decompileInputDir);
	CleanDirectory(decompileOutputDir);

	try {
		std::filesystem::create_directories(std::filesystem::path(inputPath).parent_path());
		std::filesystem::copy_file(qvmPath, inputPath, std::filesystem::copy_options::overwrite_existing);

		char cmd[2048];
		Str_SPrintf(cmd, 2048, "cd /d \"%s\" && decompile.bat", decompileDir);

		Logger::Get().Log(LogLevel::INFO, "[Decompile] Running decompiler: " + std::string(cmd));
		system(cmd);


		char outputPath[1024];
		Str_SPrintf(outputPath, 1024, "%s\\output\\objects.qsc", decompileDir);

		// Save to executable directory where igi-editor.exe is
		std::string exeDir = GetExeDirectory();
		char destPath[1024];
		Str_SPrintf(destPath, 1024, "%s\\objects.qsc", exeDir.c_str());

		if (std::filesystem::exists(outputPath)) {
			std::filesystem::copy_file(outputPath, destPath, std::filesystem::copy_options::overwrite_existing);
			Logger::Get().Log(LogLevel::INFO, "[Decompile] Success! Saved to executable directory: " + std::string(destPath));
		} else {
			Logger::Get().Log(LogLevel::ERR, "[Decompile] FAILED: Decompiler did not produce output at: " + std::string(outputPath));
		}

	}
	catch (const std::exception& e) {
		Logger::Get().Log(LogLevel::ERR, "[Decompile] Exception: " + std::string(e.what()));
	}

}

bool Level::FilesDiffer(const std::string& file1, const std::string& file2) {
	if (!std::filesystem::exists(file1) || !std::filesystem::exists(file2)) {
		return true; // If one doesn't exist, they differ
	}

	auto f1_time = std::filesystem::last_write_time(file1);
	auto f2_time = std::filesystem::last_write_time(file2);
	return f1_time != f2_time;
}

void Level::CompileCurrentQSC(int level_no) {
	char appData[1024];
	GetEnvironmentVariableA("APPDATA", appData, 1024);

	char compileDir[1024];
	Str_SPrintf(compileDir, 1024, "%s\\QEditor\\QCompiler\\Compile", appData);

	char inputPath[1024];
	Str_SPrintf(inputPath, 1024, "%s\\input\\objects.qsc", compileDir);

	char outputPath[1024];
	Str_SPrintf(outputPath, 1024, "%s\\output", compileDir);

	// Clean input and output directories before compiling
	char compileInputDir[1024];
	Str_SPrintf(compileInputDir, 1024, "%s\\input", compileDir);
	CleanDirectory(compileInputDir);
	CleanDirectory(outputPath);

	try {
		std::filesystem::create_directories(std::filesystem::path(inputPath).parent_path());

		// Copy editor's objects.qsc to compiler input
		std::string exeDir = GetExeDirectory();
		std::string editorQSC = exeDir + "\\objects.qsc";
		if (std::filesystem::exists(editorQSC)) {
			std::filesystem::copy_file(editorQSC, inputPath, std::filesystem::copy_options::overwrite_existing);
			Logger::Get().Log(LogLevel::INFO, "[Level] Copied objects.qsc to compiler input: " + std::string(inputPath));
		}

		char cmd[2048];
		Str_SPrintf(cmd, 2048, "cd /d \"%s\" && compile_v5.bat", compileDir);

		Logger::Get().Log(LogLevel::INFO, "[Level] Running compiler: " + std::string(cmd));
		system(cmd);

		// Deploy compiled QVM to IGI game path
		ConfigData& cfg = Config::Get();
		char qvmDest[1024];
		Str_SPrintf(qvmDest, 1024, "%s\\missions\\location0\\level%d", cfg.igiPath.c_str(), level_no);

		if (std::filesystem::exists(outputPath)) {
			std::filesystem::copy(outputPath, qvmDest,
				std::filesystem::copy_options::overwrite_existing | std::filesystem::copy_options::recursive);
			Logger::Get().Log(LogLevel::INFO, "[Level] Compiled QVM deployed to: " + std::string(qvmDest));
		}
	}
	catch (const std::exception& e) {
		Logger::Get().Log(LogLevel::ERR, "[Level] Compile exception: " + std::string(e.what()));
	}
}

void Level::CopyTerrainFromQEditor(int level_no) {
	char appData[1024];
	GetEnvironmentVariableA("APPDATA", appData, 1024);

	char srcTerrain[1024];
	Str_SPrintf(srcTerrain, 1024, "%s\\QEditor\\QFiles\\IGI_QSC\\missions\\location0\\level%d\\terrain", appData, level_no);

	// Copy to executable directory terrains\levelX\terrain
	std::string exeDir = GetExeDirectory();
	std::string dstTerrain = exeDir + "\\terrains\\level" + std::to_string(level_no) + "\\terrain";

	try {
		if (!std::filesystem::exists(srcTerrain)) {
			Logger::Get().Log(LogLevel::ERR, "[Level] FATAL: ERROR Missing terrain folder at: " + std::string(srcTerrain));
			throw std::runtime_error("Missing terrain folder in QEditor path");
		}

		std::filesystem::create_directories(dstTerrain);
		std::filesystem::copy(srcTerrain, dstTerrain,
			std::filesystem::copy_options::overwrite_existing | std::filesystem::copy_options::recursive);
		Logger::Get().Log(LogLevel::INFO, "[Level] Copied terrain from QEditor to: " + dstTerrain);
	}
	catch (const std::exception& e) {
		Logger::Get().Log(LogLevel::ERR, "[Level] Terrain copy exception: " + std::string(e.what()));
		throw;
	}
}

void Level::MoveTerrainToGamePath(int level_no) {
	// Source is executable directory terrains\levelX\terrain
	std::string exeDir = GetExeDirectory();
	std::string srcTerrain = exeDir + "\\terrains\\level" + std::to_string(level_no) + "\\terrain";

	ConfigData& cfg = Config::Get();
	char dstTerrain[1024];
	Str_SPrintf(dstTerrain, 1024, "%s\\missions\\location0\\level%d\\terrain", cfg.igiPath.c_str(), level_no);

	try {
		if (std::filesystem::exists(srcTerrain)) {
			// Remove existing destination terrain folder first to ensure clean copy
			if (std::filesystem::exists(dstTerrain)) {
				std::filesystem::remove_all(dstTerrain);
				Logger::Get().Log(LogLevel::INFO, "[Level] Removed old terrain at: " + std::string(dstTerrain));
			}
			std::filesystem::create_directories(std::filesystem::path(dstTerrain).parent_path());
			std::filesystem::copy(srcTerrain, dstTerrain,
				std::filesystem::copy_options::recursive);
			Logger::Get().Log(LogLevel::INFO, "[Level] Moved terrain from " + srcTerrain + " to game path: " + std::string(dstTerrain));
		}
		else {
			Logger::Get().Log(LogLevel::ERR, "[Level] Cannot move terrain, source not found: " + srcTerrain);
		}
	}
	catch (const std::exception& e) {
		Logger::Get().Log(LogLevel::ERR, "[Level] Terrain move exception: " + std::string(e.what()));
	}
}


void Level::Unload() {
	terrain_.Unload();
	level_objects_.Unload();


	for (int i = 0; i < MAX_FLAT_SKY_LAYERS; ++i) {
		flat_sky_layers_[i].Reset();
	}

	dyn_cube_item_pool_.Shutdown();
	root_dyn_cube_ = nullptr;

	loaded_ = false;
}

int	Level::GetLevelNo() const {
	return cur_level_no_;
}

void Level::Update(update_params_s& params) {
	params.flat_sky_layer_is_visible_ = false;

	if (!loaded_) {
		return;
	}

	FlatSkyLayer::fsl_update_params_s fsl_update_params = {};

	fsl_update_params.delta_seconds_ = params.delta_seconds_;
	fsl_update_params.vd_ = params.view_define_;
	fsl_update_params.vb_ = params.flat_sky_layer_vb_;
	fsl_update_params.flat_sky_fog_amount_ = flat_sky_fog_amount_;
	fsl_update_params.flat_sky_z_pos_ = flat_sky_z_pos_;
	fsl_update_params.flat_sky_distance_ = flat_sky_distance_;

	for (int i = 0; i < MAX_FLAT_SKY_LAYERS; ++i) {
		fsl_update_params.layer_no_ = i;

		params.flat_sky_layer_is_visible_ = flat_sky_layers_[i].Update(fsl_update_params);
	}

	terrain_.Update(params, root_dyn_cube_);
}

void Level::SaveChanges() {
	terrain_.Save(cur_level_no_);

	std::string exeDir = GetExeDirectory();
	std::string localQsc = exeDir + "\\objects.qsc";
	Logger::Get().Log(LogLevel::INFO, "[Level] Saving QSC to .exe path: " + localQsc);
	level_objects_.SaveToQSC(localQsc);
	Logger::Get().Log(LogLevel::INFO, "[Level] Saved QSC to: " + localQsc);

	// Move terrain from editor path to IGI game path
	MoveTerrainToGamePath(cur_level_no_);
}

bool Level::GetTerrainZ(const glm::vec3& pos, float& z) {
	if (root_dyn_cube_) {
		return terrain_.GetZ(root_dyn_cube_, pos, z);
	}
	else {
		return false;
	}
}

void Level::EditorRaycastAndModify(const glm::vec3& ray_origin, const glm::vec3& ray_dir, int brush_type) {
	if (root_dyn_cube_) {
		terrain_.EditorRaycastAndModify(root_dyn_cube_, ray_origin, ray_dir, brush_type);
	}
}

void Level::TeleportToHMP(glm::vec3& pos) const {
	terrain_.GetFirstHMPCenter(pos);
}

void Level::LoadStartPosInfo(const QSC* qsc_objects, glm::vec3& start_pos, float& start_yaw) const {
	start_pos.x = 0.0f;
	start_pos.y = 0.0f;
	start_pos.z = 175000000.0f;
	start_yaw = 0.0f;

	const QSC::func_s* qsc_funcs[1024];
	int num_func = qsc_objects->FindFuncByStr("HumanPlayer", qsc_funcs);
	if (num_func) {
		const QSC::func_s* f = qsc_funcs[0];	// read first function

		int arg_idx = 0;
		const QSC::arg_s* a = f->args_;
		while (a) {

			if (a->type_ == QSC::arg_s::type_t::DBL) {
				switch (arg_idx) {
				case 3:
					start_pos.x = (float)a->dbl_;
					break;
				case 4:
					start_pos.y = (float)a->dbl_;
					break;
				case 5:
					start_pos.z = (float)a->dbl_;
					break;
				case 6:
					start_yaw = (float)a->dbl_;
					break;
				}
			}

			a = a->next_;
			arg_idx++;
		}
	}
}

void Level::LoadFogInfo(const QSC* qsc_objects, IRenderResLoader* render_res_loader) {
	glm::vec4 fog_color(0.15f, 0.15f, 0.15f, 1.0f);
	float fog_far = 30000.0f;

	const QSC::func_s* qsc_funcs[1024];
	int num_func = qsc_objects->FindFuncByStr("GlobalLightKeyframe", qsc_funcs);
	if (num_func) {
		const QSC::func_s* f = qsc_funcs[0];	// read first function

		int arg_idx = 0;
		const QSC::arg_s * a = f->args_;
		while (a) {

			if (a->type_ == QSC::arg_s::type_t::DBL) {
				switch (arg_idx) {
				case 7:
					fog_color.r = (float)a->dbl_;
					break;
				case 8:
					fog_color.g = (float)a->dbl_;
					break;
				case 9:
					fog_color.b = (float)a->dbl_;
					break;
				case 10:
					// tune this
					fog_far = (1.0f / (float)a->dbl_) * 7200.0f;
					break;
				}
			}

			a = a->next_;
			arg_idx++;

			if (arg_idx > 10) {
				break;
			}
		}
	}

	render_res_loader->SetupFog(fog_color, fog_far);
}

void Level::LoadSkydomeInfo(const QSC* qsc_objects, IRenderResLoader* render_res_loader) {
	glm::vec4 flat_sky_fog_color(0.2f, 0.3f, 0.4f, 1.0f);

	skydome_define_s sd = {};

	const QSC::func_s* qsc_funcs[1024];
	int num_func = qsc_objects->FindFuncByStr("FlatSky", qsc_funcs);
	if (num_func) {
		const QSC::func_s* f = qsc_funcs[0];	// read first function

		int arg_idx = 0;
		const QSC::arg_s* a = f->args_;
		while (a) {

			if (a->type_ == QSC::arg_s::type_t::DBL) {
				switch (arg_idx) {
				case 3:
					flat_sky_fog_amount_ = (float)a->dbl_;
					break;
				case 4:
					flat_sky_z_pos_ = (float)a->dbl_;
					break;
				case 5:
					flat_sky_distance_ = (float)a->dbl_;
					break;
				case 6:
					flat_sky_fog_color.r = (float)a->dbl_;
					break;
				case 7:
					flat_sky_fog_color.g = (float)a->dbl_;
					break;
				case 8:
					flat_sky_fog_color.b = (float)a->dbl_;
					break;
				case 10:
					sd.angle_ = glm::radians((float)a->dbl_);
					break;
				case 11:
					sd.top_color1_[0] = (float)a->dbl_;
					sd.top_color2_[0] = (float)a->dbl_;
					break;
				case 12:
					sd.top_color1_[1] = (float)a->dbl_;
					sd.top_color2_[1] = (float)a->dbl_;
					break;
				case 13:
					sd.top_color1_[2] = (float)a->dbl_;
					sd.top_color2_[2] = (float)a->dbl_;
					break;
				case 14:
					sd.middle_color1_[0] = (float)a->dbl_;
					break;
				case 15:
					sd.middle_color1_[1] = (float)a->dbl_;
					break;
				case 16:
					sd.middle_color1_[2] = (float)a->dbl_;
					break;
				case 17:
					sd.middle_color2_[0] = (float)a->dbl_;
					break;
				case 18:
					sd.middle_color2_[1] = (float)a->dbl_;
					break;
				case 19:
					sd.middle_color2_[2] = (float)a->dbl_;
					break;
				case 20:
					sd.bottom_color1_[0] = (float)a->dbl_;
					break;
				case 21:
					sd.bottom_color1_[1] = (float)a->dbl_;
					break;
				case 22:
					sd.bottom_color1_[2] = (float)a->dbl_;
					break;
				case 23:
					sd.bottom_color2_[0] = (float)a->dbl_;
					break;
				case 24:
					sd.bottom_color2_[1] = (float)a->dbl_;
					break;
				case 25:
					sd.bottom_color2_[2] = (float)a->dbl_;
					break;
				}	// end swith
			}

			a = a->next_;
			arg_idx++;
		}
	}

	render_res_loader->SetupClearColor(flat_sky_fog_color);
	render_res_loader->SetupSkydome(sd);
}

void Level::LoadFlatSkyLayersInfo(const QSC* qsc_objects, IRenderResLoader* render_res_loader)
{
	const QSC::func_s* qsc_funcs[1024];
	int num_func = qsc_objects->FindFuncByStr("FlatSkyLayer", qsc_funcs);

	int num_layers = std::min(num_func, MAX_FLAT_SKY_LAYERS);
	for (int i = 0; i < num_layers; ++i) {

		const char* tex_file = "";
		glm::vec4 color(1.0f);
		float scale = 1.0f;
		float x_speed = 0.0f;
		float y_speed = 0.0f;

		const QSC::func_s* f = qsc_funcs[i];

		int arg_idx = 0;
		const QSC::arg_s* a = f->args_;
		while (a) {

			switch (arg_idx) {
			case 3:
				if (a->type_ == QSC::arg_s::type_t::STR) {
					tex_file = a->str_;
				}
				break;
			case 4:
				if (a->type_ == QSC::arg_s::type_t::DBL) {
					scale = (float)a->dbl_;
				}
				break;
			case 5:
				if (a->type_ == QSC::arg_s::type_t::DBL) {
					x_speed = (float)a->dbl_;
				}
				break;
			case 6:
				if (a->type_ == QSC::arg_s::type_t::DBL) {
					y_speed = (float)a->dbl_;
				}
				break;
			case 7:
				if (a->type_ == QSC::arg_s::type_t::DBL) {
					color.a = (float)a->dbl_;
				}
				break;
			case 8:
				if (a->type_ == QSC::arg_s::type_t::DBL) {
					color.r = (float)a->dbl_;
				}
				break;
			case 9:
				if (a->type_ == QSC::arg_s::type_t::DBL) {
					color.g = (float)a->dbl_;
				}
				break;
			case 10:
				if (a->type_ == QSC::arg_s::type_t::DBL) {
					color.b = (float)a->dbl_;
				}
				break;
			}

			a = a->next_;
			arg_idx++;

			if (arg_idx > 10) {
				break;
			}
		}

		flat_sky_layers_[i].Setup(i, render_res_loader,
			tex_file, color, scale, x_speed, y_speed);
	}
}

// get the cube whose lod_level equal to cube_lod_level and pos inside 
// if not exists then allocate a cube
dyn_cube_s* Level::GetDynCube(const double pos[3], int cube_lod_level, glm::ivec3& cube_ctr) {
	const Terrain::ctr_node_s* ctr_head = terrain_.GetCtr();
	const Terrain::ctr_node_s* ctr_node = ctr_head + 1;	// + 1: root node
	int cube_half_size = root_dyn_cube_->cube_half_size_;
	int lod_bit_shift = 30;
	uint8_t cube_trans_flag = 0;
	glm::ivec3 cube_node_ctr(0);

	dyn_cube_s* dyn_cube = root_dyn_cube_;

	// round to nearest integer
	glm::ivec3 rounded_pos;

	for (int i = 0; i < 3; ++i) {
		rounded_pos[i] = (int)pos[i];
		if (pos[i] < 0.0 && (double)rounded_pos[i] != pos[i]) {
			
			// pos[i] is negative and has decimal part
			// (int)pos[i] is the ceil
			// rounded_pos[i]-- is the floor

			rounded_pos[i]--;
		}
	}

	int lod_level = 0;

	// cube half size always be power of 2, only one bit is one

	// (0x40000000 >> 30) & 1 == 1

	// init pos
	//   if rounded_pos is negative
	//     ^ will make highest bit 0	???
	//   if rounded_pos is positive
	//     ^ will make highest bit 1	???
	// We only need to reason one dimension
	//  other two dimensions are all the same

	//  for positive number the ^ operator can be replaced by |

	glm::ivec3 int_pos_xor_root_cube_half_size;
	for (int i = 0; i < 3; ++i) {
		int_pos_xor_root_cube_half_size[i] =
			rounded_pos[i] ^ ROOT_CUBE_HALF_SIZE;
			// the ^ operation will keep bits of rounded_pos[i]
			//   but invert the bit 30 (0x40000000)
	}

	/* lod level in one dimension
	
	  lod:
	                 negative part     center (0)   positive part
	  0   |-------------------------------|-------------------------------|
	  1   |---------------|---------------|---------------|---------------|
	  2   |-------|-------|-------|-------|-------|-------|-------|-------|
	  3   |---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
	  4   |-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|
	  ...

	 */

	while (1) {
		// check highest bit by current cube
		int x_bit = (int_pos_xor_root_cube_half_size.x >> lod_bit_shift) & 1;
		int y_bit = (int_pos_xor_root_cube_half_size.y >> lod_bit_shift) & 1;
		int z_bit = (int_pos_xor_root_cube_half_size.z >> lod_bit_shift) & 1;

		// bit 0: go negative direction of that axis
		// bit 1: go positive direction of that axis

		// get child access order by cube_trans_flag
		const uint8_t* cube_idx_table_row = &Terrain::CUBE_IDX_TABLE[8 * cube_trans_flag];

		int child_choose_idx = cube_idx_table_row[(z_bit << 2) | (y_bit << 1) | x_bit];

		const Terrain::ctr_node_s* sub_ctr_node = ctr_head + ctr_node->children_[child_choose_idx];

		if ((dyn_cube->children_mask_ & (1 << child_choose_idx)) == 0) {
			// child cube not exists, allocate a new dynamic cube
			dyn_cube_s* child_dyn_cube = (dyn_cube_s*)dyn_cube_item_pool_.Alloc();

			memset(child_dyn_cube, 0, sizeof(dyn_cube_s));

			child_dyn_cube->cube_half_size_ = cube_half_size >> 1;
			child_dyn_cube->idx_in_parent_children_array_ = child_choose_idx;
			child_dyn_cube->parent_ = dyn_cube;	// link to parent
			child_dyn_cube->qtask_link_chain_ = nullptr;
			child_dyn_cube->children_mask_ = 0;	// no children yet
			child_dyn_cube->flags_ = 0;

			// update children mask
			dyn_cube->children_mask_ |= (1 << child_choose_idx);
			dyn_cube->children_[child_choose_idx] = child_dyn_cube;	// link child

			dyn_cube = child_dyn_cube;
		}
		else {
			dyn_cube = dyn_cube->children_[child_choose_idx];
		}

		uint8_t child_cube_trans_flag = ctr_node->cmd_transform_[child_choose_idx];

		// update cube_trans_flag
		if (cube_trans_flag >= 4 /* mesh flipped */) {
			cube_trans_flag = ((cube_trans_flag ^ child_cube_trans_flag) & 4) + ((cube_trans_flag - child_cube_trans_flag) & 3);
		}
		else {
			cube_trans_flag = (child_cube_trans_flag & 4) + ((child_cube_trans_flag + cube_trans_flag) & 3);
		}

		// assign child node to ctr_node
		ctr_node = sub_ctr_node;

		// decrease cube half size for child node
		cube_half_size >>= 1;
		lod_bit_shift--;

		/* children order
			   z     y
			   |    /
			 6 |   /  7
			   |  /
		  4    | /  5
	  _________|/__________x
			 2 |      3
			   |
		  0    |    1
		       |
			   |

		bits:
		z  y  x
		---------|---
		0  0  0  |  0
		0  0  1  |  1
		0  1  0  |  2
		0  1  1  |  3
		1  0  0  |  4
		1  0  1  |  5
		1  1  0  |  6
		1  1  1  |  7
		              
		bit_case	   child case    axis dir
		-------------|------------|-----------
		x_bit case 0 | 0, 2, 4, 6 |  -x
		x_bit case 1 | 1, 3, 5, 7 |  +x
		y_bit case 0 | 0, 1, 4, 5 |  -y
		y_bit case 1 | 2, 3, 6, 7 |  +y
		z_bit case 0 | 0, 1, 2, 3 |  -z
		z_bit case 1 | 4, 5, 6, 7 |  +z

		*/

		cube_node_ctr.x -= x_bit + (cube_half_size ^ -x_bit);
		cube_node_ctr.y -= y_bit + (cube_half_size ^ -y_bit);
		cube_node_ctr.z -= z_bit + (cube_half_size ^ -z_bit);

		// increase lod level for child node
		lod_level++;

		if (lod_level >= cube_lod_level) {
			cube_ctr = cube_node_ctr;
			break;
		}
		// else: continue check child cube node
	}

	return dyn_cube;
}

void Level::AddQTaskToDynCube(dyn_cube_s* dyn_cube, qtask_s* qtask) {
	if (dyn_cube->qtask_link_chain_) {

		qtask->prior_ = nullptr;
		qtask->next_ = dyn_cube->qtask_link_chain_;

		dyn_cube->qtask_link_chain_->prior_ = qtask;
		dyn_cube->qtask_link_chain_ = qtask;
	}
	else {
		qtask->prior_ = nullptr;
		qtask->next_ = nullptr;

		dyn_cube->qtask_link_chain_ = qtask;
	}
}
