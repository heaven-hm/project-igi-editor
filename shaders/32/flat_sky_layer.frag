/******************************************************************************
 * @file    flat_sky_layer.frag
 * @brief   fragment shader of flat sky layer (GL 3.3 Core - Wine/Mac/Linux compatible)
 *****************************************************************************/

#version 330 core

uniform sampler2D g_tex;

layout (location = 0) noperspective in  vec3 ps_interpolated_uvw;
layout (location = 1) in  vec4 ps_in_clr;

layout (location = 0) out vec4 ps_out_clr;

void main() {
	vec2 corrected_uv = ps_interpolated_uvw.xy / ps_interpolated_uvw.z;

	vec4 tex_color = texture(g_tex, corrected_uv);

	// modulate rgb & a
	ps_out_clr = tex_color * ps_in_clr;
}
