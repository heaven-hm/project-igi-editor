// gameplay_spawn.cpp - Authored vanilla player spawn selection.
#include "gameplay_spawn.h"

namespace igi {

std::optional<RuntimeSpawnPoint> SelectAuthoredPlayerSpawn(
    const std::vector<RuntimeSpawnCandidate>& candidates) {
    for (const RuntimeSpawnCandidate& candidate : candidates) {
        if (candidate.is_human_player && candidate.task_id == 0) {
            return candidate.spawn;
        }
    }

    for (const RuntimeSpawnCandidate& candidate : candidates) {
        if (candidate.is_human_player) {
            return candidate.spawn;
        }
    }

    return std::nullopt;
}

} // namespace igi
