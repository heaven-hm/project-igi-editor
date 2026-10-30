/******************************************************************************
 * @file    utils.h
 * @brief   Utility functions for the IGI Editor
 *****************************************************************************/

#pragma once

// windows.h is only needed for the Windows-typed declarations below. Guarding it
// (and those declarations) is what lets pure-logic translation units such as
// level/level_objects.cpp compile in the host-native test lane
// (tools/host-tests/host_test_runner.py), which has no Windows SDK.
#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#include <string>
#include <optional>
#include <vector>
#include <algorithm>
#include <filesystem>

struct KeyBinding;

namespace Utils {

// Logging functions
void LogInfo(const std::string& message);
void LogError(const std::string& message);
void LogWarning(const std::string& message);
void SetLogEnabled(bool enabled);

// UI message box functions
void ShowError(const std::string& message, const std::string& title = "Error");
void ShowWarning(const std::string& message, const std::string& title = "Warning");
void ShowInfo(const std::string& message, const std::string& title = "Information");
std::optional<std::string> PromptForText(const std::string& title, const std::string& label, const std::string& initial = "");

// Combined log + message box functions
void LogAndShowError(const std::string& message, const std::string& title = "Error");

// Process elevation check
bool IsElevatedProcess();

// String utilities
std::string GetLastErrorAsString();
// Trim is pure logic but is called by the parsers that the host-native test lane
// compiles (level/level_objects_serialize.cpp), and utils.cpp as a whole needs the
// Windows SDK. Keeping it inline here is what lets those suites link.
inline std::string Trim(const std::string& str) {
    const auto start = str.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    const auto end = str.find_last_not_of(" \t\n\r");
    return str.substr(start, end - start + 1);
}
std::vector<std::string> Split(const std::string& str, char delimiter);

// Data type conversion utilities
template<typename T>
std::optional<T> TryParse(const std::string& str);

template<typename T>
std::string ToString(const T& value);

// Key handling functions (Windows API)
bool HotKeysDown(const std::vector<int>& keys);
bool IsKeyPressed(int keycode);
bool IsKeyToggled(int keycode);
bool IsKeyBindingPressed(const KeyBinding& kb);
// Exact match: required modifiers down AND non-required modifiers up. Use for
// discrete event dispatch so modified siblings (Ctrl+Shift+C) don't also fire
// the base binding (Ctrl+C). ModifiersExactMatch is the pure, testable core.
bool ModifiersExactMatch(const KeyBinding& kb, bool ctrlDown, bool shiftDown, bool altDown);
bool IsKeyBindingPressedExact(const KeyBinding& kb);

// Process and window utilities (Windows API)
#if defined(_WIN32)
HANDLE FindProcess(const std::string& processName);
HWND FindWindow(const std::string& windowName);
DWORD GetProcessId();
HANDLE GetProcessHandle4mHWND(HWND hwnd);
DWORD GetProcessID4mHWND(HWND hwnd);
DWORD GetProcessBaseAddress();
#endif

// File and path utilities
// Inline so the level-load path stays compilable by the host-native test lane, which
// does not build utils.cpp. On Windows this is exactly the previous implementation
// (GetModuleFileNameA + strip the last separator); elsewhere it reports the working
// directory, which is the closest portable equivalent and only affects test runs.
inline std::string GetExeDirectory() {
#if defined(_WIN32)
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string exeDir(exePath);
    const size_t lastSlash = exeDir.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        exeDir = exeDir.substr(0, lastSlash);
    }
    return exeDir;
#elif defined(__APPLE__)
    // Native macOS build.  Inside an .app the assets live in
    // <bundle>.app/Contents/Resources/editor, not next to the executable in
    // Contents/MacOS, so resolve the executable and step up to Resources.
    // Outside a bundle (build-macos/bin) the executable's own directory is
    // correct, which is what the CMake POST_BUILD step stages for.
    char raw[4096];
    uint32_t size = sizeof(raw);
    if (_NSGetExecutablePath(raw, &size) == 0) {
        std::error_code ec;
        std::filesystem::path exe =
            std::filesystem::weakly_canonical(std::filesystem::path(raw), ec);
        if (!ec) {
            std::filesystem::path dir = exe.parent_path();
            if (dir.filename() == "MacOS" &&
                dir.parent_path().filename() == "Contents") {
                return (dir.parent_path() / "Resources").string();
            }
            return dir.string();
        }
    }
    return std::filesystem::current_path().string();
#else
    return std::filesystem::current_path().string();
#endif
}

// The engine builds paths with Windows separators ("\\") throughout.  On
// POSIX those are ordinary filename characters, so convert at the OS boundary.
// Identity on Windows, so two builds cannot diverge in behaviour.
inline std::string ToNativePath(std::string path) {
#if defined(_WIN32)
    return path;
#else
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
#endif
}
std::string GetVersionString();
std::string GetLevelQSCPath(int level_no);
std::string GetLevelQVMPath(int level_no);
// Game specific utilities
bool IsUndergroundModel(const std::string& name, const std::string& modelId);

// Clipboard utilities
void SetClipboardText(const std::string& text);
std::string GetClipboardText();

// File trim utility
void TrimFileInPlace(const std::string& filepath);

// Path resolution utilities
std::string GetIGIRootPath();
std::string GetIGIModelsPath(int level_no);

} // namespace Utils

