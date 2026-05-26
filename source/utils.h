/******************************************************************************
 * @file    utils.h
 * @brief   Utility functions for the IGI Editor
 *****************************************************************************/

#pragma once

#include <windows.h>
#include <string>
#include <optional>
#include <vector>
#include <algorithm>

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
std::string Trim(const std::string& str);
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

// Process and window utilities (Windows API)
HANDLE FindProcess(const std::string& processName);
HWND FindWindow(const std::string& windowName);
DWORD GetProcessId();
HANDLE GetProcessHandle4mHWND(HWND hwnd);
DWORD GetProcessID4mHWND(HWND hwnd);
DWORD GetProcessBaseAddress();

// File and path utilities
std::string GetExeDirectory();
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

