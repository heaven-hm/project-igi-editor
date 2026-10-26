// spline_rules.h - the retail SplineObj / SplineObjWaypoint data model and the
// curve the engine turns its control points into, resolved the way IGI-RE
// resolves it.
//
// Two editor issues are answered here, and they are one family:
//
//   #79 "Road/track spline objects" - the AUTHORED model. A road, a track, a
//       bridge or a rail is a `SplineObj` container task with a chain of
//       `SplineObjWaypoint` children; each waypoint after the first carries the
//       segment model for the span that ends at it. Issue #79 asks for the data
//       model, the authoring workflow, and "the correct longitudinal local axis
//       derived from each segment extent" - that axis is the MODEL'S OWN LOCAL
//       X RANGE, read out of the model file, not a hardcoded axis.
//
//   #69 "Hermite road/bridge segments" - the CURVE. The span between two
//       waypoints is a cubic Hermite whose tangents are each waypoint frame's
//       local X axis scaled by the chord length. The mesh is bent by blending
//       two consecutive JOINT frames, and the interior joints' roll is
//       interpolated between the two waypoints' Euler X angles - it is not a
//       zero-roll frame, which is the fidelity gap #69 reports.
//
// Every rule below is cited to IGI-RE (`/Users/haseeb-mir/Documents/Code/IGI-RE`,
// read-only), function + address + file:line. The oracle files this module is
// built from:
//
//   src/editobj/spline.c:177-225   SplineObj_Open 0x502250: both ParamTables
//   src/editobj/spline.c:227-242   SplineObj_Create 0x5026E0: the container defaults
//   src/editobj/spline.c:246-322   SplineObj_Changed 0x502740: what moving or
//                                  turning the container does to the waypoints
//   src/tagitem/spline.c:457-561   SplineObjWaypoint_Create 0x5008F0: the
//                                  waypoint defaults and the one-model-length step
//   src/tagitem/spline.c:759-846   SplineObjWaypoint_Changed 0x500EF0: the
//                                  NumAreas clamp, the snap, Align -> rigid
//   src/tagitem/spline.c:598-676   Waypoint_AutoOrient: the frame from prev/next
//   src/tagitem/spline.c:686-756   Waypoint_Straighten: a run of Align waypoints
//   src/tagitem/spline.c:219-454   SplineObj_GetPoints 0x4FEC10: the rail sampler
//   src/sound/segsetup.c:88-199    setup_segments: the Hermite joints
//   src/sound/segsetup.c:203-251   setup_rigid: the model X range -> chord frame
//   src/sound/segsetup.c:256-366   SplineObjSegment_Setup 0x4E4530: the extent
//                                  scan, the radius, the cache invalidation
//   src/dxinit/spline.c:14-119     Float_Log2 0x499FF0, SplineCache_Remove
//                                  0x49A010, SplineMesh_GetVerts 0x49A050,
//                                  SplineCache_Reset 0x49A340
//   src/terrdraw/flare.c:44-69     Hermite_Init 0x518130, Hermite_Eval 0x518190,
//                                  Hermite_Slope 0x5181C0
//   src/math/matrix.c:99-110       Matrix_SetEuler 0x4B38E0
//   src/math/matrix.c:181-208      Matrix_GetEuler 0x4B3C50
//   src/math/matrix.c:148-164      Matrix_SetRotZ 0x4B3BE0
//   src/file/vec.c:232-246         Vec3_ToAngles 0x4B2D00
//   src/physobj/paramops.c:139-186 the script argument counts per declared type
//   src/sound/soundtypes.h:39-69   the SplineObjSegment layout
//   src/editobj/editobjtypes.h     the SplineObj / SplineObjWaypoint layouts
//
// Relationship to the work already landed for #59: `source/renderer/
// spline_geometry.{h,cpp}` holds the RENDERER's half - `BuildSplineSpan`,
// `BendSegmentMesh` (SplineObj_BendVerts, src/shadow/spline.c), `BuildRailPath`,
// `WrapRailDistance`, `SampleRailPoint`, `PlaceTrainOnRail` - in GLM types and in
// the editor's 10-float vertex layout. This module is the RULES half: the
// authored parameter tables, the named Hermite primitives, the model-extent ->
// joint mapping, the `SplineMesh_GetVerts` contract (count, ordering, the
// 60000-float static buffer) and the `SplineCache_*` invalidation, none of which
// spline_geometry has, plus the curve evaluation restated over plain arrays so
// the parity record and the corpus tests carry the citations directly. Where the
// two overlap (`BuildSplineSpanJoints` vs `BuildSplineSpan`) the rule is the
// same and the doc records it; neither file was edited by this work.
//
// Named divergences (never silently fixed):
//   * The engine's x87 keeps 80-bit intermediates (`#pragma GCC optimize
//     ("excess-precision=standard")` in the oracle). This port uses `double`
//     where the engine's field is a float and `long double` where the engine
//     used x87, but a host x86-64 build computes in SSE2, so the results are
//     exact to the engine's precision and NOT bit-identical. Unverified.
//   * `Float_Log2` is not a spline rule: it is a fog helper that happens to
//     share the translation unit (`src/dxinit/spline.c:14`). It is ported
//     because it lives in the family, and marked as such.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace igi {

// ---------------------------------------------------------------------------
// Units and engine constants
// ---------------------------------------------------------------------------

// The engine works in "game units"; the conversion is 4096 units per metre.
// `src/math/mathvars.h:7` declares `float kFixed; /* 4096 */`, used at
// `src/missile/train.c:212-213`; the spline code keeps its own reciprocal as
// `kMeter = 1.0f / 4096.0f` (`src/editobj/spline.c:61`) and uses it to print
// segment lengths in metres (`src/editobj/spline.c:372-374`).
constexpr float kSplineUnitsPerMetre = 4096.0f;   // src/math/mathvars.h:7
constexpr float kSplineMetre = 1.0f / 4096.0f;    // src/editobj/spline.c:61

// `kFar = 4096.0f` (`src/editobj/spline.c:60`): the radius SplineObj_GetInfo
// reports for a container with no waypoints.
constexpr float kSplineFar = 4096.0f;             // src/editobj/spline.c:60

// The authored range of the two "NumAreas" / "Number of Matrices / Segment"
// slots, `gSplineObjSegmentRange = {2, 20}` (`src/editobj/spline.c:202-203`).
constexpr int kSplineMinSegments = 2;             // src/editobj/spline.c:202
constexpr int kSplineMaxSegments = 20;            // src/editobj/spline.c:203

// DATA constants of the spline family.
//   kPi / kSmallAngle / kTiny / kMinusTwo   src/tagitem/spline.c:849-854
//   kDefaultSegLength                       src/tagitem/spline.c:855
//   kTwoPi / kRadiusScale                   src/sound/segsetup.c:63-66
constexpr float kSplinePi = 0x1.921fb6p+1f;       // tagitem/spline.c:849
constexpr float kSplineTwoPi = 0x1.921fb6p+2f;    // sound/segsetup.c:64
constexpr float kSplineSmallAngle = 0.01f;        // tagitem/spline.c:850
constexpr double kSplineTiny = 0.001;             // tagitem/spline.c:852
constexpr float kSplineMinusTwo = -2.0f;          // tagitem/spline.c:853
constexpr float kSplineRadiusScale = 0x1.19999ap+0f;  // sound/segsetup.c:66

// `kDefaultSegLength = 141314.9f` (`src/tagitem/spline.c:855`), 34.5 m: the step
// SplineObjWaypoint_Create uses when the previous waypoint has no segment yet
// (`src/tagitem/spline.c:491-493`, `:520-522`).
constexpr float kSplineDefaultSegLength = 141314.9f;  // tagitem/spline.c:855

// `gSplineVerts` is a fixed 60000-float static buffer, `STORE(igb, B46D40, 4)
// float store_B46D40[60000]` (`src/dxinit/spline.c:118`). SplineMesh_GetVerts
// writes 3 floats per vertex into it and never bounds-checks: a model part with
// more than 20000 vertices overflows it. Reproduced, not corrected - the port
// refuses to write past the caller's capacity and reports 0 instead.
constexpr std::size_t kSplineVertexCapacity = 60000;  // dxinit/spline.c:118

// Two collision parts per model (`SplineObjSegment_Setup` loops `part < 2`,
// `src/sound/segsetup.c:311`; `SplineObjSegment_DrawMask` loops `j < 2`,
// `src/sound/splineseg.c:101`).
constexpr int kSplineModelParts = 2;              // sound/segsetup.c:311

// ---------------------------------------------------------------------------
// The authored parameter tables (ParamTable_Add order)
// ---------------------------------------------------------------------------
//
// The QSC argument index is `3 + ParamTable position`, because args 0/1/2 are
// the task id, the type string and the name. The token count of a row is the
// declared type's script argument count from `src/physobj/paramops.c:139-186`:
// Real32x9 consumes THREE script arguments and builds a 3x3 matrix from them
// (`ParamReal32x9_Parse`, `HOOK(0x4EFBA0)`, `src/physobj/paramops.c:423-435` -
// "three Euler angles"), ObjectPos aliases Real64x3 (`paramops.c:181`), RGB
// aliases Real32x3 (`paramops.c:179`), Angle aliases Real32 (`paramops.c:178`),
// and bool8/Int32/String16 each consume one (`paramops.c:170-175`).
constexpr std::size_t kSplineFirstParam = 3;

constexpr int kSplineObjParamCount = 10;          // ParamTable_New(10), editobj/spline.c:204
constexpr int kSplineObjTokenCount = 16;          // 4*1 + 2*1 + 3 + 1 + 3 + 3
constexpr int kSplineObjWaypointParamCount = 8;   // ParamTable_New(8),  editobj/spline.c:215
constexpr int kSplineObjWaypointTokenCount = 12;  // 3 + 3 + 1 + 1 + 1 + 1 + 1 + 1

enum SplineObjParamIndex {
    kSplineObjRigid = 0,        // "Linear Segments"                 bool8
    kSplineObjCollide = 1,      // "Display waypoints"               bool8
    kSplineObjSnap = 2,         // "Snap Length"                     bool8
    kSplineObjAutoOrient = 3,   // "Automatic Orientation"           bool8
    kSplineObjMaxSegments = 4,  // "Number of Matrices / Segment"    Int32, range 2..20
    kSplineObjCollisionLod = 5, // "Collision LOD"                   Int32
    kSplineObjPosition = 6,     // "Position"                        Real32x3
    kSplineObjAngle = 7,        // "Gamma Orientation"               Angle
    kSplineObjColour1 = 8,      // "Dirlight Angular Effect"         RGB
    kSplineObjColour2 = 9,      // "Dirlight Ambient Effect"         RGB
};

enum SplineObjWaypointParamIndex {
    kSplineWaypointOrientation = 0,  // "Orientation"              Real32x9 (3 angles)
    kSplineWaypointPosition = 1,     // "Position"                 ObjectPos (Real64x3)
    kSplineWaypointModel = 2,        // "Waypoint Model"           String16
    kSplineWaypointSegModel = 3,     // "Segment Model"            String16
    kSplineWaypointSegments = 4,     // "NumAreas"                 Int32, range 2..20
    kSplineWaypointStraight = 5,     // "Align"                    bool8
    kSplineWaypointReversed = 6,     // "Flip"                     bool8
    kSplineWaypointAutoOrient = 7,   // "Automatic Orientation"    bool8
};

// One row of an authored parameter table.
struct SplineParamDesc {
    const char* authored_name;  // the retail Task_DeclareParameters string
    const char* engine_field;   // the IGI-RE struct field the slot writes
    const char* type_name;      // the retail declared type
    int token_count;            // script arguments consumed
    bool has_range;             // whether Open registers a RangeI
    float range_min;            // src/editobj/spline.c:202-203
    float range_max;
};

// The 10 SplineObj rows, indexed by SplineObjParamIndex. Never null.
const SplineParamDesc* SplineObjParamTable();
// The 8 SplineObjWaypoint rows, indexed by SplineObjWaypointParamIndex.
const SplineParamDesc* SplineObjWaypointParamTable();

// ---------------------------------------------------------------------------
// The parsed authored form
// ---------------------------------------------------------------------------

// The resolved parameters of one "SplineObj" container. Field names follow the
// AUTHORED names, with the IGI-RE struct field in the comment, because two
// retail labels do not say what the field does ("Display waypoints" writes
// `collide`; "Collision LOD" writes `unk20`, which the engine only clamps at
// zero - `src/editobj/spline.c:316-317`).
struct SplineObjParams {
    bool rigid = false;           // "Linear Segments"    -> SplineObj.rigid
    bool collide = false;         // "Display waypoints"  -> SplineObj.collide
    bool snap = false;            // "Snap Length"        -> SplineObj.snap
    bool auto_orient = false;     // "Automatic Orientation" -> SplineObj.autoOrient
    int max_segments = kSplineMaxSegments;  // -> SplineObj.maxSegments
    int collision_lod = 0;        // "Collision LOD"      -> SplineObj.unk20
    // -> SplineObj.pos, which the engine declares `Vec3f pos;` (src/editobj/
    // editobjtypes.h:32) - the authored "Position" is Real32x3. Kept as double
    // here because the editor's token reader is double-typed; a delta of two
    // float-representable values narrows to the same float either way, which is
    // what SplineObj_Changed's `float dx = o->pos.x - o->oldPos.x;`
    // (src/editobj/spline.c:299-301) computes.
    double pos[3] = {0.0, 0.0, 0.0};
    float angle = kSplinePi;      // "Gamma Orientation"  -> SplineObj.angle
    float colour1[3] = {1.0f, 1.0f, 1.0f};  // -> SplineObj.color1
    float colour2[3] = {0.0f, 0.0f, 0.0f};  // -> SplineObj.color2
};

// The resolved parameters of one "SplineObjWaypoint". `orientation` is the
// THREE authored Euler angles; the engine turns them into a 3x3 row-major frame
// with Matrix_SetEuler (see SplineWaypointFrame). `segments` is the authored
// "NumAreas", which the engine clamps in SplineObjWaypoint_Changed.
struct SplineObjWaypointParams {
    float orientation[3] = {0.0f, 0.0f, 0.0f};  // -> SplineObjWaypoint.rot
    double point[3] = {0.0, 0.0, 0.0};          // -> SplineObjWaypoint.point
    std::string model_name;                     // "Waypoint Model"
    std::string seg_model;                      // "Segment Model"
    int segments = kSplineMaxSegments;          // "NumAreas"
    bool straight = false;                      // "Align"
    bool reversed = false;                      // "Flip"
    bool auto_orient = false;                   // "Automatic Orientation"
};

// Parses one authored "SplineObj" call. `arg_tokens` is the editor's
// LevelObject::argTokens layout ([0] id, [1] type, [2] name, [3] parameter 0).
// Returns false when the call is shorter than the 16 tokens the 10 parameters
// require, leaving `out` untouched - a truncated task is reported rather than
// silently filled with defaults.
bool ParseSplineObjParams(const std::vector<std::string>& arg_tokens, SplineObjParams* out);

// The same for "SplineObjWaypoint" (12 tokens).
bool ParseSplineObjWaypointParams(const std::vector<std::string>& arg_tokens,
                                  SplineObjWaypointParams* out);

// ---------------------------------------------------------------------------
// Derived authored rules
// ---------------------------------------------------------------------------

// The number of sub-segments a span is cut into. Two clamps, in this order:
//   1. SplineObjWaypoint_Changed, src/tagitem/spline.c:771-772:
//        n = min(w->segments, obj->maxSegments);  w->segments = min(n, 20);
//   2. SplineObjSegment_Setup, src/sound/segsetup.c:267-270:
//        o->segments = segments;  if (rigid) o->segments = 1;
// Note the lower bound: the engine NEVER raises a count of 0 or 1 back to 2,
// even though the authored range is 2..20. A waypoint authored with NumAreas 1
// gives a one-joint-pair span, and NumAreas 0 gives `o->segments = 0`, which
// makes `inv = 1/0` and every joint index degenerate. Reproduced.
int SplineEffectiveSegmentCount(int waypoint_segments, int max_segments, bool rigid);

// `w->rigid = obj->rigid || w->straight;` (src/tagitem/spline.c:769), passed to
// the segment setup as `w->rigid || w->straight` (src/tagitem/spline.c:80-81,
// :818-820). A span is rigid when the container says so OR the waypoint's own
// "Align" is set.
bool SplineSpanIsRigid(bool container_rigid, bool waypoint_straight);

// The local X axis of a waypoint frame, which is the Hermite tangent direction.
// `src/sound/segsetup.c:98-100` takes elements 0, 3 and 6 of the row-major 3x3 -
// the frame's first COLUMN, i.e. the object's local X axis. Elements 1/2, 4/5
// and 7/8 are never read, so the tangent carries no roll.
void SplineTangent(const float rot[10], double distance, double out[3]);

// The authored Euler triple -> the engine's row-major 3x3 frame.
// Matrix_SetEuler (HOOK 0x4B38E0, src/math/matrix.c:99-110) is Rz(az)*Ry(ay)*Rx(ax).
void EngineSetEuler(float ax, float ay, float az, float out[10]);
// The inverse, Matrix_GetEuler (HOOK 0x4B3C50, src/math/matrix.c:181-208): the
// three angles are returned normalised into [0, 2pi).
void EngineMatrixGetEuler(const float m[10], float* ax, float* ay, float* az);
// Matrix_SetRotZ (HOOK 0x4B3BE0, src/math/matrix.c:148-164).
void EngineSetRotZ(float a, float out[10]);
// Vec3_ToAngles (HOOK 0x4B2D00, src/file/vec.c:232-246):
//   yaw   = 0 when v[0] == 0 else atan2(v[1], v[0])
//   pitch = 0 when |v| == 0 else -asin(v[2] / |v|)
// The `v[0] == 0` guard means a chord with no X component yields yaw 0, not
// +/-pi/2.
void Vec3ToAngles(const float v[3], float* pitch, float* yaw);

// A waypoint's authored orientation as the engine's frame.
void SplineWaypointFrame(const SplineObjWaypointParams& waypoint, float out_rot[10]);

// SplineObjWaypoint_Create, src/tagitem/spline.c:457-546. A new waypoint copies
// its neighbour's model names, point and frame, normalises the frame's first
// column, scales it by ONE MODEL LENGTH - `seg->end - seg->start`, the segment
// model's own local X extent - and steps the point along it. When the new
// waypoint is FIRST it copies the NEXT waypoint and steps BACKWARDS.
//
// Which length is used is not uniform, and the two directions differ:
//   stepping FORWARD  (:483-494)  extent | kSplineStepDefaultLength
//   stepping BACKWARD (:518-532)  kSplineStepDefaultLength | extent | kSplineStepUnit
// The forward branch has no unit case, and the backward unit case is itself
// asymmetric: `:530-532` assigns only `dx = x`, so the X step is the normalised
// axis while y and z keep their normalised values too - a unit vector - whereas
// the backward default case multiplies all three by kDefaultSegLength. Both are
// reproduced, including the forward case's long-double product versus the
// backward case's float product for the same nominal length.
enum SplineNewWaypointStep {
    kSplineStepModelExtent = 0,    // the neighbour's segment model X extent
    kSplineStepDefaultLength = 1,  // kSplineDefaultSegLength
    kSplineStepUnit = 2,           // the unscaled normalised axis (backward only)
};

void SplineNewWaypointPoint(const SplineObjWaypointParams& neighbour, SplineNewWaypointStep step,
                            double model_extent, bool step_forward, double out_point[3]);

// SplineObjWaypoint_Changed, src/tagitem/spline.c:774-786. With the container's
// "Snap Length" set, a waypoint with a predecessor is pulled onto the sphere of
// radius `end - start` (the segment model's X extent) around that predecessor,
// along the direction between them. The engine divides by the chord length
// unconditionally, so a waypoint authored on top of its predecessor gives a
// division by zero.
void SplineSnapPoint(const double previous[3], const double point[3], double model_extent,
                     double out_point[3]);

// Waypoint_AutoOrient, src/tagitem/spline.c:598-676. Builds a frame whose first
// column is the summed direction (previous->waypoint) + (waypoint->next), with
// the second and third columns derived orthogonally. `previous` / `next` are
// nullptr when the waypoint has no such neighbour. Only reached when the
// container's or the waypoint's "Automatic Orientation" is set AND the container
// is not rigid (`src/tagitem/spline.c:767-768`).
void SplineAutoOrientFrame(const double* previous, const double* point, const double* next,
                           float out_rot[10]);

// Waypoint_Straighten, src/tagitem/spline.c:686-756: a maximal run of consecutive
// "Align" waypoints is re-spaced evenly between the run's two ends and every
// waypoint in the run gets ONE frame built from the average step. It rewrites
// POSITIONS, not just orientations - Align moves waypoints.
//
// The two ends are anchors and keep their authored positions: the backward walk
// stops on the first NON-straight waypoint and counts it (`:699-709`), the forward
// walk counts only the straight members (`:710-713`), so `count` is exactly the
// number of steps between the two anchors and `step = (last - first) / count` is
// an even split. The engine never writes `last->point` (`:749-753`) precisely
// because `last` is the far anchor.
//
// `changed` is the index of the waypoint whose parameters changed. Returns false
// when the run has a single member (the engine returns early, `:714-715`) - so a
// lone "Align" waypoint at the very start of a rail is left completely untouched.
// The engine also moves each waypoint's collision cube and re-runs the segment
// setup; those need the world, so only the geometry is returned here.
bool SplineStraightenRun(std::vector<SplineObjWaypointParams>& waypoints, std::size_t changed);

// SplineObj_Changed, src/editobj/spline.c:296-315: the container's Position is a
// DELTA applied to every waypoint, rotated by the container's Gamma angle -
// `m = Matrix_SetRotZ(angle); p += m * (pos - oldPos)`. Not a pivot: the
// authored waypoint coordinates are absolute and the container's position is a
// remembered offset that is re-applied on every change.
void SplineContainerTranslate(std::vector<SplineObjWaypointParams>& waypoints,
                              const double delta[3], float angle);

// SplineObj_Changed, src/editobj/spline.c:256-295: the container's Gamma angle
// turns every waypoint about the FIRST waypoint - `c = first.point;
// w.point = RotZ(delta) * (w.point - c) + c` - and composes the same rotation
// into each waypoint's frame with the engine's own element order (see the
// implementation; it is `rot_waypoint * RotZ(delta)`, which is the POST-multiply
// a reader might not expect).
void SplineContainerRotate(std::vector<SplineObjWaypointParams>& waypoints, float delta_angle);

// ---------------------------------------------------------------------------
// The curve: a cubic Hermite with the endpoints' local X as tangents
// ---------------------------------------------------------------------------

// Hermite_Init, HOOK(0x518130), src/terrdraw/flare.c:44-52.
//   h(u) = m0*u + p0 + (2*p0 - 2*p1 + m0 + m1)*u^2 + (3*p1 - 3*p0 - 2*m0 - m1)*u^3
// There is no tension or bias term: the engine's curve is the plain cubic
// Hermite basis, and the only shaping is the tangent, which the caller scales.
struct HermiteBasis {
    double c[4];
};

HermiteBasis HermiteInit(double p0, double p1, double m0, double m1);
// Hermite_Eval, HOOK(0x518190), src/terrdraw/flare.c:54-61.
long double HermiteEval(const HermiteBasis& h, double u);
// Hermite_Slope, HOOK(0x5181C0), src/terrdraw/flare.c:63-69.
long double HermiteSlope(const HermiteBasis& h, double u);

// One joint of a span: `segPos[i]` (0x104) and `segRot[i]` (0x200) of
// `SplineObjSegment` (src/sound/soundtypes.h:57-58). `pos` is relative to the
// span's MID point, not to `from` (src/sound/segsetup.c:103-105, :121-123,
// :161-163). `rot` is a row-major 3x3 plus the engine's spare `count` word.
struct SplineJoint {
    float pos[3] = {0.0f, 0.0f, 0.0f};
    float rot[10] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
};

// One authored span: the segment that runs from waypoint `from` to waypoint `to`
// (SplineObjSegment_Setup, HOOK(0x4E4530), src/sound/segsetup.c:256-366).
struct SplineSpan {
    double from[3] = {0.0, 0.0, 0.0};
    double to[3] = {0.0, 0.0, 0.0};
    float rot_from[10] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    float rot_to[10] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    int segments = kSplineMaxSegments;
    bool rigid = false;
    bool reversed = false;
};

// setup_segments, src/sound/segsetup.c:88-199, plus the two clamps of
// SplineObjSegment_Setup (:267-270).
struct SplineSpanJoints {
    double mid[3] = {0.0, 0.0, 0.0};  // (from + to) / 2, the engine's `Vec3d mid`
    std::vector<SplineJoint> joints;  // segments + 1 entries
    // The engine's `SplineObjSegment.segLength[22]` (0x544, float). Entry 0
    // aliases the spare word of `invRot` and is never read; entries 1..segments
    // are |joints[i] - joints[i-1]|, and `tagitem/spline.c`'s SEG_LEN(s, i) reads
    // 0x548 + i*4, i.e. entry i+1: the sub-segment from joint i to joint i+1 is
    // seg_length[i + 1]. This off-by-one is the engine's and is load-bearing for
    // SplineObj_GetPoints' arc walk. Entry `segments + 1` is zeroed by
    // SplineObjSegment_Setup.
    std::vector<float> seg_length;  // segments + 2 entries
    float length = 0.0f;            // the float sum, SplineObjSegment.length (0xF8)
};

SplineSpanJoints BuildSplineSpanJoints(const SplineSpan& span);

// setup_rigid, src/sound/segsetup.c:203-251. Maps the model's own local X range
// [min_x, max_x] onto the chord: `k = 1 / (max_x - min_x)`, the chord direction
// scaled by k becomes the frame's first COLUMN (negated when the waypoint's
// "Flip" is set), and the other two columns come from RotZ(yaw) of
// Vec3_ToAngles(chord). Returns the longest of the three columns, which is the
// bound SplineObjSegment_Setup's radius loop scales by kRadiusScale.
//
// When from == to the engine returns 1.0f WITHOUT writing the frame
// (:209-211) - `out_rot` is left untouched. Reproduced.
//
// The engine also fills `o->invRot` here (`transpose10` + `Matrix_Invert`,
// :248-249). Nothing in the decompilation ever READS `invRot` - the only
// occurrences are its declaration (`src/sound/segsetup.c:46`) and these two
// writes - so this port does not produce it.
float BuildSplineRigidFrame(const double from[3], const double to[3], float min_x, float max_x,
                            bool reversed, float out_rot[10]);

// One LOD's render vertex array, as SplineObjSegment_Setup's extent scan reads
// it (`MeshHeader.data` -> `RigidMesh.vertexCount` / `.vertices`,
// `src/render/render.c:57-62`; stride 8 floats: position, normal, uv).
struct SplineExtentSource {
    const float* verts = nullptr;
    int vert_count = 0;
};

// The model's local X range, scanned by SplineObjSegment_Setup
// (src/sound/segsetup.c:277-293) over every LOD whose mesh format is 0:
//   minX = 0; maxX = 0; for each vertex v: if (!(*v >= minX)) minX = *v;
//                                         if (*v > maxX) maxX = *v;
// Note the asymmetry - the min test is the NaN-swallowing `!(*v >= minX)` form
// while the max test is a plain `>`, so a NaN X updates minX and never maxX. The
// range always contains the model origin because both start at zero.
//
// The scan reads the RENDER vertex array, while the vertices actually bent by
// SplineMesh_GetVerts are the COLLISION array (`MeshHeader.coll` ->
// `CollMesh.verts`, stride 4 floats, `src/magicobj/rigidcoll.c:46-54`). The two
// arrays are distinct in the engine's data model (MREF/MDAT versus CVTX); on the
// retail road models they are expected to agree on X, and that agreement is
// UNVERIFIED here because the editor's MEF reader was not exercised.
// Returns the count of vertices visited.
int SplineScanModelExtent(const SplineExtentSource* lods, int lod_count, int stride, float* out_min_x,
                          float* out_max_x);

// ---------------------------------------------------------------------------
// SplineMesh_GetVerts 0x49A050 (src/dxinit/spline.c:36-105)
// ---------------------------------------------------------------------------

// `o->start` / `o->end` are the model's local X extent (src/sound/soundtypes.h:55-56,
// written at src/sound/segsetup.c:277-293). They are the "longitudinal local
// axis derived from each segment extent" of issue #79: the model's X range is
// mapped onto the span, so the same road mesh stretches or compresses to fit
// whatever length the author drew, with no hardcoded axis or model id.
struct SplineMeshExtent {
    float start = 0.0f;
    float end = 0.0f;
};

// One collision mesh part. `verts` is the engine's `CollMesh.verts`
// (`float (*)[4]`, stride 4 floats - `src/magicobj/rigidcoll.c:51`,
// `src/magicobj/magicobjtypes.h:101`); only components 0..2 are read.
struct SplineMeshPart {
    const float* verts = nullptr;
    int vert_count = 0;
    int stride = 4;
};

struct SplineMeshBend {
    bool rigid = false;
    bool reversed = false;
    const float* rigid_rot = nullptr;  // the setup_rigid frame, required when rigid
    SplineMeshExtent extent{};
};

// The vertex bend, exactly:
//
//   rigid  (src/dxinit/spline.c:49-59): mid = (start + end) * 0.5, then
//          v' = rot * (v.x - mid, v.y, v.z).
//   curved (src/dxinit/spline.c:60-100): base = -(reversed ? end : start),
//          scale = segments / (end - start) (negated when reversed),
//          t = (base + v.x) * scale, k = trunc(t), k == segments -> k--,
//          f = clamp(t - k, 0, 1), and v' = lerp(joint[k] + rot[k]*(0, y, z),
//          joint[k+1] + rot[k+1]*(0, y, z), f).
//
// The result is 3 floats per vertex, in the SOURCE vertex order: output vertex i
// is input vertex i. Returns the vertex count, or 0 when the part does not fit
// `out_capacity` (the engine writes into a fixed 60000-float buffer with no
// check; see kSplineVertexCapacity).
//
// The output is LOCAL to the segment origin, which is the chord midpoint: the
// joint positions are `curve(t) - mid`, and the rigid frame's first column is
// `chord / (max_x - min_x)`, so the model's X range maps onto the chord CENTRED
// on that origin - a vertex at min_x lands at -chord/2 and one at max_x at
// +chord/2, and the caller adds the segment position.
//
// One deliberate divergence: the engine indexes `segRot[k]` / `segRot[k + 1]`
// with no bounds check, so a vertex whose X lies OUTSIDE the model's own X range
// (k < 0 or k >= segments) reads engine memory adjacent to the arrays. This port
// clamps k into [0, segments - 1] so the output stays defined, and returns 0 when
// the span has fewer than two joints (the engine would read segRot[-1]).
int SplineMeshGetVerts(const SplineSpanJoints& joints, const SplineMeshBend& bend,
                       const SplineMeshPart& part, float* out, int out_capacity);

// `x87_fistp` (src/dxinit/dxinit.h:22-27) is `fistpll` under the current x87
// rounding mode, and FPU_SetTruncate (HOOK 0x498460, src/dxinit/dxinit.c:75-81)
// sets that mode to round-toward-zero at startup (src/win/appmain.c:212). The
// curved branch's `t` is in [0, segments] for a non-degenerate model, so
// truncation equals floor there; the port keeps the truncating form because the
// engine's `k == segments -> k--` correction only makes sense with it.
int SplineTruncateToInt(long double value);

// ---------------------------------------------------------------------------
// The segment radius (SplineObjSegment_Setup, src/sound/segsetup.c:294-322)
// ---------------------------------------------------------------------------
//
// This is the bound the engine hands to DynCube_Move for the segment's collision
// cube, so it is the largest distance from the segment ORIGIN (the chord
// midpoint) that the placed road reaches.
//
// Rigid (:294-306): `radius = setup_rigid(...)` is the longest of the frame's
// three columns, `best` is the largest LOD radius over the segment model's LODs
// (`*(float *)(LOD_INFO(*models) + 0x78)`, walked BACKWARD - `for (k =
// LOD_COUNT; k > 0; k--, models++)` - with the NaN-swallowing `if (!(best >= r))`
// form so a NaN radius never wins), and the result is `best * radius *
// kRadiusScale`, left to right, evaluated in LONG DOUBLE because the oracle
// declares `long double best = kZero;` (:295) while `radius` is a float - the
// single narrowing happens on the assignment to the float field `o->radius`
// (:326). `best` is model data, so the caller supplies it.
float SplineRigidRadius(float best_lod_radius, float longest_frame_column);

// Curved (:307-322): for each of the first two mesh parts, bend the part's own
// vertices and take the largest |v| (the sum is ordered x, z, y - `v[0]*v[0] +
// v[2]*v[2] + v[1]*v[1]`), then floor the running maximum at 1.0f - and the floor
// is applied after EVERY part, not once at the end, so part 0 alone can raise the
// radius to 1 before part 1 is measured. The engine starts from 0 and uses the
// `if (!(radius > d)) radius = d;` form, so a NaN vertex distance never wins.
// `parts` is the part array and `part_count` the engine's 2.
float SplineBentRadius(const SplineSpanJoints& joints, const SplineMeshBend& bend,
                       const SplineMeshPart* parts, int part_count);

// ---------------------------------------------------------------------------
// SplineCache_Remove 0x49A010 / SplineCache_Reset 0x49A340
// ---------------------------------------------------------------------------
//
// The engine's cache is `void *gSplineCache[2]` at 0x720558 plus
// `int gSplineCacheData[4]` at 0x720560 (`src/dxinit/spline.c:114-117`), but
// SplineCache_Remove's loop is `for (i = 0; i < 1; i++)` (`src/dxinit/spline.c:26`)
// and SplineCache_Reset only clears slot 0 (`:109`), so exactly ONE slot is ever
// touched. Nothing in the decompilation ever WRITES a non-null slot, so in this
// build the cache never holds an entry and every Remove is a no-op; what the
// code actually establishes is the INV ALIDATION POLICY: a segment is removed
// from the cache after every SplineObjSegment_Setup (`src/sound/segsetup.c:327`,
// "SplineCache_Remove(o)" right after the joints, radius and collision trees are
// rebuilt), and the whole cache is reset once by Mesh3D_Init
// (`src/lod/mesh3d.c:468`). Reproduced as the engine has it, slot count and
// loop bound included.
class SplineCache {
public:
    static constexpr int kSlots = 2;      // src/dxinit/spline.c:114-115
    static constexpr int kLiveSlots = 1;  // the loop bound, src/dxinit/spline.c:26

    // SplineCache_Remove: clears the entry for `obj` and its data word.
    void Remove(const void* obj);
    // SplineCache_Reset: clears slot 0 only.
    void Reset();

    const void* Slot(int index) const;
    int Data(int index) const;

private:
    const void* slots_[kSlots] = {nullptr, nullptr};
    int data_[kSlots] = {0, 0};
};

// ---------------------------------------------------------------------------
// Float_Log2 0x499FF0 (src/dxinit/spline.c:13-20)
// ---------------------------------------------------------------------------
//
// NOT a spline rule - a fog-depth helper that shares the translation unit.
// "exponent and top three mantissa bits of a float >= 1 (8 * log2 x, roughly),
// else 0". The decompiled body reads the raw bit pattern through an `int`
// parameter and compares it SIGNED against 0x3F800000, so every negative float
// and every NaN with the sign bit set returns 0; a value in [0, 1) also returns
// 0. Callers in the oracle declare it as taking a `float`
// (`src/sector/light.c:14`, `src/glowmesh/terrainmesh.c:37`), which is what this
// signature follows.
unsigned FloatLog2(float value);

}  // namespace igi
