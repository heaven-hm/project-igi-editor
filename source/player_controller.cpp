// player_controller.cpp - Fixed-step runtime player locomotion and health.
#include "player_controller.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace igi {

namespace {

constexpr float kMinimumInputMagnitude = 0.0001f;

glm::vec3 NormalizeMovementDirection(const glm::vec3& movement_direction) {
    const float magnitude_squared = glm::dot(movement_direction, movement_direction);
    if (magnitude_squared <= kMinimumInputMagnitude * kMinimumInputMagnitude) {
        return glm::vec3(0.0f);
    }
    if (magnitude_squared <= 1.0f) {
        return movement_direction;
    }
    return movement_direction / std::sqrt(magnitude_squared);
}

} // namespace

PlayerController::PlayerController() {
    Reset(glm::vec3(0.0f));
}

void PlayerController::Reset(const glm::vec3& spawn_position, float spawn_yaw) {
    position_ = spawn_position;
    velocity_ = glm::vec3(0.0f);
    yaw_ = spawn_yaw;
    pitch_ = 0.0f;
    current_eye_height_ = standing_eye_height_units_;
    health_ = maximum_health_;
    armor_ = maximum_armor_;
    is_grounded_ = false;
    has_been_airborne_ = false;
    stance_ = PlayerStanceState::Standing;
    maximum_downward_velocity_ = 0.0f;
    slope_slide_velocity_ = glm::vec3(0.0f);
    last_landing_impact_ = {};
}

void PlayerController::ApplyTuning(float maximum_health, float maximum_armor) {
    Tuning tuning;
    tuning.maximum_health = maximum_health;
    tuning.maximum_armor = maximum_armor;
    ApplyTuning(tuning);
}

void PlayerController::ApplyTuning(const Tuning& tuning) {
    maximum_health_ = std::max(0.0f, tuning.maximum_health);
    maximum_armor_ = std::max(0.0f, tuning.maximum_armor);
    walk_speed_units_per_tick_ = std::max(0.0f, tuning.walk_speed_units_per_tick);
    run_speed_units_per_tick_ = std::max(0.0f, tuning.run_speed_units_per_tick);
    crouch_speed_units_per_tick_ = std::max(0.0f, tuning.crouch_speed_units_per_tick);
    sprint_multiplier_ = std::max(0.0f, tuning.sprint_multiplier);
    jump_speed_units_per_tick_ = std::max(0.0f, tuning.jump_speed_units_per_tick);
    air_control_speed_units_per_tick_ = std::max(0.0f, tuning.air_control_speed_units_per_tick);
    gravity_units_per_tick_ = std::max(0.0f, tuning.gravity_units_per_tick);
    standing_eye_height_units_ = std::max(0.0f, tuning.standing_eye_height_units);
    crouching_eye_height_units_ = std::max(0.0f, tuning.crouching_eye_height_units);
    health_ = maximum_health_;
    armor_ = maximum_armor_;
    current_eye_height_ = standing_eye_height_units_;
    stance_ = PlayerStanceState::Standing;
    maximum_downward_velocity_ = 0.0f;
    last_landing_impact_ = {};
}

void PlayerController::SetCollisionQuery(SolidGeometryQuery solid_geometry_query) {
    collision_.SetSolidQuery(std::move(solid_geometry_query));
}

void PlayerController::SetCeilingQuery(CeilingHeightQuery ceiling_height_query) {
    collision_.SetCeilingQuery(std::move(ceiling_height_query));
}

void PlayerController::ApplyDamage(float damage_amount) {
    if (damage_amount <= 0.0f || !IsAlive()) {
        return;
    }

    // The editor runtime currently uses the existing 60% armor absorption
    // contract. Damage values will be replaced by the vanilla hit tables once
    // the retail weapon evidence is connected to WeaponSystem.
    float health_damage = damage_amount;
    if (armor_ > 0.0f) {
        const float absorbed_damage = std::min(armor_, damage_amount * 0.60f);
        armor_ -= absorbed_damage;
        health_damage -= absorbed_damage;
    }

    ApplyHealthDamage(health_damage);
}

void PlayerController::ApplyDirectHealthDamage(float damage_amount) {
    if (damage_amount <= 0.0f || !IsAlive()) {
        return;
    }

    // verified-reference: OpenIGI HumanHealth.ApplyDirect bypasses the armor
    // accumulator used by incoming shots and applies only to health.
    ApplyHealthDamage(damage_amount);
}

void PlayerController::ApplyHealthDamage(float damage_amount) {
    health_ = std::max(0.0f, health_ - damage_amount);
    if (health_ <= 0.0f) {
        stance_ = PlayerStanceState::Dead;
        velocity_ = glm::vec3(0.0f);
        is_grounded_ = false;
    }
}

glm::vec3 PlayerController::GetEyePosition() const {
    return glm::vec3(
        position_.x,
        position_.y,
        position_.z + current_eye_height_);
}

glm::vec3 PlayerController::CalculateMovementDirection(const PlayerInputCmd& input_command) const {
    const float yaw_radians = glm::radians(yaw_);
    const float cosine_yaw = std::cos(yaw_radians);
    const float sine_yaw = std::sin(yaw_radians);
    const glm::vec3 forward_direction(-sine_yaw, cosine_yaw, 0.0f);
    const glm::vec3 right_direction(cosine_yaw, sine_yaw, 0.0f);

    const float forward_input = std::clamp(input_command.forward, -1.0f, 1.0f);
    const float strafe_input = std::clamp(input_command.strafe, -1.0f, 1.0f);
    return NormalizeMovementDirection(
        forward_direction * forward_input + right_direction * strafe_input);
}

bool PlayerController::ResolveRequestedStance(bool requested_crouch) const {
    if (requested_crouch) {
        return true;
    }
    if (stance_ != PlayerStanceState::Crouching) {
        return false;
    }
    return !collision_.CanStandUp(position_, standing_eye_height_units_, nullptr);
}

void PlayerController::IntegrateGroundMovement(
    const PlayerInputCmd& input_command,
    const glm::vec3& movement_direction) {
    const float movement_speed = input_command.crouch
        ? crouch_speed_units_per_tick_
        : (input_command.sprint
            ? run_speed_units_per_tick_ * sprint_multiplier_
            : walk_speed_units_per_tick_);
    velocity_.x = movement_direction.x * movement_speed;
    velocity_.y = movement_direction.y * movement_speed;
    velocity_.z = 0.0f;
}

void PlayerController::IntegrateAirMovement(
    const PlayerInputCmd& input_command,
    bool took_off) {
    if (!took_off) {
        velocity_ = PlayerMotion::IntegrateAirborneVelocity(
            velocity_,
            gravity_units_per_tick_);
    }

    velocity_ += PlayerMotion::CalculateAirControl(
        input_command.forward,
        input_command.strafe,
        yaw_,
        air_control_speed_units_per_tick_);
}

void PlayerController::UpdateEyeHeight(bool crouching) {
    const float target_eye_height = crouching
        ? crouching_eye_height_units_
        : standing_eye_height_units_;
    const float eye_height_delta = std::clamp(
        target_eye_height - current_eye_height_,
        -CROUCH_EASE_UNITS_PER_TICK,
        CROUCH_EASE_UNITS_PER_TICK);
    current_eye_height_ += eye_height_delta;
}

void PlayerController::ApplyLandingImpactDamage(float maximum_downward_velocity) {
    // maximum_downward_velocity already includes the final gravity write in
    // this fixed step, matching the reference's landingVelocityZ - GravityPerTick
    // input to HumanFallDamage.Calculate.
    last_landing_impact_ = CalculateVanillaFallImpact(
        maximum_downward_velocity,
        maximum_health_);
    ApplyDirectHealthDamage(last_landing_impact_.damage);
}

void PlayerController::Tick(
    const PlayerInputCmd& input_command,
    PlayerCollision::TerrainHeightQuery terrain_height_query,
    const std::vector<ObstacleCollider>& obstacles,
    bool (*legacy_collision_query)(float x, float y, float z)) {
    last_landing_impact_ = {};
    if (!IsAlive()) {
        return;
    }

    yaw_ += input_command.yaw_delta;
    pitch_ = std::clamp(pitch_ + input_command.pitch_delta, -89.0f, 89.0f);

    if (legacy_collision_query != nullptr) {
        SetCollisionQuery([legacy_collision_query](const glm::vec3& sample_position) {
            return legacy_collision_query(
                sample_position.x,
                sample_position.y,
                sample_position.z);
        });
    }

    const bool was_grounded = is_grounded_;
    const float vertical_velocity_before_tick = velocity_.z;
    if (!was_grounded && vertical_velocity_before_tick < 0.0f) {
        maximum_downward_velocity_ = std::min(
            maximum_downward_velocity_,
            vertical_velocity_before_tick);
    }

    const bool should_crouch = ResolveRequestedStance(input_command.crouch);
    UpdateEyeHeight(should_crouch);

    const glm::vec3 root_motion_step = PlayerMotion::ApplyRootMotion(
        input_command.root_motion_delta,
        yaw_,
        input_command.root_motion_scale,
        input_command.suppress_root_motion_scale);
    const bool has_root_motion = glm::dot(
        input_command.root_motion_delta,
        input_command.root_motion_delta) > kMinimumInputMagnitude * kMinimumInputMagnitude;

    const PlayerGroundQuery ground_query = collision_.QueryGround(
        position_,
        current_eye_height_,
        terrain_height_query,
        is_grounded_,
        should_crouch);
    is_grounded_ = ground_query.is_grounded && velocity_.z <= 0.0f;

    const glm::vec3 movement_direction = CalculateMovementDirection(input_command);
    if (is_grounded_) {
        stance_ = should_crouch ? PlayerStanceState::Crouching : PlayerStanceState::Standing;
        position_.z = ground_query.ground_height;
        IntegrateGroundMovement(input_command, movement_direction);
        if (has_root_motion) {
            // OpenIGI's grounded human velocity is zero; the animation delta
            // carries the actual step. Preserve the keyboard-speed fallback
            // when no animation sample has reached this controller yet.
            velocity_.x = 0.0f;
            velocity_.y = 0.0f;
        }

        if (input_command.jump && !should_crouch) {
            velocity_.z = jump_speed_units_per_tick_;
            is_grounded_ = false;
            stance_ = PlayerStanceState::Airborne;
        }
    } else {
        stance_ = PlayerStanceState::Airborne;
        IntegrateAirMovement(input_command, false);
    }

    const glm::vec3 requested_position = position_ + velocity_ +
        root_motion_step + slope_slide_velocity_;
    const PlayerWallSweepResult wall_sweep_result = collision_.SweepWalls(
        position_,
        requested_position,
        BODY_RADIUS,
        current_eye_height_,
        is_grounded_,
        should_crouch);
    position_ += wall_sweep_result.slide_velocity;

    if (!obstacles.empty()) {
        collision_.ResolveObstacles(position_, obstacles, BODY_RADIUS);
    }

    const PlayerGroundQuery post_move_ground_query = collision_.QueryGround(
        position_,
        current_eye_height_,
        terrain_height_query,
        is_grounded_,
        should_crouch);
    if (velocity_.z < 0.0f) {
        maximum_downward_velocity_ = std::min(
            maximum_downward_velocity_,
            velocity_.z);
    }

    if (velocity_.z <= 0.0f && post_move_ground_query.is_grounded) {
        position_.z = post_move_ground_query.ground_height;
        velocity_.z = 0.0f;
        is_grounded_ = true;
        stance_ = should_crouch ? PlayerStanceState::Crouching : PlayerStanceState::Standing;
        if (!was_grounded && has_been_airborne_) {
            ApplyLandingImpactDamage(maximum_downward_velocity_);
        }
        maximum_downward_velocity_ = 0.0f;
    } else {
        is_grounded_ = false;
        has_been_airborne_ = true;
        stance_ = PlayerStanceState::Airborne;
    }

    slope_slide_velocity_ = PlayerCollision::AccumulateSlopeSlide(
        slope_slide_velocity_,
        post_move_ground_query.surface_normal,
        is_grounded_);

}

} // namespace igi
