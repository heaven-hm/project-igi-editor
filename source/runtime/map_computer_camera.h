#pragma once

#include <cstdint>

#include <glm/glm.hpp>

namespace igi {

enum class RuntimeMapComputerPhase : uint8_t {
    Idle,
    Ascend,
    Boot,
    Open,
    Shutdown,
    Descend,
    Done,
};

struct RuntimeMapComputerPose {
    glm::vec3 position = glm::vec3(0.0f);
    float yaw = 0.0f;
    float pitch = 0.0f;
};

// Renderer-free map-computer transition state. The presentation layer consumes
// this value object; the runtime owns the timing and never depends on GL state.
class RuntimeMapComputerCamera final {
public:
    static constexpr float kAscendSeconds = 1.05f;
    static constexpr float kBootSeconds = 0.55f;
    static constexpr float kShutdownSeconds = 0.22f;
    static constexpr float kDescendSeconds = 0.95f;
    static constexpr float kMapPitchRadians = -1.5707963267948966f;

    void BeginOpen(
        const RuntimeMapComputerPose& eye,
        float eye_field_of_view,
        const RuntimeMapComputerPose& vantage,
        float vantage_field_of_view);
    void BeginClose(
        const RuntimeMapComputerPose& vantage,
        float vantage_field_of_view,
        const RuntimeMapComputerPose& eye,
        float eye_field_of_view);
    void Reset();
    void Update(
        float elapsed_seconds,
        const RuntimeMapComputerPose& live_eye,
        const RuntimeMapComputerPose& live_vantage,
        float live_vantage_field_of_view);

    RuntimeMapComputerPhase GetPhase() const { return phase_; }
    bool IsRunning() const;
    bool IsFlying() const;
    bool IsInteractive() const;
    bool CanClose() const;
    const RuntimeMapComputerPose& GetPose() const { return pose_; }
    float GetFieldOfView() const { return field_of_view_; }
    float GetDisplayAmount() const;
    float GetBootEnergy() const;
    float GetSweepStrength() const;
    float GetSweepPosition() const;
    float GetMotionBlur() const;
    float GetSeconds() const { return seconds_; }

private:
    void AdvancePhase();
    void EnterPhase(RuntimeMapComputerPhase phase);
    RuntimeMapComputerPose BuildAscendingPose(float& field_of_view) const;
    RuntimeMapComputerPose BuildDescendingPose(float& field_of_view) const;

    RuntimeMapComputerPhase phase_ = RuntimeMapComputerPhase::Idle;
    RuntimeMapComputerPose from_;
    RuntimeMapComputerPose to_;
    RuntimeMapComputerPose landing_;
    RuntimeMapComputerPose pose_;
    float from_field_of_view_ = 1.0f;
    float to_field_of_view_ = 1.0f;
    float field_of_view_ = 1.0f;
    float phase_elapsed_seconds_ = 0.0f;
    float seconds_ = 0.0f;
};

} // namespace igi
