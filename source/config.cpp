#include "pch.h"
#include "config.h"
#include "logger.h"
#include "utils.h"
#include "parsers/qsc_lexer.h"
#include "parsers/qsc_parser.h"
#include "parsers/qvm_compiler.h"
#include "parsers/qvm_parser.h"
#include "parsers/qvm_decompiler.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <filesystem>
#include <algorithm>

ConfigData Config::data_;

static std::string Trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static KeyBinding ParseKeyBinding(const std::string& binding) {
    KeyBinding kb = {0, false, false, false};

    std::string upper = binding;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

    // Treat <CONTROL> as an alias for <CTRL>
    {
        size_t pos = upper.find("CONTROL");
        while (pos != std::string::npos) {
            upper.replace(pos, 7, "CTRL");
            pos = upper.find("CONTROL", pos + 4);
        }
    }

    if (upper.find("CTRL") != std::string::npos) kb.ctrl = true;
    if (upper.find("SHIFT") != std::string::npos) kb.shift = true;
    if (upper.find("ALT") != std::string::npos) kb.alt = true;

    size_t lastPlus = upper.rfind('+');
    std::string keyPart = (lastPlus != std::string::npos) ? upper.substr(lastPlus + 1) : upper;

    if (keyPart == "F1") kb.vkCode = VK_F1;
    else if (keyPart == "F2") kb.vkCode = VK_F2;
    else if (keyPart == "F3") kb.vkCode = VK_F3;
    else if (keyPart == "F4") kb.vkCode = VK_F4;
    else if (keyPart == "F5") kb.vkCode = VK_F5;
    else if (keyPart == "F6") kb.vkCode = VK_F6;
    else if (keyPart == "F7") kb.vkCode = VK_F7;
    else if (keyPart == "F8") kb.vkCode = VK_F8;
    else if (keyPart == "F9") kb.vkCode = VK_F9;
    else if (keyPart == "F10") kb.vkCode = VK_F10;
    else if (keyPart == "F11") kb.vkCode = VK_F11;
    else if (keyPart == "F12") kb.vkCode = VK_F12;
    else if (keyPart == "INSERT") kb.vkCode = VK_INSERT;
    else if (keyPart == "DELETE") kb.vkCode = VK_DELETE;
    else if (keyPart == "HOME") kb.vkCode = VK_HOME;
    else if (keyPart == "SPACE") kb.vkCode = VK_SPACE;
    else if (keyPart == "PAGEUP")   kb.vkCode = VK_PRIOR;
    else if (keyPart == "PAGEDOWN") kb.vkCode = VK_NEXT;
    else if (keyPart == "LEFT")     kb.vkCode = VK_LEFT;
    else if (keyPart == "RIGHT")    kb.vkCode = VK_RIGHT;
    else if (keyPart == "UP")       kb.vkCode = VK_UP;
    else if (keyPart == "DOWN")     kb.vkCode = VK_DOWN;
    else if (keyPart == "DECIMAL")  kb.vkCode = VK_DECIMAL;
    else if (keyPart == "DIVIDE")   kb.vkCode = VK_DIVIDE;
    else if (keyPart == "MULTIPLY") kb.vkCode = VK_MULTIPLY;
    else if (keyPart == "PLUS" || keyPart == "ADD")      kb.vkCode = VK_OEM_PLUS;
    else if (keyPart == "MINUS" || keyPart == "SUBTRACT") kb.vkCode = VK_OEM_MINUS;
    else if (keyPart == "LEFTMOUSEBUTTON") kb.vkCode = VK_LBUTTON;
    else if (keyPart.length() == 1) {
        kb.vkCode = VkKeyScanA(keyPart[0]) & 0xFF;
    }

    return kb;
}

std::string Config::GetConfigPath() {
    return Utils::GetExeDirectory() + "\\content\\qed\\qedconfig.qsc";
}

std::string Config::GetKeybindingsPath() {
    return Utils::GetExeDirectory() + "\\content\\qed\\qedkeybindings.qsc";
}

void Config::CreateDefault() {
    data_.level = 1;
    data_.keySnapGround = 'S';
    data_.keySnapObject = 'O';
    data_.keyRotateAlpha = 'A';
    data_.keyRotateBeta = 'B';
    data_.keyRotateGamma = 'G';
    data_.keyResetOri = ' ';
    data_.keyResetPos = 'P';
    data_.teleportToMarker = 0;
    data_.resetMarkerToPlayer = 'H';
    data_.keyMoveForward = 101;
    data_.keyMoveBackward = 103;
    data_.keyMoveLeft = 100;
    data_.keyMoveRight = 102;
    data_.fontSize = 12.0f;
    data_.fontColorR = 255;
    data_.fontColorG = 255;
    data_.fontColorB = 255;
    data_.keySave = {0x53, true, false, false};    
    data_.keyResetLevel = {0x52, true, false, false}; 
    data_.keyDebug = {0x44, true, false, false};   
    data_.keyQuit = {0x51, true, false, false};    
    data_.keyResetScript = {0x52, false, true, false}; 
    data_.keyClipMode = {VK_F9, false, false, false};
    data_.keyToggleGame = {VK_F3, false, false, false};
    data_.keySaveState = {0x57, true, false, false};           // Ctrl+W
    data_.keyToggleSaveStateOnExit = {0x41, true, false, false}; // Ctrl+A
    data_.keyDeleteTask = {VK_DELETE, false, false, false};
    data_.keyUndo = {0x5A, true, false, false};    // Ctrl+Z
    data_.keyRedo = {0x59, true, false, false};    // Ctrl+Y
    data_.enableLogging = true;
    data_.debugLogging = false;
    data_.enableLOD = true;
    data_.consoleAutoActivate = 2;
    data_.searchType = 133577004;
    data_.invertMouse = false;
    data_.displayTaskNote = true;
    data_.allowDynamicSwitching = false;
    data_.saveConfigOnExit = true;
    data_.runEvent = true;
    data_.cameraLock = false;
    data_.enableBackup = false;
    data_.useEditorFont  = false;
    data_.systemFontSize = 12;
    data_.findTaskName = "";
    data_.findTaskNote = "";
    data_.findTaskID = "";
    data_.findTaskText = "";
    data_.taskFileName = "";
    data_.objectFilePath = "";
    data_.interpolation = 0;
    data_.renderZNear = 2.8672f;
    data_.cameraOriX = data_.cameraOriY = data_.cameraOriZ = 0.0f;
    data_.cameraRadiusX = data_.cameraRadiusY = 0.0f;
    data_.cameraPosX = data_.cameraPosY = data_.cameraPosZ = 0.0f;
    data_.cameraMatX = data_.cameraMatY = data_.cameraMatZ = 0.0f;
}

void Config::Init() {
    CreateDefault();
    std::string contentDir = Utils::GetExeDirectory() + "\\content";
    if (!std::filesystem::exists(contentDir)) {
        Logger::Get().Log(LogLevel::FATAL, "FATAL: content directory not found: " + contentDir);
        Utils::ShowError("ERROR: FATAL\ncontent directory not found:\n" + contentDir + "\nEditor will now exit.", "IGI Editor - Launch Error");
        std::exit(1);
    }
    std::string qedDir = contentDir + "\\qed";

    // Compile all .qsc files to .qvm
    for (const auto& entry : std::filesystem::directory_iterator(qedDir)) {
        if (entry.path().extension() == ".qsc") {
            std::string qscPath = entry.path().string();
            std::string qvmPath = entry.path().parent_path().string() + "\\" + entry.path().stem().string() + ".qvm";
            std::ifstream qscIn(qscPath);
            std::string qscSrc((std::istreambuf_iterator<char>(qscIn)), std::istreambuf_iterator<char>());
            qscIn.close();
            auto lexResult = qsc::Lex(qscSrc);
            if (lexResult.ok) {
                auto parseResult = qsc::Parse(lexResult.tokens);
                if (parseResult.ok) {
                    std::string compileErr;
                    qvm::CompileToFile(*parseResult.program, qvmPath, &compileErr);
                }
            }
        }
    }
    Load();
}

void Config::Load() {
    std::string qedDir = Utils::GetExeDirectory() + "\\content\\qed";
    if (!std::filesystem::exists(qedDir)) return;

    auto ParseLine = [&](const std::string& line) {
        std::string clean = Trim(line);
        if (clean.empty() || clean.find("//") == 0) return;

        size_t openParen = clean.find('(');
        size_t closeParen = clean.rfind(')');
        if (openParen != std::string::npos && closeParen != std::string::npos && closeParen > openParen) {
            std::string key = Trim(clean.substr(0, openParen));
            std::string argsStr = clean.substr(openParen + 1, closeParen - openParen - 1);
            auto args = Utils::Split(argsStr, ',');
            for (auto& a : args) {
                a = Trim(a);
                if (!a.empty() && a.front() == '"' && a.back() == '"') a = a.substr(1, a.size() - 2);
            }
            if (key.find("QED") == 0) key = key.substr(3);
            if (args.size() >= 1) {
                std::string val = args[0];
                if (key == "Level") data_.level = std::stoi(val);
                else if (key == "FontSize") data_.fontSize = std::stof(val);
                else if (key == "FontColorR") data_.fontColorR = std::stoi(val);
                else if (key == "FontColorG") data_.fontColorG = std::stoi(val);
                else if (key == "FontColorB") data_.fontColorB = std::stoi(val);
                else if (key == "RenderZNear") {
                    data_.renderZNear = std::stof(val);
                    RENDER_Z_NEAR = data_.renderZNear;
                    WORLD_Z_NEAR = RENDER_Z_NEAR / 0.001f;
                }
                else if (key == "Logs" || key == "Enable" || key == "SaveConfigOnExit") data_.enableLogging = (val == "TRUE" || val == "true" || val == "1");
                else if (key == "Debug") data_.debugLogging = (val == "TRUE" || val == "true" || val == "1");
                else if (key == "Lod") data_.enableLOD = (val == "TRUE" || val == "true" || val == "1");
                else if (key == "ConsoleAutoActivate") data_.consoleAutoActivate = std::stoi(val);
                else if (key == "SearchType") data_.searchType = std::stoll(val);
                else if (key == "InvertMouse") data_.invertMouse = (val == "TRUE" || val == "true" || val == "1");
                else if (key == "DisplayTaskNote") data_.displayTaskNote = (val == "TRUE" || val == "true" || val == "1");
                else if (key == "AllowDynamicSwitching") data_.allowDynamicSwitching = (val == "TRUE" || val == "true" || val == "1");
                else if (key == "RunEvent") data_.runEvent = (val == "TRUE" || val == "true" || val == "1");
                else if (key == "CameraLock") data_.cameraLock = (val == "TRUE" || val == "true" || val == "1");
                else if (key == "Backup") data_.enableBackup = (val == "TRUE" || val == "true" || val == "1");
                else if (key == "UseEditorFont") data_.useEditorFont = (val == "TRUE" || val == "true" || val == "1");
                else if (key == "SystemFontSize") { int s = std::stoi(val); data_.systemFontSize = std::max(8, std::min(32, s)); }
                else if (key == "FindTaskName") data_.findTaskName = val;
                else if (key == "FindTaskNote") data_.findTaskNote = val;
                else if (key == "FindTaskID") data_.findTaskID = val;
                else if (key == "FindTaskText") data_.findTaskText = val;
                else if (key == "TaskFileName") data_.taskFileName = val;
                else if (key == "Interpolation") data_.interpolation = std::stoi(val);
                else if (key == "SetObjectFile") data_.objectFilePath = val;
            }
            if (key == "SetCameraOrientation" && args.size() >= 3) {
                data_.cameraOriX = std::stof(args[0]);
                data_.cameraOriY = std::stof(args[1]);
                data_.cameraOriZ = std::stof(args[2]);
            } else if (key == "SetCameraRadius" && args.size() >= 2) {
                data_.cameraRadiusX = std::stof(args[0]);
                data_.cameraRadiusY = std::stof(args[1]);
            } else if (key == "SetCameraPosition" && args.size() >= 3) {
                data_.cameraPosX = std::stof(args[0]);
                data_.cameraPosY = std::stof(args[1]);
                data_.cameraPosZ = std::stof(args[2]);
            } else if (key == "SetCameraMatrix" && args.size() >= 3) {
                data_.cameraMatX = std::stof(args[0]);
                data_.cameraMatY = std::stof(args[1]);
                data_.cameraMatZ = std::stof(args[2]);
            }
            if ((key == "SetEventBinding" || key == "QEDSetEventBinding") && args.size() >= 2) {
                std::string eventName = args[0];
                std::string binding = args[1];
                std::replace(binding.begin(), binding.end(), '<', ' ');
                std::replace(binding.begin(), binding.end(), '>', '+');
                if (!binding.empty() && binding.back() == '+') binding.pop_back();
                binding.erase(std::remove(binding.begin(), binding.end(), ' '), binding.end());
                KeyBinding parsed = ParseKeyBinding(binding);
                // Route to named fields (keep existing behaviour)
                if (eventName == "SaveObjectFile") data_.keySave = parsed;
                else if (eventName == "ReloadSettings") data_.keyReloadSettings = parsed;
                else if (eventName == "Undo") data_.keyUndo = parsed;
                else if (eventName == "Redo") data_.keyRedo = parsed;
                else if (eventName == "CameraEnable") data_.keyEnableCamera = parsed;
                else if (eventName == "CameraMoveForward") data_.keyMoveCameraForward = parsed;
                else if (eventName == "CameraMoveBackward") data_.keyMoveCameraBackward = parsed;
                else if (eventName == "CameraAdjustRadius") data_.keyAdjustCameraRadius = parsed;
                else if (eventName == "CameraLookDown") data_.keyLookDown = parsed;
                else if (eventName == "CameraSnapToObject") data_.keySnapToObject = parsed;
                else if (eventName == "CameraSnapToGround") data_.keySnapToGround = parsed;
                else if (eventName == "ToggleDisplay") data_.keyClipMode = parsed;
                else if (eventName == "ToggleGame")    data_.keyToggleGame = parsed;
                else if (eventName == "SaveState")     data_.keySaveState = parsed;
                else if (eventName == "ToggleSaveStateOnExit") data_.keyToggleSaveStateOnExit = parsed;
                else if (eventName == "TaskNew") data_.keyCreateNewTask = parsed;
                else if (eventName == "TaskCopy") data_.keyCopyTask = parsed;
                else if (eventName == "TaskPaste") data_.keyPasteTask = parsed;
                else if (eventName == "DeleteTask") data_.keyDeleteTask = parsed;
                else if (eventName == "TaskSetID") data_.keyAssignTaskID = parsed;
                else if (eventName == "AnimTaskStartRecording") data_.keyStartRecording = parsed;
                else if (eventName == "AnimTaskGoToCursor") data_.keyGoToCursor = parsed;
                else if (eventName == "AnimTaskToggleSyncPlayback") data_.keySyncPlayback = parsed;
                else if (eventName == "ToggleConsole") data_.keyDebug = parsed;
                else if (eventName == "TaskMagicObjToggle") data_.keyToggleMagicObj = parsed;
                // Store every parsed binding into the event map
                data_.eventBindings_[eventName] = parsed;
            }
            return;
        }

        auto eqPos = clean.find('=');
        if (eqPos != std::string::npos) {
            std::string key = Trim(clean.substr(0, eqPos));
            std::string val = Trim(clean.substr(eqPos + 1));
            if (!val.empty() && val.back() == ';') val.pop_back();
            if (!val.empty() && val.front() == '"' && val.back() == '"') val = val.substr(1, val.size() - 2);
            if (key.find("QED") == 0) key = key.substr(3);
            if (key == "Level") data_.level = std::stoi(val);
            else if (key == "FontSize") data_.fontSize = std::stof(val);
            else if (key == "FontColorR") data_.fontColorR = std::stoi(val);
            else if (key == "FontColorG") data_.fontColorG = std::stoi(val);
            else if (key == "FontColorB") data_.fontColorB = std::stoi(val);
            else if (key == "RenderZNear") {
                data_.renderZNear = std::stof(val);
                RENDER_Z_NEAR = data_.renderZNear;
                WORLD_Z_NEAR = RENDER_Z_NEAR / 0.001f;
            }
            else if (key == "Logs" || key == "Enable" || key == "SaveConfigOnExit") data_.enableLogging = (val == "TRUE" || val == "true" || val == "1");
            else if (key == "Debug") data_.debugLogging = (val == "TRUE" || val == "true" || val == "1");
            else if (key == "ConsoleAutoActivate") data_.consoleAutoActivate = std::stoi(val);
            else if (key == "SearchType") data_.searchType = std::stoll(val);
            else if (key == "InvertMouse") data_.invertMouse = (val == "TRUE" || val == "true" || val == "1");
            else if (key == "DisplayTaskNote") data_.displayTaskNote = (val == "TRUE" || val == "true" || val == "1");
            else if (key == "AllowDynamicSwitching") data_.allowDynamicSwitching = (val == "TRUE" || val == "true" || val == "1");
            else if (key == "RunEvent") data_.runEvent = (val == "TRUE" || val == "true" || val == "1");
            else if (key == "CameraLock") data_.cameraLock = (val == "TRUE" || val == "true" || val == "1");
            else if (key == "Backup") data_.enableBackup = (val == "TRUE" || val == "true" || val == "1");
            else if (key == "FindTaskName") data_.findTaskName = val;
            else if (key == "FindTaskNote") data_.findTaskNote = val;
            else if (key == "FindTaskID") data_.findTaskID = val;
            else if (key == "FindTaskText") data_.findTaskText = val;
            else if (key == "TaskFileName") data_.taskFileName = val;
            else if (key == "Interpolation") data_.interpolation = std::stoi(val);
        }
    };

    for (const auto& entry : std::filesystem::directory_iterator(qedDir)) {
        if (entry.path().extension() == ".qvm") {
            QVMFile qvm = QVM_Parse(entry.path().string());
            if (qvm.valid) {
                std::string decompiled = QVM_DecompileToString(qvm);
                std::stringstream ss(decompiled);
                std::string line;
                while (std::getline(ss, line)) ParseLine(line);
                Logger::Get().Log(LogLevel::INFO, "[Config] Loaded from memory-decompiled: " + entry.path().filename().string());
            }
        }
    }

    // Event bindings are authoritative from the human-readable keybindings file.
    // Parse it last so it overrides any stale bindings from a compiled .qvm and so
    // the full set (TaskMagicObjToggle, AutoComplete*, SaveSubTask*, ...) is loaded.
    {
        std::ifstream kbFile(GetKeybindingsPath());
        if (kbFile.is_open()) {
            std::string line;
            int n = 0;
            while (std::getline(kbFile, line)) { ParseLine(line); ++n; }
            Logger::Get().Log(LogLevel::INFO, "[Config] Loaded event bindings from qedkeybindings.qsc (" + std::to_string(n) + " lines)");
        } else {
            Logger::Get().Log(LogLevel::WARNING, "[Config] qedkeybindings.qsc not found at: " + GetKeybindingsPath());
        }
    }
}

void Config::Save() {
    std::string qscPath = GetConfigPath();
    std::ofstream file(qscPath);
    if (file.is_open()) {
        file << "// QSC FORMAT\n";
        file << "QEDLevel(" << data_.level << ");\n";
        file << "QEDFontSize(" << data_.fontSize << ");\n";
        file << "QEDFontColorR(" << data_.fontColorR << ");\n";
        file << "QEDFontColorG(" << data_.fontColorG << ");\n";
        file << "QEDFontColorB(" << data_.fontColorB << ");\n";
        file << "QEDLogs(" << (data_.enableLogging ? "TRUE" : "FALSE") << ");\n";
        file << "QEDDebug(" << (data_.debugLogging ? "TRUE" : "FALSE") << ");\n";
        file << "QEDConsoleAutoActivate(" << data_.consoleAutoActivate << ");\n";
        file << "QEDSearchType(" << data_.searchType << ");\n";
        file << "QEDInvertMouse(" << (data_.invertMouse ? "TRUE" : "FALSE") << ");\n";
        file << "QEDDisplayTaskNote(" << (data_.displayTaskNote ? "TRUE" : "FALSE") << ");\n";
        file << "QEDAllowDynamicSwitching(" << (data_.allowDynamicSwitching ? "TRUE" : "FALSE") << ");\n";
        file << "QEDSaveConfigOnExit(" << (data_.saveConfigOnExit ? "TRUE" : "FALSE") << ");\n";
        file << "QEDRunEvent(" << (data_.runEvent ? "TRUE" : "FALSE") << ");\n";
        file << "QEDCameraLock(" << (data_.cameraLock ? "TRUE" : "FALSE") << ");\n";
        file << "QEDBackup(" << (data_.enableBackup ? "TRUE" : "FALSE") << ");\n";
        file << "QEDUseEditorFont(" << (data_.useEditorFont ? "TRUE" : "FALSE") << ");\n";
        file << "QEDSystemFontSize(" << data_.systemFontSize << ");\n";
        file << "QEDFindTaskName(\"" << data_.findTaskName << "\");\n";
        file << "QEDFindTaskNote(\"" << data_.findTaskNote << "\");\n";
        file << "QEDFindTaskID(\"" << data_.findTaskID << "\");\n";
        file << "QEDFindTaskText(\"" << data_.findTaskText << "\");\n";
        file << "QEDTaskFileName(\"" << data_.taskFileName << "\");\n";
        file << "QEDSetObjectFile(\"" << data_.objectFilePath << "\");\n";
        file << "QEDInterpolation(" << data_.interpolation << ");\n";
        file << "QEDRenderZNear(" << data_.renderZNear << ");\n";
        file << "QEDSetCameraOrientation(" << data_.cameraOriX << ", " << data_.cameraOriY << ", " << data_.cameraOriZ << ");\n";
        file << "QEDSetCameraRadius(" << data_.cameraRadiusX << ", " << data_.cameraRadiusY << ");\n";
        file << "QEDSetCameraPosition(" << data_.cameraPosX << ", " << data_.cameraPosY << ", " << data_.cameraPosZ << ");\n";
        file << "QEDSetCameraMatrix(" << data_.cameraMatX << ", " << data_.cameraMatY << ", " << data_.cameraMatZ << ");\n";
        file.close();
    }
    // NOTE: qedkeybindings.qsc is a user-authored source of truth and is deliberately
    // NOT rewritten here. A previous version regenerated it from a hardcoded subset of
    // named bindings, which silently dropped TaskMagicObjToggle, AutoComplete*,
    // SaveSubTask*, TaskMove*, find variants, etc. — leaving most hotkeys unbound on the
    // next launch. The editor now only reads that file (see Config::Init).
    for (const auto& path : { qscPath }) {
        std::string qvmPath = std::filesystem::path(path).parent_path().string() + "\\" + std::filesystem::path(path).stem().string() + ".qvm";
        std::ifstream qscIn(path);
        std::string qscSrc((std::istreambuf_iterator<char>(qscIn)), std::istreambuf_iterator<char>());
        qscIn.close();
        auto lexResult = qsc::Lex(qscSrc);
        if (lexResult.ok) {
            auto parseResult = qsc::Parse(lexResult.tokens);
            if (parseResult.ok) {
                std::string compileErr;
                qvm::CompileToFile(*parseResult.program, qvmPath, &compileErr);
            }
        }
    }
}

ConfigData& Config::Get() { return data_; }
