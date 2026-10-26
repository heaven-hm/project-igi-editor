// cutscene_rules.h - the authored cutscene camera timeline, resolved the way
// IGI-RE resolves it (#80 "Intro/outro cutscenes").
//
// WHAT THIS IS FOR. The editor is a level/object editor, so the half of #80 it
// needs is the AUTHORED camera/screen data model and the rule that turns it
// into a camera pose at a time -- enough to place a cutscene and preview it.
// It is deliberately NOT the retail mission-flow state machine: the run/reset
// expression gating, the save/load and register-vars handlers, the four screen
// filters, the leave-world teardown and the IntroScreen .avi screens are all
// left out, and section "Out of scope" below says why for each.
//
// Everything here is cited to IGI-RE (`/Users/haseeb-mir/Documents/Code/IGI-RE`),
// which is the only evidence source used. The authored parameter names and
// types come from the retail `Task_DeclareParameters("CutScene", ...)` and
// `Task_DeclareParameters("EditCamera", ...)` lines, which ship inside every
// OBJECTS.QVM that authors one and are also present as decompiled text in
// `MISSIONS/location0/level1/objects.qsc:9-10` and `:4`.
//
// The IGI-RE files this module is built from:
//
//   src/cutscene/cutscene.c   CutScene_Open 0x4F4B00 (the 14-parameter table,
//                             the 0..20 time range, the 30 Hz tick rate),
//                             CutScene_CollectKeys 0x4F4880, CutScene_GetKey
//                             0x4F4910, CutScene_UnwrapAngles 0x4F4940,
//                             CutScene_Update 0x4F5400, CutScene_Interpolate
//                             0x4F55D0, CutScene_Spline 0x4F6290
//   src/editcam/editcamera.c  EditCamera_Init 0x507F10 (the 16-entry table),
//                             EditCamera_Create 0x508290 (the defaults)
//   src/terrdraw/flare.c      Hermite_Init 0x518130, Hermite_Eval 0x518190
//   src/physobj/paramops.c    ParamReal32x9_Parse 0x4EFBA0 reads THREE Euler
//                             angles; Param_Three 0x4EFB00 is the arg count, so
//                             "Orientation" consumes 3 script args and
//                             "Position"/"Colour" 3 each
//   src/game/game.c:465       CutScene_SetTicksPerSecond(30)
//   src/math/matrix.c         Matrix_SetEuler 0x4B38E0 (the editor already has
//                             this as igi::IgiEulerRotationMatrix, see
//                             docs/igi-re-parity/SCAMERA_PARAMS.md)
//   src/dxinit/dxinit.h:21-34 x87_fistp (fistpll) and x87_eq
//
// Named divergences (never silently fixed):
//   * `CutScene_UnwrapAngles` (cutscene.c:205-208) hangs forever on a NaN angle
//     -- `while (!(x >= kNegPi))` is true for NaN and `x += kTwoPi` cannot
//     escape. The engine's own comment admits it ("a NaN hangs, like the
//     original"). A level editor must not hang on authored data, so the port
//     leaves a non-finite angle exactly as authored instead. Every finite
//     input takes the identical path.
//   * `x87_eq` (dxinit.h:30-34) is `!(a < b) && !(a > b)`, so it reports NaN
//     EQUAL to anything. Reproduced, because two of the spline's guards turn on
//     it.
//   * The shake offsets need the engine's shared RNG (Random_Float 0x4B4770 on
//     gCutSceneRandom). A preview cannot reproduce that sequence -- the global
//     has already been consumed an unknown number of times by the time a
//     cutscene runs -- so the six draws are a caller argument, not an internal
//     generator. The draw ORDER and the scale factors are the engine's.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace igi {

// ---------------------------------------------------------------------------
// Engine constants. Every one is IGI-RE DATA.
// ---------------------------------------------------------------------------

constexpr double kCutScenePi = 0x1.921fb54442d1bp+1;    // cutscene.c:139 kPi
constexpr double kCutSceneNegPi = -0x1.921fb54442d1bp+1; // cutscene.c:140 kNegPi
constexpr float kCutSceneTwoPi = 0x1.921fb6p+2f;        // cutscene.c:141 kTwoPi
constexpr double kCutSceneHalfD = 0.5;                  // cutscene.c:142 kHalfD
constexpr float kCutSceneHalf = 0.5f;                   // cutscene.c:143 kHalf
constexpr float kCutSceneUnits = 4096.0f;               // cutscene.c:144 kUnits
constexpr float kCutSceneShakeAngle = 0x1.1171bap-1f;   // cutscene.c:145 kShakeAngle
constexpr float kCutSceneFilterDepth = 0x1.333334p+9f;  // cutscene.c:146 kFilterDepth
constexpr float kCutSceneWideScreenUnknown = 0x1.99999ap+8f;  // cutscene.c:147 kWideScreenUnk

// gCutSceneTimeRange, the range bound on fadeIn/fadeOut/unk210 (cutscene.c:231-232).
constexpr float kCutSceneTimeRangeMin = 0.0f;
constexpr float kCutSceneTimeRangeMax = 20.0f;

// CutScene_SetTicksPerSecond(30) at src/game/game.c:465. Every tick <-> second
// conversion below goes through this one number.
constexpr int kCutSceneTicksPerSecond = 30;

// `CutKey keys[100]` (cutscene.c:419) -- the engine's own fixed capacity, with
// no bounds check on the write (cutscene.c:170-173). A cutscene that authors
// more keys than this overruns the engine's stack; the port refuses instead of
// reproducing that, and reports it.
constexpr int kCutSceneKeyCapacity = 100;

// The camera Camera_Set is handed (cutscene.c:588):
//   Camera_Set(gCutSceneCamera, &pos, &rot, fov, 1.0f, 1000.0f, frame, NULL,
//              unk98, 0)
constexpr float kCutSceneCameraAspect = 1.0f;
constexpr float kCutSceneCameraPriority = 1000.0f;

// Authored argument counts. `[0]=task id, [1]=type, [2]=name, [3]=parameter 0`.
//
// EditCamera: 3 (Position) + 3 (three Angle slots) + 1 fov + 1 duration +
// 1 posAttach + 1 posFollow + 1 rotAttach + 1 rotFollow + 1 smooth + 1 unk98 +
// 1 filterName + 1 filterLevel + 3 colour + 1 shake = 20 declared args, so 23
// tokens. Every one of the 123 authored EditCamera instances in the three retail
// levels that have a cutscene has exactly 23.
constexpr int kCutSceneKeyTokenCount = 23;
// CutScene: 3 (Position) + 3 (Orientation as three Euler angles) + Run + Reset +
// "Time delta (seconds)" + "Start time (seconds)" + "Initial run" +
// "Time scale" + "Viewport height factor" + fade in + fade out + "Time of day" +
// "Start expression" + "Stop expression" = 18 declared args, so 21 tokens.
constexpr int kCutSceneScreenTokenCount = 21;

// ---------------------------------------------------------------------------
// The two authored parameter tables
// ---------------------------------------------------------------------------

// One row of an authored parameter table. `token_index` is the argTokens index
// of the row's first script argument; `token_count` is how many the retail type
// consumes (paramops.c's arg-count function, e.g. Param_Three 0x4EFB00 for
// ObjectPos/Real32x9/RGB, 1 for every scalar and string).
struct CutSceneParamDesc {
    const char* authored_name;  // the retail declaration string
    const char* engine_field;   // the IGI-RE struct field the slot writes
    const char* type_name;      // the retail declared type
    int token_index;            // first script-argument index in argTokens
    int token_count;            // script arguments consumed
};

// The 16 rows registered by EditCamera_Init (editcamera.c:88-107), in
// ParamTable_Add order, which is the order the retail declaration line lists
// them in (level1/objects.qsc:24-25). The table itself is `ParamTable_New(17)`
// (editcamera.c:83), so the original reserves one slot more than it fills.
int CutSceneKeyParamCount();
const CutSceneParamDesc* CutSceneKeyParamTable();

// The 14 rows registered by CutScene_Open (cutscene.c:250-263), in
// ParamTable_Add order, which is the order the retail declaration line lists
// them in (level1/objects.qsc:9-10).
int CutSceneScreenParamCount();
const CutSceneParamDesc* CutSceneScreenParamTable();

// ---------------------------------------------------------------------------
// The authored key: "EditCamera"
// ---------------------------------------------------------------------------

// One authored EditCamera key. Field names follow the AUTHORED names where the
// two disagree, and the IGI-RE struct field is named in the comment.
//
// `filter_name` is `EditCamera.filterName` (an EnumString32 whose constants are
// every "CAMERAFILTER_TYPE_.*" symbol, editcamera.c:72-73); `filter` is the
// integer that name resolves to and is set by EditCamera_Changed, not by the
// authored arguments, so it is not modelled here.
//
// `time_of_day_override` is `EditCamera.unk98` (editcamera.c:99). CutScene_Interpolate
// reads it as "this key's time of day", and substitutes the CutScene's own
// `unk210` whenever it is not >= 0 (cutscene.c:488-492) -- so the authored -1
// every retail key carries means "inherit".
struct CutSceneKeyParams {
    double position[3] = {0.0, 0.0, 0.0};   // Position            -> EditCamera.pos
    float angles[3] = {0.0f, 0.0f, 0.0f};   // Alpha/Beta/Gamma    -> EditCamera.angles
    float fov = 1.0f;                       // FOV                 -> EditCamera.fov
    float duration = 0.0f;                  // Duration            -> EditCamera.duration
    int pos_attach = -1;                    // Link task ID               -> EditCamera.posAttach
    bool pos_follow = true;                 // Update link continously    -> EditCamera.posFollow
    int rot_attach = -1;                    // Target task ID             -> EditCamera.rotAttach
    bool rot_follow = true;                 // Update target continously  -> EditCamera.rotFollow
    bool smooth = true;                     // Smooth to next             -> EditCamera.smooth
    // "Time of day (-1 means use default)" -> EditCamera.unk98
    float time_of_day_override = -1.0f;
    std::string filter_name = "CAMERAFILTER_TYPE_NONE";  // FILTER       -> EditCamera.filterName
    float filter_level = 0.5f;              // Noise                      -> EditCamera.filterLevel
    float colour[3] = {0.0f, 0.0f, 0.0f};   // Filter color               -> EditCamera.color
    float shake = 0.0f;                     // Camera shake               -> EditCamera.shake
};

// The defaults are EditCamera_Create's (editcamera.c:112-132): smooth/posFollow/
// rotFollow true, posAttach/rotAttach -1, fov 1.0, unk98 -1.0, filterLevel 0.5,
// colour and shake zero, filterName "CAMERAFILTER_TYPE_NONE".
//
// Parses one authored EditCamera call. `arg_tokens` is the LevelObject::argTokens
// layout ([0]=task id, [1]=type, [2]=name, [3]=parameter 0). Returns false when
// the call is shorter than the 23 tokens the table requires, leaving `out`
// untouched -- a truncated key is reported rather than filled with defaults.
//
// A string argument keeps its quotes in argTokens
// (level/level_objects.cpp:703 strips them at the use site), so `filter_name` is
// unquoted here the same way TaskSchemaNS::UnquoteToken does
// (level/task_schema.cpp:440-444), which makes it comparable against the
// "CAMERAFILTER_TYPE_*" constants. (The Smoke port keeps the raw token for its
// expression; this module does not, and says so rather than leaving it implied.)
bool ParseCutSceneKey(const std::vector<std::string>& arg_tokens, CutSceneKeyParams* out);

// ---------------------------------------------------------------------------
// The authored screen/timeline header: "CutScene"
// ---------------------------------------------------------------------------

// The CutScene parameters that shape the whole timeline and the frame around it.
// The run/reset/speed EXPRESSIONS are carried as text but not interpreted: they
// gate the retail mission flow, which this module leaves out.
//
// `time_scale` is the `scale` argument of CutScene_CollectKeys (cutscene.c:163)
// and the multiplier on `speedExpr` in CutScene_RunHandler (cutscene.c:385-391).
// `screen_size` is the authored "Viewport height factor" and is the
// `c->screenSize` of CutScene_Interpolate (cutscene.c:587). `time_of_day` is
// `c->unk210`, the fallback for a key's own unk98.
struct CutSceneScreenParams {
    std::string run_expression;      // Run
    std::string reset_expression;    // Reset
    std::string time_delta_expression;  // "Time delta (seconds)" -> CutScene.speedExpr
    float start_time_seconds = 0.0f;    // "Start time (seconds)" -> CutScene.startTime
    bool initial_run = true;            // "Initial run"          -> CutScene.isRun
    float time_scale = 1.0f;            // "Time scale"           -> CutScene.timeScale
    float screen_size = 1.0f;           // "Viewport height factor" -> CutScene.screenSize
    float fade_in_seconds = 0.0f;       // "... fade in time"     -> CutScene.fadeIn
    float fade_out_seconds = 0.0f;      // "... fade out time"    -> CutScene.fadeOut
    float time_of_day = 0.0f;           // "Time of day"          -> CutScene.unk210
    std::string start_expression;    // "Start expression" -> CutScene.runScript
    std::string stop_expression;     // "Stop expression"  -> CutScene.stopScript
};

// Defaults are CutScene_CreateHandler's (cutscene.c:269-297): screenSize 1.0,
// timeScale 1.0, isRun 1, startTime 0, unk210 0, tick 0, key -1.
//
// The five expression strings are unquoted like `filter_name` above; they are
// carried as text and never evaluated by this module.
bool ParseCutSceneScreen(const std::vector<std::string>& arg_tokens, CutSceneScreenParams* out);

// ---------------------------------------------------------------------------
// The timeline: CutScene_CollectKeys 0x4F4880
// ---------------------------------------------------------------------------

// One key on the resolved timeline. `start_time` is the accumulated value of
// CutScene_CollectKeys' `*time` when the key was visited:
//
//     keys[count].time = *time;
//     *time = scale * k->duration + *time;      // cutscene.c:170-172
//
// so a key's start is the sum of `scale * duration` over every key visited
// before it, and `end` -- the value *time holds after the walk -- is that sum
// over ALL keys, including the last one's own duration.
struct CutSceneKey {
    CutSceneKeyParams params;
    float start_time = 0.0f;
};

// The authored key hierarchy, as the engine walks it. `children` are the
// node's EditCamera children in file order.
//
// The engine's walk (cutscene.c:161-180) is: emit k; add scale*k->duration;
// recurse into k's FIRST EditCamera child (which itself emits and continues
// through that child's own EditCamera siblings); then continue with k's next
// EditCamera sibling. Depth first, and a child subtree is visited BEFORE the
// parent's next sibling. Every retail key is a flat sibling of the others
// (measured: all 123 across the three levels sit at one depth), so the
// recursion is only exercised by a synthetic tree -- which is why the tests
// build one.
struct CutSceneKeyNode {
    CutSceneKeyParams params;
    std::vector<CutSceneKeyNode> children;
};

// CutScene_CollectKeys. `cut_scene_children` is the CutScene task's own
// EditCamera child chain (the engine's `Task_FindChild(&c->task, ...)` at
// cutscene.c:431). Writes the total timeline length to `out_end` when non-null.
//
// Returns an empty vector -- not a truncated one -- when the walk would exceed
// kCutSceneKeyCapacity, which the engine would instead write past the end of
// its stack array.
std::vector<CutSceneKey> CollectCutSceneKeys(const std::vector<CutSceneKeyNode>& cut_scene_children,
                                             float scale, float* out_end);

// CutScene_GetKey 0x4F4910: index clamped to [0, count-1] at BOTH ends. The
// engine reads `keys[count - 1]` for an index past the end and `keys[0]` for a
// negative one, which is what makes the first and last key hold their pose
// instead of extrapolating. Returns nullptr only for an empty timeline, where
// the engine would read `keys[-1]`.
const CutSceneKey* CutSceneKeyAt(const std::vector<CutSceneKey>& keys, int index);

// CutScene_UnwrapAngles 0x4F4940: walk the keys in timeline order and keep each
// angle within pi of the previous key's, adding or subtracting 2*pi. `prev`
// starts at the FIRST key's own angles, so the first key is never touched.
// Non-finite angles are left alone (see the header's divergence note).
void UnwrapCutSceneAngles(std::vector<CutSceneKey>* keys);

// ---------------------------------------------------------------------------
// Time: CutScene_Update 0x4F5400 and CutScene_ControlHandler 0x4F6500
// ---------------------------------------------------------------------------

// `t = (long double)c->tick / gCutSceneTicksPerSec` (cutscene.c:427).
float CutSceneTimeFromTick(int tick);

// `c->tick = round_ll(gCutSceneTicksPerSec * seconds)` -- CutScene_Update's
// end-of-scene snap (cutscene.c:445) and CutScene_ParamChangedHandler's
// startTime -> tick (cutscene.c:663), with x87 `fistpll` rounding
// (dxinit.h:21-28, round-to-nearest-even).
int CutSceneTickFromTime(double seconds);

// Which keys CutScene_Update selects at time `t`, and the k[4] window
// CutScene_Interpolate is handed.
//
// The scan (cutscene.c:436-441) is the LAST key whose `time <= t`:
//     for (i = 0; i < count && !found; i++) { if (keys[i].time > t) found = 1; else cur = i; }
// so `active_key` is -1 before the first key's start time. `indices[0..3]` are
// CutScene_GetKey(active_key - 1 .. active_key + 2) AFTER the smooth collapse
// (cutscene.c:455-460), whose order matters:
//     if (!k[0]->smooth) k[0] = k[1];
//     if (!k[1]->smooth) k[2] = k[1];
//     if (!k[2]->smooth) k[3] = k[2];
// The third test reads k[2] as possibly rewritten by the second, so a
// non-smooth middle key collapses both of its tangents to itself.
//
// `finished` is `t > end` (cutscene.c:442-446); the engine sets it and rewinds
// the tick to `end`, but it does NOT change `cur`, so the selected window at the
// finished pose is still the last key's.
struct CutSceneKeyWindow {
    int active_key = -1;
    bool finished = false;
    int indices[4] = {0, 0, 0, 0};
};

CutSceneKeyWindow SelectCutSceneKeys(const std::vector<CutSceneKey>& keys, float end, float t);

// ---------------------------------------------------------------------------
// Interpolation
// ---------------------------------------------------------------------------

// CutScene_Spline 0x4F6290: a Catmull-Rom-style Hermite through p1 (time t1) and
// p2 (time t2), with the tangents scaled for UNEVEN key times:
//
//     half = (p3 - p1) * 0.5
//     m1 = (t1 - t0) * 2 / (t2 - t0) * ((p2 - p0) * 0.5)     // 0 when t2 == t0
//     m2 = (t2 - t1) * 2 / (t3 - t1) * half                  // 0 when t3 == t1
//     u  = (t - t1) / (t2 - t1)                              // 0 when t2 == t1
//     return Hermite_Eval(Hermite_Init(p1, p2, m1, m2), u)
//
// with Hermite_Init/Hermite_Eval transcribed from flare.c:46-61. The divisions
// are guarded by `x87_eq(d, 0)` (cutscene.c:640-651), which is also true for a
// NaN difference -- reproduced.
long double CutSceneSpline(double p0, double p1, double p2, double p3, double t0, double t1, double t2,
                           double t3, double t);

// CutScene_Interpolate's fade arithmetic (cutscene.c:549-567):
//
//     fadeIn  = (c->fadeIn != 0 && c->fadeIn > t) ? t / c->fadeIn : 1
//     fadeOut = (c->fadeOut == 0) ? 1
//                                 : ((end - t) >= c->fadeOut ? 1 : (end - t) / c->fadeOut)
//     mix     = clamp(min(fadeIn, fadeOut), 0, 1)
//
// and the letterbox factor derived from it:
//     screen = (1 - mix) + mix * c->screenSize
// which is 1 (no wide-screen filter) whenever the mix is 1. Both are returned.
struct CutSceneFade {
    float mix = 1.0f;
    float screen_factor = 1.0f;
};

CutSceneFade CutSceneFadeMix(const CutSceneScreenParams& screen, float t, float end);

// The shake offsets (cutscene.c:569-575), in the engine's draw order: three
// Random_Float draws for the position, then three for the angles, each mapped to
// (0.5 - r):
//
//     pos.x += (0.5 - r0) * shake * kUnits
//     pos.y += (0.5 - r1) * shake * kUnits
//     pos.z += (0.5 - r2) * shake * kUnits
//     s4 = (0.5 - r3) * shake * kShakeAngle     -> angles[2]
//     s5 = (0.5 - r4) * shake * kShakeAngle     -> angles[1]
//     s6 = (0.5 - r5) * shake * kShakeAngle     -> angles[0]
//
// `random_six` holds the six Random_Float values in that order. The shake that
// drives it is the SPLINED `shake` of the surrounding keys, not one key's.
void CutSceneShakeDelta(float shake, const long double random_six[6], double out_position_delta[3],
                        double out_angle_delta[3]);

// ---------------------------------------------------------------------------
// The whole preview rule
// ---------------------------------------------------------------------------

// The camera pose CutScene_Interpolate produces at time `t`, minus the parts
// that need a live world (see "Out of scope").
struct CutSceneCameraState {
    double position[3] = {0.0, 0.0, 0.0};
    // Euler radians in the engine's own order, ready for
    // igi::IgiEulerRotationMatrix (renderer/igi_transform.h), which is
    // Matrix_SetEuler 0x4B38E0 (matrix.c:99-110): Rz(az) * Ry(ay) * Rx(ax).
    // Float, because that is what the engine stores the spline result into
    // before handing it to Matrix_SetEuler (cutscene.c:496-498, 528).
    float angles[3] = {0.0f, 0.0f, 0.0f};
    float fov = 1.0f;                 // Camera_Set's `fov`
    float shake = 0.0f;               // the splined "Camera shake" amplitude
    float time_of_day = 0.0f;         // Camera_Set's 9th argument (the splined unk98)
    float filter_level = 0.0f;        // the splined "filterLevel"
    float colour[3] = {0.0f, 0.0f, 0.0f};  // the k[1]..k[2] colour mix
    float fade_mix = 1.0f;            // CutSceneFade::mix
    float screen_factor = 1.0f;       // CutSceneFade::screen_factor
    int active_key = -1;              // CutScene.key
    bool finished = false;            // CutScene.isFinished
};

// CutScene_Interpolate's authored-data half (cutscene.c:473-588), for a camera
// preview at time `t`:
//
//   * every value is a CutSceneSpline over the k[4] window SelectCutSceneKeys
//     returns, using the window keys' OWN times as t0..t3;
//   * `time_of_day` splines the four keys' unk98 values, with each substituted
//     by `screen.time_of_day` when it is not >= 0 (cutscene.c:496-498);
//   * `colour` is the linear mix between k[1] and k[2] over their time span
//     (cutscene.c:504-527). `band_filter_active` selects the engine's other
//     branch: with a live band filter AND a splined level of exactly 0, the
//     fraction is `(t - t1) / (span - 0.5)` clamped to [0, 1] instead of
//     `(t - t1) / span`. The editor has no filters, so it passes false, but the
//     branch is implemented so the difference is testable rather than assumed;
//   * `fade_mix`/`screen_factor` come from CutSceneFadeMix;
//   * `shake` is the splined "Camera shake" amplitude (cutscene.c:501). Feed it
//     to CutSceneShakeDelta along with your own six draws to get the jitter;
//     EvaluateCutScene cannot, because the engine's generator state is shared.
//
// `position` is double because the engine assigns the spline into a Vec3d;
// `angles`, `fov`, `shake`, `time_of_day`, `filter_level` and `colour` are float
// because that is the width the engine's own locals have.
//
// Returns false (leaving `out` untouched) for an empty timeline, where the
// engine would read `keys[-1]` in CutScene_GetKey.
bool EvaluateCutScene(const std::vector<CutSceneKey>& keys, float end,
                      const CutSceneScreenParams& screen, float t, bool band_filter_active,
                      CutSceneCameraState* out);

// ---------------------------------------------------------------------------
// Out of scope, and why
// ---------------------------------------------------------------------------
//
// Every one of these is in src/cutscene/cutscene.c and none of them is authored
// level data or a preview rule, so the editor has nothing to place with them:
//
//   * CutScene_RunHandler 0x4F51D0 (cutscene.c:371-413): the run/reset
//     expression gating, the `speedExpr` tick advance, the run/stop scripts.
//     This is the retail mission-flow state machine.
//   * CutScene_RegisterVarsHandler 0x4F4FB0 / CutScene_UnregisterVarsHandler
//     0x4F5050 / CutScene_DeleteHandler 0x4F50E0 / CutScene_LeaveWorldHandler
//     0x4F6520: the script symbol namespace and the filter teardown.
//   * CutScene_InputInfoHandler 0x4F64F0 and CutScene_SetCamera 0x4F65B0 /
//     CutScene_SetFilterParent 0x4F65C0: the world's camera and filter parent,
//     which only exist while a level is running.
//   * CutScene_Interpolate's four screen filters (LineFilter, BandFilter,
//     HandiCamFilter, WideScreenFilter) and their lifetimes -- runtime draw
//     state, not authored data. Only the `screen_factor` they are built from is
//     modelled.
//   * `EditCamera_AttachPosition` 0x508940 / `EditCamera_AttachRotation`
//     0x508620: a key with posAttach/rotAttach >= 0 takes its position or
//     rotation from ANOTHER TASK in the live world (a heli, a train). The flags
//     are parsed and exposed; resolving them needs the running task, so the
//     preview reports the authored pose and the caller decides.
//   * src/game/introscreen.c: the intro/outro .avi screens. IntroScreen_New
//     0x4154B0 selects a hard-coded movie list (eidos/innerloop/vp/intro_eu or
//     intro_us, and outro) and IntroScreen_Run 0x415380 ends with
//     Flow_SetRequest(3). Nothing about it is authored in level data and it is
//     pure flow, so #80's "intro/outro" movie half is not editor work.
//   * MenuManager_RequestScreen 0x4174A0 (src/game/menumanager.c:216-224) is
//     four lines that set `mm->screen`, `nextState` and `state`; the triage
//     entry that pointed #80 at it was pointing at the menu system, which #74
//     already ported (docs/igi-re-parity/MENU_SYSTEM.md). It sequences screens,
//     not cameras.

}  // namespace igi
