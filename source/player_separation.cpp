// player_separation.cpp - Human-versus-human move rejection implementation.
#include "player_separation.h"

#include <cmath>

namespace igi {

namespace {

double HorizontalDistance(const glm::vec3& from, const glm::vec3& to) {
    const double dx = static_cast<double>(from.x) - to.x;
    const double dy = static_cast<double>(from.y) - to.y;
    return std::sqrt((dx * dx) + (dy * dy));
}

} // namespace

glm::vec3 HumanSeparation::Resolve(
    const glm::vec3& start,
    const glm::vec3& end,
    const glm::vec3& other) {
    const double vertical_gap =
        std::abs(static_cast<double>(end.z) - other.z);
    if (vertical_gap >= kHeightInUnits) {
        return end;
    }

    const double end_distance = HorizontalDistance(end, other);
    if (end_distance >= kRadiusInUnits) {
        return end;
    }

    // Inside the cylinder. The move is only refused when it closes the gap,
    // so a human already overlapping can still walk out.
    return HorizontalDistance(start, other) > end_distance ? start : end;
}

} // namespace igi
