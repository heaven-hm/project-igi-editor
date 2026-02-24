/******************************************************************************
 * @file    skydome_wireframe.frag
 * @brief   fragment shader to draw skydome in wireframe mode
 *****************************************************************************/

#version 450 core

layout (location = 0) in vec3 ps_in_clr;

layout (location = 0) out vec4 ps_out_color;

void main() {
	ps_out_color = vec4(0.1, 0.1, 0.1, 1.0);
}
