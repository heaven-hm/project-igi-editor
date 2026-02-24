/******************************************************************************
 * @file    terrain_fog.frag
 * @brief   fragment shader to apply fog
 *****************************************************************************/

#version 410 core

uniform ub_fog {
	vec4	g_fog_color;
	float	g_fog_far;
};

layout (location = 0) in  vec3  vs_out_pos_in_view;

layout (location = 0) out vec4 ps_out_color;

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
	ps_out_color = vec4(g_fog_color.xyz, fog_a);
}
