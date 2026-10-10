#include "mcp_tools_runtime.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <objidl.h>
#include <gdiplus.h>
#include <psapi.h>
#include <wbemidl.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "wbemuuid.lib")

namespace mcp {
namespace {

struct ManagedGame {
    std::mutex mutex;
    DWORD pid = 0;
    int level = 0;
    std::filesystem::path executable;
    std::filesystem::path working_directory;
};

ManagedGame& State() {
    static ManagedGame state;
    return state;
}

JsonValue EmptyObjectSchema() {
    return JsonValue::Object{
        {"type", JsonValue("object")},
        {"properties", JsonValue::Object{}},
        {"additionalProperties", JsonValue(false)},
    };
}

JsonValue Failure(std::string& error, std::string_view code) {
    error.assign(code);
    return JsonValue(nullptr);
}

bool HasOnlyKeys(const JsonValue& value,
                 std::initializer_list<std::string_view> allowed_keys) {
    if (!value.is_object()) return false;
    for (const auto& [key, ignored] : value.as_object()) {
        bool allowed = false;
        for (const std::string_view candidate : allowed_keys) {
            if (key == candidate) {
                allowed = true;
                break;
            }
        }
        if (!allowed) return false;
    }
    return true;
}

bool ReadString(const JsonValue& value, std::string& result, std::size_t maximum,
                bool allow_empty = false) {
    if (!value.is_string() || value.as_string().size() > maximum ||
        (!allow_empty && value.as_string().empty())) return false;
    for (const unsigned char character : value.as_string()) {
        if (character < 0x20u) return false;
    }
    result = value.as_string();
    return true;
}

bool ReadInteger(const JsonValue& value, int& result, int minimum, int maximum) {
    if (!value.is_number()) return false;
    const double number = value.as_number();
    if (!std::isfinite(number) || std::trunc(number) != number ||
        number < static_cast<double>(minimum) || number > static_cast<double>(maximum)) return false;
    result = static_cast<int>(number);
    return true;
}

bool IsSafeRelativePath(std::string_view path) {
    if (path.empty() || path.front() == '/' || path.find(':') != std::string_view::npos)
        return false;
    std::string normalized(path);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    if (normalized.find("//") != std::string::npos) return false;
    std::size_t start = 0;
    while (start <= normalized.size()) {
        const std::size_t end = normalized.find('/', start);
        const std::string_view component(normalized.data() + start,
                                         end == std::string::npos ? normalized.size() - start : end - start);
        if (component.empty() || component == "..") return false;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}

std::wstring QuoteCommandPath(const std::filesystem::path& path) {
    std::wstring value = L"\"";
    for (const wchar_t character : path.wstring()) {
        if (character == L'\"') value.push_back(L'\\');
        value.push_back(character);
    }
    value += L"\"";
    return value;
}

void Release(IUnknown* object) {
    if (object != nullptr) object->Release();
}

bool WmiLaunch(const std::filesystem::path& executable,
               const std::filesystem::path& working_directory,
               int level, DWORD& pid) {
    const std::wstring command = QuoteCommandPath(executable) + L" window level" +
                                 std::to_wstring(level);
    HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitialize = SUCCEEDED(init);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) return false;

    IWbemLocator* locator = nullptr;
    IWbemServices* services = nullptr;
    IWbemClassObject* process_class = nullptr;
    IWbemClassObject* input_signature = nullptr;
    IWbemClassObject* input = nullptr;
    IWbemClassObject* output = nullptr;
    bool success = false;
    do {
        HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_IWbemLocator, reinterpret_cast<void**>(&locator));
        if (FAILED(hr)) break;
        BSTR namespace_name = SysAllocString(L"ROOT\\CIMV2");
        hr = locator->ConnectServer(namespace_name, nullptr, nullptr, nullptr, 0, nullptr,
                                     nullptr, &services);
        SysFreeString(namespace_name);
        if (FAILED(hr)) break;
        hr = CoSetProxyBlanket(services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                               RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
                               nullptr, EOAC_NONE);
        if (FAILED(hr)) break;

        BSTR class_name = SysAllocString(L"Win32_Process");
        hr = services->GetObject(class_name, 0, nullptr, &process_class, nullptr);
        SysFreeString(class_name);
        if (FAILED(hr)) break;
        BSTR method_name = SysAllocString(L"Create");
        hr = process_class->GetMethod(method_name, 0, &input_signature, nullptr);
        if (FAILED(hr)) {
            SysFreeString(method_name);
            break;
        }
        hr = input_signature->SpawnInstance(0, &input);
        if (FAILED(hr)) {
            SysFreeString(method_name);
            break;
        }

        VARIANT command_value;
        VariantInit(&command_value);
        command_value.vt = VT_BSTR;
        command_value.bstrVal = SysAllocString(command.c_str());
        hr = input->Put(L"CommandLine", 0, &command_value, 0);
        VariantClear(&command_value);
        if (FAILED(hr)) {
            SysFreeString(method_name);
            break;
        }
        VARIANT directory_value;
        VariantInit(&directory_value);
        directory_value.vt = VT_BSTR;
        directory_value.bstrVal = SysAllocString(working_directory.wstring().c_str());
        hr = input->Put(L"CurrentDirectory", 0, &directory_value, 0);
        VariantClear(&directory_value);
        if (FAILED(hr)) {
            SysFreeString(method_name);
            break;
        }

        hr = services->ExecMethod(nullptr, method_name, 0, nullptr, input, &output, nullptr);
        SysFreeString(method_name);
        if (FAILED(hr) || output == nullptr) break;

        VARIANT return_value;
        VariantInit(&return_value);
        VARIANT process_id;
        VariantInit(&process_id);
        hr = output->Get(L"ReturnValue", 0, &return_value, nullptr, nullptr);
        if (FAILED(hr) || return_value.vt != VT_I4 || return_value.lVal != 0) {
            VariantClear(&return_value);
            VariantClear(&process_id);
            break;
        }
        hr = output->Get(L"ProcessId", 0, &process_id, nullptr, nullptr);
        if (SUCCEEDED(hr) && (process_id.vt == VT_I4 || process_id.vt == VT_UI4)) {
            pid = process_id.vt == VT_I4 ? static_cast<DWORD>(process_id.lVal) : process_id.ulVal;
            success = pid != 0;
        }
        VariantClear(&return_value);
        VariantClear(&process_id);
    } while (false);

    Release(output);
    Release(input);
    Release(input_signature);
    Release(process_class);
    Release(services);
    Release(locator);
    if (uninitialize) CoUninitialize();
    return success;
}

struct WindowSearch {
    DWORD pid = 0;
    HWND hwnd = nullptr;
};

BOOL CALLBACK FindVisibleWindow(HWND hwnd, LPARAM parameter) {
    auto* search = reinterpret_cast<WindowSearch*>(parameter);
    DWORD window_pid = 0;
    GetWindowThreadProcessId(hwnd, &window_pid);
    if (window_pid == search->pid && IsWindowVisible(hwnd) && GetWindow(hwnd, GW_OWNER) == nullptr) {
        search->hwnd = hwnd;
        return FALSE;
    }
    return TRUE;
}

bool QueryImagePath(HANDLE process, std::filesystem::path& path) {
    std::wstring buffer(32768, L'\0');
    DWORD size = static_cast<DWORD>(buffer.size());
    if (!QueryFullProcessImageNameW(process, 0, buffer.data(), &size)) return false;
    buffer.resize(size);
    path = std::filesystem::path(buffer);
    return true;
}

struct ProcessSnapshot {
    bool running = false;
    bool responding = false;
    DWORD session_id = 0;
    SIZE_T working_set = 0;
    HWND window = nullptr;
    std::filesystem::path image_path;
};

bool QueryProcess(DWORD pid, const std::filesystem::path& expected_image,
                 ProcessSnapshot& snapshot) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
                                 FALSE, pid);
    if (process == nullptr) return false;
    snapshot.running = true;
    QueryImagePath(process, snapshot.image_path);
    if (!expected_image.empty() && snapshot.image_path != expected_image &&
        _wcsicmp(snapshot.image_path.wstring().c_str(), expected_image.wstring().c_str()) != 0) {
        CloseHandle(process);
        return false;
    }
    ProcessIdToSessionId(pid, &snapshot.session_id);
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(process, &counters, sizeof(counters)))
        snapshot.working_set = counters.WorkingSetSize;
    WindowSearch search{pid, nullptr};
    EnumWindows(FindVisibleWindow, reinterpret_cast<LPARAM>(&search));
    snapshot.window = search.hwnd;
    snapshot.responding = snapshot.window != nullptr && !IsHungAppWindow(snapshot.window);
    CloseHandle(process);
    return true;
}

bool FindConfiguredProcess(const std::filesystem::path& expected_image, DWORD& pid,
                           ProcessSnapshot& snapshot) {
    HANDLE processes = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (processes == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool found = false;
    if (Process32FirstW(processes, &entry)) {
        do {
            if (entry.th32ProcessID == 0) continue;
            ProcessSnapshot candidate;
            if (QueryProcess(entry.th32ProcessID, expected_image, candidate)) {
                pid = entry.th32ProcessID;
                snapshot = std::move(candidate);
                found = true;
                break;
            }
        } while (Process32NextW(processes, &entry));
    }
    CloseHandle(processes);
    return found;
}

bool ReadManagedProcess(ManagedGame& state, ProcessSnapshot& snapshot) {
    if (state.pid == 0) return false;
    if (QueryProcess(state.pid, state.executable, snapshot)) return true;
    state.pid = 0;
    state.level = 0;
    state.executable.clear();
    state.working_directory.clear();
    return false;
}

JsonValue ProcessResult(DWORD pid, int level, const std::filesystem::path& executable,
                        const ProcessSnapshot& snapshot, bool managed) {
    return JsonValue::Object{
        {"running", snapshot.running},
        {"pid", static_cast<double>(pid)},
        {"level", level},
        {"managed", managed},
        {"session_id", static_cast<double>(snapshot.session_id)},
        {"responding", snapshot.responding},
        {"working_set_bytes", static_cast<double>(snapshot.working_set)},
        {"executable", executable.string()},
        {"window_handle", static_cast<double>(reinterpret_cast<std::uintptr_t>(snapshot.window))},
    };
}

int FindPngEncoder(std::vector<Gdiplus::ImageCodecInfo>& codecs) {
    UINT count = 0;
    UINT bytes = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok || bytes == 0) return -1;
    codecs.resize(bytes / sizeof(Gdiplus::ImageCodecInfo) + 1);
    if (Gdiplus::GetImageEncoders(count, bytes, codecs.data()) != Gdiplus::Ok) return -1;
    for (UINT index = 0; index < count; ++index) {
        if (std::wstring(codecs[index].MimeType) == L"image/png") return static_cast<int>(index);
    }
    return -1;
}

bool CapturePng(HWND hwnd, const std::filesystem::path& path) {
    RECT client{};
    if (!GetClientRect(hwnd, &client)) return false;
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384) return false;
    HDC source = GetDC(hwnd);
    HDC memory = CreateCompatibleDC(source);
    HBITMAP bitmap = CreateCompatibleBitmap(source, width, height);
    if (source == nullptr || memory == nullptr || bitmap == nullptr) {
        if (bitmap) DeleteObject(bitmap);
        if (memory) DeleteDC(memory);
        if (source) ReleaseDC(hwnd, source);
        return false;
    }
    HGDIOBJ previous = SelectObject(memory, bitmap);
    const bool copied = BitBlt(memory, 0, 0, width, height, source, 0, 0, SRCCOPY | CAPTUREBLT) != FALSE;
    SelectObject(memory, previous);
    DeleteDC(memory);
    ReleaseDC(hwnd, source);
    if (!copied) {
        DeleteObject(bitmap);
        return false;
    }

    Gdiplus::GdiplusStartupInput startup_input;
    ULONG_PTR token = 0;
    if (Gdiplus::GdiplusStartup(&token, &startup_input, nullptr) != Gdiplus::Ok) {
        DeleteObject(bitmap);
        return false;
    }
    std::vector<Gdiplus::ImageCodecInfo> codecs;
    const int encoder = FindPngEncoder(codecs);
    bool saved = false;
    if (encoder >= 0) {
        Gdiplus::Bitmap image(bitmap, nullptr);
        saved = image.Save(path.wstring().c_str(), &codecs[static_cast<std::size_t>(encoder)].Clsid) == Gdiplus::Ok;
    }
    Gdiplus::GdiplusShutdown(token);
    DeleteObject(bitmap);
    return saved;
}

std::pair<std::filesystem::path, std::filesystem::path> GameInstallPaths(
    const GameDataService& service) {
    const std::filesystem::path root = service.scope().root();
    const std::filesystem::path root_executable = root / "igi.exe";
    std::error_code error;
    if (std::filesystem::is_regular_file(root_executable, error)) return {root_executable, root};
    const std::filesystem::path parent = root.parent_path();
    const std::filesystem::path parent_executable = parent / "igi.exe";
    if (std::filesystem::is_regular_file(parent_executable, error)) return {parent_executable, parent};
    return {root_executable, root};
}

}  // namespace

ToolDefinitionList RuntimeToolDefinitions() {
    JsonValue::Object launch{
        {"type", JsonValue("object")},
        {"properties", JsonValue::Object{
            {"level", JsonValue::Object{{"type", JsonValue("integer")}, {"minimum", JsonValue(1)}, {"maximum", JsonValue(14)}}},
        }},
        {"required", JsonValue::Array{JsonValue("level")}},
        {"additionalProperties", JsonValue(false)},
    };
    JsonValue::Object screenshot{
        {"type", JsonValue("object")},
        {"properties", JsonValue::Object{{"path", JsonValue::Object{{"type", JsonValue("string")}, {"minLength", JsonValue(1)}, {"maxLength", JsonValue(512)}}}}},
        {"required", JsonValue::Array{JsonValue("path")}},
        {"additionalProperties", JsonValue(false)},
    };
    return ToolDefinitionList{
        {"game_launch", JsonValue(std::move(launch))},
        {"game_stop", EmptyObjectSchema()},
        {"game_get_status", EmptyObjectSchema()},
        {"game_capture_screenshot", JsonValue(std::move(screenshot))},
    };
}

JsonValue CallRuntimeTool(GameDataService& service, std::string_view name,
                          const JsonValue& arguments, std::string& error) {
    error.clear();
    ManagedGame& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);

    if (name == "game_get_status") {
        if (!HasOnlyKeys(arguments, {})) return Failure(error, "invalid_arguments");
        ProcessSnapshot snapshot;
        if (!ReadManagedProcess(state, snapshot)) {
            const auto [configured_executable, ignored_directory] = GameInstallPaths(service);
            DWORD observed_pid = 0;
            if (FindConfiguredProcess(configured_executable, observed_pid, snapshot))
                return ProcessResult(observed_pid, 0, configured_executable, snapshot, false);
            return JsonValue::Object{{"running", false}, {"pid", 0}, {"level", 0},
                                     {"managed", false},
                                     {"session_id", 0}, {"responding", false},
                                     {"working_set_bytes", 0}, {"executable", ""},
                                     {"window_handle", 0}};
        }
        return ProcessResult(state.pid, state.level, state.executable, snapshot, true);
    }

    if (name == "game_launch") {
        if (!HasOnlyKeys(arguments, {"level"}) || !arguments.contains("level"))
            return Failure(error, "invalid_arguments");
        int level = 0;
        if (!ReadInteger(arguments.at("level"), level, 1, 14))
            return Failure(error, "invalid_arguments");
        ProcessSnapshot running;
        if (ReadManagedProcess(state, running)) return Failure(error, "game_already_running");

        const auto [executable, working_directory] = GameInstallPaths(service);
        std::error_code filesystem_error;
        if (!std::filesystem::is_regular_file(executable, filesystem_error))
            return Failure(error, "game_executable_missing");
        DWORD observed_pid = 0;
        if (FindConfiguredProcess(executable, observed_pid, running))
            return Failure(error, "game_already_running");
        DWORD pid = 0;
        if (!WmiLaunch(executable, working_directory, level, pid))
            return Failure(error, "game_launch_failed");
        state.pid = pid;
        state.level = level;
        state.executable = executable;
        state.working_directory = working_directory;
        ProcessSnapshot snapshot;
        if (!ReadManagedProcess(state, snapshot)) return Failure(error, "game_launch_failed");
        return ProcessResult(state.pid, state.level, state.executable, snapshot, true);
    }

    if (name == "game_stop") {
        if (!HasOnlyKeys(arguments, {})) return Failure(error, "invalid_arguments");
        ProcessSnapshot snapshot;
        if (!ReadManagedProcess(state, snapshot)) {
            const auto [configured_executable, ignored_directory] = GameInstallPaths(service);
            DWORD observed_pid = 0;
            if (FindConfiguredProcess(configured_executable, observed_pid, snapshot))
                return Failure(error, "game_not_managed");
            return Failure(error, "game_not_running");
        }
        HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, state.pid);
        if (process == nullptr || !TerminateProcess(process, 0)) {
            if (process) CloseHandle(process);
            return Failure(error, "game_stop_failed");
        }
        WaitForSingleObject(process, 5000);
        CloseHandle(process);
        const DWORD stopped_pid = state.pid;
        state.pid = 0;
        state.level = 0;
        state.executable.clear();
        state.working_directory.clear();
        return JsonValue::Object{{"stopped", true}, {"pid", static_cast<double>(stopped_pid)}};
    }

    if (name == "game_capture_screenshot") {
        if (!HasOnlyKeys(arguments, {"path"}) || !arguments.contains("path"))
            return Failure(error, "invalid_arguments");
        std::string relative_path;
        if (!ReadString(arguments.at("path"), relative_path, 512) ||
            !IsSafeRelativePath(relative_path) ||
            std::filesystem::path(relative_path).extension() != ".png")
            return Failure(error, "path_forbidden");
        ProcessSnapshot snapshot;
        bool managed = ReadManagedProcess(state, snapshot);
        const DWORD previous_pid = state.pid;
        const int previous_level = state.level;
        const std::filesystem::path previous_executable = state.executable;
        const std::filesystem::path previous_working_directory = state.working_directory;
        if (!managed) {
            const auto [configured_executable, ignored_directory] = GameInstallPaths(service);
            DWORD observed_pid = 0;
            if (!FindConfiguredProcess(configured_executable, observed_pid, snapshot))
                return Failure(error, "game_not_running");
            state.pid = observed_pid;
            state.executable = configured_executable;
        }
        if (snapshot.window == nullptr || !snapshot.responding) {
            state.pid = previous_pid;
            state.level = previous_level;
            state.executable = previous_executable;
            state.working_directory = previous_working_directory;
            return Failure(error, "game_window_unavailable");
        }
        std::filesystem::path destination;
        if (!service.scope().ResolveRelative(relative_path, destination, error)) {
            state.pid = previous_pid;
            state.level = previous_level;
            state.executable = previous_executable;
            state.working_directory = previous_working_directory;
            return JsonValue(nullptr);
        }
        std::filesystem::path destination_relative;
        if (!service.scope().RelativeToRoot(destination, destination_relative, error) ||
            !service.scope().IsSupportedPath(destination_relative)) {
            state.pid = previous_pid;
            state.level = previous_level;
            state.executable = previous_executable;
            state.working_directory = previous_working_directory;
            error = "path_forbidden";
            return JsonValue(nullptr);
        }
        std::error_code filesystem_error;
        if (!std::filesystem::is_directory(destination.parent_path(), filesystem_error)) {
            state.pid = previous_pid;
            state.level = previous_level;
            state.executable = previous_executable;
            state.working_directory = previous_working_directory;
            return Failure(error, "path_forbidden");
        }
        if (!CapturePng(snapshot.window, destination)) {
            state.pid = previous_pid;
            state.level = previous_level;
            state.executable = previous_executable;
            state.working_directory = previous_working_directory;
            return Failure(error, "screenshot_failed");
        }
        std::error_code size_error;
        const auto captured_bytes = std::filesystem::file_size(destination, size_error);
        if (size_error) {
            state.pid = previous_pid;
            state.level = previous_level;
            state.executable = previous_executable;
            state.working_directory = previous_working_directory;
            return Failure(error, "screenshot_failed");
        }
        const DWORD capture_pid = state.pid;
        state.pid = previous_pid;
        state.level = previous_level;
        state.executable = previous_executable;
        state.working_directory = previous_working_directory;
        return JsonValue::Object{{"path", relative_path}, {"pid", static_cast<double>(capture_pid)},
                                 {"format", "png"}, {"bytes", static_cast<double>(captured_bytes)}};
    }

    return Failure(error, "unknown_tool");
}

}  // namespace mcp
