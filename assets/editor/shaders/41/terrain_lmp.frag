/******************************************************************************
 * @file    terrain_lmp.frag
 * @brief   fragment shader of terrain light map
 *****************************************************************************/

#version 410 core

uniform sampler2D g_tex;

layout (location = 0) in  vec3  vs_out_pos_in_view;
layout (location = 1) in  float vs_out_tex_alpha;   // not used
layout (location = 2) in  vec2  vs_out_uv;

layout (location = 0) out vec4 ps_out_color;

void main() {
	vec4 tex_color = texture(g_tex, vs_out_uv);

	// avoid the surface get too dark
	float f = 0.333333 + 0.666667 * tex_color.x;

	ps_out_color = vec4(f);
}
