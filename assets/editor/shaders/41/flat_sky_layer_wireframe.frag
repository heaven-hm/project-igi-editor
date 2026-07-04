/******************************************************************************
 * @file    flat_sky_layer_wireframe.frag
 * @brief   fragment shader of flat sky layer in wireframe mode
 *****************************************************************************/

#version 410 core

layout (location = 0) out vec4 ps_out_clr;

void main() {
	ps_out_clr = vec4(0.1, 0.1, 0.1, 1.0);
}
