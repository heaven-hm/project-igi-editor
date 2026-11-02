// spline_geometry.h - Road, bridge, and rail spline geometry generation and vertex skinning.
//
// MIT License.
#pragma once

#include "runtime/spline_rules.h"

#include <cstddef>
#include <vector>

namespace igi::spline {

// ---------------------------------------------------------------------------
// Control points
// ---------------------------------------------------------------------------

// Waypoint world position and orientation frame.
struct SplineControlPoint {
    double point[3] = {0.0, 0.0, 0.0};
    float rot[10] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
};

SplineControlPoint MakeControlPoint(const SplineObjWaypointParams& waypoint);

// ---------------------------------------------------------------------------
// Placed spline model state
// ---------------------------------------------------------------------------

struct SplineModel {
    float spline_min = 0.0f;
    float spline_max = 0.0f;
    std::vector<SplineJoint> joints;
    int seg_count = 0;
    bool straight = false;
    bool reverse = false;
    float matrix[10] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
};

struct SplineSpanInput {
    SplineControlPoint from;
    SplineControlPoint to;
    int segments = kSplineMaxSegments;
    bool rigid = false;
    bool reversed = false;
    float spline_min = 0.0f;
    float spline_max = 0.0f;
    float matrix[10] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
};

struct SplineSpanGeometry {
    SplineModel model;
    double mid[3] = {0.0, 0.0, 0.0};
    float length = 0.0f;
    std::vector<float> seg_length;
};

// Generates the evaluated spline span geometry from input endpoints.
SplineSpanGeometry BuildSplineSpanGeometry(const SplineSpanInput& span);

// ---------------------------------------------------------------------------
// Spline parameter mapping
// ---------------------------------------------------------------------------

struct SplineParams {
    long double start = 0.0L;
    long double k = 0.0L;
};

SplineParams SplineParamsFor(const SplineModel& model);

// Evaluates a single vertex position deformed along the spline curve.
int SplinePoint(const SplineModel& model, const float v[3], const SplineParams& params, float out[3]);

// ---------------------------------------------------------------------------
// Vertex buffer generation
// ---------------------------------------------------------------------------

struct SplineRenderVertex {
    float pos[3] = {0.0f, 0.0f, 0.0f};
    float normal[3] = {0.0f, 0.0f, 0.0f};
    float uv[2] = {0.0f, 0.0f};
};

struct SplineMeshSource {
    const float* verts = nullptr;
    int vert_count = 0;
    int stride = 8;
};

// Evaluates deformed vertex positions, normals, and UVs for the spline segment.
int FillSplineVertices(const SplineModel& model, const SplineMeshSource& src, SplineRenderVertex* out,
                       int out_capacity);

int FillSplineSpanVertices(const SplineSpanGeometry& span, const SplineMeshSource& src,
                           SplineRenderVertex* out, int out_capacity);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void PackSplineMeshSource(const float* positions, int position_stride, const float* normals,
                          int normal_stride, const float* uv, int uv_stride, int vertex_count,
                          float* out);

inline int TruncateToInt(long double value) { return SplineTruncateToInt(value); }

}  // namespace igi::spline
