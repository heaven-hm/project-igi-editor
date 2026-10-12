/******************************************************************************
 * @file    level.h
 * @brief   level
 *****************************************************************************/

#pragma once
#include "level_objects.h"


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
	void					SaveObjectsLocalOnly();
	void					SaveChanges();
	void					SaveAndReloadObjects();
	// Reparse the live objects from a QSC file (used by Load Sub-Task after merging
	// a saved task block into the temp objects QSC).
	void					ReloadObjectsFromFile(const std::string& qscPath);

	bool					GetTerrainZ(double x, double y, float & z, bool ignore_discard = false);
	// World (x,y) -> terrain CTR node index (the "terrain id"), or -1 if off-terrain.
	int						GetTerrainNodeId(double x, double y);
	void					EditorRaycastAndModify(const glm::vec3& ray_origin, const glm::vec3& ray_dir, int brush_type, double radius, double strength);
	void					TeleportToHMP(glm::vec3& pos) const;
	void					CompileCurrentQSC(int level_no);

	const LevelObjects&		GetLevelObjects() const { return level_objects_; }
	LevelObjects&			GetLevelObjects() { return level_objects_; }

	const glm::vec3&		GetStartPos() const { return start_pos_; }
	float					GetStartYaw() const { return start_yaw_; }

private:
	glm::vec3				start_pos_ = glm::vec3(0.0f);
	float					start_yaw_ = 0.0f;

	int						cur_level_no_;
	std::string				qsc_path_;

	float					flat_sky_fog_amount_;
	float					flat_sky_z_pos_;
	float					flat_sky_distance_;
	FlatSkyLayer			flat_sky_layers_[MAX_FLAT_SKY_LAYERS];
	Terrain					terrain_;
	LevelObjects			level_objects_;


	fixed_size_item_pool_s	dyn_cube_item_pool_;
	dyn_cube_s *			root_dyn_cube_;

	bool					loaded_;

	void					LoadStartPosInfo(const QSC* qsc_objects, glm::vec3& start_pos, float& start_yaw) const;
	void					LoadFogInfo(const QSC * qsc_objects, IRenderResLoader* render_res_loader);
	void					LoadSkydomeInfo(const QSC* qsc_objects, IRenderResLoader* render_res_loader);
	void					LoadFlatSkyLayersInfo(const QSC* qsc_objects, IRenderResLoader* render_res_loader);
	void					DecompileObjects(int levelNo);
	bool					FilesDiffer(const std::string& file1, const std::string& file2);
	void					CopyTerrainFromIGI(int level_no);
	void					MoveTerrainToGamePath(int level_no);

	// pos range: [-2^30, 2^30]
	dyn_cube_s *			GetDynCube(const double pos[3], int cube_lod_level, glm::ivec3& cube_ctr) override;
	void					AddQTaskToDynCube(dyn_cube_s* dyn_cube, qtask_s* qtask) override;
};
