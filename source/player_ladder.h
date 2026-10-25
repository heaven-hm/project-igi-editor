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

    // `climb_axis` is the ladder's model-local +Z column: IGI-RE measures the
    // rung height along the MODEL's Z axis, not along world Z (see the .cpp).
    LadderClimbLine(const glm::vec3& bottom, const glm::vec3& top,
                    const glm::vec3& climb_axis);

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

// The player's view frame: the columns of the view matrix IGI-RE hands the use
// query as `q.matrix` (src/humanbase/human.c:1284 HumanView_GetMatrix). Column 1
// is the look direction -- UseQuery_Consider requires a positive component along
// it (src/game/game.c:909,912) -- which is also the convention this codebase's
// cameras use (source/runtime/runtime_world.cpp:1858).
struct ViewFrame {
    glm::vec3 right = glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 forward = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 up = glm::vec3(0.0f, 0.0f, 1.0f);
};

class LadderPlacement final {
public:
    // verified-reference: OpenIGI LadderPlacement constants recovered from
    // the vanilla ladder activation and mount handlers.
    static constexpr float ActivateRadiusUnits = 8192.0f;
    static constexpr float ActivateHalfWidthUnits = 2867.2f;
    // IGI-RE src/props/ladder.c:55 declares kLadderAngle and
    // src/plane/gear.c:156-157 resolves it to kQuarterPi = 0.7853982f.
    // ladder.c:157 gates on the ANGLE between the view's column 1 and the
    // ladder's column 1, so the previous cosine comparison is replaced by it.
    static constexpr float FacingAngleRadians = 0.7853982f;
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

    // The object matrix's columns, i.e. the ladder's local axes: IGI-RE reads
    // them as m[0..2] (across), m[1],m[4],m[7] (the face normal) and
    // m[2],m[5],m[8] (up) in src/props/ladder.c:133-148.
    const glm::vec3& GetAcrossAxis() const { return orientation_[0]; }
    const glm::vec3& GetNormalAxis() const { return orientation_[1]; }
    const glm::vec3& GetClimbAxis() const { return orientation_[2]; }

private:
    glm::vec3 origin_ = glm::vec3(0.0f);
    glm::vec3 activation_top_ = glm::vec3(0.0f);
    glm::mat3 orientation_ = glm::mat3(1.0f);
    LadderClimbLine climb_line_;
    glm::vec3 bottom_mount_ = glm::vec3(0.0f);
    glm::vec3 top_mount_ = glm::vec3(0.0f);
    float player_yaw_degrees_ = 0.0f;
};

// IGI-RE src/game/game.c:897-928 UseQuery_Consider (HOOK 0x416AD0) plus the query
// state Ladder_Use shares across every ladder of the engine's task scan
// (src/humanbase/human.c:1291-1298). bestPitch/bestYaw start at kQuarterPi for a
// player (src/humanbase/human.c:1286), so the first top candidate must already be
// within 45 degrees of straight ahead on both axes; a bottom offer zeroes them
// (src/props/ladder.c:158-160), which locks out every later top offer.
class LadderUseQuery final {
public:
    static constexpr float InitialBestAngleRadians = 0.7853982f;

    void Reset();

    // Applies one ladder's Ladder_Use (IGI-RE src/props/ladder.c:122-174) to the
    // shared query. `index` identifies the ladder in the caller's own list.
    void Consider(const LadderPlacement& ladder, const glm::vec3& player_eye,
                  const ViewFrame& view, int index);

    bool HasBest() const { return best_index_ >= 0; }
    int GetBestIndex() const { return best_index_; }
    bool IsBestAtTop() const { return best_at_top_; }
    float GetBestPitch() const { return best_pitch_; }
    float GetBestYaw() const { return best_yaw_; }

private:
    // IGI-RE src/game/game.c:903-927, the body of UseQuery_Consider.
    void ConsiderReferencePoint(const glm::vec3& reference,
                                const glm::vec3& player_eye,
                                const ViewFrame& view, int index);

    float best_pitch_ = InitialBestAngleRadians;
    float best_yaw_ = InitialBestAngleRadians;
    int best_index_ = -1;
    bool best_at_top_ = false;
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
