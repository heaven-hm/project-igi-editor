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

// A landing at exactly the safe speed must do no damage.  It did: on i686 the
// intermediate speed is evaluated in x87 80-bit precision, so the unit round-trip
// (velocity / 4096 * 30) lands a hair above 14.0f for the exact threshold --
// 14.00000006 -- and a landing that should have been harmless produced 4.6e-07
// damage, a spurious "player_fall_1" sound, and the doubled hearing radius.
// In IEEE float32 the same expression is exactly 14.0f, so this was invisible in
// the host-native lane and only appeared in the 32-bit build.
//
// The engine has this hazard too and IGI-RE answers it with
// `#pragma GCC optimize("excess-precision=standard")` (src/dirlight/lightmapbuild.c:4-5).
// That pragma governs a whole translation unit; here only one comparison matters, so
// the excess is clamped against the threshold instead.  The tolerance is the precision
// the reference boundaries are themselves specified to (1e-4 m/s -- about 100 float
// ULP at 14.0f, and 0.1 mm/s of fall speed, far below anything the game can express).
constexpr float kThresholdToleranceMetersPerSecond = 1e-4f;

} // namespace

PlayerFallImpact CalculateVanillaFallImpact(
    float vertical_velocity_units_per_tick,
    float maximum_health) {
    const float non_negative_maximum_health = std::max(0.0f, maximum_health);
    const float speed_meters_per_second = std::abs(vertical_velocity_units_per_tick) /
        kWorldUnitsPerMeter * kSimulationTicksPerSecond;
    // Only speeds meaningfully above the safe threshold are damaging, so a landing
    // exactly at it yields 0 damage -- and, through that, no sound and the safe
    // hearing radius, since both are derived from `damage > 0.0f` below.
    const float excess_speed = speed_meters_per_second - kSafeSpeedMetersPerSecond;
    const float damage = excess_speed > kThresholdToleranceMetersPerSecond
        ? excess_speed / kLethalSpeedRangeMetersPerSecond * non_negative_maximum_health
        : 0.0f;
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
