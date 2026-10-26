/******************************************************************************
 * @file    terrain_fog.frag
 * @brief   fragment shader to apply fog
 *****************************************************************************/

#version 120

// GLSL 1.20 port of shaders/41/terrain_fog.frag
// The ub_fog uniform block is replaced by two plain file-scope uniforms of the
// same name/type/order (no UBO support on a legacy 2.1 context). The Lead
// looks these up by name with glGetUniformLocation.

uniform vec4	g_fog_color;
uniform float	g_fog_far;

varying vec3  vs_out_pos_in_view;

const float FOG_NEAR_FACTOR = 0.333333;	// tune this

float calc_fog_blend_alpha() {
	float distance = length(vs_out_pos_in_view);

	float fog_near = g_fog_far * FOG_NEAR_FACTOR;

	float visibility = (g_fog_far - distance) / (g_fog_far - fog_near);
	
	visibility = clamp(visibility, 0.0, 1.0);
	float fog_a =  (1.0 - visibility);

	return fog_a;
}

void main() {
	float fog_a = calc_fog_blend_alpha();
	gl_FragColor = vec4(g_fog_color.xyz, fog_a);
}
