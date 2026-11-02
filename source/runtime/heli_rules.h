// heli_rules.h - Helicopter vehicle task and rotor dynamics modeling.
//
// MIT License.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace igi {

// ---------------------------------------------------------------------------
// Engine constants
// ---------------------------------------------------------------------------

constexpr float kHeliUnit = 4096.0f;
constexpr float kHeliTickRate = 30.0f;
constexpr float kHeliOne = 1.0f;
constexpr float kHeliZero = 0.0f;
constexpr double kHeliOneD = 1.0;
constexpr double kHeliZeroD = 0.0;
constexpr float kHeliTwo = 2.0f;
constexpr float kHeliSevenTenths = 0.7f;
constexpr float kHeliHundredth = 0.01f;
constexpr float kHeliMilli = 0.001f;
constexpr float kHeliDegToRad = 0.017453292f;
constexpr float kHeliQuarter = 0.25f;
constexpr float kHeliTwentieth = 0.05f;
constexpr float kHeliTwoHundredth = 0.005f;
constexpr float kHeliHeliDamp = 0.95f;
constexpr float kHeliDampZ = 0.97f;
constexpr float kHeliDamp98 = 0.98f;
constexpr float kHeliPitchDown = -0.7f;
constexpr float kHeliRollForce = 0.08f;
constexpr float kHeliYawForce = -0.8f;
constexpr float kHeliLift = 11.150222f;
constexpr float kHeliTwoMeters = 8192.0f;
constexpr double kHeliTwoMetersD = 8192.0;
constexpr double kHeliPitchScale = 0.0002908882086657217;
constexpr double kHeliAccelSpeed = 0.8;
constexpr double kHeliMaxThrottle = 1.25;
constexpr double kHeliSpeedStep = 0.01;
constexpr float kHeliAeroK = 0x1.56e264p-12f;

constexpr float kHeliCollideRadius = 0x1.64ce9ep+5f;
constexpr float kHeliCollideElasticity = 1.0f;
constexpr float kHeliCollideFriction = 0x1.d70a3ep-1f;

constexpr int kHeliParamCount = 13;
constexpr int kHeliTokenCount = 19;
constexpr int kHeliTokenPrefix = 3;

constexpr uint32_t kHeliThrustNoneBits = 0xBF800000u;
constexpr float kHeliThrustNone = -1.0f;
constexpr float kHeliPilotAnimNone = -2.0f;
constexpr int kHeliControlCount = 5;
constexpr int kHeliThrottleStepCount = 10;

// ---------------------------------------------------------------------------
// Parameter table
// ---------------------------------------------------------------------------

struct HeliParamDesc {
    const char* authored_name;
    const char* engine_field;
    const char* type_name;
    int token_count;
    bool has_range;
    float range_min;
    float range_max;
};

const HeliParamDesc* HeliParamTable();

struct HeliControlInfo {
    const char* name;
    float min;
    float max;
};

const HeliControlInfo* HeliControlTable();

// ---------------------------------------------------------------------------
// The parsed authored form
// ---------------------------------------------------------------------------

// A "Heli" task's 13 authored parameters.
struct HeliParams {
    double position[3];
    float orientation[3];
    float orientation_matrix[9];
    float original_thrust;
    float speed[3];
    std::string model;
    bool collision_detection;
    bool force_z;
    std::string open_door;
    std::string can_fire;
    std::string play_sound;
    float viewcone_alpha;
    float viewcone_gamma;
    float viewcone_length;
};

bool ParseHeliParams(const std::vector<std::string>& tokens, HeliParams* out);

// ---------------------------------------------------------------------------
// 3x3 rotation helpers
// ---------------------------------------------------------------------------

void HeliMatrixMul(float out[9], const float a[9], const float b[9], int last0, int last1, int last2);
void HeliSetRotX(float m[9], float a);
void HeliSetRotZ(float m[9], float a);
void HeliSetEuler(float m[9], float ax, float ay, float az);
bool HeliFne(long double a, long double b);

// ---------------------------------------------------------------------------
// The flight-preview state
// ---------------------------------------------------------------------------

struct HeliVehicleConfig {
    float start_speed_scale;
    struct {
        float rate;
        float smooth;
    } axis[3];
    float thrust;
    float accel;
    float accel_slow;
    int part_count;
    float parts[8][7];
    float unk10C[3];
    bool valid;
};

struct HeliMoveState {
    float speed;
    float target_speed;
    float steer[3];
    float turn[3];
    float matrix[9];
    int matrix_count;
    float body_speed;
    float mass;
    int touching;
    float stale;
    int moved;
    int force_count;
    float ang_vel[3];
    float rest_speed;
    float rest_spin;
};

struct HeliForce {
    float force[3];
    float point[3];
};

struct HeliMoveResult {
    float speed;
    float turn[3];
    float matrix[9];
    int matrix_count;
    int force_count;
    HeliForce forces[8];
    bool turn_forces_added;
    bool nose_pitch_applied;
    bool stale_used;
    bool resting_gate_open;
    bool sound_updated;
    float sound_params[3];
};

// ---------------------------------------------------------------------------
// Flight control operations
// ---------------------------------------------------------------------------

bool HeliControlThrustRequested(const float axes_in[5]);
void HeliControlReset(HeliMoveState* state, float axes_out[5], const double start_pos[3],
                      const float start_vel[3], const float start_matrix[9], float start_speed,
                      float out_vel[3]);
void HeliControlApplyAxes(HeliMoveState* state, const float axes_in[5]);
const float* HeliThrottleSteps();
float HeliThrottleStep(float target_speed, int delta_steps);

// ---------------------------------------------------------------------------
// Flight simulation operations
// ---------------------------------------------------------------------------

float HeliAccelRate(float speed, const HeliVehicleConfig& config);
float HeliApproachSpeed(float speed, float target, float rate);
long double HeliApproachSpeedWide(float speed, float target, float rate);
void HeliSmoothTurn(const float steer[3], float turn[3], const HeliVehicleConfig& config,
                    float out_turn[3]);
int HeliTurnForces(const float turn[3], const HeliVehicleConfig& config, long double speed,
                   float mass,
                   int touching, float body_speed, HeliForce out_forces[3], float* out_stale);
long double HeliNosePitch(const float matrix[9], float stale, bool* used_stale);
void HeliComposeNoseMatrix(const float matrix[9], int matrix_count,
                           const HeliVehicleConfig& config, long double pitch, float out_matrix[9],
                           int* out_count);
void HeliRotorSoundParams(float speed, float out_sound_params[3]);
bool HeliIsResting(int touching, bool moved, int force_count, float body_speed, float rest_speed,
                   const float ang_vel[3], float rest_spin);
void HeliMoveStep(HeliMoveState* state, const HeliVehicleConfig& config, HeliMoveResult* out);

// ---------------------------------------------------------------------------
// Landing gear operations
// ---------------------------------------------------------------------------

struct HeliWheelParams {
    float len;
    float top;
};

HeliWheelParams ParseHeliWheelParams(const std::vector<std::string>& tokens);

struct HeliWheelProbe {
    float len;
    float top;
    float offset[3];
    double depth;
    double start_z;
    double end_z;
};

HeliWheelProbe HeliBuildWheelProbe(const HeliWheelParams& params, const double wheel_pos[3],
                                   const float offset[3]);
struct HeliWheelLiftResult {
    bool applied;
    bool landed;
    float force[6];
    double max_lift;
};

HeliWheelLiftResult HeliWheelLift(const HeliWheelProbe& probe, bool found, double hit_dist,
                                  float mass);
void HeliWheelDamp(float vel[3], float ang_mom[3]);

// ---------------------------------------------------------------------------
// Rotor dynamics
// ---------------------------------------------------------------------------

constexpr float kRotorLift = 44.600887298583984f;
constexpr float kRotorTilt = 2.5f;
constexpr float kRotorTiltY = 8192.0f;
constexpr float kRotorMinPitch = -0.10000000149011612f;
constexpr float kRotorYawTilt = 0.0263671875f;
constexpr float kRotorMassTilt = 0.4000000059604645f;
constexpr float kRotorSpin = 21.362831115722656f;
constexpr double kRotorDegToRad = 0.017453293005625408;
constexpr int kRotorDefSize = 0x20;
constexpr int kRotorDefArgCount = 6;

struct RotorDef {
    int lift;
    int tilt;
    int dir;
    float angle;
    float speed;
    std::string name;
    float angle_degrees;
};

int RotorDefArgIndex(int joint_count);
bool ParseRotorDef(const std::vector<std::string>& args, int index, RotorDef* out);

// Physics definition header.
struct PhysicsDefHeader {
    std::string task_type;
    int type_id;
    float mass;
    float size[3];
    std::string model;
    int joint_count;
    int first_arg;
};

bool ParsePhysicsDefHeader(const std::vector<std::string>& args, PhysicsDefHeader* out);
std::vector<std::vector<std::string>> ParseDefinePhysicsObjTypeCalls(const std::string& text);
bool ParseHeliVehicleConfig(const std::vector<std::string>& args, int first_arg,
                            HeliVehicleConfig* out);

struct HeliConfig {
    HeliVehicleConfig v;
    int count1;
    int list[13];
    int count2;
};

bool ParseHeliConfig(const std::vector<std::string>& args, int first_arg, HeliConfig* out);

// ---------------------------------------------------------------------------
// Rotor dynamics operations
// ---------------------------------------------------------------------------

float RotorRunValue(float value, const RotorDef& def, float car_speed);
float RotorSpinAngle(float value);
float RotorBladeStep(const RotorDef& def, float car_speed);
float RotorBladeTilt(const RotorDef& def, float car_speed, const float owner_matrix[9],
                     const float owner_vel[3], float start_speed_scale);
bool RotorLiftForce(const RotorDef& def, float mass, float car_speed, const float owner_matrix[9],
                    HeliForce* out_force);
bool RotorTiltForce(const RotorDef& def, float mass, float car_speed, int touching,
                    HeliForce* out_force);
void RotorOffsetPos(const float offset[3], const float owner_matrix[9], const double owner_pos[3],
                    double out_pos[3]);
void RotorLocalMatrix(float out[9], const float owner_matrix[9], const float local[9]);
void RotorBladeMatrix(float out[9], const float base[9], float angle, float tilt);

}  // namespace igi
