/******************************************************************************
 * @file    renderer_flat_sky_layer.h
 * @brief   flat sky layer renderer
 *****************************************************************************/

#pragma once

/*
================================================================================
 Renderer_FlatSkyLayer
================================================================================
*/
class Renderer_FlatSkyLayer {
public:

	Renderer_FlatSkyLayer();
	~Renderer_FlatSkyLayer();

	bool					Init(int layer_no, GLuint vbo, int vert_per_layer);
	void					Shutdown();

	void					UnloadTex();
	void					LoadTex(const pic_s* pic);

	void					Draw(bool wireframe_mode);

private:

	GLuint					vao_;
	GLuint					vao_wireframe_;

	GLuint					tex_;
};
