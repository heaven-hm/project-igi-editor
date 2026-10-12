// player_controller.cpp - Runtime player locomotion, physics integration & camera placement implementation
#include "player_controller.h"
#include <algorithm>
#include <cmath>

namespace igi {

PlayerController::PlayerController() {
    Reset(glm::vec3(0.0f));
}

void PlayerController::Reset(const glm::vec3& spawn_pos, float spawn_yaw) {
    position_ = spawn_pos;
    velocity_ = glm::vec3(0.0f);
    yaw_ = spawn_yaw;
    pitch_ = 0.0f;
    current_eye_height_ = STANDING_EYE_HEIGHT;
    health_ = 100.0f;
    armor_ = 100.0f;
    is_grounded_ = true;
    stance_ = PlayerStanceState::Standing;
}

void PlayerController::ApplyTuning(float max_health, float max_armor) {
    health_ = max_health;
    armor_ = max_armor;
}

void PlayerController::ApplyDamage(float amount) {
    if (amount <= 0.0f || health_ <= 0.0f) return;

    // Kevlar armor absorbs 60% of damage while armor > 0
    float health_damage = amount;
    if (armor_ > 0.0f) {
        float absorbed = amount * 0.60f;
        float actual_absorbed = std::min(armor_, absorbed);
        armor_ -= actual_absorbed;
        health_damage = amount - actual_absorbed;
    }

    health_ = std::max(0.0f, health_ - health_damage);
    if (health_ <= 0.0f) {
        stance_ = PlayerStanceState::Dead;
        velocity_ = glm::vec3(0.0f);
    }
}

glm::vec3 PlayerController::GetEyePosition() const {
    return glm::vec3(position_.x, position_.y, position_.z + current_eye_height_);
}

void PlayerController::Tick(const PlayerInputCmd& cmd, float (*get_terrain_z)(float x, float y), const std::vector<ObstacleCollider>& obstacles) {
    if (!IsAlive()) return;

    // 1. Look orientation integration
    yaw_ += cmd.yaw_delta;
    pitch_ = std::clamp(pitch_ + cmd.pitch_delta, -89.0f, 89.0f);

    float yaw_rad = glm::radians(yaw_);
    float cos_y = (float)std::cos(yaw_rad);
    float sin_y = (float)std::sin(yaw_rad);
    glm::vec3 forward_dir(-sin_y, cos_y, 0.0f);
    glm::vec3 right_dir(cos_y, sin_y, 0.0f);

    // 2. Stance and eye height interpolation
    bool wants_crouch = cmd.crouch;
    float target_eye_height = wants_crouch ? CROUCHING_EYE_HEIGHT : STANDING_EYE_HEIGHT;
    float eye_step = 250.0f; // Smooth ease per tick
    if (std::abs(current_eye_height_ - target_eye_height) <= eye_step) {
        current_eye_height_ = target_eye_height;
    } else {
        current_eye_height_ += (target_eye_height > current_eye_height_) ? eye_step : -eye_step;
    }

    // 3. Ground query
    PlayerGroundQuery gq = collision_.QueryGround(position_, current_eye_height_, get_terrain_z);
    float dt = 1.0f / 30.0f;

    // 4. Locomotion integration
    if (gq.is_grounded) {
        is_grounded_ = true;
        stance_ = wants_crouch ? PlayerStanceState::Crouching : PlayerStanceState::Standing;
        velocity_.z = 0.0f;
        position_.z = gq.ground_height;

        // Ground movement from input
        float move_speed = wants_crouch ? CROUCH_SPEED : RUN_SPEED;
        glm::vec3 wish_vel = (forward_dir * cmd.forward + right_dir * cmd.strafe) * move_speed;
        velocity_.x = wish_vel.x;
        velocity_.y = wish_vel.y;

        // Jump trigger
        if (cmd.jump && !wants_crouch) {
            velocity_.z = JUMP_SPEED;
            is_grounded_ = false;
            stance_ = PlayerStanceState::Airborne;
        }
    } else {
        is_grounded_ = false;
        // Airborne gravity & steering
        stance_ = PlayerStanceState::Airborne;
        velocity_.z -= GRAVITY * dt;

        // Air control
        glm::vec3 air_nudge = (forward_dir * cmd.forward + right_dir * cmd.strafe) * AIR_CONTROL_SPEED;
        velocity_.x = air_nudge.x;
        velocity_.y = air_nudge.y;
    }

    // 5. Apply delta translation (Fixed 30Hz tick)
    glm::vec3 step = velocity_ * dt;
    position_ += step;

    // Resolve collision against obstacles & enemies
    if (!obstacles.empty()) {
        collision_.ResolveObstacles(position_, obstacles, 0.4f * WORLD_METER);
    }

    // 6. Ground snap if landing
    if (!is_grounded_ && position_.z <= gq.ground_height) {
        position_.z = gq.ground_height;
        velocity_.z = 0.0f;
        is_grounded_ = true;
        stance_ = wants_crouch ? PlayerStanceState::Crouching : PlayerStanceState::Standing;
    }
}

} // namespace igi
