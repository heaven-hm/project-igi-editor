/******************************************************************************
 * @file    renderer.h
 * @brief   main renderer
 *****************************************************************************/

#pragma once

#include <string>
#include <unordered_map>
#include "renderer_objects.h"
#include "renderer_splines.h"


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
    static constexpr int    DRAW_BUILDINGS = FLAG_BIT(4);      // 16
    static constexpr int    DRAW_PROPS = FLAG_BIT(5);          // 32
    static constexpr int    DRAW_AI = FLAG_BIT(6);             // 64
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
		bool				show_magic_obj_spheres_;

	};

	struct task_tree_view_params_s {
		bool show_hud_;
		std::string status_msg_;
		bool pause_mode_;
		bool show_debug_;
		bool show_help_;
		bool edit_mode_;
		bool terrain_edit_enabled_;
		int selected_object_index_;
		int hover_object_index_;
		int hover_tree_index_;
		int mouse_x_;
		int mouse_y_;
		int tree_scroll_offset = 0; // For scrolling the object tree
		bool tree_decl_expanded = false;
		const class LevelObjects* level_objects_;
		bool task_editor_open_;
		std::string edit_string_;
		int edit_cursor_pos_;
		int edit_selection_start_;
		int edit_selection_end_;
		int edit_box_w_;
		int edit_box_h_;
		int edit_scroll_x_;
		bool task_picker_open_;
		int task_picker_selected_idx_;
		int task_picker_scroll_offset_;
		std::string task_picker_search_;
		bool enable_camera_mode_;
	};


	Renderer();
	~Renderer();

	bool					Init();
	void					Shutdown();

	void					BeginLoadLevel();
	void					SetLevel(int level) { objects_.SetLevel(level); }

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

	void					Draw(const draw_params_s& params, const task_tree_view_params_s& task_tree_view);
	glm::vec3				GetMeshExtents(const std::string& modelId, bool isBuilding) { return objects_.GetMeshExtents(modelId, isBuilding); }
	float					GetMeshZOffset(const std::string& modelId, bool isBuilding) { return objects_.GetMeshZOffset(modelId, isBuilding); }

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
	Renderer_Splines		splines_;

	glm::mat4				mat_proj_;
	glm::mat4				mat_view_;
	void					SetupUBOMats(const view_define_s& vd);

};
