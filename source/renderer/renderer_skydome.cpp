/******************************************************************************
 * @file    renderer_skydome.cpp
 * @brief   skydome renderer
 *****************************************************************************/

#include "pch.h"

/*
================================================================================
 Renderer_Skydome
================================================================================
*/
Renderer_Skydome::Renderer_Skydome():
	vbo_(0),
	ibo_(0),
	vao_(0)
{
	// do nothing
}

Renderer_Skydome::~Renderer_Skydome() {
	// do nothing
}

bool Renderer_Skydome::Init() {
	if (!GL_CreateProgramVSFS("skydome.vert", "skydome.frag", prg_solid_)) {
		return false;
	}

	if (!GL_CreateProgramVSFS("skydome.vert", "skydome_wireframe.frag", prg_wireframe_)) {
		return false;
	}

	if (!g_gl_info.support_version_45_) {

		if (!GL_SetUniformBlockBinding(prg_solid_, "ub_mats", 0)) {
			return false;
		}

		if (!GL_SetUniformBlockBinding(prg_wireframe_, "ub_mats", 0)) {
			return false;
		}

	}

	vbo_ = GL_CreateBuffer(GL_ARRAY_BUFFER, sizeof(vert_pos_rgb_s) * 321, nullptr, GL_STATIC_DRAW);

	uint32_t indices[INDEX_COUNT];
	uint32_t* pi = indices;
	uint32_t j = 19;

	// triangle fan
	for (uint32_t i = 0; i < 20; ++i) {
		pi[0] = 0;
		pi[1] = i + 1;
		pi[2] = j + 1;
		j = i;

		pi += 3;
	}

	// triangle strip
	for (uint32_t i = 0; i < 15; ++i) {

		uint32_t k = 19;

		for (uint32_t j = 0; j < 20; ++j) {

			/*
			 1--0
			 | /|
			 3--2
			 */

			uint32_t idx0 = 1 + 20 * i + k;
			uint32_t idx1 = 1 + 20 * i + j;

			uint32_t idx2 = idx0 + 20;
			uint32_t idx3 = idx1 + 20;

			k = j;

			pi[0] = idx0;
			pi[1] = idx1;
			pi[2] = idx3;

			pi += 3;

			pi[0] = idx3;
			pi[1] = idx2;
			pi[2] = idx0;

			pi += 3;
		}
	}

	ibo_ = GL_CreateBuffer(GL_ELEMENT_ARRAY_BUFFER, sizeof(uint32_t) * INDEX_COUNT, indices, GL_STATIC_DRAW);

	// create vao
	glGenVertexArrays(1, &vao_);
	glBindVertexArray(vao_);

	glBindBuffer(GL_ARRAY_BUFFER, vbo_);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vert_pos_rgb_s), (const void*)0);

	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(vert_pos_rgb_s), (const void*)12);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);

	glBindVertexArray(0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	return true;
}

void Renderer_Skydome::Shutdown() {
	GL_DeleteVertexArray(vao_);

	GL_DeleteBuffer(ibo_);
	GL_DeleteBuffer(vbo_);

	GL_DeleteProgram(prg_wireframe_);
	GL_DeleteProgram(prg_solid_);
}

void Renderer_Skydome::UpdateVertices(const skydome_define_s& d) {
	last_sd_ = d;
	BakeVertices();
}

void Renderer_Skydome::SetFogColor(const glm::vec3& fog_color) {
	fog_color_ = fog_color;
	BakeVertices();
}

void Renderer_Skydome::BakeVertices() {
	const skydome_define_s& d = last_sd_;

	auto InterplateColor = [](
		const float clr1[3], const float clr2[3],
		const float clr3[3], const float clr4[3],
		float f1, float f2, glm::vec3 & ret) {
			for (int i = 0; i < 3; ++i) {

				float v1 = (clr2[i] - clr1[i]) * f1 + clr1[i];
				float v2 = (clr4[i] - clr3[i]) * f1 + clr3[i];
				ret[i] = (v2 - v1) * f2 + v1;
			}
		};

	// define the semisphere
	constexpr float RADIUS = 409600000.0f * 0.80000001f;
	constexpr int STACKS = 15;
	constexpr int SLICES = 20;

	glBindBuffer(GL_ARRAY_BUFFER, vbo_);
	vert_pos_rgb_s* vb = (vert_pos_rgb_s*)glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
	if (vb) {
		// zenith vertex — no fog (s=1 → fog_a=0)
		vb[0].pos_[0] = 0.0f;
		vb[0].pos_[1] = 0.0f;
		vb[0].pos_[2] = RADIUS;

		InterplateColor(d.top_color1_, d.top_color1_,
			d.top_color1_, d.top_color1_, 0.0f, 0.0f, vb[0].color_);

		float pitch = glm::half_pi<float>();	// 90 degrees
		constexpr float pitch_step = glm::half_pi<float>() / STACKS;

		float t1 = 0.0f;

		for (int i = 0; i < STACKS; ++i) {
			pitch -= pitch_step;

			float c = (float)std::cos(pitch);
			float s = (float)std::sin(pitch);

			float t = 0.0f;
			const float* start_color = nullptr;

			if (pitch < d.angle_) {
				float half_skydome_angle_in_rad = d.angle_ * 0.5f;
				start_color = d.middle_color1_;

				t = pitch < half_skydome_angle_in_rad ? 0.0f : pitch / half_skydome_angle_in_rad - 1.0f;
			}
			else {
				start_color = d.top_color1_;

				t = (pitch - d.angle_) / (glm::half_pi<float>() - d.angle_);
			}

			float f2 = 1.0f - t;

			// Atmospheric fog — physically, looking near the horizon means looking
			// through much more atmosphere than looking straight up. Use a power-law
			// on (1 - sin(pitch)) so the fog fades smoothly from full at the horizon
			// to zero at the zenith without hard edges at any angle.
			float fog_a = glm::pow(glm::max(0.0f, 1.0f - s), 1.5f);

			float yaw = 0.0f;
			constexpr float yaw_step = glm::two_pi<float>() / SLICES;

			for (int j = 0; j < SLICES; ++j) {
				int v_idx = 1 + i * SLICES + j;

				vb[v_idx].pos_[0] = (float)std::cos(yaw) * RADIUS * c;
				vb[v_idx].pos_[1] = (float)std::sin(yaw) * RADIUS * c;
				vb[v_idx].pos_[2] = RADIUS * s;

				float f1 = t1 >= 1.0f ? t1 - 1.0f : 1.0f - t1;

				glm::vec3 sky_col;
				InterplateColor(start_color, start_color + 3,
					start_color + 6, start_color + 9, f1, f2, sky_col);

				// Blend toward atmospheric fog color at horizon.
				vb[v_idx].color_ = glm::mix(sky_col, fog_color_, fog_a);

				yaw += yaw_step;
			}

			t1 += 0.1f;
		}

		// extrude downward — use full fog color at skirt (below horizon)
		for (int i = 0; i < SLICES; ++i) {
			int pri_v_idx = 1 + (STACKS-1) * SLICES + i;
			int v_idx = 1 + STACKS * SLICES + i;

			vb[v_idx] = vb[pri_v_idx];
			vb[v_idx].pos_[2] = RADIUS * -0.5f;
			vb[v_idx].color_ = fog_color_;
		}

		glUnmapBuffer(GL_ARRAY_BUFFER);
	}
	glBindBuffer(GL_ARRAY_BUFFER, 0);	// unbind
}

void Renderer_Skydome::Draw(GLuint ubo_mats, bool overlay_wireframe) {
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);

	glBindVertexArray(vao_);

	glUseProgram(prg_solid_.program_);
	glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo_mats);

	glDrawElements(GL_TRIANGLES, INDEX_COUNT, GL_UNSIGNED_INT, nullptr);
	
	if (overlay_wireframe) {
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		glEnable(GL_POLYGON_OFFSET_LINE);

		glUseProgram(prg_wireframe_.program_);
		glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo_mats);

		glDrawElements(GL_TRIANGLES, INDEX_COUNT, GL_UNSIGNED_INT, nullptr);

		glDisable(GL_POLYGON_OFFSET_LINE);
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	}
	
	glBindVertexArray(0);

	glDepthMask(GL_TRUE);
	glEnable(GL_DEPTH_TEST);
}
