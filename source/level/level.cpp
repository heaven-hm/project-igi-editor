/******************************************************************************
 * @file    level.cpp
 * @brief   level
 *****************************************************************************/

#include "pch.h"
#include <filesystem>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include "logger.h"
#include "utils.h"
#include "cli/asset_extractor.h"
#include "parsers/qvm_parser.h"
#include "parsers/qvm_decompiler.h"
#include "parsers/qsc_lexer.h"
#include "parsers/qsc_parser.h"
#include "parsers/qvm_compiler.h"



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
			// Completely remove the directory and all its contents
			std::filesystem::remove_all(dirPath);
			Logger::Get().Log(LogLevel::INFO, "[Clean] Removed directory: " + dirPath);
		}
		// Recreate it fresh
		std::filesystem::create_directories(dirPath);
		Logger::Get().Log(LogLevel::INFO, "[Clean] Created fresh directory: " + dirPath);
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

	// Extract textures and models from IGI .res archives (cached per-level)
	AssetExtractor::EnsureLevelAssets(params.level_no_, Utils::GetIGIRootPath(), exeDir);
	// Extract textures from all other levels once per session so cross-level
	// texture references (e.g. model 618 in level 3 whose textures live in level 6)
	// are available for the FindTextureFile cross-level search.
	AssetExtractor::EnsureAllLevelTextures(Utils::GetIGIRootPath(), exeDir);

	// Verify terrain folder exists in game directory
	try {
		std::string terrainPath = Utils::GetIGIRootPath() + "\\missions\\location0\\level" + std::to_string(params.level_no_) + "\\terrain";
		if (!std::filesystem::exists(terrainPath)) {
			Logger::Get().Log(LogLevel::ERR, "[Level] FATAL: ERROR Missing terrain folder at: " + terrainPath);
			return false;
		}
	}
	catch (const std::exception& e) {
		Logger::Get().Log(LogLevel::ERR, "[Level] FATAL: ERROR " + std::string(e.what()));
		return false;
	}

	// Always decompile from objects.qvm — never use a cached QSC.
	char igiQvm[1024];
	Str_SPrintf(igiQvm, 1024, "%s\\missions\\location0\\level%d\\objects.qvm", Utils::GetIGIRootPath().c_str(), params.level_no_);

	if (!File_Exists(igiQvm)) {
		Logger::Get().Log(LogLevel::ERR, "[Level] FATAL: objects.qvm not found at: " + std::string(igiQvm));
		return false;
	}

	Logger::Get().Log(LogLevel::INFO, "[Level] Decompiling objects.qvm for level " + std::to_string(params.level_no_));
	DecompileObjects(params.level_no_);
	Str_SPrintf(filename, 1024, "%s\\objects.qsc", exeDir.c_str());

	qsc_path_ = filename;

	if (!File_Exists(filename)) {
		Logger::Get().Log(LogLevel::ERR, "[Level] FATAL: ERROR Missing 'objects.qsc' file at: " + qsc_path_);
		return false;
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
	std::string igiPath = Utils::GetIGIRootPath();

	char qvmPath[1024];
	Str_SPrintf(qvmPath, 1024, "%s\\missions\\location0\\level%d\\objects.qvm", igiPath.c_str(), levelNo);

	if (!File_Exists(qvmPath)) {
		Logger::Get().Log(LogLevel::ERR, "[Decompile] QVM file not found at: " + std::string(qvmPath));
		return;
	}

	std::string exeDir = GetExeDirectory();
	char destPath[1024];
	Str_SPrintf(destPath, 1024, "%s\\objects.qsc", exeDir.c_str());

	try {
		// Use internal C++ decompiler
		Logger::Get().Log(LogLevel::INFO, "[Decompile] Decompiling " + std::string(qvmPath) + " -> " + std::string(destPath));
		
		QVMFile qvm = QVM_Parse(qvmPath);
		if (qvm.valid) {
			if (QVM_Decompile(qvm, destPath)) {
				Logger::Get().Log(LogLevel::INFO, "[Decompile] Success! Saved to executable directory: " + std::string(destPath));
				Utils::TrimFileInPlace(destPath);
			} else {
				Logger::Get().Log(LogLevel::ERR, "[Decompile] FAILED: Decompiler failed to write output.");
			}
		} else {
			Logger::Get().Log(LogLevel::ERR, "[Decompile] FAILED: Could not parse QVM: " + qvm.error);
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
	std::string exeDir = GetExeDirectory();
	std::string editorQSC = exeDir + "\\objects.qsc";

	if (!std::filesystem::exists(editorQSC)) {
		Logger::Get().Log(LogLevel::ERR, "[Level] FAILED: Source QSC not found: " + editorQSC);
		return;
	}

	// Deploy compiled QVM to IGI game path
	char qvmDest[1024];
	Str_SPrintf(qvmDest, 1024, "%s\\missions\\location0\\level%d\\objects.qvm", Utils::GetIGIRootPath().c_str(), level_no);

	try {
		Logger::Get().Log(LogLevel::INFO, "[Level] Compiling " + editorQSC + " -> " + std::string(qvmDest));
		std::ifstream qscFile(editorQSC);
		std::string qscSrc((std::istreambuf_iterator<char>(qscFile)), std::istreambuf_iterator<char>());
		auto lexR   = qsc::Lex(qscSrc);
		auto parseR = lexR.ok ? qsc::Parse(lexR.tokens) : qsc::ParseResult{};
		std::string compErr;
		bool compiled = lexR.ok && parseR.ok &&
		                qvm::CompileToFile(*parseR.program, std::string(qvmDest), &compErr);
		if (compiled) {
			Logger::Get().Log(LogLevel::INFO, "[Level] SUCCESS! Compiled QVM deployed to: " + std::string(qvmDest));
		} else {
			Logger::Get().Log(LogLevel::ERR, "[Level] FAILED: " + (compErr.empty() ? lexR.error + parseR.error : compErr));
		}
	}
	catch (const std::exception& e) {
		Logger::Get().Log(LogLevel::ERR, "[Level] Compile exception: " + std::string(e.what()));
	}
}

void Level::CopyTerrainFromIGI(int level_no) {
	// Obsolete - terrain is loaded directly from IGI game path
}

void Level::MoveTerrainToGamePath(int level_no) {
	// Obsolete - terrain is saved directly to IGI game path
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

void Level::SaveObjectsLocalOnly() {
	std::string exeDir = GetExeDirectory();
	std::string localQsc = exeDir + "\\objects.qsc";
	Logger::Get().Log(LogLevel::INFO, "[Level] Saving local live QSC to: " + localQsc);
	level_objects_.SaveToQSC(localQsc);
}

void Level::SaveChanges() {
	terrain_.Save(cur_level_no_);

	// IMPORTANT: AppData\Roaming\QEditor\QFiles is READ-ONLY.
	// We NEVER write back to the QFiles source. All saves go to the local
	// exe-directory copy of objects.qsc, which is then compiled to QVM.
	SaveObjectsLocalOnly();
}

void Level::SaveAndReloadObjects() {
	std::string exeDir = GetExeDirectory();
	std::string localQsc = exeDir + "\\objects.qsc";

	// Helper to get a unique tree path for an object to preserve expansion state
	auto GetObjectTreePath = [](const std::vector<LevelObject>& objects, int idx) -> std::string {
		std::string path;
		int curr = idx;
		while (curr >= 0 && curr < (int)objects.size()) {
			const auto& obj = objects[curr];
			std::string part = obj.type + ":" + obj.name + ":" + obj.taskId;
			if (path.empty()) {
				path = part;
			} else {
				path = part + "/" + path;
			}
			curr = obj.parentIndex;
		}
		return path;
	};

	// 1. Gather expanded states of containers before reloading
	std::unordered_map<std::string, bool> expandedStates;
	const auto& oldObjs = level_objects_.GetObjects();
	for (int i = 0; i < (int)oldObjs.size(); ++i) {
		if (oldObjs[i].isContainer && oldObjs[i].expanded) {
			expandedStates[GetObjectTreePath(oldObjs, i)] = true;
		}
	}

	// 2. Save changes to local QSC file
	level_objects_.SaveToQSC(localQsc);

	// 3. Reload objects from the saved file to ensure live synchronization
	QSC* qsc = new QSC();
	if (qsc) {
		qsc->Load(localQsc.c_str());
		level_objects_.Load(this, qsc);
		delete qsc; // LevelObjects::Load copies everything, so we can free the temp QSC

		// 4. Restore expanded states to the newly loaded objects
		auto& newObjs = level_objects_.GetObjects();
		for (int i = 0; i < (int)newObjs.size(); ++i) {
			if (newObjs[i].isContainer) {
				std::string path = GetObjectTreePath(newObjs, i);
				if (expandedStates.find(path) != expandedStates.end()) {
					newObjs[i].expanded = true;
				}
			}
		}

		Logger::Get().Log(LogLevel::INFO, "[Level] SaveAndReloadObjects: Synchronized and expansion states preserved for: " + localQsc);
	}
}

bool Level::GetTerrainZ(double x, double y, float& z, bool ignore_discard) {
	if (root_dyn_cube_) {
		return terrain_.GetZ(root_dyn_cube_, x, y, z, ignore_discard);
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
