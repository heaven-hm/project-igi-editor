/******************************************************************************
 * @file    renderer_skydome.h
 * @brief   skydome renderer
 *****************************************************************************/

#pragma once

/*
================================================================================
 Renderer_Skydome
================================================================================
*/
class Renderer_Skydome {
public:

	Renderer_Skydome();
	~Renderer_Skydome();

	bool					Init();
	void					Shutdown();

	void					UpdateVertices(const skydome_define_s& d);

	void					Draw(GLuint ubo_mats, bool overlay_wireframe);

private:

	static constexpr int	INDEX_COUNT = 620 * 3;

	GLuint					vbo_;
	GLuint					ibo_;
	GLuint					vao_;

	gl_program_s			prg_solid_;
	gl_program_s			prg_wireframe_;
};
