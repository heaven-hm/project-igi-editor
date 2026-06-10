/******************************************************************************
 * @file    skydome_wireframe.frag
 * @brief   fragment shader to draw skydome in wireframe mode (GL 3.3 Core - Wine/Mac/Linux compatible)
 *****************************************************************************/

#version 330 core

layout (location = 0) in vec3 ps_in_clr;

layout (location = 0) out vec4 ps_out_color;

void main() {
	ps_out_color = vec4(0.1, 0.1, 0.1, 1.0);
}
