// level_flow.cpp - Mission objective evaluation, flow state, and extraction logic implementation
#include "level_flow.h"

namespace igi {

LevelFlow::LevelFlow() {
    InitializeMission(1);
}

void LevelFlow::InitializeMission(uint32_t mission_number) {
    mission_number_ = mission_number;
    objectives_.clear();
    status_ = MissionStatus::InProgress;

    // Default Mission 1 objective
    AddObjective(1, "Get to the train depot and rendezvous with Anya", true);
}

void LevelFlow::AddObjective(uint32_t id, const std::string& desc, bool is_primary) {
    MissionObjective obj;
    obj.id = id;
    obj.description = desc;
    obj.is_primary = is_primary;
    obj.state = ObjectiveState::Pending;
    objectives_.push_back(obj);
}

void LevelFlow::SetObjectiveState(uint32_t id, ObjectiveState state) {
    for (auto& obj : objectives_) {
        if (obj.id == id) {
            obj.state = state;
            break;
        }
    }
}

void LevelFlow::Update(bool player_alive, bool in_extraction_zone) {
    if (status_ != MissionStatus::InProgress) return;

    if (!player_alive) {
        status_ = MissionStatus::Failed;
        return;
    }

    // Check primary objectives
    bool all_primaries_complete = true;
    for (const auto& obj : objectives_) {
        if (obj.is_primary) {
            if (obj.state == ObjectiveState::Failed) {
                status_ = MissionStatus::Failed;
                return;
            }
            if (obj.state != ObjectiveState::Completed) {
                all_primaries_complete = false;
            }
        }
    }

    if (all_primaries_complete && in_extraction_zone) {
        status_ = MissionStatus::Success;
    }
}

} // namespace igi
