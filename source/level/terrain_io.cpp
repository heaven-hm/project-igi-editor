/******************************************************************************
 * @file    terrain_io.cpp
 * @brief   Terrain implementation: file loaders + QSC metadata parsing
 *          Split from terrain.cpp; shares terrain_internal.h.
 *****************************************************************************/
#include "terrain_internal.h"
#include <memory>
#include <cmath>

/*
================================================================================
 Terrain child-task cell grid  (transcribed from IGI-RE, retail IGI.exe)
================================================================================

 Every terrain child task is placed by snapping its script position to a
 power-of-two cell grid and using the cell's *centre* as the object position;
 the cell minimum then becomes the origin of the task's map (bitmap / lightmap
 / heightmap):

     int cell2 = (0x40000000 >> shift) * 2;    // cell edge; shift = QSC "Level"
     min.x     = fistp(pos.x) & -cell2;        // pos already includes w->offset[]
     max.x     = min.x + cell2;
     centre.x  = min.x + cell2 / 2;
     ...
     <map>->x  = <task>->pos.x - (0x40000000 >> shift);    // == min.x

   * TextureModifier_Changed   HOOK 0x5062A0  src/editcam/texmod.c:235-464
   * TerrainLightMap_Changed   HOOK 0x4F8780  src/graphnode/lightmap.c:325-431
   * HeightMap_Changed         HOOK 0x507410  src/editcam/heightmap.c:265-505
   * DiscardTerrain_Changed    HOOK 0x52C290  src/terredit/discard.c:200-248

 The engine's fistp (src/graphnode/lightmap.c:108-112) is `fistpll`, i.e. round
 to nearest even, which is what `std::nearbyint` does in the default rounding
 mode (the x87 asm itself is not portable off i686). `w->offset[]` is zero for a
 freshly loaded world (World_New, HOOK 0x4C5B20, src/magicobj/world.c:183-185).
*/
static int Terrain_EngineFistp(double v) {
	return (int)std::nearbyint(v);
}

// out_min: the 3 snapped cell minima; out_center: the engine's object position.
static void Terrain_SnapCell(const double pos[3], int shift, double out_min[3], double out_center[3]) {
	const int cell2 = (0x40000000 >> (shift & 31)) * 2;

	for (int i = 0; i < 3; ++i) {
		const int cell_min = Terrain_EngineFistp(pos[i]) & -cell2;
		out_min[i] = (double)cell_min;
		out_center[i] = (double)cell_min + (double)cell2 * 0.5;
	}
}

bool Terrain::Load(load_params_s & params) {
	Logger::Get().Log(LogLevel::INFO, "[Terrain] ==========================================");
	Logger::Get().Log(LogLevel::INFO, "[Terrain] Load() START for level " + std::to_string(params.level_no_));
	Logger::Get().Log(LogLevel::INFO, "[Terrain] ==========================================");
	
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
	
	Logger::Get().Log(LogLevel::INFO, "[Terrain] Loading terrain.qsc from: " + std::string(filename));

	// Check if terrain.qsc exists; if not, try decompiling terrain.qvm on the fly
	if (!File_Exists(filename)) {
		std::string qvmPath = terrainDir + "/terrain.qvm";
		if (File_Exists(qvmPath.c_str())) {
			Logger::Get().Log(LogLevel::INFO, "[Terrain] terrain.qsc missing — decompiling terrain.qvm...");
			QVMFile qvm = QVM_Parse(qvmPath);
			if (!qvm.valid || !QVM_Decompile(qvm, std::string(filename))) {
				Logger::Get().Log(LogLevel::ERR, "[Terrain] FATAL: terrain.qvm decompile failed for: " + qvmPath);
				return false;
			}
			Logger::Get().Log(LogLevel::INFO, "[Terrain] terrain.qvm decompiled to terrain.qsc successfully");
		} else {
			Logger::Get().Log(LogLevel::ERR, "[Terrain] FATAL: terrain.qsc and terrain.qvm both missing at: " + terrainDir);
			return false;
		}
	}
	Logger::Get().Log(LogLevel::INFO, "[Terrain] terrain.qsc exists, loading...");

	auto qsc_terrain = std::make_unique<QSC>();
	qsc_terrain->Load(filename);
	if (qsc_terrain->HadOverflow()) {
		Logger::Get().Log(LogLevel::ERR, "[Terrain] FATAL: terrain.qsc parser overflow (too many functions/args)");
		return false;
	}
	Logger::Get().Log(LogLevel::INFO, "[Terrain] terrain.qsc loaded successfully");

	// File order is the engine's, not ours.
	//
	// LevelStart 0x415B30 (src/game/game.c:405-407) runs
	//     crt_sprintf(terrain, "%sterrain/terrain", gMissionPath);
	//     World_LoadTextures(gGame->world, terrain);
	//     Terrain_Load(gGame->world, terrain);
	// so World_LoadTextures 0x4C5F00 (src/magicobj/world.c:295-343) runs BEFORE
	// Terrain_Load 0x4C6240 (src/terrain/terrain.c:70-153):
	//   World_LoadTextures: <path>.tex (TexSet_Load, :305-308), then <path>.lmp
	//     (:317-322) whose full name is kept for the later Terrain_LoadLightmaps
	//     pass 0x4C6160 (src/terrain/terrain.c:36-67).
	//   Terrain_Load: .bit (:80-98), .hmp (:100-122), .cmd (:124-126),
	//     .ctr (:128-130), then <path>.qsc (:132-140) whose run invokes
	//     CreateTerrainMaterial / CreateTerrainTileMap.
	// Net file order: tex, lmp, bit, hmp, cmd, ctr, qsc.

	Logger::Get().Log(LogLevel::INFO, "[Terrain] Loading TEX file...");
	if (!LoadTEXFile(params)) {
		Logger::Get().Log(LogLevel::ERR, "[Terrain] FATAL: LoadTEXFile failed");
		return false;
	}
	Logger::Get().Log(LogLevel::INFO, "[Terrain] TEX file loaded");

	Logger::Get().Log(LogLevel::INFO, "[Terrain] Loading LMP file...");
	if (!LoadLMPFile(params)) {
		Logger::Get().Log(LogLevel::ERR, "[Terrain] FATAL: LoadLMPFile failed");
		return false;
	}
	Logger::Get().Log(LogLevel::INFO, "[Terrain] LMP file loaded");

	Logger::Get().Log(LogLevel::INFO, "[Terrain] Loading BIT file...");
	if (!LoadBITFile(params)) {
		Logger::Get().Log(LogLevel::ERR, "[Terrain] FATAL: LoadBITFile failed");
		return false;
	}
	Logger::Get().Log(LogLevel::INFO, "[Terrain] BIT file loaded");

	Logger::Get().Log(LogLevel::INFO, "[Terrain] Loading HMP file...");
	if (!LoadHMPFile(params)) {
		Logger::Get().Log(LogLevel::ERR, "[Terrain] FATAL: LoadHMPFile failed");
		return false;
	}
	Logger::Get().Log(LogLevel::INFO, "[Terrain] HMP file loaded");

	Logger::Get().Log(LogLevel::INFO, "[Terrain] Loading CMD file...");
	if (!LoadCMDFile(params)) {
		Logger::Get().Log(LogLevel::ERR, "[Terrain] FATAL: LoadCMDFile failed");
		return false;
	}
	Logger::Get().Log(LogLevel::INFO, "[Terrain] CMD file loaded");

	// LoadCTRFile also applies the engine's leaf fixup
	// (Terrain_Load 0x4C6240, src/terrain/terrain.c:142-143:
	//  t->ctr[i].leaf = (const short *)((const char *)t->ctr[i].leaf + (uintptr_t)t->cmd)).
	// The engine does that after the qsc run below; the two qsc callbacks only
	// write gTerrainMaterials / gTerrainTileMaps (src/terrain/terrain.c:165-195)
	// and never touch the ctr, so the position of the fixup is immaterial.
	Logger::Get().Log(LogLevel::INFO, "[Terrain] Loading CTR file...");
	if (!LoadCTRFile(params)) {
		Logger::Get().Log(LogLevel::ERR, "[Terrain] FATAL: LoadCTRFile failed");
		return false;
	}
	Logger::Get().Log(LogLevel::INFO, "[Terrain] CTR file loaded");

	// The engine's .qsc pass: Terrain_Load 0x4C6240 (src/terrain/terrain.c:132-140)
	// registers CreateTerrainMaterial/CreateTerrainTileMap, runs the script,
	// frees it and unregisters. The two passes below are that run.
	LoadMaterialInfo(qsc_terrain.get());
	LoadTileMapInfo(qsc_terrain.get());
	// qsc_terrain freed automatically when it goes out of scope (RAII).

	Logger::Get().Log(LogLevel::INFO, "[Terrain] Loading texture modifiers...");
	LoadTextureModifier(params.level_dyn_cube_, params.qsc_objects_);
	
	Logger::Get().Log(LogLevel::INFO, "[Terrain] Loading terrain lightmap info...");
	LoadTerrainLightMapInfo(params.level_dyn_cube_, params.qsc_objects_);
	
	Logger::Get().Log(LogLevel::INFO, "[Terrain] Loading heightmap info...");
	LoadHeightMapInfo(params.level_dyn_cube_, params.qsc_objects_);
	
	Logger::Get().Log(LogLevel::INFO, "[Terrain] Loading discard terrain info...");
	LoadDiscardTerrainInfo(params.level_dyn_cube_, params.qsc_objects_);
	
	Logger::Get().Log(LogLevel::INFO, "[Terrain] ==========================================");
	Logger::Get().Log(LogLevel::INFO, "[Terrain] Load() COMPLETE for level " + std::to_string(params.level_no_));
	Logger::Get().Log(LogLevel::INFO, "[Terrain] ==========================================");

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

	Logger::Get().Log(LogLevel::INFO, "[Terrain::Save] Saving terrain to: " + std::string(filename));

	if (!hmp_) {
		Logger::Get().Log(LogLevel::ERR, "[Terrain::Save] hmp_ is null, cannot save terrain");
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

	// Check if file already exists and compare content
	if (File_Exists(filename)) {
		void* existingBuf = nullptr;
		int32_t existingSize = 0;
		if (File_LoadBinary(filename, existingBuf, existingSize)) {
			if (existingSize == total_file_size && memcmp(existingBuf, hmp_, total_file_size) == 0) {
				Logger::Get().Log(LogLevel::INFO, "[Terrain::Save] No changes detected in terrain, skipping save to: " + std::string(filename));
				File_FreeBuf(existingBuf);
				return false;
			}
			File_FreeBuf(existingBuf);
		}
	}

	if (File_SaveBinary(filename, hmp_, total_file_size)) {
		Logger::Get().Log(LogLevel::INFO, "[Terrain::Save] Successfully saved terrain to: " + std::string(filename));
		return true;
	}

	Logger::Get().Log(LogLevel::ERR, "[Terrain::Save] Failed to save terrain to: " + std::string(filename));
	return false;
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

		if ((int)src->cmd_offset_ >= cmd_sz_) {
			Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "CTR cmd_offset out of bounds\n");
			return false;
		}

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
			if (pics.pics_[i].width_ != pics.pics_[i].height_) {
				Log(log_type_t::LOG_INFOR, __FILE__, __LINE__, "Lightmap width != height\n");
			}
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

	// Terrain_Load 0x4C6240 (src/terrain/terrain.c:82-98). A .bit shorter than
	// the 0x100-entry header (0x100 * sizeof(bit_item_s) = 0xC00) is replaced by
	// a zeroed 0xC00 header and every slot is left non-resident: the engine sets
	// gBitResident[i] = 0 for all 0x100 entries and links no item data.
	if (bit_sz < 0xC00) {
		File_FreeBuf(bit_);
		bit_ = MEM_ALLOC(0xC00);
		if (!bit_) {
			return false;
		}
		memset(bit_, 0, 0xC00);
		for (int i = 0; i < MAX_TEX_MOD; ++i) {
			bitmap_items_[i] = nullptr;
		}
		return true;
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

	// Terrain_Load 0x4C6240 (src/terrain/terrain.c:100-104): the .hmp is optional,
	// and a missing one leaves t->hmp == NULL with no height map items linked.
	if (!File_Exists(filename)) {
		hmp_ = nullptr;
		hmp_sz_ = 0;
		for (int i = 0; i < MAX_HMP; ++i) {
			height_map_items_[i] = nullptr;
		}
		return true;	// not an error, hmp file can be omitted
	}

	int32_t hmp_sz = 0;
	if (!File_LoadBinary(filename, hmp_, hmp_sz)) {
		return false;
	}
	hmp_sz_ = hmp_sz;

	// Terrain_Load 0x4C6240 (src/terrain/terrain.c:106-121). A .hmp shorter than
	// the 0x100-entry header (0xC00) is replaced by a zeroed 0xC00 header and
	// every slot is left non-resident (gHmpResident[i] = 0 for all 0x100).
	if (hmp_sz < 0xC00) {
		File_FreeBuf(hmp_);
		hmp_ = MEM_ALLOC(0xC00);
		if (!hmp_) {
			hmp_sz_ = 0;
			return false;
		}
		memset(hmp_, 0, 0xC00);
		hmp_sz_ = 0xC00;
		for (int i = 0; i < MAX_HMP; ++i) {
			height_map_items_[i] = nullptr;
		}
		return true;
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
		if (qtask_texture_modifier->bitmap_size_ < 8) {
			Log(log_type_t::LOG_INFOR, __FILE__, __LINE__, "bitmap_size < 8\n");
		}
		if (bitmap_item_size_array_[qtask_texture_modifier->bitmap_id_] != qtask_texture_modifier->bitmap_size_) {
			Log(log_type_t::LOG_INFOR, __FILE__, __LINE__, "bitmap size mismatch\n");
		}
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

		// TextureModifier_Changed 0x5062A0 (src/editcam/texmod.c:289-296) snaps
		// the task to the cell grid; the engine's DynCube position is the cell
		// centre, never the raw script point.
		double cell_min[3];
		double cell_center[3];
		Terrain_SnapCell(qtask->script_defined_pos_, (int)qtask->lod_level_, cell_min, cell_center);

		glm::ivec3 cube_ctr;
		dyn_cube_s* dyn_cube = level_dyn_cube->GetDynCube(cell_center, (int)qtask->lod_level_, cube_ctr);
		if (dyn_cube) {
			level_dyn_cube->AddQTaskToDynCube(dyn_cube, qtask);

			texture_modifier_s * texture_modifier = texture_modifiers_ + qtask->idx_in_array_;

			// texmod.c:459-460: hole->x = tm->obj.pos.x - (0x40000000 >> shift)
			// i.e. the snapped cell minimum, not the ctr bit-walk centre.
			texture_modifier->cube_min_x_ = cell_min[0];
			texture_modifier->cube_min_y_ = cell_min[1];
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
		if (light_map_item_size_array_[qtask_terrain_light_map->tex_idx_] != qtask_terrain_light_map->light_map_size_) {
			Log(log_type_t::LOG_INFOR, __FILE__, __LINE__, "lightmap size mismatch\n");
		}
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

		// TerrainLightMap_Changed 0x4F8780 (src/graphnode/lightmap.c:335-370):
		// half = (0x40000000 >> shift) * 2; min = fistp(place) & -half; the box is
		// [min, min + half) and its centre is what DynCube_AddBox/MoveBox store.
		double cell_min[3];
		double cell_center[3];
		Terrain_SnapCell(qtask->script_defined_pos_, (int)qtask->lod_level_, cell_min, cell_center);

		glm::ivec3 cube_ctr;	// return the cube center

		dyn_cube_s* dyn_cube = level_dyn_cube->GetDynCube(cell_center, (int)qtask->lod_level_, cube_ctr);
		if (dyn_cube) {
			level_dyn_cube->AddQTaskToDynCube(dyn_cube, qtask);

			terrain_light_map_s * terrain_light_map = terrain_light_maps_ + qtask->idx_in_array_;

			// TerrainLightMap_SetDrawInfo 0x4F8EF0 (src/graphnode/lightmap.c:434-437):
			// d->x = lm->pos.x - (0x40000000 >> shift) == the snapped cell minimum.
			terrain_light_map->cube_min_x_ = cell_min[0];
			terrain_light_map->cube_min_y_ = cell_min[1];

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
			Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "Too many heightmaps");
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

		// HeightMap_Enter 0x506F00 (src/editcam/heightmap.c:126-137) only makes a
		// height map take effect when its .hmp slot actually carries data:
		//     if (hm->hmpIndex >= 0 && hm->world->hmp[hm->hmpIndex].data) { shown = 1; ... }
		//     else { hm->shown = 0; }
		// Terrain_Load 0x4C6240 leaves the data pointer unset for every .hmp slot
		// whose header size is 0 (src/terrain/terrain.c:112-120), so an absent
		// slot means no bump is applied. Skip the entry entirely, as the engine
		// never enters it.
		if (!height_map_items_[qtask_height_map->height_map_id_]) {
			continue;
		}

#ifdef _DEBUG
		if (qtask_height_map->height_map_size_ < 2) {
			Log(log_type_t::LOG_INFOR, __FILE__, __LINE__, "height_map_size < 2\n");
		}
		if (height_map_item_size_array_[qtask_height_map->height_map_id_] != qtask_height_map->height_map_size_) {
			Log(log_type_t::LOG_INFOR, __FILE__, __LINE__, "heightmap size mismatch\n");
		}
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

		// HeightMap_Changed 0x507410 (src/editcam/heightmap.c:267-310): cell2 =
		// (0x40000000 >> shift) * 2; min = fistp(place) & -cell2; the box is
		// [min, min + cell2) and its centre is the DynCube position.
		double cell_min[3];
		double cell_center[3];
		Terrain_SnapCell(qtask_height_map->script_defined_pos_, (int)qtask_height_map->lod_level_, cell_min, cell_center);

		glm::ivec3 cube_ctr;

		dyn_cube_s* dyn_cube = level_dyn_cube->GetDynCube(cell_center, (int)qtask_height_map->lod_level_, cube_ctr);
		if (dyn_cube) {
			level_dyn_cube->AddQTaskToDynCube(dyn_cube, (qtask_s*)qtask_height_map);

			height_map_s * height_map = height_maps_ + qtask_height_map->idx_in_array_;

			// heightmap.c:459-460: bump->x = hm->obj.pos.x - (0x40000000 >> shift)
			// == the snapped cell minimum.
			height_map->cube_min_x_ = cell_min[0];
			height_map->cube_min_y_ = cell_min[1];
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

		// DiscardTerrain_Changed 0x52C290 (src/terredit/discard.c:203-247):
		// size = (0x40000000 >> (shift & 31)) * 2; min = fistp(pos) & -size;
		// max = min + size; DynCube_MoveBox(w, dt, &centre, &min, &max).
		// The walk drops the terrain of every cell inside that box
		// (DiscardTerrain_Visit 0x52C0B0 sets gQCubeSkipCells, src/lod/qcubewalk.c:294),
		// which is the cube at lod == shift and everything under it — the same
		// subtree the editor marks with CUBE_FLAG_DISCARD_TERRAIN.
		double cell_min[3];
		double cell_center[3];
		Terrain_SnapCell(qtask_discard_terrain->script_defined_pos_, (int)qtask_discard_terrain->lod_level_,
			cell_min, cell_center);

		glm::ivec3 cube_ctr;

		dyn_cube_s* dyn_cube = level_dyn_cube->GetDynCube(cell_center, (int)qtask_discard_terrain->lod_level_, cube_ctr);
		if (dyn_cube) {
			level_dyn_cube->AddQTaskToDynCube(dyn_cube, qtask_discard_terrain);
			dyn_cube->flags_ |= CUBE_FLAG_DISCARD_TERRAIN;
		}
	}
}

