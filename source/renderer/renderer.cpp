/******************************************************************************
 * @file    renderer.cpp
 * @brief   main renderer
 *   GL 4.1: need manually set binding point of uniform blocks
 *                             texture unit of sample object
 *   GL 4.5: binding point, texture unit can specified in shaders
 *****************************************************************************/

#include "pch.h"

/*
================================================================================
 Renderer
================================================================================
*/
Renderer::Renderer():
	ubo_mats_(0),
	ubo_fog_(0)
{
	//
}

Renderer::~Renderer() {
	Shutdown();
}

bool Renderer::Init() {
	ubo_mats_ = GL_CreateBuffer(GL_UNIFORM_BUFFER, sizeof(ubo_mats_s), nullptr, GL_DYNAMIC_DRAW);
	ubo_fog_ = GL_CreateBuffer(GL_UNIFORM_BUFFER, sizeof(ubo_fog_s), nullptr, GL_STATIC_DRAW);

	if (!skydome_.Init()) {
		return false;
	}

	if (!flat_sky_layers_.Init()) {
		return false;
	}

	if (!terrain_.Init()) {
		return false;
	}

	// init default state
	glClearColor(0.1f, 0.1f, 0.1f, 1.0f);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_TRUE);
	glDepthRangef(RENDER_DEPTH_MIN, RENDER_DEPTH_MAX);

	glEnable(GL_CULL_FACE);
	glFrontFace(GL_CCW);
	glCullFace(GL_BACK);

#if defined(_WIN32)
	glPolygonOffset(-1.0f, -1.0f);
#else
	glPolygonOffset(-64.0f, -64.0f);	// tune this
#endif
	glDisable(GL_POLYGON_OFFSET_LINE);

	GL_TryEnableVSync();

	return true;
}

void Renderer::Shutdown() {
	terrain_.Shutdown();
	flat_sky_layers_.Shutdown();
	skydome_.Shutdown();

	GL_DeleteBuffer(ubo_fog_);
	GL_DeleteBuffer(ubo_mats_);
}

void Renderer::BeginLoadLevel() {
	flat_sky_layers_.UnloadAllTexs();
	terrain_.UnloadAllTexs();
}

void Renderer::SetupClearColor(const glm::vec4& color) {
	glClearColor(color.r, color.g, color.b, 1.0f);
}

void Renderer::SetupFog(const glm::vec4& color, float fog_far) {
	ubo_fog_s ubo_fog;

	ubo_fog.color_ = color;
	ubo_fog.far_ = fog_far;

	GL_BufferData(ubo_fog_, GL_UNIFORM_BUFFER, sizeof(ubo_fog), &ubo_fog, GL_STATIC_DRAW);
}

void Renderer::SetupSkydome(const skydome_define_s& d) {
	skydome_.UpdateVertices(d);
}

void Renderer::LoadFlatSkyLayerTex(int layer_no, const pic_s* pic) {
	flat_sky_layers_.LoadLayerTex(layer_no, pic);
}

void Renderer::LoadTerrainMatTex(const pic_s* pic) {
	terrain_.LoadMatTex(pic);
}

void Renderer::LoadTerrainLMPTex(const pic_s* pic) {
	terrain_.LoadLMPTex(pic);
}

vert_flat_sky_layer_s* Renderer::MapFlatSkyLayersVB() {
	return flat_sky_layers_.MapVB();
}

void Renderer::UnmapFlatSkyLayersVB() {
	flat_sky_layers_.UnmapVB();
}

vert_pos_a_uv_s* Renderer::MapTerrainVB() {
	return terrain_.MapVB();
}

void Renderer::UnmapTerrainVB() {
	terrain_.UnmapVB();
}

uint32_t* Renderer::MapTerrainIB() {
	return terrain_.MapIB();
}

void Renderer::UnmapTerrainIB() {
	terrain_.UnmapIB();
}

render_chunk_s* Renderer::GetTerrainRenderChunckBuffer() {
	return terrain_.GetRenderChunckBuffer();
}

void Renderer::Draw(const draw_params_s& params) {
	SetupUBOMats(*params.view_define_);

	// start draw
	glDepthMask(GL_TRUE);	// insure clear depth buffer
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glViewport(0, 0, params.view_define_->viewport_width_, params.view_define_->viewport_height_);

	if (params.draw_parts_ & DRAW_SKYDOME) {
		skydome_.Draw(ubo_mats_, params.overlay_wireframe_);
	}

	if ((params.draw_parts_ & DRAW_FLAT_SKY_LAYER) && params.flat_sky_layer_is_visible_) {
		flat_sky_layers_.Draw(ubo_mats_, params.overlay_wireframe_);
	}

	if (params.draw_parts_ & DRAW_TERRAIN) {
		terrain_.Draw(ubo_mats_, ubo_fog_, params.overlay_wireframe_, params.draw_terrain_options_, params.num_terrain_render_chunk_);
	}

	GL_CHECK_ERROR;

	glFlush();
}

void Renderer::SetupUBOMats(const view_define_s& vd) {
	glm::mat4 mat_proj_persp = glm::perspective(vd.fovy_, (float)vd.viewport_width_ / vd.viewport_height_, vd.render_z_near_, vd.render_z_far_);

	glm::vec3 scaled_down_pos = vd.pos_ * RENDERER_MODEL_SCALE_DOWN;
	glm::mat4 mat_view = glm::lookAt(scaled_down_pos, scaled_down_pos + vd.forward_ * WORLD_UNITS_PER_METER, vd.up_);
	glm::mat4 mat_follow_view = glm::lookAt(VEC3_ORIGIN, vd.forward_, vd.up_);
	glm::mat4 mat_scale = glm::scale(glm::mat4(1.0f), glm::vec3(RENDERER_MODEL_SCALE_DOWN));

	// setup uniform buffer
	ubo_mats_s ubo_mats;

	// skydome
	ubo_mats.mvp_mat_follow_view_ = mat_proj_persp * mat_follow_view * mat_scale;

	// flat sky layer
	ubo_mats.mvp_flat_sky_layer_ = glm::ortho(0.0f, (float)vd.viewport_width_, 0.0f, (float)vd.viewport_height_, -1.0f, 1.0f);

	// terrain
	ubo_mats.mvp_objects_ = mat_proj_persp * mat_view * mat_scale;

	// flush ubo buffer
	GL_BufferData(ubo_mats_, GL_UNIFORM_BUFFER, sizeof(ubo_mats), &ubo_mats, GL_DYNAMIC_DRAW);
}
