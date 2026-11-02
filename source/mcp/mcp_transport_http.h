#pragma once

#include <cstdint>
#include <atomic>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace mcp {

class McpServer;

struct HttpOptions {
    std::string host = "127.0.0.1";
    std::uint16_t port = 0;
    std::string bearer_token;
    std::size_t max_body_bytes = 8u * 1024u * 1024u;
    std::uint32_t receive_timeout_ms = 5000;
    std::size_t max_connections = 64;
};

struct HttpEndpoint {
    std::string host;
    std::uint16_t port = 0;
    std::string bearer_token;
};

class HttpTransport {
public:
    HttpTransport() = default;
    ~HttpTransport();

    bool Start(const HttpOptions& options, McpServer& server,
               HttpEndpoint& endpoint, std::string& error);
    void Stop() noexcept;

    static bool ValidateRequest(const std::string& method, const std::string& path,
                                const std::map<std::string, std::string>& headers,
                                std::size_t body_bytes, const HttpOptions& options,
                                std::string& error);

private:
    void Run(McpServer& server);
    void HandleConnection(std::uintptr_t connection, McpServer& server);

    std::atomic<std::uintptr_t> listen_socket_{0};
    std::atomic_bool stopping_{false};
    std::thread worker_;
    std::vector<std::thread> connection_workers_;
    bool winsock_started_ = false;
    HttpOptions options_;
    HttpEndpoint endpoint_;
    std::mutex active_mutex_;
    std::set<std::uintptr_t> active_connections_;
};

}  // namespace mcp
