#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace igi {

struct ProfileConfig {
    std::string name = "Player";
    int active_mission = 1;
    int difficulty = 1;
    bool invert_mouse = false;
    float mouse_sensitivity = 0.4f;
    float sound_fx_volume = 1.0f;
    float sound_speech_volume = 1.0f;
    float sound_music_volume = 0.8f;
    bool blood = true;

    // Action name -> mapped character key (e.g. "MoveUp" -> 'W', "Jump" -> ' ')
    std::unordered_map<std::string, unsigned char> key_bindings;
    // Action name -> mapped mouse button (0 = Left, 1 = Right, 2 = Middle)
    std::unordered_map<std::string, int> mouse_bindings;

    void SetDefaultBindings();
    unsigned char GetKeyForAction(const std::string& action, unsigned char fallback) const;
    int GetMouseButtonForAction(const std::string& action, int fallback) const;
};

class ConfigQvmLoader {
public:
    static bool Load(const std::string& path, std::vector<ProfileConfig>& profiles, int& active_profile_idx);
    static ProfileConfig GetActiveProfile(const std::string& custom_path = "");
};

} // namespace igi
