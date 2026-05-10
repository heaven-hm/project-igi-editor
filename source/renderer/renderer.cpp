/******************************************************************************
 * @file    renderer.cpp
 * @brief   main renderer
 *   GL 4.1: need manually set binding point of uniform blocks
 *                             texture unit of sample object
 *   GL 4.5: binding point, texture unit can specified in shaders
 *****************************************************************************/

#include "pch.h"
#include "logger.h"
#include <glm/gtc/type_ptr.hpp>


#include <freeglut.h>

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

	if (!objects_.Init()) {
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
	objects_.Shutdown();
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

void Renderer::Draw(const draw_params_s& params, const hud_params_s& hud) {
    static bool logged_params = false;
    if (!logged_params) {
        Logger::Get().Log(LogLevel::INFO, "[Renderer] Draw Params: draw_parts=" + std::to_string(params.draw_parts_) + " level_objects=" + (params.level_objects_ ? "VALID" : "NULL"));
        logged_params = true;
    }
    SetupUBOMats(*params.view_define_);


        // start draw
        glDepthMask(GL_TRUE);   // insure clear depth buffer
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

        if ((params.draw_parts_ & DRAW_OBJECTS) && params.level_objects_) {
                glMatrixMode(GL_PROJECTION);
                glLoadMatrixf(glm::value_ptr(mat_proj_));
                glMatrixMode(GL_MODELVIEW);
                glLoadMatrixf(glm::value_ptr(mat_view_));
                objects_.Draw(ubo_mats_, params.overlay_wireframe_, params.level_objects_->GetObjects(), params.selected_object_index_);
        }



        if (hud.show_hud_) {
                glUseProgram(0); // Disable any active shaders for fixed-function HUD
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, 0);
                GL_CHECK_ERROR;

                glDisable(GL_DEPTH_TEST);
                glDisable(GL_CULL_FACE);
                glDisable(GL_LIGHTING);
                glDisable(GL_TEXTURE_2D);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

                glMatrixMode(GL_PROJECTION);
                glPushMatrix();
                glLoadIdentity();
                glOrtho(0, params.view_define_->viewport_width_, 0, params.view_define_->viewport_height_, -1, 1);

                glMatrixMode(GL_MODELVIEW);
                glPushMatrix();
                glLoadIdentity();

                auto draw_text = [&](int x, int y, const char* str, float r, float g, float b) {
                        // Draw shadow
                        glColor3f(0.0f, 0.0f, 0.0f);
                        glRasterPos2i(x + 1, params.view_define_->viewport_height_ - y - 1);
                        for (const char* c = str; *c != '\0'; c++) {
                                glutBitmapCharacter(GLUT_BITMAP_8_BY_13, *c);
                        }
                        // Draw main text
                        glColor3f(r, g, b);
                        glRasterPos2i(x, params.view_define_->viewport_height_ - y);
                        for (const char* c = str; *c != '\0'; c++) {
                                glutBitmapCharacter(GLUT_BITMAP_8_BY_13, *c);
                        }
                };

                int line_y = 30;
                draw_text(20, line_y, "--- IGI TERRAIN EDITOR ---", 1.0f, 1.0f, 1.0f); line_y += 15;
                
                float status_r = 1.0f, status_g = 1.0f, status_b = 0.0f; // Yellow
                if (hud.status_msg_.find("CONNECTED") != std::string::npos) { status_r = 0.0f; status_g = 1.0f; status_b = 0.0f; } // Green
                draw_text(20, line_y, hud.status_msg_.c_str(), status_r, status_g, status_b); line_y += 15;

                // Always show Editor Position in NATIVE MODE or CONNECTED mode
                char buf[256];
                sprintf(buf, "EDITOR X: %.2f Y: %.2f Z: %.2f", hud.raw_pos_.x, hud.raw_pos_.y, hud.raw_pos_.z);
                draw_text(20, line_y, buf, 1.0f, 1.0f, 1.0f); line_y += 15;
                
                sprintf(buf, "POSITION X: %.3fm Y: %.3fm Z: %.3fm", hud.meters_pos_.x, hud.meters_pos_.y, hud.meters_pos_.z);
                draw_text(20, line_y, buf, 1.0f, 1.0f, 1.0f); line_y += 15;
                
                sprintf(buf, "GROUND DIST: %.3fm", hud.ground_offset_ / 4096.0f);
                draw_text(20, line_y, buf, 1.0f, 1.0f, 1.0f); line_y += 15;
                
                sprintf(buf, "YAW: %.3f PITCH: %.3f ROLL: %.3f", hud.cam_yaw_, hud.cam_pitch_, hud.cam_roll_);
                draw_text(20, line_y, buf, 0.5f, 1.0f, 0.5f); line_y += 15;
                
                sprintf(buf, "ANGLE H: %.3f V: %.3f", hud.view_h_, hud.view_v_);
                draw_text(20, line_y, buf, 1.0f, 0.5f, 0.0f); line_y += 15;

                sprintf(buf, "LEVEL: %d | FOV: %.1f", hud.game_level_, hud.cam_fov_);
                draw_text(20, line_y, buf, 0.7f, 0.7f, 1.0f); line_y += 15;
                
                draw_text(20, line_y, "Checks: 0", 1.0f, 1.0f, 1.0f);


                if (hud.pause_mode_) {
                        int menu_w = 200;
                        int menu_h = 160;

                        int menu_x = (params.view_define_->viewport_width_ - menu_w) / 2;
                        int menu_y = (params.view_define_->viewport_height_ - menu_h) / 2;

                        // Draw background
                        glColor4f(0.1f, 0.1f, 0.1f, 0.8f);
                        glBegin(GL_QUADS);
                        glVertex2i(menu_x, menu_y);
                        glVertex2i(menu_x + menu_w, menu_y);
                        glVertex2i(menu_x + menu_w, menu_y + menu_h);
                        glVertex2i(menu_x, menu_y + menu_h);
                        glEnd();

                        // Draw border
                        glColor3f(1.0f, 1.0f, 1.0f);
                        glBegin(GL_LINE_LOOP);
                        glVertex2i(menu_x, menu_y);
                        glVertex2i(menu_x + menu_w, menu_y);
                        glVertex2i(menu_x + menu_w, menu_y + menu_h);
                        glVertex2i(menu_x, menu_y + menu_h);
                        glEnd();

                        draw_text(menu_x + 60, params.view_define_->viewport_height_ - (menu_y + 120), "PAUSE MENU", 1.0f, 1.0f, 0.0f);
                        draw_text(menu_x + 30, params.view_define_->viewport_height_ - (menu_y + 90), "[ESC] RESUME", 1.0f, 1.0f, 1.0f);
                        draw_text(menu_x + 30, params.view_define_->viewport_height_ - (menu_y + 70), "[S]   SAVE LEVEL", 1.0f, 1.0f, 1.0f);
                        draw_text(menu_x + 30, params.view_define_->viewport_height_ - (menu_y + 50), "[=]   RESET LEVEL", 0.5f, 1.0f, 1.0f);
                        draw_text(menu_x + 30, params.view_define_->viewport_height_ - (menu_y + 30), "[R]   RESET SCRIPT", 1.0f, 0.7f, 0.3f);
                        draw_text(menu_x + 30, params.view_define_->viewport_height_ - (menu_y + 10), "[D]   DEBUG", 0.0f, 1.0f, 0.0f);
                        draw_text(menu_x + 30, params.view_define_->viewport_height_ - (menu_y - 10), "[Q]   EXIT", 1.0f, 0.5f, 0.5f);


                }

                if (hud.show_debug_) {
                        const auto& entries = Logger::Get().GetEntries();
                        int startY = 250;
                        int count = 0;
                        for (auto it = entries.rbegin(); it != entries.rend() && count < 15; ++it, ++count) {
                                float r = 1.0f, g = 1.0f, b = 1.0f;
                                if (it->level == LogLevel::ERR) { r = 1.0f; g = 0.2f; b = 0.2f; }
                                else if (it->level == LogLevel::FATAL) { r = 1.0f; g = 0.0f; b = 0.0f; }
                                else if (it->level == LogLevel::WARNING) { r = 1.0f; g = 1.0f; b = 0.0f; }

                                draw_text(10, startY - count * 15, it->message.c_str(), r, g, b);
                        }

                        // Display selected object info
                        if (hud.selected_object_index_ >= 0 && hud.level_objects_) {
                                const auto& objects = hud.level_objects_->GetObjects();
                                if (hud.selected_object_index_ < (int)objects.size()) {
                                        const auto& obj = objects[hud.selected_object_index_];
                                        char buf[512];
                                        snprintf(buf, sizeof(buf), "SELECTED: [%d] %s (%s)", hud.selected_object_index_, obj.name.c_str(), obj.modelId.c_str());
                                        draw_text(10, 50, buf, 1.0f, 1.0f, 0.0f);
                                        snprintf(buf, sizeof(buf), "Pos: (%.0f, %.0f, %.0f)", obj.pos.x, obj.pos.y, obj.pos.z);
                                        draw_text(10, 35, buf, 1.0f, 1.0f, 1.0f);
                                        snprintf(buf, sizeof(buf), "Rot (Alpha/Beta/Gamma): (%.2f, %.2f, %.2f)", obj.rot.x, obj.rot.y, obj.rot.z);
                                        draw_text(10, 20, buf, 1.0f, 1.0f, 1.0f);
                                        snprintf(buf, sizeof(buf), "Scale: %.2f", obj.scale);
                                        draw_text(10, 5, buf, 1.0f, 1.0f, 1.0f);
                                }
                        }
                }


                glMatrixMode(GL_PROJECTION);
                glPopMatrix();
                glMatrixMode(GL_MODELVIEW);
                glPopMatrix();

                // Restore all states
                glEnable(GL_DEPTH_TEST);
                glEnable(GL_CULL_FACE);
                glEnable(GL_LIGHTING);
                glEnable(GL_TEXTURE_2D);
                glDisable(GL_BLEND);
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

	mat_proj_ = mat_proj_persp;
	mat_view_ = mat_view;

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
