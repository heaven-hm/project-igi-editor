#pragma once

#include "mcp_tools_session.h"

namespace mcp {

ToolDefinitionList MissionToolDefinitions();

JsonValue CallMissionTool(GameDataService& service, std::string_view name,
                          const JsonValue& arguments, std::string& error);

}  // namespace mcp
