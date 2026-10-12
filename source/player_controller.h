// player_controller.h - Runtime player locomotion, physics integration & camera placement
#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdint>
#include "player_collision.h"

namespace igi {

struct PlayerInputCmd {
    float forward = 0.0f;     // [-1.0 .. +1.0]
    float strafe = 0.0f;      // [-1.0 .. +1.0]
    float yaw_delta = 0.0f;   // Look yaw delta degrees
    float pitch_delta = 0.0f; // Look pitch delta degrees
    bool jump = false;        // Jump key triggered
    bool crouch = false;      // Crouch key held
    bool fire = false;        // Primary fire held
    bool reload = false;      // Reload key pressed
};

enum class PlayerStanceState {
    Standing,
    Crouching,
    Airborne,
    Dead
};

class PlayerController {
public:
    static constexpr float WORLD_METER = 4096.0f;
    static constexpr float GRAVITY = 18.6f * WORLD_METER;          // 76185.6 units/s^2
    static constexpr float JUMP_SPEED = 7.5f * WORLD_METER;        // 27.0 km/h = 30720.0 units/s
    static constexpr float STANDING_EYE_HEIGHT = 1.75f * WORLD_METER; // 7168.0 units (1.75m)
    static constexpr float CROUCHING_EYE_HEIGHT = 0.95f * WORLD_METER;// 3891.2 units (0.95m)
    static constexpr float RUN_SPEED = 8.5f * WORLD_METER;         // 34816.0 units/s (8.5 m/s)
    static constexpr float WALK_SPEED = 4.5f * WORLD_METER;        // 18432.0 units/s (4.5 m/s)
    static constexpr float CROUCH_SPEED = 2.5f * WORLD_METER;      // 10240.0 units/s (2.5 m/s)
    static constexpr float AIR_CONTROL_SPEED = 6.0f * WORLD_METER; // 24576.0 units/s

    PlayerController();

    void Reset(const glm::vec3& spawn_pos, float spawn_yaw = 0.0f);
    void ApplyTuning(float max_health, float max_armor);

    // Fixed 30 Hz simulation tick
    void Tick(const PlayerInputCmd& cmd, float (*get_terrain_z)(float x, float y), const std::vector<ObstacleCollider>& obstacles = {});

    // Damage & Health
    void ApplyDamage(float amount);
    float GetHealth() const { return health_; }
    float GetArmor() const { return armor_; }
    bool IsAlive() const { return health_ > 0.0f; }

    // Coordinates & Camera
    const glm::vec3& GetPosition() const { return position_; }
    const glm::vec3& GetVelocity() const { return velocity_; }
    glm::vec3 GetEyePosition() const;
    float GetYaw() const { return yaw_; }
    float GetPitch() const { return pitch_; }
    PlayerStanceState GetStance() const { return stance_; }
    bool IsGrounded() const { return is_grounded_; }

    void SetPosition(const glm::vec3& pos) { position_ = pos; }
    void SetRotation(float yaw, float pitch) { yaw_ = yaw; pitch_ = pitch; }
    void SetOrientation(float yaw, float pitch) { yaw_ = yaw; pitch_ = pitch; }

private:
    glm::vec3 position_ = glm::vec3(0.0f);
    glm::vec3 velocity_ = glm::vec3(0.0f);
    float yaw_ = 0.0f;
    float pitch_ = 0.0f;
    float current_eye_height_ = STANDING_EYE_HEIGHT;
    float health_ = 100.0f;
    float armor_ = 100.0f;
    bool is_grounded_ = true;
    PlayerStanceState stance_ = PlayerStanceState::Standing;
    PlayerCollision collision_;
};

} // namespace igi
