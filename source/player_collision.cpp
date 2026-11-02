// player_collision.cpp - Runtime ground, ceiling, and wall collision queries.
#include "player_collision.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace igi {

namespace {

constexpr float kMinimumDistance = 0.0001f;
constexpr float kNormalProbeDistance = 16.0f;
constexpr int kRayMarchSampleCount = 256;
constexpr int kRaycastRefinementIterations = 20;

float LengthSquared(const glm::vec3& vector) {
    return glm::dot(vector, vector);
}

glm::vec3 NormalizeOrFallback(const glm::vec3& vector, const glm::vec3& fallback) {
    const float length_squared = LengthSquared(vector);
    if (length_squared <= kMinimumDistance * kMinimumDistance) {
        return fallback;
    }
    return vector / std::sqrt(length_squared);
}

bool IsWallNormal(const glm::vec3& normal) {
    return std::abs(normal.z) < PlayerCollision::WallNormalZLimit;
}

} // namespace

void PlayerCollision::SetSolidQuery(SolidGeometryQuery solid_geometry_query) {
    solid_geometry_query_ = std::move(solid_geometry_query);
}

void PlayerCollision::SetCeilingQuery(CeilingHeightQuery ceiling_height_query) {
    ceiling_height_query_ = std::move(ceiling_height_query);
}

glm::vec3 PlayerCollision::AccumulateSlopeSlide(
    const glm::vec3& current_slide_velocity,
    const glm::vec3& ground_normal,
    bool grounded) {
    if (!grounded) {
        return glm::vec3(0.0f);
    }

    const float normal_length = std::sqrt(LengthSquared(ground_normal));
    if (normal_length <= kMinimumDistance) {
        return glm::vec3(0.0f);
    }

    const glm::vec3 unit_normal = ground_normal / normal_length;
    const float clamped_normal_z = std::clamp(unit_normal.z, -1.0f, 1.0f);
    const float slope_degrees = 90.0f - glm::degrees(std::asin(clamped_normal_z));

    glm::vec3 accumulated_slide_velocity(0.0f);
    if (slope_degrees > VerySteepSlopeDegrees) {
        accumulated_slide_velocity = unit_normal * VerySteepSlopeSpeedInUnitsPerTick;
    } else if (slope_degrees > SteepSlopeDegrees) {
        accumulated_slide_velocity = unit_normal * SteepSlopeSpeedInUnitsPerTick;
    } else if (slope_degrees > ModerateSlopeDegrees) {
        accumulated_slide_velocity = current_slide_velocity;
        accumulated_slide_velocity +=
            unit_normal * ModerateSlopeSpeedInUnitsPerTick;
    } else if (slope_degrees > ShallowSlopeDegrees) {
        accumulated_slide_velocity = current_slide_velocity;
        accumulated_slide_velocity +=
            unit_normal * ShallowSlopeSpeedInUnitsPerTick;
    }

    accumulated_slide_velocity.z = 0.0f;
    return accumulated_slide_velocity * SlopeSlideDamping;
}

PlayerGroundQuery PlayerCollision::QueryGround(
    const glm::vec3& body_position,
    float current_eye_height,
    TerrainHeightQuery terrain_height_query,
    bool was_grounded,
    bool crouching,
    bool sliding) const {
    (void)current_eye_height;
    (void)crouching;

    PlayerGroundQuery ground_query;
    if (terrain_height_query != nullptr) {
        ground_query.ground_height = terrain_height_query(body_position.x, body_position.y);
    }

    constexpr float terrain_sample_distance = 2048.0f;
    if (terrain_height_query != nullptr) {
        const float positive_x_height = terrain_height_query(
            body_position.x + terrain_sample_distance, body_position.y);
        const float negative_x_height = terrain_height_query(
            body_position.x - terrain_sample_distance, body_position.y);
        const float positive_y_height = terrain_height_query(
            body_position.x, body_position.y + terrain_sample_distance);
        const float negative_y_height = terrain_height_query(
            body_position.x, body_position.y - terrain_sample_distance);

        const glm::vec3 tangent_along_x(
            terrain_sample_distance * 2.0f,
            0.0f,
            positive_x_height - negative_x_height);
        const glm::vec3 tangent_along_y(
            0.0f,
            terrain_sample_distance * 2.0f,
            positive_y_height - negative_y_height);
        ground_query.surface_normal = NormalizeOrFallback(
            glm::cross(tangent_along_x, tangent_along_y),
            glm::vec3(0.0f, 0.0f, 1.0f));
        if (ground_query.surface_normal.z < 0.0f) {
            ground_query.surface_normal = -ground_query.surface_normal;
        }
    }

    if (sliding) {
        ground_query.step_down_budget = SlidingStepDownInUnits;
    } else if (was_grounded) {
        ground_query.step_down_budget = GroundedStepDownInUnits;
    } else {
        ground_query.step_down_budget = AirborneStepDownInUnits;
    }

    const float distance_to_ground = body_position.z - ground_query.ground_height;
    ground_query.is_grounded = distance_to_ground <= ground_query.step_down_budget;

    if (ceiling_height_query_) {
        ground_query.ceiling_height = ceiling_height_query_(body_position);
        const float distance_to_ceiling = ground_query.ceiling_height - body_position.z;
        ground_query.is_under_roof = distance_to_ceiling < RoofClearanceInUnits;
    }

    return ground_query;
}

PlayerWallSweepResult PlayerCollision::SweepWalls(
    const glm::vec3& current_position,
    const glm::vec3& target_position,
    float body_radius,
    float body_height,
    bool grounded,
    bool crouching) const {
    (void)body_radius;
    (void)body_height;

    PlayerWallSweepResult sweep_result;
    glm::vec3 resolved_position = current_position;
    glm::vec3 requested_position = target_position;
    const glm::vec3 initial_motion = target_position - current_position;
    const float initial_motion_length = std::sqrt(LengthSquared(initial_motion));

    if (!solid_geometry_query_ || initial_motion_length <= kMinimumDistance) {
        sweep_result.slide_velocity = initial_motion;
        return sweep_result;
    }

    const float middle_probe_height = crouching
        ? MiddleProbeCrouchingHeightInUnits
        : MiddleProbeStandingHeightInUnits;
    const float high_probe_height = crouching
        ? HighProbeCrouchingHeightInUnits
        : HighProbeStandingHeightInUnits;
    const float probe_heights[] = {
        LowProbeHeightInUnits,
        middle_probe_height,
        high_probe_height,
    };
    const int first_probe_index = grounded ? 1 : 0;
    const int probe_count = grounded ? 2 : 3;

    bool abandoned_after_maximum_iterations = false;
    for (int iteration = 0; iteration < MaximumSweepIterations; ++iteration) {
        const glm::vec3 remaining_motion = requested_position - resolved_position;
        const float remaining_motion_length = std::sqrt(LengthSquared(remaining_motion));
        if (remaining_motion_length <= kMinimumDistance) {
            break;
        }

        const glm::vec3 movement_direction = remaining_motion / remaining_motion_length;
        const glm::vec3 extended_motion = movement_direction *
            (remaining_motion_length + SkinWidthInUnits);

        bool found_wall = false;
        float nearest_wall_distance = std::numeric_limits<float>::max();
        RaycastHit nearest_wall_hit;
        RaycastHit low_probe_hit;
        RaycastHit high_probe_hit;
        bool low_probe_hit_found = false;
        bool high_probe_hit_found = false;

        for (int probe_index = first_probe_index;
             probe_index < first_probe_index + probe_count;
             ++probe_index) {
            const glm::vec3 probe_origin = resolved_position + glm::vec3(
                0.0f, 0.0f, probe_heights[probe_index]);
            RaycastHit probe_hit;
            if (!RaycastSolidGeometry(probe_origin, probe_origin + extended_motion, probe_hit)) {
                continue;
            }

            if (probe_index == 0) {
                low_probe_hit = probe_hit;
                low_probe_hit_found = true;
            }
            if (probe_index == 2) {
                high_probe_hit = probe_hit;
                high_probe_hit_found = true;
            }

            if (!IsWallNormal(probe_hit.normal) || probe_hit.distance >= nearest_wall_distance) {
                continue;
            }

            nearest_wall_distance = probe_hit.distance;
            nearest_wall_hit = probe_hit;
            found_wall = true;
        }

        // Airborne landing and head-bump contacts are handled outside the
        // horizontal wall slide, matching the reference pass ordering.
        if (!grounded && !found_wall && low_probe_hit_found && low_probe_hit.normal.z > 0.99989998f) {
            resolved_position.z = low_probe_hit.position.z - probe_heights[0];
        }
        if (!grounded && !found_wall && high_probe_hit_found && high_probe_hit.normal.z >= -0.99989998f) {
            resolved_position.x = high_probe_hit.position.x;
            resolved_position.y = high_probe_hit.position.y;
        }

        if (!found_wall) {
            break;
        }

        sweep_result.hit_wall = true;
        sweep_result.wall_normal = nearest_wall_hit.normal;
        sweep_result.hit_fraction = std::min(
            sweep_result.hit_fraction,
            nearest_wall_hit.distance / initial_motion_length);

        const glm::vec3 wall_offset = requested_position - nearest_wall_hit.position;
        const float normal_projection = glm::dot(wall_offset, nearest_wall_hit.normal);
        requested_position = nearest_wall_hit.position + wall_offset -
            normal_projection * nearest_wall_hit.normal +
            SkinWidthInUnits * nearest_wall_hit.normal;

        if (iteration == MaximumSweepIterations - 1) {
            abandoned_after_maximum_iterations = true;
        }
    }

    resolved_position = abandoned_after_maximum_iterations
        ? current_position
        : requested_position;
    sweep_result.slide_velocity = resolved_position - current_position;
    return sweep_result;
}

bool PlayerCollision::CanStandUp(
    const glm::vec3& body_position,
    float standing_height,
    TerrainHeightQuery terrain_height_query) const {
    (void)terrain_height_query;
    if (standing_height <= 0.0f) {
        return false;
    }
    if (!ceiling_height_query_) {
        return true;
    }

    const float ceiling_height = ceiling_height_query_(body_position);
    return body_position.z + standing_height <= ceiling_height;
}

void PlayerCollision::ResolveObstacles(
    glm::vec3& body_position,
    const std::vector<ObstacleCollider>& obstacles,
    float player_radius) const {
    for (const ObstacleCollider& obstacle : obstacles) {
        const float vertical_distance = std::abs(body_position.z - obstacle.center.z);
        if (vertical_distance > obstacle.height) {
            continue;
        }

        const glm::vec2 horizontal_offset(
            body_position.x - obstacle.center.x,
            body_position.y - obstacle.center.y);
        const float horizontal_distance_squared = glm::dot(horizontal_offset, horizontal_offset);
        const float minimum_distance = player_radius + obstacle.radius;
        if (horizontal_distance_squared >= minimum_distance * minimum_distance) {
            continue;
        }

        const float horizontal_distance = std::sqrt(horizontal_distance_squared);
        const glm::vec2 separation_direction = horizontal_distance > kMinimumDistance
            ? horizontal_offset / horizontal_distance
            : glm::vec2(1.0f, 0.0f);
        const float overlap_distance = minimum_distance - horizontal_distance;
        body_position.x += separation_direction.x * overlap_distance;
        body_position.y += separation_direction.y * overlap_distance;
    }
}

bool PlayerCollision::RaycastSolidGeometry(
    const glm::vec3& ray_origin,
    const glm::vec3& ray_end,
    RaycastHit& hit) const {
    if (!solid_geometry_query_) {
        return false;
    }

    const glm::vec3 ray_delta = ray_end - ray_origin;
    const float ray_length = std::sqrt(LengthSquared(ray_delta));
    if (ray_length <= kMinimumDistance) {
        return false;
    }

    const int sample_count = std::clamp(
        static_cast<int>(std::ceil(ray_length / 512.0f)),
        1,
        kRayMarchSampleCount);
    bool previous_solid = solid_geometry_query_(ray_origin);
    float previous_fraction = 0.0f;

    for (int sample_index = 1; sample_index <= sample_count; ++sample_index) {
        const float current_fraction = static_cast<float>(sample_index) /
            static_cast<float>(sample_count);
        const glm::vec3 sample_position = ray_origin + ray_delta * current_fraction;
        const bool current_solid = solid_geometry_query_(sample_position);
        if (!current_solid || previous_solid) {
            previous_fraction = current_fraction;
            previous_solid = current_solid;
            continue;
        }

        float lower_fraction = previous_fraction;
        float upper_fraction = current_fraction;
        for (int refinement_index = 0;
             refinement_index < kRaycastRefinementIterations;
             ++refinement_index) {
            const float midpoint_fraction = (lower_fraction + upper_fraction) * 0.5f;
            const glm::vec3 midpoint_position = ray_origin + ray_delta * midpoint_fraction;
            if (solid_geometry_query_(midpoint_position)) {
                upper_fraction = midpoint_fraction;
            } else {
                lower_fraction = midpoint_fraction;
            }
        }

        hit.position = ray_origin + ray_delta * upper_fraction;
        hit.distance = ray_length * upper_fraction;
        hit.normal = EstimateSurfaceNormal(hit.position, ray_delta);
        return true;
    }

    return false;
}

glm::vec3 PlayerCollision::EstimateSurfaceNormal(
    const glm::vec3& surface_position,
    const glm::vec3& movement_direction) const {
    if (!solid_geometry_query_) {
        return glm::vec3(0.0f, 0.0f, 1.0f);
    }

    const glm::vec3 x_axis(kNormalProbeDistance, 0.0f, 0.0f);
    const glm::vec3 y_axis(0.0f, kNormalProbeDistance, 0.0f);
    const glm::vec3 z_axis(0.0f, 0.0f, kNormalProbeDistance);
    const float x_gradient = static_cast<float>(solid_geometry_query_(surface_position + x_axis)) -
        static_cast<float>(solid_geometry_query_(surface_position - x_axis));
    const float y_gradient = static_cast<float>(solid_geometry_query_(surface_position + y_axis)) -
        static_cast<float>(solid_geometry_query_(surface_position - y_axis));
    const float z_gradient = static_cast<float>(solid_geometry_query_(surface_position + z_axis)) -
        static_cast<float>(solid_geometry_query_(surface_position - z_axis));

    const glm::vec3 outward_normal(-x_gradient, -y_gradient, -z_gradient);
    const glm::vec3 horizontal_movement(-movement_direction.x, -movement_direction.y, 0.0f);
    const glm::vec3 fallback_normal = NormalizeOrFallback(
        horizontal_movement,
        glm::vec3(1.0f, 0.0f, 0.0f));
    return NormalizeOrFallback(outward_normal, fallback_normal);
}

} // namespace igi
