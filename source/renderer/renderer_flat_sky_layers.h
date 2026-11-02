/******************************************************************************
 * @file    renderer_flat_sky_layers.h
 * @brief   flat sky layers renderer
 *****************************************************************************/

#pragma once

#include <vector>

/*
================================================================================
 Renderer_FlatSkyLayers
================================================================================
*/
class Renderer_FlatSkyLayers {
public:

	Renderer_FlatSkyLayers();
	~Renderer_FlatSkyLayers();

	bool					Init();
	void					Shutdown();

	void					UnloadAllTexs();
	void					LoadLayerTex(int layer_no, const pic_s* pic);

	// Per-frame dynamic geometry, filled by FlatSkyLayer::Update.  MapVB hands
	// out the CPU-side staging array and UnmapVB publishes it with an orphan +
	// glBufferSubData instead of glMapBuffer: Wine's wow64 GL cannot map a
	// buffer without copying it (fixme:opengl:wow64_map_buffer).
	vert_flat_sky_layer_s*	MapVB();
	void					UnmapVB();

	void					Draw(GLuint ubo_mats, bool overlay_wireframe);

private:

	GLuint					vbo_;

	gl_program_s			prg_solid_;
	gl_program_s			prg_wireframe_;

	// for GL 4.1
	GLint					tex_loc_;

	Renderer_FlatSkyLayer	layers_[MAX_FLAT_SKY_LAYERS];

	// CPU-side staging for the per-frame vertex fill (see MapVB/UnmapVB).
	std::vector<vert_flat_sky_layer_s>	vb_staging_;
};
