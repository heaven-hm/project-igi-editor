#pragma once

#include "level_flow.h"

#include <string>
#include <vector>

namespace igi {

// Lossless task input for one authored LevelFlow declaration. The runtime
// consumes this small value type instead of depending on editor-owned objects.
struct MissionFlowTaskSource {
    std::string task_type;
    std::vector<std::string> argument_tokens;
};

// Reads the LevelFlow fields declared by vanilla IGI1 objects.qsc files.
// Multiple rows are preserved in task order so callers can apply the same
// last-authored-definition rule used by the retail task tree.
std::vector<AuthoredMissionFlowDefinition> LoadAuthoredMissionFlowDefinitions(
    const std::vector<MissionFlowTaskSource>& task_sources);

} // namespace igi
