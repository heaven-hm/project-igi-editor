#include "game_data_service.h"
#include "mcp_server.h"
#include "mcp_transport_http.h"
#include "mcp_transport_stdio.h"

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

#ifdef _WIN32
HANDLE g_http_stop_event = nullptr;

BOOL WINAPI HandleConsoleControl(DWORD control_type) {
    if ((control_type == CTRL_C_EVENT || control_type == CTRL_BREAK_EVENT ||
         control_type == CTRL_CLOSE_EVENT) && g_http_stop_event != nullptr) {
        SetEvent(g_http_stop_event);
        return TRUE;
    }
    return FALSE;
}

void WaitForHttpStop() {
    g_http_stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (g_http_stop_event == nullptr) return;
    SetConsoleCtrlHandler(HandleConsoleControl, TRUE);
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    std::thread stdin_watcher([input] {
        if (input == nullptr || input == INVALID_HANDLE_VALUE) return;
        const HANDLE wait_handles[] = {input, g_http_stop_event};
        if (GetFileType(input) == FILE_TYPE_CHAR) {
            for (;;) {
                const DWORD wait = WaitForMultipleObjects(2, wait_handles, FALSE, INFINITE);
                if (wait == WAIT_OBJECT_0 + 1) return;
                if (wait != WAIT_OBJECT_0) {
                    SetEvent(g_http_stop_event);
                    return;
                }
                INPUT_RECORD records[32]{};
                DWORD count = 0;
                if (!ReadConsoleInputW(input, records, 32, &count)) {
                    SetEvent(g_http_stop_event);
                    return;
                }
                for (DWORD index = 0; index < count; ++index) {
                    if (records[index].EventType == KEY_EVENT &&
                        records[index].Event.KeyEvent.bKeyDown &&
                        (records[index].Event.KeyEvent.uChar.UnicodeChar == L'\r' ||
                         records[index].Event.KeyEvent.uChar.UnicodeChar == L'\n')) {
                        SetEvent(g_http_stop_event);
                        return;
                    }
                }
            }
        }
        for (;;) {
            const DWORD wait = WaitForMultipleObjects(2, wait_handles, FALSE, INFINITE);
            if (wait == WAIT_OBJECT_0 + 1) return;
            if (wait != WAIT_OBJECT_0) {
                SetEvent(g_http_stop_event);
                return;
            }
            char character = '\0';
            DWORD count = 0;
            if (!ReadFile(input, &character, 1, &count, nullptr) || count == 0) {
                SetEvent(g_http_stop_event);
                return;
            }
            if (character == '\n') {
                SetEvent(g_http_stop_event);
                return;
            }
        }
    });
    WaitForSingleObject(g_http_stop_event, INFINITE);
    if (stdin_watcher.joinable()) stdin_watcher.join();
    SetConsoleCtrlHandler(HandleConsoleControl, FALSE);
    CloseHandle(g_http_stop_event);
    g_http_stop_event = nullptr;
}
#else
void WaitForHttpStop() {
    std::string line;
    std::getline(std::cin, line);
}
#endif

}  // namespace

int main(int argc, char** argv) {
    bool stdio = true;
    std::filesystem::path project;
    std::string host = "127.0.0.1";
    std::uint16_t port = 0;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--stdio") {
            stdio = true;
        } else if (argument == "--http") {
            stdio = false;
        } else if (argument == "--project" && index + 1 < argc) {
            project = argv[++index];
        } else if (argument == "--host" && index + 1 < argc) {
            host = argv[++index];
        } else if (argument == "--port" && index + 1 < argc) {
            const std::string_view port_text = argv[++index];
            std::uint32_t parsed = 0;
            const auto [end, parse_error] = std::from_chars(
                port_text.data(), port_text.data() + port_text.size(), parsed);
            if (parse_error != std::errc{} || end != port_text.data() + port_text.size() ||
                parsed > 65535) {
                std::cerr << "invalid port\n";
                return 2;
            }
            port = static_cast<std::uint16_t>(parsed);
        } else {
            std::cerr << "usage: igi_mcp [--stdio | --http [--host <ip>] [--port <port>]]"
                         " --project <game-root>\n";
            return 2;
        }
    }
    if (project.empty()) {
        std::cerr << "--project is required\n";
        return 2;
    }
    std::string error;
    const auto scope = mcp::ProjectScope::Open(project, error);
    if (!scope) {
        std::cerr << "invalid project root\n";
        return 2;
    }
    mcp::GameDataService service(*scope);
    mcp::McpServer server(service);
    if (!stdio) {
        mcp::HttpTransport transport;
        mcp::HttpEndpoint endpoint;
        mcp::HttpOptions options;
        options.host = host;
        options.port = port;
        if (!transport.Start(options, server, endpoint, error)) {
            std::cerr << "HTTP start failed\n";
            return 2;
        }
        std::cerr << "MCP HTTP listening on http://" << endpoint.host << ':' << endpoint.port
                  << "/mcp\nBearer token: " << endpoint.bearer_token << "\n";
        WaitForHttpStop();
        transport.Stop();
        return 0;
    }
    return mcp::StdioTransport().Run(server);
}
