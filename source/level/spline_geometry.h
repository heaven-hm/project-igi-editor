// spline_geometry.h - the ROAD/BRIDGE/RAIL geometry the engine itself evaluates,
// for the editor's level layer: control points -> Hermite joints -> the bent,
// lit vertex stream the retail renderer draws.
//
// This is the level-layer half of the spline family (#79 authored model, #69
// curve). `source/runtime/spline_rules.{h,cpp}` (namespace `igi`) holds the
// AUTHORED model and the rules - the parameter tables, the authoring helpers,
// the Hermite primitives, the model-extent scan, the rigid frame, both radius
// rules, `SplineMesh_GetVerts` and the `SplineCache_*` policy. This file holds
// the RENDER path: what the engine does with a placed span to produce the
// vertices it hands to D3D, including the vertex NORMALS, which the rules half
// deliberately does not cover.
//
// Every rule below is cited to IGI-RE (`/Users/haseeb-mir/Documents/Code/IGI-RE`,
// read-only), function + address + file:line. Nothing here is invented: where the
// engine has a branch, the branch is transcribed; where the port cannot reproduce
// the engine's behaviour (an unbounded read, a hang), the divergence is named in
// the function comment rather than hidden.
//
// The oracle functions this module is built from:
//
//   src/sound/segsetup.c:88-199    setup_segments: the Hermite joints. Reused
//                                  through igi::BuildSplineSpanJoints.
//   src/render/render.c:286-317    spline_point (static): one vertex's position,
//                                  blended from two adjacent joint frames, and
//                                  the joint index it used.
//   src/render/render.c:319-326    spline_params (static): the model X range ->
//                                  curve parameter mapping (`start`, `k`).
//   src/render/render.c:613-670    Direct3DRender_FillSplineVB HOOK(0x49CC80):
//                                  the vertex stream - position, normal, uv -
//                                  for both the straight and the curved branch.
//   src/render/render.c:86-112     ModelObj: splineMin/splineMax, points[21][3],
//                                  segs[21] (Matrix = 10 floats), segCount,
//                                  straight, reverse, matrix.
//   src/render/render.c:57-67      RigidMesh: `vertices` is 8 floats per vertex.
//   src/dxinit/spline.c:36-105     SplineMesh_GetVerts HOOK(0x49A050): the
//                                  collision-path twin of spline_point, used
//                                  here only to keep the two contracts aligned.
//
// Relationship to `source/renderer/spline_geometry.{h,cpp}` (the #59 module, in
// namespace `spline_geometry`): that module bends a parsed MEF mesh with
// `SplineObj_BendVerts` (HOOK 0x4E1440, src/shadow/spline.c:24-90) - the
// COLLISION path, which produces positions only. This module is the RENDER path
// (`Direct3DRender_FillSplineVB`), which is where the vertex normals come from,
// and it takes its joints from the `igi` rules instead of re-deriving them. The
// two paths differ in a way that matters and is reproduced here: the collision
// path blends the two joint frames per vertex, while the render path transforms
// the normal by the SINGLE joint frame `spline_point` selected - with no blend
// and no renormalisation.
//
// Named divergences (never silently "fixed"):
//   * `spline_point` indexes `o->segs[seg]` / `o->segs[seg + 1]` with no bounds
//     check (:301-302), so a model vertex outside the model's own X range reads
//     memory adjacent to the array. This port clamps the index into
//     [0, seg_count - 1] and returns 0 when the span has fewer than two joints,
//     the same policy `igi::SplineMeshGetVerts` records.
//   * The engine writes into a dynamic vertex buffer sized by the device; this
//     port writes into the caller's array and refuses rather than overrunning it.
//   * The oracle is compiled with `#pragma GCC optimize("excess-precision=standard")`
//     so its x87 intermediates keep 80 bits. This port keeps the same declared
//     types (`float` fields, `long double` intermediates) but a host x86-64 build
//     evaluates in SSE2, so results are exact to the engine's declared precision
//     and NOT bit-identical.
#pragma once

#include "runtime/spline_rules.h"

#include <cstddef>
#include <vector>

namespace igi::spline {

// ---------------------------------------------------------------------------
// Control points
// ---------------------------------------------------------------------------

// One control point of a road, bridge or rail: the waypoint's world point and
// its engine frame. `SplineObjWaypoint.point` is a `Vec3d` and `.rot` a `Matrix`
// (src/editobj/editobjtypes.h:10-11).
struct SplineControlPoint {
    double point[3] = {0.0, 0.0, 0.0};
    float rot[10] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
};

// The authored Euler triple -> the engine frame, via the same rule
// ParamReal32x9_Parse uses (HOOK 0x4EFBA0, src/physobj/paramops.c:423-435:
// Matrix_SetEuler). This is the way a `SplineObjWaypoint` read out of the level
// becomes a control point.
SplineControlPoint MakeControlPoint(const SplineObjWaypointParams& waypoint);

// ---------------------------------------------------------------------------
// The engine's placed spline (ModelObj's spline fields)
// ---------------------------------------------------------------------------

// `ModelObj`'s spline state, src/render/render.c:86-112. The joints are the same
// data `SplineObjSegment` keeps in `segPos`/`segRot`
// (src/sound/soundtypes.h:55-58): `points[i]` is the joint position relative to
// the span's mid point and `segs[i]` its frame.
struct SplineModel {
    // The segment MODEL's local X range, `SplineObjSegment.start`/`.end`
    // (src/sound/soundtypes.h:55-56), scanned by SplineObjSegment_Setup
    // (src/sound/segsetup.c:277-293) - see `igi::SplineScanModelExtent`.
    float spline_min = 0.0f;
    float spline_max = 0.0f;

    // `float points[21][3]` and `Matrix segs[21]` (10 floats each): joint i is
    // `joints[i].pos` / `joints[i].rot`. `segCount` is `o->segments` after the
    // rigid clamp, so `joints` holds `seg_count + 1` entries.
    std::vector<SplineJoint> joints;
    int seg_count = 0;

    // `char straight;  /* not bent along the spline */` (:111). This is the
    // engine's render-side name for the authored "Linear Segments" / "Align"
    // (it is set from `w->rigid || w->straight`, src/tagitem/spline.c:80-81).
    bool straight = false;
    // `char reverse;` (:112) - the authored "Flip" (src/tagitem/spline.c:82).
    bool reverse = false;

    // `Matrix matrix;` (:93), the placed object's world frame. Only the straight
    // branch reads it (src/render/render.c:640-646).
    float matrix[10] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
};

// One authored span plus the model range it is drawn with.
struct SplineSpanInput {
    SplineControlPoint from;
    SplineControlPoint to;
    // The authored "NumAreas", already clamped against the container's
    // "Number of Matrices / Segment" (see `igi::SplineEffectiveSegmentCount`).
    int segments = kSplineMaxSegments;
    // `w->rigid || w->straight` (src/tagitem/spline.c:80-81) - "Linear Segments"
    // or "Align".
    bool rigid = false;
    bool reversed = false;  // the authored "Flip"
    // The segment model's local X range; `igi::SplineScanModelExtent` produces
    // these from the model's own vertices.
    float spline_min = 0.0f;
    float spline_max = 0.0f;
    // `ModelObj.matrix`, used by the straight branch only.
    float matrix[10] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
};

// A placed span, ready for SplinePoint / FillSplineVertices.
struct SplineSpanGeometry {
    SplineModel model;
    double mid[3] = {0.0, 0.0, 0.0};  // (from + to) / 2, the joint origin
    float length = 0.0f;              // SplineObjSegment.length, the float sum
    std::vector<float> seg_length;    // SplineObjSegment.segLength, entry i + 1
};

// The whole chain from the two control points: the Hermite joints of
// `setup_segments` (through `igi::BuildSplineSpanJoints`) turned into the
// `ModelObj` spline state the render path reads.
//
// The rigid clamp is the engine's: SplineObjSegment_Setup forces `segments = 1`
// when the span is rigid (src/sound/segsetup.c:269-270), which is why a
// "Linear Segments" bridge is one straight chord and not 20 Hermite steps.
SplineSpanGeometry BuildSplineSpanGeometry(const SplineSpanInput& span);

// ---------------------------------------------------------------------------
// spline_params / spline_point (src/render/render.c:319-326, :286-317)
// ---------------------------------------------------------------------------

// `spline_params`:
//   float inv = kOne.f / ((long double)o->splineMax - o->splineMin);
//   *start    = -(long double)(o->reverse ? o->splineMax : o->splineMin);
//   *k        = (long double)o->segCount * inv;   if (o->reverse) *k = -*k;
// `inv` is a FLOAT holding the long-double quotient, and `k` widens it again -
// the same narrowing `setup_segments` does at src/sound/segsetup.c:107.
struct SplineParams {
    long double start = 0.0L;
    long double k = 0.0L;
};

SplineParams SplineParamsFor(const SplineModel& model);

// `spline_point`: maps a vertex's local X to a curve parameter, picks the joint
// pair, and writes the blended position into `out[0..2]`. Returns the index of
// the FIRST joint of the pair - the engine's return value, which
// Direct3DRender_FillSplineVB uses to transform the normal.
//
// The joint position is `joints[seg].pos` plus the joint frame applied to the
// vertex's (0, y, z): the model's own X is a PARAMETER, not a position
// (src/render/render.c:305-316). The `seg == segCount -> seg--` correction
// (:294-295) and the `f` clamp (:297-300, a NaN `f` becomes 1.0) are the
// engine's. Returns -1 when the span has fewer than two joints (see the
// divergence note at the top of this file).
int SplinePoint(const SplineModel& model, const float v[3], const SplineParams& params, float out[3]);

// ---------------------------------------------------------------------------
// Direct3DRender_FillSplineVB 0x49CC80 (src/render/render.c:613-670)
// ---------------------------------------------------------------------------

// `RigidMesh.vertices` is 8 floats per vertex (src/render/render.c:56-61):
// position xyz, normal xyz, uv. The engine writes the same 8 floats into
// `gVBNormal`, whose stride is 32 bytes (src/render/render.c:626-641).
struct SplineRenderVertex {
    float pos[3] = {0.0f, 0.0f, 0.0f};
    float normal[3] = {0.0f, 0.0f, 0.0f};
    float uv[2] = {0.0f, 0.0f};
};

// The source mesh: `RigidMesh.vertexCount` / `.vertices` (:60-61), 8 floats per
// vertex.
struct SplineMeshSource {
    const float* verts = nullptr;
    int vert_count = 0;
    int stride = 8;
};

// The vertex stream, exactly as the engine builds it:
//
//   straight (:637-648): `center = (splineMax + splineMin) * 0.5f` (a long
//     double sum narrowed to float), `x = v[0] - center` (a FLOAT subtraction
//     widened to long double), the position rotated by `ModelObj.matrix`, and
//     the normal and the uv copied STRAIGHT THROUGH - the straight branch never
//     touches `dst[3..7]`.
//
//   curved (:649-668): the position comes from `spline_point`; the normal is
//     transformed by the SINGLE joint frame `o->segs[seg].m` that `spline_point`
//     returned - `dst[3] = a[2]*v[5] + a[1]*v[4] + v[3]*a[0]`, and the same
//     row-major product for rows 1 and 2 - with no blend between joints and NO
//     renormalisation; the uv is copied through.
//
// Returns the number of vertices written, or 0 when the source does not fit
// `out_capacity` or the span has fewer than two joints (the engine overruns the
// buffer and reads before the joint array; see the divergence note above).
int FillSplineVertices(const SplineModel& model, const SplineMeshSource& src, SplineRenderVertex* out,
                       int out_capacity);

// `FillSplineVertices` for a span built by `BuildSplineSpanGeometry`.
int FillSplineSpanVertices(const SplineSpanGeometry& span, const SplineMeshSource& src,
                           SplineRenderVertex* out, int out_capacity);

// ---------------------------------------------------------------------------
// Small helpers shared with callers
// ---------------------------------------------------------------------------

// Packs a mesh whose attributes live in separate arrays into the engine's single
// interleaved `RigidMesh.vertices` layout - 8 floats per vertex: position xyz,
// normal xyz, uv (src/render/render.c:56-61) - which is what
// `FillSplineVertices` consumes. `out` must hold `8 * vertex_count` floats.
// Strides are in floats; a null normal or uv array writes zeros there.
void PackSplineMeshSource(const float* positions, int position_stride, const float* normals,
                          int normal_stride, const float* uv, int uv_stride, int vertex_count,
                          float* out);

// `long double` -> `int` the way `x87_fistp` does under the round-toward-zero
// control word the engine installs at startup. Same rule as
// `igi::SplineTruncateToInt`, re-exported here because the render path uses it.
inline int TruncateToInt(long double value) { return SplineTruncateToInt(value); }

}  // namespace igi::spline
