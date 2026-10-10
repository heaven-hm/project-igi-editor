#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>

#include "sound_effect.h"

namespace igi {

class AudioSystem {
public:
    static void Initialize(const std::string& game_root = {});
    static void SetActiveLevel(int level_number);
    static void Play(SoundEffect sfx);
    static void PlayWeaponFire(
        const std::string& authored_sound,
        SoundEffect fallback = SoundEffect::Gunshot);
    static void PlayConditionalSound(
        const std::string& channel_id,
        const std::string& authored_sound,
        SoundEffect fallback = SoundEffect::ObjectiveComplete);
    static void StopConditionalSound(const std::string& channel_id);
    static void PlayWavFile(const std::string& path);

private:
    static void StopAllConditionalSounds();
    static std::filesystem::path FindExistingPath(const std::string& relative_path);
    static std::filesystem::path ResolveSoundPath(const std::string& authored_sound);
    static std::string game_root_;
    static std::filesystem::path audio_cache_directory_;
    static int active_level_number_;
    static std::unordered_map<std::string, std::string>
        conditional_sound_aliases_;
    static uint64_t next_conditional_sound_alias_;
};

} // namespace igi
