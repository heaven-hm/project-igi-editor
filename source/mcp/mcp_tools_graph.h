#pragma once

#include "mcp_tools_session.h"

#include <string>
#include <string_view>

namespace mcp {

ToolDefinitionList GraphToolDefinitions();

JsonValue CallGraphTool(GameDataService& service, std::string_view name,
                        const JsonValue& arguments, std::string& error);

}  // namespace mcp
