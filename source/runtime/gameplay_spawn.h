// gameplay_spawn.h - Authored vanilla player spawn selection.
#pragma once

#include <optional>
#include <vector>

#include <glm/glm.hpp>

namespace igi {

struct RuntimeSpawnPoint {
    glm::vec3 position = glm::vec3(0.0f);
    float yaw = 0.0f;
    float pitch = 0.0f;
};

struct RuntimeSpawnCandidate {
    bool is_human_player = false;
    int task_id = -1;
    RuntimeSpawnPoint spawn;
};

// OpenIGI's PlayerSpawn.TryFindGameplay selects the level's HumanPlayer task
// with script id zero, then the first remaining HumanPlayer task. The editor
// camera and level-start position are application fallbacks for incomplete
// editor-only levels and are intentionally outside this authored resolver.
std::optional<RuntimeSpawnPoint> SelectAuthoredPlayerSpawn(
    const std::vector<RuntimeSpawnCandidate>& candidates);

} // namespace igi
