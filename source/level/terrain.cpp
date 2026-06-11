/******************************************************************************
 * @file    terrain.cpp
 * @brief   terrain
 *****************************************************************************/

#include "terrain_internal.h"

// cube data pool capacities (tune these values)
constexpr int CUBE_DATA_1600_POOL_CAPACITY = 700;
constexpr int CUBE_DATA_3200_POOL_CAPACITY = 50;
constexpr int CUBE_DATA_8000_POOL_CAPACITY = 16;



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

void Terrain::Unload() {
	File_FreeBuf(hmp_);
	hmp_ = nullptr;
	File_FreeBuf(bit_);
	bit_ = nullptr;
	SAFE_FREE(ctr_);
	File_FreeBuf(cmd_);
	cmd_ = nullptr;
	cmd_sz_ = 0;

	// Reset renderer buffer pointers - these point to renderer-managed memory
	// that gets freed/reallocated when switching levels
	vertices_ = nullptr;
	indices_ = nullptr;
	render_chunks_ = nullptr;
	num_vertex_ = 0;
	num_index_ = 0;
	num_render_chunk_ = 0;

	ClearCubeDataHash();
}

const Terrain::ctr_node_s* Terrain::GetCtr() const {
	return ctr_;
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
