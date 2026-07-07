/******************************************************************************
 * @file    utils.cpp
 * @brief   Utility functions for the IGI Editor
 *****************************************************************************/

#include "pch.h"
#include "utils.h"
#include "logger.h"
#include <windows.h>
#include <tlhelp32.h>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include "config.h"

namespace Utils {

static bool g_logEnabled = true;

static HANDLE g_processHandle = nullptr;
static DWORD g_processId = 0;
static HWND g_processWindow = NULL;
static DWORD g_processBaseAddress = 0;
static std::string g_processName = "";

template<typename T>
static std::string to_hex_string(T val) {
	std::ostringstream ss;
	ss << "0x" << std::hex << val;
	return ss.str();
}

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
template std::optional<long long> TryParse<long long>(const std::string&);
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

bool ModifiersExactMatch(const KeyBinding& kb, bool ctrlDown, bool shiftDown, bool altDown) {
	// Required modifiers must be held AND non-required modifiers must be released,
	// so e.g. Ctrl+C does not also match while Ctrl+Shift+C is pressed.
	return kb.ctrl == ctrlDown && kb.shift == shiftDown && kb.alt == altDown;
}

bool IsKeyBindingPressedExact(const KeyBinding& kb) {
	// A binding with no key and no modifiers can never be "pressed".
	if (!kb.vkCode && !kb.ctrl && !kb.shift && !kb.alt) return false;
	auto down = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
	if (kb.vkCode && !down(kb.vkCode)) return false;
	return ModifiersExactMatch(kb, down(VK_CONTROL), down(VK_SHIFT), down(VK_MENU));
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
	return "1.5.0"; // Fallback
}

std::string GetLevelQSCPath(int level_no) {
	const auto& config = Config::Get();
	if (!config.objectFilePath.empty()) {
		std::string path = config.objectFilePath;
		// Handle LOCAL: prefix if needed (for now just strip it as we assume relative to game/exe)
		if (path.find("LOCAL:") == 0) {
			path = path.substr(6);
			// In IGI, LOCAL: usually means it's relative to the game root or mission folder
			// For the editor, we'll try to resolve it relative to the game root.
			return GetIGIRootPath() + "\\" + path;
		}
		return path;
	}
	std::string exeDir = GetExeDirectory();
	std::string dir = exeDir + "\\editor\\qed\\temp";
	std::filesystem::create_directories(dir);
	return dir + "\\objects.qsc";
}

std::string GetLevelQVMPath(int level_no) {
	std::string game_path = GetIGIRootPath();
	Logger::Get().Log(LogLevel::INFO, "[Utils] GetLevelQVMPath using custom location: " + game_path);
	return game_path + "\\missions\\location0\\level" + std::to_string(level_no) + "\\objects.qvm";
}

static void CopyDirExcludingQFiles(const std::filesystem::path& source, const std::filesystem::path& destination) {
	std::filesystem::create_directories(destination);
	for (const auto& entry : std::filesystem::directory_iterator(source)) {
		const auto& path = entry.path();
		auto filename = path.filename().string();
		
		if (filename == "QFiles") {
			printf("[Utils] Skipping sync/copy of read-only QFiles backup folder.\n");
			continue;
		}
		
		auto dest_path = destination / filename;
		if (std::filesystem::is_directory(path)) {
			CopyDirExcludingQFiles(path, dest_path);
		} else {
			std::filesystem::copy_file(path, dest_path, std::filesystem::copy_options::overwrite_existing);
		}
	}
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
		matches("491_") ||
		matches("472_01_1") ||   // ANYA_HQ — underground command bunker
		matches("ANYA_HQ");
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

void TrimFileInPlace(const std::string& filepath) {
	std::ifstream inFile(filepath, std::ios::in | std::ios::binary);
	if (!inFile) return;
	std::string content((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
	inFile.close();

	std::stringstream ss(content);
	std::string line;
	std::string result = "";
	bool first = true;
	while (std::getline(ss, line)) {
		std::string trimmedLine = Trim(line);
		if (trimmedLine.empty()) continue;
		if (!first) result += "\n";
		result += trimmedLine;
		first = false;
	}

	std::ofstream outFile(filepath, std::ios::out | std::ios::binary | std::ios::trunc);
	if (outFile) {
		outFile << result;
		outFile.close();
	}
}

std::string GetIGIRootPath() {
#if defined(_WIN32)
	char* envPath = nullptr;
	size_t len = 0;
	if (_dupenv_s(&envPath, &len, "IGI_GAME_PATH") == 0 && envPath != nullptr) {
		std::string path(envPath);
		free(envPath);
		if (!path.empty()) {
			return path;
		}
	}
#else
	char* envPath = std::getenv("IGI_GAME_PATH");
	if (envPath != nullptr) {
		std::string path(envPath);
		if (!path.empty()) {
			return path;
		}
	}
#endif

	std::string path = GetExeDirectory();
	// Trim trailing slashes/spaces
	while (!path.empty() && (path.back() == '\\' || path.back() == '/' || path.back() == ' ')) {
		path.pop_back();
	}
	// If it contains \missions, strip it
	size_t pos = path.find("\\missions");
	if (pos != std::string::npos) {
		path = path.substr(0, pos);
	}
	// Trim trailing slashes/spaces again
	while (!path.empty() && (path.back() == '\\' || path.back() == '/' || path.back() == ' ')) {
		path.pop_back();
	}
	return path;
}

std::string GetIGIModelsPath(int level_no) {
	std::string root = GetIGIRootPath();
	std::string pathLoc = root + "\\missions\\location0\\level" + std::to_string(level_no) + "\\models";
	
	if (std::filesystem::exists(pathLoc)) {
		return pathLoc;
	}
	return "";
}

HANDLE FindProcess(const std::string &processName)
{
    LogInfo("FindProcess: Trying to find process: " + processName);

    auto EndsWith = [](const std::string &str, const std::string &suffix)
    {
        if (suffix.size() > str.size())
        {
            return false;
        }
        return std::equal(suffix.rbegin(), suffix.rend(), str.rbegin());
    };

    // Check if process name ends with .exe
    std::string processNameExe = processName;
    if (!EndsWith(processNameExe, ".exe"))
    {
        processNameExe += ".exe";
        LogInfo("FindProcess: Process name appended with .exe: " + processNameExe);
    }

    PROCESSENTRY32 pe;
    HANDLE hSnapshot = INVALID_HANDLE_VALUE;

    try
    {
        hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        LogInfo("FindProcess: Snapshot handle: " + to_hex_string(hSnapshot));

        if (hSnapshot == INVALID_HANDLE_VALUE)
        {
            std::string errMsg = "Failed to create process snapshot\n" + GetLastErrorAsString();
            LogError("FindProcess: " + errMsg);
            return nullptr;
        }

        pe.dwSize = sizeof(PROCESSENTRY32);

        if (!Process32First(hSnapshot, &pe))
        {
            std::string errMsg = "Failed to get first process entry\n" + GetLastErrorAsString();
            LogError("FindProcess: " + errMsg);
            CloseHandle(hSnapshot);
            return nullptr;
        }

        std::wstring processNameExeW(processNameExe.begin(), processNameExe.end());

        do
        {
            if (!lstrcmpiW(pe.szExeFile, processNameExeW.c_str()))
            {
                CloseHandle(hSnapshot);
                g_processHandle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pe.th32ProcessID);

                if (g_processHandle == NULL && pe.th32ProcessID > 0)
                {
                    BOOL isElevated = IsElevatedProcess();
                    if (!isElevated)
                    {
                        CloseHandle(g_processHandle);
                        throw std::runtime_error("Try to run this program with Admin privileges\n" + GetLastErrorAsString());
                    }
                    else
                    {
                        throw std::runtime_error("Handle could not be detected for specified process\n" + GetLastErrorAsString());
                    }
                }

                /*set current process info.*/
                g_processName = processName;
                g_processId = pe.th32ProcessID;
                g_processWindow = FindWindow(processName); // Only works if process name is same as window name.
                g_processBaseAddress = GetProcessBaseAddress();
                LogInfo("FindProcess: Process: " + processName + " Process ID: " + to_hex_string(g_processId) +
                                              " Handle: " + to_hex_string(g_processHandle) + " Window Handle: " + to_hex_string(g_processWindow) + " Base Address: " + to_hex_string(g_processBaseAddress));

                return g_processHandle;
            }
        } while (Process32Next(hSnapshot, &pe));
        // Set Last error to file not found.
        SetLastError(ERROR_FILE_NOT_FOUND);

        throw std::runtime_error("Process not found '" + processName + "'\n" + GetLastErrorAsString());
    }
    catch (const std::exception &e)
    {
        LogError("FindProcess: " + std::string(e.what()));
    }

    if (hSnapshot != INVALID_HANDLE_VALUE) {
        CloseHandle(hSnapshot);
    }
    return g_processHandle;
}

HWND FindWindow(const std::string &windowName)
{
    LogInfo("FindWindow: Trying to find window: " + windowName);
    return FindWindowA(NULL, windowName.c_str());
}

DWORD GetProcessId()
{
    LogInfo("GetProcessId: Trying to get process ID");
    try
    {
        if (g_processId != 0)
        {
            return g_processId;
        }
        else
        {
            LogInfo("GetProcessId: Error: process ID is 0");
        }
    }
    catch (const std::exception &e)
    {
        LogError("GetProcessId: Error: " + std::string(e.what()));
        return 0;
    }
    return 0;
}

HANDLE GetProcessHandle4mHWND(HWND hwnd)
{
    LogInfo("GetProcessHandle4mHWND: Trying to get process handle");
    HANDLE processHandle = nullptr;
    try
    {
        DWORD processId;
        if (!GetWindowThreadProcessId(hwnd, &processId))
        {
            throw std::runtime_error("GetWindowThreadProcessId failed\n" + GetLastErrorAsString());
        }
        processHandle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);

        if (!processHandle)
        {
            throw std::runtime_error("OpenProcess failed\n" + GetLastErrorAsString());
        }
        else
        {
            LogInfo("GetProcessHandle4mHWND: Return value: " + to_hex_string(processHandle));
            g_processHandle = processHandle;
        }
    }
    catch (const std::exception &e)
    {
        LogError("GetProcessHandle4mHWND: Error: " + std::string(e.what()));
        return nullptr;
    }
    return processHandle;
}

DWORD GetProcessID4mHWND(HWND hwnd)
{
    try {
        LogInfo("GetProcessID4mHWND: Trying to get process ID");
        DWORD processId;

        if (!GetWindowThreadProcessId(hwnd, &processId))
        {
            throw std::runtime_error("GetWindowThreadProcessId failed\n" + GetLastErrorAsString());
        }
        else
        {
            LogInfo("GetProcessID4mHWND: Return value: " + to_hex_string(processId));
            g_processId = processId;
        }
        return processId;
    }
    catch (const std::exception &e)
    {
        LogError("GetProcessID4mHWND: Error: " + std::string(e.what()));
        return 0;
    }
}

DWORD GetProcessBaseAddress()
{
    LogInfo("GetProcessBaseAddress: Trying to get process base address of process ID: " + std::to_string(g_processId));
    try
    {
        MODULEENTRY32 module;
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, g_processId);
        if (snapshot == INVALID_HANDLE_VALUE)
        {
            LogInfo("GetProcessBaseAddress: Error: snapshot is invalid\n" + GetLastErrorAsString());
            return 0;
        }
        module.dwSize = sizeof(MODULEENTRY32);
        if (!Module32First(snapshot, &module))
        {
            LogInfo("GetProcessBaseAddress: Error: module32first failed\n" + GetLastErrorAsString());
            CloseHandle(snapshot);
            return 0;
        }
        else
        {
            CloseHandle(snapshot);
            uintptr_t modBaseAddr = reinterpret_cast<uintptr_t>(module.modBaseAddr);
            g_processBaseAddress = static_cast<DWORD>(modBaseAddr);
            LogInfo("GetProcessBaseAddress: Return value: " + to_hex_string(g_processBaseAddress));
        }
        return g_processBaseAddress;
    }
    catch (const std::exception &e)
    {
        LogError("GetProcessBaseAddress: Error: " + std::string(e.what()));
        return 0;
    }
}

} // namespace Utils
