#include "human_player_config.h"
#include "../level/qvm_parser.h"
#include "../game_clock.h"
#include <filesystem>
#include <vector>
#include <string>

namespace igi {

PlayerController::Tuning HumanPlayerTuning::ToControllerTuning() const {
    constexpr float world_units_per_meter = PlayerController::WORLD_METER;
    constexpr float ticks_per_second = static_cast<float>(GameClock::TICK_RATE_HZ);

    PlayerController::Tuning controller_tuning;
    controller_tuning.maximum_health = max_health;
    controller_tuning.maximum_armor = max_armor;
    controller_tuning.walk_speed_units_per_tick =
        walk_speed * world_units_per_meter / ticks_per_second;
    controller_tuning.run_speed_units_per_tick =
        run_speed * world_units_per_meter / ticks_per_second;
    controller_tuning.crouch_speed_units_per_tick =
        crouch_speed * world_units_per_meter / ticks_per_second;
    controller_tuning.jump_speed_units_per_tick = jump_impulse;
    // jump_hspeed is launch metadata in humanplayer.qvm; airborne steering uses
    // the verified HumanMotion per-tick air-control constant by default.
    controller_tuning.gravity_units_per_tick =
        gravity * world_units_per_meter / (ticks_per_second * ticks_per_second);
    controller_tuning.standing_eye_height_units = eye_height_stand * world_units_per_meter;
    controller_tuning.crouching_eye_height_units = eye_height_crouch * world_units_per_meter;
    return controller_tuning;
}

HumanPlayerTuning HumanPlayerConfigLoader::Load(const std::string& custom_path) {
    HumanPlayerTuning tuning;

    std::vector<std::string> search_paths;
    if (!custom_path.empty()) search_paths.push_back(custom_path);
    search_paths.push_back("humanplayer/humanplayer.qvm");
    search_paths.push_back("humanplayer.qvm");
    search_paths.push_back("D:\\IGI1\\humanplayer\\humanplayer.qvm");
    search_paths.push_back("D:\\IGI1\\humanplayer.qvm");

    for (const auto& path : search_paths) {
        if (!std::filesystem::exists(path)) continue;

        QVMFile qvm = QVM_Parse(path);
        if (!qvm.valid) continue;

        std::vector<float> float_stack;
        std::vector<int> int_stack;

        for (const auto& ins : qvm.instructions) {
            if (ins.type == QVMOpType::PUSHF) {
                float_stack.push_back(ins.operand_float);
            } else if (ins.type == QVMOpType::PUSHI || ins.type == QVMOpType::PUSHIIB || ins.type == QVMOpType::PUSHB || ins.type == QVMOpType::PUSHW) {
                int_stack.push_back((int)ins.operand);
            } else if (ins.type == QVMOpType::CALL || ins.type == QVMOpType::JSR) {
                std::string func_name = "";
                if (!ins.call_targets.empty() && ins.call_targets[0] >= 0 && (size_t)ins.call_targets[0] < qvm.identifiers.size()) {
                    func_name = qvm.identifiers[ins.call_targets[0]];
                }

                if (func_name == "DefineHumanPlayerGeneral") {
                    // DefineHumanPlayerGeneral takes 43 arguments
                    // args pushed in reverse or forward
                    if (float_stack.size() >= 8) {
                        // arg[1]: jump hspeed (17.5 km/h)
                        // arg[2]: jump vspeed (27.0 km/h) -> 1024 impulse
                        // arg[7]: max health
                        tuning.jump_hspeed = float_stack[1];
                        tuning.jump_impulse = float_stack[2] * 4096000.0f * 9.259259e-6f;
                        if (tuning.jump_impulse <= 0.0f) tuning.jump_impulse = 1024.0f;
                        tuning.max_health = float_stack[7];
                        if (tuning.max_health <= 0.0f) tuning.max_health = 100.0f;
                    }
                } else if (func_name == "DefineHumanPlayerWeaponCycle") {
                    tuning.weapon_cycle.clear();
                    for (int w : int_stack) {
                        tuning.weapon_cycle.push_back(w);
                    }
                }
            }
        }
        return tuning;
    }

    return tuning;
}

} // namespace igi
