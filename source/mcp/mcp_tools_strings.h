#pragma once

#include "game_data_service.h"
#include "mcp_tools_session.h"

#include <string>
#include <string_view>

namespace mcp {

ToolDefinitionList StringToolDefinitions();
JsonValue CallStringTool(GameDataService& service, std::string_view name,
                         const JsonValue& arguments, std::string& error);

}  // namespace mcp
