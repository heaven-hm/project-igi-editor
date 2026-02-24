/******************************************************************************
 * @file    renderer_flat_sky_layers.cpp
 * @brief   flat sky layers renderer
 *****************************************************************************/

#include "pch.h"

/*
================================================================================
 Renderer_FlatSkyLayers
================================================================================
*/
Renderer_FlatSkyLayers::Renderer_FlatSkyLayers():
	vbo_(0),
	tex_loc_(-1)
{
	// do nothing
}

Renderer_FlatSkyLayers::~Renderer_FlatSkyLayers() {
	// do nothing
}

bool Renderer_FlatSkyLayers::Init() {
	if (!GL_CreateProgramVSFS("flat_sky_layer.vert", "flat_sky_layer.frag", prg_solid_)) {
		return false;
	}

	if (!GL_CreateProgramVSFS("flat_sky_layer_wireframe.vert", "flat_sky_layer_wireframe.frag", prg_wireframe_)) {
		return false;
	}

	if (!g_gl_info.support_version_45_) {
		// set binding points

		if (!GL_SetUniformBlockBinding(prg_solid_, "ub_mats", 0)) {
			return false;
		}

		if (!GL_SetUniformBlockBinding(prg_wireframe_, "ub_mats", 0)) {
			return false;
		}

		tex_loc_ = glGetUniformLocation(prg_solid_.program_, "g_tex");
		if (tex_loc_ == -1) {
			return false;
		}
	}

	vbo_ = GL_CreateBuffer(GL_ARRAY_BUFFER, sizeof(vert_flat_sky_layer_s) * NUM_VERTEX_PER_FLAT_SKY_LAYER * MAX_FLAT_SKY_LAYERS, nullptr, GL_DYNAMIC_DRAW);

	for (int i = 0; i < MAX_FLAT_SKY_LAYERS; ++i) {
		if (!layers_[i].Init(i, vbo_, NUM_VERTEX_PER_FLAT_SKY_LAYER)) {
			return false;
		}
	}

	return true;
}

void Renderer_FlatSkyLayers::Shutdown() {
	for (int i = 0; i < MAX_FLAT_SKY_LAYERS; ++i) {
		layers_[i].Shutdown();
	}

	GL_DeleteBuffer(vbo_);

	GL_DeleteProgram(prg_wireframe_);
	GL_DeleteProgram(prg_solid_);
}

void Renderer_FlatSkyLayers::UnloadAllTexs() {
	for (int i = 0; i < MAX_FLAT_SKY_LAYERS; ++i) {
		layers_[i].UnloadTex();
	}
}

void Renderer_FlatSkyLayers::LoadLayerTex(int layer_no, const pic_s* pic) {
	// no range check
	layers_[layer_no].LoadTex(pic);
}

vert_flat_sky_layer_s* Renderer_FlatSkyLayers::MapVB() {
	glBindBuffer(GL_ARRAY_BUFFER, vbo_);
	vert_flat_sky_layer_s * vb = (vert_flat_sky_layer_s*)glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	return vb;
}

void Renderer_FlatSkyLayers::UnmapVB() {
	glBindBuffer(GL_ARRAY_BUFFER, vbo_);
	glUnmapBuffer(GL_ARRAY_BUFFER);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void Renderer_FlatSkyLayers::Draw(GLuint ubo_mats, bool overlay_wireframe) {
	glDisable(GL_CULL_FACE);

	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);	// do not write z

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glUseProgram(prg_solid_.program_);

	if (!g_gl_info.support_version_45_) {
		glUniform1i(tex_loc_, 0);	// bind texture unit 0
	}

	glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo_mats);

	for (int i = 0; i < MAX_FLAT_SKY_LAYERS; ++i) {
		layers_[i].Draw(false);
	}

	glDisable(GL_BLEND);

	if (overlay_wireframe) {
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		glEnable(GL_POLYGON_OFFSET_LINE);

		glUseProgram(prg_wireframe_.program_);
		glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo_mats);

		for (int i = 0; i < MAX_FLAT_SKY_LAYERS; ++i) {
			layers_[i].Draw(true);
		}

		glDisable(GL_POLYGON_OFFSET_LINE);
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	}

	glDepthMask(GL_TRUE);
	glEnable(GL_DEPTH_TEST);

	glEnable(GL_CULL_FACE);
}
