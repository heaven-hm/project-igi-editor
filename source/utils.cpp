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

struct PromptDialogState {
	std::string title;
	std::string label;
	std::string initial;
	std::string result;
	bool accepted = false;
	bool done = false;
	HWND edit = NULL;
};

static LRESULT CALLBACK PromptDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
	case WM_NCCREATE: {
		const CREATESTRUCTA* create = reinterpret_cast<const CREATESTRUCTA*>(lParam);
		SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
		return TRUE;
	}
	case WM_CREATE: {
		auto* state = reinterpret_cast<PromptDialogState*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
		if (!state) return -1;

		CreateWindowExA(0, "STATIC", state->label.c_str(),
			WS_CHILD | WS_VISIBLE,
			12, 12, 356, 20,
			hwnd, NULL, GetModuleHandleA(NULL), NULL);

		state->edit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", state->initial.c_str(),
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
			12, 36, 356, 24,
			hwnd, reinterpret_cast<HMENU>(1001), GetModuleHandleA(NULL), NULL);

		CreateWindowExA(0, "BUTTON", "OK",
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
			212, 72, 74, 26,
			hwnd, reinterpret_cast<HMENU>(IDOK), GetModuleHandleA(NULL), NULL);

		CreateWindowExA(0, "BUTTON", "Cancel",
			WS_CHILD | WS_VISIBLE | WS_TABSTOP,
			294, 72, 74, 26,
			hwnd, reinterpret_cast<HMENU>(IDCANCEL), GetModuleHandleA(NULL), NULL);

		SendMessageA(state->edit, EM_SETSEL, 0, -1);
		SetFocus(state->edit);
		return 0;
	}
	case WM_COMMAND: {
		auto* state = reinterpret_cast<PromptDialogState*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
		if (!state) break;

		const int command = LOWORD(wParam);
		if (command == IDOK) {
			char buffer[1024] = {};
			GetWindowTextA(state->edit, buffer, sizeof(buffer));
			state->result = Trim(buffer);
			state->accepted = true;
			state->done = true;
			DestroyWindow(hwnd);
			return 0;
		}
		if (command == IDCANCEL) {
			state->accepted = false;
			state->done = true;
			DestroyWindow(hwnd);
			return 0;
		}
		break;
	}
	case WM_CLOSE: {
		auto* state = reinterpret_cast<PromptDialogState*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
		if (state) {
			state->accepted = false;
			state->done = true;
		}
		DestroyWindow(hwnd);
		return 0;
	}
	}

	return DefWindowProcA(hwnd, msg, wParam, lParam);
}

std::optional<std::string> PromptForText(const std::string& title, const std::string& label, const std::string& initial) {
	static bool registered = false;
	static const char* kClassName = "IGIEditorPromptDialog";

	if (!registered) {
		WNDCLASSA wc = {};
		wc.lpfnWndProc = PromptDialogProc;
		wc.hInstance = GetModuleHandleA(NULL);
		wc.lpszClassName = kClassName;
		wc.hCursor = LoadCursor(NULL, IDC_ARROW);
		wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
		RegisterClassA(&wc);
		registered = true;
	}

	PromptDialogState state;
	state.title = title;
	state.label = label;
	state.initial = initial;

	HWND owner = GetActiveWindow();
	if (owner) EnableWindow(owner, FALSE);

	const int width = 392;
	const int height = 146;
	const int screenW = GetSystemMetrics(SM_CXSCREEN);
	const int screenH = GetSystemMetrics(SM_CYSCREEN);
	HWND hwnd = CreateWindowExA(
		WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
		kClassName,
		title.c_str(),
		WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_VISIBLE,
		(screenW - width) / 2, (screenH - height) / 2,
		width, height,
		owner, NULL, GetModuleHandleA(NULL), &state);

	if (!hwnd) {
		if (owner) EnableWindow(owner, TRUE);
		return std::nullopt;
	}

	MSG msg;
	while (!state.done && GetMessageA(&msg, NULL, 0, 0) > 0) {
		if (!IsDialogMessageA(hwnd, &msg)) {
			TranslateMessage(&msg);
			DispatchMessageA(&msg);
		}
	}

	if (owner) {
		EnableWindow(owner, TRUE);
		SetForegroundWindow(owner);
	}

	if (!state.accepted) return std::nullopt;
	return state.result;
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

std::string GetVersionString() {
	std::string exeDir = GetExeDirectory();
	std::string versionPath = exeDir + "\\version";
	std::ifstream file(versionPath);
	if (file.is_open()) {
		std::string version;
		std::getline(file, version);
		return version;
	}
	return "0.0.7"; // Fallback
}

std::string GetLevelQSCPath(int level_no) {
	std::string qeditor_path = Config::Get().qEditorPath;
	std::string qfiles_path = qeditor_path + "\\QFiles\\IGI_QSC\\missions\\location0\\level" + std::to_string(level_no);
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

	std::string appDataQEditor = Config::Get().qEditorPath;
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
		matches("ELEVATORSHAFT") ||
		matches("ELEVATORTUNNEL") ||
		matches("UNDERGROUNDROOM") ||
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

void SetClipboardText(const std::string& text) {
	if (!OpenClipboard(NULL)) return;
	EmptyClipboard();
	HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
	if (!hGlob) {
		CloseClipboard();
		return;
	}
	char* pBuf = (char*)GlobalLock(hGlob);
	if (pBuf) {
		memcpy(pBuf, text.c_str(), text.size() + 1);
		GlobalUnlock(hGlob);
		SetClipboardData(CF_TEXT, hGlob);
	}
	CloseClipboard();
}

std::string GetClipboardText() {
	if (!OpenClipboard(NULL)) return "";
	HANDLE hData = GetClipboardData(CF_TEXT);
	if (!hData) {
		CloseClipboard();
		return "";
	}
	char* pszText = static_cast<char*>(GlobalLock(hData));
	std::string text = pszText ? std::string(pszText) : "";
	GlobalUnlock(hData);
	CloseClipboard();
	return text;
}

} // namespace Utils
