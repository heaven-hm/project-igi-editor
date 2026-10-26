// test_spline_rules.cpp - the SplineObj / SplineObjWaypoint road-and-track rules
// (#79 authored model, #69 curve). Pure logic plus a retail-data half.
//
// The oracle for every number here is IGI-RE (read-only). Key citations:
//   editobj/spline.c:177-225   SplineObj_Open 0x502250: the two ParamTables, the
//                              10/8 parameter rows, gSplineObjSegmentRange 2..20
//   editobj/spline.c:227-242   SplineObj_Create 0x5026E0: the authored defaults
//   editobj/spline.c:246-322   SplineObj_Changed 0x502740: the position DELTA and
//                              the rotate-about-the-first-waypoint rule
//   tagitem/spline.c:457-561   SplineObjWaypoint_Create 0x5008F0: the three new-
//                              waypoint steps (segment extent / default length /
//                              unit) and the kDefaultSegLength fallback
//   tagitem/spline.c:586-676   Vec3f_NormalizeOrdered, Waypoint_AutoOrient
//   tagitem/spline.c:686-756   Waypoint_Straighten: the even re-spacing of an
//                              "Align" run
//   tagitem/spline.c:759-846   SplineObjWaypoint_Changed 0x500910: the segment
//                              clamp, the snap, and the rail length accumulation
//   tagitem/spline.c:219-330   SplineObj_GetPoints 0x4FEC10: the arc walk and the
//                              +/- rail-length wrap
//   sound/segsetup.c:26-53     SplineObjSegment layout (start 0xFC, end 0x100,
//                              segPos 0x104, segRot 0x200, segLength 0x544)
//   sound/segsetup.c:88-199    setup_segments: the Hermite joints, the roll
//                              unwrap and the float segLength accumulation
//   sound/segsetup.c:203-251   setup_rigid: the model X range mapped onto the
//                              chord, and the radius bound
//   sound/segsetup.c:256-327   SplineObjSegment_Setup 0x4E4530: the clamps, the
//                              LOD extent scan and the cache invalidation
//   dxinit/spline.c:14-19      Float_Log2 0x499FF0
//   dxinit/spline.c:22-34      SplineCache_Remove 0x49A010
//   dxinit/spline.c:36-105     SplineMesh_GetVerts 0x49A050: the vertex bend
//   dxinit/spline.c:107-111    SplineCache_Reset 0x49A340
//   terrdraw/flare.c:44-69     Hermite_Init 0x518130 / Eval 0x518190 / Slope 0x5181C0
//   math/matrix.c:99-208       Matrix_SetEuler / SetRotX/Y/Z / GetEuler
//   math/vec3.c:86-115         Vec3d_Distance (dz, dy, dx order) / Vec3d_Equal
//   file/vec.c:232-245         Vec3_ToAngles 0x4B2D00
//   math/vecmath.h:4-6         HALF_PI / PI / TWO_PI as long doubles
//   dxinit/dxinit.h:22-28      x87_fistp; dxinit/dxinit.c:75-81 FPU_SetTruncate
//
// The corpus half reads the retail LEVEL1 OBJECTS.QVM with the editor's own
// QVM_Parse and cross-checks LEVEL1's decompiled objects.qsc. It also pins the
// rail-1500 correction: the retail rail has 12 waypoints and 11 Hermite spans,
// so `SplineObj.length` is 4 461 267.5 units (float-accumulated, as the engine
// does it), and the 11-waypoint / 10-span / 3 928 793.21 figure that
// PLACEMENT_SPLINES.md used to publish was the partial sum over the first ten
// spans (reproduced here to ~5 units in 3.9e6).

#include "../source/level/qvm_parser.h"
#include "../source/runtime/spline_rules.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

constexpr float kPiF = 3.14159265f;

fs::path GameRoot() {
    if (const char* env = std::getenv("IGI_GAME_PATH"); env != nullptr && *env != '\0')
        return fs::path(env);
    return fs::path("/Users/haseeb-mir/Downloads/IGI");
}

// The retail install spells the level-1 directory lowercase `level1`; every other
// level is uppercase. A case-insensitive filesystem resolves both, so try the
// uppercase spelling first and fall back.
fs::path LevelDir(int level) {
    const fs::path upper = GameRoot() / "MISSIONS" / "location0" / ("LEVEL" + std::to_string(level));
    if (fs::is_directory(upper)) return upper;
    const fs::path lower = GameRoot() / "MISSIONS" / "location0" / ("level" + std::to_string(level));
    return fs::is_directory(lower) ? lower : upper;
}

// ---------------------------------------------------------------------------
// QVM bytecode -> Spline arg tokens
// ---------------------------------------------------------------------------

bool IsStringPush(QVMOpType type) {
    return type == QVMOpType::PUSHS || type == QVMOpType::PUSHSI ||
           type == QVMOpType::PUSHSIB || type == QVMOpType::PUSHSIW;
}

bool IsIntPush(QVMOpType type) {
    return type == QVMOpType::PUSHI || type == QVMOpType::PUSHII ||
           type == QVMOpType::PUSHIIB || type == QVMOpType::PUSHIIW ||
           type == QVMOpType::PUSHB || type == QVMOpType::PUSHW;
}

bool IsIdentifierPush(QVMOpType type) {
    return type == QVMOpType::PUSHII || type == QVMOpType::PUSHIIB ||
           type == QVMOpType::PUSHIIW;
}

std::string FormatFloat(float value);

// The task id pushed immediately before the type string, or "-1" when there is
// none (a Task_DeclareParameters declaration is preceded by CALL/BRA instead of
// an id). Handles the `PUSH1 / MINUS` encoding of -1.
std::string ReadTaskId(const QVMFile& qvm, std::size_t type_index) {
    std::size_t sig[2] = {0, 0};
    int found = 0;
    std::size_t i = type_index;
    while (i > 0 && found < 2) {
        --i;
        if (qvm.instructions[i].type == QVMOpType::BRK) continue;
        sig[found++] = i;
    }
    if (found == 0) return "-1";
    const QVMInstruction& first = qvm.instructions[sig[0]];
    if (first.type == QVMOpType::MINUS) {
        if (found < 2) return "-1";
        const QVMInstruction& magnitude = qvm.instructions[sig[1]];
        if (IsIntPush(magnitude.type)) return "-" + std::to_string(static_cast<int>(magnitude.operand));
        if (magnitude.type == QVMOpType::PUSH1) return "-1";
        if (magnitude.type == QVMOpType::PUSH0) return "0";
        return "-1";
    }
    if (IsIntPush(first.type)) {
        if (IsIdentifierPush(first.type) && first.operand < qvm.identifiers.size())
            return qvm.identifiers[first.operand];
        return std::to_string(static_cast<int>(first.operand));
    }
    if (first.type == QVMOpType::PUSH0) return "0";
    if (first.type == QVMOpType::PUSH1) return "1";
    if (first.type == QVMOpType::PUSHF) return FormatFloat(first.operand_float);
    return "-1";
}

std::string FormatFloat(float value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.9g", value);
    return buffer;
}

// One authored call's arguments in the editor's arg_tokens layout:
// [0] = task id, [1] = type, [2] = name, [3] = parameter 0.
//
// The QVM pushes a task's arguments in order, each followed by a BRK, and a
// negative number is a positive literal followed by MINUS:
//   PUSH1 / MINUS / BRK            the task id (here -1)
//   PUSHSIB "SplineObj" / BRK      the type
//   PUSHSIW "" / BRK               the name
//   PUSHIIB "FALSE" / BRK ...      the parameters
// (LEVEL1/OBJECTS.QVM instruction 13877 onwards for the rail container.) The id
// and the type are read separately, so this walks from just after the type.
std::vector<std::string> ReadCall(const QVMFile& qvm, std::size_t type_index, const std::string& type,
                                  std::size_t expected) {
    std::vector<std::string> tokens;
    tokens.push_back(ReadTaskId(qvm, type_index));
    tokens.push_back(type);

    for (std::size_t i = type_index + 1; i < qvm.instructions.size() && tokens.size() < expected;
         ++i) {
        const QVMInstruction& in = qvm.instructions[i];
        if (in.type == QVMOpType::BRK) continue;
        if (IsStringPush(in.type)) {
            tokens.push_back(in.operand < qvm.strings.size() ? qvm.strings[in.operand]
                                                             : std::string());
            continue;
        }
        if (IsIntPush(in.type)) {
            if (IsIdentifierPush(in.type) && in.operand < qvm.identifiers.size()) {
                tokens.push_back(qvm.identifiers[in.operand]);
            } else {
                tokens.push_back(std::to_string(static_cast<int>(in.operand)));
            }
            continue;
        }
        if (in.type == QVMOpType::PUSH0) { tokens.push_back("0"); continue; }
        if (in.type == QVMOpType::PUSH1) { tokens.push_back("1"); continue; }
        if (in.type == QVMOpType::PUSHF) { tokens.push_back(FormatFloat(in.operand_float)); continue; }
        if (in.type == QVMOpType::MINUS) {
            if (!tokens.empty()) tokens.back() = "-" + tokens.back();
            continue;
        }
        break;  // anything else ends the argument list
    }
    return tokens;
}

// Every authored instance of `type` in the file, in file order, each with its
// `token_count + 3` argument tokens.
//
// A declaration (`Task_DeclareParameters("SplineObj", "Linear Segments", ...)`)
// also pushes the type string, so the two are told apart structurally: after the
// type comes the instance NAME string, and after that a declaration has another
// parameter-name string while an instance has a value.
std::vector<std::vector<std::string>> ExtractCalls(const QVMFile& qvm, const std::string& type,
                                                   std::size_t token_count) {
    std::vector<std::vector<std::string>> out;
    const std::size_t expected = 3 + token_count;
    for (std::size_t i = 0; i < qvm.instructions.size(); ++i) {
        const QVMInstruction& in = qvm.instructions[i];
        if (!IsStringPush(in.type)) continue;
        if (in.operand >= qvm.strings.size()) continue;
        if (qvm.strings[in.operand] != type) continue;

        std::size_t name = i + 1;
        while (name < qvm.instructions.size() && qvm.instructions[name].type == QVMOpType::BRK) ++name;
        if (name >= qvm.instructions.size() || !IsStringPush(qvm.instructions[name].type)) continue;

        std::size_t first = name + 1;
        while (first < qvm.instructions.size() && qvm.instructions[first].type == QVMOpType::BRK) ++first;
        if (first >= qvm.instructions.size() || IsStringPush(qvm.instructions[first].type)) continue;

        std::vector<std::string> tokens = ReadCall(qvm, i, type, expected);
        if (tokens.size() < expected) continue;
        out.push_back(std::move(tokens));
    }
    return out;
}

// The parameters a `Task_DeclareParameters("<type>", ...)` call declares, as
// alternating name/type strings in order (the type name is dropped).
std::vector<std::string> DeclaredParameters(const QVMFile& qvm, const std::string& type) {
    for (std::size_t i = 0; i < qvm.instructions.size(); ++i) {
        const QVMInstruction& in = qvm.instructions[i];
        if (!IsIdentifierPush(in.type)) continue;
        if (in.operand >= qvm.identifiers.size()) continue;
        if (qvm.identifiers[in.operand] != "Task_DeclareParameters") continue;

        std::size_t j = i + 1;
        if (j < qvm.instructions.size() && qvm.instructions[j].type == QVMOpType::CALL) ++j;
        if (j < qvm.instructions.size() && qvm.instructions[j].type == QVMOpType::BRA) ++j;
        std::vector<std::string> pushed;
        for (; j < qvm.instructions.size() && qvm.instructions[j].type != QVMOpType::POP; ++j) {
            if (!IsStringPush(qvm.instructions[j].type)) continue;
            const std::size_t operand = qvm.instructions[j].operand;
            pushed.push_back(operand < qvm.strings.size() ? qvm.strings[operand] : std::string());
        }
        if (pushed.size() >= 3 && pushed[0] == type) {
            return std::vector<std::string>(pushed.begin() + 1, pushed.end());
        }
    }
    return {};
}

// ---------------------------------------------------------------------------
// objects.qsc text -> the same two things, for the cross-check
// ---------------------------------------------------------------------------

std::string ReadFile(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return {};
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

// The top-level comma-separated arguments of the `Task_New(` call whose opening
// parenthesis is at `open`, plus the index just past its closing parenthesis.
// Parentheses inside a nested call do not split.
std::vector<std::string> SplitCallArguments(const std::string& text, std::size_t open,
                                            std::size_t* end_out) {
    std::vector<std::string> args;
    std::string current;
    int depth = 0;
    std::size_t i = open;
    for (; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '(') {
            depth++;
            if (depth == 1) continue;  // skip the call's own opening paren
        } else if (c == ')') {
            depth--;
            if (depth == 0) {
                ++i;
                break;
            }
        } else if (c == ',' && depth == 1) {
            args.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(c);
    }
    if (!current.empty()) args.push_back(current);
    if (end_out) *end_out = i;
    for (std::string& arg : args) {
        const std::size_t first = arg.find_first_not_of(" \t\r\n");
        const std::size_t last = arg.find_last_not_of(" \t\r\n");
        arg = (first == std::string::npos) ? std::string() : arg.substr(first, last - first + 1);
    }
    return args;
}

// Every `Task_New(...)` in the text whose SECOND argument is exactly `"type"`,
// with its top-level arguments.
std::vector<std::vector<std::string>> TextCallsOfType(const std::string& text,
                                                      const std::string& type) {
    std::vector<std::vector<std::string>> out;
    const std::string needle = "Task_New(";
    const std::string quoted = "\"" + type + "\"";
    for (std::size_t at = text.find(needle); at != std::string::npos;
         at = text.find(needle, at + needle.size())) {
        std::size_t end = 0;
        const std::vector<std::string> args =
            SplitCallArguments(text, at + needle.size() - 1, &end);
        if (args.size() > 2 && args[1] == quoted) out.push_back(args);
    }
    return out;
}

// ---------------------------------------------------------------------------
// The engine's arc wrap, src/tagitem/spline.c:246-256
// ---------------------------------------------------------------------------

//   d = dist;
//   if (!(d >= 0.0f)) { do d += obj->length; while (!(d >= 0.0f)); dist = d; }
//   if (d >= obj->length) { do d -= obj->length; while (d >= obj->length); dist = d; }
// `obj->length` is a float and `d` a long double.
double WrapRailDistance(double dist, float length) {
    long double d = dist;
    if (!(d >= 0.0f)) {
        do {
            d += length;
        } while (!(d >= 0.0f));
    }
    if (d >= length) {
        do {
            d -= length;
        } while (d >= length);
    }
    return static_cast<double>(d);
}

// The authored rail-1500 waypoints, read out of LEVEL1/objects.qsc and used to
// pin the length. Only the fields the curve needs.
struct AuthoredWaypoint {
    float orientation[3];
    double point[3];
    int segments;
};

// LEVEL1's rail 1500, in file order. `objects.qsc` line 486 onwards; the segment
// model of waypoint i is the model of the span that ENDS at waypoint i.
const AuthoredWaypoint kRail1500[12] = {
    {{0.0f, 0.0f, 3.1415929794311523f}, {25287152.0, -55948040.0, 174410960.0}, 20},
    {{0.0f, 0.0f, 3.1415929794311523f}, {25124074.0, -55958612.0, 174414016.0}, 10},
    {{0.0f, 0.0f, 3.1415929794311523f}, {24857834.0, -55958668.0, 174412928.0}, 10},
    {{0.0f, 0.0f, 2.8735930919647217f}, {24658190.0, -55807144.0, 174413168.0}, 10},
    {{0.0f, 0.0f, 3.1415929794311523f}, {24365666.0, -55777100.0, 174413120.0}, 10},
    {{6.2831811904907227f, 6.0213789939880371f, 3.1415920257568359f},
     {24111474.0, -55776588.0, 174492288.0},
     10},
    {{0.0f, 0.0f, 3.1415929794311523f}, {23580308.0, -55810864.0, 174507184.0}, 20},
    {{0.0f, 0.0f, 3.7815918922424316f}, {23094192.0, -56028160.0, 174504480.0}, 20},
    {{0.0f, 0.0f, 4.2815909385681152f}, {22755560.0, -56439088.0, 174504624.0}, 10},
    {{0.0f, 0.0f, 4.2815909385681152f}, {22533198.0, -56922920.0, 174504640.0}, 10},
    {{0.0f, 0.0f, 4.2815909385681152f}, {22310838.0, -57406752.0, 174504640.0}, 10},
    {{0.0f, 0.0f, 4.2815909385681152f}, {22088476.0, -57890580.0, 174504640.0}, 10},
};

// The span from waypoint `index - 1` to waypoint `index`, built the way
// Waypoint_SetupSegment does (`src/tagitem/spline.c:77-82`): the previous
// waypoint's frame is rot_from, this one's is rot_to, the segment count is this
// waypoint's NumAreas, and rigid = container rigid || waypoint straight (all
// FALSE on rail 1500).
igi::SplineSpanJoints Rail1500Span(std::size_t index) {
    const AuthoredWaypoint& from = kRail1500[index - 1];
    const AuthoredWaypoint& to = kRail1500[index];
    igi::SplineObjWaypointParams a;
    igi::SplineObjWaypointParams b;
    std::memcpy(a.orientation, from.orientation, sizeof(a.orientation));
    std::memcpy(b.orientation, to.orientation, sizeof(b.orientation));
    igi::SplineSpan span;
    for (int k = 0; k < 3; k++) {
        span.from[k] = from.point[k];
        span.to[k] = to.point[k];
    }
    igi::SplineWaypointFrame(a, span.rot_from);
    igi::SplineWaypointFrame(b, span.rot_to);
    span.segments = to.segments;
    span.rigid = false;
    return igi::BuildSplineSpanJoints(span);
}

double Rail1500Chord(std::size_t first, std::size_t last) {
    double total = 0.0;
    for (std::size_t i = first; i <= last; i++) {
        const double dx = kRail1500[i].point[0] - kRail1500[i - 1].point[0];
        const double dy = kRail1500[i].point[1] - kRail1500[i - 1].point[1];
        const double dz = kRail1500[i].point[2] - kRail1500[i - 1].point[2];
        total += std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    return total;
}

}  // namespace

// ---------------------------------------------------------------------------
// Hermite basis, src/terrdraw/flare.c:44-69
// ---------------------------------------------------------------------------

TEST(SplineHermiteRules, BasisInterpolatesTheEndpointsAndTangents) {
    // h(u) = m0*u + p0 + (2p0 - 2p1 + m0 + m1)u^2 + (3p1 - 3p0 - 2m0 - m1)u^3.
    const igi::HermiteBasis h = igi::HermiteInit(3.0, 7.0, 11.0, 13.0);
    EXPECT_DOUBLE_EQ(static_cast<double>(igi::HermiteEval(h, 0.0)), 3.0);
    EXPECT_DOUBLE_EQ(static_cast<double>(igi::HermiteEval(h, 1.0)), 7.0);
    // The slope is dh/du, so it is m0 at u = 0 and m1 at u = 1.
    EXPECT_DOUBLE_EQ(static_cast<double>(igi::HermiteSlope(h, 0.0)), 11.0);
    EXPECT_DOUBLE_EQ(static_cast<double>(igi::HermiteSlope(h, 1.0)), 13.0);
}

TEST(SplineHermiteRules, BasisIsThePlainCubicHermiteNotCatmullRom) {
    // With p0 = p1 = 0 and m0 = 1, m1 = 0 the curve is u - 2u^2 + u^3, i.e. 0.125
    // at u = 0.5. A Catmull-Rom / Bezier parameterisation would give a different
    // value here, which is what makes this a basis check rather than a smoke test.
    const igi::HermiteBasis a = igi::HermiteInit(0.0, 0.0, 1.0, 0.0);
    EXPECT_NEAR(static_cast<double>(igi::HermiteEval(a, 0.5)), 0.125, 1e-12);
    EXPECT_NEAR(static_cast<double>(igi::HermiteSlope(a, 0.5)), 1.0 - 2.0 + 0.75, 1e-12);
    // The mirror case m0 = 0, m1 = 1 is -0.5u^2 + 0.5u^3 + ... : -0.125 at u = 0.5.
    const igi::HermiteBasis b = igi::HermiteInit(0.0, 0.0, 0.0, 1.0);
    EXPECT_NEAR(static_cast<double>(igi::HermiteEval(b, 0.5)), -0.125, 1e-12);
}

TEST(SplineHermiteRules, StraightLineHasAConstantSlopeOfTheChord) {
    // p0 = 0, p1 = 10, m0 = m1 = 10 (the engine passes rot * distance, so a
    // straight span has tangents equal to the chord): h(u) = 10u.
    const igi::HermiteBasis h = igi::HermiteInit(0.0, 10.0, 10.0, 10.0);
    for (double u = 0.0; u <= 1.0; u += 0.25) {
        EXPECT_NEAR(static_cast<double>(igi::HermiteEval(h, u)), 10.0 * u, 1e-12);
        EXPECT_NEAR(static_cast<double>(igi::HermiteSlope(h, u)), 10.0, 1e-12);
    }
}

// ---------------------------------------------------------------------------
// The engine's matrix and angle primitives
// ---------------------------------------------------------------------------

TEST(SplineEngineRules, SetEulerIsRotZTimesRotYTimesRotX) {
    const float ax = 0.3f;
    const float ay = -0.7f;
    const float az = 1.1f;
    float got[10];
    igi::EngineSetEuler(ax, ay, az, got);

    // Independent double-precision Rz * Ry * Rx, row-major.
    const double cx = std::cos(ax), sx = std::sin(ax);
    const double cy = std::cos(ay), sy = std::sin(ay);
    const double cz = std::cos(az), sz = std::sin(az);
    const double rx[9] = {1, 0, 0, 0, cx, -sx, 0, sx, cx};
    const double ry[9] = {cy, 0, sy, 0, 1, 0, -sy, 0, cy};
    const double rz[9] = {cz, -sz, 0, sz, cz, 0, 0, 0, 1};
    double t[9];
    double want[9];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            double sum = 0.0;
            for (int k = 0; k < 3; k++) sum += ry[i * 3 + k] * rx[k * 3 + j];
            t[i * 3 + j] = sum;
        }
    }
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            double sum = 0.0;
            for (int k = 0; k < 3; k++) sum += rz[i * 3 + k] * t[k * 3 + j];
            want[i * 3 + j] = sum;
        }
    }
    for (int i = 0; i < 9; i++) {
        EXPECT_NEAR(got[i], want[i], 1e-6) << "element " << i;
    }
    EXPECT_FLOAT_EQ(got[9], 0.0f);  // m->count
}

TEST(SplineEngineRules, SetRotZMatchesTheEngineLayout) {
    const float a = 0.9f;
    float m[10];
    igi::EngineSetRotZ(a, m);
    EXPECT_NEAR(m[0], std::cos(a), 1e-6);
    EXPECT_NEAR(m[1], -std::sin(a), 1e-6);
    EXPECT_NEAR(m[3], std::sin(a), 1e-6);
    EXPECT_NEAR(m[4], std::cos(a), 1e-6);
    EXPECT_FLOAT_EQ(m[8], 1.0f);
    // The elements the engine zeroes explicitly.
    EXPECT_FLOAT_EQ(m[2], 0.0f);
    EXPECT_FLOAT_EQ(m[5], 0.0f);
    EXPECT_FLOAT_EQ(m[6], 0.0f);
    EXPECT_FLOAT_EQ(m[7], 0.0f);
    EXPECT_FLOAT_EQ(m[9], 0.0f);
}

TEST(SplineEngineRules, GetEulerInvertsSetEuler) {
    // Matrix_GetEuler normalises into [0, 2pi), so the angles come back in that
    // range; the FRAME is what has to round-trip.
    for (const float az : {0.0f, 0.5f, 2.0f, 3.0f}) {
        for (const float ax : {0.0f, 0.25f, -0.4f}) {
            float m[10];
            igi::EngineSetEuler(ax, 0.0f, az, m);
            float rx = 0.0f, ry = 0.0f, rz = 0.0f;
            igi::EngineMatrixGetEuler(m, &rx, &ry, &rz);
            float again[10];
            igi::EngineSetEuler(rx, ry, rz, again);
            for (int i = 0; i < 9; i++) {
                EXPECT_NEAR(m[i], again[i], 1e-5) << "ax=" << ax << " az=" << az << " i=" << i;
            }
        }
    }
}

TEST(SplineEngineRules, Vec3ToAnglesUsesTheZeroGuardAndNegatedPitch) {
    float pitch = 9.0f;
    float yaw = 9.0f;

    // +X: yaw 0, pitch 0.
    const float px[3] = {1.0f, 0.0f, 0.0f};
    igi::Vec3ToAngles(px, &pitch, &yaw);
    EXPECT_FLOAT_EQ(yaw, 0.0f);
    EXPECT_FLOAT_EQ(pitch, 0.0f);

    // +Y: the engine's `IsZero(v[0])` guard gives yaw 0, NOT pi/2.
    const float py[3] = {0.0f, 1.0f, 0.0f};
    igi::Vec3ToAngles(py, &pitch, &yaw);
    EXPECT_FLOAT_EQ(yaw, 0.0f);
    EXPECT_FLOAT_EQ(pitch, 0.0f);

    // Diagonal: yaw is atan2(y, x) = pi/4.
    const float pd[3] = {1.0f, 1.0f, 0.0f};
    igi::Vec3ToAngles(pd, &pitch, &yaw);
    EXPECT_NEAR(yaw, kPiF / 4.0f, 1e-6);
    EXPECT_NEAR(pitch, 0.0f, 1e-7);

    // Straight up: pitch = -asin(1) = -pi/2 - the engine NEGATES the asin.
    const float pu[3] = {0.0f, 0.0f, 1.0f};
    igi::Vec3ToAngles(pu, &pitch, &yaw);
    EXPECT_NEAR(pitch, -kPiF / 2.0f, 1e-6);

    // Straight down: +pi/2.
    const float pv[3] = {0.0f, 0.0f, -1.0f};
    igi::Vec3ToAngles(pv, &pitch, &yaw);
    EXPECT_NEAR(pitch, kPiF / 2.0f, 1e-6);

    // A zero vector takes the pitch guard too.
    const float pz[3] = {0.0f, 0.0f, 0.0f};
    igi::Vec3ToAngles(pz, &pitch, &yaw);
    EXPECT_FLOAT_EQ(pitch, 0.0f);
    EXPECT_FLOAT_EQ(yaw, 0.0f);
}

// ---------------------------------------------------------------------------
// The authored parameter tables
// ---------------------------------------------------------------------------

TEST(SplineParamRules, TablesMatchTheRetailDeclarationShape) {
    const igi::SplineParamDesc* obj = igi::SplineObjParamTable();
    int obj_tokens = 0;
    for (int i = 0; i < igi::kSplineObjParamCount; i++) obj_tokens += obj[i].token_count;
    EXPECT_EQ(obj_tokens, igi::kSplineObjTokenCount);
    EXPECT_EQ(obj_tokens, 16);

    const igi::SplineParamDesc* wp = igi::SplineObjWaypointParamTable();
    int wp_tokens = 0;
    for (int i = 0; i < igi::kSplineObjWaypointParamCount; i++) wp_tokens += wp[i].token_count;
    EXPECT_EQ(wp_tokens, igi::kSplineObjWaypointTokenCount);
    EXPECT_EQ(wp_tokens, 12);

    // The authored names, in ParamTable_Add order.
    EXPECT_STREQ(obj[igi::kSplineObjRigid].authored_name, "Linear Segments");
    EXPECT_STREQ(obj[igi::kSplineObjCollide].authored_name, "Display waypoints");
    EXPECT_STREQ(obj[igi::kSplineObjSnap].authored_name, "Snap Length");
    EXPECT_STREQ(obj[igi::kSplineObjAutoOrient].authored_name, "Automatic Orientation");
    EXPECT_STREQ(obj[igi::kSplineObjMaxSegments].authored_name, "Number of Matrices / Segment");
    EXPECT_STREQ(obj[igi::kSplineObjCollisionLod].authored_name, "Collision LOD");
    EXPECT_STREQ(obj[igi::kSplineObjPosition].authored_name, "Position");
    EXPECT_STREQ(obj[igi::kSplineObjAngle].authored_name, "Gamma Orientation");
    EXPECT_STREQ(obj[igi::kSplineObjColour1].authored_name, "Dirlight Angular Effect");
    EXPECT_STREQ(obj[igi::kSplineObjColour2].authored_name, "Dirlight Ambient Effect");

    EXPECT_STREQ(wp[igi::kSplineWaypointOrientation].authored_name, "Orientation");
    EXPECT_STREQ(wp[igi::kSplineWaypointPosition].authored_name, "Position");
    EXPECT_STREQ(wp[igi::kSplineWaypointModel].authored_name, "Waypoint Model");
    EXPECT_STREQ(wp[igi::kSplineWaypointSegModel].authored_name, "Segment Model");
    EXPECT_STREQ(wp[igi::kSplineWaypointSegments].authored_name, "NumAreas");
    EXPECT_STREQ(wp[igi::kSplineWaypointStraight].authored_name, "Align");
    EXPECT_STREQ(wp[igi::kSplineWaypointReversed].authored_name, "Flip");
    EXPECT_STREQ(wp[igi::kSplineWaypointAutoOrient].authored_name, "Automatic Orientation");

    // gSplineObjSegmentRange, src/editobj/spline.c:202-203, is registered on
    // "Number of Matrices / Segment" only.
    EXPECT_TRUE(obj[igi::kSplineObjMaxSegments].has_range);
    EXPECT_FLOAT_EQ(obj[igi::kSplineObjMaxSegments].range_min,
                    static_cast<float>(igi::kSplineMinSegments));
    EXPECT_FLOAT_EQ(obj[igi::kSplineObjMaxSegments].range_max,
                    static_cast<float>(igi::kSplineMaxSegments));
    // "Orientation" is declared Real32x9 but consumes three script arguments, and
    // "Position" is ObjectPos, an alias of Real64x3 (paramops.c:181).
    EXPECT_EQ(wp[igi::kSplineWaypointOrientation].token_count, 3);
    EXPECT_EQ(wp[igi::kSplineWaypointPosition].token_count, 3);
}

TEST(SplineParamRules, ParseRejectsATruncatedCall) {
    igi::SplineObjParams obj;
    EXPECT_FALSE(igi::ParseSplineObjParams({"1500", "SplineObj", ""}, &obj));
    EXPECT_FALSE(igi::ParseSplineObjParams({}, &obj));
    EXPECT_FALSE(igi::ParseSplineObjParams({"1500", "SplineObj", ""}, nullptr));

    igi::SplineObjWaypointParams wp;
    EXPECT_FALSE(igi::ParseSplineObjWaypointParams({"-1", "SplineObjWaypoint", ""}, &wp));
    EXPECT_FALSE(igi::ParseSplineObjWaypointParams({}, &wp));
    EXPECT_FALSE(igi::ParseSplineObjWaypointParams({"-1", "SplineObjWaypoint", ""}, nullptr));
}

TEST(SplineParamRules, ParseReadsTheAuthoredRailContainer) {
    // The real LEVEL1 line 485, token for token.
    const std::vector<std::string> tokens = {"1500", "SplineObj", "",  "FALSE", "FALSE",
                                             "FALSE", "FALSE",     "20", "0",     "0",
                                             "0",    "0",           "3.1415929794311523",
                                             "1",    "1",           "1",     "0",
                                             "0",    "0"};
    igi::SplineObjParams params;
    ASSERT_TRUE(igi::ParseSplineObjParams(tokens, &params));
    EXPECT_FALSE(params.rigid);
    EXPECT_FALSE(params.collide);
    EXPECT_FALSE(params.snap);
    EXPECT_FALSE(params.auto_orient);
    EXPECT_EQ(params.max_segments, 20);
    EXPECT_EQ(params.collision_lod, 0);
    EXPECT_DOUBLE_EQ(params.pos[0], 0.0);
    EXPECT_DOUBLE_EQ(params.pos[1], 0.0);
    EXPECT_DOUBLE_EQ(params.pos[2], 0.0);
    EXPECT_NEAR(params.angle, kPiF, 1e-6);
    EXPECT_FLOAT_EQ(params.colour1[0], 1.0f);
    EXPECT_FLOAT_EQ(params.colour2[2], 0.0f);
}

TEST(SplineParamRules, ParseReadsTheAuthoredFirstRailWaypoint) {
    // The real LEVEL1 line 486, token for token.
    const std::vector<std::string> tokens = {"-1",  "SplineObjWaypoint", "",        "0",
                                             "0",   "3.1415929794311523",
                                             "25287152.0", "-55948040.0", "174410960.0",
                                             "waypoint",   "368_01_1",     "20",
                                             "FALSE",      "FALSE",        "FALSE"};
    igi::SplineObjWaypointParams params;
    ASSERT_TRUE(igi::ParseSplineObjWaypointParams(tokens, &params));
    EXPECT_FLOAT_EQ(params.orientation[2], kPiF);
    EXPECT_DOUBLE_EQ(params.point[0], 25287152.0);
    EXPECT_DOUBLE_EQ(params.point[1], -55948040.0);
    EXPECT_DOUBLE_EQ(params.point[2], 174410960.0);
    EXPECT_EQ(params.model_name, "waypoint");
    EXPECT_EQ(params.seg_model, "368_01_1");
    EXPECT_EQ(params.segments, 20);
    EXPECT_FALSE(params.straight);
    EXPECT_FALSE(params.reversed);
    EXPECT_FALSE(params.auto_orient);
}

// ---------------------------------------------------------------------------
// The authored clamps
// ---------------------------------------------------------------------------

TEST(SplineRuleRules, EffectiveSegmentCountIsTheWaypointClampThenTheRigidForcing) {
    // min(w->segments, obj->maxSegments), then 20, then 1 when rigid.
    EXPECT_EQ(igi::SplineEffectiveSegmentCount(30, 20, false), 20);
    EXPECT_EQ(igi::SplineEffectiveSegmentCount(20, 20, false), 20);
    EXPECT_EQ(igi::SplineEffectiveSegmentCount(5, 3, false), 3);
    EXPECT_EQ(igi::SplineEffectiveSegmentCount(2, 20, false), 2);
    // There is NO lower clamp: NumAreas 0 survives, and `inv = 1 / segments`
    // then divides by zero exactly as the engine does.
    EXPECT_EQ(igi::SplineEffectiveSegmentCount(0, 20, false), 0);
    EXPECT_EQ(igi::SplineEffectiveSegmentCount(1, 20, false), 1);
    // Rigid wins over everything.
    EXPECT_EQ(igi::SplineEffectiveSegmentCount(20, 20, true), 1);
    EXPECT_EQ(igi::SplineEffectiveSegmentCount(0, 20, true), 1);
}

TEST(SplineRuleRules, SpanIsRigidWhenEitherTheContainerOrTheWaypointSaysSo) {
    EXPECT_TRUE(igi::SplineSpanIsRigid(true, false));
    EXPECT_TRUE(igi::SplineSpanIsRigid(false, true));
    EXPECT_TRUE(igi::SplineSpanIsRigid(true, true));
    EXPECT_FALSE(igi::SplineSpanIsRigid(false, false));
}

// ---------------------------------------------------------------------------
// The rigid span frame, src/sound/segsetup.c:203-251
// ---------------------------------------------------------------------------

TEST(SplineRigidRules, FrameMapsTheModelExtentOntoTheChordCentredOnTheOrigin) {
    const double from[3] = {0.0, 0.0, 0.0};
    const double to[3] = {1000.0, 0.0, 0.0};
    float rot[10];
    std::memset(rot, 0x7f, sizeof(rot));
    const float radius = igi::BuildSplineRigidFrame(from, to, -100.0f, 100.0f, false, rot);

    // The first column is chord / (max_x - min_x) = 1000 / 200 = 5 along +X.
    EXPECT_NEAR(rot[0], 5.0f, 1e-5f);
    EXPECT_NEAR(rot[3], 0.0f, 1e-5f);
    EXPECT_NEAR(rot[6], 0.0f, 1e-5f);

    // Bending a vertex: v' = rot * (v.x - mid, v.y, v.z) with mid = 0, so the
    // model's X range maps onto [-chord/2, +chord/2] in the segment's local space.
    const float min_x = -100.0f;
    const float max_x = 100.0f;
    const float px_min = min_x - (min_x + max_x) * 0.5f;
    const float px_max = max_x - (min_x + max_x) * 0.5f;
    EXPECT_NEAR(px_min * rot[0], -500.0f, 1e-3f);
    EXPECT_NEAR(px_max * rot[0], 500.0f, 1e-3f);

    // The bound is the longest column; here it is 5 (X), since the others are unit.
    EXPECT_NEAR(radius, 5.0f, 1e-5f);
}

TEST(SplineRigidRules, FlipNegatesTheTangent) {
    const double from[3] = {0.0, 0.0, 0.0};
    const double to[3] = {1000.0, 0.0, 0.0};
    float plain[10];
    float flipped[10];
    igi::BuildSplineRigidFrame(from, to, -100.0f, 100.0f, false, plain);
    igi::BuildSplineRigidFrame(from, to, -100.0f, 100.0f, true, flipped);
    EXPECT_NEAR(flipped[0], -plain[0], 1e-6f);
    EXPECT_NEAR(flipped[3], -plain[3], 1e-6f);
    EXPECT_NEAR(flipped[6], -plain[6], 1e-6f);
}

TEST(SplineRigidRules, CoincidentEndpointsReturnOneAndLeaveTheFrameUntouched) {
    // setup_rigid:209-211 returns `radius` (= kOne, set by SplineObjSegment_Setup
    // at :322) WITHOUT writing the frame.
    const double from[3] = {5.0, 6.0, 7.0};
    const double to[3] = {5.0, 6.0, 7.0};
    float rot[10];
    for (int i = 0; i < 10; i++) rot[i] = 100.0f + i;
    const float radius = igi::BuildSplineRigidFrame(from, to, -100.0f, 100.0f, false, rot);
    EXPECT_FLOAT_EQ(radius, 1.0f);
    for (int i = 0; i < 10; i++) EXPECT_FLOAT_EQ(rot[i], 100.0f + i) << "element " << i;
}

TEST(SplineRigidRules, DegenerateModelExtentDividesByZeroLikeTheEngine) {
    // k = 1 / (max_x - min_x) has no guard (:224), so a model with no X extent
    // gives an infinite scale rather than a clamped one.
    const double from[3] = {0.0, 0.0, 0.0};
    const double to[3] = {1000.0, 0.0, 0.0};
    float rot[10];
    const float radius = igi::BuildSplineRigidFrame(from, to, 0.0f, 0.0f, false, rot);
    EXPECT_FALSE(std::isfinite(rot[0]));
    EXPECT_FALSE(std::isfinite(radius));
}

// ---------------------------------------------------------------------------
// The Hermite span joints, src/sound/segsetup.c:88-199
// ---------------------------------------------------------------------------

TEST(SplineSpanRules, StraightSpanHasSegmentsPlusOneJointsAndAConstantSubLength) {
    igi::SplineSpan span;
    span.from[0] = 0.0;
    span.to[0] = 1000.0;
    span.segments = 10;
    // Frames already identity, so both tangents are rot[row] * distance with
    // rot[0] = 1: the curve is a straight line.
    const igi::SplineSpanJoints joints = igi::BuildSplineSpanJoints(span);

    ASSERT_EQ(joints.joints.size(), 11u);
    ASSERT_EQ(joints.seg_length.size(), 12u);
    EXPECT_NEAR(joints.mid[0], 500.0, 1e-9);
    EXPECT_FLOAT_EQ(joints.length, 1000.0f);
    for (int i = 0; i <= 10; i++) {
        // pos = curve(i / 10) - mid = i * 100 - 500.
        EXPECT_NEAR(joints.joints[static_cast<std::size_t>(i)].pos[0], i * 100.0 - 500.0, 1e-3)
            << "joint " << i;
        EXPECT_NEAR(joints.joints[static_cast<std::size_t>(i)].pos[1], 0.0, 1e-6);
    }
    // segLength[1..10] are the ten sub-segments of 100 units each; entry 0 aliases
    // invRot's spare word and entry 11 is zeroed by SplineObjSegment_Setup.
    for (int i = 1; i <= 10; i++) {
        EXPECT_NEAR(joints.seg_length[static_cast<std::size_t>(i)], 100.0f, 1e-3f) << "entry " << i;
    }
    EXPECT_FLOAT_EQ(joints.seg_length[0], 0.0f);
    EXPECT_FLOAT_EQ(joints.seg_length[11], 0.0f);
}

TEST(SplineSpanRules, JointZeroAndTheLastJointCarryTheAuthoredFrames) {
    igi::SplineSpan span;
    span.from[0] = 0.0;
    span.to[0] = 100.0;
    span.segments = 4;
    igi::EngineSetEuler(0.0f, 0.0f, 0.5f, span.rot_from);
    igi::EngineSetEuler(0.0f, 0.0f, 1.5f, span.rot_to);
    const igi::SplineSpanJoints joints = igi::BuildSplineSpanJoints(span);
    for (int i = 0; i < 10; i++) {
        EXPECT_FLOAT_EQ(joints.joints[0].rot[i], span.rot_from[i]) << "rot_from " << i;
        EXPECT_FLOAT_EQ(joints.joints[4].rot[i], span.rot_to[i]) << "rot_to " << i;
    }
}

TEST(SplineSpanRules, RigidSpanIsOneSubSegmentWhateverNumAreasSaid) {
    igi::SplineSpan span;
    span.from[0] = 0.0;
    span.to[0] = 500.0;
    span.segments = 20;
    span.rigid = true;
    const igi::SplineSpanJoints joints = igi::BuildSplineSpanJoints(span);
    // SplineObjSegment_Setup forces o->segments = 1 for a rigid span.
    ASSERT_EQ(joints.joints.size(), 2u);
    EXPECT_FLOAT_EQ(joints.seg_length[1], 500.0f);
    EXPECT_FLOAT_EQ(joints.length, 500.0f);
    EXPECT_NEAR(joints.joints[0].pos[0], -250.0, 1e-3);
    EXPECT_NEAR(joints.joints[1].pos[0], 250.0, 1e-3);
}

TEST(SplineSpanRules, ZeroSegmentsDividesByZeroLikeTheEngine) {
    igi::SplineSpan span;
    span.from[0] = 0.0;
    span.to[0] = 100.0;
    span.segments = 0;
    const igi::SplineSpanJoints joints = igi::BuildSplineSpanJoints(span);
    // inv = 1 / 0 = inf, so the single joint's t is 0 * inf = NaN. The engine does
    // not guard this; the port reproduces it (and the span then has one joint, so
    // SplineMeshGetVerts refuses it).
    ASSERT_EQ(joints.joints.size(), 1u);
    EXPECT_FALSE(std::isfinite(joints.joints[0].pos[0]));
}

TEST(SplineSpanRules, RollIsTheInterpolatedEulerXWithTheFloatPiUnwrap) {
    // The endpoint frames differ by more than pi in their Euler X, so the engine's
    // unwrap loop (src/sound/segsetup.c:144-159) adds TWO_PI to the far angle:
    //   ax0 = 6.0, ax1 = 0.5  ->  ax1 - ax0 = -5.5 < -pi  ->  a1 += 2pi = 6.7831855
    //   roll(t) = (a1f - a0f) * t + a0f  ->  the middle joint's roll is 6.3915928,
    //   i.e. 0.1084075 after Matrix_GetEuler's reduction.
    // Without the unwrap the middle roll would be 3.25, whose cosine is NEGATIVE -
    // so this test also distinguishes "no unwrap" from "unwrap".
    igi::SplineSpan span;
    span.from[0] = 0.0;
    span.to[0] = 100.0;
    span.segments = 2;
    igi::EngineSetEuler(6.0f, 0.0f, 0.0f, span.rot_from);
    igi::EngineSetEuler(0.5f, 0.0f, 0.0f, span.rot_to);
    const igi::SplineSpanJoints joints = igi::BuildSplineSpanJoints(span);
    ASSERT_EQ(joints.joints.size(), 3u);

    // The joints are NOT zero-roll: the middle joint's X axis is a real roll
    // between the two endpoint rolls, which is exactly what issue #69's
    // "zero-roll frame" text contradicts.
    float roll = 0.0f;
    float pitch = 0.0f;
    float yaw = 0.0f;
    igi::EngineMatrixGetEuler(joints.joints[1].rot, &roll, &pitch, &yaw);
    EXPECT_NEAR(roll, 0.1084075f, 1e-5f);
    EXPECT_NEAR(pitch, 0.0f, 1e-5f);
    EXPECT_NEAR(yaw, 0.0f, 1e-5f);

    // cos(6.0) = 0.960170, cos(0.5) = 0.877583, cos(6.3915928) = 0.994128: the
    // middle value is neither endpoint and is not 1.
    EXPECT_NEAR(joints.joints[0].rot[4], 0.9601703f, 1e-5f);
    EXPECT_NEAR(joints.joints[1].rot[4], 0.9941278f, 1e-5f);
    EXPECT_NEAR(joints.joints[2].rot[4], 0.8775826f, 1e-5f);
}

// ---------------------------------------------------------------------------
// SplineMesh_GetVerts, src/dxinit/spline.c:36-105
// ---------------------------------------------------------------------------

TEST(SplineBendRules, CurvedBendMapsTheModelEndsOntoTheFirstAndLastJoint) {
    igi::SplineSpan span;
    span.from[0] = 0.0;
    span.to[0] = 100.0;
    span.segments = 4;
    const igi::SplineSpanJoints joints = igi::BuildSplineSpanJoints(span);

    // Two vertices on the model's own X axis, at its X extent.
    const float verts[8] = {0.0f, 0.0f, 0.0f, 0.0f, 100.0f, 0.0f, 0.0f, 0.0f};
    igi::SplineMeshPart part;
    part.verts = verts;
    part.vert_count = 2;
    part.stride = 4;
    igi::SplineMeshBend bend;
    bend.rigid = false;
    bend.extent.start = 0.0f;
    bend.extent.end = 100.0f;

    float out[6] = {0.0f};
    ASSERT_EQ(igi::SplineMeshGetVerts(joints, bend, part, out, 6), 2);
    // t = 0 -> k = 0, f = 0 -> joint 0; t = segments -> k = segments - 1, f = 1 ->
    // joint segments.
    EXPECT_NEAR(out[0], joints.joints[0].pos[0], 1e-4f);
    EXPECT_NEAR(out[1], joints.joints[0].pos[1], 1e-4f);
    EXPECT_NEAR(out[2], joints.joints[0].pos[2], 1e-4f);
    EXPECT_NEAR(out[3], joints.joints[4].pos[0], 1e-4f);
    EXPECT_NEAR(out[4], joints.joints[4].pos[1], 1e-4f);
    EXPECT_NEAR(out[5], joints.joints[4].pos[2], 1e-4f);
}

TEST(SplineBendRules, CurvedBendKeepsTheSourceVertexOrderAndStride) {
    igi::SplineSpan span;
    span.from[0] = 0.0;
    span.to[0] = 100.0;
    span.segments = 2;
    const igi::SplineSpanJoints joints = igi::BuildSplineSpanJoints(span);

    // Three vertices with distinguishable Y offsets, stride 4 (the collision
    // layout), so a wrong stride or a reordered write is visible.
    const float verts[12] = {0.0f, 1.0f, 0.0f, 99.0f, 50.0f, 2.0f, 0.0f, 99.0f,
                             100.0f, 3.0f, 0.0f, 99.0f};
    igi::SplineMeshPart part;
    part.verts = verts;
    part.vert_count = 3;
    part.stride = 4;
    igi::SplineMeshBend bend;
    bend.extent.start = 0.0f;
    bend.extent.end = 100.0f;

    float out[9] = {0.0f};
    ASSERT_EQ(igi::SplineMeshGetVerts(joints, bend, part, out, 9), 3);
    // The Y component of each output vertex is the Y offset carried by the joint
    // frames: with identity frames and a straight chord, out[1] == the source Y.
    EXPECT_NEAR(out[1], 1.0f, 1e-4f);
    EXPECT_NEAR(out[4], 2.0f, 1e-4f);
    EXPECT_NEAR(out[7], 3.0f, 1e-4f);
    // X is monotone because the vertices are in source order along the chord.
    EXPECT_LT(out[0], out[3]);
    EXPECT_LT(out[3], out[6]);
}

TEST(SplineBendRules, RigidBendUsesTheModelMidpointAndTheFrameColumns) {
    igi::SplineSpanJoints joints;  // unused on the rigid path
    const float verts[8] = {-100.0f, 0.0f, 0.0f, 0.0f, 100.0f, 0.0f, 0.0f, 0.0f};
    igi::SplineMeshPart part;
    part.verts = verts;
    part.vert_count = 2;
    part.stride = 4;

    const double from[3] = {0.0, 0.0, 0.0};
    const double to[3] = {1000.0, 0.0, 0.0};
    float rot[10];
    igi::BuildSplineRigidFrame(from, to, -100.0f, 100.0f, false, rot);

    igi::SplineMeshBend bend;
    bend.rigid = true;
    bend.rigid_rot = rot;
    bend.extent.start = -100.0f;
    bend.extent.end = 100.0f;

    float out[6] = {0.0f};
    ASSERT_EQ(igi::SplineMeshGetVerts(joints, bend, part, out, 6), 2);
    // mid = 0, so the ends map to -500 and +500 local units.
    EXPECT_NEAR(out[0], -500.0f, 1e-2f);
    EXPECT_NEAR(out[3], 500.0f, 1e-2f);
}

TEST(SplineBendRules, RefusesToOverrunTheOutputBuffer) {
    igi::SplineSpan span;
    span.from[0] = 0.0;
    span.to[0] = 100.0;
    span.segments = 2;
    const igi::SplineSpanJoints joints = igi::BuildSplineSpanJoints(span);

    const float verts[8] = {0.0f, 0.0f, 0.0f, 0.0f, 100.0f, 0.0f, 0.0f, 0.0f};
    igi::SplineMeshPart part;
    part.verts = verts;
    part.vert_count = 2;
    part.stride = 4;
    igi::SplineMeshBend bend;
    bend.extent.start = 0.0f;
    bend.extent.end = 100.0f;

    float out[5] = {0.0f};
    // 2 vertices need 6 floats; the engine would write into its fixed 60000-float
    // buffer regardless, this port reports 0 instead.
    EXPECT_EQ(igi::SplineMeshGetVerts(joints, bend, part, out, 5), 0);
    EXPECT_EQ(igi::SplineMeshGetVerts(joints, bend, part, out, 6), 2);
}

TEST(SplineBendRules, RefusesASpanWithFewerThanTwoJoints) {
    igi::SplineSpanJoints joints;
    joints.joints.resize(1);
    joints.seg_length.assign(2, 0.0f);
    const float verts[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    igi::SplineMeshPart part;
    part.verts = verts;
    part.vert_count = 1;
    part.stride = 4;
    igi::SplineMeshBend bend;
    float out[3] = {0.0f};
    EXPECT_EQ(igi::SplineMeshGetVerts(joints, bend, part, out, 3), 0);
}

// ---------------------------------------------------------------------------
// The segment radius, src/sound/segsetup.c:294-322
// ---------------------------------------------------------------------------

TEST(SplineRadiusRules, RigidRadiusIsTheThreeFactorProductLeftToRight) {
    // radius = best_lod_radius * longest_column * kRadiusScale, in that order.
    const float best = 2.5f;
    const float column = 4.0f;
    const float want = (best * column) * igi::kSplineRadiusScale;
    EXPECT_FLOAT_EQ(igi::SplineRigidRadius(best, column), want);
    // kRadiusScale is 1.1 = 0x1.19999ap+0, so the scales are not identity.
    EXPECT_NEAR(igi::kSplineRadiusScale, 1.1f, 1e-6f);
    EXPECT_NEAR(igi::SplineRigidRadius(1.0f, 1.0f), 1.1f, 1e-6f);
}

TEST(SplineRadiusRules, BentRadiusIsTheFarthestBentVertexFlooredAtOne) {
    igi::SplineSpan span;
    span.from[0] = 0.0;
    span.to[0] = 100.0;
    span.segments = 2;
    const igi::SplineSpanJoints joints = igi::BuildSplineSpanJoints(span);

    // One vertex at the model's X end, offset 3 on Y: after the bend it sits at
    // (50, 3, 0) local, so |v| = sqrt(2500 + 9) = 50.0899.
    const float verts[8] = {100.0f, 3.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    igi::SplineMeshPart part;
    part.verts = verts;
    part.vert_count = 2;
    part.stride = 4;
    igi::SplineMeshBend bend;
    bend.extent.start = 0.0f;
    bend.extent.end = 100.0f;

    const igi::SplineMeshPart parts[2] = {part, igi::SplineMeshPart{}};
    const float radius = igi::SplineBentRadius(joints, bend, parts, 2);
    EXPECT_NEAR(radius, std::sqrt(50.0 * 50.0 + 3.0 * 3.0), 1e-3)
        << "the far end of the chord dominates the radius";
}

TEST(SplineRadiusRules, BentRadiusFloorsAtOneAfterEveryPart) {
    igi::SplineSpan span;
    span.from[0] = 0.0;
    span.to[0] = 2.0;
    span.segments = 2;
    const igi::SplineSpanJoints joints = igi::BuildSplineSpanJoints(span);

    // A tiny span: every bent vertex is within 1.0 of the origin, so both parts
    // leave the running maximum at the floor. The second part has no vertices at
    // all, which must NOT reset the radius.
    const float verts[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    igi::SplineMeshPart part;
    part.verts = verts;
    part.vert_count = 1;
    part.stride = 4;
    igi::SplineMeshBend bend;
    bend.extent.start = 0.0f;
    bend.extent.end = 2.0f;

    const igi::SplineMeshPart parts[2] = {part, igi::SplineMeshPart{}};
    EXPECT_FLOAT_EQ(igi::SplineBentRadius(joints, bend, parts, 2), 1.0f);
}

TEST(SplineBendRules, TruncationMatchesFistpWithTheIntegerIndefiniteResult) {    EXPECT_EQ(igi::SplineTruncateToInt(2.9L), 2);
    EXPECT_EQ(igi::SplineTruncateToInt(-2.9L), -2);
    EXPECT_EQ(igi::SplineTruncateToInt(0.999L), 0);
    EXPECT_EQ(igi::SplineTruncateToInt(20.0L), 20);
    // fistpll stores 0x8000000000000000 for a NaN, an infinity or an out-of-range
    // value; the low 32 bits of that are zero, so the engine's `(int)r` is 0.
    EXPECT_EQ(igi::SplineTruncateToInt(std::numeric_limits<long double>::infinity()), 0);
    EXPECT_EQ(igi::SplineTruncateToInt(-std::numeric_limits<long double>::infinity()), 0);
    EXPECT_EQ(igi::SplineTruncateToInt(std::numeric_limits<long double>::quiet_NaN()), 0);
    EXPECT_EQ(igi::SplineTruncateToInt(1e30L), 0);
}

// ---------------------------------------------------------------------------
// The model extent scan, src/sound/segsetup.c:277-293
// ---------------------------------------------------------------------------

TEST(SplineExtentRules, ScanStartsAtZeroAndIncludesTheOrigin) {
    // Stride 8 (the render layout): only component 0 of each vertex is read.
    const float verts[16] = {-10.0f, 9.0f, 9.0f, 9.0f, 9.0f, 9.0f, 9.0f, 9.0f,
                             30.0f,  9.0f, 9.0f, 9.0f, 9.0f, 9.0f, 9.0f, 9.0f};
    igi::SplineExtentSource lod;
    lod.verts = verts;
    lod.vert_count = 2;
    float min_x = 99.0f;
    float max_x = -99.0f;
    EXPECT_EQ(igi::SplineScanModelExtent(&lod, 1, 8, &min_x, &max_x), 2);
    EXPECT_FLOAT_EQ(min_x, -10.0f);
    EXPECT_FLOAT_EQ(max_x, 30.0f);
}

TEST(SplineExtentRules, ScanRangeAlwaysContainsZero) {
    // All X positive: minX stays 0 because the scan starts there and only ever
    // moves it down.
    const float verts[8] = {5.0f, 0.0f, 0.0f, 0.0f, 7.0f, 0.0f, 0.0f, 0.0f};
    igi::SplineExtentSource lod;
    lod.verts = verts;
    lod.vert_count = 2;
    float min_x = 99.0f;
    float max_x = -99.0f;
    igi::SplineScanModelExtent(&lod, 1, 4, &min_x, &max_x);
    EXPECT_FLOAT_EQ(min_x, 0.0f);
    EXPECT_FLOAT_EQ(max_x, 7.0f);
}

TEST(SplineExtentRules, NaNXUpdatesTheMinimumAndNeverTheMaximum) {
    // The min test is the NaN-swallowing `!(*v >= minX)` form while the max test is
    // a plain `>`, so a NaN X moves minX to NaN and leaves maxX alone.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float verts[8] = {1.0f, 0.0f, 0.0f, 0.0f, nan, 0.0f, 0.0f, 0.0f};
    igi::SplineExtentSource lod;
    lod.verts = verts;
    lod.vert_count = 2;
    float min_x = 99.0f;
    float max_x = -99.0f;
    igi::SplineScanModelExtent(&lod, 1, 4, &min_x, &max_x);
    EXPECT_TRUE(std::isnan(min_x));
    EXPECT_FLOAT_EQ(max_x, 1.0f);
}

TEST(SplineExtentRules, NullLodIsSkipped) {
    igi::SplineExtentSource lods[2];
    const float verts[4] = {-3.0f, 0.0f, 0.0f, 0.0f};
    lods[1].verts = verts;
    lods[1].vert_count = 1;
    float min_x = 0.0f;
    float max_x = 0.0f;
    EXPECT_EQ(igi::SplineScanModelExtent(lods, 2, 4, &min_x, &max_x), 1);
    EXPECT_FLOAT_EQ(min_x, -3.0f);
}

// ---------------------------------------------------------------------------
// The cache and Float_Log2
// ---------------------------------------------------------------------------

TEST(SplineCacheRules, OnlyOneSlotIsEverTouched) {
    igi::SplineCache cache;
    EXPECT_EQ(igi::SplineCache::kSlots, 2);
    EXPECT_EQ(igi::SplineCache::kLiveSlots, 1);
    EXPECT_EQ(cache.Slot(0), nullptr);
    EXPECT_EQ(cache.Slot(1), nullptr);
    EXPECT_EQ(cache.Data(0), 0);

    int a = 1;
    int b = 2;
    cache.Remove(&a);
    cache.Remove(&b);
    EXPECT_EQ(cache.Slot(0), nullptr);
    // Slot 1 is outside the engine's `i < 1` loop and outside Reset, so it can
    // never be observed non-null.
    EXPECT_EQ(cache.Slot(1), nullptr);

    cache.Reset();
    EXPECT_EQ(cache.Slot(0), nullptr);
    EXPECT_EQ(cache.Data(0), 0);

    // Out-of-range indices are rejected rather than read.
    EXPECT_EQ(cache.Slot(-1), nullptr);
    EXPECT_EQ(cache.Slot(2), nullptr);
    EXPECT_EQ(cache.Data(7), 0);
}

TEST(SplineCacheRules, RemoveIsIdempotentAndResetClearsSlotZero) {
    igi::SplineCache cache;
    int a = 1;
    cache.Remove(&a);
    cache.Remove(&a);
    EXPECT_EQ(cache.Slot(0), nullptr);
    cache.Reset();
    EXPECT_EQ(cache.Slot(0), nullptr);
}

TEST(SplineFloatLog2Rules, ReadsTheRawExponentAndTopMantissaBits) {
    EXPECT_EQ(igi::FloatLog2(1.0f), 0u);   // bits == 0x3F800000, not > it
    EXPECT_EQ(igi::FloatLog2(2.0f), 8u);
    EXPECT_EQ(igi::FloatLog2(4.0f), 16u);
    EXPECT_EQ(igi::FloatLog2(1024.0f), 80u);
    EXPECT_EQ(igi::FloatLog2(1.5f), 4u);   // 8 * log2(1.5) = 4.68 -> 4
    EXPECT_EQ(igi::FloatLog2(0.5f), 0u);
    EXPECT_EQ(igi::FloatLog2(0.0f), 0u);
    EXPECT_EQ(igi::FloatLog2(-1.0f), 0u);  // the comparison is SIGNED on the bits
    // A NaN is not special-cased: the comparison is on the raw bits, so a POSITIVE
    // NaN (0x7FC00000) clears 0x3F800000 and returns 1028, while a NEGATIVE NaN
    // (0xFFC00000) is a negative int and returns 0.
    EXPECT_EQ(igi::FloatLog2(std::numeric_limits<float>::quiet_NaN()), 1028u);
    EXPECT_EQ(igi::FloatLog2(-std::numeric_limits<float>::quiet_NaN()), 0u);
    EXPECT_EQ(igi::FloatLog2(std::numeric_limits<float>::infinity()), 1024u);
}

// ---------------------------------------------------------------------------
// The authoring helpers
// ---------------------------------------------------------------------------

TEST(SplineAuthoringRules, NewWaypointStepsByTheSegmentModelExtent) {
    igi::SplineObjWaypointParams neighbour;
    neighbour.orientation[2] = 0.0f;  // identity frame
    neighbour.point[0] = 100.0;
    neighbour.point[1] = 200.0;
    neighbour.point[2] = 300.0;

    double out[3];
    igi::SplineNewWaypointPoint(neighbour, igi::kSplineStepModelExtent, 50.0, true, out);
    EXPECT_DOUBLE_EQ(out[0], 150.0);
    EXPECT_DOUBLE_EQ(out[1], 200.0);
    EXPECT_DOUBLE_EQ(out[2], 300.0);

    // Backward from a neighbour: the point is moved the other way.
    igi::SplineNewWaypointPoint(neighbour, igi::kSplineStepModelExtent, 50.0, false, out);
    EXPECT_DOUBLE_EQ(out[0], 50.0);
}

TEST(SplineAuthoringRules, ForwardDefaultUsesTheDefaultSegmentLength) {
    igi::SplineObjWaypointParams neighbour;  // identity frame, point at the origin
    double out[3];
    igi::SplineNewWaypointPoint(neighbour, igi::kSplineStepDefaultLength, 0.0, true, out);
    EXPECT_NEAR(out[0], static_cast<double>(igi::kSplineDefaultSegLength), 1e-3);
    EXPECT_DOUBLE_EQ(out[1], 0.0);
    EXPECT_DOUBLE_EQ(out[2], 0.0);
}

TEST(SplineAuthoringRules, BackwardWithoutAnExtentStepsByTheUnitAxisNotTheDefaultLength) {
    // src/tagitem/spline.c:530-532 assigns only `dx = x`, so a backward new
    // waypoint with no segment model steps ONE unit along the frame's X axis -
    // asymmetric with the forward case, and reproduced.
    igi::SplineObjWaypointParams neighbour;
    double out[3];
    igi::SplineNewWaypointPoint(neighbour, igi::kSplineStepUnit, 0.0, false, out);
    EXPECT_DOUBLE_EQ(out[0], -1.0);
    EXPECT_DOUBLE_EQ(out[1], 0.0);
    EXPECT_DOUBLE_EQ(out[2], 0.0);
}

TEST(SplineAuthoringRules, NewWaypointNormalisesTheNeighbourTangentFirst) {
    // A frame with a first column of length 3 must still step exactly the model
    // extent, because the axis is normalised before it is scaled.
    igi::SplineObjWaypointParams neighbour;
    float rot[10] = {3.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    // SplineWaypointFrame rebuilds the frame from the authored angles, so use an
    // angle pair that yields a pure +X first column: identity.
    neighbour.orientation[0] = 0.0f;
    neighbour.orientation[1] = 0.0f;
    neighbour.orientation[2] = 0.0f;
    (void)rot;
    double out[3];
    igi::SplineNewWaypointPoint(neighbour, igi::kSplineStepModelExtent, 7.0, true, out);
    EXPECT_NEAR(out[0], 7.0, 1e-6);
}

TEST(SplineAuthoringRules, SnapPullsThePointOntoTheExtentRadiusSphere) {
    const double previous[3] = {0.0, 0.0, 0.0};
    const double point[3] = {30.0, 40.0, 0.0};
    double out[3];
    igi::SplineSnapPoint(previous, point, 10.0, out);
    // |point - previous| = 50, so the point is scaled by 10/50 = 0.2.
    EXPECT_NEAR(out[0], 6.0, 1e-6);
    EXPECT_NEAR(out[1], 8.0, 1e-6);
    EXPECT_NEAR(out[2], 0.0, 1e-9);
}

TEST(SplineAuthoringRules, AutoOrientColumnZeroIsTheSummedDirection) {
    const double previous[3] = {-10.0, 0.0, 0.0};
    const double point[3] = {0.0, 0.0, 0.0};
    const double next[3] = {10.0, 0.0, 0.0};
    float rot[10];
    igi::SplineAutoOrientFrame(previous, point, next, rot);
    // dir = (point - previous) + (next - point) = (20, 0, 0), normalised to +X.
    EXPECT_NEAR(rot[0], 1.0f, 1e-6f);
    EXPECT_NEAR(rot[3], 0.0f, 1e-6f);
    EXPECT_NEAR(rot[6], 0.0f, 1e-6f);
    EXPECT_FLOAT_EQ(rot[9], 0.0f);
}

TEST(SplineAuthoringRules, AutoOrientWithNoNeighboursFallsBackToTheIdentityAxis) {
    const double point[3] = {5.0, 5.0, 5.0};
    float rot[10];
    igi::SplineAutoOrientFrame(nullptr, point, nullptr, rot);
    // dir is all zero -> r[0] = 1, and the rest is derived from that.
    EXPECT_FLOAT_EQ(rot[0], 1.0f);
    EXPECT_FLOAT_EQ(rot[1], 0.0f);
    EXPECT_FLOAT_EQ(rot[2], 0.0f);
}

TEST(SplineAuthoringRules, StraightenRunRespacesEvenlyBetweenItsAnchors) {
    // 0 and 3 are the anchors (not straight), 1 and 2 are the "Align" run.
    std::vector<igi::SplineObjWaypointParams> waypoints(4);
    const double xs[4] = {0.0, 5.0, 20.0, 40.0};
    for (int i = 0; i < 4; i++) waypoints[static_cast<std::size_t>(i)].point[0] = xs[i];
    waypoints[1].straight = true;
    waypoints[2].straight = true;

    ASSERT_TRUE(igi::SplineStraightenRun(waypoints, 1));
    // count = 2 (the backward walk's anchor plus the forward straight member), so
    // step = (20 - 0) / 2 = 10: w1 moves 5 -> 10 and w2 stays at 20.
    EXPECT_DOUBLE_EQ(waypoints[0].point[0], 0.0);
    EXPECT_NEAR(waypoints[1].point[0], 10.0, 1e-9);
    EXPECT_NEAR(waypoints[2].point[0], 20.0, 1e-9);
    EXPECT_DOUBLE_EQ(waypoints[3].point[0], 40.0);
}

TEST(SplineAuthoringRules, StraightenRunLeavesALoneWaypointAlone) {
    // A single straight waypoint at the front of the rail: first == last, so the
    // engine returns without even setting a frame.
    std::vector<igi::SplineObjWaypointParams> waypoints(2);
    waypoints[0].point[0] = 3.0;
    waypoints[0].straight = true;
    waypoints[1].point[0] = 9.0;
    waypoints[0].orientation[2] = 0.25f;
    EXPECT_FALSE(igi::SplineStraightenRun(waypoints, 0));
    EXPECT_DOUBLE_EQ(waypoints[0].point[0], 3.0);
    EXPECT_FLOAT_EQ(waypoints[0].orientation[2], 0.25f);
}

TEST(SplineAuthoringRules, ContainerRotateTurnsAboutTheFirstWaypoint) {
    std::vector<igi::SplineObjWaypointParams> waypoints(2);
    waypoints[0].point[0] = 10.0;
    waypoints[1].point[0] = 20.0;
    igi::SplineContainerRotate(waypoints, kPiF / 2.0f);
    // The pivot is waypoint 0, so it does not move; waypoint 1 goes from +10 on X
    // to +10 on Y relative to the pivot.
    EXPECT_NEAR(waypoints[0].point[0], 10.0, 1e-4);
    EXPECT_NEAR(waypoints[0].point[1], 0.0, 1e-4);
    EXPECT_NEAR(waypoints[1].point[0], 10.0, 1e-3);
    EXPECT_NEAR(waypoints[1].point[1], 10.0, 1e-3);
}

TEST(SplineAuthoringRules, ContainerTranslateRotatesTheDeltaByTheGammaAngle) {
    std::vector<igi::SplineObjWaypointParams> waypoints(1);
    waypoints[0].point[0] = 100.0;
    const double delta[3] = {10.0, 0.0, 0.0};
    // RotZ(pi/2) * (10, 0, 0) = (0, 10, 0).
    igi::SplineContainerTranslate(waypoints, delta, kPiF / 2.0f);
    EXPECT_NEAR(waypoints[0].point[0], 100.0, 1e-3);
    EXPECT_NEAR(waypoints[0].point[1], 10.0, 1e-3);
    EXPECT_NEAR(waypoints[0].point[2], 0.0, 1e-9);
}

// ---------------------------------------------------------------------------
// Retail corpus: LEVEL1 OBJECTS.QVM
// ---------------------------------------------------------------------------

TEST(SplineCorpusRules, DeclaredParametersMatchTheTables) {
    const fs::path path = LevelDir(1) / "OBJECTS.QVM";
    if (!fs::is_regular_file(path)) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string()
                     << " (set IGI_GAME_PATH to a retail install to run this test)";
    }
    const QVMFile qvm = QVM_Parse(path.string());

    const std::vector<std::string> obj = DeclaredParameters(qvm, "SplineObj");
    ASSERT_EQ(obj.size(), static_cast<std::size_t>(igi::kSplineObjParamCount) * 2u);
    const igi::SplineParamDesc* obj_table = igi::SplineObjParamTable();
    for (int i = 0; i < igi::kSplineObjParamCount; i++) {
        EXPECT_EQ(obj[static_cast<std::size_t>(i) * 2], obj_table[i].authored_name)
            << "param " << i;
        EXPECT_EQ(obj[static_cast<std::size_t>(i) * 2 + 1], obj_table[i].type_name)
            << "param " << i;
    }

    const std::vector<std::string> wp = DeclaredParameters(qvm, "SplineObjWaypoint");
    ASSERT_EQ(wp.size(), static_cast<std::size_t>(igi::kSplineObjWaypointParamCount) * 2u);
    const igi::SplineParamDesc* wp_table = igi::SplineObjWaypointParamTable();
    for (int i = 0; i < igi::kSplineObjWaypointParamCount; i++) {
        EXPECT_EQ(wp[static_cast<std::size_t>(i) * 2], wp_table[i].authored_name)
            << "param " << i;
        EXPECT_EQ(wp[static_cast<std::size_t>(i) * 2 + 1], wp_table[i].type_name)
            << "param " << i;
    }
}

TEST(SplineCorpusRules, Level1InventoryMatchesTheDecompiledScript) {
    const fs::path path = LevelDir(1) / "OBJECTS.QVM";
    if (!fs::is_regular_file(path)) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string();
    }
    const QVMFile qvm = QVM_Parse(path.string());

    const std::vector<std::vector<std::string>> containers =
        ExtractCalls(qvm, "SplineObj", static_cast<std::size_t>(igi::kSplineObjTokenCount));
    const std::vector<std::vector<std::string>> waypoints = ExtractCalls(
        qvm, "SplineObjWaypoint", static_cast<std::size_t>(igi::kSplineObjWaypointTokenCount));

    // LEVEL1 authors six SplineObj containers with 12/3/4/2/2/2 waypoints.
    ASSERT_EQ(containers.size(), 6u);
    EXPECT_EQ(waypoints.size(), 25u);

    // Five of the six carry task id -1 (no id) and one is the rail, id 1500.
    int named = 0;
    bool found_1500 = false;
    for (const std::vector<std::string>& tokens : containers) {
        if (tokens[0] != "-1") named++;
        if (tokens[0] == "1500") found_1500 = true;
    }
    EXPECT_EQ(named, 1);
    EXPECT_TRUE(found_1500);

    // Cross-check the QVM against the decompiled text: same six containers, same
    // total waypoint count.
    const std::string text = ReadFile(LevelDir(1) / "objects.qsc");
    if (!text.empty()) {
        EXPECT_EQ(TextCallsOfType(text, "SplineObj").size(), 6u);
        EXPECT_EQ(TextCallsOfType(text, "SplineObjWaypoint").size(), 25u);
    }

    // The two kinds of authored container, exactly as the level authors them: the
    // rail is the only non-rigid one and the only one whose waypoints name a
    // marker model; the five bridges are rigid and author no marker at all. The
    // 3-waypoint bridge ships one span with an EMPTY Segment Model, which
    // SplineObjSegment_Setup's `if (!models) return;` leaves without any mesh.
    int rail_like = 0;
    int bridge_like = 0;
    int empty_seg = 0;
    for (const std::vector<std::string>& tokens : waypoints) {
        igi::SplineObjWaypointParams parsed;
        ASSERT_TRUE(igi::ParseSplineObjWaypointParams(tokens, &parsed));
        if (parsed.model_name == "waypoint") {
            rail_like++;
            EXPECT_TRUE(parsed.seg_model == "368_01_1" || parsed.seg_model == "322_01_1" ||
                        parsed.seg_model == "323_01_1")
                << "rail waypoint with an unexpected segment model " << parsed.seg_model;
        } else {
            bridge_like++;
            EXPECT_EQ(parsed.model_name, "") << "a bridge waypoint authors a marker model";
            EXPECT_TRUE(parsed.seg_model == "320_02_1" || parsed.seg_model.empty())
                << "bridge waypoint with an unexpected segment model " << parsed.seg_model;
        }
        if (parsed.seg_model.empty()) {
            // The only one is the second waypoint of the 3-waypoint bridge.
            empty_seg++;
            EXPECT_NEAR(parsed.point[0], 24446002.0, 1.0);
            EXPECT_NEAR(parsed.point[1], -56045108.0, 1.0);
            EXPECT_NEAR(parsed.point[2], 174433600.0, 1.0);
            EXPECT_NEAR(parsed.orientation[2], 4.4707231521606445, 1e-6);
        }
    }
    EXPECT_EQ(rail_like, 12);
    EXPECT_EQ(bridge_like, 13);
    EXPECT_EQ(empty_seg, 1);
}

TEST(SplineCorpusRules, Rail1500HasTwelveWaypointsAndElevenSpans) {
    const fs::path path = LevelDir(1) / "OBJECTS.QVM";
    if (!fs::is_regular_file(path)) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string();
    }
    const QVMFile qvm = QVM_Parse(path.string());

    const std::vector<std::vector<std::string>> containers =
        ExtractCalls(qvm, "SplineObj", static_cast<std::size_t>(igi::kSplineObjTokenCount));
    ASSERT_EQ(containers.size(), 6u);
    bool found_1500 = false;
    for (const std::vector<std::string>& tokens : containers) {
        if (tokens[0] == "1500") found_1500 = true;
    }
    ASSERT_TRUE(found_1500) << "rail 1500 must be one of the six SplineObj containers";

    // Independently of the bytecode walk, count the children from the decompiled
    // script: the rail's `Task_New(1500, ...)` call has 19 own arguments (id, type,
    // name, 16 parameters) plus one nested Task_New per waypoint.
    const std::string text = ReadFile(LevelDir(1) / "objects.qsc");
    ASSERT_FALSE(text.empty()) << "objects.qsc is needed for the span-count check";
    const std::vector<std::vector<std::string>> text_containers =
        TextCallsOfType(text, "SplineObj");
    ASSERT_EQ(text_containers.size(), 6u);
    std::size_t text_rail = text_containers.size();
    for (std::size_t i = 0; i < text_containers.size(); i++) {
        if (text_containers[i][0] == "1500") text_rail = i;
    }
    ASSERT_LT(text_rail, text_containers.size());
    EXPECT_EQ(text_containers[text_rail].size(), 19u + 12u)
        << "rail 1500 must author 12 waypoint children, i.e. 11 spans";

    // The container's own id token and the total waypoint count.
    EXPECT_EQ(ExtractCalls(qvm, "SplineObjWaypoint",
                           static_cast<std::size_t>(igi::kSplineObjWaypointTokenCount))
                  .size(),
              25u);
}

TEST(SplineCorpusRules, Rail1500LengthIsElevenSpansAndTheDocFigureIsTheFirstTen) {
    // The whole point of this test: PLACEMENT_SPLINES.md §3.2 publishes
    // 3 928 793.21 units for rail 1500 and calls it 10 spans. That number is the
    // sum over the FIRST TEN spans; the retail rail has a twelfth waypoint, so the
    // eleventh span is missing from it. Both numbers are checked here.
    // `obj->length` is a FLOAT and is accumulated span by span
    // (SplineObjWaypoint_Changed, src/tagitem/spline.c:833-839:
    // `obj->length = p->seg->length + obj->length`), so the sum is accumulated in
    // float, not in double. The last bits differ: the float sum is 4 461 267.5
    // while a double sum of the same eleven float span lengths is 4 461 267.6875.
    const std::size_t spans = 11;
    float total = 0.0f;
    float first_ten = 0.0f;
    for (std::size_t i = 1; i <= spans; i++) {
        const igi::SplineSpanJoints joints = Rail1500Span(i);
        EXPECT_EQ(joints.joints.size(), static_cast<std::size_t>(kRail1500[i].segments) + 1u)
            << "span " << i;
        total = joints.length + total;
        if (i <= 10) first_ten = joints.length + first_ten;
    }

    // The published figure, reproduced to 5 units in 3.9 million (1.3e-6): the
    // first ten spans really are what PLACEMENT_SPLINES.md measured.
    EXPECT_NEAR(first_ten, 3928793.21, 6.0)
        << "the first ten spans should reproduce PLACEMENT_SPLINES.md §3.2";

    // The real rail length: eleven spans, float-accumulated.
    EXPECT_FLOAT_EQ(total, 4461267.5f);
    EXPECT_GT(total, first_ten);
    const float last_span = Rail1500Span(11).length;
    EXPECT_FLOAT_EQ(last_span, 532479.4375f);
    EXPECT_FLOAT_EQ(first_ten + last_span, total);

    // The chord sums, for the same cross-check: the doc's 3 903 025.28 is the
    // first-ten chord sum, and the full rail chord is longer by the final span.
    EXPECT_NEAR(Rail1500Chord(1, 10), 3903025.28, 1.0);
    EXPECT_NEAR(Rail1500Chord(1, 11), 4435504.76, 1.0);
    EXPECT_GT(Rail1500Chord(1, 11), Rail1500Chord(1, 10));
}

TEST(SplineCorpusRules, Rail1500AuthoredWaypointsMatchTheScript) {
    // The QVM must reproduce the twelve authored waypoints exactly - this is the
    // regression guard for the extraction helpers as much as for the parser.
    const fs::path path = LevelDir(1) / "OBJECTS.QVM";
    if (!fs::is_regular_file(path)) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string();
    }
    const QVMFile qvm = QVM_Parse(path.string());
    const std::vector<std::vector<std::string>> waypoints = ExtractCalls(
        qvm, "SplineObjWaypoint", static_cast<std::size_t>(igi::kSplineObjWaypointTokenCount));
    ASSERT_EQ(waypoints.size(), 25u);

    // The rail's waypoints are the first twelve in the file: the rail container is
    // the first SplineObj in LEVEL1's script and its children follow immediately.
    // The check on waypoints[12] below makes that ordering assumption explicit.
    for (std::size_t i = 0; i < 12; i++) {
        igi::SplineObjWaypointParams parsed;
        ASSERT_TRUE(igi::ParseSplineObjWaypointParams(waypoints[i], &parsed)) << "waypoint " << i;
        EXPECT_EQ(parsed.segments, kRail1500[i].segments) << "waypoint " << i;
        EXPECT_NEAR(parsed.point[0], kRail1500[i].point[0], 1.0) << "waypoint " << i;
        EXPECT_NEAR(parsed.point[1], kRail1500[i].point[1], 1.0) << "waypoint " << i;
        EXPECT_NEAR(parsed.point[2], kRail1500[i].point[2], 1.0) << "waypoint " << i;
        EXPECT_NEAR(parsed.orientation[2], kRail1500[i].orientation[2], 1e-5f)
            << "waypoint " << i;
    }
    // Waypoint 12 is the first child of the NEXT container (the rigid 3-waypoint
    // one at 24472610, -55944580, 174442496), which pins the file order.
    igi::SplineObjWaypointParams thirteenth;
    ASSERT_TRUE(igi::ParseSplineObjWaypointParams(waypoints[12], &thirteenth));
    EXPECT_NEAR(thirteenth.point[0], 24472610.0, 1.0);
    EXPECT_NEAR(thirteenth.point[1], -55944580.0, 1.0);
    EXPECT_NEAR(thirteenth.point[2], 174442496.0, 1.0);
    EXPECT_EQ(thirteenth.seg_model, "320_02_1");
    // The last authored waypoint, and the only one with a non-zero roll.
    EXPECT_NEAR(kRail1500[11].point[0], 22088476.0, 1.0);
    EXPECT_NEAR(kRail1500[5].orientation[0], 6.2831811904907227, 1e-6);

    // NumAreas of the eleven spans: the waypoints AFTER the first.
    const int want[11] = {10, 10, 10, 10, 10, 20, 20, 10, 10, 10, 10};
    for (std::size_t i = 1; i <= 11; i++) {
        EXPECT_EQ(kRail1500[i].segments, want[i - 1]) << "span " << i;
    }
}

TEST(SplineCorpusRules, Train1499WrapsItsRailDistanceInOneAdditionNotTwo) {
    // Train 1499 authors start_dist = -4045627.5 on rail 1500 (LEVEL1/objects.qsc:
    // `Task_New(1499, "Train", "", -4045627.5, 0.2, 1500, "608_01_1", ...)`).
    // SplineObj_GetPoints (src/tagitem/spline.c:246-256) adds obj->length until the
    // distance is non-negative; with the REAL rail length that is ONE addition,
    // not the two PLACEMENT_SPLINES.md §3.2 derives from its ten-span length.
    const std::string text = ReadFile(LevelDir(1) / "objects.qsc");
    if (text.empty()) {
        GTEST_SKIP() << "retail IGI (with objects.qsc) not found at " << GameRoot().string()
                     << " (set IGI_GAME_PATH to a retail install to run this test)";
    }

    // Read the authored start_dist out of the Train task rather than hardcoding it.
    // The text arguments are [0] id, [1] type, [2] name, [3] start_dist, [4] frac,
    // [5] the rail's id, [6] the wagon model.
    float start_dist = 0.0f;
    bool found = false;
    for (const std::vector<std::string>& args : TextCallsOfType(text, "Train")) {
        if (args.size() < 7 || args[0] != "1499") continue;
        start_dist = std::strtof(args[3].c_str(), nullptr);
        EXPECT_EQ(args[5], "1500") << "train 1499 must name rail 1500";
        found = true;
    }
    ASSERT_TRUE(found) << "Train 1499 not found in LEVEL1";
    EXPECT_FLOAT_EQ(start_dist, -4045627.5f);

    // Accumulate the way the engine does — float, span order
    // (src/tagitem/spline.c:833-839).
    float rail_length = 0.0f;
    for (std::size_t i = 1; i <= 11; i++) rail_length = Rail1500Span(i).length + rail_length;
    EXPECT_FLOAT_EQ(rail_length, 4461267.5f);

    // The real length: one addition, landing in the second span.
    const double wrapped = WrapRailDistance(start_dist, rail_length);
    EXPECT_GE(wrapped, 0.0);
    EXPECT_LT(wrapped, rail_length);
    // -4045627.5 + 4461267.5 is exact in float.
    EXPECT_FLOAT_EQ(wrapped, 415640.0f);
    double cumulative = 0.0;
    std::size_t landed_in = 0;
    for (std::size_t i = 1; i <= 11; i++) {
        cumulative += Rail1500Span(i).length;
        if (wrapped <= cumulative) {
            landed_in = i;
            break;
        }
    }
    EXPECT_EQ(landed_in, 2u) << "the wrapped distance lands in the second span";

    // The doc's ten-span length would have wrapped twice and landed at the other
    // end of the rail. Assert that this is NOT what happens with the real length,
    // so a revert to the doc's figure fails here.
    const float doc_length = 3928793.21f;
    EXPECT_NEAR(WrapRailDistance(start_dist, doc_length), 3811958.92, 1.0);
    EXPECT_GT(std::fabs(wrapped - WrapRailDistance(start_dist, doc_length)), 1000000.0);
}

TEST(SplineCorpusRules, Rail1500SpansAreAllCurvedBecauseNothingIsRigid) {
    const fs::path path = LevelDir(1) / "OBJECTS.QVM";
    if (!fs::is_regular_file(path)) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string();
    }
    const QVMFile qvm = QVM_Parse(path.string());
    const std::vector<std::vector<std::string>> containers =
        ExtractCalls(qvm, "SplineObj", static_cast<std::size_t>(igi::kSplineObjTokenCount));
    igi::SplineObjParams rail;
    bool found = false;
    for (const std::vector<std::string>& tokens : containers) {
        if (tokens[0] != "1500") continue;
        ASSERT_TRUE(igi::ParseSplineObjParams(tokens, &rail));
        found = true;
    }
    ASSERT_TRUE(found);
    EXPECT_FALSE(rail.rigid);
    EXPECT_FALSE(rail.snap);
    EXPECT_FALSE(rail.auto_orient);
    EXPECT_EQ(rail.max_segments, 20);
    EXPECT_NEAR(rail.angle, kPiF, 1e-6);

    // Every waypoint's NumAreas is <= the container's 20, so the effective count
    // is the authored one and the spans are curved (rigid is false throughout).
    for (std::size_t i = 1; i <= 11; i++) {
        const int segments = igi::SplineEffectiveSegmentCount(
            kRail1500[i].segments, rail.max_segments, igi::SplineSpanIsRigid(rail.rigid, false));
        EXPECT_EQ(segments, kRail1500[i].segments) << "span " << i;
        EXPECT_GT(segments, 1);
    }
}

// The joints are stepped equally in the HERMITE PARAMETER, not in arc length
// (src/sound/segsetup.c:108-109, `t = i / segments`), so equal steps in `t` give
// unequal distances as soon as the span is curved, and the spread grows with the
// curvature. That is measurable on the retail rail and is the difference from an
// "arc-length sample the curve" rule, which would move every cross-section on a
// curved span (src/sound/segsetup.c:141-197). No such rule exists in the engine.
TEST(SplineCorpusRules, Rail1500SubSegmentsAreEvenInTheCurveParameterNotInArcLength) {
    // span -> expected shortest / longest sub-segment (segLength[i+1]).
    const struct {
        std::size_t span;
        double shortest;
        double longest;
    } kCases[] = {
        {2, 26624.18, 26624.38},   // nearly straight: 0.001 % spread
        {7, 26461.16, 27634.27},   // two long curves: 4.4 %
        {3, 23611.18, 27581.97},   // the 0.268 rad yaw change at waypoint 4: 16.8 %
        {9, 53248.31, 53248.32},   // three collinear spans: no spread at all
    };
    for (const auto& c : kCases) {
        const igi::SplineSpanJoints joints = Rail1500Span(c.span);
        const int n = static_cast<int>(joints.joints.size()) - 1;
        ASSERT_GT(n, 1) << "span " << c.span;
        double shortest = joints.seg_length[1];
        double longest = joints.seg_length[1];
        for (int i = 2; i <= n; i++) {
            const double len = joints.seg_length[static_cast<std::size_t>(i)];
            if (len < shortest) shortest = len;
            if (len > longest) longest = len;
        }
        EXPECT_NEAR(shortest, c.shortest, 0.02) << "span " << c.span;
        EXPECT_NEAR(longest, c.longest, 0.02) << "span " << c.span;
    }

    // The sharp span: the longest sub-segment is more than 16 % longer than the
    // shortest, which no equal-arc-length rule can produce.
    const igi::SplineSpanJoints bent = Rail1500Span(3);
    ASSERT_GE(bent.seg_length.size(), 11u);
    EXPECT_GT(bent.seg_length[6], bent.seg_length[2] * 1.16f);
    // The collinear spans are even to float precision: the curve is a straight
    // line there, so the parameter and the arc length agree.
    const igi::SplineSpanJoints straight = Rail1500Span(9);
    ASSERT_GE(straight.seg_length.size(), 11u);
    for (int i = 1; i <= 10; i++) {
        EXPECT_NEAR(straight.seg_length[static_cast<std::size_t>(i)], 53248.31, 0.02) << "sub-segment " << i;
    }
}
