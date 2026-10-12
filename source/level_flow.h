// level_flow.h - Mission objective evaluation, flow state, and extraction logic
#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace igi {

enum class ObjectiveState {
    Pending,
    Completed,
    Failed,
    Cancelled
};

struct MissionObjective {
    uint32_t id = 0;
    std::string description;
    bool is_primary = true;
    ObjectiveState state = ObjectiveState::Pending;
};

enum class MissionStatus {
    InProgress,
    Success,
    Failed
};

class LevelFlow {
public:
    LevelFlow();

    void InitializeMission(uint32_t mission_number);
    void AddObjective(uint32_t id, const std::string& desc, bool is_primary = true);
    void SetObjectiveState(uint32_t id, ObjectiveState state);

    void Update(bool player_alive, bool in_extraction_zone);

    MissionStatus GetStatus() const { return status_; }
    const std::vector<MissionObjective>& GetObjectives() const { return objectives_; }
    uint32_t GetMissionNumber() const { return mission_number_; }

private:
    uint32_t mission_number_ = 1;
    std::vector<MissionObjective> objectives_;
    MissionStatus status_ = MissionStatus::InProgress;
};

} // namespace igi
