#pragma once

namespace igi {

// Fixed-step first-person rig lowering used by the vanilla weapon state
// machine. The renderer consumes the current angles; the simulation owns the
// clock so render frequency cannot change the transition.
class WeaponViewSway final {
public:
    // verified-reference: OpenIGI HumanViewSway literals recovered from the
    // vanilla HumanView weapon-change path.
    static constexpr float LoweredPitchRadians = -0.61086524f;
    static constexpr float LoweredYawRadians = -0.34906587f;
    static constexpr float PitchStepRadians = 0.058177643f;
    static constexpr float YawStepRadians = 0.033244368f;
    static constexpr int TicksToTravel = 11;

    void Reset();
    void Lower();
    void Raise();
    void SnapTo(float pitch_radians, float yaw_radians);
    void Advance();

    bool IsSettled() const {
        return pitch_radians_ == target_pitch_radians_ &&
            yaw_radians_ == target_yaw_radians_;
    }
    float GetPitchRadians() const { return pitch_radians_; }
    float GetYawRadians() const { return yaw_radians_; }
    float GetTargetPitchRadians() const { return target_pitch_radians_; }
    float GetTargetYawRadians() const { return target_yaw_radians_; }

private:
    static float StepTowards(
        float current_radians,
        float target_radians,
        float step_radians);

    float pitch_radians_ = 0.0f;
    float yaw_radians_ = 0.0f;
    float target_pitch_radians_ = 0.0f;
    float target_yaw_radians_ = 0.0f;
};

} // namespace igi
