#include "door_state.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace igi {

namespace {

constexpr float kFixedTickRate = 30.0f;
constexpr float kAngleEpsilon = 0.000001f;
constexpr float kSlideEpsilon = 0.000001f;

bool NearlyEqual(float left, float right, float epsilon) {
    return std::abs(left - right) <= epsilon;
}

} // namespace

RuntimeDoorState::RuntimeDoorState(RuntimeDoorDefinition definition)
    : definition_(std::move(definition)) {
    definition_.open_time_seconds = std::max(0.0f, definition_.open_time_seconds);
}

void RuntimeDoorState::Toggle() {
    open_latch_ = !open_latch_;
}

void RuntimeDoorState::CommandOpen() {
    open_latch_ = true;
}

void RuntimeDoorState::CommandClosed() {
    open_latch_ = false;
}

float RuntimeDoorState::GetAngleRadians() const {
    return glm::radians(angle_degrees_);
}

glm::vec3 RuntimeDoorState::GetSlideOffsetUnits() const {
    return definition_.slide_offset_units * slide_fraction_;
}

RuntimeDoorUseState RuntimeDoorState::GetUseState() const {
    if (is_fully_open_) {
        return RuntimeDoorUseState::Open;
    }
    if (is_fully_closed_) {
        return RuntimeDoorUseState::Closed;
    }
    return open_latch_
        ? RuntimeDoorUseState::Opening
        : RuntimeDoorUseState::Closing;
}

void RuntimeDoorState::Tick() {
    was_fully_open_ = is_fully_open_;
    was_fully_closed_ = is_fully_closed_;

    const float authored_tick_count =
        definition_.open_time_seconds * kFixedTickRate;
    const float angle_target = open_latch_
        ? definition_.maximum_angle_degrees
        : 0.0f;
    const float slide_target = open_latch_ &&
            glm::dot(definition_.slide_offset_units, definition_.slide_offset_units) >
                kSlideEpsilon * kSlideEpsilon
        ? 1.0f
        : 0.0f;

    if (!NearlyEqual(angle_degrees_, angle_target, kAngleEpsilon) &&
        authored_tick_count > 0.0f &&
        !NearlyEqual(definition_.maximum_angle_degrees, 0.0f, kAngleEpsilon)) {
        const float step = std::abs(definition_.maximum_angle_degrees) /
            authored_tick_count;
        if (angle_degrees_ < angle_target) {
            angle_degrees_ = std::min(angle_degrees_ + step, angle_target);
        } else {
            angle_degrees_ = std::max(angle_degrees_ - step, angle_target);
        }
        // A step can land within epsilon of the target; store the exact target
        // immediately so reported motion and stored state agree on this tick.
        if (NearlyEqual(angle_degrees_, angle_target, kAngleEpsilon)) {
            angle_degrees_ = angle_target;
        }
    } else {
        angle_degrees_ = angle_target;
    }

    if (!NearlyEqual(slide_fraction_, slide_target, kSlideEpsilon) &&
        authored_tick_count > 0.0f &&
        glm::dot(definition_.slide_offset_units, definition_.slide_offset_units) >
            kSlideEpsilon * kSlideEpsilon) {
        const float step = 1.0f / authored_tick_count;
        if (slide_fraction_ < slide_target) {
            slide_fraction_ = std::min(slide_fraction_ + step, slide_target);
        } else {
            slide_fraction_ = std::max(slide_fraction_ - step, slide_target);
        }
        // Same-tick snap so a fully-closed/open report never carries residue.
        if (NearlyEqual(slide_fraction_, slide_target, kSlideEpsilon)) {
            slide_fraction_ = slide_target;
        }
    } else {
        slide_fraction_ = slide_target;
    }

    const bool at_targets =
        NearlyEqual(angle_degrees_, angle_target, kAngleEpsilon) &&
        NearlyEqual(slide_fraction_, slide_target, kSlideEpsilon);
    if (at_targets) {
        const bool closed = NearlyEqual(angle_target, 0.0f, kAngleEpsilon) &&
            NearlyEqual(slide_target, 0.0f, kSlideEpsilon);
        is_fully_closed_ = closed;
        is_fully_open_ = !closed;
    } else {
        is_fully_closed_ = false;
        is_fully_open_ = false;
    }

    ticks_open_ = is_fully_open_ ? ticks_open_ + 1 : 0;
}

} // namespace igi
