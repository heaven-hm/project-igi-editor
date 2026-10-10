#include "config_qvm.h"
#include "../level/qvm_parser.h"
#include "../utils.h"
#include "../logger.h"
#include <filesystem>
#include <algorithm>

namespace igi {

static unsigned char KeyNameToAscii(const std::string& keyName) {
    if (keyName.empty()) return 0;

    std::string k = keyName;
    std::transform(k.begin(), k.end(), k.begin(), [](unsigned char c) { return (unsigned char)std::toupper(c); });

    if (k == "KEY_W") return 'W';
    if (k == "KEY_A") return 'A';
    if (k == "KEY_S") return 'S';
    if (k == "KEY_D") return 'D';
    if (k == "KEY_E") return 'E';
    if (k == "KEY_R") return 'R';
    if (k == "KEY_C") return 'C';
    if (k == "KEY_B") return 'B';
    if (k == "KEY_M") return 'M';
    if (k == "KEY_O") return 'O';
    if (k == "KEY_Q") return 'Q';
    if (k == "KEY_F") return 'F';
    if (k == "KEY_SPACE") return ' ';
    if (k == "KEY_RETURN") return '\r';
    if (k == "KEY_TAB") return '\t';
    if (k == "KEY_BACKSPACE") return 8;
    if (k == "KEY_ESCAPE") return 27;
    if (k == "KEY_LEFT_SHIFT" || k == "KEY_RIGHT_SHIFT") return 16; // Shift
    if (k == "KEY_LEFT_CTRL" || k == "KEY_RIGHT_CTRL") return 17; // Ctrl
    if (k == "KEY_1") return '1';
    if (k == "KEY_2") return '2';
    if (k == "KEY_3") return '3';
    if (k == "KEY_4") return '4';
    if (k == "KEY_5") return '5';
    if (k == "KEY_6") return '6';
    if (k == "KEY_7") return '7';
    if (k == "KEY_8") return '8';
    if (k == "KEY_9") return '9';
    if (k == "KEY_0") return '0';

    if (k.rfind("KEY_", 0) == 0 && k.length() == 5) {
        return (unsigned char)k[4];
    }
    return 0;
}

static int MouseButtonNameToIndex(const std::string& mouseName) {
    std::string m = mouseName;
    std::transform(m.begin(), m.end(), m.begin(), [](unsigned char c) { return (unsigned char)std::toupper(c); });
    if (m == "MOUSE_BUTTON_1") return 0; // Left
    if (m == "MOUSE_BUTTON_2") return 1; // Right
    if (m == "MOUSE_BUTTON_3") return 2; // Middle
    return 0;
}

void ProfileConfig::SetDefaultBindings() {
    key_bindings.clear();
    mouse_bindings.clear();

    key_bindings["MoveUp"] = 'W';
    key_bindings["MoveDown"] = 'S';
    key_bindings["MoveLeft"] = 'A';
    key_bindings["MoveRight"] = 'D';
    key_bindings["Jump"] = ' ';
    // OpenIGI's vanilla profile binds crouch to Right Ctrl so C remains the
    // grounded map-computer action.
    key_bindings["Crouch"] = 17;
    key_bindings["Reload"] = 'R';
    key_bindings["Activate"] = 'E';
    key_bindings["Binoculars"] = 'B';
    key_bindings["MapComputer"] = 'C';
    key_bindings["WeaponCategory1"] = '1';
    key_bindings["WeaponCategory2"] = '2';
    key_bindings["WeaponCategory3"] = '3';
    key_bindings["WeaponCategory4"] = '4';
    key_bindings["WeaponCategory5"] = '5';
    key_bindings["WeaponCategory6"] = '6';

    mouse_bindings["Fire"] = 0; // Left click
    mouse_bindings["NextWeapon"] = 1; // Right click
}

unsigned char ProfileConfig::GetKeyForAction(const std::string& action, unsigned char fallback) const {
    auto it = key_bindings.find(action);
    if (it != key_bindings.end() && it->second != 0) {
        return it->second;
    }
    return fallback;
}

int ProfileConfig::GetMouseButtonForAction(const std::string& action, int fallback) const {
    auto it = mouse_bindings.find(action);
    if (it != mouse_bindings.end()) {
        return it->second;
    }
    return fallback;
}

bool ConfigQvmLoader::Load(const std::string& path, std::vector<ProfileConfig>& profiles, int& active_profile_idx) {
    profiles.clear();
    active_profile_idx = 0;

    if (!std::filesystem::exists(path)) {
        return false;
    }

    QVMFile qvm = QVM_Parse(path);
    if (!qvm.valid) {
        return false;
    }

    ProfileConfig current_profile;
    current_profile.SetDefaultBindings();
    bool in_profile = false;

    // Simple stack simulation to evaluate GO* calls in config.qvm
    std::vector<std::string> val_stack;

    auto pop_str = [&]() -> std::string {
        if (val_stack.empty()) return "";
        std::string s = val_stack.back();
        val_stack.pop_back();
        return s;
    };

    for (const auto& ins : qvm.instructions) {
        if (ins.type == QVMOpType::PUSHS || ins.type == QVMOpType::PUSHSIB) {
            if (ins.operand < qvm.strings.size()) {
                val_stack.push_back(qvm.strings[ins.operand]);
            } else {
                val_stack.push_back("");
            }
        } else if (ins.type == QVMOpType::PUSHI || ins.type == QVMOpType::PUSHIIB || ins.type == QVMOpType::PUSHB || ins.type == QVMOpType::PUSHW) {
            if (ins.operand < qvm.identifiers.size()) {
                val_stack.push_back(qvm.identifiers[ins.operand]);
            } else {
                val_stack.push_back(std::to_string(ins.operand));
            }
        } else if (ins.type == QVMOpType::PUSHF) {
            val_stack.push_back(std::to_string(ins.operand_float));
        } else if (ins.type == QVMOpType::PUSH0) {
            val_stack.push_back("0");
        } else if (ins.type == QVMOpType::PUSH1) {
            val_stack.push_back("1");
        } else if (ins.type == QVMOpType::CALL || ins.type == QVMOpType::JSR) {
            std::string func_name = "";
            if (!ins.call_targets.empty() && ins.call_targets[0] >= 0 && (size_t)ins.call_targets[0] < qvm.identifiers.size()) {
                func_name = qvm.identifiers[ins.call_targets[0]];
            }

            if (func_name == "GOStart") {
                std::string arg = pop_str();
                try { active_profile_idx = std::stoi(arg); } catch(...) {}
            } else if (func_name == "GOPlayer") {
                if (in_profile) {
                    profiles.push_back(current_profile);
                }
                current_profile = ProfileConfig();
                current_profile.SetDefaultBindings();
                current_profile.name = pop_str();
                in_profile = true;
            } else if (func_name == "GOActiveMission") {
                std::string arg = pop_str();
                try { current_profile.active_mission = std::stoi(arg); } catch(...) {}
            } else if (func_name == "GOInMouSens") {
                std::string arg = pop_str();
                try { current_profile.mouse_sensitivity = std::stof(arg); } catch(...) {}
            } else if (func_name == "GOInMouInv") {
                std::string arg = pop_str();
                try { current_profile.invert_mouse = (std::stoi(arg) != 0); } catch(...) {}
            } else if (func_name == "GOSoundFX") {
                std::string arg = pop_str();
                try { current_profile.sound_fx_volume = std::stof(arg); } catch(...) {}
            } else if (func_name == "GOSoundSpeech") {
                std::string arg = pop_str();
                try { current_profile.sound_speech_volume = std::stof(arg); } catch(...) {}
            } else if (func_name == "GOSoundMusic") {
                std::string arg = pop_str();
                try { current_profile.sound_music_volume = std::stof(arg); } catch(...) {}
            } else if (func_name == "GOInRemap") {
                std::string key_str = pop_str();
                std::string dev_str = pop_str();
                std::string act_str = pop_str();

                if (dev_str.find("MOUSE") != std::string::npos) {
                    current_profile.mouse_bindings[act_str] = MouseButtonNameToIndex(key_str);
                } else {
                    unsigned char ascii = KeyNameToAscii(key_str);
                    if (ascii != 0) {
                        current_profile.key_bindings[act_str] = ascii;
                    }
                }
            }
        }
    }

    if (in_profile) {
        profiles.push_back(current_profile);
    }

    return !profiles.empty();
}

ProfileConfig ConfigQvmLoader::GetActiveProfile(const std::string& custom_path) {
    std::vector<std::string> search_paths;
    if (!custom_path.empty()) search_paths.push_back(custom_path);
    // The editor's own directory wins: a config.qvm shipped next to the exe
    // (or run from) is the authoring copy the user edits.
    search_paths.push_back("config.qvm");
    search_paths.push_back(Utils::GetExeDirectory() + "\\config.qvm");
    search_paths.push_back(Utils::GetIGIRootPath() + "\\config.qvm");

    std::vector<ProfileConfig> profiles;
    int active_idx = 0;

    for (const auto& sp : search_paths) {
        if (Load(sp, profiles, active_idx)) {
            if (active_idx >= 0 && (size_t)active_idx < profiles.size()) {
                return profiles[active_idx];
            }
            if (!profiles.empty()) {
                return profiles.front();
            }
        }
    }

    ProfileConfig fallback;
    fallback.SetDefaultBindings();
    return fallback;
}

} // namespace igi
