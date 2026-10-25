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
#else
    return std::filesystem::current_path().string();
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

