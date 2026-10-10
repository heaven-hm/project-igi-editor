// player_ladder.h - Verified-reference ladder placement and traversal state.
#pragma once

#include <glm/glm.hpp>

namespace igi {

enum class LadderTraversalPhase {
    Inactive,
    Climbing,
    GettingOnTop,
    GettingOffTop,
    SlidingDown,
};

enum class LadderStepResult {
    Idle,
    SteppingUp,
    SteppingDown,
    ReachedTop,
    Dismounted,
    Sliding,
};

class LadderClimbLine final {
public:
    static constexpr float StepLengthUnits = 1228.8f;
    static constexpr int TopStepMargin = 6;

    LadderClimbLine(const glm::vec3& bottom, const glm::vec3& top);

    const glm::vec3& GetBottom() const { return bottom_; }
    const glm::vec3& GetTop() const { return top_; }
    int GetStepCount() const { return step_count_; }
    int GetTopStep() const { return top_step_; }
    glm::vec3 PositionAtStep(int step) const;

private:
    glm::vec3 bottom_ = glm::vec3(0.0f);
    glm::vec3 top_ = glm::vec3(0.0f);
    int step_count_ = 1;
    int top_step_ = 0;
};

class LadderPlacement final {
public:
    // verified-reference: OpenIGI LadderPlacement constants recovered from
    // the vanilla ladder activation and mount handlers.
    static constexpr float ActivateRadiusUnits = 8192.0f;
    static constexpr float ActivateHalfWidthUnits = 2867.2f;
    static constexpr float FacingCosine = 0.70710678f;
    static constexpr float BottomStandoffUnits = 1515.52f;
    static constexpr float BottomVerticalOffsetUnits = 3915.776f;
    static constexpr float TopVerticalOffsetUnits = 3481.6f;
    static constexpr float TopStandoffUnits = 1638.4f;

    LadderPlacement(
        const glm::vec3& origin,
        const glm::vec3& activation_top,
        const glm::vec3& bottom,
        const glm::vec3& top,
        const glm::mat3& orientation);

    const glm::vec3& GetOrigin() const { return origin_; }
    const glm::vec3& GetActivationTop() const { return activation_top_; }
    const glm::vec3& GetBottom() const { return climb_line_.GetBottom(); }
    const glm::vec3& GetTop() const { return climb_line_.GetTop(); }
    const glm::vec3& GetBottomMount() const { return bottom_mount_; }
    const glm::vec3& GetTopMount() const { return top_mount_; }
    const LadderClimbLine& GetClimbLine() const { return climb_line_; }
    float GetPlayerYawDegrees() const { return player_yaw_degrees_; }

    bool CanActivate(
        const glm::vec3& player_position,
        float player_yaw_degrees,
        bool& at_top) const;

private:
    glm::vec3 origin_ = glm::vec3(0.0f);
    glm::vec3 activation_top_ = glm::vec3(0.0f);
    glm::mat3 orientation_ = glm::mat3(1.0f);
    LadderClimbLine climb_line_;
    glm::vec3 bottom_mount_ = glm::vec3(0.0f);
    glm::vec3 top_mount_ = glm::vec3(0.0f);
    float player_yaw_degrees_ = 0.0f;
};

class LadderTraversal final {
public:
    void Mount(const LadderPlacement& ladder, bool at_top);

    LadderStepResult Decide(bool up, bool down, bool slide);
    void Move(const glm::vec3& world_delta);
    bool CompleteStep();
    bool CompleteTopTransition();
    void UpdateSlidePosition(const glm::vec3& position);
    bool CompleteSlide();

    LadderTraversalPhase GetPhase() const { return phase_; }
    bool IsOnLadder() const { return phase_ != LadderTraversalPhase::Inactive; }
    bool IsAtBoundary() const {
        return phase_ == LadderTraversalPhase::Climbing && direction_ == 0;
    }
    int GetDirection() const { return direction_; }
    int GetStep() const { return step_; }
    int GetTopStep() const { return top_step_; }
    float GetFacingYawDegrees() const { return facing_yaw_degrees_; }
    const glm::vec3& GetPosition() const { return position_; }

private:
    glm::vec3 position_ = glm::vec3(0.0f);
    float bottom_z_ = 0.0f;
    float top_z_ = 0.0f;
    int top_step_ = 0;
    int step_ = 0;
    int direction_ = 0;
    float facing_yaw_degrees_ = 0.0f;
    LadderTraversalPhase phase_ = LadderTraversalPhase::Inactive;
};

} // namespace igi
