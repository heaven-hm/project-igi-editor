#include "mcp_transport_http.h"

#include "mcp_json.h"
#include "mcp_json_rpc.h"
#include "mcp_server.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <chrono>
#include <cstring>
#include <sstream>
#include <string_view>
#include <thread>
#include <vector>

namespace mcp {
namespace {

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool ConstantTimeEqual(std::string_view left, std::string_view right) {
    std::size_t difference = left.size() ^ right.size();
    const std::size_t count = left.size() > right.size() ? left.size() : right.size();
    for (std::size_t index = 0; index < count; ++index) {
        const unsigned char left_byte = index < left.size()
            ? static_cast<unsigned char>(left[index]) : 0;
        const unsigned char right_byte = index < right.size()
            ? static_cast<unsigned char>(right[index]) : 0;
        difference |= static_cast<std::size_t>(left_byte ^ right_byte);
    }
    return difference == 0;
}

std::string MakeToken() {
    std::array<unsigned char, 32> bytes{};
#ifdef _WIN32
    if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) return {};
#else
    return {};
#endif
    static constexpr char hex[] = "0123456789abcdef";
    std::string token;
    token.reserve(bytes.size() * 2);
    for (const unsigned char byte : bytes) {
        token.push_back(hex[byte >> 4]);
        token.push_back(hex[byte & 0x0f]);
    }
    return token;
}

bool ParseHeaders(std::string_view text, std::map<std::string, std::string>& headers) {
    headers.clear();
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t end = text.find("\r\n", start);
        const std::string_view line = text.substr(
            start, end == std::string_view::npos ? text.size() - start : end - start);
        start = end == std::string_view::npos ? text.size() : end + 2;
        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos || colon == 0 ||
            line[colon - 1] == ' ' || line[colon - 1] == '\t') return false;
        std::string key = Lower(std::string(line.substr(0, colon)));
        std::string value(line.substr(colon + 1));
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.pop_back();
        if (!headers.emplace(std::move(key), std::move(value)).second) return false;
    }
    return true;
}

bool ParseContentLength(std::string_view text, std::size_t maximum,
                        std::size_t& content_length) {
    if (text.empty()) return false;
    content_length = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), content_length);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() &&
           content_length <= maximum;
}

#ifdef _WIN32
void SendText(SOCKET socket, int status, std::string_view status_text,
              std::string_view body, std::string_view protocol = "2026-07-28") {
    std::ostringstream response;
    response << "HTTP/1.1 " << status << " " << status_text << "\r\n"
             << "Content-Type: application/json\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n"
             << "MCP-Protocol-Version: " << protocol << "\r\n\r\n" << body;
    const std::string serialized = response.str();
    std::size_t sent = 0;
    while (sent < serialized.size()) {
        const int count = send(socket, serialized.data() + sent,
                               static_cast<int>(serialized.size() - sent), 0);
        if (count <= 0) break;
        sent += static_cast<std::size_t>(count);
    }
}

#endif

bool IsAllowedOrigin(std::string_view origin) {
    for (const std::string_view prefix : {"http://127.0.0.1", "http://localhost"}) {
        if (origin == prefix) return true;
        if (!origin.starts_with(prefix) || origin.size() == prefix.size() ||
            origin[prefix.size()] != ':') continue;
        const std::string_view port = origin.substr(prefix.size() + 1);
        if (port.empty()) return false;
        unsigned long value = 0;
        for (const char character : port) {
            if (character < '0' || character > '9') return false;
            value = value * 10u + static_cast<unsigned long>(character - '0');
            if (value > 65535u) return false;
        }
        return value != 0;
    }
    return false;
}

}  // namespace

HttpTransport::~HttpTransport() {
    Stop();
}

bool HttpTransport::ValidateRequest(const std::string& method, const std::string& path,
                                    const std::map<std::string, std::string>& headers,
                                    std::size_t body_bytes, const HttpOptions& options,
                                    std::string& error) {
    if (options.host != "127.0.0.1") {
        error = "non_loopback_bind_forbidden";
        return false;
    }
    if (method != "POST" || path != "/mcp") {
        error = "http_route_forbidden";
        return false;
    }
    if (body_bytes > options.max_body_bytes) {
        error = "body_too_large";
        return false;
    }
    const auto authorization = headers.find("authorization");
    const std::string expected_authorization = "Bearer " + options.bearer_token;
    if (authorization == headers.end() || options.bearer_token.empty() ||
        !ConstantTimeEqual(authorization->second, expected_authorization)) {
        error = "unauthorized";
        return false;
    }
    const auto content_type = headers.find("content-type");
    std::string media_type = content_type == headers.end() ? std::string{} : Lower(content_type->second);
    const std::size_t parameters = media_type.find(';');
    if (parameters != std::string::npos) media_type.resize(parameters);
    while (!media_type.empty() && std::isspace(static_cast<unsigned char>(media_type.back())))
        media_type.pop_back();
    if (media_type != "application/json") {
        error = "content_type_required";
        return false;
    }
    const auto accept = headers.find("accept");
    if (accept != headers.end() && Lower(accept->second).find("text/event-stream") != std::string::npos) {
        error = "event_stream_not_supported";
        return false;
    }
    const auto origin = headers.find("origin");
    if (origin != headers.end() && !IsAllowedOrigin(origin->second)) {
        error = "origin_forbidden";
        return false;
    }
    const auto protocol = headers.find("mcp-protocol-version");
    const auto mcp_method = headers.find("mcp-method");
    const auto mcp_name = headers.find("mcp-name");
    if (protocol == headers.end() || protocol->second != "2026-07-28" ||
        mcp_method == headers.end() || mcp_method->second.empty() ||
        mcp_name == headers.end() || mcp_name->second.empty()) {
        error = "protocol_headers_required";
        return false;
    }
    error.clear();
    return true;
}

bool HttpTransport::Start(const HttpOptions& options, McpServer& server,
                          HttpEndpoint& endpoint, std::string& error) {
#ifndef _WIN32
    (void)options; (void)server; (void)endpoint;
    error = "http_unavailable";
    return false;
#else
    if (options.host != "127.0.0.1") {
        error = "non_loopback_bind_forbidden";
        return false;
    }
    if (options.max_connections == 0) {
        error = "invalid_max_connections";
        return false;
    }
    options_ = options;
    stopping_.store(false);
    if (options_.bearer_token.empty()) options_.bearer_token = MakeToken();
    if (options_.bearer_token.empty()) {
        error = "token_generation_failed";
        return false;
    }
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        error = "socket_init_failed";
        return false;
    }
    winsock_started_ = true;
    const SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        WSACleanup();
        winsock_started_ = false;
        error = "socket_create_failed";
        return false;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(options_.port);
    if (bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
        listen(listener, 8) == SOCKET_ERROR) {
        closesocket(listener);
        WSACleanup();
        winsock_started_ = false;
        error = "socket_bind_failed";
        return false;
    }
    sockaddr_in bound{};
    int bound_size = sizeof(bound);
    getsockname(listener, reinterpret_cast<sockaddr*>(&bound), &bound_size);
    listen_socket_.store(static_cast<std::uintptr_t>(listener));
    endpoint_ = {"127.0.0.1", ntohs(bound.sin_port), options_.bearer_token};
    endpoint = endpoint_;
    worker_ = std::thread([this, &server]() { Run(server); });
    error.clear();
    return true;
#endif
}

void HttpTransport::Stop() noexcept {
#ifdef _WIN32
    std::vector<ConnectionWorker> connection_workers;
    stopping_.store(true);
    const std::uintptr_t listener_value = listen_socket_.exchange(0);
    if (listener_value != 0) {
        closesocket(static_cast<SOCKET>(listener_value));
    }
    if (worker_.joinable()) worker_.join();
    {
        std::lock_guard<std::mutex> lock(active_mutex_);
        for (const std::uintptr_t connection_value : active_connections_) {
            shutdown(static_cast<SOCKET>(connection_value), SD_BOTH);
        }
        connection_workers.swap(connection_workers_);
    }
    for (ConnectionWorker& connection_worker : connection_workers) {
        if (connection_worker.thread.joinable()) connection_worker.thread.join();
    }
    if (winsock_started_) {
        WSACleanup();
        winsock_started_ = false;
    }
#endif
}

void HttpTransport::ReapCompletedWorkers() {
    std::vector<std::thread> completed;
    {
        std::lock_guard<std::mutex> lock(active_mutex_);
        for (auto it = connection_workers_.begin(); it != connection_workers_.end();) {
            if (!it->done->load(std::memory_order_acquire)) {
                ++it;
                continue;
            }
            completed.emplace_back(std::move(it->thread));
            it = connection_workers_.erase(it);
        }
    }
    for (std::thread& thread : completed) {
        if (thread.joinable()) thread.join();
    }
}

void HttpTransport::Run(McpServer& server) {
#ifdef _WIN32
    const SOCKET listener = static_cast<SOCKET>(listen_socket_.load());
    while (!stopping_.load()) {
        ReapCompletedWorkers();
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(listener, &read_set);
        timeval poll_timeout{};
        poll_timeout.tv_usec = 100000;
        const int ready = select(0, &read_set, nullptr, nullptr, &poll_timeout);
        if (ready == 0) continue;
        if (ready == SOCKET_ERROR) {
            if (stopping_.load()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        const SOCKET connection = accept(listener, nullptr, nullptr);
        if (connection == INVALID_SOCKET) {
            if (stopping_.load()) break;
            const int accept_error = WSAGetLastError();
            if (accept_error == WSAEINVAL || accept_error == WSAENETDOWN ||
                accept_error == WSAENOTSOCK || accept_error == WSAEBADF) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        const DWORD timeout = options_.receive_timeout_ms;
        setsockopt(connection, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        bool accepted = false;
        {
            std::lock_guard<std::mutex> lock(active_mutex_);
            if (!stopping_.load() && active_connections_.size() < options_.max_connections) {
                active_connections_.insert(static_cast<std::uintptr_t>(connection));
                const auto done = std::make_shared<std::atomic_bool>(false);
                connection_workers_.push_back(ConnectionWorker{std::thread([this, &server, connection, done]() {
                    HandleConnection(static_cast<std::uintptr_t>(connection), server);
                    {
                        std::lock_guard<std::mutex> lock(active_mutex_);
                        active_connections_.erase(static_cast<std::uintptr_t>(connection));
                    }
                    closesocket(connection);
                    done->store(true, std::memory_order_release);
                }), done});
                accepted = true;
            }
        }
        if (!accepted) {
            shutdown(connection, SD_BOTH);
            closesocket(connection);
        }
    }
#else
    (void)server;
#endif
}

void HttpTransport::HandleConnection(std::uintptr_t connection_value, McpServer& server) {
#ifdef _WIN32
    const SOCKET connection = static_cast<SOCKET>(connection_value);
    std::string request;
    std::array<char, 4096> buffer{};
    std::size_t header_end = std::string::npos;
    constexpr std::size_t kMaxHeaderBytes = 64u * 1024u;
    while (request.size() < kMaxHeaderBytes && header_end == std::string::npos) {
        const int received = recv(connection, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (received <= 0) return;
        request.append(buffer.data(), static_cast<std::size_t>(received));
        header_end = request.find("\r\n\r\n");
    }
    if (header_end == std::string::npos) {
        SendText(connection, 400, "Bad Request", "{\"error\":\"headers_too_large\"}");
        return;
    }
    const std::size_t first_end = request.find("\r\n");
    if (first_end == std::string::npos) return;
    std::istringstream request_line(request.substr(0, first_end));
    std::string method, path, version;
    std::string trailing;
    if (!(request_line >> method >> path >> version) || version != "HTTP/1.1" ||
        (request_line >> trailing)) {
        SendText(connection, 400, "Bad Request", "{\"error\":\"request_rejected\"}");
        return;
    }
    std::map<std::string, std::string> headers;
    if (!ParseHeaders(std::string_view(request).substr(first_end + 2, header_end - first_end - 2), headers)) {
        SendText(connection, 400, "Bad Request", "{\"error\":\"request_rejected\"}");
        return;
    }
    std::size_t content_length = 0;
    const auto content_length_header = headers.find("content-length");
    if (content_length_header == headers.end() || headers.find("transfer-encoding") != headers.end()) {
        SendText(connection, 400, "Bad Request", "{\"error\":\"request_rejected\"}");
        return;
    }
    if (!ParseContentLength(content_length_header->second, options_.max_body_bytes, content_length)) {
        SendText(connection, 413, "Payload Too Large", "{\"error\":\"request_rejected\"}");
        return;
    }
    std::string validation_error;
    if (!ValidateRequest(method, path, headers, content_length, options_, validation_error)) {
        const int status = validation_error == "unauthorized" ? 401 :
                           validation_error == "origin_forbidden" ? 403 :
                           validation_error == "event_stream_not_supported" ? 406 :
                           validation_error == "body_too_large" ? 413 : 400;
        const char* status_text = status == 401 ? "Unauthorized" :
                                  status == 403 ? "Forbidden" :
                                  status == 406 ? "Not Acceptable" :
                                  status == 413 ? "Payload Too Large" : "Bad Request";
        SendText(connection, status, status_text,
                 "{\"error\":\"request_rejected\"}");
        return;
    }
    while (request.size() - (header_end + 4) < content_length) {
        const int received = recv(connection, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (received <= 0) return;
        request.append(buffer.data(), static_cast<std::size_t>(received));
    }
    JsonValue body;
    JsonError parse_error;
    const std::string_view payload(request.data() + header_end + 4, content_length);
    if (!JsonParse(payload, body, parse_error)) {
        SendText(connection, 400, "Bad Request",
                 JsonStringify(MakeJsonRpcError(JsonValue(nullptr), kParseError, "invalid JSON")));
        return;
    }
    if (!body.is_object()) {
        SendText(connection, 400, "Bad Request",
                 JsonStringify(MakeJsonRpcError(JsonValue(nullptr), kInvalidRequest,
                                                 "JSON-RPC request must be an object")));
        return;
    }
    const auto body_method = body.as_object().find("method");
    const auto header_method = headers.find("mcp-method");
    if (body_method == body.as_object().end() || !body_method->second.is_string() ||
        header_method == headers.end() || body_method->second.as_string() != header_method->second) {
        SendText(connection, 400, "Bad Request",
                 JsonStringify(MakeJsonRpcError(JsonValue(nullptr), kInvalidRequest,
                                                 "MCP method header does not match request")));
        return;
    }
    JsonValue request_id = JsonValue(nullptr);
    const auto id = body.as_object().find("id");
    if (id != body.as_object().end() &&
        (id->second.is_null() || id->second.is_string() || id->second.is_number())) {
        request_id = id->second;
    }
    try {
        const JsonValue response = server.Handle(body);
        if (response.is_null()) {
            SendText(connection, 202, "Accepted", "");
            return;
        }
        const std::string serialized = JsonStringify(response);
        SendText(connection, 200, "OK", serialized);
    } catch (...) {
        SendText(connection, 200, "OK",
                 JsonStringify(MakeJsonRpcError(request_id, kInternalError, "internal error")));
    }
#else
    (void)connection_value; (void)server;
#endif
}

}  // namespace mcp
