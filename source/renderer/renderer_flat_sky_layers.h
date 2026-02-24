/******************************************************************************
 * @file    renderer_flat_sky_layers.h
 * @brief   flat sky layers renderer
 *****************************************************************************/

#pragma once

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
};
