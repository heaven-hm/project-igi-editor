/******************************************************************************
 * @file    terrain.cpp
 * @brief   terrain
 *****************************************************************************/

#include "pch.h"
#include "terrain_files.h"
#include <filesystem>

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

static std::string GetTerrainDir(int level_no) {
	return GetExeDirectory() + "\\terrains\\level" + std::to_string(level_no) + "\\terrain";
}

/*
================================================================================
 Terrain
================================================================================
*/

// tune these values
constexpr int CUBE_DATA_1600_POOL_CAPACITY = 700;
constexpr int CUBE_DATA_3200_POOL_CAPACITY = 50;
constexpr int CUBE_DATA_8000_POOL_CAPACITY = 16;

const double	SQRT_3 = 1.7320508075688773;
const double	ONE_OVER_3 = 1.0 / 3.0;
const float		ONE_OVER_256 = 1.0f / 256.0f;

// cube lod control

constexpr	float	CUBE_ACTIVE_COMPARE = 163.0f;
constexpr	int		CUBE_ACTIVE_COMPARE_INT = (int)CUBE_ACTIVE_COMPARE;
constexpr	float	CUBE_START_MERGE_DELTA = 64.0;
constexpr	float	CUBE_ACTIVE_COMPARE_ADD_CUBE_START_MERGE_DELTA = CUBE_ACTIVE_COMPARE + CUBE_START_MERGE_DELTA;
constexpr	float	CUBE_ACTIVE_COMPARE_SUB_CUBE_START_MERGE_DELTA = CUBE_ACTIVE_COMPARE - CUBE_START_MERGE_DELTA;
constexpr	float	CUBE_MORPHING_FACTOR_COMPARE = CUBE_ACTIVE_COMPARE_ADD_CUBE_START_MERGE_DELTA / CUBE_ACTIVE_COMPARE_SUB_CUBE_START_MERGE_DELTA;


/* children nodes default layout

			Z     Y
			|    /
		6	|   / 7
			|  /
	  4		| /	5
  __________|/__________X
		2   /	  3
		   /|
	  0	  / |	1
         /	|
		    |

 */

// child access indices when applied transform on a cube
const uint8_t Terrain::CUBE_IDX_TABLE[] = {
	0, 1, 2, 3, 4, 5, 6, 7,		// tranform case 0: not transform
	2, 0, 3, 1, 6, 4, 7, 5,		// tranform case 1: rotate round Z axis  90 degrees (CCW)
	3, 2, 1, 0, 7, 6, 5, 4,		// tranform case 2: rotate round Z axis 180 degrees (CCW)
	1, 3, 0, 2, 5, 7, 4, 6,		// tranform case 3: rotate round Z axis -90 degrees (CCW)
	1, 0, 3, 2, 5, 4, 7, 6,		// tranform case 4: flip along YOZ plane
	3, 1, 2, 0, 7, 5, 6, 4,		// tranform case 5: flip along YOZ plane and rotate round Z axis  90 degrees (CCW)
	2, 3, 0, 1, 6, 7, 4, 5,		// tranform case 6: flip along YOZ plane and rotate round Z axis 180 degrees (CCW)
	0, 2, 1, 3, 4, 6, 5, 7,		// tranform case 7: flip along YOZ plane and rotate round Z axis -90 degrees (CCW)
};

// combined transforms of parent & child cube
// rows:    parent cube tranform case, from 0 ~ 7
// columns: child cube tranform case defined in ctr_node_s
// value:   child cube final transform case 
const uint8_t Terrain::CUBE_TRANS_TABLE[] = {
	0, 1, 2, 3, 4, 5, 6, 7,
	1, 2, 3, 0, 5, 6, 7, 4,
	2, 3, 0, 1, 6, 7, 4, 5,
	3, 0, 1, 2, 7, 4, 5, 6,
	4, 7, 6, 5, 0, 3, 2, 1,
	5, 4, 7, 6, 1, 0, 3, 2,
	6, 5, 4, 7, 2, 1, 0, 3,
	7, 6, 5, 4, 3, 2, 1, 0,
};

Terrain::Terrain():
	cmd_(nullptr),
	cmd_sz_(0),
	ctr_(nullptr),
	bit_(nullptr),
	hmp_(nullptr),
	num_texture_modifier_(0),
	num_terrain_light_map_(0),
	num_height_map_(0),
	num_discard_terrain_(0),
	num_render_cube_(0),
	num_cube_data_(0),
	material_indices_stack_size_(0),
	light_map_stack_size_(0),
	height_map_stack_size_(0),
	pre_frame_(-1),
	cur_frame_(0),
	pre_mod_options_(0),
	cur_mod_options_(0),
	vertices_(nullptr),
	indices_(nullptr),
	render_chunks_(nullptr),
	num_vertex_(0),
	num_index_(0),
	num_render_chunk_(0),
	tan_half_fovx_(0.0f),
	tan_half_fovy_(0.0f),
	get_z_is_int_pos_(false),
	get_z_ix_(0),
	get_z_iy_(0)
{
	// init cube data item pool
	cube_data_1600_bytes_item_pool_.Init(1600, CUBE_DATA_1600_POOL_CAPACITY, 4);
	cube_data_3200_bytes_item_pool_.Init(3200, CUBE_DATA_3200_POOL_CAPACITY, 4);
	cube_data_8000_bytes_item_pool_.Init(8000, CUBE_DATA_8000_POOL_CAPACITY, 4);

	memset(render_cubes_, 0, sizeof(render_cubes_));
	memset(cube_data_array_, 0, sizeof(cube_data_array_));

	memset(cube_data_hash_, 0, sizeof(cube_data_hash_));
	for (int i = 0; i < CUBE_DATA_HASH_ITEM_COUNT; ++i) {
		cube_data_hash_[i].frame_ = INVALID_FRAME;
	}

	memset(material_indices_stack_, 0, sizeof(material_indices_stack_));
	memset(light_map_stack_, 0, sizeof(light_map_stack_));
	memset(height_map_stack_, 0, sizeof(height_map_stack_));

	int* t = dim_table_;

	for (int z = -1; z <= 1; z += 2) {
		for (int y = -1; y <= 1; y += 2) {
			for (int x = -1; x <= 1; x += 2) {
				t[0] = x * ROOT_CUBE_HALF_SIZE;
				t[1] = y * ROOT_CUBE_HALF_SIZE;
				t[2] = z * ROOT_CUBE_HALF_SIZE;

				t += 3;
			}
		}
	}

	memcpy(dim_table_rotated_, dim_table_, sizeof(dim_table_rotated_));

	memset(bitmap_items_, 0, sizeof(bitmap_items_));
	memset(height_map_items_, 0, sizeof(height_map_items_));

#ifdef _DEBUG
	memset(bitmap_item_size_array_, 0, sizeof(bitmap_item_size_array_));
	memset(light_map_item_size_array_, 0, sizeof(light_map_item_size_array_));
	memset(height_map_item_size_array_, 0, sizeof(height_map_item_size_array_));
#endif

	cur_int_view_pos_ = glm::ivec3(0);
	cur_int_view_pos_rotated_ = glm::ivec3(0);

	get_z_pos_ = VEC3_ORIGIN;
}

Terrain::~Terrain() {
	Unload();
	FreeCubeDataPools();
}

void Terrain::FreeCubeDataPools() {
	cube_data_8000_bytes_item_pool_.Shutdown();
	cube_data_3200_bytes_item_pool_.Shutdown();
	cube_data_1600_bytes_item_pool_.Shutdown();
}

bool Terrain::Load(load_params_s & params) {
	Unload();

#ifdef _DEBUG
	memset(bitmap_item_size_array_, 0, sizeof(bitmap_item_size_array_));
	memset(light_map_item_size_array_, 0, sizeof(light_map_item_size_array_));
	memset(height_map_item_size_array_, 0, sizeof(height_map_item_size_array_));
#endif

	char filename[1024];
	std::string terrainDir = GetTerrainDir(params.level_no_);
	Str_SPrintf(filename, 1024,
		"%s/terrain.qsc",
		terrainDir.c_str());

	QSC* qsc_terrain = new QSC();
	if (!qsc_terrain) {
		return false;
	}

	qsc_terrain->Load(filename);

	LoadMaterialInfo(qsc_terrain);
	LoadTileMapInfo(qsc_terrain);

	delete qsc_terrain;

	if (!LoadCMDFile(params)) {
		return false;
	}

	if (!LoadCTRFile(params)) {
		return false;
	}

	if (!LoadTEXFile(params)) {
		return false;
	}

	if (!LoadLMPFile(params)) {
		return false;
	}

	if (!LoadBITFile(params)) {
		return false;
	}

	if (!LoadHMPFile(params)) {
		return false;
	}

	LoadTextureModifier(params.level_dyn_cube_, params.qsc_objects_);
	LoadTerrainLightMapInfo(params.level_dyn_cube_, params.qsc_objects_);
	LoadHeightMapInfo(params.level_dyn_cube_, params.qsc_objects_);
	LoadDiscardTerrainInfo(params.level_dyn_cube_, params.qsc_objects_);

	return true;
}

bool Terrain::Save(int level_no) {
	char filename[1024];
	// Save to executable directory terrains\levelX\terrain
	std::string terrainDir = GetTerrainDir(level_no);
	std::filesystem::create_directories(terrainDir);
	Str_SPrintf(filename, 1024,
		"%s\\terrain.hmp",
		terrainDir.c_str());

	if (!hmp_) {
		return false;
	}

	// We need to know the total size of the buffer to save it.
	// Since File_LoadBinary didn't store the size globally, we recalculate it.
	int32_t total_body_size = 0;
	hmp_item_s* head = (hmp_item_s*)hmp_;
	for (int i = 0; i < MAX_HMP; ++i) {
		uint32_t sz = head[i].size_;
		if (sz) {
			total_body_size += SQUARE(sz + 1);
		}
	}

	int32_t total_file_size = sizeof(hmp_item_s) * MAX_HMP + total_body_size;

	if (File_SaveBinary(filename, hmp_, total_file_size)) {
		printf("Successfully saved terrain modifications to %s\n", filename);
		return true;
	}

	return false;
}

void Terrain::Unload() {
	File_FreeBuf(hmp_);
	hmp_ = nullptr;
	File_FreeBuf(bit_);
	bit_ = nullptr;
	SAFE_FREE(ctr_);
	File_FreeBuf(cmd_);
	cmd_ = nullptr;
	cmd_sz_ = 0;

	ClearCubeDataHash();
}

const Terrain::ctr_node_s* Terrain::GetCtr() const {
	return ctr_;
}

void Terrain::LoadMaterialInfo(const QSC* qsc_terrain) {
	const QSC::func_s* qsc_funcs[1024];
	int num_func = qsc_terrain->FindFuncByName("CreateTerrainMaterial", qsc_funcs);
	for (int i = 0; i < num_func; ++i) {
		const QSC::func_s* f = qsc_funcs[i];

		const QSC::arg_s* a = f->args_;
		if (a && a->type_ == QSC::arg_s::type_t::DBL) {
			int group_idx = (int)a->dbl_;

			material_array_s* ma = materials_.material_array_map_ + group_idx;

			a = a->next_;
			if (a && a->type_ == QSC::arg_s::type_t::DBL) {
				ma->id_ = (int)a->dbl_;

				a = a->next_;
				int j = 0;
				material_s* m = ma->material_array_;

				while (a) {
					switch (j % 7) {
					case 0:
						if (a->type_ == QSC::arg_s::type_t::BOOL) {
							m->bool_ = a->bool_;
						}
						break;
					case 1:
						if (a->type_ == QSC::arg_s::type_t::DBL) {
							m->v1_ = (float)a->dbl_;
						}
						break;
					case 2:
						if (a->type_ == QSC::arg_s::type_t::DBL) {
							m->v2_ = (float)a->dbl_;
						}
						break;
					case 3:
						if (a->type_ == QSC::arg_s::type_t::DBL) {
							m->f_tex2_ = (float)a->dbl_ * 0.5f;
						}
						break;
					case 4:
						if (a->type_ == QSC::arg_s::type_t::DBL) {
							m->tex2_tile_map_group_idx_ = (int)a->dbl_;
						}
						break;
					case 5:
						if (a->type_ == QSC::arg_s::type_t::DBL) {
							m->f_tex1_ = (float)a->dbl_ * 0.5f;
						}
						break;
					case 6:
						if (a->type_ == QSC::arg_s::type_t::DBL) {
							m->tex1_tile_map_group_idx_ = (int)a->dbl_;
						}
						break;
					}

					a = a->next_;
					j++;

					if (j % 7 == 0) {
						m++;	// next item
					}
				}

			}
		}
	}
}

void Terrain::LoadTileMapInfo(const QSC* qsc_terrain) {
	const QSC::func_s* qsc_funcs[1024];
	int num_func = qsc_terrain->FindFuncByName("CreateTerrainTileMap", qsc_funcs);
	for (int i = 0; i < num_func; ++i) {
		const QSC::func_s* f = qsc_funcs[i];

		const QSC::arg_s* a = f->args_;
		if (a && a->type_ == QSC::arg_s::type_t::DBL) {
			int group_idx = (int)a->dbl_;

			tile_map_s* ttm = tile_maps_ + group_idx;

			a = a->next_;
			int j = 0;

			while (a) {
				if (a->type_ == QSC::arg_s::type_t::DBL) {
					ttm->tex_idx_in_terrain_tex_[j] = (int)a->dbl_;
				}

				a = a->next_;
				j++;

				if (j >= 64) {
					break;
				}
			}
		}
	}
}

bool Terrain::LoadCMDFile(load_params_s & params) {
	char filename[1024];
	std::string terrainDir = GetTerrainDir(params.level_no_);
	Str_SPrintf(filename, 1024,
		"%s/terrain.cmd",
		terrainDir.c_str());

	return File_LoadBinary(filename, cmd_, cmd_sz_);
}

bool Terrain::LoadCTRFile(load_params_s & params) {
	char filename[1024];
	std::string terrainDir = GetTerrainDir(params.level_no_);
	Str_SPrintf(filename, 1024,
		"%s/terrain.ctr",
		terrainDir.c_str());

	ctr_s ctr_file_contents = {};
	if (!CTR_Load(filename, ctr_file_contents)) {
		return false;
	}

	ctr_ = (ctr_node_s*)MEM_ALLOC(sizeof(ctr_node_s) * ctr_file_contents.num_item_);
	if (!ctr_) {
		CTR_Free(ctr_file_contents);
		return false;
	}

	for (int i = 0; i < ctr_file_contents.num_item_; ++i) {
		const ctr_item_s* src = ctr_file_contents.head_ + i;
		ctr_node_s* dst = ctr_ + i;

		assert((int)src->cmd_offset_ < cmd_sz_);

		memcpy(dst, src, sizeof(*src));

		// link to cmd item
		dst->cmd_ = (cmd_item_s*)((char*)cmd_ + src->cmd_offset_);
	}

	CTR_Free(ctr_file_contents);

	return true;
}

bool Terrain::LoadTEXFile(load_params_s & params) {
	char filename[1024];
	std::string terrainDir = GetTerrainDir(params.level_no_);
	Str_SPrintf(filename, 1024,
		"%s/terrain.tex",
		terrainDir.c_str());

	pics_s pics = {};
	if (!Tex_Load(filename, pics)) {
		return false;
	}

	try {
		for (int i = 0; i < pics.num_pic_; ++i) {
			params.render_res_loader_->LoadTerrainMatTex(pics.pics_ + i);
		}
	}
	catch (const std::exception&) {
		Pic_FreePics(pics);
		throw;	// re throw 
	}

	Pic_FreePics(pics);

	return true;
}

bool Terrain::LoadLMPFile(load_params_s & params) {
	char filename[1024];
	std::string terrainDir = GetTerrainDir(params.level_no_);
	Str_SPrintf(filename, 1024,
		"%s/terrain.lmp",
		terrainDir.c_str());

	pics_s pics = {};
	if (!LMP_Load(filename, pics)) {
		return false;
	}

	try {
		for (int i = 0; i < pics.num_pic_; ++i) {
			params.render_res_loader_->LoadTerrainLMPTex(pics.pics_ + i);

#ifdef _DEBUG
			assert(pics.pics_[i].width_ == pics.pics_[i].height_);
			light_map_item_size_array_[i] = pics.pics_[i].width_;
#endif
		}
	}
	catch (const std::exception&) {
		Pic_FreePics(pics);
		throw;	// re throw 
	}

	Pic_FreePics(pics);

	return true;
}

bool Terrain::LoadBITFile(load_params_s & params) {
	char filename[1024];
	std::string terrainDir = GetTerrainDir(params.level_no_);
	Str_SPrintf(filename, 1024,
		"%s/terrain.bit",
		terrainDir.c_str());

	int32_t bit_sz = 0;
	if (!File_LoadBinary(filename, bit_, bit_sz)) {
		return false;
	}

	bit_item_s* head = (bit_item_s*)bit_;

	uint8_t* body = (uint8_t*)(head + MAX_TEX_MOD);
	uint8_t* cur_body = body;

	for (int i = 0; i < MAX_TEX_MOD; ++i) {
		uint32_t sz = head[i].size_;
		if (sz) {
			int32_t cur_body_sz = (sz * sz) / 8;	// each slot is one bit
			bitmap_items_[i] = cur_body;
			cur_body += cur_body_sz;
		}
		else {
			bitmap_items_[i] = nullptr;
		}

#ifdef _DEBUG
		bitmap_item_size_array_[i] = sz;
#endif
	}

	return true;
}

bool Terrain::LoadHMPFile(load_params_s & params) {
	char filename[1024];
	std::string terrainDir = GetTerrainDir(params.level_no_);
	Str_SPrintf(filename, 1024,
		"%s/terrain.hmp",
		terrainDir.c_str());

	if (!File_Exists(filename)) {
		return true;	// not an error, hmp file can be omitted
	}

	int32_t hmp_sz = 0;
	if (!File_LoadBinary(filename, hmp_, hmp_sz)) {
		return false;
	}

	hmp_item_s* head = (hmp_item_s*)hmp_;

	int8_t * body = (int8_t*)(head + MAX_HMP);
	int8_t * cur_body = body;

	for (int i = 0; i < MAX_HMP; ++i) {
		uint32_t sz = head[i].size_;
		if (sz) {
			// real size is power of 2 + 1
			int32_t cur_body_sz = SQUARE(sz + 1);
			height_map_items_[i] = cur_body;
			cur_body += cur_body_sz;
		}
		else {
			height_map_items_[i] = nullptr;
		}

#ifdef _DEBUG
		height_map_item_size_array_[i] = sz;
#endif
	}

	return true;
}

void Terrain::LoadTextureModifier(ILevelDynCube* level_dyn_cube, const QSC* qsc_objects) {
	/* 

	"TextureModifier" examples from level 5:

	  Task_DeclareParameters("TextureModifier", "Position", "ObjectPos", "Level", "Int32", "Material Index", "Int32", "Bitmap ID", "Int32", "Size", "Int32", "isEdit", "bool8");
															   lod  mat_idx  bit idx  size
	  Task_New(-1, "TextureModifier", "Rock",           -25865818.0, -33569448.0, 180876336.0,  6,  5,       60,      256, FALSE),
	  Task_New(-1, "TextureModifier", "Grass",          -24373846.0, -35335356.0, 181129328.0,  7,  1,        1,      256, FALSE),
	  Task_New(-1, "TextureModifier", "Dirt",           -24373846.0, -35335356.0, 181129328.0, 11,  2,        3,      128, FALSE),
	  Task_New(-1, "TextureModifier", "Tarmat",         -24373846.0, -35335356.0, 181129328.0,  8,  4,        2,      128, FALSE),
	  Task_New(-1, "TextureModifier", "Dirt compound2", -26109950.0, -35487336.0, 180756432.0, 12,  2,       22,      128, FALSE)),

	 */
	
	num_texture_modifier_ = 0;

	const QSC::func_s* qsc_funcs[1024];
	int num_func = qsc_objects->FindFuncByStr("TextureModifier", qsc_funcs);

	for (int i = 0; i < num_func; ++i) {
		const QSC::func_s* f = qsc_funcs[i];

		const QSC::arg_s* a = f->args_;

		if (num_texture_modifier_ >= MAX_TEX_MOD) {
			Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "Too many texturemodfiers\n");
			return;
		}

		qtask_texture_modifier_s* qtask_texture_modifier = qtask_texture_modifiers_ + num_texture_modifier_;

		qtask_texture_modifier->task_type_ = TASK_TYPE_TERRAIN_TEXTURE_MODIFIER;
		qtask_texture_modifier->idx_in_array_ = num_texture_modifier_;

		int arg_idx = 0;
		while (a) {

			switch (arg_idx) {
			case 3:
				qtask_texture_modifier->script_defined_pos_[0] = a->dbl_;
				break;
			case 4:
				qtask_texture_modifier->script_defined_pos_[1] = a->dbl_;
				break;
			case 5:
				qtask_texture_modifier->script_defined_pos_[2] = a->dbl_;
				break;
			case 6:
				qtask_texture_modifier->lod_level_ = (int)a->dbl_;
				break;
			case 7:
				qtask_texture_modifier->material_idx_ = (int)a->dbl_;
				break;
			case 8:
				qtask_texture_modifier->bitmap_id_ = (int)a->dbl_;
				break;
			case 9:
				qtask_texture_modifier->bitmap_size_ = (int)a->dbl_;
				break;
			}

			a = a->next_;

			arg_idx++;

			if (arg_idx > 9) {
				break;
			}
		}

#ifdef _DEBUG
		assert(qtask_texture_modifier->bitmap_size_ >= 8);
		assert(bitmap_item_size_array_[qtask_texture_modifier->bitmap_id_] == qtask_texture_modifier->bitmap_size_);
#endif

		int cube_half_size_shift = 30 - qtask_texture_modifier->lod_level_;
		int bitmap_half_size_shift = get_highest_bit(qtask_texture_modifier->bitmap_size_) - 1;

		texture_modifier_s* texture_modifier = texture_modifiers_ + qtask_texture_modifier->idx_in_array_;

		texture_modifier->material_idx_ = qtask_texture_modifier->material_idx_;
		texture_modifier->bitmap_line_width_shift_ = get_highest_bit(qtask_texture_modifier->bitmap_size_) - 3;	// - 3: div 8

		float v = (float)((double)(qtask_texture_modifier->bitmap_size_ - 1) / (double)(qtask_texture_modifier->bitmap_size_));

		texture_modifier->local_pos_to_bitmap_pos_ = (1.0f / (1 << (cube_half_size_shift - bitmap_half_size_shift))) * v;

		texture_modifier->bitmap_item_ = bitmap_items_[qtask_texture_modifier->bitmap_id_];

		num_texture_modifier_++;
	}

	// add to dynamic cube
	for (int i = 0; i < num_texture_modifier_; ++i) {
		qtask_texture_modifier_s* qtask = qtask_texture_modifiers_ + i;

		glm::ivec3 cube_ctr;
		dyn_cube_s* dyn_cube = level_dyn_cube->GetDynCube(qtask->script_defined_pos_, qtask->lod_level_, cube_ctr);
		if (dyn_cube) {
			level_dyn_cube->AddQTaskToDynCube(dyn_cube, qtask);

			texture_modifier_s * texture_modifier = texture_modifiers_ + qtask->idx_in_array_;

			int cube_half_size = ROOT_CUBE_HALF_SIZE >> qtask->lod_level_;
			texture_modifier->cube_min_x_ = cube_ctr[0] - cube_half_size;
			texture_modifier->cube_min_y_ = cube_ctr[1] - cube_half_size;
		}
	}
}

void Terrain::LoadTerrainLightMapInfo(ILevelDynCube* level_dyn_cube, const QSC* qsc_objects) {
	/*
	Task_DeclareParameters("TerrainLightMap", "Position", "ObjectPos", "Level", "Int32", "Size", "Int32", "TextureIndex", "Int32", "GeneratedWhenCreated", "bool8");

	Task_New(-1, "TerrainLightMap", "", -22020092.0, -38797308.0, 179656016.0, 10, 64, 177, TRUE),
	Task_New(-1, "TerrainLightMap", "", -24117244.0, -38797308.0, 179656016.0, 10, 64, 178, TRUE),
	Task_New(-1, "TerrainLightMap", "", -19922940.0, -38797308.0, 179656016.0, 10, 64, 179, TRUE),

	 */
	
	num_terrain_light_map_ = 0;

	const QSC::func_s* qsc_funcs[1024];
	int num_func = qsc_objects->FindFuncByStr("TerrainLightMap", qsc_funcs);

	for (int i = 0; i < num_func; ++i) {
		const QSC::func_s* func = qsc_funcs[i];

		const QSC::arg_s* a = func->args_;

		qtask_terrain_light_map_s * qtask_terrain_light_map = qtask_terrain_light_maps_ + num_terrain_light_map_;

		qtask_terrain_light_map->task_type_ = TASK_TYPE_TERRAIN_LIGHT_MAP;
		qtask_terrain_light_map->idx_in_array_ = num_terrain_light_map_;

		int arg_idx = 0;
		while (a) {

			switch (arg_idx) {
			case 3:
				qtask_terrain_light_map->script_defined_pos_[0] = a->dbl_;
				break;
			case 4:
				qtask_terrain_light_map->script_defined_pos_[1] = a->dbl_;
				break;
			case 5:
				qtask_terrain_light_map->script_defined_pos_[2] = a->dbl_;
				break;
			case 6:
				qtask_terrain_light_map->lod_level_ = (int)a->dbl_;
				break;
			case 7:
				qtask_terrain_light_map->light_map_size_ = (int)a->dbl_;
				break;
			case 8:
				qtask_terrain_light_map->tex_idx_ = (int)a->dbl_;
				break;
			}

			a = a->next_;

			arg_idx++;

			if (arg_idx > 8) {
				break;
			}
		}

#ifdef _DEBUG
		assert(light_map_item_size_array_[qtask_terrain_light_map->tex_idx_] == qtask_terrain_light_map->light_map_size_);
#endif

		if (num_terrain_light_map_ >= MAX_LMP) {
			Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "Too many lightmaps when generated");
			return;
		}

		terrain_light_map_s * terrain_light_map = terrain_light_maps_ + qtask_terrain_light_map->idx_in_array_;

		terrain_light_map->one_over_cube_size_ = 1.0f / (float)(1 << (31 - qtask_terrain_light_map->lod_level_));
		terrain_light_map->half_pixel_uv_ = 0.5f / qtask_terrain_light_map->light_map_size_;
		terrain_light_map->cube_min_x_ = 0.0f;	// update later
		terrain_light_map->cube_min_y_ = 0.0f;	// update later
		terrain_light_map->tex_idx_ = qtask_terrain_light_map->tex_idx_;

		num_terrain_light_map_++;
	}

	for (int i = 0; i < num_terrain_light_map_; ++i) {
		qtask_terrain_light_map_s * qtask = qtask_terrain_light_maps_ + i;

		glm::ivec3 cube_ctr;	// return the cube center

		dyn_cube_s* dyn_cube = level_dyn_cube->GetDynCube(qtask->script_defined_pos_, qtask->lod_level_, cube_ctr);
		if (dyn_cube) {
			level_dyn_cube->AddQTaskToDynCube(dyn_cube, qtask);

			terrain_light_map_s * terrain_light_map = terrain_light_maps_ + qtask->idx_in_array_;

			int cube_half_size = ROOT_CUBE_HALF_SIZE >> qtask->lod_level_;
			terrain_light_map->cube_min_x_ = cube_ctr[0] - cube_half_size;
			terrain_light_map->cube_min_y_ = cube_ctr[1] - cube_half_size;

		}
	}
}

void Terrain::LoadHeightMapInfo(ILevelDynCube* level_dyn_cube, const QSC* qsc_objects) {
	/*
	Task_DeclareParameters("HeightMap", "Position", "ObjectPos", "Level", "Int32", "Size", "Int32", "isEdit", "bool8", "Bitmap ID", "Int32", "NORMALSMOOTH", "Int32", "BLURSMOOTH", "Int32");

	Task_New(-1, "HeightMap", "", 8496908.0, 152896992.0, 174481120.0, 12, 128, FALSE, 0, 8, 7),
	Task_New(-1, "HeightMap", "", 8158224.0, 152881072.0, 174530496.0, 12, 128, FALSE, 1, 8, 7)),
	 */
	
	num_height_map_ = 0;

	const QSC::func_s* qsc_funcs[1024];
	int num_func = qsc_objects->FindFuncByStr("HeightMap", qsc_funcs);

	for (int i = 0; i < num_func; ++i) {
		const QSC::func_s* func = qsc_funcs[i];

		const QSC::arg_s* a = func->args_;

		if (num_height_map_ >= MAX_HMP) {
			Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "Too many texturemodfiers");
			return;
		}

		qtask_height_map_s* qtask_height_map = qtask_height_maps_ + num_height_map_;

		qtask_height_map->task_type_ = TASK_TYPE_TERRAIN_HEIGHT_MAP;
		qtask_height_map->idx_in_array_ = num_height_map_;

		int arg_idx = 0;
		while (a) {

			switch (arg_idx) {
			case 3:
				qtask_height_map->script_defined_pos_[0] = a->dbl_;
				break;
			case 4:
				qtask_height_map->script_defined_pos_[1] = a->dbl_;
				break;
			case 5:
				qtask_height_map->script_defined_pos_[2] = a->dbl_;
				break;
			case 6:
				qtask_height_map->lod_level_ = (int)a->dbl_;
				break;
			case 7:
				qtask_height_map->height_map_size_ = (int)a->dbl_;
				break;
			case 8:
				// isEdit: not used in this demo
				break;
			case 9:
				qtask_height_map->height_map_id_ = (int)a->dbl_;
				break;
			case 10:
				// NORMALSMOOTH: not used in this demo
				break;
			case 11:
				// BLURSMOOTH: not used in this demo
				break;
			}

			a = a->next_;

			arg_idx++;

			if (arg_idx > 11) {
				break;
			}
		}

#ifdef _DEBUG
		assert(qtask_height_map->height_map_size_ >= 2);
		assert(height_map_item_size_array_[qtask_height_map->height_map_id_] == qtask_height_map->height_map_size_);
#endif

		int cube_half_size_shift = 30 - qtask_height_map->lod_level_;
		int hmp_half_size_shift = get_highest_bit(qtask_height_map->height_map_size_) - 1;

		height_map_s * height_map = height_maps_ + qtask_height_map->idx_in_array_;

		height_map->height_map_line_width_shift_ = get_highest_bit(qtask_height_map->height_map_size_);
		height_map->local_pos_to_hmp_pos_ = 1.0f / (1 << (cube_half_size_shift - hmp_half_size_shift));
		height_map->height_map_item_ = (int8_t*)height_map_items_[qtask_height_map->height_map_id_];

		num_height_map_++;
	}

	for (int i = 0; i < num_height_map_; ++i) {
		qtask_height_map_s * qtask_height_map = qtask_height_maps_ + i;

		glm::ivec3 cube_ctr;

		dyn_cube_s* dyn_cube = level_dyn_cube->GetDynCube(qtask_height_map->script_defined_pos_, qtask_height_map->lod_level_, cube_ctr);
		if (dyn_cube) {
			level_dyn_cube->AddQTaskToDynCube(dyn_cube, (qtask_s*)qtask_height_map);

			height_map_s * height_map = height_maps_ + qtask_height_map->idx_in_array_;

			int cube_half_size = ROOT_CUBE_HALF_SIZE >> qtask_height_map->lod_level_;
			height_map->cube_min_x_ = cube_ctr[0] - cube_half_size;
			height_map->cube_min_y_ = cube_ctr[1] - cube_half_size;
		}
	}
}

void Terrain::LoadDiscardTerrainInfo(ILevelDynCube* level_dyn_cube, const QSC* qsc_objects) {
	num_discard_terrain_ = 0;

	const QSC::func_s* qsc_funcs[1024];
	int num_func = qsc_objects->FindFuncByStr("DiscardTerrain", qsc_funcs);

	for (int i = 0; i < num_func; ++i) {
		const QSC::func_s* func = qsc_funcs[i];

		const QSC::arg_s* a = func->args_;

		if (num_discard_terrain_ >= MAX_DISCARD_TERRAIN) {
			Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "Too many discard terrain");
			return;
		}

		qtask_discard_terrain_s* qtask_discard_terrain = qtask_discard_terrains_ + num_discard_terrain_;

		qtask_discard_terrain->task_type_ = TASK_TYPE_DISCARD_TERRAIN;

		int arg_idx = 0;
		while (a) {

			switch (arg_idx) {
			case 3:
				qtask_discard_terrain->script_defined_pos_[0] = a->dbl_;
				break;
			case 4:
				qtask_discard_terrain->script_defined_pos_[1] = a->dbl_;
				break;
			case 5:
				qtask_discard_terrain->script_defined_pos_[2] = a->dbl_;
				break;
			case 6:
				qtask_discard_terrain->lod_level_ = (int)a->dbl_;
				break;
			}

			a = a->next_;

			arg_idx++;

			if (arg_idx > 6) {
				break;
			}
		}

		num_discard_terrain_++;
	}

	for (int i = 0; i < num_discard_terrain_; ++i) {
		qtask_discard_terrain_s* qtask_discard_terrain = qtask_discard_terrains_ + i;

		glm::ivec3 cube_ctr;

		dyn_cube_s* dyn_cube = level_dyn_cube->GetDynCube(qtask_discard_terrain->script_defined_pos_, qtask_discard_terrain->lod_level_, cube_ctr);
		if (dyn_cube) {
			level_dyn_cube->AddQTaskToDynCube(dyn_cube, qtask_discard_terrain);
			dyn_cube->flags_ |= CUBE_FLAG_DISCARD_TERRAIN;
		}
	}
}

void Terrain::Update(update_params_s& params, const dyn_cube_s* root_dyn_cube) {
	tan_half_fovx_ = params.view_define_->tan_half_fovx_;
	tan_half_fovy_ = params.view_define_->tan_half_fovy_;

	pre_frame_ = cur_frame_;
	cur_frame_ = params.frame_;

	pre_mod_options_ = cur_mod_options_;
	cur_mod_options_ = params.terrain_mod_options_;
	if (cur_mod_options_ != pre_mod_options_) {
		// modification options changed, need reset cube data hash table
		ClearCubeDataHash();
	}

	vertices_ = params.terrain_vb_;
	indices_ = params.terrain_ib_;
	render_chunks_ = params.terrain_render_chunks_;
	num_vertex_ = 0;
	num_index_ = 0;
	num_render_chunk_ = 0;

	cur_int_view_pos_[0] = (int)params.view_define_->pos_.x;
	cur_int_view_pos_[1] = (int)params.view_define_->pos_.y;
	cur_int_view_pos_[2] = (int)params.view_define_->pos_.z;

	double cur_dbl_view_pos[3];

	cur_dbl_view_pos[0] = cur_int_view_pos_[0];
	cur_dbl_view_pos[1] = cur_int_view_pos_[1];
	cur_dbl_view_pos[2] = cur_int_view_pos_[2];

	double trans_out[3];
	TransformDoubleVec3(params.view_define_->mat_rot_, cur_dbl_view_pos, trans_out);

	cur_int_view_pos_rotated_[0] = (int)trans_out[0];
	cur_int_view_pos_rotated_[1] = (int)trans_out[1];
	cur_int_view_pos_rotated_[2] = (int)trans_out[2];

	// init dim_table_rotated_
	const int* t = dim_table_;
	int* t_rot = dim_table_rotated_;

	for (int z = -1; z <= 1; z += 2) {
		for (int y = -1; y <= 1; y += 2) {
			for (int x = -1; x <= 1; x += 2) {

				double temp[3] = { (double)t[0], (double)t[1], (double)t[2] };
				double temp_out[3];

				TransformDoubleVec3(params.view_define_->mat_rot_, temp, temp_out);

				t_rot[0] = (int)temp_out[0];
				t_rot[1] = (int)temp_out[1];
				t_rot[2] = (int)temp_out[2];

				t += 3;
				t_rot += 3;
			}
		}
	}

	num_render_cube_ = 0;
	num_cube_data_ = 0;

	const double UPDATE_ROOT_CUBE_HALF_SIZE = 1073741800.0;	// slightly less than 2^30
	const double UPDATE_ROOT_CUBE_RADIUS = UPDATE_ROOT_CUBE_HALF_SIZE * SQRT_3;

	float root_cube_ctr_horizontal_dist_to_clip_plane = (float)(UPDATE_ROOT_CUBE_RADIUS / std::cos(params.view_define_->fovx_ * 0.5));
	float root_cube_ctr_vertical_dist_to_clip_plane   = (float)(UPDATE_ROOT_CUBE_RADIUS / std::cos(params.view_define_->fovy_ * 0.5));

	GenerateRenderCube(root_dyn_cube, ctr_ + 1, root_cube_ctr_horizontal_dist_to_clip_plane, root_cube_ctr_vertical_dist_to_clip_plane);

	// clear old hash data
	for (int i = 0; i < CUBE_DATA_HASH_ITEM_COUNT; ++i) {
		cube_data_hash_s* cube_data_hash = cube_data_hash_ + i;
		if (cube_data_hash->frame_ != cur_frame_) {
			cube_data_hash->frame_ = INVALID_FRAME;	// mark not used
			if (cube_data_hash->cube_data_) {
				FreeCubeData(
					cube_data_hash->cube_data_,
					cube_data_hash->buf_type_);

				cube_data_hash->cube_data_ = nullptr;
			}
		}
	}

	GenerateCubeData();

	for (int i = 0; i < num_cube_data_; ++i) {
		GenerateCubeMesh(cube_data_array_[i]);
	}

	params.num_terrain_vert_ = num_vertex_;
	params.num_terrain_idx_ = num_index_;
	params.num_terrain_render_chunk_ = num_render_chunk_;
}

bool Terrain::GetFirstHMPCenter(glm::vec3& out_pos) const {
	if (num_height_map_ > 0) {
		const height_map_s* hmp = height_maps_ + qtask_height_maps_[0].idx_in_array_;
		int hmp_dim = 1 << hmp->height_map_line_width_shift_;
		double width = hmp_dim / hmp->local_pos_to_hmp_pos_;
		out_pos.x = (float)(hmp->cube_min_x_ + width * 0.5);
		out_pos.y = (float)(hmp->cube_min_y_ + width * 0.5);
		out_pos.z = 175000000.0f; // High up
		return true;
	}
	return false;
}

bool Terrain::GetZ(const dyn_cube_s* root_dyn_cube, const glm::vec3& pos, float & ret_z) {
	get_z_pos_ = pos;

	glm::ivec3 cube_pos(0);
	return RecursiveGetZ(root_dyn_cube, ctr_ + 1, cube_pos, 0, ROOT_CUBE_HALF_SIZE, 0, ret_z);
}

void Terrain::ClearCubeDataHash() {
	// clear old hash data
	for (int i = 0; i < CUBE_DATA_HASH_ITEM_COUNT; ++i) {
		cube_data_hash_s* cube_data_hash = cube_data_hash_ + i;
		cube_data_hash->frame_ = INVALID_FRAME;
		if (cube_data_hash->cube_data_) {
			FreeCubeData(
				cube_data_hash->cube_data_,
				cube_data_hash->buf_type_);

			cube_data_hash->cube_data_ = nullptr;
		}
	}
}

void Terrain::GenerateRenderCube(const dyn_cube_s* root_dyn_cube,
	const ctr_node_s* root_ctr_cube, float root_cube_ctr_horizontal_dist_to_clip_plane, float root_cube_ctr_vertical_dist_to_clip_plane)
{
	struct cube_info_s {
		bool				clipped_checked_;
		bool				clipped_;
		bool				added_to_render_cube_;
		bool				lod_checked_;
		const dyn_cube_s*	dyn_cube_;
		const ctr_node_s*	ctr_node_;
		uint32_t			lod_level_;
		glm::ivec3			cube_pos_;
		glm::ivec3			cube_pos_rotated_;
		float				cube_ctr_horizontal_dist_to_clip_plane_;	// decrease by half when down to child cube
		float				cube_ctr_vertical_dist_to_clip_plane_;		// decrease by half when down to child cube
		uint32_t			child_no_;
		uint32_t			trans_flag_;
	};

	cube_info_s stack[17];	// LOD level: 0~16
	int stack_size = 0;

	auto PushToStack = [this, &stack, &stack_size](
		const dyn_cube_s* dyn_cube,
		const ctr_node_s* ctr_node,
		uint32_t lod_level,
		const glm::ivec3& cube_pos,
		const glm::ivec3& cube_pos_rotated,
		float cube_ctr_horizontal_dist_to_clip_plane,
		float cube_ctr_vertical_dist_to_clip_plane,
		uint32_t child_no,
		uint32_t trans_flag
		)
		{
			if (stack_size > 16) {
				Log(log_type_t::LOG_FATAL, __FILE__, __LINE__, "stack overflow\n");
				return;
			}

			cube_info_s* ci = stack + stack_size;

			ci->clipped_checked_ = false;
			ci->clipped_ = false;
			ci->added_to_render_cube_ = false;
			ci->lod_checked_ = false;
			ci->dyn_cube_ = dyn_cube;
			ci->ctr_node_ = ctr_node;
			ci->lod_level_ = lod_level;
			ci->cube_pos_ = cube_pos;
			ci->cube_pos_rotated_ = cube_pos_rotated;
			ci->cube_ctr_horizontal_dist_to_clip_plane_ = cube_ctr_horizontal_dist_to_clip_plane;
			ci->cube_ctr_vertical_dist_to_clip_plane_ = cube_ctr_vertical_dist_to_clip_plane;
			ci->child_no_ = child_no;
			ci->trans_flag_ = trans_flag;

			stack_size++;

			DynCube_RunQTaskPushFunc(dyn_cube);
		};

	auto PopFromStack = [this, &stack, &stack_size]()
		{
			if (stack_size < 0) {
				Log(log_type_t::LOG_FATAL, __FILE__, __LINE__, "stack underflow\n");
				return;
			}

			DynCube_RunQTaskPopFunc(stack[stack_size - 1].dyn_cube_);

			stack_size--;
		};

	// push root node to stack
	glm::ivec3 cube_pos(0);
	glm::ivec3 cube_pos_rotated(0);

	PushToStack(root_dyn_cube, root_ctr_cube, 0, cube_pos, cube_pos_rotated,
		root_cube_ctr_horizontal_dist_to_clip_plane, 
		root_cube_ctr_vertical_dist_to_clip_plane, 0, 0);

	// start process
	while (stack_size) {	// if stack is not empty
		cube_info_s& ci = stack[stack_size - 1];	// fetch item from stack top

		if (ci.added_to_render_cube_ || ci.clipped_ || ci.child_no_ > 7 /* loop children finished */) {
			PopFromStack();
		}
		else {
			int cube_half_size = ROOT_CUBE_HALF_SIZE >> ci.lod_level_;

			// check clipping
			bool clipped = false;

			if (ci.clipped_checked_) {
				clipped = ci.clipped_;
			}
			else {
				glm::ivec3 cube_pos_rotated_sub_view_pos_rotated =
					ci.cube_pos_rotated_ - cur_int_view_pos_rotated_;

				if ((64.0f - ci.cube_ctr_vertical_dist_to_clip_plane_) >= cube_pos_rotated_sub_view_pos_rotated.z) {
					clipped = true;
				}

				/*
				
				   horizontal cull

				     the demo cube just touch the left frustum plane

				        \               /
	  left frustum plane \             / right frustum plane
				          \           /
				     ______\ forward (+Z)
				    |      |\   |   /
					| cube_|_\D1|  /
					|___|__| |\ | /
					    |    | \|/
					    |	 | view
				        |-D2-|


					D1: horizontal_half_width_at_z
					D2: cube_radius / cos(fovx * 0.5)
				  
				 
				 */

				if (!clipped) {
					double x_test = cube_pos_rotated_sub_view_pos_rotated.z * tan_half_fovx_ + ci.cube_ctr_horizontal_dist_to_clip_plane_;
					if (cube_pos_rotated_sub_view_pos_rotated.x <= -x_test || cube_pos_rotated_sub_view_pos_rotated.x >= x_test) {
						// clipped by left or right frustum plane
						clipped = true;
					}
				}

				if (!clipped) {
					double y_test = cube_pos_rotated_sub_view_pos_rotated.z * tan_half_fovy_ + ci.cube_ctr_vertical_dist_to_clip_plane_;
					if (cube_pos_rotated_sub_view_pos_rotated.y <= -y_test || cube_pos_rotated_sub_view_pos_rotated.y >= y_test) {
						// clipped by top or bottom frustum plane
						clipped = true;
					}
				}

				if (!clipped) {
					if ((ci.cube_pos_.z + cube_half_size) < -LEVEL1_CUBE_HALF_SIZE) {
						clipped = true;
					}
				}

				ci.clipped_checked_ = true;
				ci.clipped_ = clipped;
			}

			if (!clipped) {

				bool draw_cur_cube = false;

				if (!ci.lod_checked_) {

					// check detail level
					glm::ivec3 cube_pos_sub_view_pos = ci.cube_pos_ - cur_int_view_pos_;

					int max_axis_aligned_dist_to_view = cube_pos_sub_view_pos.x > 0 ? cube_pos_sub_view_pos.x : -cube_pos_sub_view_pos.x;

					int temp = cube_pos_sub_view_pos.y > 0 ? cube_pos_sub_view_pos.y : -cube_pos_sub_view_pos.y;
					if (temp > max_axis_aligned_dist_to_view) {
						max_axis_aligned_dist_to_view = temp;
					}

					temp = cube_pos_sub_view_pos.z > 0 ? cube_pos_sub_view_pos.z : -cube_pos_sub_view_pos.z;
					if (temp > max_axis_aligned_dist_to_view) {
						max_axis_aligned_dist_to_view = temp;
					}

					// max_axis_aligned_dist_to_view is positive
					// but delta might be negtiave if view_pos inside current cube
					//   if delta > 0 then delta is the distance from view to cube's face
					//   along that axis

					int delta = max_axis_aligned_dist_to_view - cube_half_size;

					// div 1/32 cube_half_size

					/*
					    ______
					   |      |
					   | cube |
					   |______|
					       |\
						   | \ 
						   |  face
						   |
						   | distance_to_face
						   |
                        viewpos


					  if distance_to_face >= 5.09375 times cube_half_size then
					  the cube add to render queue

					    // 5.09375 = 1 / 32

					  render cube as coarser as possible

					  i.e. if a cube is far enough then that cube can be renderer

					 */

					// v: delta div (cube_haf_size / 32)
					int v = delta >> (25 - ci.lod_level_);

					// if view_pos inside current test_cube,
					//    delta must be negative 
					//    and then v must be negative
					//    the v >= CUBE_ACTIVE_COMPARE_INT test will fail so we will
					//    continue to check its children.
					// delta 

					// v: be directly  proportional to delta
					//    be inversely proportional to (25 - ci.lod_level_)
					//    be directly  proportional to ci.lod_level_

					//    if delta to test_cube more small or cube_half_size more larger
					//    nore less likely to draw test_cube

					draw_cur_cube = v >= CUBE_ACTIVE_COMPARE_INT || ci.lod_level_ >= LEAF_CUBE_LOD_LEVEL;

					ci.lod_checked_ = true;
				}

				if (draw_cur_cube) {

					// reach max level or this cube is active
					TryAddRenderCube(ci.ctr_node_, ci.cube_pos_, ci.lod_level_, ci.trans_flag_, ci.dyn_cube_);
					ci.added_to_render_cube_ = true;

				}
				else {
					const uint8_t* children_access_order = CUBE_IDX_TABLE + ci.trans_flag_ * 8;
					const uint8_t* trans_lines = CUBE_TRANS_TABLE + ci.trans_flag_ * 8;

					uint8_t children_mask = ci.ctr_node_->children_mask_;
					uint8_t child_idx = children_access_order[ci.child_no_];

					if (children_mask & (1 << child_idx)) {

						int sub_cube_lod_level = ci.lod_level_ + 1;

						glm::ivec3 sub_cube_pos;
						glm::ivec3 sub_cube_pos_rotated;

						const int* dims = dim_table_ + ci.child_no_ * 3;
						const int* rot_dims = dim_table_rotated_ + ci.child_no_ * 3;

						for (int j = 0; j < 3; ++j) {
							sub_cube_pos[j] = ci.cube_pos_[j] + (dims[j] >> sub_cube_lod_level);
							sub_cube_pos_rotated[j] = ci.cube_pos_rotated_[j] + (rot_dims[j] >> sub_cube_lod_level);
						}

						const ctr_node_s* sub_ctr_node = ctr_ + ci.ctr_node_->children_[child_idx];

						const dyn_cube_s* sub_dyn_cube = nullptr;
						if (ci.dyn_cube_ && ci.dyn_cube_->children_mask_ & (1 << child_idx)) {
							sub_dyn_cube = ci.dyn_cube_->children_[child_idx];
						}

						if (!sub_dyn_cube || !(cur_mod_options_ & TERRAIN_DISCARD_MOD) || !(sub_dyn_cube->flags_ & CUBE_FLAG_DISCARD_TERRAIN)) {
							PushToStack(sub_dyn_cube, sub_ctr_node,
								sub_cube_lod_level,
								sub_cube_pos, sub_cube_pos_rotated,
								ci.cube_ctr_horizontal_dist_to_clip_plane_ * 0.5f, 
								ci.cube_ctr_vertical_dist_to_clip_plane_ * 0.5f,
								0,
								trans_lines[ci.ctr_node_->cmd_transform_[child_idx]]);
						}
						// else: discarded
					}
				}
			}

			ci.child_no_ += 1;	// process next child
		}
	}
}

void Terrain::DynCube_RunQTaskPushFunc(const dyn_cube_s* dyn_cube) {
	if (dyn_cube) {

		const qtask_s* qtask = dyn_cube->qtask_link_chain_;
		while (qtask) {

			if (qtask->task_type_ == TASK_TYPE_TERRAIN_TEXTURE_MODIFIER) {
				const qtask_texture_modifier_s* qtask_texture_modifier = (const qtask_texture_modifier_s*)qtask;

				// +1: make it > 0
				//     0 means that texture modifier does not exists
				uint8_t idx_add_one = (uint8_t)qtask_texture_modifier->idx_in_array_ + 1;

				material_indices_stack_[material_indices_stack_size_++] = idx_add_one;
				packed_material_indices_ <<= 8;
				packed_material_indices_ |= idx_add_one;

			}
			else if (qtask->task_type_ == TASK_TYPE_TERRAIN_LIGHT_MAP) {
				const qtask_terrain_light_map_s * qtask_terrain_light_map = (const qtask_terrain_light_map_s*)qtask;

				uint8_t push_idx = qtask_terrain_light_map->idx_in_array_;
				light_map_stack_[light_map_stack_size_++] = push_idx;
			}
			else if (qtask->task_type_ == TASK_TYPE_TERRAIN_HEIGHT_MAP) {
				const qtask_height_map_s * qtask_height_map = (const qtask_height_map_s*)qtask;

				uint32_t push_idx = qtask_height_map->idx_in_array_;
				height_map_stack_[height_map_stack_size_++] = push_idx + 1;
			}

			qtask = qtask->next_;
		}
	}
}

void Terrain::DynCube_RunQTaskPopFunc(const dyn_cube_s* dyn_cube) {
	if (dyn_cube) {
		const qtask_s* qtask = dyn_cube->qtask_link_chain_;
		while (qtask) {

			if (qtask->task_type_ == TASK_TYPE_TERRAIN_TEXTURE_MODIFIER) {
				if (material_indices_stack_size_) {
					material_indices_stack_size_--;

					packed_material_indices_ >>= 8;

					if (material_indices_stack_size_ >= 4) {
						uint8_t material_idx = material_indices_stack_[material_indices_stack_size_ - 4];
						packed_material_indices_ |= (material_idx << 24);
					}
				}
			}
			else if (qtask->task_type_ == TASK_TYPE_TERRAIN_LIGHT_MAP) {
				light_map_stack_size_--;
			}
			else if (qtask->task_type_ == TASK_TYPE_TERRAIN_HEIGHT_MAP) {
				height_map_stack_size_--;
			}

			qtask = qtask->next_;
		}
	}
}

void Terrain::TryAddRenderCube(const ctr_node_s* ctr_node, const glm::ivec3& cube_pos, int cube_level,
	uint32_t trans_flag, const dyn_cube_s* dyn_cube)
{
	if (cube_level >= RENDER_CUBE_MIN_LOD_LEVEL && 
		(!dyn_cube || !(cur_mod_options_ & TERRAIN_DISCARD_MOD) || !(dyn_cube->flags_ & CUBE_FLAG_DISCARD_TERRAIN)))
	{
		// generate a position based hash value
		int cube_data_search_value = (
			((cube_pos[0] ^ cube_pos[1] ^ cube_pos[2]) & 0x1FFC0)
			^
			(
				(
					(cube_pos[1] & 0x3FF800)
					^
					(
						(cube_pos[0] & 0x1FFC000)
						^
						(cube_pos[2] >> 2) & 0x1FFC000
						) >> 3
					)
				>> 5
				)
			) >> 6;

		for (uint32_t hash_frame = cube_data_hash_[cube_data_search_value].frame_;
			hash_frame != INVALID_FRAME;
			hash_frame = cube_data_hash_[cube_data_search_value].frame_)
		{
			cube_data_hash_s* cur_cube_data_hash = cube_data_hash_ + cube_data_search_value;

			if (pre_frame_ == hash_frame && cur_cube_data_hash->pos_ == cube_pos) {
				break;
			}

			cube_data_search_value = (cube_data_search_value + 1) & 0x7FF /* 2047 */;
		}

		cube_data_hash_s* cube_data_hash = cube_data_hash_ + cube_data_search_value;

		if (pre_frame_ == cube_data_hash->frame_) {

			if (!cube_data_hash->cube_data_) {
				Log(log_type_t::LOG_FATAL, __FILE__, __LINE__, "Should have allocated data from cube exiting previous frame\n");
				return;
			}

			cube_data_array_[num_cube_data_++] = cube_data_hash->cube_data_;
			cube_data_hash->frame_ = cur_frame_;
		}
		else {

			if (cube_data_hash->cube_data_) {
				Log(log_type_t::LOG_FATAL, __FILE__, __LINE__, "Should not have allocated hash data\n");
				return;
			}

			if (num_render_cube_ < MAX_RENDER_CUBE) {

				cube_data_hash->pos_ = cube_pos;
				cube_data_hash->frame_ = cur_frame_;

				render_cube_s* render_cube = render_cubes_ + num_render_cube_++;

				render_cube->pos_[0] = (double)cube_pos[0];
				render_cube->pos_[1] = (double)cube_pos[1];
				render_cube->pos_[2] = (double)cube_pos[2];
				render_cube->lod_level_ = cube_level;
				render_cube->trans_flag_ = trans_flag;
				render_cube->cmd_ = ctr_node->cmd_;

				render_cube->packed_hmp_idx_lmp_idx_ = 0;

				uint16_t hmp_idx = 0;
				if ((cur_mod_options_ & TERRAIN_HEIGHT_MOD) && height_map_stack_size_) {
					hmp_idx = height_map_stack_[height_map_stack_size_ - 1];
				}

				uint16_t lmp_idx = 0xFFFF;
				if (light_map_stack_size_) {
					lmp_idx = light_map_stack_[light_map_stack_size_ - 1];
				}

				render_cube->packed_hmp_idx_lmp_idx_ = (hmp_idx << 16) | lmp_idx;
				render_cube->packed_tex_modifier_indices_ = cur_mod_options_ & TERRAIN_TEXTURE_MOD ? packed_material_indices_ : 0;

				render_cube->cube_data_hash_ = cube_data_hash;
			}
		}
	}
}

Terrain::cube_data_s* Terrain::AllocCubeData(uint32_t need_size, uint32_t& buf_type) {
	cube_data_s* cube_data = nullptr;
	buf_type = CUBE_DATA_BUF_TYPE_1600;

	if (need_size <= 1600) {
		cube_data = (cube_data_s*)cube_data_1600_bytes_item_pool_.Alloc();
	}

	if (!cube_data || (need_size > 1600 && need_size <= 3200)) {
		cube_data = (cube_data_s*)cube_data_3200_bytes_item_pool_.Alloc();
		buf_type = CUBE_DATA_BUF_TYPE_3200;
	}

	if (!cube_data || (need_size > 3200 && need_size <= 8000)) {
		cube_data = (cube_data_s*)cube_data_8000_bytes_item_pool_.Alloc();
		buf_type = CUBE_DATA_BUF_TYPE_8000;
	}

	return cube_data;
}

void Terrain::FreeCubeData(void* ptr, uint32_t buf_type) {
	switch (buf_type) {
	case CUBE_DATA_BUF_TYPE_1600:
		cube_data_1600_bytes_item_pool_.Free(ptr);
		break;
	case CUBE_DATA_BUF_TYPE_3200:
		cube_data_3200_bytes_item_pool_.Free(ptr);
		break;
	default:
		cube_data_8000_bytes_item_pool_.Free(ptr);
		break;
	}
}

void Terrain::GenerateCubeData() {
	// tex mod

	struct tex_mod_info_s {
		int			bitmap_line_width_shift_;
		float		local_pos_to_bitmap_pos_;
		float		cube_min_x_;
		float		cube_min_y_;
		const uint8_t* bitmap_item_;
		int			material_idx_;
	} tex_mod_info_buf[4];	// at most blend 4 material textures

	// clear tex_mod_info_buf
	memset(&tex_mod_info_buf, 0, sizeof(tex_mod_info_buf));

	uint32_t pri_tex_mod_flags = 0;

	for (int i = 0; i < num_render_cube_; ++i) {
		render_cube_s* render_cube = render_cubes_ + i;

		if (render_cube->packed_tex_modifier_indices_ != pri_tex_mod_flags) {
			// need update tex_mod_info_buf
			pri_tex_mod_flags = render_cube->packed_tex_modifier_indices_;

			uint32_t p = render_cube->packed_tex_modifier_indices_;

			for (int i = 0; i < 4; ++i) {
				// check 4 bytes one by one
				uint8_t tm_idx_add_one = (uint8_t)p;

				if (tm_idx_add_one) {
					const texture_modifier_s* texture_modifier = texture_modifiers_ + (tm_idx_add_one - 1);

					tex_mod_info_buf[i].bitmap_line_width_shift_ = texture_modifier->bitmap_line_width_shift_;
					tex_mod_info_buf[i].local_pos_to_bitmap_pos_ = texture_modifier->local_pos_to_bitmap_pos_;
					tex_mod_info_buf[i].cube_min_x_ = (float)texture_modifier->cube_min_x_;
					tex_mod_info_buf[i].cube_min_y_ = (float)texture_modifier->cube_min_y_;
					tex_mod_info_buf[i].bitmap_item_ = texture_modifier->bitmap_item_;
					tex_mod_info_buf[i].material_idx_ = texture_modifier->material_idx_;
				}
				else {
					tex_mod_info_buf[i].bitmap_line_width_shift_ = 0;	// no modifier
				}

				p >>= 8;
			}
		}

		uint32_t buf_type = CUBE_DATA_BUF_TYPE_1600;
		cube_data_s* cube_data = AllocCubeData(0, buf_type);
		if (!cube_data) {
			Log(log_type_t::LOG_FATAL, __FILE__, __LINE__, "cube_data  overflow (pre-alloc)");
			return;
		}

		render_cube->cube_data_hash_->cube_data_ = cube_data;
		render_cube->cube_data_hash_->buf_type_ = buf_type;

		memcpy(cube_data->cube_ctr_pos_, render_cube->pos_, sizeof(double) * 3);

		const cmd_item_s* cmd = render_cube->cmd_;

		uint16_t parent_vert_cnt = cmd->num_parent_vertex_;
		uint16_t child_vert_cnt = cmd->num_child_vertex_;
		uint16_t total_vert_cnt = parent_vert_cnt + child_vert_cnt;

		const uint32_t* cmd_triangles = (const uint32_t*)(cmd + 1);
		const uint32_t* cmd_vertices = cmd_triangles + cmd->num_triangle_;

		cube_data->total_vertex_count_ = total_vert_cnt;
		cube_data->parent_vertex_count_ = parent_vert_cnt;
		cube_data->lod_level_ = render_cube->lod_level_;

		uint32_t trans_flag = render_cube->trans_flag_;

		const height_map_s* height_map = nullptr;
		uint16_t hmp_idx = render_cube->packed_hmp_idx_lmp_idx_ >> 16;
		if (hmp_idx) {
			height_map = height_maps_ + hmp_idx - 1;
		}

		//	lod level,	scale
		//	16			2048
		//	15			4096
		//	14			8192
		//	...
		double scale = (double)(1 << (27 - render_cube->lod_level_));

		cube_data_vert_pos_s* cube_data_vert_pos = (cube_data_vert_pos_s*)(cube_data + 1);

		cube_data_vert_pos_s* vert_pos = cube_data_vert_pos;
		for (uint16_t i = 0; i < total_vert_cnt; ++i) {
			uint32_t cmd_vert = cmd_vertices[i];

			// raw x, y, z range: 0~63
			int shifted_x = (cmd_vert >> 26) & 0x3F;
			int shifted_y = (cmd_vert >> 20) & 0x3F;
			int shifted_z = (cmd_vert >> 14) & 0x3F;

			int x = shifted_x - 24;
			int y = shifted_y - 24;
			int z = shifted_z - 24;

			if (trans_flag & 4) {	// case: 4, 5, 6, 7: mirror and rotation
				// bit2 is 1

				// center (0, 0)
				// x flipped

				x = 24 - shifted_x;
			}
			// other case: rotation

			if (trans_flag & 1) {
				// bit0 is 1

				if (trans_flag & 2) {
					// bit1 is 1
					x = -x;
				}
				else {
					// bit1 is 0
					y = 24 - shifted_y;
				}

				// swap x, y
				int temp = x;
				x = y;
				y = temp;
			}
			else {
				// bit0 is 0

				if (trans_flag & 2) {
					// bit1 is 1
					x = -x;
					y = 24 - shifted_y;
				}
				// else: bit1 is 0: do nothing
			}

			double vert_x = (double)x * scale * ONE_OVER_3 + render_cube->pos_[0];
			double vert_y = (double)y * scale * ONE_OVER_3 + render_cube->pos_[1];
			double vert_z = (double)z * scale * ONE_OVER_3 + render_cube->pos_[2];

			float delta_z = 0.0f;

			if (height_map && render_cube->lod_level_ >= MIN_CUBE_LOD_LEVEL_TO_APPLY_HMP) {
				float dz = (float)CalcHMPDeltaZ(height_map, vert_x, vert_y);
				if (i >= parent_vert_cnt) {
					// children
					vert_z += dz;
				}
				else {
					// parent
					if (render_cube->lod_level_ > MIN_CUBE_LOD_LEVEL_TO_APPLY_HMP) {
						vert_z += dz;
					}
					else {
						// MIN_CUBE_LOD_LEVEL_TO_APPLY_HMP
						delta_z = dz;
					}
				}
			}

			vert_pos->pos_[0] = (float)vert_x;
			vert_pos->pos_[1] = (float)vert_y;
			vert_pos->pos_[2] = (float)vert_z;

			if (i >= parent_vert_cnt) {
				// child vertex
				vert_pos->parent_idx_ = cmd_vert & 0x3F;
			}
			else {
				// parent vertex
				vert_pos->delta_z_ = delta_z;
			}

			vert_pos++;
		}

		struct vert_info_s {
			int			ref_vert_idx_;
			float		vert_x_sub_cube_ctr_x_;
			float		vert_y_sub_cube_ctr_y_;
			float		vert_z_sub_cube_ctr_z_;
			float		unk1_;
			uint32_t	packed_flags_;   // high 16 bits: cur vertex mat_idx, lower 16 bits: parent vertex mat_idx
			float		unk2_;
			float		u_;
			float		v_;
		} vert_info_buf[64];	// atmost 64 vertices per cube

		uint32_t tex_mods = 0;

		for (uint16_t i = 0; i < total_vert_cnt; ++i) {
			uint32_t cmd_vert = cmd_vertices[i];
			vert_info_s* vi = vert_info_buf + i;

			uint32_t mat_of_vert = 0;

			vi->vert_x_sub_cube_ctr_x_ = cube_data_vert_pos[i].pos_[0] - (float)render_cube->pos_[0];
			vi->vert_y_sub_cube_ctr_y_ = cube_data_vert_pos[i].pos_[1] - (float)render_cube->pos_[1];
			vi->vert_z_sub_cube_ctr_z_ = cube_data_vert_pos[i].pos_[2] - (float)render_cube->pos_[2];

			for (int j = 0; j < 4; ++j) {

				if (tex_mod_info_buf[j].bitmap_line_width_shift_) {

					float local_pos_to_bitmap_pos = tex_mod_info_buf[j].local_pos_to_bitmap_pos_;

					uint32_t bitmap_x = (uint32_t)((cube_data_vert_pos[i].pos_[0] - tex_mod_info_buf[j].cube_min_x_) * local_pos_to_bitmap_pos);
					uint32_t bitmap_y = (uint32_t)((cube_data_vert_pos[i].pos_[1] - tex_mod_info_buf[j].cube_min_y_) * local_pos_to_bitmap_pos);

					uint8_t bits = *(tex_mod_info_buf[j].bitmap_item_ + (bitmap_y << tex_mod_info_buf[j].bitmap_line_width_shift_) + (bitmap_x >> 3));

					if ((bits >> (bitmap_x & 7)) & 1) {
						// that bit is 1
						mat_of_vert = tex_mod_info_buf[j].material_idx_;
						break;
					}
				}
				else {
					break;
				}
			}

			tex_mods |= (1 << mat_of_vert);

			if (i < parent_vert_cnt) {
				vi->ref_vert_idx_ = i;

				vi->packed_flags_ = (mat_of_vert << 16) | 255;
			}
			else {
				vi->ref_vert_idx_ = cmd_vert & 0x3F;

				vert_info_s* ref_vi = vert_info_buf + vi->ref_vert_idx_;

				uint16_t mat_of_ref_vert = (ref_vi->packed_flags_ >> 16);

				if (mat_of_ref_vert == mat_of_vert) {
					vi->packed_flags_ = (mat_of_vert << 16) | 255;
				}
				else {
					vi->packed_flags_ = (mat_of_vert << 16) | mat_of_ref_vert;
				}

			}
		}

		int tex_mod_indices[32];
		int tex_mod_indices_cnt = 0;

		tex_mod_indices[0] = 0;

		int check_bit = 0;
		while (tex_mods) {

			if (tex_mods & 1) {
				tex_mod_indices[tex_mod_indices_cnt++] = check_bit;
			}

			check_bit++;
			tex_mods >>= 1;
		}

		uint32_t size_before_tri_and_tex = (uint32_t)(sizeof(cube_data_s)
			+ sizeof(cube_data_vert_pos_s) * total_vert_cnt);

		uint32_t cube_data_one_tex_parent_vert_uv_size = (uint32_t)sizeof(cube_data_parent_vert_uv_s) * parent_vert_cnt;
		uint32_t cube_data_one_tex_child_vert_uv_size = (uint32_t)sizeof(cube_data_child_vert_uv_s) * child_vert_cnt;
		uint32_t cube_data_one_tex_vert_uv_size =
			cube_data_one_tex_parent_vert_uv_size
			+ cube_data_one_tex_child_vert_uv_size
			+ sizeof(uint32_t) /* tex */;

		uint32_t total_cube_data_size = (uint32_t)(size_before_tri_and_tex
			+ sizeof(cube_data_tri_and_tex_s)
			+ 2 * tex_mod_indices_cnt * cube_data_one_tex_vert_uv_size	// tile
			+ cube_data_one_tex_vert_uv_size);	// lmp

		if (total_cube_data_size > 8000) {
			Log(log_type_t::LOG_FATAL, __FILE__, __LINE__, "total_cube_data_size %u > 8000", total_cube_data_size);
			FreeCubeData(cube_data, buf_type);
			return;
		}

		if ((total_cube_data_size > 3200 && buf_type < CUBE_DATA_BUF_TYPE_8000)
			|| (total_cube_data_size > 1600 && buf_type < CUBE_DATA_BUF_TYPE_3200)) {

			cube_data_s* old_cube_data = cube_data;
			uint32_t old_buf_type = buf_type;

			cube_data = AllocCubeData(total_cube_data_size, buf_type);
			if (cube_data) {
				memcpy(cube_data, old_cube_data, size_before_tri_and_tex);

				render_cube->cube_data_hash_->cube_data_ = cube_data;
				render_cube->cube_data_hash_->buf_type_ = buf_type;
			}

			FreeCubeData(old_cube_data, old_buf_type);

			if (!cube_data) {
				Log(log_type_t::LOG_FATAL, __FILE__, __LINE__, "cube_data overflow, need size: %u", total_cube_data_size);
				return;
			}
		}

		cube_data_tri_and_tex_s* cube_data_tri_and_tex = (cube_data_tri_and_tex_s*)((char*)cube_data + size_before_tri_and_tex);

		cube_data_tri_and_tex->trans_flag_ = trans_flag;
		cube_data_tri_and_tex->triangle_indices_ = (uint32_t*)(cmd + 1);
		cube_data_tri_and_tex->tile_count_ = tex_mod_indices_cnt;
		cube_data_tri_and_tex->triangle_count_ = cmd->num_triangle_;

		int cube_half_size = ROOT_CUBE_HALF_SIZE >> render_cube->lod_level_;

		char* tile_uv_start = (char*)(cube_data_tri_and_tex + 1);

		for (int i = 0; i < tex_mod_indices_cnt; ++i) {

			uint16_t cur_mat_idx = (uint16_t)tex_mod_indices[i];

			float* curr_uv_ptr = (float*)(tile_uv_start + i * 2 * cube_data_one_tex_vert_uv_size);
			float* next_uv_ptr = (float*)(tile_uv_start + i * 2 * cube_data_one_tex_vert_uv_size + cube_data_one_tex_vert_uv_size);

			const material_array_s* tma = materials_.material_array_map_ + cur_mat_idx;
			const material_s* cur_tm = tma->material_array_ + render_cube->lod_level_;

			tile_map_s* tile_map_tex1 = tile_maps_ + cur_tm->tex1_tile_map_group_idx_;
			tile_map_s* tile_map_tex2 = tile_maps_ + cur_tm->tex2_tile_map_group_idx_;

			int shift_for_tex1 = 22 - render_cube->lod_level_;

			uint32_t y_off = (((int)(int64_t)(cur_tm->f_tex1_ * render_cube->pos_[1]) >> shift_for_tex1) & 0x7FF) >> 8;
			uint32_t x_off = (((int)(int64_t)(cur_tm->f_tex1_ * render_cube->pos_[0]) >> shift_for_tex1) & 0x7FF) >> 8;

			uint32_t tex1 = tile_map_tex1->tex_idx_in_terrain_tex_[8 * y_off + x_off];

			*(uint32_t*)curr_uv_ptr++ = tex1;

			int shift_for_tex2 = 23 - render_cube->lod_level_;

			y_off = (((int)(int64_t)(cur_tm->f_tex2_ * render_cube->pos_[1]) >> shift_for_tex2) & 0x7FF) >> 8;
			x_off = (((int)(int64_t)(cur_tm->f_tex2_ * render_cube->pos_[0]) >> shift_for_tex2) & 0x7FF) >> 8;

			uint32_t tex2 = tile_map_tex2->tex_idx_in_terrain_tex_[8 * y_off + x_off];

			*(uint32_t*)next_uv_ptr++ = tex2;

			// tex1
			float uv1_x = ((uint8_t)((int)(cur_tm->f_tex1_ * render_cube->pos_[0]) >> shift_for_tex1) + 1.0f) * ONE_OVER_256;
			float uv1_y = ((uint8_t)((int)(cur_tm->f_tex1_ * render_cube->pos_[1]) >> shift_for_tex1) + 1.0f) * ONE_OVER_256;
			float uv1_z = ((uint8_t)((int)(cur_tm->f_tex1_ * render_cube->pos_[2]) >> shift_for_tex1) + 1.0f) * ONE_OVER_256;
			float uv1_f = cur_tm->f_tex1_ / cube_half_size;

			// tex2
			float uv2_x = ((uint8_t)((int)(cur_tm->f_tex2_ * render_cube->pos_[0]) >> shift_for_tex2) + 1.0f) * ONE_OVER_256;
			float uv2_y = ((uint8_t)((int)(cur_tm->f_tex2_ * render_cube->pos_[1]) >> shift_for_tex2) + 1.0f) * ONE_OVER_256;
			float uv2_z = ((uint8_t)((int)(cur_tm->f_tex2_ * render_cube->pos_[2]) >> shift_for_tex2) + 1.0f) * ONE_OVER_256;
			float uv2_f = cur_tm->f_tex2_ * 0.5f / cube_half_size;

			for (uint16_t j = 0; j < total_vert_cnt; ++j) {
				uint32_t cmd_vert = cmd_vertices[j];

				uint16_t parent_vert_idx  =  cmd_vert & 0x3F;
				uint32_t cmd_vert_bit_8_9 = (cmd_vert >> 8) & 3;
				uint32_t cmd_vert_bit_6_7 = (cmd_vert >> 6) & 3;

				cube_data_vert_pos_s* cur_vert = cube_data_vert_pos + j;
				cube_data_vert_pos_s* ref_vert = nullptr;

				if (j < parent_vert_cnt) {
					ref_vert = cur_vert;
				}
				else {
					ref_vert = cube_data_vert_pos + parent_vert_idx;
				}

				float cur_vert_x_sub_pos_x = (float)(cur_vert->pos_[0] - render_cube->pos_[0]);
				float cur_vert_y_sub_pos_y = (float)(cur_vert->pos_[1] - render_cube->pos_[1]);
				float cur_vert_z_sub_pos_z = (float)(cur_vert->pos_[2] - render_cube->pos_[2]);

				float ref_vert_x_sub_pos_x = (float)(ref_vert->pos_[0] - render_cube->pos_[0]);
				float ref_vert_y_sub_pos_y = (float)(ref_vert->pos_[1] - render_cube->pos_[1]);
				float ref_vert_z_sub_pos_z = (float)(ref_vert->pos_[2] - render_cube->pos_[2]);

				// tex1
				float u = 0.0f, v = 0.0f, du = 0.0f, dv = 0.0f;

				if (cmd_vert_bit_8_9) {

					if (((trans_flag ^ (uint8_t)cmd_vert_bit_8_9) & 1) != 0) {
						u = cur_vert_y_sub_pos_y * uv1_f + uv1_y;

						float ref_u = ref_vert_y_sub_pos_y * uv1_f + uv1_y;
						du = ref_u - u;
					}
					else {
						u = cur_vert_x_sub_pos_x * uv1_f + uv1_x;

						float ref_u = ref_vert_x_sub_pos_x * uv1_f + uv1_x;
						du = ref_u - u;
					}

					v = cur_vert_z_sub_pos_z * uv1_f + uv1_z;

					dv = 0.0f;

				}
				else {
					u = cur_vert_x_sub_pos_x * uv1_f + uv1_x;
					v = cur_vert_y_sub_pos_y * uv1_f + uv1_y;

					float ref_u = ref_vert_x_sub_pos_x * uv1_f + uv1_x;
					float ref_v = ref_vert_y_sub_pos_y * uv1_f + uv1_y;

					du = ref_u - u;
					dv = ref_v - v;
				}

				curr_uv_ptr[1] = u;
				curr_uv_ptr[2] = v;

				auto calc_uv_flags = [](uint16_t parent_vert_idx, uint16_t cur_mat_idx, uint32_t vert_packed_flags) -> uint32_t {

					uint32_t diffuse_byte_offset = 0;

					uint16_t ref_vert_mat_idx = (uint16_t)vert_packed_flags;
					uint16_t cur_vert_mat_idx = (uint16_t)(vert_packed_flags >> 16);

					if (cur_mat_idx == ref_vert_mat_idx) {
						diffuse_byte_offset = 8;
					}
					else if (cur_mat_idx == cur_vert_mat_idx) {
						diffuse_byte_offset = ref_vert_mat_idx != 255 ? 16 : 24 /* ref_vert_mat_idx == 255: cur == ref mat idx */;
					}

					// 32: 8 float diffuse values: sizeof(float) * 8 = 32 bytes
					//     each parent's morphing factors are different
					// diffuse_byte_offset as row offset
					return (parent_vert_idx << 16) | (diffuse_byte_offset + 32 * parent_vert_idx);
				};

				uint32_t tex1_packed_flags = 0;

				if (j >= parent_vert_cnt) {
					// child vertex

					tex1_packed_flags = calc_uv_flags(parent_vert_idx, cur_mat_idx, vert_info_buf[j].packed_flags_);
					*(uint32_t*)curr_uv_ptr = tex1_packed_flags;

					curr_uv_ptr[3] = du;
					curr_uv_ptr[4] = dv;

					// +5: cube_data_child_vert_uv_s
					curr_uv_ptr += 5;
				}
				else {
					// parent vertex

					tex1_packed_flags = calc_uv_flags(j, cur_mat_idx, vert_info_buf[j].packed_flags_);
					*(uint32_t*)curr_uv_ptr = tex1_packed_flags;

					// +3: cube_data_parent_vert_uv_s
					curr_uv_ptr += 3;
				}

				// tex2
				if (cmd_vert_bit_6_7) {

					if (((trans_flag ^ (uint8_t)cmd_vert_bit_6_7) & 1) != 0) {
						u = cur_vert_y_sub_pos_y * uv2_f + uv2_y;

						float ref_u = ref_vert_y_sub_pos_y * uv2_f + uv2_y;

						du = ref_u - u;
					}
					else {
						u = cur_vert_x_sub_pos_x * uv2_f + uv2_x;

						float ref_u = ref_vert_x_sub_pos_x * uv2_f + uv2_x;

						du = ref_u - u;
					}

					v = cur_vert_z_sub_pos_z * uv2_f + uv2_z;
					float ref_v = ref_vert_z_sub_pos_z * uv2_f + uv2_z;

					dv = ref_v - v;
				}
				else {
					u = cur_vert_x_sub_pos_x * uv2_f + uv2_x;
					v = cur_vert_y_sub_pos_y * uv2_f + uv2_y;

					float ref_u = ref_vert_x_sub_pos_x * uv2_f + uv2_x;
					float ref_v = ref_vert_y_sub_pos_y * uv2_f + uv2_y;

					du = ref_u - u;
					dv = ref_v - v;
				}

				*(uint32_t*)next_uv_ptr = tex1_packed_flags + 4; /* + 4: byte offset plus 4, point to next diffuse color for texture blending */
				next_uv_ptr[1] = u;
				next_uv_ptr[2] = v;

				if (j >= parent_vert_cnt) {
					// child
					next_uv_ptr[3] = du;
					next_uv_ptr[4] = dv;

					// +5: cube_data_child_vert_uv_s
					next_uv_ptr += 5;
				}
				else {
					// parent

					// +3: cube_data_parent_vert_uv_s
					next_uv_ptr += 3;
				}
			}

		}

		// lmp
		float* lmp_uv_ptr = (float*)(tile_uv_start + 2 * tex_mod_indices_cnt * cube_data_one_tex_vert_uv_size);

		if ((uint16_t)render_cube->packed_hmp_idx_lmp_idx_ != 0xFFFF) {

			const terrain_light_map_s * terrain_light_map = terrain_light_maps_ + (uint16_t)render_cube->packed_hmp_idx_lmp_idx_;
			*(uint32_t*)lmp_uv_ptr++ = terrain_light_map->tex_idx_ + 1;

			float one_over_cube_size = terrain_light_map->one_over_cube_size_;
			float half_pixel_uv = terrain_light_map->half_pixel_uv_;

			for (uint16_t j = 0; j < total_vert_cnt; ++j) {
				uint32_t cmd_vert = cmd_vertices[j];

				uint16_t parent_vert_idx = cmd_vert & 0x3F;

				cube_data_vert_pos_s* cur_vert = cube_data_vert_pos + j;
				cube_data_vert_pos_s* ref_vert = nullptr;

				if (j < parent_vert_cnt) {
					ref_vert = cube_data_vert_pos + j;
				}
				else {
					ref_vert = cube_data_vert_pos + parent_vert_idx;
				}

				float u = (cur_vert->pos_[0] - (float)terrain_light_map->cube_min_x_) * one_over_cube_size + half_pixel_uv;
				float v = (cur_vert->pos_[1] - (float)terrain_light_map->cube_min_y_) * one_over_cube_size + half_pixel_uv;

				float ref_u = (ref_vert->pos_[0] - (float)terrain_light_map->cube_min_x_) * one_over_cube_size + half_pixel_uv;
				float ref_v = (ref_vert->pos_[1] - (float)terrain_light_map->cube_min_y_) * one_over_cube_size + half_pixel_uv;

				lmp_uv_ptr[1] = u;
				lmp_uv_ptr[2] = v;

				if (j >= parent_vert_cnt) {
					// child

					uint16_t parent_vert_idx = cmd_vert & 0x3F;

					*(uint32_t*)lmp_uv_ptr = (parent_vert_idx << 16) | (parent_vert_idx * 32);

					// delta uv, uv interpolation of child vertex
					lmp_uv_ptr[3] = ref_u - u;
					lmp_uv_ptr[4] = ref_v - v;

					// +5: cube_data_child_vert_uv_s
					lmp_uv_ptr += 5;
				}
				else {
					// parent

					*(uint32_t*)lmp_uv_ptr = (j << 16) | (j * 32);

					// +3: cube_data_parent_vert_uv_s
					lmp_uv_ptr += 3;
				}
			}

		}
		else {
			*(uint32_t*)lmp_uv_ptr = 0;
		}

		cube_data_array_[num_cube_data_++] = cube_data;
	}
}

void Terrain::GenerateCubeMesh(const cube_data_s* cube_data) {
	float inter_factors[64];	// each parent vertices

	float diffuse_table[64 * 8] = {};

	uint16_t parent_vert_count = cube_data->parent_vertex_count_;
	uint16_t total_vert_count = cube_data->total_vertex_count_;

	const cube_data_vert_pos_s* vert_pos = (const cube_data_vert_pos_s*)(cube_data + 1);
	for (uint16_t i = 0; i < parent_vert_count; ++i) {
		float delta_x = vert_pos[i].pos_[0] - cur_int_view_pos_[0];
		float delta_y = vert_pos[i].pos_[1] - cur_int_view_pos_[1];
		float delta_z = vert_pos[i].pos_[2] - cur_int_view_pos_[2];

		// max abslute axis distance
		float max_delta = delta_x;
		if (max_delta < 0.0f) {
			max_delta = -delta_x;
		}

		if (delta_y < 0.0f) {
			delta_y = -delta_y;
		}

		if (delta_y > max_delta) {
			max_delta = delta_y;
		}

		if (delta_z < 0.0f) {
			delta_z = -delta_z;
		}

		if (delta_z > max_delta) {
			max_delta = delta_z;
		}

		/* lod_level:	1 << (25 - lod_level)
		   7			262144
		   8			131072
		   9			65536
		   10			32768
		   11			16384
		   12			8192
		   13			4096
		   14			2048
		   15			1024
		   16			512
		 */

		float factor = max_delta / ((1 << (25 - cube_data->lod_level_)) * CUBE_ACTIVE_COMPARE_SUB_CUBE_START_MERGE_DELTA)
			- CUBE_MORPHING_FACTOR_COMPARE;

		if (factor >= 0.0f) {
			if (factor > 1.0f) {
				factor = 1.0f;
			}
		}
		else {
			factor = 0.0f;
		}

		inter_factors[i] = factor;

		// calc diffuse
		float one_minus_inter_factor = 1.0f - factor;

		float diffuse = one_minus_inter_factor;
		float diffuse2 = one_minus_inter_factor * one_minus_inter_factor;

		float reverse_diffuse = factor;

		float* cur_parent_vert_diffuse_row = diffuse_table + 8 * i;

		cur_parent_vert_diffuse_row[0] = 0.0f;
		cur_parent_vert_diffuse_row[1] = 0.0f;

		cur_parent_vert_diffuse_row[2] = diffuse - diffuse2;
		cur_parent_vert_diffuse_row[3] = diffuse2 + reverse_diffuse - diffuse; // diffuse2 + reverse_diffuse - diffuse;

		cur_parent_vert_diffuse_row[4] = diffuse2;
		cur_parent_vert_diffuse_row[5] = diffuse - diffuse2;

		cur_parent_vert_diffuse_row[6] = one_minus_inter_factor;
		cur_parent_vert_diffuse_row[7] = factor;
	}

	glm::vec3 temp_vert3[64];

	for (int i = 0; i < cube_data->parent_vertex_count_; ++i) {
		// parent vertices

		float factor = inter_factors[i];
		float one_minus_factor = 1.0f - factor;

		temp_vert3[i].x = vert_pos[i].pos_[0];
		temp_vert3[i].y = vert_pos[i].pos_[1];

		// factor is 0: +delta_z_
		// factor is 1: +0.0
		temp_vert3[i].z = vert_pos[i].pos_[2] + vert_pos[i].delta_z_ * one_minus_factor;
	}

	for (int i = cube_data->parent_vertex_count_; i < cube_data->total_vertex_count_; ++i) {
		// child vertices
		
		const glm::vec3* ref_vert_pos = temp_vert3 + vert_pos[i].parent_idx_;

		
		float factor = inter_factors[vert_pos[i].parent_idx_];
		float one_minus_factor = 1.0f - factor;

		// factor is 0: completely splitted
		// factor is 1: completely merged
		temp_vert3[i].x = ref_vert_pos->x * factor + vert_pos[i].pos_[0] * one_minus_factor;
		temp_vert3[i].y = ref_vert_pos->y * factor + vert_pos[i].pos_[1] * one_minus_factor;
		temp_vert3[i].z = ref_vert_pos->z * factor + vert_pos[i].pos_[2] * one_minus_factor;
	}

	const cube_data_tri_and_tex_s* tri_and_tex = (const cube_data_tri_and_tex_s*)(vert_pos + cube_data->total_vertex_count_);

	uint32_t indices_count = tri_and_tex->triangle_count_ * 3;
	int tex_group_cnt = tri_and_tex->tile_count_;

	const float* src_uv = (const float*)(tri_and_tex + 1);

	uint32_t temp_indices[256];

	if (indices_count > 256) {
		Log(log_type_t::LOG_FATAL, __FILE__, __LINE__, "too many triangles per cube");
		return;
	}

	const uint32_t* triangles = tri_and_tex->triangle_indices_;

	const uint32_t* src_tri_ptr = triangles;
	uint32_t* dst_idx_of_temp_indices = temp_indices;

	for (uint16_t i = 0; i < tri_and_tex->triangle_count_; ++i) {
		uint32_t src_tri = *src_tri_ptr;

		if (tri_and_tex->trans_flag_ & 4) {
			// mirrored
			dst_idx_of_temp_indices[0] = (uint8_t)((src_tri & 0xff00) >> 8);
			dst_idx_of_temp_indices[1] = (uint8_t)(src_tri & 0xff);
			dst_idx_of_temp_indices[2] = (uint8_t)((src_tri & 0xff0000) >> 16);
		}
		else {
			dst_idx_of_temp_indices[0] = (uint8_t)((src_tri & 0xff0000) >> 16);
			dst_idx_of_temp_indices[1] = (uint8_t)(src_tri & 0xff);
			dst_idx_of_temp_indices[2] = (uint8_t)((src_tri & 0xff00) >> 8);
		}

		src_tri_ptr++;
		dst_idx_of_temp_indices += 3;
	}

	// tile
	for (int i = 0; i < tex_group_cnt * 2; ++i) {
		uint32_t tex = *(uint32_t*)src_uv++;

		render_chunk_s* rc = nullptr;

		if (i == 0) {
			rc = GetRenderChunkByTex(0, tex);
		}
		else {
			rc = GetRenderChunkByTex(1, tex);
		}

		if (!rc) {
			Log(log_type_t::LOG_FATAL, __FILE__, __LINE__, "render chunk overflow");
			break;
		}

		if (rc->num_render_triangle_ >= MAX_TRIANGLE_LIST_PER_CHUNK) {
			Log(log_type_t::LOG_FATAL, __FILE__, __LINE__, "rc->num_render_triangle_ > %d\n", MAX_TRIANGLE_LIST_PER_CHUNK);
			break;
		}

		// put long loop as inner loop
		int free_vert_count = MAX_TERRAIN_VERTICES - num_vertex_;
		if (free_vert_count < total_vert_count) {
			Log(log_type_t::LOG_FATAL, __FILE__, __LINE__, "vertex overflow");
			break;
		}

		int free_idx_count = MAX_TERRAIN_INDICES - num_index_;
		if (free_idx_count < (int)indices_count) {
			Log(log_type_t::LOG_FATAL, __FILE__, __LINE__, "index overflow");
			break;
		}

		vert_pos_a_uv_s* dst_vert = vertices_ + num_vertex_;

		for (int j = 0; j < parent_vert_count; ++j) {

			dst_vert->pos_ = temp_vert3[j];

			dst_vert->uv_[0] = src_uv[1];
			dst_vert->uv_[1] = src_uv[2];

			uint32_t flags = *(uint32_t*)&src_uv[0];
			uint16_t diffuse_byte_offset = (uint16_t)flags;

			dst_vert->tex_alpha_ = diffuse_table[diffuse_byte_offset >> 2];

			src_uv += 3;	// cube_data_parent_vert_uv_s
			dst_vert++;
		}

		for (int j = parent_vert_count; j < total_vert_count; ++j) {
			float factor = inter_factors[vert_pos[j].parent_idx_];

			dst_vert->pos_ = temp_vert3[j];

			dst_vert->uv_[0] = src_uv[1] + src_uv[3] * factor;
			dst_vert->uv_[1] = src_uv[2] + src_uv[4] * factor;

			uint32_t flags = *(uint32_t*)&src_uv[0];
			uint16_t diffuse_byte_offset = (uint16_t)flags;

			dst_vert->tex_alpha_ = diffuse_table[diffuse_byte_offset >> 2];

			src_uv += 5;	// cube_data_child_vert_uv_s
			dst_vert++;
		}

		uint32_t* dst_idx = indices_ + num_index_;

		for (uint32_t j = 0; j < indices_count; ++j) {
			dst_idx[j] = temp_indices[j] + num_vertex_ /* as offset */;
		}

		render_triangles_s* rt = rc->render_triangles_ + rc->num_render_triangle_++;
		rt->first_vert_idx_ = num_index_;
		rt->num_triangle_ = tri_and_tex->triangle_count_;

		num_vertex_ += total_vert_count;
		num_index_ += indices_count;
	}

	// lmp
	uint32_t light_map = *(uint32_t*)src_uv++;
	if (light_map) {
		// has lmp
		uint32_t tex = light_map - 1;

		render_chunk_s* rc = GetRenderChunkByTex(2, tex);
		if (!rc) {
			Log(log_type_t::LOG_FATAL, __FILE__, __LINE__, "render chunk overflow");
			return;
		}

		if (rc->num_render_triangle_ >= MAX_TRIANGLE_LIST_PER_CHUNK) {
			Log(log_type_t::LOG_FATAL, __FILE__, __LINE__, "rc->num_render_triangle_ > %d\n", MAX_TRIANGLE_LIST_PER_CHUNK);
			return;
		}

		int free_vert_count = MAX_TERRAIN_VERTICES - num_vertex_;
		if (free_vert_count < total_vert_count) {
			Log(log_type_t::LOG_FATAL, __FILE__, __LINE__, "vertex overflow\n");
			return;
		}

		int free_idx_count = MAX_TERRAIN_INDICES - num_index_;
		if (free_idx_count < (int)indices_count) {
			Log(log_type_t::LOG_FATAL, __FILE__, __LINE__, "index overflow");
			return;
		}

		vert_pos_a_uv_s* dst_vert = vertices_ + num_vertex_;

		for (int j = 0; j < parent_vert_count; ++j) {

			dst_vert->pos_ = temp_vert3[j];

			dst_vert->uv_[0] = src_uv[1];
			dst_vert->uv_[1] = src_uv[2];

			dst_vert->tex_alpha_ = 0.0f;

			src_uv += 3;
			dst_vert++;
		}

		for (int j = parent_vert_count; j < total_vert_count; ++j) {
			float factor = inter_factors[vert_pos[j].parent_idx_];

			dst_vert->pos_ = temp_vert3[j];

			dst_vert->uv_[0] = src_uv[1] + src_uv[3] * factor;
			dst_vert->uv_[1] = src_uv[2] + src_uv[4] * factor;

			dst_vert->tex_alpha_ = 0.0f;

			src_uv += 5;
			dst_vert++;
		}

		uint32_t* dst_idx = indices_ + num_index_;

		for (uint32_t j = 0; j < indices_count; ++j) {
			dst_idx[j] = temp_indices[j] + num_vertex_;
		}

		render_triangles_s* rt = rc->render_triangles_ + rc->num_render_triangle_++;
		rt->first_vert_idx_ = num_index_;
		rt->num_triangle_ = tri_and_tex->triangle_count_;

		num_vertex_ += total_vert_count;
		num_index_ += indices_count;
	}
}

render_chunk_s* Terrain::GetRenderChunkByTex(uint32_t render_layer, uint32_t tex) {
	for (int i = 0; i < num_render_chunk_; ++i) {
		render_chunk_s* rc = render_chunks_ + i;
		if (rc->render_layer_ == render_layer && rc->tex_idx_ == tex) {
			return rc;
		}
	}

	if (num_render_chunk_ >= MAX_TERRAIN_RENDER_CHUNK) {
		Log(log_type_t::LOG_FATAL, __FILE__, __LINE__, "num_render_chunk_ > %d", MAX_TERRAIN_RENDER_CHUNK);
		return nullptr;	// overflow
	}

	render_chunk_s* new_rc = render_chunks_ + num_render_chunk_++;

	new_rc->render_layer_ = render_layer;
	new_rc->tex_idx_ = tex;
	new_rc->num_render_triangle_ = 0;

	return new_rc;
}

// missions/location0/levels has terrain.hmp file: 1, 3, 6, 8, 9, 10
double Terrain::CalcHMPDeltaZ(const height_map_s* height_map, double vert_x, double vert_y) {
	const uint8_t * height_map_item = (const uint8_t*)height_map->height_map_item_;

	double xf = (vert_x - height_map->cube_min_x_) * height_map->local_pos_to_hmp_pos_;
	double yf = (vert_y - height_map->cube_min_y_) * height_map->local_pos_to_hmp_pos_;

	int int_xf = (int)xf;
	int int_yf = (int)yf;

	double decimal_xf = xf - (double)int_xf;
	double decimal_yf = yf - (double)int_yf;

	int shifts = height_map->height_map_line_width_shift_;

	// height map size is  2^x + 1

	double h0, h1, h2, h3;

	int hmp_dim = 1 << shifts;
	int hmp_row = hmp_dim + 1;

	int iy0 = int_yf * hmp_row;
	int iy1 = (int_yf + 1) * hmp_row;
	if (int_yf >= hmp_dim) {
		iy1 = iy0;	// clamp
	}

	int ix0 = int_xf;
	int ix1 = int_xf + 1;
	if (int_xf >= hmp_dim) {
		ix1 = ix0;	// clamp
	}

	// get samples
	h0 = (double)*(height_map_item + iy0 + ix0);
	h1 = (double)*(height_map_item + iy0 + ix1);
	h2 = (double)*(height_map_item + iy1 + ix0);
	h3 = (double)*(height_map_item + iy1 + ix1);

	// Bilinear interpolcation

	// first interplate along x direction
	double x_dir_interpolated1 = (h1 - h0) * decimal_xf + h0;
	double x_dir_interpolated2 = (h3 - h2) * decimal_xf + h2;

	// then interplate along y direction
	double y_dir_interpolated = (x_dir_interpolated2 - x_dir_interpolated1) * decimal_yf + x_dir_interpolated1;

	// y_dir_interpolated range: [-128.0, 127]
	// (y_dir_interpolated - 64.0) range: [-192, 63]
	// delta_z range: [-49152, 16128]

	double delta_z = (y_dir_interpolated - 64.0) * 256.0;

	return delta_z;
}

bool Terrain::RecursiveGetZ(const dyn_cube_s* dyn_cube, const ctr_node_s* cube_node,
	glm::ivec3 & cube_pos, int cube_level, int cube_dim, uint8_t trans_flag, float & ret_z)
{
	if (dyn_cube) {
		DynCube_RunQTaskPushFunc(dyn_cube);
	}

	bool found = false;

	if (cube_level == LEAF_CUBE_LOD_LEVEL) {

		const height_map_s * hmpc = nullptr;
		if (height_map_stack_size_ && (cur_mod_options_ & TERRAIN_HEIGHT_MOD)) {
			uint32_t hmp_idx = height_map_stack_[height_map_stack_size_ - 1];
			hmpc = height_maps_ + hmp_idx - 1;
		}

		glm::dvec3 v_buf[64];
		uint32_t v_cnt = 0;

		const cmd_item_s* cmd = cube_node->cmd_;

		double scale = (double)(1 << (27 - cube_level));

		const uint32_t* triangles = (const uint32_t*)(cmd + 1);
		const uint32_t* vertices = triangles + cmd->num_triangle_;

		uint16_t grp1_vert_cnt = cmd->num_parent_vertex_;
		uint16_t grp2_vert_cnt = cmd->num_child_vertex_;

		uint16_t vert_cnt = grp1_vert_cnt + grp2_vert_cnt;

		for (uint16_t j = 0; j < vert_cnt; ++j) {
			uint32_t v = vertices[j];

			int shifted_x = (v >> 26) & 0x3F;
			int shifted_y = (v >> 20) & 0x3F;
			int shifted_z = (v >> 14) & 0x3F;

			int x = shifted_x - 24;
			int y = shifted_y - 24;
			int z = shifted_z - 24;

			if (trans_flag & 4) {	// case: 4, 5, 6, 7: mirror and rotation
				// bit2 is 1

				// center (0, 0)
				// x flipped

				x = 24 - shifted_x;
			}
			// other case: rotation

			if (trans_flag & 1) {
				// bit0 is 1

				if (trans_flag & 2) {
					// bit1 is 1
					x = -x;
				}
				else {
					// bit1 is 0
					y = 24 - shifted_y;
				}

				// swap x, y
				int temp = x;
				x = y;
				y = temp;
			}
			else {
				// bit0 is 0

				if (trans_flag & 2) {
					// bit1 is 1
					x = -x;
					y = 24 - shifted_y;
				}
				// else: bit1 is 0: do nothing
			}

			glm::dvec3 * vert = v_buf + v_cnt++;

			double vert_x = (double)x * scale * ONE_OVER_3 + cube_pos[0];
			double vert_y = (double)y * scale * ONE_OVER_3 + cube_pos[1];
			double vert_z = (double)z * scale * ONE_OVER_3 + cube_pos[2];

			if (hmpc) {
				double delta_z = CalcHMPDeltaZ(hmpc, vert_x, vert_y);
				vert_z += delta_z;
			}

			vert->x = vert_x;
			vert->y = vert_y;
			vert->z = vert_z;
		}

		// check vertices
		if (get_z_is_int_pos_) {
			for (uint32_t j = 0; j < v_cnt; ++j) {
				const glm::dvec3 & v0 = v_buf[j];

				if ((int)v0.x == get_z_ix_ && (int)v0.y == get_z_iy_) {
					ret_z = (float)v0.z;

					found = true;
					goto DONE;
				}
			}
		}

		for (uint16_t j = 0; j < cmd->num_triangle_; ++j) {
			uint32_t tri = triangles[j];

			uint8_t indices[3];

			if (trans_flag & 4) {
				// mirrored
				indices[0] = (uint8_t)((tri & 0xff00) >> 8);
				indices[1] = (uint8_t)(tri & 0xff);
				indices[2] = (uint8_t)((tri & 0xff0000) >> 16);
			}
			else {
				indices[0] = (uint8_t)((tri & 0xff0000) >> 16);
				indices[1] = (uint8_t)(tri & 0xff);
				indices[2] = (uint8_t)((tri & 0xff00) >> 8);
			}

			bool inside_cur_tri = true;

			for (int k = 0; k < 3; ++k) {
				int idx0 = indices[k];
				int idx1 = indices[(k + 1) % 3];

				glm::dvec3 v_1_0 = v_buf[idx1] - v_buf[idx0];
				v_1_0.z = 0.0;

				glm::dvec3 v_pos_0;

				v_pos_0.x = get_z_pos_.x - v_buf[idx0].x;
				v_pos_0.y = get_z_pos_.y - v_buf[idx0].y;
				v_pos_0.z = 0.0;

				glm::dvec3 v_c = glm::cross(v_1_0, v_pos_0);

				if (v_c.z <= 0.0) {
					inside_cur_tri = false;
					break;
				}
			}

			if (inside_cur_tri) {
				glm::dvec3 v10 = v_buf[indices[1]] - v_buf[indices[0]];
				glm::dvec3 v20 = v_buf[indices[2]] - v_buf[indices[0]];

				glm::dvec3 n = glm::normalize( glm::cross(v10, v20) );

				double d = -glm::dot(v_buf[indices[2]], n);

				ret_z = (float)(-(n.x * get_z_pos_.x + n.y * get_z_pos_.y + d) / n.z);

				found = true;
				goto DONE;
			}

		}

		goto DONE;
	}
	else {
		uint8_t children_mask = cube_node->children_mask_;

		if (children_mask) {

			// check down to next level
			const uint8_t* children_access_order = CUBE_IDX_TABLE + trans_flag * 8;
			const uint8_t* trans_lines = CUBE_TRANS_TABLE + trans_flag * 8;

			int sub_cube_level = cube_level + 1;
			int sub_cube_dim = cube_dim >> 1;

			constexpr int CHILD_ACCESS_ORDER[] = { 4, 0, 5, 1, 6, 2, 7, 3 };

			for (int i = 0; i < 8; ++i) {

				int access_idx = CHILD_ACCESS_ORDER[i];

				uint8_t child_idx = children_access_order[access_idx];

				if (children_mask & (1 << child_idx)) {

					glm::ivec3 sub_cube_pos;
					float min_xyz[3];
					float max_xyz[3];

					const int* dims = dim_table_ + access_idx * 3;

					for (int j = 0; j < 3; ++j) {
						sub_cube_pos[j] = cube_pos[j] + (dims[j] >> sub_cube_level);

						min_xyz[j] = (float)(sub_cube_pos[j] - sub_cube_dim);
						max_xyz[j] = (float)(sub_cube_pos[j] + sub_cube_dim);
					}

					// check x & y range
					if (get_z_pos_.x >= min_xyz[0] && get_z_pos_.x <= max_xyz[0] &&
						get_z_pos_.y >= min_xyz[1] && get_z_pos_.y <= max_xyz[1])
					{

						const dyn_cube_s* sub_dyn_cube = nullptr;
						if (dyn_cube && dyn_cube->children_mask_ & (1 << child_idx)) {
							sub_dyn_cube = dyn_cube->children_[child_idx];
						}

						if (!sub_dyn_cube || !(cur_mod_options_ & TERRAIN_DISCARD_MOD) || !(sub_dyn_cube->flags_ & CUBE_FLAG_DISCARD_TERRAIN)) {

							found = RecursiveGetZ(
								sub_dyn_cube,
								ctr_ + cube_node->children_[child_idx],
								sub_cube_pos,
								sub_cube_level,
								sub_cube_dim,
								trans_lines[cube_node->cmd_transform_[child_idx]],
								ret_z
							);

							if (found) {
								goto DONE;
							}


						}

					}
				}

			}

			ret_z = get_z_pos_.z;

			goto DONE;	// not found, return old value
		}
		else {
			ret_z = get_z_pos_.z;
			goto DONE;	// not found, return old value
		}
	}

DONE:

	if (dyn_cube) {
		DynCube_RunQTaskPopFunc(dyn_cube);
	}

	return found;
}

void Terrain::EditorRaycastAndModify(const dyn_cube_s* root_dyn_cube, const glm::vec3& ray_origin, const glm::vec3& ray_dir, int brush_type) {
	// Basic Raymarch
	glm::vec3 current_pos = ray_origin;
	float step_size = 500.0f; // 500 units per step
	bool hit = false;
	
	for (int i = 0; i < 10000; ++i) { // max 5,000,000 units distance
		current_pos += ray_dir * step_size;
		
		float terrain_z = 0.0f;
		if (GetZ(root_dyn_cube, current_pos, terrain_z)) {
			if (current_pos.z <= terrain_z + 100.0f) { // Added a tiny tolerance to prevent slipping through cracks
				hit = true;
				break;
			}
		}
	}
	
	if (!hit) {
		printf("Raymarch miss\n");
		return;
	}

	printf("Raymarch HIT at (%.2f, %.2f, %.2f)\n", current_pos.x, current_pos.y, current_pos.z);

	// We have an approximate hit point on the terrain (current_pos.x, current_pos.y)
	// Now we need to modify the height map at this location.
	// Iterate over all active height maps to find which one covers this X/Y area.
	
	double radius = 50000.0; // Brush radius
	double max_strength = 15.0; // Max height change per frame at center
	
	int changed_count = 0;

	for (int i = 0; i < num_height_map_; ++i) {
		height_map_s* hmp = height_maps_ + qtask_height_maps_[i].idx_in_array_;
		
		int hmp_dim = 1 << hmp->height_map_line_width_shift_;
		
		// Find world coordinate bounds for this height map
		double min_x = hmp->cube_min_x_;
		double min_y = hmp->cube_min_y_;
		double max_x = min_x + (hmp_dim / hmp->local_pos_to_hmp_pos_);
		double max_y = min_y + (hmp_dim / hmp->local_pos_to_hmp_pos_);
		
		// Does the brush intersect this height map bounding box?
		if (current_pos.x + radius < min_x || current_pos.x - radius > max_x ||
			current_pos.y + radius < min_y || current_pos.y - radius > max_y) {
			continue;
		}
		
		// Modify pixels in the height map
		for (int y = 0; y <= hmp_dim; ++y) {
			for (int x = 0; x <= hmp_dim; ++x) {
				double world_x = min_x + x / hmp->local_pos_to_hmp_pos_;
				double world_y = min_y + y / hmp->local_pos_to_hmp_pos_;
				
				double dx = (double)current_pos.x - world_x;
				double dy = (double)current_pos.y - world_y;
				double dist_sq = dx*dx + dy*dy;
				
				double rad_sq = radius * radius;
				if (dist_sq <= rad_sq) {
					int idx = y * (hmp_dim + 1) + x;
					// Read as uint8_t because CalcHMPDeltaZ casts it to uint8_t!
					int current_val = (uint8_t)hmp->height_map_item_[idx];
					
					// Smooth falloff based on distance
					double falloff = 1.0 - (dist_sq / rad_sq);
					int applied_strength = (int)(max_strength * falloff);
					if (applied_strength < 1) applied_strength = 1;

					if (brush_type == 0) { // BRUSH_RAISE
						current_val += applied_strength;
					} else {
						current_val -= applied_strength;
					}
					
					// Clamp to uint8 bounds (0 to 255)
					if (current_val > 255) current_val = 255;
					if (current_val < 0) current_val = 0;
					
					hmp->height_map_item_[idx] = (int8_t)current_val;
					changed_count++;
				}
			}
		}
	}
	
	printf("Modified %d pixels across height maps\n", changed_count);

	// Force the terrain engine to rebuild the chunks since data changed
	ClearCubeDataHash();
}
