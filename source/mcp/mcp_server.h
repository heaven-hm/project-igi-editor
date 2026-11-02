#pragma once

#include "game_data_service.h"
#include "mcp_json.h"

#include <string>
#include <string_view>
#include <vector>

namespace mcp {

class McpServer {
public:
    explicit McpServer(GameDataService& service) : service_(service) {}

    JsonValue Handle(const JsonValue& request);
    JsonValue ToolList() const;
    JsonValue ResourceList() const;

private:
    struct RegisteredTool {
        std::string name;
        JsonValue input_schema;
        std::string domain;
    };

    std::vector<RegisteredTool> RegisteredTools() const;
    JsonValue CallTool(std::string_view name, const JsonValue& arguments, std::string& error);
    JsonValue ReadResource(std::string_view uri, std::string& error) const;

    GameDataService& service_;
};

}  // namespace mcp
