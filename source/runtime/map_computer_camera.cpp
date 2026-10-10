#include "map_computer_camera.h"

#include <algorithm>
#include <cmath>

namespace igi {

namespace {

constexpr float kApproachAboveUnits = 3.2f * 4096.0f;
constexpr float kPeakMotionBlur = 0.55f;

float ClampUnit(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float EaseInOutCubic(float value) {
    const float t = ClampUnit(value);
    return t < 0.5f
        ? 4.0f * t * t * t
        : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f;
}

float EaseInQuad(float value) {
    const float t = ClampUnit(value);
    return t * t;
}

float EaseOutCubic(float value) {
    const float inverse = 1.0f - ClampUnit(value);
    return 1.0f - inverse * inverse * inverse;
}

float EaseInCubic(float value) {
    const float t = ClampUnit(value);
    return t * t * t;
}

float EaseOutQuint(float value) {
    const float inverse = 1.0f - ClampUnit(value);
    return 1.0f - inverse * inverse * inverse * inverse * inverse;
}

float ZoomLerp(float from, float to, float amount) {
    const float safe_from = std::max(from, 0.0001f);
    const float safe_to = std::max(to, 0.0001f);
    return safe_from * std::pow(safe_to / safe_from, ClampUnit(amount));
}

float LerpAngle(float from, float to, float amount) {
    float difference = std::remainder(to - from, 6.2831853071795865f);
    return from + difference * ClampUnit(amount);
}

RuntimeMapComputerPose LerpPose(
    const RuntimeMapComputerPose& from,
    const RuntimeMapComputerPose& to,
    float amount) {
    const float t = ClampUnit(amount);
    RuntimeMapComputerPose result;
    result.position = glm::mix(from.position, to.position, t);
    result.yaw = LerpAngle(from.yaw, to.yaw, t);
    result.pitch = from.pitch + (to.pitch - from.pitch) * t;
    return result;
}

} // namespace

void RuntimeMapComputerCamera::BeginOpen(
    const RuntimeMapComputerPose& eye,
    float eye_field_of_view,
    const RuntimeMapComputerPose& vantage,
    float vantage_field_of_view) {
    from_ = eye;
    to_ = vantage;
    landing_ = eye;
    pose_ = eye;
    from_field_of_view_ = eye_field_of_view;
    to_field_of_view_ = vantage_field_of_view;
    field_of_view_ = eye_field_of_view;
    phase_elapsed_seconds_ = 0.0f;
    seconds_ = 0.0f;
    phase_ = RuntimeMapComputerPhase::Ascend;
}

void RuntimeMapComputerCamera::BeginClose(
    const RuntimeMapComputerPose& vantage,
    float vantage_field_of_view,
    const RuntimeMapComputerPose& eye,
    float eye_field_of_view) {
    if (!CanClose()) {
        return;
    }

    from_ = vantage;
    to_ = eye;
    landing_ = eye;
    pose_ = vantage;
    from_field_of_view_ = vantage_field_of_view;
    to_field_of_view_ = eye_field_of_view;
    field_of_view_ = vantage_field_of_view;
    phase_elapsed_seconds_ = 0.0f;
    phase_ = RuntimeMapComputerPhase::Shutdown;
}

void RuntimeMapComputerCamera::Reset() {
    phase_ = RuntimeMapComputerPhase::Idle;
    phase_elapsed_seconds_ = 0.0f;
    seconds_ = 0.0f;
}

void RuntimeMapComputerCamera::Update(
    float elapsed_seconds,
    const RuntimeMapComputerPose& live_eye,
    const RuntimeMapComputerPose& live_vantage,
    float live_vantage_field_of_view) {
    if (phase_ == RuntimeMapComputerPhase::Idle ||
        phase_ == RuntimeMapComputerPhase::Done) {
        return;
    }

    const float step = std::clamp(elapsed_seconds, 0.0f, 0.1f);
    seconds_ += step;
    phase_elapsed_seconds_ += step;
    landing_ = live_eye;

    if (phase_ == RuntimeMapComputerPhase::Ascend) {
        to_ = live_vantage;
        to_field_of_view_ = live_vantage_field_of_view;
    }

    AdvancePhase();
    switch (phase_) {
        case RuntimeMapComputerPhase::Ascend:
            pose_ = BuildAscendingPose(field_of_view_);
            break;
        case RuntimeMapComputerPhase::Descend:
            pose_ = BuildDescendingPose(field_of_view_);
            break;
        case RuntimeMapComputerPhase::Boot:
        case RuntimeMapComputerPhase::Open:
            pose_ = live_vantage;
            field_of_view_ = live_vantage_field_of_view;
            break;
        case RuntimeMapComputerPhase::Shutdown:
            // Keep the exact close-start pose. This preserves continuity when
            // the player presses the map key during the opening flight.
            break;
        default:
            pose_ = landing_;
            field_of_view_ = to_field_of_view_;
            break;
    }
}

bool RuntimeMapComputerCamera::IsRunning() const {
    return phase_ > RuntimeMapComputerPhase::Idle &&
           phase_ < RuntimeMapComputerPhase::Done;
}

bool RuntimeMapComputerCamera::IsFlying() const {
    return phase_ == RuntimeMapComputerPhase::Ascend ||
           phase_ == RuntimeMapComputerPhase::Descend;
}

bool RuntimeMapComputerCamera::IsInteractive() const {
    return phase_ == RuntimeMapComputerPhase::Open;
}

bool RuntimeMapComputerCamera::CanClose() const {
    return phase_ == RuntimeMapComputerPhase::Ascend ||
           phase_ == RuntimeMapComputerPhase::Boot ||
           phase_ == RuntimeMapComputerPhase::Open;
}

float RuntimeMapComputerCamera::GetDisplayAmount() const {
    switch (phase_) {
        case RuntimeMapComputerPhase::Ascend:
            return EaseInOutCubic(
                ((phase_elapsed_seconds_ / kAscendSeconds) - 0.45f) / 0.55f);
        case RuntimeMapComputerPhase::Boot:
        case RuntimeMapComputerPhase::Open:
        case RuntimeMapComputerPhase::Shutdown:
            return 1.0f;
        case RuntimeMapComputerPhase::Descend:
            return 1.0f - EaseInOutCubic(
                phase_elapsed_seconds_ / (kDescendSeconds * 0.5f));
        default:
            return 0.0f;
    }
}

float RuntimeMapComputerCamera::GetBootEnergy() const {
    switch (phase_) {
        case RuntimeMapComputerPhase::Ascend:
            return 0.45f * EaseInQuad(
                (phase_elapsed_seconds_ / kAscendSeconds - 0.75f) / 0.25f);
        case RuntimeMapComputerPhase::Boot:
            return 1.0f - EaseOutCubic(
                phase_elapsed_seconds_ / (kBootSeconds * 0.8f));
        case RuntimeMapComputerPhase::Shutdown:
            return EaseInCubic(phase_elapsed_seconds_ / kShutdownSeconds) * 0.8f;
        case RuntimeMapComputerPhase::Descend:
            return 0.8f * (1.0f - EaseOutQuint(
                phase_elapsed_seconds_ / (kDescendSeconds * 0.35f)));
        default:
            return 0.0f;
    }
}

float RuntimeMapComputerCamera::GetSweepStrength() const {
    return phase_ == RuntimeMapComputerPhase::Boot
        ? 1.0f - EaseInQuad(phase_elapsed_seconds_ / kBootSeconds)
        : 0.0f;
}

float RuntimeMapComputerCamera::GetSweepPosition() const {
    return EaseOutCubic(phase_elapsed_seconds_ / kBootSeconds);
}

float RuntimeMapComputerCamera::GetMotionBlur() const {
    if (phase_ == RuntimeMapComputerPhase::Ascend) {
        return kPeakMotionBlur * std::sin(
            3.14159265358979323846f *
            ClampUnit(phase_elapsed_seconds_ / kAscendSeconds));
    }
    if (phase_ == RuntimeMapComputerPhase::Descend) {
        return kPeakMotionBlur * std::sin(
            3.14159265358979323846f *
            ClampUnit(phase_elapsed_seconds_ / kDescendSeconds));
    }
    return 0.0f;
}

void RuntimeMapComputerCamera::AdvancePhase() {
    switch (phase_) {
        case RuntimeMapComputerPhase::Ascend:
            if (phase_elapsed_seconds_ >= kAscendSeconds) {
                EnterPhase(RuntimeMapComputerPhase::Boot);
            }
            break;
        case RuntimeMapComputerPhase::Boot:
            if (phase_elapsed_seconds_ >= kBootSeconds) {
                EnterPhase(RuntimeMapComputerPhase::Open);
            }
            break;
        case RuntimeMapComputerPhase::Shutdown:
            if (phase_elapsed_seconds_ >= kShutdownSeconds) {
                from_ = pose_;
                from_field_of_view_ = field_of_view_;
                to_ = landing_;
                EnterPhase(RuntimeMapComputerPhase::Descend);
            }
            break;
        case RuntimeMapComputerPhase::Descend:
            if (phase_elapsed_seconds_ >= kDescendSeconds) {
                EnterPhase(RuntimeMapComputerPhase::Done);
            }
            break;
        default:
            break;
    }
}

void RuntimeMapComputerCamera::EnterPhase(RuntimeMapComputerPhase phase) {
    phase_ = phase;
    phase_elapsed_seconds_ = 0.0f;
}

RuntimeMapComputerPose RuntimeMapComputerCamera::BuildAscendingPose(
    float& field_of_view) const {
    const float progress = ClampUnit(phase_elapsed_seconds_ / kAscendSeconds);
    const float rise = EaseInOutCubic(progress);
    const float settle = EaseInOutCubic(progress / 0.8f);
    field_of_view = ZoomLerp(from_field_of_view_, to_field_of_view_, rise);
    return LerpPose(from_, to_, settle);
}

RuntimeMapComputerPose RuntimeMapComputerCamera::BuildDescendingPose(
    float& field_of_view) const {
    const float progress = ClampUnit(phase_elapsed_seconds_ / kDescendSeconds);
    const float fall = EaseInOutCubic(progress);
    field_of_view = ZoomLerp(from_field_of_view_, to_field_of_view_, fall);

    const glm::vec3 approach(
        landing_.position.x,
        landing_.position.y,
        landing_.position.z + kApproachAboveUnits);
    const float inverse = 1.0f - fall;
    const float first_weight = inverse * inverse;
    const float second_weight = 2.0f * inverse * fall;
    const float third_weight = fall * fall;

    RuntimeMapComputerPose result;
    result.position = from_.position * first_weight +
        approach * second_weight + landing_.position * third_weight;
    const float settle = EaseInOutCubic(progress / 0.8f);
    result.yaw = LerpAngle(from_.yaw, landing_.yaw, settle);
    result.pitch = from_.pitch + (landing_.pitch - from_.pitch) * settle;
    return result;
}

} // namespace igi
