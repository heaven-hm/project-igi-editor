// runtime_world.h - Isolated simulation world and entity snapshot manager
#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <string>
#include "../game_clock.h"
#include "../player_controller.h"
#include "../weapon_system.h"
#include "../ai_system.h"
#include "../level_flow.h"
#include "../level/task_tree.h"
#include "../level/qvm_interpreter.h"

namespace igi {

class RuntimeWorld {
public:
    RuntimeWorld();
    ~RuntimeWorld();

    // Lifecycle
    void Initialize(float (*get_terrain_z)(float x, float y));
    void Reset();

    // Simulation tick
    void UpdateSimulationTick(uint64_t tick_number, const PlayerInputCmd& input_cmd);

    // Subsystems
    PlayerController& GetPlayer() { return player_; }
    const PlayerController& GetPlayer() const { return player_; }

    WeaponSystem& GetWeapons() { return weapons_; }
    const WeaponSystem& GetWeapons() const { return weapons_; }

    AiSystem& GetAi() { return ai_; }
    const AiSystem& GetAi() const { return ai_; }

    LevelFlow& GetLevelFlow() { return level_flow_; }
    const LevelFlow& GetLevelFlow() const { return level_flow_; }

    TaskTree& GetTaskTree() { return task_tree_; }
    const TaskTree& GetTaskTree() const { return task_tree_; }

    QvmNativeRegistry& GetNativeRegistry() { return qvm_registry_; }

    bool IsMissionActive() const { return level_flow_.GetStatus() == MissionStatus::InProgress; }

private:
    float (*get_terrain_z_)(float x, float y) = nullptr;

    PlayerController player_;
    WeaponSystem weapons_;
    AiSystem ai_;
    LevelFlow level_flow_;
    TaskTree task_tree_;
    QvmNativeRegistry qvm_registry_;
};

} // namespace igi
