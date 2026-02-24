/******************************************************************************
 * @file    level.h
 * @brief   level
 *****************************************************************************/

#pragma once

/*
================================================================================
 Level
================================================================================
*/
class Level : public ILevelDynCube {
public:

	struct load_params_s {
		int					level_no_;
		IRenderResLoader*	render_res_loader_;
	};

	Level();
	~Level();

	void					FreeTerrainCubeDataPools();

	bool					Load(load_params_s & params, glm::vec3& start_pos, float& start_yaw);
	void					Unload();

	int						GetLevelNo() const;

	void					Update(update_params_s & params);

	bool					GetTerrainZ(const glm::vec3 & pos, float & z);

private:

	int						cur_level_no_;

	float					flat_sky_fog_amount_;
	float					flat_sky_z_pos_;
	float					flat_sky_distance_;
	FlatSkyLayer			flat_sky_layers_[MAX_FLAT_SKY_LAYERS];
	Terrain					terrain_;

	fixed_size_item_pool_s	dyn_cube_item_pool_;
	dyn_cube_s *			root_dyn_cube_;

	bool					loaded_;

	void					LoadStartPosInfo(const QSC* qsc_objects, glm::vec3& start_pos, float& start_yaw) const;
	void					LoadFogInfo(const QSC * qsc_objects, IRenderResLoader* render_res_loader);
	void					LoadSkydomeInfo(const QSC* qsc_objects, IRenderResLoader* render_res_loader);
	void					LoadFlatSkyLayersInfo(const QSC* qsc_objects, IRenderResLoader* render_res_loader);

	// pos range: [-2^30, 2^30]
	dyn_cube_s *			GetDynCube(const double pos[3], int cube_lod_level, glm::ivec3& cube_ctr) override;
	void					AddQTaskToDynCube(dyn_cube_s* dyn_cube, qtask_s* qtask) override;
};
