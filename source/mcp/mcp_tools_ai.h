#pragma once

#include "mcp_tools_session.h"

namespace mcp {

ToolDefinitionList AiToolDefinitions();

JsonValue CallAiTool(GameDataService& service, std::string_view name,
                     const JsonValue& arguments, std::string& error);

}  // namespace mcp
