/******************************************************************************
 * @file    terrain.h
 * @brief   terrain
 *****************************************************************************/

#pragma once

#include <vector>

/*
================================================================================
 Terrain
================================================================================
*/

class Terrain {
public:

	struct ctr_node_s {
		int16_t				children_[8];		// children index in ctr_ array
		int8_t				cmd_transform_[8];	// for 8 children
		uint8_t				children_mask_;		// which children are valid
		const struct cmd_item_s *	cmd_;		// mesh
	};

	struct load_params_s {
		int					level_no_;			// 1~13
		ILevelDynCube*		level_dyn_cube_;	// interface
		IRenderResLoader*	render_res_loader_;
		const QSC*			qsc_objects_;		// loaded objects.qsc
	};

	Terrain();
	~Terrain();

	void					FreeCubeDataPools();

	bool					Load(load_params_s & params);
	bool					Save(int level_no);
	void					Unload();

	const ctr_node_s*		GetCtr() const;

	void					Update(update_params_s& params, const dyn_cube_s* root_dyn_cube);

	bool					GetZ(const dyn_cube_s* root_dyn_cube, double x, double y, float & ret_z, bool ignore_discard = false);
	void					EditorRaycastAndModify(const dyn_cube_s* root_dyn_cube, const glm::vec3& ray_origin, const glm::vec3& ray_dir, int brush_type, double radius, double strength);
	bool					GetFirstHMPCenter(glm::vec3& out_pos) const;

	// Snapshot/restore the height-map pixel buffer for undo/redo. The HMP file
	// body is one contiguous block; height_map_items_[] points into it, so a
	// byte-for-byte copy preserves all in-place edits without recomputing
	// pointers. Returns an empty buffer when the level has no .hmp file.
	std::vector<uint8_t>	SnapshotHMP() const;
	void					RestoreHMP(const std::vector<uint8_t>& snap);

	// Index (into ctr_) of the leaf CTR node found by the most recent GetZ() query,
	// or -1 if none. Used to surface a "terrain id" for the hover tooltip (issue 3).
	int						GetLastLeafNodeIndex() const {
		return last_getz_leaf_node_ ? (int)(last_getz_leaf_node_ - ctr_) : -1;
	}

public:

	static const uint8_t	CUBE_IDX_TABLE[];

private:

	static const uint8_t	CUBE_TRANS_TABLE[];

	static constexpr int	MAX_TEX_MOD = 256;
	static constexpr int	MAX_LMP = 2048;
	static constexpr int	MAX_HMP = 256;
	static constexpr int	MAX_DISCARD_TERRAIN = 256;

	// task types for terrain
	static constexpr int32_t	TASK_TYPE_TERRAIN_TEXTURE_MODIFIER = 74;
	static constexpr int32_t	TASK_TYPE_TERRAIN_LIGHT_MAP = 113;
	static constexpr int32_t	TASK_TYPE_TERRAIN_HEIGHT_MAP = 119;
	static constexpr int32_t	TASK_TYPE_DISCARD_TERRAIN = 62;

	static constexpr uint32_t	CUBE_DATA_BUF_TYPE_1600 = 0;
	static constexpr uint32_t	CUBE_DATA_BUF_TYPE_3200 = 1;
	static constexpr uint32_t	CUBE_DATA_BUF_TYPE_8000 = 2;

	static constexpr int	LEAF_CUBE_LOD_LEVEL = 16;
	static constexpr int	RENDER_CUBE_MIN_LOD_LEVEL = 7;	// draw cube lod level in between 7 ~ 16
	static constexpr int	MIN_CUBE_LOD_LEVEL_TO_APPLY_HMP = 14;
	static constexpr int	MAX_RENDER_CUBE = 4096;
	static constexpr int	CUBE_DATA_HASH_ITEM_COUNT = 2048;

	// objects.qsc "TextureModifier"
	struct qtask_texture_modifier_s : qtask_s {
		double				script_defined_pos_[3];
		uint32_t			lod_level_;
		int					bitmap_id_;
		uint32_t			material_idx_;
		uint32_t			bitmap_size_;	// power of 2
		uint32_t			idx_in_array_;	// index in qtask_texture_modifiers_
	};

	struct texture_modifier_s {
		uint32_t			material_idx_;
		uint32_t			bitmap_line_width_shift_;
		float				local_pos_to_bitmap_pos_;
		double				cube_min_x_;
		double				cube_min_y_;
		const uint8_t*		bitmap_item_;
	};

	// objects.qsc "TerrainLightMap"
	struct qtask_terrain_light_map_s : qtask_s {
		double				script_defined_pos_[3];
		uint32_t			lod_level_;
		uint32_t			light_map_size_;
		uint32_t			idx_in_array_;		// index in qtask_terrain_light_maps_
		uint32_t			tex_idx_;
	};

	struct terrain_light_map_s {
		float				one_over_cube_size_;
		float				half_pixel_uv_;
		double				cube_min_x_;
		double				cube_min_y_;
		int					tex_idx_;
	};

	// objects.qsc "HeightMap"
	struct qtask_height_map_s : qtask_s {
		double				script_defined_pos_[3];
		uint32_t			lod_level_;
		uint32_t			height_map_id_;
		uint32_t			height_map_size_;	// power of 2
		uint32_t			idx_in_array_;		// index in qtask_height_maps_
	};

	struct height_map_s {
		int					height_map_line_width_shift_;
		float				local_pos_to_hmp_pos_;
		double				cube_min_x_;
		double				cube_min_y_;
		int8_t*				height_map_item_;
	};

	// objects.qsc "DiscardTerrain"
	struct qtask_discard_terrain_s : qtask_s {
		double				script_defined_pos_[3];
		uint32_t			lod_level_;
	};

	// material info
	struct material_s {
		uint32_t			bool_;
		float				v1_;
		float				v2_;
		float				f_tex2_;					// value calc cube tile map for tex2
		int					tex2_tile_map_group_idx_;	// choose tile map group for tex2
		float				f_tex1_;					// value calc cube tile map for tex1
		int					tex1_tile_map_group_idx_;	// choose tile map group for tex1
	};

	struct material_array_s {
		uint32_t			id_;
		// item for lod level 0 ~ 31
		material_s			material_array_[32];
	};

	struct materials_s {
		// terrain_material_array_s choose by set bits
		//   bit range [0~31]
		material_array_s	material_array_map_[32];	// total 32 groups
	} materials_;

	// tile map info
	struct tile_map_s {
		uint8_t				tex_idx_in_terrain_tex_[64];
	} tile_maps_[64];

	struct cube_data_hash_s;

	struct render_cube_s {
		double				pos_[3];
		uint32_t			lod_level_;
		uint32_t			trans_flag_;	// 0~7
		const cmd_item_s*	cmd_;
		uint32_t			packed_hmp_idx_lmp_idx_;		// BYTE2: as height map search index, LOWER uint16_t as lightmap search index
		uint32_t			packed_tex_modifier_indices_;
		cube_data_hash_s*	cube_data_hash_;
	};

	struct cube_data_s {
		double				cube_ctr_pos_[3];
		uint16_t			total_vertex_count_;
		uint16_t			parent_vertex_count_;
		uint32_t			lod_level_;
		// cube_data_vert_pos_s goes here, variable length
		// cube_data_tri_and_tex_s
	};

	struct cube_data_vert_pos_s {
		float				pos_[3];
		union {
			float			delta_z_;
			uint32_t		parent_idx_;
		};
	};

	struct cube_data_tri_and_tex_s {
		uint32_t			trans_flag_;
		uint32_t*			triangle_indices_;	// each triange indices are packed to a uint32_t value, point to cmd file node
		uint32_t			tile_count_;		// set bits count
		uint32_t			triangle_count_;
	};

	struct cube_data_parent_vert_uv_s {
		// hi6 bits: self vert index
		uint32_t			packed_indices_;
		float				u_;
		float				v_;
	};

	struct cube_data_child_vert_uv_s {
		// high 16 bits: ref vertex index
		// lower 8 bits:
		uint32_t			packed_indices_;
		float				u_;
		float				v_;
		float				delta_u_to_parent_u_;
		float				delta_v_to_parent_v_;
	};

	struct cube_data_hash_s {
		glm::ivec3			pos_;
		uint32_t			frame_;
		uint32_t			buf_type_;	// allocate from which pool
		cube_data_s*		cube_data_;
	};

	// cmd file contents
	void*					cmd_;
	int32_t					cmd_sz_;

	// octree nodes. NOTE: ctr_ + 1 is the root node
	ctr_node_s*				ctr_;

	// leaf CTR node found by the most recent GetZ() query (issue 3), or nullptr
	const ctr_node_s*		last_getz_leaf_node_ = nullptr;

	// bit file contents
	void*					bit_;
	const uint8_t *			bitmap_items_[MAX_TEX_MOD];
#ifdef _DEBUG
	uint32_t				bitmap_item_size_array_[MAX_TEX_MOD];
#endif

#ifdef _DEBUG
	uint32_t				light_map_item_size_array_[MAX_LMP];
#endif

	// hmp file contents
	void*					hmp_;
	int32_t					hmp_sz_ = 0;
	const int8_t *			height_map_items_[MAX_HMP];
#ifdef _DEBUG
	uint32_t				height_map_item_size_array_[MAX_HMP];
#endif

	// texture modifer
	qtask_texture_modifier_s	qtask_texture_modifiers_[MAX_TEX_MOD];
	texture_modifier_s		texture_modifiers_[MAX_TEX_MOD];
	int						num_texture_modifier_;

	// terrain lightmap
	qtask_terrain_light_map_s	qtask_terrain_light_maps_[MAX_LMP];
	terrain_light_map_s		terrain_light_maps_[MAX_LMP];
	int						num_terrain_light_map_;

	// heightmap
	qtask_height_map_s		qtask_height_maps_[MAX_HMP];
	height_map_s			height_maps_[MAX_HMP];
	int						num_height_map_;

	// discard terrain
	qtask_discard_terrain_s		qtask_discard_terrains_[MAX_DISCARD_TERRAIN];
	int						num_discard_terrain_;

	// render cubes
	render_cube_s			render_cubes_[MAX_RENDER_CUBE];
	int						num_render_cube_;

	cube_data_s*			cube_data_array_[MAX_RENDER_CUBE];
	int						num_cube_data_;

	cube_data_hash_s		cube_data_hash_[CUBE_DATA_HASH_ITEM_COUNT];

    // texture modifier stack
	uint8_t					material_indices_stack_[20];
	int						material_indices_stack_size_;
	uint32_t				packed_material_indices_;

	// light map stack
	uint8_t					light_map_stack_[20];
	int						light_map_stack_size_;

	// height map stack
	uint32_t				height_map_stack_[64];
	int						height_map_stack_size_;

	// frames
	uint32_t				pre_frame_;
	uint32_t				cur_frame_;

	// modifier options
	int						pre_mod_options_;
	int						cur_mod_options_;

	// vbo, ibo buffers
	vert_pos_a_uv_s *		vertices_;
	uint32_t *				indices_;
	render_chunk_s *		render_chunks_;
	int						num_vertex_;
	int						num_index_;
	int						num_render_chunk_;

	glm::ivec3				cur_int_view_pos_;
	glm::ivec3				cur_int_view_pos_rotated_;

	// for 8 children
	int						dim_table_[3 * 8];
	int						dim_table_rotated_[3 * 8];

	float					tan_half_fovx_;
	float					tan_half_fovy_;

	// cube data pools
	fixed_size_item_pool_s	cube_data_1600_bytes_item_pool_;
	fixed_size_item_pool_s	cube_data_3200_bytes_item_pool_;
	fixed_size_item_pool_s	cube_data_8000_bytes_item_pool_;

	// get z
	glm::dvec3				get_z_pos_;
	bool					get_z_is_int_pos_;
	int						get_z_ix_;
	int						get_z_iy_;

	void					LoadMaterialInfo(const QSC* qsc_terrain);
	void					LoadTileMapInfo(const QSC* qsc_terrain);
	bool					LoadCMDFile(load_params_s & params);
	bool					LoadCTRFile(load_params_s & params);
	bool					LoadTEXFile(load_params_s & params);
	bool					LoadLMPFile(load_params_s & params);
	bool					LoadBITFile(load_params_s & params);
	bool					LoadHMPFile(load_params_s & params);
	void					LoadTextureModifier(ILevelDynCube * level_dyn_cube, const QSC* qsc_objects);
	void					LoadTerrainLightMapInfo(ILevelDynCube* level_dyn_cube, const QSC* qsc_objects);
	void					LoadHeightMapInfo(ILevelDynCube* level_dyn_cube, const QSC* qsc_objects);
	void					LoadDiscardTerrainInfo(ILevelDynCube* level_dyn_cube, const QSC* qsc_objects);

	void					ClearCubeDataHash();

	void					GenerateRenderCube(const dyn_cube_s* root_dyn_cube,
								const ctr_node_s* root_ctr_cube,
								float root_cube_ctr_horizontal_dist_to_clip_plane, float root_cube_ctr_vertical_dist_to_clip_plane);

	void					DynCube_RunQTaskPushFunc(const dyn_cube_s* dyn_cube);
	void					DynCube_RunQTaskPopFunc(const dyn_cube_s* dyn_cube);

	void					TryAddRenderCube(const ctr_node_s* ctr_node, const glm::ivec3 & cube_pos, int cube_level,
								uint32_t trans_flag, const dyn_cube_s* dyn_cube);

	cube_data_s *			AllocCubeData(uint32_t need_size, uint32_t & buf_type);
	void					FreeCubeData(void* ptr, uint32_t buf_type);

	void					GenerateCubeData();

	void					GenerateCubeMesh(const cube_data_s* cube_data);
	render_chunk_s *		GetRenderChunkByTex(uint32_t render_layer, uint32_t tex);

	double					CalcHMPDeltaZ(const height_map_s* height_map, double vert_x, double vert_y);

	// get height
	bool					RecursiveGetZ(const dyn_cube_s* dyn_cube, const ctr_node_s* cube_node,
								glm::ivec3 & cube_pos, int cube_level, int cube_dim, uint8_t trans_flag, float & ret_z, bool ignore_discard);
};
