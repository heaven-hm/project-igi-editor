// spline_rules.h - SplineObj and SplineObjWaypoint curve and data modeling.
//
// Implements 3D spline data models, Hermite curve generation, coordinate
// transformations, vertex deformation along spline joints, and waypoint
// alignment for road, rail, and pathway entities.
//
// MIT License.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace igi {

// ---------------------------------------------------------------------------
// Units and engine constants
// ---------------------------------------------------------------------------

// World units: 4096 units per metre.
constexpr float kSplineUnitsPerMetre = 4096.0f;
constexpr float kSplineMetre = 1.0f / 4096.0f;

// Bounding radius reported for an empty container without waypoints.
constexpr float kSplineFar = 4096.0f;

// Allowed segment subdivision range per span.
constexpr int kSplineMinSegments = 2;
constexpr int kSplineMaxSegments = 20;

// Geometric constants for spline orientation and curves.
constexpr float kSplinePi = 0x1.921fb6p+1f;
constexpr float kSplineTwoPi = 0x1.921fb6p+2f;
constexpr float kSplineSmallAngle = 0.01f;
constexpr double kSplineTiny = 0.001;
constexpr float kSplineMinusTwo = -2.0f;
constexpr float kSplineRadiusScale = 0x1.19999ap+0f;

// Default length step for placing a new waypoint without a segment model.
constexpr float kSplineDefaultSegLength = 141314.9f;

// Maximum vertex output capacity for mesh bending buffers.
constexpr std::size_t kSplineVertexCapacity = 60000;

// Maximum collision parts per segment model.
constexpr int kSplineModelParts = 2;

// ---------------------------------------------------------------------------
// Parameter table definitions
// ---------------------------------------------------------------------------

constexpr std::size_t kSplineFirstParam = 3;

constexpr int kSplineObjParamCount = 10;
constexpr int kSplineObjTokenCount = 16;
constexpr int kSplineObjWaypointParamCount = 8;
constexpr int kSplineObjWaypointTokenCount = 12;

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

// Parameter metadata descriptor for authored spline attributes.
struct SplineParamDesc {
    const char* authored_name;  // User-facing parameter name
    const char* engine_field;   // Internal bound field name
    const char* type_name;      // Script type specification
    int token_count;            // Number of tokens consumed
    bool has_range;             // Whether a valid range is defined
    float range_min;
    float range_max;
};

// Returns metadata tables for SplineObj and SplineObjWaypoint parameters.
const SplineParamDesc* SplineObjParamTable();
const SplineParamDesc* SplineObjWaypointParamTable();

// ---------------------------------------------------------------------------
// Parsed authored structures
// ---------------------------------------------------------------------------

// Resolved configuration for a SplineObj container.
struct SplineObjParams {
    bool rigid = false;
    bool collide = false;
    bool snap = false;
    bool auto_orient = false;
    int max_segments = kSplineMaxSegments;
    int collision_lod = 0;
    double pos[3] = {0.0, 0.0, 0.0};
    float angle = kSplinePi;
    float colour1[3] = {1.0f, 1.0f, 1.0f};
    float colour2[3] = {0.0f, 0.0f, 0.0f};
};

// Resolved configuration for an individual SplineObjWaypoint.
struct SplineObjWaypointParams {
    float orientation[3] = {0.0f, 0.0f, 0.0f};  // Euler angles: pitch, yaw, roll
    double point[3] = {0.0, 0.0, 0.0};          // 3D position
    std::string model_name;                     // Placed model at waypoint
    std::string seg_model;                      // Segment model spanning to next waypoint
    int segments = kSplineMaxSegments;          // Subdivision steps
    bool straight = false;                      // Straight/aligned flag
    bool reversed = false;                      // Flipped orientation flag
    bool auto_orient = false;                   // Automatic orientation flag
};

bool ParseSplineObjParams(const std::vector<std::string>& arg_tokens, SplineObjParams* out);
bool ParseSplineObjWaypointParams(const std::vector<std::string>& arg_tokens,
                                  SplineObjWaypointParams* out);

// ---------------------------------------------------------------------------
// Derived evaluation rules
// ---------------------------------------------------------------------------

// Computes the effective subdivision segment count for a span.
int SplineEffectiveSegmentCount(int waypoint_segments, int max_segments, bool rigid);

// Determines whether a span should be evaluated as a linear rigid segment.
bool SplineSpanIsRigid(bool container_rigid, bool waypoint_straight);

// Extracts the longitudinal tangent vector from a 3x3 rotation frame scaled by distance.
void SplineTangent(const float rot[10], double distance, double out[3]);

// Constructs a row-major 3x3 matrix from Euler angles (Z * Y * X rotation).
void EngineSetEuler(float ax, float ay, float az, float out[10]);

// Decomposes a row-major 3x3 rotation matrix into normalized Euler angles [0, 2pi).
void EngineMatrixGetEuler(const float m[10], float* ax, float* ay, float* az);

// Builds a 3x3 rotation matrix around the Z axis.
void EngineSetRotZ(float a, float out[10]);

// Calculates pitch and yaw spherical angles from a direction vector.
void Vec3ToAngles(const float v[3], float* pitch, float* yaw);

// Converts a waypoint's Euler orientation into a 3x3 rotation matrix.
void SplineWaypointFrame(const SplineObjWaypointParams& waypoint, float out_rot[10]);

enum SplineNewWaypointStep {
    kSplineStepModelExtent = 0,
    kSplineStepDefaultLength = 1,
    kSplineStepUnit = 2,
};

// Computes the initial position for newly inserted waypoints.
void SplineNewWaypointPoint(const SplineObjWaypointParams& neighbour, SplineNewWaypointStep step,
                            double model_extent, bool step_forward, double out_point[3]);

// Snaps a waypoint's position along the vector to its predecessor to match model extent.
void SplineSnapPoint(const double previous[3], const double point[3], double model_extent,
                     double out_point[3]);

// Calculates an orthonormal orientation frame aligned to the tangent formed by neighboring waypoints.
void SplineAutoOrientFrame(const double* previous, const double* point, const double* next,
                           float out_rot[10]);

// Straightens a contiguous run of waypoints, re-spacing them uniformly between run boundaries.
bool SplineStraightenRun(std::vector<SplineObjWaypointParams>& waypoints, std::size_t changed);

// Applies container translational offset to all waypoints.
void SplineContainerTranslate(std::vector<SplineObjWaypointParams>& waypoints,
                              const double delta[3], float angle);

// Rotates all waypoints in a container around the first waypoint pivot by a delta angle.
void SplineContainerRotate(std::vector<SplineObjWaypointParams>& waypoints, float delta_angle);

// ---------------------------------------------------------------------------
// Hermite cubic spline evaluation
// ---------------------------------------------------------------------------

// Standard cubic Hermite polynomial basis coefficients:
// h(u) = c[1] + c[0]*u + c[2]*u^2 + c[3]*u^3
struct HermiteBasis {
    double c[4];
};

HermiteBasis HermiteInit(double start_point, double end_point, double start_tangent, double end_tangent);
long double HermiteEval(const HermiteBasis& hermite, double param_u);
long double HermiteSlope(const HermiteBasis& hermite, double param_u);

// Joint state describing position and orientation along a subdivided span.
struct SplineJoint {
    float pos[3] = {0.0f, 0.0f, 0.0f};
    float rot[10] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
};

// Input parameters defining an authored spline span between two endpoints.
struct SplineSpan {
    double from[3] = {0.0, 0.0, 0.0};
    double to[3] = {0.0, 0.0, 0.0};
    float rot_from[10] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    float rot_to[10] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    int segments = kSplineMaxSegments;
    bool rigid = false;
    bool reversed = false;
};

// Evaluated joints and lengths for a span.
struct SplineSpanJoints {
    double mid[3] = {0.0, 0.0, 0.0};
    std::vector<SplineJoint> joints;
    std::vector<float> seg_length;
    float length = 0.0f;
};

SplineSpanJoints BuildSplineSpanJoints(const SplineSpan& span);

// Computes the rigid rotation frame and bounding radius for a linear segment.
float BuildSplineRigidFrame(const double from[3], const double to[3], float min_x, float max_x,
                            bool reversed, float out_rot[10]);

struct SplineExtentSource {
    const float* verts = nullptr;
    int vert_count = 0;
};

// Scans vertex extents along the longitudinal X axis across LOD meshes.
int SplineScanModelExtent(const SplineExtentSource* lods, int lod_count, int stride, float* out_min_x,
                          float* out_max_x);

// ---------------------------------------------------------------------------
// Mesh deformation and vertex bending
// ---------------------------------------------------------------------------

struct SplineMeshExtent {
    float start = 0.0f;
    float end = 0.0f;
};

struct SplineMeshPart {
    const float* verts = nullptr;
    int vert_count = 0;
    int stride = 4;
};

struct SplineMeshBend {
    bool rigid = false;
    bool reversed = false;
    const float* rigid_rot = nullptr;
    SplineMeshExtent extent{};
};

// Deforms source mesh vertices along the spline span joints.
int SplineMeshGetVerts(const SplineSpanJoints& joints, const SplineMeshBend& bend,
                       const SplineMeshPart& part, float* out, int out_capacity);

// Truncates floating-point values to integer representation with safety guards.
int SplineTruncateToInt(long double value);

// Calculates the collision bounding radius for rigid and curved spans.
float SplineRigidRadius(float best_lod_radius, float longest_frame_column);
float SplineBentRadius(const SplineSpanJoints& joints, const SplineMeshBend& bend,
                       const SplineMeshPart* parts, int part_count);

// ---------------------------------------------------------------------------
// Spline cache management
// ---------------------------------------------------------------------------

class SplineCache {
public:
    static constexpr int kSlots = 2;
    static constexpr int kLiveSlots = 1;

    void Remove(const void* obj);
    void Reset();

    const void* Slot(int index) const;
    int Data(int index) const;

private:
    const void* slots_[kSlots] = {nullptr, nullptr};
    int data_[kSlots] = {0, 0};
};

// Floating point exponent helper.
unsigned FloatLog2(float value);

}  // namespace igi
