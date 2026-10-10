// player_ladder.cpp - Verified-reference ladder placement and traversal state.
#include "player_ladder.h"

#include <algorithm>
#include <cmath>

namespace igi {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegreesToRadians = kPi / 180.0f;
constexpr float kRadiansToDegrees = 180.0f / kPi;
constexpr float kMinimumDistance = 0.0001f;

} // namespace

LadderClimbLine::LadderClimbLine(
    const glm::vec3& bottom,
    const glm::vec3& top)
    : bottom_(bottom),
      top_(top) {
    const float height = std::abs(top.z - bottom.z);
    step_count_ = std::max(
        1,
        static_cast<int>(height / StepLengthUnits));
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
      climb_line_(bottom, top) {
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

bool LadderPlacement::CanActivate(
    const glm::vec3& player_position,
    float player_yaw_degrees,
    bool& at_top) const {
    at_top = false;

    const glm::vec3 from_origin = player_position - origin_;
    const glm::vec3 across_axis = orientation_[0];
    const glm::vec3 normal_axis = orientation_[1];
    const float across = glm::dot(from_origin, across_axis);
    const float normal_distance = glm::dot(from_origin, normal_axis);

    const float yaw_radians = player_yaw_degrees * kDegreesToRadians;
    const glm::vec2 forward_direction(std::sin(yaw_radians), std::cos(yaw_radians));
    const glm::vec2 target_direction = normal_distance > 0.0f
        ? glm::vec2(-normal_axis.x, -normal_axis.y)
        : glm::vec2(normal_axis.x, normal_axis.y);
    const float target_length = glm::length(target_direction);
    const float facing_dot = target_length > kMinimumDistance
        ? glm::dot(forward_direction, target_direction) / target_length
        : -1.0f;

    const bool bottom_candidate =
        std::abs(normal_distance) < ActivateRadiusUnits &&
        across > -ActivateHalfWidthUnits &&
        across < ActivateHalfWidthUnits &&
        facing_dot > FacingCosine &&
        player_position.z > origin_.z &&
        player_position.z < activation_top_.z;
    const glm::vec3 from_top = player_position - activation_top_;
    const glm::vec3 to_top = activation_top_ - player_position;
    const float top_distance = glm::length(to_top);
    const float top_forward = glm::dot(
        forward_direction,
        glm::vec2(to_top.x, to_top.y));
    const bool top_candidate =
        glm::dot(from_top, normal_axis) < 0.0f &&
        top_distance < ActivateRadiusUnits &&
        top_forward > 0.0f;
    if (top_candidate) {
        at_top = true;
        return true;
    }

    return bottom_candidate;
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
