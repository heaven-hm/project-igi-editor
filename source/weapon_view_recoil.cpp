#include "weapon_view_recoil.h"

namespace igi {

void WeaponViewRecoil::Reset() {
    peak_pitch_radians_ = 0.0f;
    peak_yaw_radians_ = 0.0f;
    pitch_radians_ = 0.0f;
    yaw_radians_ = 0.0f;
    recovery_ticks_remaining_ = 0;
}

void WeaponViewRecoil::TriggerDegrees(
    float pitch_degrees,
    float yaw_degrees) {
    peak_pitch_radians_ = pitch_degrees * DegreesToRadians;
    peak_yaw_radians_ = yaw_degrees * DegreesToRadians;
    pitch_radians_ = peak_pitch_radians_;
    yaw_radians_ = peak_yaw_radians_;
    recovery_ticks_remaining_ = RecoveryTicks;
}

void WeaponViewRecoil::Advance() {
    if (recovery_ticks_remaining_ <= 0) {
        return;
    }

    --recovery_ticks_remaining_;
    const float recovery_fraction = static_cast<float>(
        recovery_ticks_remaining_) /
        static_cast<float>(RecoveryTicks);
    pitch_radians_ = peak_pitch_radians_ * recovery_fraction;
    yaw_radians_ = peak_yaw_radians_ * recovery_fraction;
}

} // namespace igi
