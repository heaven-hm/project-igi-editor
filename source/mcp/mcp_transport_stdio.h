#pragma once

namespace mcp {

class McpServer;

class StdioTransport {
public:
    int Run(McpServer& server) const;
};

}  // namespace mcp
