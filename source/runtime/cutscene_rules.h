// cutscene_rules.h - Authored cutscene camera timeline and keyframe interpolation.
//
// MIT License.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace igi {

// ---------------------------------------------------------------------------
// Engine constants
// ---------------------------------------------------------------------------

constexpr double kCutScenePi = 0x1.921fb54442d1bp+1;
constexpr double kCutSceneNegPi = -0x1.921fb54442d1bp+1;
constexpr float kCutSceneTwoPi = 0x1.921fb6p+2f;
constexpr double kCutSceneHalfD = 0.5;
constexpr float kCutSceneHalf = 0.5f;
constexpr float kCutSceneUnits = 4096.0f;
constexpr float kCutSceneShakeAngle = 0x1.1171bap-1f;
constexpr float kCutSceneFilterDepth = 0x1.333334p+9f;
constexpr float kCutSceneWideScreenUnknown = 0x1.99999ap+8f;

constexpr float kCutSceneTimeRangeMin = 0.0f;
constexpr float kCutSceneTimeRangeMax = 20.0f;

constexpr int kCutSceneTicksPerSecond = 30;

constexpr int kCutSceneKeyCapacity = 100;

constexpr float kCutSceneCameraAspect = 1.0f;
constexpr float kCutSceneCameraPriority = 1000.0f;

// Authored argument counts. `[0]=task id, [1]=type, [2]=name, [3]=parameter 0`.
constexpr int kCutSceneKeyTokenCount = 23;
constexpr int kCutSceneScreenTokenCount = 21;

// ---------------------------------------------------------------------------
// The two authored parameter tables
// ---------------------------------------------------------------------------

// One row of an authored parameter table.
struct CutSceneParamDesc {
    const char* authored_name;  // the declaration string
    const char* engine_field;   // the struct field the slot writes
    const char* type_name;      // the declared type
    int token_index;            // first script-argument index in argTokens
    int token_count;            // script arguments consumed
};

int CutSceneKeyParamCount();
const CutSceneParamDesc* CutSceneKeyParamTable();

int CutSceneScreenParamCount();
const CutSceneParamDesc* CutSceneScreenParamTable();

// ---------------------------------------------------------------------------
// The authored key: "EditCamera"
// ---------------------------------------------------------------------------

// One authored EditCamera key.
struct CutSceneKeyParams {
    double position[3] = {0.0, 0.0, 0.0};   // Position
    float angles[3] = {0.0f, 0.0f, 0.0f};   // Alpha/Beta/Gamma
    float fov = 1.0f;                       // FOV
    float duration = 0.0f;                  // Duration
    int pos_attach = -1;                    // Link task ID
    bool pos_follow = true;                 // Update link continously
    int rot_attach = -1;                    // Target task ID
    bool rot_follow = true;                 // Update target continously
    bool smooth = true;                     // Smooth to next
    float time_of_day_override = -1.0f;     // Time of day (-1 means use default)
    std::string filter_name = "CAMERAFILTER_TYPE_NONE";  // FILTER
    float filter_level = 0.5f;              // Noise
    float colour[3] = {0.0f, 0.0f, 0.0f};   // Filter color
    float shake = 0.0f;                     // Camera shake
};

bool ParseCutSceneKey(const std::vector<std::string>& arg_tokens, CutSceneKeyParams* out);

// ---------------------------------------------------------------------------
// The authored screen/timeline header: "CutScene"
// ---------------------------------------------------------------------------

struct CutSceneScreenParams {
    std::string run_expression;         // Run
    std::string reset_expression;       // Reset
    std::string time_delta_expression;  // "Time delta (seconds)"
    float start_time_seconds = 0.0f;    // "Start time (seconds)"
    bool initial_run = true;            // "Initial run"
    float time_scale = 1.0f;            // "Time scale"
    float screen_size = 1.0f;           // "Viewport height factor"
    float fade_in_seconds = 0.0f;       // "... fade in time"
    float fade_out_seconds = 0.0f;      // "... fade out time"
    float time_of_day = 0.0f;           // "Time of day"
    std::string start_expression;       // "Start expression"
    std::string stop_expression;        // "Stop expression"
};

bool ParseCutSceneScreen(const std::vector<std::string>& arg_tokens, CutSceneScreenParams* out);

// ---------------------------------------------------------------------------
// The timeline
// ---------------------------------------------------------------------------

struct CutSceneKey {
    CutSceneKeyParams params;
    float start_time = 0.0f;
};

struct CutSceneKeyNode {
    CutSceneKeyParams params;
    std::vector<CutSceneKeyNode> children;
};

std::vector<CutSceneKey> CollectCutSceneKeys(const std::vector<CutSceneKeyNode>& cut_scene_children,
                                             float scale, float* out_end);

const CutSceneKey* CutSceneKeyAt(const std::vector<CutSceneKey>& keys, int index);

void UnwrapCutSceneAngles(std::vector<CutSceneKey>* keys);

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------

float CutSceneTimeFromTick(int tick);

int CutSceneTickFromTime(double seconds);

// Which keys are selected at time `t`, and the 4-key interpolation window.
struct CutSceneKeyWindow {
    int active_key = -1;
    bool finished = false;
    int indices[4] = {0, 0, 0, 0};
};

CutSceneKeyWindow SelectCutSceneKeys(const std::vector<CutSceneKey>& keys, float end, float t);

// ---------------------------------------------------------------------------
// Interpolation
// ---------------------------------------------------------------------------

long double CutSceneSpline(double p0, double p1, double p2, double p3, double t0, double t1, double t2,
                           double t3, double t);

struct CutSceneFade {
    float mix = 1.0f;
    float screen_factor = 1.0f;
};

CutSceneFade CutSceneFadeMix(const CutSceneScreenParams& screen, float t, float end);

void CutSceneShakeDelta(float shake, const long double random_six[6], double out_position_delta[3],
                        double out_angle_delta[3]);

// ---------------------------------------------------------------------------
// Camera preview state
// ---------------------------------------------------------------------------

struct CutSceneCameraState {
    double position[3] = {0.0, 0.0, 0.0};
    float angles[3] = {0.0f, 0.0f, 0.0f};
    float fov = 1.0f;
    float shake = 0.0f;
    float time_of_day = 0.0f;
    float filter_level = 0.0f;
    float colour[3] = {0.0f, 0.0f, 0.0f};
    float fade_mix = 1.0f;
    float screen_factor = 1.0f;
    int active_key = -1;
    bool finished = false;
};

bool EvaluateCutScene(const std::vector<CutSceneKey>& keys, float end,
                      const CutSceneScreenParams& screen, float t, bool band_filter_active,
                      CutSceneCameraState* out);

}  // namespace igi
