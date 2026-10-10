// player_fall_impact.h - Vanilla landing impact calculation.
#pragma once

#include <string>

namespace igi {

struct PlayerFallImpact {
    float speed_meters_per_second = 0.0f;
    float damage = 0.0f;
    std::string sound_name;
    float view_kick_units = 0.0f;
    float hearing_radius_units = 0.0f;
};

// Calculates the complete set of effects produced by a vanilla landing.
// The returned sound and hearing radius are presentation/gameplay events;
// callers decide how and when to dispatch them.
PlayerFallImpact CalculateVanillaFallImpact(
    float vertical_velocity_units_per_tick,
    float maximum_health);

} // namespace igi
