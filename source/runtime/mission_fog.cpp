#include "runtime/mission_fog.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>

namespace igi {
namespace {

// Parses one authored token. Returns fallback when the token is absent or is not
// a number, so a level that omits a trailing parameter still resolves.
float TokenFloat(const std::vector<std::string>& tokens, std::size_t index, float fallback = 0.0f) {
    if (index >= tokens.size()) return fallback;
    const std::string& token = tokens[index];
    if (token.empty()) return fallback;
    char* end = nullptr;
    const float value = std::strtof(token.c_str(), &end);
    if (end == token.c_str()) return fallback;
    return value;
}

int TokenInt(const std::vector<std::string>& tokens, std::size_t index, int fallback = 0) {
    if (index >= tokens.size()) return fallback;
    const std::string& token = tokens[index];
    if (token.empty()) return fallback;
    char* end = nullptr;
    const long value = std::strtol(token.c_str(), &end, 10);
    if (end == token.c_str()) return fallback;
    return static_cast<int>(value);
}

// Token layout: [0]=task id, [1]=type, [2]=name, [3]=declared parameter 0.
// So declared parameter k lives at token index k + 3. This is the same offset
// Level::LoadFogInfo documents for GlobalLightKeyframe and app_level.cpp uses
// for argTokens[10] == "Fog density terrain".
constexpr std::size_t kFirstParam = 3;

// The parameter index of each field, from the ParamTable order in
// GlobalLight_Open (globallight.c:264-282), which the shipped
// Task_DeclareParameters line repeats verbatim:
//   0      Link All Sliders
//   1..3   Ambient color terrain
//   4..6   Fog color terrain
//   7      Fog density terrain
//   8      Link setting terrain
//   then for i = 1..4: ambient 3, fog 3, density, link  (8 each)
//   41..43 Sky color, 44..46 Water ambient, 47..49 Water color, 50 Time
constexpr int kParamDensityTerrain = 7;
constexpr int kParamLinkTerrain = 8;
constexpr int kParamSkyColor = 41;

int ParamBaseForSet(int set) {
    // set 0 is the terrain set at parameter 1; set i >= 1 starts at 9 + (i-1)*8.
    return set == 0 ? 1 : 9 + (set - 1) * 8;
}

}  // namespace

unsigned int FloatLog2(float value) {
    // src/dxinit/spline.c:13-20. `bits` is an `int` in the engine and the
    // comparison is signed, so any negative float (sign bit set) returns 0.
    std::int32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    if (bits > 0x3F800000) {
        return static_cast<unsigned int>(static_cast<std::uint32_t>(bits - 0x3F800000) >> 20);
    }
    return 0u;
}

float ExponentialFogTransmittance(float depth_units, float terrain_fog_scale) {
    // terraind3d.c:164. The engine is `1 / exp(depth * gWorldScale * scale)` and
    // clamps the result up to 0 before scaling it into the specular alpha.
    const long double exponent =
        static_cast<long double>(depth_units) * kWorldScale * terrain_fog_scale;
    const long double transmittance = 1.0L / std::exp(exponent);
    if (!(transmittance >= 0.0L)) return 0.0f;
    if (transmittance > 1.0L) return 1.0f;
    return static_cast<float>(transmittance);
}

float ExponentialFogAmount(float depth_units, float terrain_fog_scale) {
    return 1.0f - ExponentialFogTransmittance(depth_units, terrain_fog_scale);
}

float ExponentialFogDistanceForFraction(float terrain_fog_scale, float fraction) {
    if (!(terrain_fog_scale > 0.0f)) return 0.0f;
    if (!(fraction > 0.0f) || !(fraction < 1.0f)) return 0.0f;
    const long double coefficient =
        static_cast<long double>(kWorldScale) * terrain_fog_scale;
    const long double distance = -std::log(1.0L - fraction) / coefficient;
    return static_cast<float>(distance);
}

float ExponentialFogEFoldingDistance(float terrain_fog_scale) {
    // 1 - 1/e.
    return ExponentialFogDistanceForFraction(terrain_fog_scale, 0.63212055882855767840f);
}

float LinearFogFarMetresForAuthoredDensity(float authored_density, float fallback_metres) {
    // See the header: kEFoldingMetresPerAuthoredDensity is the engine's e-folding
    // distance in metres per unit of authored density. The expression is the one
    // Level::LoadFogInfo used inline before this module had a caller, so it is
    // bit-for-bit the same result for every input.
    if (!(authored_density > 0.0f)) return fallback_metres;
    return (1.0f / authored_density) * kEFoldingMetresPerAuthoredDensity;
}

// Fog_SetRange HOOK 0x49B2D0 (src/dxinit/d3drender.c:108-131). See the header for
// the full transcription and for why the returned units are the ones a GL
// `gl_Position.w` carries in this editor.
//
// `fog_density` is gFogDensity, i.e. the value publish() writes --
// `authored "Fog density object category 1" * kFogScale` (globallight.c:115,133-134)
// -- NOT the authored number. Passing the authored density here inflates the
// exponent by 1/0.0005 = 2000x and collapses the ramp.
FogRange FogSetRangeForDepth(float depth_units, float radius, float fog_density) {
    const long double n = static_cast<long double>(depth_units) - radius;
    const long double f = static_cast<long double>(depth_units) + radius;
    // x87_exp is e^x (`fldl2e; fmulp; f2xm1; fscale`, d3drender.c:88-105), so these
    // two are the TRANSMITTANCE at the near and far side of the sphere.
    const long double a = 1.0L / std::exp(static_cast<long double>(kWorldScale) * n * fog_density);
    const long double b = 1.0L / std::exp(static_cast<long double>(kWorldScale) * f * fog_density);
    const long double diff = b - a;

    FogRange out;
    // `if (x87_eq(diffl, 0) || x87_eq(a, 0)) { start = 0; end = 0; }` -- the
    // engine's x87_eq is exact equality on the long double. On a 64-bit `long
    // double` (MSVC, arm64 macOS) `a` reaches 0 at an exponent of ~710 where x87
    // needs ~11356, so this branch fires for shallower fog there; both are "the
    // near edge is already fully fogged" states, and the object SHADER does not use
    // this ramp at all.
    if (diff == 0.0L || a == 0.0L) {
        out.start = 0.0f;
        out.end = 0.0f;
        return out;
    }
    const long double t = (b * n - a * f) / diff;
    out.start = static_cast<float>((t - (t - n) / a) * static_cast<long double>(kWorldScale));
    out.end = static_cast<float>(static_cast<long double>(kWorldScale) * t);
    return out;
}

bool FogRangeIsDegenerate(const FogRange& range) {
    // The engine's branch above: start == end == 0 means "no ramp". A non-finite or
    // inverted span would make the shader's division meaningless, so both are
    // reported as degenerate too rather than left for the caller to notice -- and
    // that is also what makes the long-double divergence above harmless: whichever
    // of the two branches a platform takes, a caller that checks this flag never
    // divides by an infinite span.
    if (!(range.start == range.start) || !(range.end == range.end)) return true;  // NaN
    if (!std::isfinite(range.start) || !std::isfinite(range.end)) return true;
    return !(range.end > range.start);
}

float FogComputeAmount(float haze_amount, float depth_units, float scale, float fov) {
    // src/sector/light.c:55-72.
    const float f = fov * kFovScale + kFovBase;
    float depth = depth_units;
    if (f < 1.0f) depth = f * depth;

    // `*out = *out * kFogScale` then the log2 term is added on top.
    const long double out = static_cast<long double>(haze_amount) * kFogScaleOneOver65536;
    const long double v = (static_cast<long double>(FloatLog2(depth)) - kFogBias) * scale *
                              kFogScaleOneOver65536 +
                          out;
    if (!(v >= 0.0L)) return 0.0f;
    if (v > 1.0L) return 1.0f;
    return static_cast<float>(v);
}

float LightmapFogValue(float depth_units, float terrain_fog_scale) {
    // terrainmesh.c:282: `e = (Float_Log2(d) - 130) * gTerrainFogScale`, clamped
    // to 0..255 -- an 8-bit lightmap value, not a 0..1 factor.
    const long double e =
        (static_cast<long double>(FloatLog2(depth_units)) - kFogBias) * terrain_fog_scale;
    if (!(e >= 0.0L)) return 0.0f;
    if (e > 255.0L) return 255.0f;
    return static_cast<float>(e);
}

float FlatWaterFogAmount(float depth_units, float terrain_fog_scale) {
    // flatwater.c:421: the same shape with an extra k2000 * k256th gain, and
    // clamped to 0..1 rather than 0..255.
    const long double f = (static_cast<long double>(FloatLog2(depth_units)) - kFogBias) *
                          terrain_fog_scale * kFlatWaterFogGain;
    if (!(f >= 0.0L)) return 0.0f;
    if (f > 1.0L) return 1.0f;
    return static_cast<float>(f);
}

float FastLength3(const float v[3]) {
    // src/file/vec.c:140-181.
    float x = v[0], y = v[1], z = v[2];
    if (!(x >= 0.0f)) x = -x;
    if (!(y >= 0.0f)) y = -y;
    if (!(z >= 0.0f)) z = -z;

    float max, mid, min;
    if (x > y && x > z) {
        max = x;
        if (y > z) { mid = y; min = z; } else { mid = z; min = y; }
    } else if (y > z) {
        max = y;
        if (x > z) { mid = x; min = z; } else { mid = z; min = x; }
    } else {
        max = z;
        if (x > y) { mid = x; min = y; } else { mid = y; min = x; }
    }
    // sqrt(2) - 1 and sqrt(3) - sqrt(2).
    return static_cast<float>(static_cast<long double>(min) * 0.31783724L +
                              static_cast<long double>(mid) * 0.41421357L +
                              static_cast<long double>(max));
}

MissionFog ResolveMissionFog(const std::vector<FogTaskObject>& objects) {
    MissionFog fog;

    // Counted separately: `fog` is replaced wholesale by the winning candidate,
    // so a count carried inside it would be reset on every improvement.
    int keyframeCount = 0;
    bool haveBest = false;
    float bestTime = 0.0f;

    for (std::size_t i = 0; i < objects.size(); ++i) {
        const FogTaskObject& object = objects[i];
        if (object.type != "GlobalLightKeyframe") continue;
        ++keyframeCount;

        // A keyframe needs at least the terrain fog fields and the Time tail.
        if (object.arg_tokens.size() < kFirstParam + kGlobalLightKeyframeParams) continue;

        const std::vector<std::string>& t = object.arg_tokens;
        const float time = TokenFloat(t, kFirstParam + 50, 0.0f);
        if (haveBest && !(time < bestTime)) continue;

        MissionFog candidate;
        candidate.authored = true;
        candidate.keyframe_index = static_cast<int>(i);
        candidate.time_seconds = time;

        for (int set = 0; set < kLightSetCount; ++set) {
            const int base = ParamBaseForSet(set);
            FogLightSet& out = candidate.sets[set];
            for (int c = 0; c < 3; ++c) {
                out.ambient[c] = TokenFloat(t, kFirstParam + base + c);
                out.fog[c] = TokenFloat(t, kFirstParam + base + 3 + c);
            }
            out.density = TokenFloat(t, kFirstParam + base + 6);
            out.link = TokenInt(t, kFirstParam + base + 7);
        }

        // "Link setting terrain" is parameter 8; the per-set link for the terrain
        // set is written there rather than inside the 1..3/4..6 block above.
        candidate.sets[0].link = TokenInt(t, kFirstParam + kParamLinkTerrain);
        candidate.sets[0].density = TokenFloat(t, kFirstParam + kParamDensityTerrain);

        // GlobalLightKeyframe_ParamsChanged HOOK 0x5282C0 (src/animcam/globallight.c:
        // 392-403) resolves the authored links BEFORE anything reads the sets:
        //
        //     for (n = 5; n; n--)
        //         for (i = 0; i < 5; i++) {
        //             int l = k->sets[i].link;
        //             if (l >= 0 && l < 5) k->sets[i].set.color = k->sets[l].set.color;
        //         }
        //
        // The whole table is swept FIVE times so that chains resolve (set 4 -> 3 ->
        // 2 -> 1 -> 0), and each copy carries ambient, fog AND fogDensity together
        // because they are one LightColor. A level that authors `link` and leaves
        // placeholder zeros in the linked set relies on this, so the resolved
        // colours -- not the raw tokens -- are what publish() reads
        // (globallight.c:127-134) and therefore what both the terrain fog (set 0)
        // and the object fog (set 1) must use.
        for (int pass = 0; pass < kLightSetCount; ++pass) {
            for (int set = 0; set < kLightSetCount; ++set) {
                const int link = candidate.sets[set].link;
                if (link >= 0 && link < kLightSetCount) {
                    candidate.sets[set].ambient[0] = candidate.sets[link].ambient[0];
                    candidate.sets[set].ambient[1] = candidate.sets[link].ambient[1];
                    candidate.sets[set].ambient[2] = candidate.sets[link].ambient[2];
                    candidate.sets[set].fog[0] = candidate.sets[link].fog[0];
                    candidate.sets[set].fog[1] = candidate.sets[link].fog[1];
                    candidate.sets[set].fog[2] = candidate.sets[link].fog[2];
                    candidate.sets[set].density = candidate.sets[link].density;
                }
            }
        }

        for (int c = 0; c < 3; ++c) {
            candidate.sky_color[c] = TokenFloat(t, kFirstParam + kParamSkyColor + c);
            candidate.water_ambient[c] = TokenFloat(t, kFirstParam + kParamSkyColor + 3 + c);
            candidate.water_color[c] = TokenFloat(t, kFirstParam + kParamSkyColor + 6 + c);
        }

        // The 0..16384 ceiling TerrainMesh_SetFog applies to gTerrainFogScale is
        // enforced by MissionFog::terrain_fog_scale(), so `density` stays as
        // authored and the authored value remains reportable.

        fog = candidate;
        bestTime = time;
        haveBest = true;
    }

    // Every keyframe was seen, whether or not one was usable, so the count is
    // meaningful even when `authored` stays false (the caller keeps whatever
    // default it had rather than being handed a silent black fog).
    fog.keyframe_count = keyframeCount;
    return fog;
}

float HazeGetFog(const HazeBox* boxes, int count, const float eye[3], const float point[3],
                 float color[3]) {
    // src/haze/haze.c:174-239.
    static const float kFade = 0x1.333334p-2f;  // 0.3

    float d[3] = {point[0] - eye[0], point[1] - eye[1], point[2] - eye[2]};
    color[0] = 0.0f;
    color[1] = 0.0f;
    color[2] = 0.0f;
    float density = 0.0f;
    // DIVERGENCE: the engine never initialises this weight (haze.c:177, :188).
    float wsum = 0.0f;

    for (int i = 0; i < count; ++i) {
        const HazeBox& l = boxes[i];
        if (!l.visible) continue;

        const double fz = eye[2];
        const double tz = point[2];
        const double b = l.bottom;
        const double t = l.top;
        float frac = 1.0f;

        const float inv = 1.0f / static_cast<float>((t - b) * kFade);
        if (d[2] > 0.0f && tz > t) frac = std::clamp(static_cast<float>((t - fz) * inv), 0.0f, 1.0f);
        if (!(d[2] >= 0.0f) && !(tz >= b)) {
            frac = std::clamp(static_cast<float>((fz - b) * inv), 0.0f, 1.0f);
        }

        const float len = FastLength3(d);
        if (!(len > 0.0f)) continue;
        if (!(fz > b) && !(tz > b)) continue;
        if (fz >= t && tz >= t) continue;

        float s = d[2];
        if (!(s >= 0.0f)) s = -s;

        long double v = 0.0L;
        if ((!(fz > b) && tz >= t) || (fz >= t && !(tz > b))) {
            long double h = static_cast<long double>(t) - b;
            if (!(h >= 0.0L)) h = -h;
            if (s > 0.0f) v = h * len / s;
        } else if (fz > b && !(tz >= t) && !(fz >= t) && tz > b) {
            v = len;
        } else if (fz > b && !(fz >= t) && (!(tz > b) || tz >= t)) {
            long double e = (tz > b ? t : b) - fz;
            if (!(e >= 0.0L)) e = -e;
            if (s > 0.0f) v = len * e / s;
        } else if (tz > b && !(tz >= t) && (!(fz > b) || fz >= t)) {
            long double e = (fz > b ? t : b) - tz;
            if (!(e >= 0.0L)) e = -e;
            if (s > 0.0f) v = len * e / s;
        } else {
            // The engine shows an error and spins forever here. Contribute
            // nothing instead of hanging the editor.
            continue;
        }

        v = v * l.density;
        density = static_cast<float>(v * frac) + density;
        if (!(v < 0.0L || v > 0.0L)) continue;

        const long double sum = v + wsum;
        if (sum > 0.0L) {
            color[0] = static_cast<float>((v * l.color[0] + wsum * color[0]) / sum);
            color[1] = static_cast<float>((v * l.color[1] + wsum * color[1]) / sum);
            color[2] = static_cast<float>((v * l.color[2] + wsum * color[2]) / sum);
        }
        wsum = static_cast<float>(sum);
    }

    return density;
}

}  // namespace igi
