/******************************************************************************
 * @file    utils.cpp
 * @brief   Utility functions for the IGI Editor
 *****************************************************************************/

#include "pch.h"
#include "utils.h"
#include "logger.h"
#include <windows.h>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include "config.h"

namespace Utils {

static bool g_logEnabled = true;

// Logging functions
void LogInfo(const std::string& message) {
	if (g_logEnabled) {
		Logger::Get().Log(LogLevel::INFO, message);
	}
}

void LogError(const std::string& message) {
	if (g_logEnabled) {
		Logger::Get().Log(LogLevel::ERR, message);
	}
}

void LogWarning(const std::string& message) {
	if (g_logEnabled) {
		Logger::Get().Log(LogLevel::WARNING, message);
	}
}

void SetLogEnabled(bool enabled) {
	g_logEnabled = enabled;
}

// UI message box functions
void ShowError(const std::string& message, const std::string& title) {
	MessageBoxA(NULL, message.c_str(), title.c_str(), MB_OK | MB_ICONERROR);
}

void ShowWarning(const std::string& message, const std::string& title) {
	MessageBoxA(NULL, message.c_str(), title.c_str(), MB_OK | MB_ICONWARNING);
}

void ShowInfo(const std::string& message, const std::string& title) {
	MessageBoxA(NULL, message.c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION);
}

// Combined log + message box functions
void LogAndShowError(const std::string& message, const std::string& title) {
	LogError(message);
	ShowError(message, title);
}

// Process elevation check
bool IsElevatedProcess() {
	bool isElevated = false;
	HANDLE token = NULL;
	if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
		TOKEN_ELEVATION elevation;
		DWORD token_sz = sizeof(TOKEN_ELEVATION);
		if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &token_sz)) {
			isElevated = elevation.TokenIsElevated;
		}
	}
	if (token) {
		CloseHandle(token);
	}
	return isElevated;
}

// String utilities
std::string GetLastErrorAsString() {
	DWORD error = GetLastError();
	if (error == 0)
		return "No error";

	std::ostringstream ss;
	ss << "Error code: " << error << " - " << std::system_category().message(error);
	return ss.str();
}

std::string Trim(const std::string& str) {
	auto start = str.find_first_not_of(" \t\n\r");
	if (start == std::string::npos) return "";
	auto end = str.find_last_not_of(" \t\n\r");
	return str.substr(start, end - start + 1);
}

std::vector<std::string> Split(const std::string& str, char delimiter) {
	std::vector<std::string> tokens;
	std::stringstream ss(str);
	std::string token;
	while (std::getline(ss, token, delimiter)) {
		tokens.push_back(token);
	}
	return tokens;
}

// Template implementations
template<typename T>
std::optional<T> TryParse(const std::string& str) {
	std::stringstream ss(str);
	T value;
	if (ss >> value) {
		if (ss.eof()) {
			return value;
		}
	}
	return {};
}

template<typename T>
std::string ToString(const T& value) {
	if constexpr (std::is_arithmetic_v<T>) {
		return std::to_string(value);
	}
	else if constexpr (std::is_same_v<T, std::string>) {
		return value;
	}
	else {
		throw std::invalid_argument("Unsupported data type for ToString.");
	}
}

// Explicit template instantiations
template std::optional<int> TryParse<int>(const std::string&);
template std::optional<float> TryParse<float>(const std::string&);
template std::optional<double> TryParse<double>(const std::string&);
template std::string ToString<int>(const int&);
template std::string ToString<float>(const float&);
template std::string ToString<double>(const double&);

// Key handling functions (Windows API)
bool HotKeysDown(const std::vector<int>& keys) {
	if (keys.empty()) return false;

	short result = GetAsyncKeyState(keys[0]);
	for (const auto& key : keys) {
		result &= GetAsyncKeyState(key);
	}
	return ((result & 0x8000) != 0);
}

bool IsKeyPressed(int keycode) {
	try {
		return (GetAsyncKeyState(keycode) & 0x8000);
	}
	catch (const std::exception& e) {
		LogError("IsKeyPressed: " + std::string(e.what()));
		return false;
	}
}

bool IsKeyToggled(int keycode) {
	try {
		return (GetAsyncKeyState(keycode) & 0x1);
	}
	catch (const std::exception& e) {
		LogError("IsKeyToggled: " + std::string(e.what()));
		return false;
	}
}

bool IsKeyBindingPressed(const KeyBinding& kb) {
	std::vector<int> keys;
	if (kb.ctrl) keys.push_back(VK_CONTROL);
	if (kb.shift) keys.push_back(VK_SHIFT);
	if (kb.alt) keys.push_back(VK_MENU);
	if (kb.vkCode) keys.push_back(kb.vkCode);
	return HotKeysDown(keys);
}

std::string GetExeDirectory() {
	char exePath[MAX_PATH];
	GetModuleFileNameA(NULL, exePath, MAX_PATH);
	std::string exeDir(exePath);
	size_t lastSlash = exeDir.find_last_of("\\/");
	if (lastSlash != std::string::npos) {
		exeDir = exeDir.substr(0, lastSlash);
	}
	return exeDir;
}

std::string GetLevelQSCPath(int level_no) {
	char appDataPath[MAX_PATH];
	GetEnvironmentVariableA("APPDATA", appDataPath, MAX_PATH);
	std::string qfiles_path = std::string(appDataPath) + "\\QEditor\\QFiles\\IGI_QSC\\missions\\location0\\level" + std::to_string(level_no);
	return qfiles_path + "\\objects.qsc";
}

std::string GetLevelQVMPath(int level_no) {
	// Read from config in exe directory
	std::string exeDir = GetExeDirectory();
	std::string configPath = exeDir + "\\config.ini";
	char igiPath[MAX_PATH];
	GetPrivateProfileStringA("GamePath", "IGIPath", "D:\\IGI1", igiPath, MAX_PATH, configPath.c_str());
	std::string game_path = std::string(igiPath);
	Logger::Get().Log(LogLevel::INFO, "[Utils] GetLevelQVMPath using IGIPath: " + game_path + " (from config: " + configPath + ")");
	return game_path + "\\missions\\location0\\level" + std::to_string(level_no) + "\\objects.qvm";
}

bool ValidateAndSetupQEditor() {
	namespace fs = std::filesystem;

	char appData[1024];
	GetEnvironmentVariableA("APPDATA", appData, 1024);
	std::string appDataQEditor = std::string(appData) + "\\QEditor";
	std::string exeDir = GetExeDirectory();
	std::string exeQEditor = exeDir + "\\QEditor";

	printf("[Utils] Validating QEditor folder structure...\n");
	Logger::Get().Log(LogLevel::INFO, "[Utils] Validating QEditor folder structure...");
	printf("[Utils] AppData QEditor: %s\n", appDataQEditor.c_str());
	printf("[Utils] Exe QEditor: %s\n", exeQEditor.c_str());

	// Check if QEditor exists in AppData
	bool appDataExists = fs::exists(appDataQEditor);
	bool exeExists = fs::exists(exeQEditor);

	if (appDataExists) {
		// QEditor exists in AppData - use it (exe directory is optional)
		printf("[Utils] QEditor found in AppData, using it\n");
		Logger::Get().Log(LogLevel::INFO, "[Utils] QEditor found in AppData, using it");

		// If exe QEditor also exists, sync it to AppData
		if (exeExists) {
			printf("[Utils] QEditor also exists in exe directory, performing sync...\n");
			Logger::Get().Log(LogLevel::INFO, "[Utils] QEditor also exists in exe directory, performing sync...");
			try {
				fs::copy(exeQEditor, appDataQEditor,
					fs::copy_options::recursive | fs::copy_options::overwrite_existing);
				printf("[Utils] Successfully synced QEditor from exe to AppData\n");
				Logger::Get().Log(LogLevel::INFO, "[Utils] Successfully synced QEditor from exe to AppData");
			}
			catch (const std::exception& e) {
				// Sync failed but AppData QEditor is still usable, just log a warning
				std::string warningMsg = "WARNING: Failed to sync QEditor from exe to AppData (using existing AppData version): " + std::string(e.what());
				printf("[Utils] %s\n", warningMsg.c_str());
				Logger::Get().Log(LogLevel::WARNING, warningMsg);
			}
		}
		return true;
	}

	// QEditor not in AppData, check exe directory
	if (exeExists) {
		// Copy from exe to AppData
		printf("[Utils] QEditor not found in AppData, copying from exe directory...\n");
		Logger::Get().Log(LogLevel::INFO, "[Utils] QEditor not found in AppData, copying from exe directory...");

		try {
			fs::create_directories(appDataQEditor);
			fs::copy(exeQEditor, appDataQEditor,
				fs::copy_options::recursive | fs::copy_options::overwrite_existing);
			printf("[Utils] Successfully copied QEditor to AppData\n");
			Logger::Get().Log(LogLevel::INFO, "[Utils] Successfully copied QEditor to AppData");
		}
		catch (const std::exception& e) {
			std::string errorMsg = "FATAL ERROR: Failed to copy QEditor from exe to AppData:\n" + std::string(e.what());
			ShowError(errorMsg, "IGI Editor - Fatal Error");
			printf("[Utils] %s\n", errorMsg.c_str());
			Log(log_type_t::LOG_FATAL, __FILE__, __LINE__,
				"FATAL ERROR: Failed to copy QEditor from exe to AppData: %s", e.what());
			return false;
		}
		return true;
	}

	// QEditor missing from both locations
	std::string errorMsg = "INSTALL ERROR: QEditor not found in AppData or exe directory!\n\n"
		"AppData: " + appDataQEditor + "\n"
		"Exe Directory: " + exeQEditor + "\n\n"
		"Please ensure QEditor is properly installed in either location.";
	LogAndShowError(errorMsg, "IGI Editor - Installation Error");
	printf("[Utils] %s\n", errorMsg.c_str());
	Log(log_type_t::LOG_FATAL, __FILE__, __LINE__, errorMsg.c_str());
	return false;
}

bool IsUndergroundModel(const std::string& name, const std::string& modelId) {
	auto prepare = [](std::string s) {
		std::transform(s.begin(), s.end(), s.begin(), ::toupper);
		// Basic trim
		s.erase(0, s.find_first_not_of(" \t\r\n"));
		if (s.empty()) return s;
		s.erase(s.find_last_not_of(" \t\r\n") + 1);
		return s;
	};
	std::string n = prepare(name);
	std::string m = prepare(modelId);

	auto matches = [&](const std::string& pattern) {
		return n.find(pattern) != std::string::npos || m.find(pattern) != std::string::npos;
	};

	return matches("UNDERGROUND") ||
		matches("TUNNEL") ||
		matches("T-JUNCTION") ||
		matches("METALDOORBASE") ||
		matches("METAL_DOOR_BASE") ||
		matches("ELEVATORROOM") ||
		matches("GUARDROOM") ||
		matches("STRAIGHTUPWARDS") ||
		matches("JOINT_FIXER") ||
		matches("JOINTFIXER") ||
		matches("JOINT") ||
		matches("FIXER") ||
		matches("JNT") ||
		matches("FIX") ||
		matches("471_") ||
		matches("491_");
}

} // namespace Utils
