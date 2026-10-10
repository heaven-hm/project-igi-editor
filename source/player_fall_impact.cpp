// player_fall_impact.cpp - Vanilla landing impact calculation.
#include "player_fall_impact.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace igi {

namespace {

// verified-reference: OpenIGI/src/OpenIGI.Game/Player/HumanFallImpact.cs.
constexpr float kWorldUnitsPerMeter = 4096.0f;
constexpr float kSimulationTicksPerSecond = 30.0f;
constexpr float kSafeSpeedMetersPerSecond = 14.0f;
constexpr float kLethalSpeedRangeMetersPerSecond = 13.0f;
constexpr float kMediumSoundDamageFraction = 0.1f;
constexpr float kHeavySoundDamageFraction = 0.2f;
constexpr float kMaximumViewKickUnits = 1024.0f;
constexpr float kSafeImpactHearingRadiusUnits = 20480.0f;
constexpr float kDamagingImpactHearingRadiusUnits = 40960.0f;

} // namespace

PlayerFallImpact CalculateVanillaFallImpact(
    float vertical_velocity_units_per_tick,
    float maximum_health) {
    const float non_negative_maximum_health = std::max(0.0f, maximum_health);
    const float speed_meters_per_second = std::abs(vertical_velocity_units_per_tick) /
        kWorldUnitsPerMeter * kSimulationTicksPerSecond;
    const float damage = std::max(
        0.0f,
        speed_meters_per_second - kSafeSpeedMetersPerSecond) /
        kLethalSpeedRangeMetersPerSecond * non_negative_maximum_health;
    const float normalized_view_kick = std::clamp(
        speed_meters_per_second * 0.125f,
        0.0f,
        1.0f);

    std::string sound_name;
    if (damage > 0.0f && damage < non_negative_maximum_health * kMediumSoundDamageFraction) {
        sound_name = "player_fall_1";
    } else if (damage > 0.0f && damage < non_negative_maximum_health * kHeavySoundDamageFraction) {
        sound_name = "player_fall_2";
    } else if (damage > 0.0f) {
        sound_name = "player_fall_3";
    }

    return PlayerFallImpact{
        speed_meters_per_second,
        damage,
        std::move(sound_name),
        -normalized_view_kick * kMaximumViewKickUnits,
        damage > 0.0f
            ? kDamagingImpactHearingRadiusUnits
            : kSafeImpactHearingRadiusUnits,
    };
}

} // namespace igi
