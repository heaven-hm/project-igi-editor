/******************************************************************************
 * @file    renderer_terrain.h
 * @brief   terrain renderer
 *****************************************************************************/

#pragma once

#include <vector>

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

	// Per-frame dynamic terrain geometry.  The engine filled a D3D *dynamic*
	// vertex buffer (D3DLOCK_DISCARD) every frame; the editor used to do that
	// through a persistently mapped GL buffer, which Wine's wow64 GL cannot map
	// without copying the whole buffer in and back out
	// (fixme:opengl:wow64_map_buffer).  MapVB/MapIB now hand out the CPU-side
	// staging array, and UnmapVB/UnmapIB publish only the range the engine wrote
	// with an orphan + glBufferSubData: same data, no map.
	vert_pos_a_uv_s*		MapVB();
	void					UnmapVB(int num_vert);

	uint32_t *				MapIB();
	void					UnmapIB(int num_index);

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

	// CPU-side staging for the two per-frame dynamic buffers (see MapVB/MapIB).
	std::vector<vert_pos_a_uv_s>	vb_staging_;
	std::vector<uint32_t>			ib_staging_;

	void					SetRenderState(uint32_t render_layer) const;
	void					DrawLayer0RenderChunks(int num_render_chunk) const;
	void					DrawRenderChunk(const render_chunk_s* rc) const;
};
