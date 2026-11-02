/******************************************************************************
 * @file    flat_sky_layer.vert
 * @brief   vertex shader of flat sky layer
 *****************************************************************************/

#version 120

// GLSL 1.20 port of shaders/41/flat_sky_layer.vert
// The ub_mats uniform block is replaced by three plain file-scope uniforms of
// the same name/type/order (no UBO support on a legacy 2.1 context). The Lead
// looks these up by name with glGetUniformLocation.

uniform mat4 g_mvp_mat_follow_view;
uniform mat4 g_mvp_flat_sky_layer;
uniform mat4 g_mvp_objects;

// Attribute locations are bound from C++ with glBindAttribLocation (1.20 has
// no `layout (location = N)`); the original indices are kept:
attribute vec4 vs_in_pos;	// screen_x, screen_y, z, RHW -- location = 0
attribute vec2 vs_in_texcoord;	// location = 1
attribute vec4 vs_in_clr;	// location = 2

// BEHAVIOURAL DIVERGENCE: the 410 original declares vs_interpolated_uvw with
// the `noperspective` qualifier, which does not exist in GLSL 1.20. The
// qualifier is dropped, so this varying is interpolated smoothly (default
// perspective-correct interpolation) instead of screen-linearly. The
// flat-sky-layer texture coordinates therefore no longer reproduce the
// perspective-corrected interpolation of the 410 path. The per-vertex divide
// by RHW below is unchanged.
varying vec3 vs_interpolated_uvw;
varying vec4 vs_out_clr;

void main() {
	gl_Position = g_mvp_flat_sky_layer * vec4(vs_in_pos.x, vs_in_pos.y, 0.0, 1.0);

	float rhw = vs_in_pos.w;	// RHW comes from old API Direct3D7

	// since we are using orthographic projection
	// we need to calculate perspecitve correct texture coordinates
	// manually divied by W (not gl_Position.w but passed RHW value)
	vs_interpolated_uvw.xy = vs_in_texcoord.xy * rhw;

    vs_interpolated_uvw.z = rhw;

	vs_out_clr = vs_in_clr;
}
