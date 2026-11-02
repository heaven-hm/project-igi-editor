// player_separation.h - Human-versus-human move rejection (vanilla A11).
// Reference: OpenIGI Player/HumanSeparation.cs from retail 0x462BA0. Not a
// push-out: a move that would end nearer to another human than it started is
// rejected outright, so bodies block each other without being displaced.
#pragma once

#include <glm/glm.hpp>
#include <vector>

namespace igi {

struct HumanSeparation {
    // Half-height of the blocking cylinder, engine units (~1.8 m tall).
    static constexpr double kHeightInUnits = 7372.8;
    // Radius of the blocking cylinder, engine units (~0.55 m).
    static constexpr double kRadiusInUnits = 2252.8;

    // Returns `end` when the move is allowed, `start` when `other` blocks it.
    // Only the horizontal distance decides "closer"; vertical gap at or above
    // the cylinder height never blocks. A body already overlapping may still
    // walk out: the move is refused only when it closes the gap.
    static glm::vec3 Resolve(
        const glm::vec3& start,
        const glm::vec3& end,
        const glm::vec3& other);

    // Resolves against every other human in turn; each blocker overwrites the
    // candidate, so later humans are tested against an already-refused spot.
    template <typename Iterator>
    static glm::vec3 ResolveAll(
        const glm::vec3& start,
        const glm::vec3& end,
        Iterator first,
        Iterator last) {
        glm::vec3 result = end;
        for (Iterator it = first; it != last; ++it) {
            result = Resolve(start, result, *it);
        }
        return result;
    }
};

} // namespace igi
