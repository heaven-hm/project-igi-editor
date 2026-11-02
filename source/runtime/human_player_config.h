#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include "../player_controller.h"

namespace igi {

struct HumanPlayerTuning {
    float max_health = 100.0f;
    float max_armor = 100.0f;
    float walk_speed = 4.5f;     // m/s
    float run_speed = 8.5f;      // m/s
    float crouch_speed = 2.5f;   // m/s
    float jump_impulse = 1024.0f; // vertical impulse (units/tick)
    float jump_hspeed = 17.5f;   // km/h
    float gravity = 18.6f;       // m/s^2
    float eye_height_stand = 1.68f;
    float eye_height_crouch = 0.95f;
    std::vector<int> weapon_cycle;

    PlayerController::Tuning ToControllerTuning() const;
};

class HumanPlayerConfigLoader {
public:
    static HumanPlayerTuning Load(const std::string& custom_path = "");
};

} // namespace igi
