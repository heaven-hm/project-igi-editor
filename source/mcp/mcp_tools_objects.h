#pragma once

#include "mcp_tools_session.h"

#include <string>
#include <string_view>
#include <vector>

namespace mcp {

ToolDefinitionList ObjectToolDefinitions();

JsonValue CallObjectTool(GameDataService& service, std::string_view name,
                         const JsonValue& arguments, std::string& error);

}  // namespace mcp
