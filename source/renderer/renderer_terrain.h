/******************************************************************************
 * @file    renderer_terrain.h
 * @brief   terrain renderer
 *****************************************************************************/

#pragma once

/*
================================================================================
 Renderer_Terrain
================================================================================
*/
class Renderer_Terrain {
public:

	static constexpr int	DRAW_TERRAIN_OPT_MAT = FLAG_BIT(0);
	static constexpr int	DRAW_TERRAIN_OPT_LMP = FLAG_BIT(1);
	static constexpr int	DRAW_TERRAIN_OPT_FOG = FLAG_BIT(2);

	Renderer_Terrain();
	~Renderer_Terrain();

	bool					Init();
	void					Shutdown();

	void					UnloadAllTexs();
	void					LoadMatTex(const pic_s* pic);
	void					LoadLMPTex(const pic_s* pic);

	vert_pos_a_uv_s*		MapVB();
	void					UnmapVB();

	uint32_t *				MapIB();
	void					UnmapIB();

	render_chunk_s *		GetRenderChunckBuffer();

	void					Draw(GLuint ubo_mats, GLuint ubo_fog, bool overlay_wireframe, int draw_options, int num_render_chunk);

private:

	static constexpr int	MAX_TEX_IN_LIST = 4096;

	struct gl_tex_list_s {
		uint32_t			texs_[MAX_TEX_IN_LIST];
		int32_t				num_tex_;
	};

	gl_tex_list_s			mat_tex_;
	gl_tex_list_s			lmp_tex_;

	GLuint					vbo_;
	GLuint					ibo_;
	GLuint					vao_;
	GLuint					vao_fog_;

	gl_program_s			prg_mat_;
	gl_program_s			prg_lmp_;
	gl_program_s			prg_fog_;
	gl_program_s			prg_wireframe_;

	// for GL 4.1
	GLint					prg_mat_tex_loc_;
	GLint					prg_lmp_tex_loc_;

	render_chunk_s			render_chunks_[MAX_TERRAIN_RENDER_CHUNK];

	void					SetRenderState(uint32_t render_layer) const;
	void					DrawLayer0RenderChunks(int num_render_chunk) const;
	void					DrawRenderChunk(const render_chunk_s* rc) const;
};
