#include <gtest/gtest.h>

#include "mcp/mcp_server.h"
#include "mcp/mcp_transport_http.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

namespace {

namespace fs = std::filesystem;

std::map<std::string, std::string> GoodHeaders(const std::string& token) {
    return {
        {"authorization", "Bearer " + token},
        {"content-type", "application/json"},
        {"origin", "http://127.0.0.1"},
        {"mcp-protocol-version", "2026-07-28"},
        {"mcp-method", "server/discover"},
        {"mcp-name", "test-client"},
    };
}

TEST(McpHttpSecurityTest, RequiresLoopbackBearerAndProtocolHeaders) {
    mcp::HttpOptions options;
    options.bearer_token = "test-token";
    std::string error;
    EXPECT_TRUE(mcp::HttpTransport::ValidateRequest(
        "POST", "/mcp", GoodHeaders(options.bearer_token), 2, options, error));
    EXPECT_TRUE(error.empty());

    auto parameterized = GoodHeaders(options.bearer_token);
    parameterized["content-type"] = "application/json; charset=utf-8";
    EXPECT_TRUE(mcp::HttpTransport::ValidateRequest(
        "POST", "/mcp", parameterized, 2, options, error));

    auto headers = GoodHeaders(options.bearer_token);
    headers["authorization"] = "Bearer wrong";
    EXPECT_FALSE(mcp::HttpTransport::ValidateRequest("POST", "/mcp", headers, 2, options, error));
    EXPECT_EQ(error, "unauthorized");

    options.host = "0.0.0.0";
    EXPECT_FALSE(mcp::HttpTransport::ValidateRequest(
        "POST", "/mcp", GoodHeaders("test-token"), 2, options, error));
    EXPECT_EQ(error, "non_loopback_bind_forbidden");
}

TEST(McpHttpSecurityTest, RejectsOversizedBodiesAndUntrustedOrigins) {
    mcp::HttpOptions options;
    options.bearer_token = "test-token";
    options.max_body_bytes = 4;
    std::string error;
    auto headers = GoodHeaders(options.bearer_token);
    headers["origin"] = "https://attacker.example";
    EXPECT_FALSE(mcp::HttpTransport::ValidateRequest("POST", "/mcp", headers, 2, options, error));
    EXPECT_EQ(error, "origin_forbidden");

    headers = GoodHeaders(options.bearer_token);
    EXPECT_FALSE(mcp::HttpTransport::ValidateRequest("POST", "/mcp", headers, 5, options, error));
    EXPECT_EQ(error, "body_too_large");

    headers.erase("mcp-name");
    EXPECT_FALSE(mcp::HttpTransport::ValidateRequest("POST", "/mcp", headers, 2, options, error));
    EXPECT_EQ(error, "protocol_headers_required");

    headers = GoodHeaders(options.bearer_token);
    headers["accept"] = "text/event-stream";
    EXPECT_FALSE(mcp::HttpTransport::ValidateRequest("POST", "/mcp", headers, 2, options, error));
    EXPECT_EQ(error, "event_stream_not_supported");
}

#ifdef _WIN32

class McpHttpIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / "igi_mcp_http_test";
        std::error_code error;
        fs::remove_all(root_, error);
        const fs::path level = root_ / "missions/location0/level1";
        fs::create_directories(level, error);
        ASSERT_FALSE(error) << error.message();
        std::ofstream(level / "objects.qvm", std::ios::binary) << "fixture-qvm";
        std::ofstream(level / "objects.qsc", std::ios::binary)
            << "Task_New(1, \"Building\", \"Test\", 0, 0, 0, 0, 0, 0, \"300_01_1\");\n";
        std::string open_error;
        scope_ = mcp::ProjectScope::Open(root_, open_error);
        ASSERT_TRUE(scope_.has_value()) << open_error;
    }

    void TearDown() override {
        std::error_code error;
        fs::remove_all(root_, error);
    }

    fs::path root_;
    std::optional<mcp::ProjectScope> scope_;
};

std::string SendRequest(const mcp::HttpEndpoint& endpoint, std::string content_length,
                        std::string body, std::string request_line = "POST /mcp HTTP/1.1") {
    std::string request =
        request_line + "\r\n"
        "Host: 127.0.0.1\r\n"
        "Authorization: Bearer " + endpoint.bearer_token + "\r\n"
        "Content-Type: application/json\r\n"
        "Origin: http://127.0.0.1\r\n"
        "MCP-Protocol-Version: 2026-07-28\r\n"
        "Mcp-Method: server/discover\r\n"
        "Mcp-Name: test-client\r\n"
        "Connection: close\r\n";
    if (!content_length.empty()) request += "Content-Length: " + std::move(content_length) + "\r\n";
    request += "\r\n" + body;

    const SOCKET socket_handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    EXPECT_NE(socket_handle, INVALID_SOCKET);
    if (socket_handle == INVALID_SOCKET) return {};
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(endpoint.port);
    EXPECT_EQ(connect(socket_handle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)), 0);
    if (send(socket_handle, request.data(), static_cast<int>(request.size()), 0) == SOCKET_ERROR) {
        closesocket(socket_handle);
        return {};
    }
    std::string response;
    char buffer[2048];
    for (;;) {
        const int received = recv(socket_handle, buffer, sizeof(buffer), 0);
        if (received <= 0) break;
        response.append(buffer, static_cast<std::size_t>(received));
    }
    closesocket(socket_handle);
    return response;
}

std::string SendDiscover(const mcp::HttpEndpoint& endpoint) {
    const std::string body =
        R"({"jsonrpc":"2.0","id":7,"method":"server/discover","params":{"_meta":{"io.modelcontextprotocol/protocolVersion":"2026-07-28"}}})";
    return SendRequest(endpoint, std::to_string(body.size()), body);
}

TEST_F(McpHttpIntegrationTest, ServesStatelessDiscoveryWithoutSessionState) {
    mcp::GameDataService service(*scope_);
    mcp::McpServer server(service);
    mcp::HttpTransport transport;
    mcp::HttpOptions options;
    mcp::HttpEndpoint endpoint;
    std::string error;
    ASSERT_TRUE(transport.Start(options, server, endpoint, error)) << error;
    ASSERT_EQ(endpoint.host, "127.0.0.1");
    ASSERT_NE(endpoint.port, 0);
    ASSERT_EQ(endpoint.bearer_token.size(), 64u);

    const std::string response = SendDiscover(endpoint);
    transport.Stop();
    EXPECT_NE(response.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(response.find("MCP-Protocol-Version: 2026-07-28"), std::string::npos);
    EXPECT_EQ(response.find("Mcp-Session-Id"), std::string::npos);
    EXPECT_NE(response.find("\"protocolVersion\":\"2026-07-28\""), std::string::npos);
}

TEST_F(McpHttpIntegrationTest, RejectsZeroConnectionLimit) {
    mcp::GameDataService service(*scope_);
    mcp::McpServer server(service);
    mcp::HttpTransport transport;
    mcp::HttpOptions options;
    options.max_connections = 0;
    mcp::HttpEndpoint endpoint;
    std::string error;
    EXPECT_FALSE(transport.Start(options, server, endpoint, error));
    EXPECT_EQ(error, "invalid_max_connections");
}

TEST_F(McpHttpIntegrationTest, RejectsContentLengthThatDoesNotFitTheTargetSizeType) {
    mcp::GameDataService service(*scope_);
    mcp::McpServer server(service);
    mcp::HttpTransport transport;
    mcp::HttpOptions options;
    mcp::HttpEndpoint endpoint;
    std::string error;
    ASSERT_TRUE(transport.Start(options, server, endpoint, error)) << error;

    const std::string response = SendRequest(endpoint, "4294967296", "");
    transport.Stop();
    EXPECT_NE(response.find("HTTP/1.1 413 Payload Too Large"), std::string::npos);
}

TEST_F(McpHttpIntegrationTest, RejectsRequestsWithoutContentLength) {
    mcp::GameDataService service(*scope_);
    mcp::McpServer server(service);
    mcp::HttpTransport transport;
    mcp::HttpOptions options;
    mcp::HttpEndpoint endpoint;
    std::string error;
    ASSERT_TRUE(transport.Start(options, server, endpoint, error)) << error;

    const std::string body =
        R"({"jsonrpc":"2.0","id":7,"method":"server/discover","params":{"_meta":{"io.modelcontextprotocol/protocolVersion":"2026-07-28"}}})";
    const std::string response = SendRequest(endpoint, "", body);
    transport.Stop();
    EXPECT_NE(response.find("HTTP/1.1 400 Bad Request"), std::string::npos);
    EXPECT_NE(response.find("{\"error\":\"request_rejected\"}"), std::string::npos);
}

TEST_F(McpHttpIntegrationTest, RejectsRequestsWithDuplicateContentLength) {
    mcp::GameDataService service(*scope_);
    mcp::McpServer server(service);
    mcp::HttpTransport transport;
    mcp::HttpOptions options;
    mcp::HttpEndpoint endpoint;
    std::string error;
    ASSERT_TRUE(transport.Start(options, server, endpoint, error)) << error;

    const std::string response = SendRequest(endpoint, "1\r\nContent-Length: 999", "");
    transport.Stop();
    EXPECT_NE(response.find("HTTP/1.1 400 Bad Request"), std::string::npos);
    EXPECT_NE(response.find("{\"error\":\"request_rejected\"}"), std::string::npos);
}

TEST_F(McpHttpIntegrationTest, RejectsMalformedHeaderLines) {
    mcp::GameDataService service(*scope_);
    mcp::McpServer server(service);
    mcp::HttpTransport transport;
    mcp::HttpOptions options;
    mcp::HttpEndpoint endpoint;
    std::string error;
    ASSERT_TRUE(transport.Start(options, server, endpoint, error)) << error;

    const std::string body =
        R"({"jsonrpc":"2.0","id":7,"method":"server/discover","params":{"_meta":{"io.modelcontextprotocol/protocolVersion":"2026-07-28"}}})";
    const std::string response = SendRequest(
        endpoint, std::to_string(body.size()) + "\r\nMalformedHeaderWithoutColon", body);
    transport.Stop();
    EXPECT_NE(response.find("HTTP/1.1 400 Bad Request"), std::string::npos);
    EXPECT_NE(response.find("{\"error\":\"request_rejected\"}"), std::string::npos);
}

TEST_F(McpHttpIntegrationTest, RejectsMalformedRequestLines) {
    mcp::GameDataService service(*scope_);
    mcp::McpServer server(service);
    mcp::HttpTransport transport;
    mcp::HttpOptions options;
    mcp::HttpEndpoint endpoint;
    std::string error;
    ASSERT_TRUE(transport.Start(options, server, endpoint, error)) << error;

    const std::string body =
        R"({"jsonrpc":"2.0","id":7,"method":"server/discover","params":{"_meta":{"io.modelcontextprotocol/protocolVersion":"2026-07-28"}}})";
    const std::string response = SendRequest(endpoint, std::to_string(body.size()), body,
                                              "POST /mcp HTTP/2");
    transport.Stop();
    EXPECT_NE(response.find("HTTP/1.1 400 Bad Request"), std::string::npos);
    EXPECT_NE(response.find("{\"error\":\"request_rejected\"}"), std::string::npos);
}

TEST_F(McpHttpIntegrationTest, StopUnblocksAnIdleAuthenticatedOrUnauthenticatedClient) {
    mcp::GameDataService service(*scope_);
    mcp::McpServer server(service);
    mcp::HttpTransport transport;
    mcp::HttpOptions options;
    mcp::HttpEndpoint endpoint;
    std::string error;
    ASSERT_TRUE(transport.Start(options, server, endpoint, error)) << error;

    const SOCKET idle_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ASSERT_NE(idle_socket, INVALID_SOCKET);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(endpoint.port);
    ASSERT_EQ(connect(idle_socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)), 0);

    const auto started = std::chrono::steady_clock::now();
    transport.Stop();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    closesocket(idle_socket);
    EXPECT_LT(elapsed.count(), 1000);
}

#endif

}  // namespace
