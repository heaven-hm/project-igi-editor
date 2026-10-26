#include "runtime/cutscene_rules.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace igi {
namespace {

// ---------------------------------------------------------------------------
// Authored token readers. Layout: [0]=task id, [1]=type, [2]=name,
// [3]=declared parameter 0, so declared parameter k lives at index k + 3.
// ---------------------------------------------------------------------------

float TokenFloat(const std::vector<std::string>& tokens, std::size_t index, float fallback = 0.0f) {
    if (index >= tokens.size()) return fallback;
    const std::string& token = tokens[index];
    if (token.empty()) return fallback;
    char* end = nullptr;
    const float value = std::strtof(token.c_str(), &end);
    if (end == token.c_str()) return fallback;
    return value;
}

double TokenDouble(const std::vector<std::string>& tokens, std::size_t index, double fallback = 0.0) {
    if (index >= tokens.size()) return fallback;
    const std::string& token = tokens[index];
    if (token.empty()) return fallback;
    char* end = nullptr;
    const double value = std::strtod(token.c_str(), &end);
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

// bool8: the retail scripts and the QVM identifier table both write TRUE/FALSE.
bool TokenBool(const std::vector<std::string>& tokens, std::size_t index, bool fallback = false) {
    if (index >= tokens.size()) return fallback;
    const std::string& token = tokens[index];
    if (token == "TRUE") return true;
    if (token == "FALSE") return false;
    return TokenInt(tokens, index, fallback ? 1 : 0) != 0;
}

std::string TokenString(const std::vector<std::string>& tokens, std::size_t index,
                        const char* fallback = "") {
    if (index >= tokens.size()) return fallback;
    return tokens[index];
}

// The editor's argTokens keep a string argument's quotes
// (level/level_objects.cpp:703 strips them at the use site), so the string
// fields below are unquoted the same way TaskSchemaNS::UnquoteToken does
// (level/task_schema.cpp:440-444). `filter_name` has to be comparable against
// the "CAMERAFILTER_TYPE_*" constants and the expressions are shown to the user,
// so the quotes are not useful to keep.
std::string UnquoteToken(const std::string& token) {
    if (token.size() >= 2 && token.front() == '"' && token.back() == '"')
        return token.substr(1, token.size() - 2);
    return token;
}

std::string TokenUnquoted(const std::vector<std::string>& tokens, std::size_t index,
                          const char* fallback = "") {
    return UnquoteToken(TokenString(tokens, index, fallback));
}

// x87 fcom equality (dxinit.h:31-34): unordered counts as equal, so this reports
// NaN equal to everything. Two of CutScene_Spline's guards depend on it.
bool X87Equal(long double a, long double b) {
    return !(a < b) && !(a > b);
}

// `round_ll` (src/mission/mission.h:113-119 and the identical copies in nine
// other headers) is `fistpll` with the x87 control word's rounding mode, which
// the engine never changes from the default round-to-nearest-even. The
// "integer indefinite" the x87 produces for NaN or out-of-range input is
// 0x8000000000000000, whose low 32 bits are zero; the engine's `(int)` cast
// truncates to those, which is the same operation as the cast below. Same
// transcription as igi::SmokeRoundInt (runtime/smoke.cpp).
int RoundLl(long double value) {
    if (std::isnan(value)) return 0;
    if (!(value > -9.223372036854775808e18L && value < 9.223372036854775808e18L)) return 0;
    const long long rounded = static_cast<long long>(std::nearbyint(value));
    return static_cast<int>(rounded);
}

}  // namespace

// ---------------------------------------------------------------------------
// The authored parameter tables
// ---------------------------------------------------------------------------

namespace {

// EditCamera_Init 0x507F10, src/editcam/editcamera.c:88-107. 16 ParamTable_Add
// calls into a `ParamTable_New(17)` table (editcamera.c:83) -- the original
// reserves one slot more than it fills. Token indices are in the editor's
// argTokens layout ([3] is the first declared parameter).
//
// The authored names are the retail declaration's
// (level1/objects.qsc:24-25, LEVEL2/objects.qsc:21-22), which names all sixteen
// -- including "Time of day (-1 means use default)" for unk98, the sentence that
// fixes the substitution CutScene_Interpolate makes at cutscene.c:488-492, and
// "Noise" for filterLevel.
const CutSceneParamDesc kKeyParams[] = {
    {"Position", "pos", "ObjectPos", 3, 3},
    {"Alpha", "angles[0]", "Angle", 6, 1},
    {"Beta", "angles[1]", "Angle", 7, 1},
    {"Gamma", "angles[2]", "Angle", 8, 1},
    {"FOV", "fov", "Real32", 9, 1},
    {"Duration", "duration", "Real32", 10, 1},
    {"Link task ID", "posAttach", "Int32", 11, 1},
    {"Update link continously", "posFollow", "bool8", 12, 1},
    {"Target task ID", "rotAttach", "Int32", 13, 1},
    {"Update target continously", "rotFollow", "bool8", 14, 1},
    {"Smooth to next", "smooth", "bool8", 15, 1},
    {"Time of day (-1 means use default)", "unk98", "Real32", 16, 1},
    {"FILTER", "filterName", "EnumString32", 17, 1},
    {"Noise", "filterLevel", "Real32", 18, 1},
    {"Filter color", "color", "RGB", 19, 3},
    {"Camera shake", "shake", "Real32", 22, 1},
};

// CutScene_Open 0x4F4B00, src/cutscene/cutscene.c:250-263. 14 ParamTable_Add
// calls into `ParamTable_New(14)` (cutscene.c:249). The authored names are the
// ones the retail declaration line gives (level1/objects.qsc:9-10), and they
// match the C table in order.
const CutSceneParamDesc kScreenParams[] = {
    {"Position", "pos", "ObjectPos", 3, 3},
    {"Orientation", "matrix", "Real32x9", 6, 3},
    {"Run", "runExpr", "VarString", 9, 1},
    {"Reset", "resetExpr", "VarString", 10, 1},
    {"Time delta (seconds)", "speedExpr", "VarString", 11, 1},
    {"Start time (seconds)", "startTime", "Real32", 12, 1},
    {"Initial run", "isRun", "bool8", 13, 1},
    {"Time scale", "timeScale", "Real32", 14, 1},
    {"Viewport height factor", "screenSize", "Real32", 15, 1},
    {"Viewport height factor fade in time", "fadeIn", "Real32", 16, 1},
    {"Viewport height factor fade out time", "fadeOut", "Real32", 17, 1},
    {"Time of day", "unk210", "Real32", 18, 1},
    {"Start expression", "runScript", "VarString", 19, 1},
    {"Stop expression", "stopScript", "VarString", 20, 1},
};

}  // namespace

int CutSceneKeyParamCount() {
    return static_cast<int>(sizeof(kKeyParams) / sizeof(kKeyParams[0]));
}

const CutSceneParamDesc* CutSceneKeyParamTable() {
    return kKeyParams;
}

int CutSceneScreenParamCount() {
    return static_cast<int>(sizeof(kScreenParams) / sizeof(kScreenParams[0]));
}

const CutSceneParamDesc* CutSceneScreenParamTable() {
    return kScreenParams;
}

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

bool ParseCutSceneKey(const std::vector<std::string>& arg_tokens, CutSceneKeyParams* out) {
    if (out == nullptr) return false;
    if (arg_tokens.size() < static_cast<std::size_t>(kCutSceneKeyTokenCount)) return false;

    CutSceneKeyParams key;  // EditCamera_Create's defaults (editcamera.c:112-132)
    for (int axis = 0; axis < 3; ++axis) {
        key.position[axis] = TokenDouble(arg_tokens, 3 + static_cast<std::size_t>(axis));
        key.angles[axis] = TokenFloat(arg_tokens, 6 + static_cast<std::size_t>(axis));
        key.colour[axis] = TokenFloat(arg_tokens, 19 + static_cast<std::size_t>(axis));
    }
    key.fov = TokenFloat(arg_tokens, 9, key.fov);
    key.duration = TokenFloat(arg_tokens, 10, key.duration);
    key.pos_attach = TokenInt(arg_tokens, 11, key.pos_attach);
    key.pos_follow = TokenBool(arg_tokens, 12, key.pos_follow);
    key.rot_attach = TokenInt(arg_tokens, 13, key.rot_attach);
    key.rot_follow = TokenBool(arg_tokens, 14, key.rot_follow);
    key.smooth = TokenBool(arg_tokens, 15, key.smooth);
    key.time_of_day_override = TokenFloat(arg_tokens, 16, key.time_of_day_override);
    key.filter_name = TokenUnquoted(arg_tokens, 17, key.filter_name.c_str());
    key.filter_level = TokenFloat(arg_tokens, 18, key.filter_level);
    key.shake = TokenFloat(arg_tokens, 22, key.shake);

    *out = std::move(key);
    return true;
}

bool ParseCutSceneScreen(const std::vector<std::string>& arg_tokens, CutSceneScreenParams* out) {
    if (out == nullptr) return false;
    if (arg_tokens.size() < static_cast<std::size_t>(kCutSceneScreenTokenCount)) return false;

    CutSceneScreenParams screen;  // CutScene_CreateHandler's defaults (cutscene.c:269-297)
    screen.run_expression = TokenUnquoted(arg_tokens, 9);
    screen.reset_expression = TokenUnquoted(arg_tokens, 10);
    screen.time_delta_expression = TokenUnquoted(arg_tokens, 11);
    screen.start_time_seconds = TokenFloat(arg_tokens, 12, screen.start_time_seconds);
    screen.initial_run = TokenBool(arg_tokens, 13, screen.initial_run);
    screen.time_scale = TokenFloat(arg_tokens, 14, screen.time_scale);
    screen.screen_size = TokenFloat(arg_tokens, 15, screen.screen_size);
    screen.fade_in_seconds = TokenFloat(arg_tokens, 16, screen.fade_in_seconds);
    screen.fade_out_seconds = TokenFloat(arg_tokens, 17, screen.fade_out_seconds);
    screen.time_of_day = TokenFloat(arg_tokens, 18, screen.time_of_day);
    screen.start_expression = TokenUnquoted(arg_tokens, 19);
    screen.stop_expression = TokenUnquoted(arg_tokens, 20);

    *out = std::move(screen);
    return true;
}

// ---------------------------------------------------------------------------
// CutScene_CollectKeys 0x4F4880
// ---------------------------------------------------------------------------

namespace {

// The engine's do-while over a sibling chain: emit the node, advance the clock,
// recurse into the node's own child chain, then continue with the next sibling.
// `over_capacity` reports the write the engine would have made past keys[100].
void WalkKeys(const std::vector<CutSceneKeyNode>& siblings, float scale, float* time,
              std::vector<CutSceneKey>* keys, bool* over_capacity) {
    for (const CutSceneKeyNode& node : siblings) {
        if (*over_capacity) return;
        if (keys->size() >= static_cast<std::size_t>(kCutSceneKeyCapacity)) {
            *over_capacity = true;
            return;
        }
        CutSceneKey key;
        key.params = node.params;
        key.start_time = *time;
        keys->push_back(std::move(key));
        *time = scale * node.params.duration + *time;  // cutscene.c:172
        WalkKeys(node.children, scale, time, keys, over_capacity);
    }
}

}  // namespace

std::vector<CutSceneKey> CollectCutSceneKeys(const std::vector<CutSceneKeyNode>& cut_scene_children,
                                             float scale, float* out_end) {
    std::vector<CutSceneKey> keys;
    float time = 0.0f;
    bool over_capacity = false;

    WalkKeys(cut_scene_children, scale, &time, &keys, &over_capacity);

    if (over_capacity) {
        // The engine would have written past its stack array here
        // (cutscene.c:170-173 has no bounds check). Report by returning nothing
        // rather than a truncated timeline that would silently preview wrong.
        keys.clear();
        time = 0.0f;
    }
    if (out_end != nullptr) *out_end = time;
    return keys;
}

const CutSceneKey* CutSceneKeyAt(const std::vector<CutSceneKey>& keys, int index) {
    if (keys.empty()) return nullptr;
    if (index < 0) index = 0;
    if (index >= static_cast<int>(keys.size())) index = static_cast<int>(keys.size()) - 1;
    return &keys[static_cast<std::size_t>(index)];
}

void UnwrapCutSceneAngles(std::vector<CutSceneKey>* keys) {
    if (keys == nullptr || keys->empty()) return;

    float prev[3];
    for (int axis = 0; axis < 3; ++axis) {
        prev[axis] = (*keys)[0].params.angles[axis];
    }

    for (std::size_t i = 1; i < keys->size(); ++i) {
        for (int axis = 0; axis < 3; ++axis) {
            float& angle = (*keys)[i].params.angles[axis];
            if (!std::isfinite(angle) || !std::isfinite(prev[axis])) {
                // The engine's second loop cannot terminate on a NaN
                // (cutscene.c:205-208); leave the authored value alone instead.
                prev[axis] = angle;
                continue;
            }
            while (angle - prev[axis] > kCutScenePi) angle -= kCutSceneTwoPi;
            while (!(angle - prev[axis] >= kCutSceneNegPi)) angle += kCutSceneTwoPi;
            prev[axis] = angle;
        }
    }
}

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------

float CutSceneTimeFromTick(int tick) {
    // cutscene.c:427: `float t = (long double)c->tick / gCutSceneTicksPerSec`.
    return static_cast<float>(static_cast<long double>(tick) /
                              static_cast<long double>(kCutSceneTicksPerSecond));
}

int CutSceneTickFromTime(double seconds) {
    // cutscene.c:445 and :663: `(int)round_ll((long double)tps * seconds)`.
    return RoundLl(static_cast<long double>(kCutSceneTicksPerSecond) *
                   static_cast<long double>(seconds));
}

CutSceneKeyWindow SelectCutSceneKeys(const std::vector<CutSceneKey>& keys, float end, float t) {
    CutSceneKeyWindow window;
    if (keys.empty()) return window;

    int cur = -1;
    bool found = false;
    for (std::size_t i = 0; i < keys.size() && !found; ++i) {
        if (keys[i].start_time > t) {
            found = true;
        } else {
            cur = static_cast<int>(i);
        }
    }
    if (t > end) window.finished = true;

    window.active_key = cur;
    const CutSceneKey* k[4];
    for (int slot = 0; slot < 4; ++slot) {
        k[slot] = CutSceneKeyAt(keys, cur - 1 + slot);
    }
    // cutscene.c:455-460, in this order: the third test reads k[2] as possibly
    // rewritten by the second.
    if (!k[0]->params.smooth) k[0] = k[1];
    if (!k[1]->params.smooth) k[2] = k[1];
    if (!k[2]->params.smooth) k[3] = k[2];
    for (int slot = 0; slot < 4; ++slot) {
        window.indices[slot] = static_cast<int>(k[slot] - keys.data());
    }
    return window;
}

// ---------------------------------------------------------------------------
// Interpolation
// ---------------------------------------------------------------------------

long double CutSceneSpline(double p0, double p1, double p2, double p3, double t0, double t1, double t2,
                           double t3, double t) {
    // cutscene.c:631-654, with Hermite_Init 0x518130 / Hermite_Eval 0x518190 from
    // flare.c:46-60. Every intermediate keeps the engine's own width: the
    // parameters are double, `half` and `d` are long double, the tangent
    // expressions are double until the division by `d`, m1/m2/u are stored back
    // into double locals, and Hermite_Eval's leading `u * h[0]` is a double
    // product.
    const long double half = (p3 - p1) * kCutSceneHalfD;
    double m1 = 0.0;
    double m2 = 0.0;
    double u = 0.0;

    long double d = t2 - t0;
    if (!X87Equal(d, 0.0)) {
        m1 = static_cast<double>(static_cast<long double>((t1 - t0) * 2) / d *
                                 ((p2 - p0) * kCutSceneHalfD));
    }
    d = t3 - t1;
    if (!X87Equal(d, 0.0)) {
        m2 = static_cast<double>(static_cast<long double>((t2 - t1) * 2) / d * half);
    }

    double h[4];
    h[0] = m1;
    h[1] = p1;
    h[2] = p1 + p1 - (p2 + p2) + m1 + m2;
    h[3] = p2 * 3.0 - p1 * 3.0 - (m1 + m1) - m2;

    d = t2 - t1;
    if (!X87Equal(d, 0.0)) {
        u = static_cast<double>((t - t1) / d);
    }

    const long double uu = static_cast<long double>(u) * u;
    return static_cast<long double>(u * h[0]) + (uu * u * h[2] + uu * h[3]) + h[1];
}

CutSceneFade CutSceneFadeMix(const CutSceneScreenParams& screen, float t, float end) {
    CutSceneFade fade;
    float fade_in = 1.0f;
    float fade_out = 1.0f;

    // cutscene.c:549-559. `x87_eq` treats NaN as equal to zero, so a NaN fadeIn
    // skips the ramp and a NaN fadeOut takes the constant branch. The ramp's
    // division is `t / c->fadeIn` with `t` a double and `fadeIn` a float, i.e. a
    // double division stored back into a float; `d` is a double subtraction
    // widened to the long double local.
    if (!X87Equal(screen.fade_in_seconds, 0.0f) && screen.fade_in_seconds > t) {
        fade_in = static_cast<float>(static_cast<double>(t) / screen.fade_in_seconds);
    }
    if (X87Equal(screen.fade_out_seconds, 0.0f)) {
        fade_out = 1.0f;
    } else {
        const long double d = static_cast<double>(end) - static_cast<double>(t);
        if (!(d >= screen.fade_out_seconds)) {
            fade_out = static_cast<float>(d / screen.fade_out_seconds);
        }
    }

    float mix = !(fade_in >= fade_out) ? fade_in : fade_out;
    if (mix < 0.0f) {
        mix = 0.0f;
    } else if (!(mix <= 1.0f)) {
        mix = 1.0f;
    }
    fade.mix = mix;
    // cutscene.c:587.
    fade.screen_factor = (1.0f - mix) + mix * screen.screen_size;
    return fade;
}

void CutSceneShakeDelta(float shake, const long double random_six[6], double out_position_delta[3],
                        double out_angle_delta[3]) {
    // cutscene.c:569-575. Draw order: x, y, z for the position, then the three
    // angle draws in the order s4, s5, s6 -- and Matrix_SetEuler(&shk, s6, s5, s4)
    // means s6 is the X angle and s4 the Z angle.
    for (int axis = 0; axis < 3; ++axis) {
        out_position_delta[axis] =
            static_cast<double>((kCutSceneHalf - random_six[axis]) * shake * kCutSceneUnits);
    }
    const double s4 = static_cast<double>((kCutSceneHalf - random_six[3]) * shake * kCutSceneShakeAngle);
    const double s5 = static_cast<double>((kCutSceneHalf - random_six[4]) * shake * kCutSceneShakeAngle);
    const double s6 = static_cast<double>((kCutSceneHalf - random_six[5]) * shake * kCutSceneShakeAngle);
    out_angle_delta[0] = s6;
    out_angle_delta[1] = s5;
    out_angle_delta[2] = s4;
}

// ---------------------------------------------------------------------------
// The preview rule
// ---------------------------------------------------------------------------

bool EvaluateCutScene(const std::vector<CutSceneKey>& keys, float end,
                      const CutSceneScreenParams& screen, float t, bool band_filter_active,
                      CutSceneCameraState* out) {
    if (out == nullptr) return false;
    if (keys.empty()) return false;

    const CutSceneKeyWindow window = SelectCutSceneKeys(keys, end, t);
    const CutSceneKey* k[4];
    for (int slot = 0; slot < 4; ++slot) {
        k[slot] = CutSceneKeyAt(keys, window.indices[slot]);
    }

    // cutscene.c:488-492: a key's own unk98, or the CutScene's unk210 when it is
    // not >= 0 (which is also true for NaN).
    float w[4];
    for (int slot = 0; slot < 4; ++slot) {
        w[slot] = k[slot]->params.time_of_day_override;
        if (!(w[slot] >= 0.0f)) w[slot] = screen.time_of_day;
    }

    const double t0 = k[0]->start_time;
    const double t1 = k[1]->start_time;
    const double t2 = k[2]->start_time;
    const double t3 = k[3]->start_time;

    CutSceneCameraState state;
    const auto spline = [&](double p0, double p1, double p2, double p3) {
        return CutSceneSpline(p0, p1, p2, p3, t0, t1, t2, t3, t);
    };

    for (int axis = 0; axis < 3; ++axis) {
        state.position[axis] = static_cast<double>(spline(k[0]->params.position[axis],
                                                          k[1]->params.position[axis],
                                                          k[2]->params.position[axis],
                                                          k[3]->params.position[axis]));
        // The engine stores the spline result into a float before
        // Matrix_SetEuler, so the state carries the float (cutscene.c:496-498).
        state.angles[axis] = static_cast<float>(spline(k[0]->params.angles[axis],
                                                       k[1]->params.angles[axis],
                                                       k[2]->params.angles[axis],
                                                       k[3]->params.angles[axis]));
    }
    state.fov = static_cast<float>(
        spline(k[0]->params.fov, k[1]->params.fov, k[2]->params.fov, k[3]->params.fov));
    state.shake = static_cast<float>(spline(k[0]->params.shake, k[1]->params.shake,
                                            k[2]->params.shake, k[3]->params.shake));
    state.time_of_day =
        static_cast<float>(CutSceneSpline(w[0], w[1], w[2], w[3], t0, t1, t2, t3, t));
    state.filter_level = static_cast<float>(spline(k[0]->params.filter_level,
                                                   k[1]->params.filter_level,
                                                   k[2]->params.filter_level,
                                                   k[3]->params.filter_level));

    // cutscene.c:504-527. `span` is a float subtraction and `f` holds a double,
    // because that is the width the engine computes each expression at.
    const long double d = static_cast<float>(t2) - static_cast<float>(t1);
    const float span = static_cast<float>(d);
    if (X87Equal(d, 0.0)) {
        for (int axis = 0; axis < 3; ++axis) {
            state.colour[axis] = k[1]->params.colour[axis];
        }
    } else {
        long double f;
        if (band_filter_active && X87Equal(static_cast<long double>(state.filter_level), 0.0)) {
            if (!(span > kCutSceneHalf)) {
                f = 1.0;
            } else {
                f = static_cast<double>(t - static_cast<double>(t1)) /
                    static_cast<double>(span - kCutSceneHalf);
                if (f < 0.0L) {
                    f = 0.0L;
                } else if (!(f <= 1.0L)) {
                    f = 1.0L;
                }
            }
        } else {
            f = static_cast<double>(t - static_cast<double>(t1)) / static_cast<double>(span);
        }
        for (int axis = 0; axis < 3; ++axis) {
            // The difference is a float subtraction, then widened for `* f`.
            const float a = k[1]->params.colour[axis];
            const float b = k[2]->params.colour[axis];
            state.colour[axis] = static_cast<float>(static_cast<long double>(b - a) * f + a);
        }
    }

    const CutSceneFade fade = CutSceneFadeMix(screen, t, end);
    state.fade_mix = fade.mix;
    state.screen_factor = fade.screen_factor;
    state.active_key = window.active_key;
    state.finished = window.finished;

    *out = state;
    return true;
}

}  // namespace igi
