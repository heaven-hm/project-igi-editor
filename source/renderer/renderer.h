/******************************************************************************
 * @file    renderer.h
 * @brief   main renderer
 *****************************************************************************/

#pragma once

#include <string>
#include "renderer_objects.h"


/*
================================================================================
 Renderer
================================================================================
*/
class Renderer : public IRenderResLoader {
public:

	// draw parts
	static constexpr int	DRAW_TERRAIN = FLAG_BIT(0);        // 1
	static constexpr int	DRAW_SKY = FLAG_BIT(1);            // 2
	static constexpr int	DRAW_OBJECTS = FLAG_BIT(2);        // 4
	static constexpr int	DRAW_FLAT_SKY_LAYER = FLAG_BIT(3); // 8
    static constexpr int    DRAW_SKYDOME = FLAG_BIT(1);        // Alias for SKY



	struct draw_params_s {
		const view_define_s* view_define_;
		bool				overlay_wireframe_;
		int					draw_parts_;
		int					draw_terrain_options_;
		bool				flat_sky_layer_is_visible_;
		int					num_terrain_render_chunk_;
		const class LevelObjects* level_objects_;
		int					selected_object_index_;

	};

	struct hud_params_s {
		bool show_hud_;
		std::string status_msg_;
		glm::vec3 raw_pos_;
		glm::vec3 meters_pos_;
		glm::vec3 native_pos_;
		float ground_offset_;
		uint32_t human_addr_;
		int game_level_;
		float view_h_, view_v_;
		float cam_pitch_, cam_yaw_, cam_roll_, cam_fov_;
		bool pause_mode_;
		bool show_debug_;
		int selected_object_index_;
		const class LevelObjects* level_objects_;
	};


	Renderer();
	~Renderer() override;

	bool					Init();
	void					Shutdown();

	void					BeginLoadLevel();

	// interface of IRendererLoader
	void					SetupClearColor(const glm::vec4& color) override;
	void					SetupFog(const glm::vec4& color, float fog_far) override;
	void					SetupSkydome(const skydome_define_s& d) override;

	void					LoadFlatSkyLayerTex(int layer_no, const pic_s* pic) override;
	void					LoadTerrainMatTex(const pic_s* pic) override;
	void					LoadTerrainLMPTex(const pic_s* pic) override;

	vert_flat_sky_layer_s *	MapFlatSkyLayersVB();
	void					UnmapFlatSkyLayersVB();

	vert_pos_a_uv_s*		MapTerrainVB();
	void					UnmapTerrainVB();

	uint32_t*				MapTerrainIB();
	void					UnmapTerrainIB();

	render_chunk_s*			GetTerrainRenderChunckBuffer();

	void					Draw(const draw_params_s& params, const hud_params_s& hud);

private:

	struct ubo_mats_s {
		glm::mat4			mvp_mat_follow_view_;
		glm::mat4			mvp_flat_sky_layer_;
		glm::mat4			mvp_objects_;
	};

	struct ubo_fog_s {
		glm::vec4			color_;
		float				far_;
	};

	GLuint					ubo_mats_;
	GLuint					ubo_fog_;

	Renderer_Skydome		skydome_;
	Renderer_FlatSkyLayers	flat_sky_layers_;
	Renderer_Terrain		terrain_;
	Renderer_Objects		objects_;


	glm::mat4				mat_proj_;
	glm::mat4				mat_view_;
	void					SetupUBOMats(const view_define_s& vd);

};
