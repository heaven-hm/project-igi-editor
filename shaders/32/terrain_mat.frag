/******************************************************************************
 * @file    terrain_mat.frag
 * @brief   fragment shader of terrain material (GL 3.3 Core - Wine/Mac/Linux compatible)
 *****************************************************************************/

#version 330 core

uniform sampler2D g_tex;

layout (location = 0) in  vec3  vs_out_pos_in_view;
layout (location = 1) in  float vs_out_tex_alpha;
layout (location = 2) in  vec2  vs_out_uv;

layout (location = 0) out vec4 ps_out_color;

void main() {
	ps_out_color = texture(g_tex, vs_out_uv) * vs_out_tex_alpha;
}
