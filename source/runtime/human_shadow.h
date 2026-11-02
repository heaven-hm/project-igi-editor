// human_shadow.h - Blob shadow quad for Human-derived entities.
//
// MIT License.
#pragma once

#include <string>

namespace igi {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

inline constexpr float kShadowFade = 0.8f;
inline constexpr float kShadowMinFade = 0.3f;
inline constexpr float kShadowDepth = 10.0f;
inline constexpr float kShadowColor[3] = {0.9f, 0.9f, 0.9f};
inline constexpr float kShadowMaxHeight = 4096000.0f;
inline constexpr float kShadowQuadSize = 0x1.ccccccp+10f;
inline constexpr const char* kShadowTextureName = "shadow1.tex";
inline constexpr double kShadowUnitsPerMetre = 4096.0;

// ---------------------------------------------------------------------------
// Geometry & shading
// ---------------------------------------------------------------------------

float HumanShadowFade(double height, double maxHeight);
void HumanShadowColor(float fade, float out[3]);
void HumanShadowBasis(const double up[3], float m[9]);
void HumanShadowQuad(const double up[3], float size, float depth, float verts[4][3]);

struct HumanShadowTriangles {
    float pos[2][3][3];   // [triangle][vertex][xyz]
    float uv[2][3][2];    // [triangle][vertex][uv]
    int count = 2;
};

HumanShadowTriangles HumanShadowBuildTriangles(const float verts[4][3]);

// ---------------------------------------------------------------------------
// Placement
// ---------------------------------------------------------------------------

inline bool HumanShadowDrawn(int visible, int enabled) {
    return visible != 0 && enabled != 0;
}

struct HumanShadowPlacement {
    bool valid = false;
    double ground_pos[3] = {0.0, 0.0, 0.0};
    float verts[4][3] = {};
    float color[3] = {0.0f, 0.0f, 0.0f};
    float fade = 0.0f;
};

bool IsHumanShadowOwner(const std::string& type);

HumanShadowPlacement BuildHumanShadowPlacement(double x, double y, double placement_z,
                                               bool have_ground, double ground_z);

}  // namespace igi
