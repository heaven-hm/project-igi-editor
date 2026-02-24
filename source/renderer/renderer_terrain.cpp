/******************************************************************************
 * @file    renderer_terrain.cpp
 * @brief   terrain renderer
 *****************************************************************************/

#include "pch.h"

/*
================================================================================
 Renderer_Terrain
================================================================================
*/
Renderer_Terrain::Renderer_Terrain():
	vbo_(0),
	ibo_(0),
	vao_(0),
	vao_fog_(0),
	prg_mat_tex_loc_(-1),
	prg_lmp_tex_loc_(-1)
{
	mat_tex_.num_tex_ = 0;
	lmp_tex_.num_tex_ = 0;
}

Renderer_Terrain::~Renderer_Terrain() {
	// do nothing
}

bool Renderer_Terrain::Init() {
	if (!GL_CreateProgramVSFS("terrain_passing.vert", "terrain_mat.frag", prg_mat_)) {
		return false;
	}

	if (!GL_CreateProgramVSFS("terrain_passing.vert", "terrain_lmp.frag", prg_lmp_)) {
		return false;
	}

	if (!GL_CreateProgramVSFS("terrain_fog.vert", "terrain_fog.frag", prg_fog_)) {
		return false;
	}

	if (!GL_CreateProgramVSFS("terrain_passing.vert", "terrain_wireframe.frag", prg_wireframe_)) {
		return false;
	}

	if (!g_gl_info.support_version_45_) {
		// set binding points

		if (!GL_SetUniformBlockBinding(prg_mat_, "ub_mats", 0)) {
			return false;
		}

		if (!GL_SetUniformBlockBinding(prg_lmp_, "ub_mats", 0)) {
			return false;
		}

		if (!GL_SetUniformBlockBinding(prg_fog_, "ub_mats", 0)) {
			return false;
		}

		if (!GL_SetUniformBlockBinding(prg_fog_, "ub_fog", 1)) {
			return false;
		}

		if (!GL_SetUniformBlockBinding(prg_wireframe_, "ub_mats", 0)) {
			return false;
		}

		prg_mat_tex_loc_ = glGetUniformLocation(prg_mat_.program_, "g_tex");
		if (prg_mat_tex_loc_ == -1) {
			return false;
		}

		prg_lmp_tex_loc_ = glGetUniformLocation(prg_lmp_.program_, "g_tex");
		if (prg_lmp_tex_loc_ == -1) {
			return false;
		}
	}

	vbo_ = GL_CreateBuffer(GL_ARRAY_BUFFER, sizeof(vert_pos_a_uv_s) * MAX_TERRAIN_VERTICES, nullptr, GL_DYNAMIC_DRAW);
	ibo_ = GL_CreateBuffer(GL_ELEMENT_ARRAY_BUFFER, sizeof(uint32_t) * MAX_TERRAIN_INDICES, nullptr, GL_DYNAMIC_DRAW);

	// create vao
	glGenVertexArrays(1, &vao_);
	glBindVertexArray(vao_);

	glBindBuffer(GL_ARRAY_BUFFER, vbo_);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(vert_pos_a_uv_s), (const void*)0);	// xyz & a

	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(vert_pos_a_uv_s), (const void*)16);	// uv

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);

	glBindVertexArray(0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	// vao_fog_
	glGenVertexArrays(1, &vao_fog_);
	glBindVertexArray(vao_fog_);

	glBindBuffer(GL_ARRAY_BUFFER, vbo_);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vert_pos_a_uv_s), (const void*)0);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);

	glBindVertexArray(0);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	return true;
}

void Renderer_Terrain::Shutdown() {
	UnloadAllTexs();

	GL_DeleteVertexArray(vao_fog_);
	GL_DeleteVertexArray(vao_);

	GL_DeleteBuffer(ibo_);
	GL_DeleteBuffer(vbo_);

	GL_DeleteProgram(prg_wireframe_);
	GL_DeleteProgram(prg_fog_);
	GL_DeleteProgram(prg_lmp_);
	GL_DeleteProgram(prg_mat_);
}

void Renderer_Terrain::UnloadAllTexs() {
	auto DeleteTexList = [](gl_tex_list_s & tex_list) {
		if (tex_list.num_tex_) {
			glDeleteTextures(tex_list.num_tex_, tex_list.texs_);
			tex_list.num_tex_ = 0;
		}
	};

	DeleteTexList(mat_tex_);
	DeleteTexList(lmp_tex_);
}

void Renderer_Terrain::LoadMatTex(const pic_s* pic) {
	GLuint t = GL_RegisterTexture(pic, GL_REPEAT, GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR, true);

	if (mat_tex_.num_tex_ >= MAX_TEX_IN_LIST) {
		Log(log_type_t::LOG_FATAL, __FILE__, __LINE__, "terrain material texture array overflow, please enlarge constant MAX_TEX_IN_LIST.\n");
	}
	else {
		mat_tex_.texs_[mat_tex_.num_tex_++] = t;
	}
}

void Renderer_Terrain::LoadLMPTex(const pic_s* pic) {
	
	GLuint t = GL_RegisterTexture(pic, GL_CLAMP_TO_EDGE, GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR, true);

	if (lmp_tex_.num_tex_ >= MAX_TEX_IN_LIST) {
		Log(log_type_t::LOG_FATAL, __FILE__, __LINE__, "terrain lightmap texture array overflow, please enlarge constant MAX_TEX_IN_LIST.\n");
	}
	else {
		lmp_tex_.texs_[lmp_tex_.num_tex_++] = t;
	}
}

vert_pos_a_uv_s* Renderer_Terrain::MapVB() {
	glBindBuffer(GL_ARRAY_BUFFER, vbo_);
	vert_pos_a_uv_s* vb = (vert_pos_a_uv_s*)glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	return vb;
}

void Renderer_Terrain::UnmapVB() {
	glBindBuffer(GL_ARRAY_BUFFER, vbo_);
	glUnmapBuffer(GL_ARRAY_BUFFER);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

uint32_t* Renderer_Terrain::MapIB() {
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
	uint32_t* ib = (uint32_t*)glMapBuffer(GL_ELEMENT_ARRAY_BUFFER, GL_WRITE_ONLY);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	return ib;
}

void Renderer_Terrain::UnmapIB() {
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
	glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

render_chunk_s* Renderer_Terrain::GetRenderChunckBuffer() {
	return render_chunks_;
}

void Renderer_Terrain::Draw(GLuint ubo_mats, GLuint ubo_fog, 
	bool overlay_wireframe, int draw_options, int num_render_chunk)
{
	glBindVertexArray(vao_);

	for (uint32_t cur_render_layer = 0; cur_render_layer < 3; ++cur_render_layer) {
		SetRenderState(cur_render_layer);

		if (cur_render_layer == 0) {
			glUseProgram(prg_mat_.program_);	// for layer 0 & 1

			if (!g_gl_info.support_version_45_) {
				glUniform1i(prg_mat_tex_loc_, 0);	// texture unit 0
			}
		}
		else if (cur_render_layer == 2) {
			glUseProgram(prg_lmp_.program_);

			if (!g_gl_info.support_version_45_) {
				glUniform1i(prg_lmp_tex_loc_, 0);	// texture unit 0
			}
		}

		glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo_mats);

		for (int i = 0; i < num_render_chunk; ++i) {
			const render_chunk_s* rc = render_chunks_ + i;
			if (rc->render_layer_ == cur_render_layer) {

				if (cur_render_layer < 2) {
					if (draw_options & DRAW_TERRAIN_OPT_MAT) {
						GL_BindTexture2D(0, mat_tex_.texs_[rc->tex_idx_]);
						DrawRenderChunk(rc);
					}
				}
				else {
					if (draw_options & DRAW_TERRAIN_OPT_LMP) {
						GL_BindTexture2D(0, lmp_tex_.texs_[rc->tex_idx_]);
						DrawRenderChunk(rc);
					}
				}
				
			}
		}
	}

	// fog
	if (draw_options & DRAW_TERRAIN_OPT_FOG) {

		glBindVertexArray(vao_fog_);

		glUseProgram(prg_fog_.program_);

		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LEQUAL);
		glDepthMask(GL_FALSE);

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo_mats);
		glBindBufferBase(GL_UNIFORM_BUFFER, 1, ubo_fog);

		DrawLayer0RenderChunks(num_render_chunk);

		glDisable(GL_BLEND);
	}

	if (draw_options /* at least one option enabed */ && overlay_wireframe) {
		if (draw_options & DRAW_TERRAIN_OPT_FOG) {
			glBindVertexArray(vao_);
		}

		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LEQUAL);
		glDepthMask(GL_FALSE);

		// overlay wireframe mesh
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		glEnable(GL_POLYGON_OFFSET_LINE);

		glUseProgram(prg_wireframe_.program_);

		glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo_mats);

		DrawLayer0RenderChunks(num_render_chunk);

		glDisable(GL_POLYGON_OFFSET_LINE);
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	}

	glBindVertexArray(0);
}

void Renderer_Terrain::SetRenderState(uint32_t render_layer) const {
	if (render_layer == 0) {
		// base texture
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LESS);
		glDepthMask(GL_TRUE);

		glDisable(GL_BLEND);
	}
	else if (render_layer == 1) {
		// overlay texture
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LEQUAL);
		glDepthMask(GL_FALSE);

		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE);
	}
	else {
		// light map
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LEQUAL);
		glDepthMask(GL_FALSE);

		glEnable(GL_BLEND);
		glBlendFunc(GL_ZERO, GL_SRC_COLOR);
	}
}

void Renderer_Terrain::DrawLayer0RenderChunks(int num_render_chunk) const {
	for (int i = 0; i < num_render_chunk; ++i) {
		const render_chunk_s* rc = render_chunks_ + i;
		if (rc->render_layer_ == 0) {
			DrawRenderChunk(rc);
		}
	}
}

void Renderer_Terrain::DrawRenderChunk(const render_chunk_s* rc) const {
	for (int i = 0; i < rc->num_render_triangle_; ++i) {
		const render_triangles_s* rt = rc->render_triangles_ + i;
		glDrawElements(GL_TRIANGLES, rt->num_triangle_ * 3, GL_UNSIGNED_INT,
			(const void*)(rt->first_vert_idx_ * sizeof(uint32_t)));
	}
}
