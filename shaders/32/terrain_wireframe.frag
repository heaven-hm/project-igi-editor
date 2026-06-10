/******************************************************************************
 * @file    terrain_wireframe.frag
 * @brief   fragment shader of terrain in wireframe mode (GL 3.3 Core - Wine/Mac/Linux compatible)
 *****************************************************************************/

#version 330 core

layout (location = 0) in  vec3  vs_out_pos_in_view;
layout (location = 1) in  float vs_out_tex_alpha;
layout (location = 2) in  vec2  vs_out_uv;

layout (location = 0) out vec4  ps_out_color;

void main() {
	ps_out_color = vec4(0.1, 0.1, 0.1, 1.0);
}
