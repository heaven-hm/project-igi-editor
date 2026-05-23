/******************************************************************************
 * @file    skydome.frag
 * @brief   fragment shader of skydome
 *****************************************************************************/

#version 410 core

layout (location = 0) in vec3 ps_in_clr;

layout (location = 0) out vec4 ps_out_color;

void main() {
	ps_out_color = vec4(ps_in_clr, 1.0);
}
