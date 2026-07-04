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
	// Call after SetupFog so the horizon fog blend re-bakes into the VBO.
	void					SetFogColor(const glm::vec3& fog_color);

	void					Draw(GLuint ubo_mats, bool overlay_wireframe);

private:

	void					BakeVertices();

	static constexpr int	INDEX_COUNT = 620 * 3;

	GLuint					vbo_;
	GLuint					ibo_;
	GLuint					vao_;

	gl_program_s			prg_solid_;
	gl_program_s			prg_wireframe_;

	skydome_define_s		last_sd_  = {};
	glm::vec3				fog_color_ = glm::vec3(0.15f, 0.15f, 0.15f);
};
