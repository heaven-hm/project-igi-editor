#pragma once

#include "level_flow.h"

#include <string>
#include <vector>

namespace igi {

// The editor already has a lossless token view of each Task_New call. Keeping
// this input independent from LevelObject lets the runtime consume authored
// mission data without depending on editor ownership or renderer headers.
struct MissionObjectiveTaskSource {
    std::string task_type;
    std::vector<std::string> argument_tokens;
};

// Reads the six fixed DefineComputerObjective slots used by the vanilla QVM.
// Empty text-resource slots are omitted while their authored expressions are
// retained for the expression-runtime slice that follows this loader.
std::vector<AuthoredMissionObjectiveSet> LoadAuthoredMissionObjectiveDefinitions(
    const std::vector<MissionObjectiveTaskSource>& task_sources);

} // namespace igi
