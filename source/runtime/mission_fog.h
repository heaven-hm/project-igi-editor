// mission_fog.h - per-mission authored fog, resolved the way IGI-RE resolves it.
//
// Issue #66 asked for fog "derived from the level's FlatSky task", citing
// open-igi's FogPresets.cs. The premise is not baseless: FlatSky's ParamTable
// really does declare "Fog Amount" (Real32, 0..1), "Z Pos" (Real32),
// "Distance" (Real32) and "Fog Color" (RGB) (src/flatwater/flatsky.c:89-92), and
// retail LEVEL1 authors 0.9417 / 4.0 / 100.0 / (0.2, 0.288462, 0.4) on its
// "TheSky" instance. But those four fields parameterise a flat-sky GROUND fog
// BAND, not the depth-based scene fog: FlatSky_Render (flatsky.c:126-128) reads
//   a = Distance * k4096;  b = ZPos * k4096;  ce = a - (a - kNear) * FogAmount;
// and builds a horizon quad from them. The scene fog is GlobalLight's instead:
//
//   * a GlobalLight fog colour + density per light set, authored in the level and
//     named "Fog color terrain" / "Fog density terrain" (globallight.c:265-285),
//     published as `density * kFogScale` (globallight.c:117-140),
//   * an exponential depth falloff in the terrain renderer
//     (`exp(-depth * gWorldScale * gTerrainFogScale)`, terraind3d.c:164), and
//   * a cheap bit-trick log2 falloff used for the terrain lightmap fog table and
//     for water, which is NOT a real log2 (src/dxinit/spline.c:13-20).
//
// This module implements the scene fog. FlatSky's ground band is a separate
// feature and is deliberately not implemented here.
//
// Everything here is cited to IGI-RE. Where the reconstructed engine is
// self-inconsistent or leaves a value undefined, the divergence is named in a
// comment and in docs/igi-re-parity/MISSION_FOG.md rather than silently fixed.
#pragma once

#include <string>
#include <vector>

namespace igi {

// ---------------------------------------------------------------------------
// Engine constants. Every one is IGI-RE DATA.
// ---------------------------------------------------------------------------

// publish(): `g->sets[0].color.fogDensity * kFogScale` (globallight.c:115,133).
// The authored "Fog density terrain" is a user-facing number; this converts it
// to the value the renderer stores in gTerrainFogScale.
constexpr float kGlobalLightFogScale = 0.0005f;

// gWorldScale, set once in src/d3d/direct3d.c:532. This is the factor the
// Lead's open question was missing: the exponential terrain fog is
// exp(-depth * kWorldScale * gTerrainFogScale), so the authored density does
// reach a visible range -- see ExponentialFogDistanceForFraction.
constexpr float kWorldScale = 0.001f;

// Fog_Compute's data constants (src/sector/light.c:172-175).
constexpr float kFogBias = 130.0f;
constexpr float kFogScaleOneOver65536 = 1.5258789e-05f;
constexpr float kFovBase = 0.17f;
constexpr float kFovScale = 0.83f;

// flatwater.c:421 multiplies the same expression by k2000 * k256th.
constexpr float kFlatWaterFogGain = 2000.0f * 0.00390625f;  // == 7.8125

// TerrainMesh_SetFog clamps gTerrainFogScale to 0..16384 (terrainmesh.c:302-320).
constexpr float kTerrainFogScaleMax = 16384.0f;

// GlobalLight carries five light sets; set 0 is "terrain" (globallight.c:266-268).
constexpr int kLightSetCount = 5;

// The OBJECT renderer reads a DIFFERENT set from the terrain's, and IGI-RE proves
// it by address arithmetic rather than by naming it:
//
//   publish() fills, for i = 0..4 (globallight.c:127-134),
//       gFogColors[i]    = s->color.fog          AT 0xB818E0, Vec3f[5]
//       gFogDensities[i] = s->color.fogDensity * kFogScale   AT 0xB46D00, float[5]
//
//   The object pass uses the plain globals
//       gFogColor   (float[3])  AT 0xB818EC   -- addr.c:1992, render/rendervars.h:18,
//                                                read by render.c:775-777 (set_fog) and
//                                                dxinit/polylist.c:243-244
//       gFogDensity (float)     AT 0xB46D04   -- addr.c:1971, dxinit/dxinitvars.h:8,
//                                                read by render.c:780, polylist.c:245 and
//                                                Fog_SetRange (d3drender.c:117-118)
//
//   0xB818E0 + 12 == 0xB818EC  (sizeof(Vec3f) == 12), so gFogColor IS gFogColors[1].
//   0xB46D00 +  4 == 0xB46D04  (sizeof(float) == 4),  so gFogDensity IS gFogDensities[1].
//
// So retail fogs OBJECTS with "object category 1", not with the terrain set.
// (The same aliasing trick identifies the object pass's D3D ambient material
// colour: `ALIAS(gLightColor, gAmbient, 12)` at src/nodereg/water.c:544 makes
// gLightColor == gAmbient[1], and polylist.c:249-250 sets D3DRENDERSTATE_AMBIENT
// from it -- which is why app_level.cpp reading "Ambient color object category 1"
// is the engine's rule and not a guess.)
constexpr int kObjectLightSet = 1;

// GlobalLightKeyframe declares 51 parameters ending in "Time" (globallight.c:264-282).
constexpr int kGlobalLightKeyframeParams = 51;

// One authored light set.
struct FogLightSet {
    float ambient[3] = {0.0f, 0.0f, 0.0f};
    float fog[3] = {0.0f, 0.0f, 0.0f};
    float density = 0.0f;   // the authored "Fog density <set>" value
    int link = 0;           // "Link setting <set>"
};

// One task as the editor's loader hands it over -- the same shape as
// igi::WeatherEffectObject, so it can be filled straight from
// LevelObject::{type,argTokens}. arg_tokens is the raw token vector, i.e.
// [0]=task id, [1]=type, [2]=name, [3]=first declared parameter.
struct FogTaskObject {
    std::string type;
    std::vector<std::string> arg_tokens;
};

struct MissionFog {
    bool authored = false;          // a GlobalLightKeyframe was found and parsed
    int keyframe_index = -1;        // index into the object list, for diagnostics
    int keyframe_count = 0;         // how many keyframes the level authors
    FogLightSet sets[kLightSetCount];
    float sky_color[3] = {0.0f, 0.0f, 0.0f};
    float water_ambient[3] = {0.0f, 0.0f, 0.0f};
    float water_color[3] = {1.0f, 1.0f, 1.0f};
    float time_seconds = 0.0f;

    // The terrain set after publish()'s conversion and TerrainMesh_SetFog's
    // clamp: what the engine actually stores in gTerrainFogScale
    // (globallight.c:133, terrainmesh.c:302-320). `sets[0].density` stays as
    // authored.
    float terrain_fog_scale() const {
        const float scale = sets[0].density * kGlobalLightFogScale;
        if (!(scale >= 0.0f)) return 0.0f;      // NaN or negative -> no fog
        return scale > kTerrainFogScaleMax ? kTerrainFogScaleMax : scale;
    }

    // The colour publish() hands to TerrainMesh_SetFog.
    const float* terrain_fog_color() const { return sets[0].fog; }

    // The object pass's fog, i.e. gFogColors[1] / gFogDensities[1] (kObjectLightSet).
    //
    // NO 16384 CLAMP HERE, deliberately: that ceiling is TerrainMesh_SetFog's
    // (terrainmesh.c:302-320) and it applies only to gTerrainFogScale. publish()
    // writes gFogDensities[i] as a bare product (globallight.c:134), and the only
    // consumers of gFogDensity are the D3D fog-density render state
    // (render.c:780, polylist.c:245) and Fog_SetRange's exponent (d3drender.c:117),
    // neither of which clamps. The negative/NaN guard is the caller's protection
    // against exp() of a negative coefficient, which would give transmittance > 1.
    float object_fog_scale() const {
        const float scale = sets[kObjectLightSet].density * kGlobalLightFogScale;
        return scale >= 0.0f ? scale : 0.0f;   // NaN or negative -> no fog
    }
    const float* object_fog_color() const { return sets[kObjectLightSet].fog; }
};

// Picks the keyframe the level's *default* (daytime) state comes from: the one
// with the smallest Time, matching Level::LoadFogInfo and app_level.cpp's
// Dirlight selection. The decompiler does not always emit keyframes in
// chronological order, so "the first one" is not a safe rule.
MissionFog ResolveMissionFog(const std::vector<FogTaskObject>& objects);

// ---------------------------------------------------------------------------
// Engine maths, each reproduced exactly
// ---------------------------------------------------------------------------

// Float_Log2 HOOK 0x499FF0 (src/dxinit/spline.c:13-20). This is NOT log2: it
// reinterprets the float's bits, returns ~8*log2(x), and returns 0 for x <= 1
// and for every negative input. The comparison is SIGNED in the engine, which is
// what makes negatives return 0 -- an unsigned compare would return a large
// value instead.
unsigned int FloatLog2(float value);

// terraind3d.c:164 -- `1 / exp(depth * gWorldScale * gTerrainFogScale)`.
// Returns the remaining light (transmittance): 1 = no fog, 0 = fully fogged.
// The engine stores `fistp(transmittance * 255)` in the vertex specular alpha.
float ExponentialFogTransmittance(float depth_units, float terrain_fog_scale);
// The blend factor the game actually shows: 1 - transmittance.
float ExponentialFogAmount(float depth_units, float terrain_fog_scale);

// The distance at which the exponential fog reaches `fraction` of full fog:
//   z = -ln(1 - fraction) / (kWorldScale * terrain_fog_scale)
// Returns 0 when the scale is 0 or the fraction is out of (0,1).
float ExponentialFogDistanceForFraction(float terrain_fog_scale, float fraction);

// The e-folding distance (fraction = 1 - 1/e = 0.6321), in game units. This is
// the natural "fog distance" of the engine's exponential and equals
// 2 000 000 / authored_density game units = 488.28125 / authored_density metres
// (kFixed = 4096 units/m). It is the quantity a linear-fog renderer needs in
// order to match, and it is where the editor's invented `120 / density` is wrong
// by a factor of 4.069 -- see docs/igi-re-parity/MISSION_FOG.md §5.
float ExponentialFogEFoldingDistance(float terrain_fog_scale);

// The editor's world scale: common.h:40 WORLD_UNITS_PER_METER = 4096.0f, which
// is the engine's own kFixed (src/math/mathvars.h:7). Kept here so the fog
// distance below can be derived without pulling an editor header into this
// host-portable module.
constexpr float kEditorWorldUnitsPerMetre = 4096.0f;

// The engine's e-folding distance per unit of AUTHORED density, in metres:
//   1 / (kWorldScale * kGlobalLightFogScale) = 2 000 000 world units, and
//   2 000 000 / 4096 units-per-metre          = 488.28125 m.
//
// The literal is deliberate and the static_assert below is the point: the
// "obvious" constexpr form 1.0f / (kWorldScale * kGlobalLightFogScale *
// kEditorWorldUnitsPerMetre) evaluates to 488.2812f -- one ulp LOW, because
// 0.001f * 0.0005f is not exactly 5e-7 -- and that ulp would move every level's
// fog distance in the last place. This value is a bridge to a linear renderer,
// not an equivalence: it is the engine's exponential sampled at its e-folding
// point. Matching retail exactly would need exponential fog in the shader.
constexpr float kEFoldingMetresPerAuthoredDensity = 488.28125f;
static_assert(kEFoldingMetresPerAuthoredDensity * kEditorWorldUnitsPerMetre == 2000000.0f,
              "the e-folding distance must be exactly 2 000 000 world units / 4096 per metre");

// The fog distance the editor's linear renderer is handed, in METRES, for an
// authored "Fog density terrain" value: the engine's e-folding distance,
// 488.28125 / density. Returns `fallback_metres` for a density that is 0
// (the engine's own "no fog at this keyframe" state -- every retail level
// authors a Time=100 night keyframe with density 0), negative, or NaN.
//
// The arithmetic is deliberately `(1.0f / density) * kEFolding...`: that is the
// expression Level::LoadFogInfo used before this module had a caller, so moving
// it here changes no value for any input.
float LinearFogFarMetresForAuthoredDensity(float authored_density, float fallback_metres);

// ---------------------------------------------------------------------------
// Fog_SetRange: how the engine fogs OBJECTS
// ---------------------------------------------------------------------------

// Fog_SetRange HOOK 0x49B2D0 (src/dxinit/d3drender.c:108-131), verbatim:
//
//     void Fog_SetRange(const Vec3d *pos, float radius)
//     {
//         long double px = pos->x - gTransContext.pos.x, py = ..., pz = ...;
//         long double d = gTransContext.matrix.m[8] * pz + m[7] * py + m[6] * px;
//         float n = d - radius;
//         float f = d + radius;
//         float a = 1 / exp(gWorldScale * n * gFogDensity);
//         long double bl = 1 / exp(gWorldScale * f * gFogDensity);
//         float b = bl;
//         long double diffl = bl - a;
//         if (eq(diffl, 0) || eq(a, 0)) { start = 0; end = 0; }
//         else {
//             long double t = (b * n - a * f) / diffl;
//             start = (t - (t - n) / a) * gWorldScale;
//             end = gWorldScale * t;
//         }
//         SET_RS(D3DRENDERSTATE_FOGSTART, f2u(start));
//         SET_RS(D3DRENDERSTATE_FOGEND, f2u(end));
//     }
//
// D3D7 could only fog with a linear ramp (render.c:781 sets FOGVERTEXMODE =
// D3DFOG_LINEAR), so the engine picks the ramp that passes through the
// exponential's own value at the near and far side of a sphere around the
// object's centre: start/end satisfy (end-n)/(end-start) == a and
// (end-f)/(end-start) == b. Solving gives end = (b*n - a*f)/(b-a) and
// start = end - (end-n)/a, which is exactly the code above.
//
// `depth_units` is the engine's `d`, the view-space z of the object's centre in
// GAME UNITS: m[6..8] is the view matrix's third row, so
// d == dot(forward, pos - camera). `radius` is the object's bounding radius in
// the same units.
//
// `fog_density` is gFogDensity -- the value publish() writes, i.e. the AUTHORED
// "Fog density object category 1" times kGlobalLightFogScale (globallight.c:115,
// 133-134) -- NOT the authored number. MissionFog::object_fog_scale() is that
// value. Passing the authored density inflates the exponent by 2000x.
//
// The returned start/end are in the units the engine's D3D fog consumes, which
// is game units * gWorldScale (the trailing `* gWorldScale` above). That is the
// SAME space as a GL `gl_Position.w` in this editor: RENDERER_MODEL_SCALE_DOWN
// (source/common.h:54) is 0.001f, the engine's gWorldScale, so the editor's
// clip-space w is game units * 0.001 too.
struct FogRange {
    float start = 0.0f;
    float end = 0.0f;
};

// NAMED DIVERGENCE (bounded by FogRangeIsDegenerate): the engine's `a`/`b` are x87
// `long double` (80-bit, exponent range ~1e-4932), so its `a == 0` branch needs an
// exponent of ~11356. A C++ `long double` is 64-bit on MSVC and on arm64 macOS,
// where the same reciprocal reaches 0 at an exponent of ~710 -- so on those
// platforms the engine's "no ramp" branch fires for shallower fog. Both are the
// same physical state (the near edge of the object's sphere is already fully
// fogged), `FogRangeIsDegenerate` rejects a non-finite span so no caller can divide
// by one, and the editor's object SHADER uses the exponential directly rather than
// this ramp.
FogRange FogSetRangeForDepth(float depth_units, float radius, float fog_density);

// Fog_SetRange's degenerate branch, exposed so the renderer can take the same
// "no ramp" path the engine does. True when the ramp collapses (a zero-length
// span, or a transmittance that has already reached 0).
bool FogRangeIsDegenerate(const FogRange& range);

// Fog_Compute HOOK 0x4DA790 (src/sector/light.c:55-72).
//   haze_amount is the accumulated HazeLayer density, i.e. the value Haze_GetFog
//   leaves in *out -- Fog_Compute calls it as its second statement, so the term
//   IS defined in IGI-RE (an earlier note here claimed it was not; it is written
//   by the callee, and with no visible HazeLayer -- which is every shipped level,
//   no OBJECTS.QVM references "Hazinglayers" -- it is 0). It is an explicit
//   argument because it is an input to the arithmetic, not because it is unknown.
//   fov is gTransContext.fov; the engine scales depth by fov*0.83 + 0.17 when
//   that factor is below 1.
// Returns the value the engine writes back to *out, clamped to 0..1.
float FogComputeAmount(float haze_amount, float depth_units, float scale, float fov);

// terrainmesh.c:282 -- the lightmap fog table entry, in 0..255.
float LightmapFogValue(float depth_units, float terrain_fog_scale);

// flatwater.c:421 -- the water fog factor, in 0..1, with the extra
// k2000 * k256th gain. This is the only one of the three log2 variants that
// reaches a visible range at the authored densities.
float FlatWaterFogAmount(float depth_units, float terrain_fog_scale);

// Vec3_FastLength HOOK 0x4B2A10 (src/file/vec.c:140-181):
//   max + (sqrt(2)-1)*mid + (sqrt(3)-sqrt(2))*min
// Exact on axis-aligned and equal-component vectors, an over-estimate otherwise.
float FastLength3(const float v[3]);

// ---------------------------------------------------------------------------
// HazeLayer fog boxes ("Hazinglayers", src/haze/haze.c). No retail level places
// one, so this is reconstructed from the decompiled engine but unexercised.
// ---------------------------------------------------------------------------

struct HazeBox {
    float density = 0.0f;
    float bottom = 0.0f;
    float top = 0.0f;
    float color[3] = {0.0f, 0.0f, 0.0f};
    bool visible = false;
};

// NO token parser is provided for HazeBox, deliberately. The engine's fields are
// known (HazeLayer, src/haze/haze.h:56-63, written by HazeLayer_Set /
// set_fields at haze.c:40-53), but HazeLayer_Open (haze.c:106-114) registers the
// task type with NO ParamTable, so IGI-RE does not reconstruct the authored
// parameter order, and no retail level declares a "Hazinglayers" task to read one
// from. A field-by-position parser here would be invented, so the caller must
// fill the struct from a layout it has actually verified. HazeGetFog below is
// fully reconstructed and is the part that matters for rendering.
//
// HazeLayer_Set also takes the box's min/max corners, but stores neither: they
// only place the DynCube_AddBox volume. They are therefore absent here.

// Haze_GetFog HOOK 0x4DA300 (src/haze/haze.c:174-239). `color` receives the
// density-weighted fog colour, and the return value is the accumulated density.
// Only boxes with visible == true participate (HazeLayer_Push/Enter maintain
// gHazeVisible).
//
// DIVERGENCE, named: the engine never initialises its colour weight `wsum`
// (haze.c:177 declared, :188 the asm, and the decompiler marks it as never set).
// This uses 0.0f, which
// makes the first contributing layer's colour exact and later ones a
// density-weighted average -- the evident intent. The engine's `else` branch is
// an `Error_Show` + infinite loop; here an unreachable geometry combination
// simply contributes nothing.
float HazeGetFog(const HazeBox* boxes, int count, const float eye[3], const float point[3],
                 float color[3]);

}  // namespace igi
