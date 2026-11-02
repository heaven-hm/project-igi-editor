#pragma once

#include "game_data_service.h"

#include <string>
#include <string_view>
#include <vector>

namespace mcp {

struct ToolDefinition {
    std::string name;
    JsonValue input_schema;
};

using ToolDefinitionList = std::vector<ToolDefinition>;

ToolDefinitionList SessionToolDefinitions();

JsonValue CallSessionTool(GameDataService& service, std::string_view name,
                          const JsonValue& arguments, std::string& error);

}  // namespace mcp
