/******************************************************************************
 * @file    renderer_flat_sky_layer.cpp
 * @brief   flat sky layer renderer
 *****************************************************************************/

#include "pch.h"

/*
================================================================================
 Renderer_FlatSkyLayer
================================================================================
*/
Renderer_FlatSkyLayer::Renderer_FlatSkyLayer():
	vao_(0),
	vao_wireframe_(0),
	tex_(0)
{
	// do nothing
}

Renderer_FlatSkyLayer::~Renderer_FlatSkyLayer() {
	// do nothing
}

bool Renderer_FlatSkyLayer::Init(int layer_no, GLuint vbo, int vert_per_layer) {
	// create vao
	glGenVertexArrays(1, &vao_);
	glBindVertexArray(vao_);

	glBindBuffer(GL_ARRAY_BUFFER, vbo);

	int vert_offset = layer_no * sizeof(vert_flat_sky_layer_s) * vert_per_layer;
	const auto base_offset = static_cast<std::uintptr_t>(vert_offset);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(vert_flat_sky_layer_s), reinterpret_cast<const void*>(base_offset));

	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(vert_flat_sky_layer_s), reinterpret_cast<const void*>(base_offset + 16u));

	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(vert_flat_sky_layer_s), reinterpret_cast<const void*>(base_offset + 24u));

	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glGenVertexArrays(1, &vao_wireframe_);
	glBindVertexArray(vao_wireframe_);
	
	glBindBuffer(GL_ARRAY_BUFFER, vbo);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(vert_flat_sky_layer_s), reinterpret_cast<const void*>(base_offset));

	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	return true;
}

void Renderer_FlatSkyLayer::Shutdown() {
	UnloadTex();
	GL_DeleteVertexArray(vao_wireframe_);
	GL_DeleteVertexArray(vao_);
}

void Renderer_FlatSkyLayer::UnloadTex() {
	GL_DeleteTexture(tex_);
}

void Renderer_FlatSkyLayer::LoadTex(const pic_s* pic) {
	tex_ = GL_RegisterTexture(pic, GL_REPEAT, GL_LINEAR, GL_LINEAR, false);
}

void Renderer_FlatSkyLayer::Draw(bool wireframe_mode) {
	if (!wireframe_mode) {
		GL_BindTexture2D(0, tex_);
	}

	glBindVertexArray(wireframe_mode ? vao_wireframe_ : vao_);

	int tri_strip_count = NUM_VERTEX_PER_FLAT_SKY_LAYER / 3;
	for (int i = 0; i < tri_strip_count; ++i) {
		glDrawArrays(GL_TRIANGLE_STRIP, i * 3, 3);
	}

	glBindVertexArray(0);
}
