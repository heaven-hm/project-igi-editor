// spline_geometry.cpp - Road, bridge, and rail spline geometry evaluation and vertex skinning.
//
// MIT License.

#include "level/spline_geometry.h"

#include <cstddef>
#include <cstring>
#include <utility>

namespace igi::spline {
namespace {

constexpr float kZero = 0.0f;
constexpr float kHalf = 0.5f;
constexpr float kOne = 1.0f;

using igi::SplineTruncateToInt;

const float* JointRot(const SplineModel& model, int index) {
    return model.joints[static_cast<std::size_t>(index)].rot;
}

const float* JointPos(const SplineModel& model, int index) {
    return model.joints[static_cast<std::size_t>(index)].pos;
}

}  // namespace

SplineControlPoint MakeControlPoint(const SplineObjWaypointParams& waypoint) {
    SplineControlPoint out;
    out.point[0] = waypoint.point[0];
    out.point[1] = waypoint.point[1];
    out.point[2] = waypoint.point[2];
    igi::SplineWaypointFrame(waypoint, out.rot);
    return out;
}

SplineSpanGeometry BuildSplineSpanGeometry(const SplineSpanInput& span) {
    SplineSpanGeometry out;
    out.model.spline_min = span.spline_min;
    out.model.spline_max = span.spline_max;
    out.model.straight = span.rigid;
    out.model.reverse = span.reversed;
    std::memcpy(out.model.matrix, span.matrix, sizeof(out.model.matrix));

    SplineSpan engine_span;
    std::memcpy(engine_span.from, span.from.point, sizeof(engine_span.from));
    std::memcpy(engine_span.to, span.to.point, sizeof(engine_span.to));
    std::memcpy(engine_span.rot_from, span.from.rot, sizeof(engine_span.rot_from));
    std::memcpy(engine_span.rot_to, span.to.rot, sizeof(engine_span.rot_to));
    engine_span.segments = span.segments;
    engine_span.rigid = span.rigid;
    engine_span.reversed = span.reversed;

    SplineSpanJoints joints = BuildSplineSpanJoints(engine_span);

    out.mid[0] = joints.mid[0];
    out.mid[1] = joints.mid[1];
    out.mid[2] = joints.mid[2];
    out.length = joints.length;
    out.seg_length = joints.seg_length;
    out.model.joints = std::move(joints.joints);
    out.model.seg_count = static_cast<int>(out.model.joints.size()) - 1;
    return out;
}

SplineParams SplineParamsFor(const SplineModel& model) {
    const float inv =
        kOne / (static_cast<long double>(model.spline_max) - model.spline_min);
    SplineParams params;
    params.start = -static_cast<long double>(model.reverse ? model.spline_max : model.spline_min);
    params.k = static_cast<long double>(model.seg_count) * static_cast<long double>(inv);
    if (model.reverse) params.k = -params.k;
    return params;
}

int SplinePoint(const SplineModel& model, const float v[3], const SplineParams& params, float out[3]) {
    const long double t = (params.start + v[0]) * params.k;
    const int seg_count = model.seg_count;
    if (seg_count < 1 || static_cast<int>(model.joints.size()) < seg_count + 1) return -1;

    int seg = SplineTruncateToInt(t);
    if (seg == seg_count) seg--;
    if (seg < 0) seg = 0;
    if (seg > seg_count - 1) seg = seg_count - 1;

    long double f = t - seg;
    if (kZero > f) {
        f = kZero;
    } else if (!(kOne >= f)) {
        f = kOne;
    }

    const float* a = JointRot(model, seg);
    const float* b = JointRot(model, seg + 1);
    const float* pa = JointPos(model, seg);
    const float* pb = JointPos(model, seg + 1);

    long double p0 = static_cast<long double>(a[2]) * v[2] + static_cast<long double>(a[1]) * v[1];
    float b1 = static_cast<float>(static_cast<long double>(a[5]) * v[2] +
                                  static_cast<long double>(a[4]) * v[1]);
    float b2 = static_cast<float>(static_cast<long double>(a[8]) * v[2] +
                                  static_cast<long double>(a[7]) * v[1]);
    p0 = p0 + pa[0];
    b1 = b1 + pa[1];
    b2 = b2 + pa[2];
    long double c0 = static_cast<long double>(b[2]) * v[2] + static_cast<long double>(b[1]) * v[1];
    float c1 = static_cast<float>(static_cast<long double>(b[5]) * v[2] +
                                  static_cast<long double>(b[4]) * v[1]);
    float c2 = static_cast<float>(static_cast<long double>(b[8]) * v[2] +
                                  static_cast<long double>(b[7]) * v[1]);
    c0 = c0 + pb[0];
    c1 = c1 + pb[1];
    c2 = c2 + pb[2];

    out[0] = static_cast<float>((c0 - p0) * f + p0);
    out[1] = static_cast<float>((static_cast<long double>(c1) - b1) * f + b1);
    out[2] = static_cast<float>((static_cast<long double>(c2) - b2) * f + b2);
    return seg;
}

int FillSplineVertices(const SplineModel& model, const SplineMeshSource& src, SplineRenderVertex* out,
                       int out_capacity) {
    if (src.verts == nullptr || out == nullptr || src.vert_count <= 0) return 0;
    if (src.stride < 8) return 0;
    if (src.vert_count > out_capacity) return 0;

    const float* m = model.matrix;

    if (model.straight) {
        const float center =
            static_cast<float>((static_cast<long double>(model.spline_max) + model.spline_min) * kHalf);
        const float* v = src.verts;
        for (int i = 0; i < src.vert_count; i++, v += src.stride) {
            const long double x = v[0] - center;
            const float y = v[1];
            const float z = v[2];
            SplineRenderVertex& dst = out[i];
            dst.pos[0] = static_cast<float>((z * m[2] + y * m[1]) + x * m[0]);
            dst.pos[1] = static_cast<float>((x * m[3] + z * m[5]) + y * m[4]);
            dst.pos[2] = static_cast<float>((z * m[8] + y * m[7]) + x * m[6]);
            dst.normal[0] = v[3];
            dst.normal[1] = v[4];
            dst.normal[2] = v[5];
            dst.uv[0] = v[6];
            dst.uv[1] = v[7];
        }
        return src.vert_count;
    }

    const SplineParams params = SplineParamsFor(model);
    if (model.seg_count < 1) return 0;
    const float* v = src.verts;
    for (int i = 0; i < src.vert_count; i++, v += src.stride) {
        SplineRenderVertex& dst = out[i];
        float position[3];
        const int seg = SplinePoint(model, v, params, position);
        if (seg < 0) return 0;
        dst.pos[0] = position[0];
        dst.pos[1] = position[1];
        dst.pos[2] = position[2];

        const float* a = JointRot(model, seg);
        dst.normal[0] = static_cast<float>((static_cast<long double>(a[2]) * v[5] +
                                            static_cast<long double>(a[1]) * v[4]) +
                                           static_cast<long double>(v[3]) * a[0]);
        dst.normal[1] = static_cast<float>((static_cast<long double>(a[4]) * v[4] +
                                            static_cast<long double>(a[3]) * v[3]) +
                                           static_cast<long double>(a[5]) * v[5]);
        dst.normal[2] = static_cast<float>((static_cast<long double>(a[7]) * v[4] +
                                            static_cast<long double>(a[6]) * v[3]) +
                                           static_cast<long double>(a[8]) * v[5]);
        dst.uv[0] = v[6];
        dst.uv[1] = v[7];
    }
    return src.vert_count;
}

int FillSplineSpanVertices(const SplineSpanGeometry& span, const SplineMeshSource& src,
                           SplineRenderVertex* out, int out_capacity) {
    return FillSplineVertices(span.model, src, out, out_capacity);
}

void PackSplineMeshSource(const float* positions, int position_stride, const float* normals,
                          int normal_stride, const float* uv, int uv_stride, int vertex_count,
                          float* out) {
    if (positions == nullptr || out == nullptr || vertex_count <= 0) return;
    if (position_stride <= 0) position_stride = 3;
    if (normal_stride <= 0) normal_stride = 3;
    if (uv_stride <= 0) uv_stride = 2;
    for (int i = 0; i < vertex_count; i++) {
        const float* p = positions + static_cast<std::size_t>(i) * position_stride;
        float* dst = out + static_cast<std::size_t>(i) * 8;
        dst[0] = p[0];
        dst[1] = p[1];
        dst[2] = p[2];
        if (normals != nullptr) {
            const float* n = normals + static_cast<std::size_t>(i) * normal_stride;
            dst[3] = n[0];
            dst[4] = n[1];
            dst[5] = n[2];
        } else {
            dst[3] = 0.0f;
            dst[4] = 0.0f;
            dst[5] = 0.0f;
        }
        if (uv != nullptr) {
            const float* t = uv + static_cast<std::size_t>(i) * uv_stride;
            dst[6] = t[0];
            dst[7] = t[1];
        } else {
            dst[6] = 0.0f;
            dst[7] = 0.0f;
        }
    }
}

}  // namespace igi::spline
