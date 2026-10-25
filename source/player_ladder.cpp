// player_ladder.cpp - Verified-reference ladder placement and traversal state.
#include "player_ladder.h"

#include <algorithm>
#include <cmath>

namespace igi {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kRadiansToDegrees = 180.0f / kPi;

// IGI-RE src/file/vec.c:183-187 IsZero: true for 0 and for NaN, exactly like the
// original's equality tests.
inline bool IsZero(float value) {
    return !(value < 0.0f) && !(value > 0.0f);
}

// IGI-RE src/file/vec.c:189-208 Vec3_Angle (HOOK 0x4B2B30): the angle between two
// vectors in radians, with the clamped branches returning exactly 0 or pi.
float AngleBetweenRadians(const glm::vec3& a, const glm::vec3& b) {
    if (IsZero(a.x) && IsZero(a.y) && IsZero(a.z)) return 0.0f;
    if (IsZero(b.x) && IsZero(b.y) && IsZero(b.z)) return 0.0f;
    const float cosine = glm::dot(a, b) / (glm::length(a) * glm::length(b));
    if (cosine >= 1.0f) return 0.0f;
    if (!(cosine > -1.0f)) return 3.14159265f;
    return std::acos(cosine);
}

// IGI-RE src/game/misc.c:42-56 Vec3d_PitchAngle (HOOK 0x4148F0): the angle of v
// projected into the y/z plane, measured from +y, negative below.
float PitchAngleRadians(const glm::vec3& v) {
    glm::vec3 projected(0.0f, v.y, v.z);
    if (IsZero(projected.x) && IsZero(projected.y) && IsZero(projected.z)) {
        return 0.0f;
    }
    projected /= glm::length(projected);
    float angle = std::acos(std::clamp(projected.y, -1.0f, 1.0f));
    if (!(projected.z >= 0.0f)) angle = -angle;
    return angle;
}

// IGI-RE src/game/misc.c:58-72 Vec3d_YawAngle (HOOK 0x414950): the angle of v
// projected into the x/y plane, measured from +y, negative towards +x.
float YawAngleRadians(const glm::vec3& v) {
    glm::vec3 projected(v.x, v.y, 0.0f);
    if (IsZero(projected.x) && IsZero(projected.y) && IsZero(projected.z)) {
        return 0.0f;
    }
    projected /= glm::length(projected);
    float angle = std::acos(std::clamp(projected.y, -1.0f, 1.0f));
    if (projected.x > 0.0f) angle = -angle;
    return angle;
}

} // namespace

LadderClimbLine::LadderClimbLine(
    const glm::vec3& bottom,
    const glm::vec3& top,
    const glm::vec3& climb_axis)
    : bottom_(bottom),
      top_(top) {
    // IGI-RE src/props/ladder.c:86-91 Ladder_Create (HOOK 0x440280):
    //   Model_GetLocalPoint(&p2, l, 2); Model_GetLocalPoint(&p3, l, 3);
    //   h = |p2.z - p3.z|; l->rungs = (int)round_ll(h * kRungScale);
    // Two things matter here: p2/p3 are MODEL-LOCAL points, so the height is the
    // difference along the model's own Z axis (a ladder lying on its side still
    // counts rungs), and round_ll ROUNDS rather than truncates. round_ll is an x87
    // `fistpll` (src/props/props.h:50-57), i.e. the current rounding mode, which is
    // round-to-nearest-even by default -- std::nearbyint reproduces that. The caller
    // passes world points plus the ladder's local +Z column, so the projection
    // recovers the local difference.
    const float height = std::abs(glm::dot(top - bottom, climb_axis));
    step_count_ = std::max(
        1,
        static_cast<int>(std::nearbyint(height / StepLengthUnits)));
    top_step_ = std::max(0, step_count_ - TopStepMargin);
}

glm::vec3 LadderClimbLine::PositionAtStep(int step) const {
    const int clamped_step = std::clamp(step, 0, step_count_);
    const float fraction = static_cast<float>(clamped_step) /
        static_cast<float>(step_count_);
    return bottom_ + (top_ - bottom_) * fraction;
}

LadderPlacement::LadderPlacement(
    const glm::vec3& origin,
    const glm::vec3& activation_top,
    const glm::vec3& bottom,
    const glm::vec3& top,
    const glm::mat3& orientation)
    : origin_(origin),
      activation_top_(activation_top),
      orientation_(orientation),
      climb_line_(bottom, top, orientation[2]) {
    const glm::vec3 standoff_axis = orientation_[1];
    const glm::vec3 vertical_axis = orientation_[2];
    bottom_mount_ = bottom +
        standoff_axis * BottomStandoffUnits +
        vertical_axis * BottomVerticalOffsetUnits;
    top_mount_ = top +
        vertical_axis * TopVerticalOffsetUnits -
        standoff_axis * TopStandoffUnits;

    // OpenIGI's Matrix3f.M21/M11 read is the first column's second/first row.
    // The player camera uses the mirrored yaw convention, hence the negation.
    const float facing_radians = std::atan2(
        orientation_[0].y,
        orientation_[0].x) + kPi;
    player_yaw_degrees_ = -facing_radians * kRadiansToDegrees;
}

void LadderUseQuery::Reset() {
    best_pitch_ = InitialBestAngleRadians;
    best_yaw_ = InitialBestAngleRadians;
    best_index_ = -1;
    best_at_top_ = false;
}

void LadderUseQuery::Consider(const LadderPlacement& ladder,
                              const glm::vec3& player_eye,
                              const ViewFrame& view,
                              int index) {
    // IGI-RE src/props/ladder.c:130-138. The player's coordinates in the ladder's
    // frame are built from the row-major matrix's columns, i.e. the local axes.
    const glm::vec3 from_origin = player_eye - ladder.GetOrigin();
    const float across = glm::dot(from_origin, ladder.GetAcrossAxis());
    const float normal_distance = glm::dot(from_origin, ladder.GetNormalAxis());
    if (std::abs(normal_distance) < LadderPlacement::ActivateRadiusUnits &&
        across > -LadderPlacement::ActivateHalfWidthUnits &&
        across < LadderPlacement::ActivateHalfWidthUnits) {
        // ladder.c:143-157: the view's own column 1 against the ladder's column 1,
        // flipped to the player's side, then the strict angle gate and the world-Z
        // span taken from magic point 1.
        glm::vec3 target = ladder.GetNormalAxis();
        if (normal_distance > 0.0f) {
            target = -target;
        }
        const float angle = AngleBetweenRadians(view.forward, target);
        if (angle < LadderPlacement::FacingAngleRadians &&
            ladder.GetActivationTop().z > player_eye.z &&
            player_eye.z > ladder.GetOrigin().z) {
            // ladder.c:158-161: the bottom offer overwrites the winner and zeroes
            // the angle bounds, which locks out every later top offer.
            best_index_ = index;
            best_at_top_ = false;
            best_pitch_ = 0.0f;
            best_yaw_ = 0.0f;
        }
    }

    // IGI-RE src/props/ladder.c:164-173: the top offer runs unconditionally, needs
    // the eye on the reference point's -Y side, and then goes through
    // UseQuery_Consider.
    if (glm::dot(player_eye - ladder.GetActivationTop(),
                 ladder.GetNormalAxis()) < 0.0f) {
        ConsiderReferencePoint(ladder.GetActivationTop(), player_eye, view, index);
    }
}

void LadderUseQuery::ConsiderReferencePoint(const glm::vec3& reference,
                                            const glm::vec3& player_eye,
                                            const ViewFrame& view,
                                            int index) {
    // IGI-RE src/game/game.c:903-927 UseQuery_Consider (HOOK 0x416AD0).
    const glm::vec3 delta = reference - player_eye;
    if (glm::length(delta) >= LadderPlacement::ActivateRadiusUnits) {
        return;
    }
    // game.c:908-911: the vector in the view matrix's own frame.
    const glm::vec3 local(glm::dot(delta, view.right),
                          glm::dot(delta, view.forward),
                          glm::dot(delta, view.up));
    if (!(local.y > 0.0f)) {
        return;
    }
    // game.c:914-921: the absolute pitch and yaw deviation from straight ahead.
    const float pitch = std::abs(PitchAngleRadians(local));
    const float yaw = std::abs(YawAngleRadians(local));
    if (pitch >= best_pitch_ || yaw >= best_yaw_) {
        return;
    }
    best_pitch_ = pitch;
    best_yaw_ = yaw;
    best_index_ = index;
    best_at_top_ = true;
}

void LadderTraversal::Mount(const LadderPlacement& ladder, bool at_top) {
    bottom_z_ = ladder.GetBottom().z;
    top_z_ = ladder.GetTop().z;
    top_step_ = ladder.GetClimbLine().GetTopStep();
    facing_yaw_degrees_ = ladder.GetPlayerYawDegrees();
    position_ = at_top ? ladder.GetTopMount() : ladder.GetBottomMount();
    step_ = at_top ? top_step_ : 0;
    direction_ = 0;
    phase_ = at_top
        ? LadderTraversalPhase::GettingOnTop
        : LadderTraversalPhase::Climbing;
}

LadderStepResult LadderTraversal::Decide(bool up, bool down, bool slide) {
    if (phase_ != LadderTraversalPhase::Climbing) {
        return LadderStepResult::Idle;
    }

    // Sliding can interrupt a rung in progress; changing rung direction cannot.
    if (slide) {
        direction_ = 0;
        phase_ = LadderTraversalPhase::SlidingDown;
        return LadderStepResult::Sliding;
    }

    if (direction_ != 0) {
        return LadderStepResult::Idle;
    }

    if (up) {
        if (step_ == top_step_) {
            phase_ = LadderTraversalPhase::GettingOffTop;
            return LadderStepResult::ReachedTop;
        }
        direction_ = 1;
        return LadderStepResult::SteppingUp;
    }

    if (down) {
        if (step_ == 0) {
            phase_ = LadderTraversalPhase::Inactive;
            return LadderStepResult::Dismounted;
        }
        direction_ = -1;
        return LadderStepResult::SteppingDown;
    }

    return LadderStepResult::Idle;
}

void LadderTraversal::Move(const glm::vec3& world_delta) {
    const bool moves_during_climb =
        phase_ == LadderTraversalPhase::Climbing && direction_ != 0;
    const bool moves_during_top_transition =
        phase_ == LadderTraversalPhase::GettingOnTop ||
        phase_ == LadderTraversalPhase::GettingOffTop;
    if (!moves_during_climb && !moves_during_top_transition) {
        return;
    }

    float next_z = position_.z + world_delta.z;
    if (phase_ == LadderTraversalPhase::Climbing) {
        next_z = std::clamp(next_z, bottom_z_, top_z_);
    }
    position_ = glm::vec3(
        position_.x + world_delta.x,
        position_.y + world_delta.y,
        next_z);
}

bool LadderTraversal::CompleteStep() {
    if (phase_ != LadderTraversalPhase::Climbing || direction_ == 0) {
        return false;
    }

    step_ += direction_;
    direction_ = 0;
    return true;
}

bool LadderTraversal::CompleteTopTransition() {
    if (phase_ == LadderTraversalPhase::GettingOnTop) {
        phase_ = LadderTraversalPhase::Climbing;
        direction_ = 0;
        return true;
    }

    if (phase_ == LadderTraversalPhase::GettingOffTop) {
        phase_ = LadderTraversalPhase::Inactive;
        direction_ = 0;
        return true;
    }

    return false;
}

void LadderTraversal::UpdateSlidePosition(const glm::vec3& position) {
    if (phase_ == LadderTraversalPhase::SlidingDown) {
        position_ = position;
    }
}

bool LadderTraversal::CompleteSlide() {
    if (phase_ != LadderTraversalPhase::SlidingDown) {
        return false;
    }

    phase_ = LadderTraversalPhase::Inactive;
    return true;
}

} // namespace igi
