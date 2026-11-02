#include "weapon_view_sway.h"

#include <algorithm>

namespace igi {

void WeaponViewSway::Reset() {
    SnapTo(0.0f, 0.0f);
}

void WeaponViewSway::Lower() {
    target_pitch_radians_ = LoweredPitchRadians;
    target_yaw_radians_ = LoweredYawRadians;
}

void WeaponViewSway::Raise() {
    target_pitch_radians_ = 0.0f;
    target_yaw_radians_ = 0.0f;
}

void WeaponViewSway::SnapTo(float pitch_radians, float yaw_radians) {
    pitch_radians_ = pitch_radians;
    yaw_radians_ = yaw_radians;
    target_pitch_radians_ = pitch_radians;
    target_yaw_radians_ = yaw_radians;
}

void WeaponViewSway::Advance() {
    if (IsSettled()) {
        return;
    }

    pitch_radians_ = StepTowards(
        pitch_radians_,
        target_pitch_radians_,
        PitchStepRadians);
    yaw_radians_ = StepTowards(
        yaw_radians_,
        target_yaw_radians_,
        YawStepRadians);
}

float WeaponViewSway::StepTowards(
    float current_radians,
    float target_radians,
    float step_radians) {
    if (current_radians == target_radians) {
        return current_radians;
    }

    if (current_radians < target_radians) {
        return std::min(current_radians + step_radians, target_radians);
    }
    return std::max(current_radians - step_radians, target_radians);
}

} // namespace igi
