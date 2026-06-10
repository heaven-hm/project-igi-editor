/******************************************************************************
 * @file    terrain_fog.frag
 * @brief   fragment shader to apply fog (GLSL 1.20 / OpenGL 2.1 legacy)
 *****************************************************************************/

#version 120

uniform vec4  g_fog_color;
uniform float g_fog_far;

varying vec3 vs_out_pos_in_view;

const float FOG_NEAR_FACTOR = 0.333333;

float calc_fog_blend_alpha() {
    float distance = length(vs_out_pos_in_view);
    float fog_near = g_fog_far * FOG_NEAR_FACTOR;
    float visibility = (g_fog_far - distance) / (g_fog_far - fog_near);
    visibility = clamp(visibility, 0.0, 1.0);
    return 1.0 - visibility;
}

void main() {
    float fog_a = calc_fog_blend_alpha();
    gl_FragColor = vec4(g_fog_color.xyz, fog_a);
}
