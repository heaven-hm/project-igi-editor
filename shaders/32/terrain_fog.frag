/******************************************************************************
 * @file    terrain_fog.frag
 * @brief   fragment shader to apply fog (GL 3.3 Core - Wine/Mac/Linux compatible)
 *
 * The engine's terrain fog is EXPONENTIAL, not a linear ramp, and the exponent is
 * its own gTerrainFogScale:
 *
 *     r = kOne / x87_exp(vz * gWorldScale * gTerrainFogScale);
 *     tl->specular = fistp(r * 255) << 24;      // the fog factor, in alpha
 *   -- td_project (inside the terrain D3D draw), src/glowmesh/terraind3d.c:164-167,
 *      with gWorldScale = 0.001f (src/d3d/direct3d.c:532) and
 *      gTerrainFogScale = authored "Fog density terrain" * 0.0005
 *      (src/animcam/globallight.c:115,125), clamped to 0..16384 by
 *      TerrainMesh_SetFog (src/glowmesh/terrainmesh.c:302-320).
 *
 * `vz` there is the view-space z in game units. Here the same quantity is
 * `1.0 / gl_FragCoord.w`: the vertex shader puts `g_mvp_objects * v4` into
 * gl_Position, so gl_Position.w is the view depth and gl_FragCoord.w is its
 * reciprocal, interpolated. `g_mvp_objects` is Proj * View * GlobalScale with
 * GlobalScale = RENDERER_MODEL_SCALE_DOWN = 0.001f (source/common.h:54) -- the
 * engine's own gWorldScale -- so this depth IS `vz * gWorldScale` and the engine's
 * exponent is exactly `depth * g_fog_scale` with g_fog_scale = gTerrainFogScale.
 * No unit conversion happens here on purpose; Renderer::ApplyFogIntensity writes
 * the coefficient, and the object pass (a different light set) uses its own
 * uniforms.
 *
 * This replaces a LINEAR ramp with an invented `FOG_NEAR_FACTOR = 0.333333` whose
 * two faults were (a) the ramp's near edge was a tuned constant with no oracle and
 * (b) the distance it compared against was `length(clip.xyz)` -- a clip-space
 * length, in a different unit from the world-unit `g_fog_far` it was handed, so
 * full-strength fog sat about 1000x beyond the far plane and the terrain was
 * effectively unfogged. Both faults are gone: the curve is the engine's and the
 * units agree.
 *****************************************************************************/

#version 330 core

uniform ub_fog {
	vec4	g_fog_color;
	float	g_fog_scale;
};

layout (location = 0) out vec4 ps_out_color;

float calc_fog_blend_alpha() {
	// View depth, in the units g_fog_scale is calibrated for (see above).
	float depth = 1.0 / gl_FragCoord.w;

	// terraind3d.c:164-166: 1 - 1/exp(depth * gTerrainFogScale), clamped at 0.
	float fog_a = 1.0 - exp(-depth * g_fog_scale);
	return clamp(fog_a, 0.0, 1.0);
}

void main() {
	ps_out_color = vec4(g_fog_color.xyz, calc_fog_blend_alpha());
}
