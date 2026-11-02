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
	// NOTE: there is deliberately no SetFogColor. Retail draws the skydome with
	// D3DRENDERSTATE_FOGENABLE = 0 (SkyDome_Render HOOK 0x52CF80,
	// src/terredit/skydome.c:242) and takes every vertex colour from the level's
	// authored FlatSky gradient (SkyDome_Fill HOOK 0x52CB60, skydome.c:94-135), so
	// there is no fog term to set. Renderer::ApplyFogIntensity no longer calls one.

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
};
