#pragma once

namespace igi {

// Short fixed-step first-person recoil cue. The weapon system remains the
// authority for aim recoil and ballistics; this type only animates the visible
// weapon rig. The recovery timing is an inferred presentation fallback until
// the retail first-person weapon animation tracks are fully imported.
class WeaponViewRecoil final {
public:
    static constexpr int RecoveryTicks = 3;

    void Reset();
    void TriggerDegrees(float pitch_degrees, float yaw_degrees);
    void Advance();

    float GetPitchRadians() const { return pitch_radians_; }
    float GetYawRadians() const { return yaw_radians_; }

private:
    static constexpr float DegreesToRadians = 0.017453292519943295f;

    float peak_pitch_radians_ = 0.0f;
    float peak_yaw_radians_ = 0.0f;
    float pitch_radians_ = 0.0f;
    float yaw_radians_ = 0.0f;
    int recovery_ticks_remaining_ = 0;
};

} // namespace igi
