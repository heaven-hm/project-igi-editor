#pragma once

#include "mcp_tools_session.h"

#include <string_view>

namespace mcp {

ToolDefinitionList AssetToolDefinitions();

JsonValue CallAssetTool(GameDataService& service, std::string_view name,
                        const JsonValue& arguments, std::string& error);

}  // namespace mcp
