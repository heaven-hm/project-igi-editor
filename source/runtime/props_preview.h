#pragma once
/******************************************************************************
 * @file    props_preview.h
 * @brief   The engine's own placement/bounds for a `Wire`, shared by the draw
 *          pass and the pick pass so they cannot disagree.
 *
 * ORACLE: /Users/haseeb-mir/Documents/Code/IGI-RE (read-only).
 *
 * WHAT A WIRE IS. `Wire` (Wire_Open HOOK 0x43EFF0, src/props/wire.c:89-113) is
 * a RigidDynCubeObj with exactly three authored parameters -- two ObjectPos
 * anchors and one String16 model (wire.c:108-111). It draws ONE model and no
 * child or ATTA parts: Wire_Create/Wire_ParamsChanged never call
 * RigidDynCubeObj_CreateMagicObjs, and the shipped wire model `320_01_1` has an
 * empty ATTA chunk (measured in MISSIONS/location0/LEVEL6/MODELS/LEVEL6.RES).
 * So "the wire's model is missing" can only be the editor not drawing the one
 * model it owns, never an extra piece.
 *
 * THE PLACEMENT (Wire_ParamsChanged HOOK 0x43F3D0, wire.c:160-191):
 *
 *     len = |end - start|;                                  // :172
 *     Matrix_SetYAxis(&m, normalise(end - start));          // :173-177
 *     w->obj.matrix = m;                                    // :178
 *     w->obj.scale[1] = len * kWireScale;                   // :179  (1/4096)
 *     mid = (start + end) / 2;                              // :181-186
 *     radius = VirModel_GetRadius(model) * max_scale(obj);  // :187-188
 *     DynCube_Move(world, w, &mid, radius);                 // :189
 *
 * The last two lines are the object's collision/visibility sphere: the engine
 * centres it on the MIDPOINT and grows it by the stretched Y scale, so it
 * already covers the whole span. The editor's draw pass instead tested the
 * UNSTRETCHED model's bound sphere at the START anchor (obj.pos is the start
 * anchor), i.e. a ~0.5 m sphere on one end of a 25-143 m wire, and its pick
 * pass drew the unstretched model at that same anchor. Both make a placed wire
 * vanish or become unclickable as soon as the start anchor leaves the view:
 * the "Wire missing models" symptom. This module is that engine rule, once.
 *
 * `max_scale` is src/binocular/binocular.h (`max_scale`), the engine's own
 * helper: the largest of the three scale factors.
 *****************************************************************************/

#include <glm/glm.hpp>

#include "../level/task_schema.h"

namespace igi {
namespace props {

// kWireScale: src/props/wire.c:320-321
//   DATA(5334B0) double kWireScale INIT(0.000244140625);   // 1/4096
constexpr double kWireScale = 0.000244140625;

// World units per metre in the object pass' model matrix (renderer_objects.cpp
// `base_scale`). The engine's own conversion is baked into the mesh loader; the
// editor keeps 40.96 as its model-space -> world multiplier.
constexpr double kBaseScale = 40.96;

// The engine's `max_scale(float s[3])` (src/binocular/binocular.h): s[0] wins
// ties.
inline double MaxScale(double sx, double sy, double sz) {
    double m = sz < sy ? sy : sz;
    return sx > m ? sx : m;
}

// The object's `scale[1]` for a wire (wire.c:179), an ENGINE scale factor: 1.0
// is an unstretched model. The shipped model is 1 m long (MVTX/vertex data
// spans +/-2048 world units), so a 4096-unit wire has scale 1.
inline double WireScaleY(double length_world_units) {
    return length_world_units * kWireScale;
}

// Wire_ParamsChanged' midpoint (wire.c:181-186).
inline glm::dvec3 WireMidpoint(const glm::dvec3& start, const glm::dvec3& end) {
    return (start + end) * 0.5;
}

// Wire_ParamsChanged' DynCube sphere radius (wire.c:187-189): the model's bound
// radius times the LARGEST scale factor, which for a wire of any length is the
// stretched Y.
//
// `mesh_bound_radius` is the model's bound radius in EDITOR mesh units -- the
// same |halfExtents| value the pass already has -- so the result is in world
// units: mesh radius * 40.96 * engine scale.
inline double WireBoundRadius(double mesh_bound_radius, double scale_x,
                              double scale_z, double length_world_units) {
    return mesh_bound_radius * kBaseScale *
           MaxScale(scale_x, WireScaleY(length_world_units), scale_z);
}

// One sphere covering everything the wire draws: the engine's DynCube sphere.
struct WireDrawSphere {
    glm::dvec3 center{0.0};
    double radius = 0.0;
};

inline WireDrawSphere WireDrawBounds(const glm::dvec3& start, const glm::dvec3& end,
                                     double mesh_bound_radius, double scale_x,
                                     double scale_z) {
    WireDrawSphere s;
    s.center = WireMidpoint(start, end);
    s.radius = WireBoundRadius(mesh_bound_radius, scale_x, scale_z,
                               glm::length(end - start));
    return s;
}

// The model matrix Wire_ParamsChanged implies, in the editor's convention: the
// object is translated to the midpoint, its local +Y points along end - start,
// X/Z keep the authored scale and only Y is stretched (wire.c:177-179). This is
// exactly what renderer_objects.cpp drew inline; the pick pass now uses the same
// function instead of drawing an unstretched stub at the start anchor. Returns
// false when the two anchors coincide (Wire_ParamsChanged divides by a zero
// length there; the editor leaves the object unplaced).
inline bool WireModelMatrix(const glm::dvec3& start, const glm::dvec3& end,
                            double obj_scale, glm::mat4* out) {
    using TaskSchemaNS::Props::Basis;
    using TaskSchemaNS::Props::ComputeWireSpan;
    using TaskSchemaNS::Props::Vec3;
    using TaskSchemaNS::Props::YAxisBasis;

    if (out == nullptr) return false;
    const auto span = ComputeWireSpan(Vec3(start.x, start.y, start.z),
                                      Vec3(end.x, end.y, end.z));
    if (!span.valid) return false;

    const Basis basis = YAxisBasis(span.dir);
    const glm::mat3 frame(
        glm::vec3((float)basis.x.x, (float)basis.x.y, (float)basis.x.z),
        glm::vec3((float)basis.y.x, (float)basis.y.y, (float)basis.y.z),
        glm::vec3((float)basis.z.x, (float)basis.z.y, (float)basis.z.z));
    const float total_scale = (float)(kBaseScale * obj_scale);
    glm::mat4 model(1.0f);
    model = glm::translate(model, glm::vec3((float)span.mid.x, (float)span.mid.y,
                                           (float)span.mid.z));
    model = model * glm::mat4(frame);
    model = glm::scale(model, glm::vec3(total_scale,
                                        (float)(span.length * kWireScale) * (float)kBaseScale,
                                        total_scale));
    *out = model;
    return true;
}

}  // namespace props
}  // namespace igi
