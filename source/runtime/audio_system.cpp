#include "audio_system.h"
#include "audio_asset_resolver.h"
#include <filesystem>
#include <algorithm>
#include <vector>
#include <string>
#include <unordered_map>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#endif

namespace igi {

std::string AudioSystem::game_root_;
std::filesystem::path AudioSystem::audio_cache_directory_;
int AudioSystem::active_level_number_ = 1;
std::unordered_map<std::string, std::string>
    AudioSystem::conditional_sound_aliases_;
uint64_t AudioSystem::next_conditional_sound_alias_ = 1;

AudioAssetResolver& GetAudioAssetResolver() {
    static AudioAssetResolver resolver;
    return resolver;
}

void AudioSystem::Initialize(const std::string& game_root) {
    StopAllConditionalSounds();
    game_root_ = game_root;
    active_level_number_ = 1;
    std::error_code error_code;
    audio_cache_directory_ = std::filesystem::temp_directory_path(
        error_code) / "igi-editor-audio-cache";
    if (error_code) {
        audio_cache_directory_.clear();
    }
    GetAudioAssetResolver().Configure(game_root_, audio_cache_directory_);
}

void AudioSystem::SetActiveLevel(int level_number) {
    active_level_number_ = std::max(1, level_number);
    GetAudioAssetResolver().SetActiveLevel(active_level_number_);
}

std::filesystem::path AudioSystem::ResolveSoundPath(
    const std::string& authored_sound) {
    const std::filesystem::path loose_path = FindExistingPath(authored_sound);
    if (!loose_path.empty()) {
        return loose_path;
    }

    AudioAssetResolver& resolver = GetAudioAssetResolver();
    resolver.SetActiveLevel(active_level_number_);
    return resolver.ResolveWavPath(authored_sound);
}

std::filesystem::path AudioSystem::FindExistingPath(const std::string& relative_path) {
    const std::filesystem::path requested_path(relative_path);
    if (requested_path.is_absolute() && std::filesystem::exists(requested_path)) {
        return requested_path;
    }

    std::vector<std::filesystem::path> candidates;
    if (!game_root_.empty()) {
        candidates.emplace_back(std::filesystem::path(game_root_) / requested_path);
    }
    candidates.emplace_back(requested_path);
#if defined(_WIN32)
    candidates.emplace_back(std::filesystem::path("D:\\IGI1") / requested_path);
#endif

    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

void AudioSystem::PlayWavFile(const std::string& path) {
#if defined(_WIN32)
    const std::filesystem::path resolved_path = FindExistingPath(path);
    if (!resolved_path.empty()) {
        const std::string native_path = resolved_path.string();
        PlaySoundA(native_path.c_str(), NULL, SND_ASYNC | SND_FILENAME | SND_NODEFAULT);
    }
#else
    (void)path;
#endif
}

void AudioSystem::PlayConditionalSound(
    const std::string& channel_id,
    const std::string& authored_sound,
    SoundEffect fallback) {
    StopConditionalSound(channel_id);

    if (authored_sound.empty()) {
        Play(fallback);
        return;
    }

    const std::filesystem::path resolved_path = ResolveSoundPath(authored_sound);
    if (resolved_path.empty()) {
        Play(fallback);
        return;
    }

#if defined(_WIN32)
    const std::string alias = "igi_conditional_" +
        std::to_string(next_conditional_sound_alias_++);
    const std::string open_command = "open \"" + resolved_path.string() +
        "\" type waveaudio alias " + alias;
    if (mciSendStringA(open_command.c_str(), nullptr, 0, nullptr) == 0) {
        const std::string play_command = "play " + alias + " repeat";
        if (mciSendStringA(play_command.c_str(), nullptr, 0, nullptr) == 0) {
            conditional_sound_aliases_[channel_id] = alias;
            return;
        }
        mciSendStringA(("close " + alias).c_str(), nullptr, 0, nullptr);
    }
#endif

    // Asset conversion or the native looping device can be unavailable in an
    // editor installation. Preserve an audible one-shot fallback instead of
    // silently dropping the authored edge.
    Play(fallback);
}

void AudioSystem::StopConditionalSound(const std::string& channel_id) {
    const auto alias_iterator = conditional_sound_aliases_.find(channel_id);
    if (alias_iterator == conditional_sound_aliases_.end()) {
        return;
    }

#if defined(_WIN32)
    const std::string& alias = alias_iterator->second;
    mciSendStringA(("stop " + alias).c_str(), nullptr, 0, nullptr);
    mciSendStringA(("close " + alias).c_str(), nullptr, 0, nullptr);
#endif
    conditional_sound_aliases_.erase(alias_iterator);
}

void AudioSystem::StopAllConditionalSounds() {
    for (const auto& [channel_id, alias] : conditional_sound_aliases_) {
#if defined(_WIN32)
        mciSendStringA(("stop " + alias).c_str(), nullptr, 0, nullptr);
        mciSendStringA(("close " + alias).c_str(), nullptr, 0, nullptr);
#else
        (void)alias;
#endif
        (void)channel_id;
    }
    conditional_sound_aliases_.clear();
}

void AudioSystem::PlayWeaponFire(
    const std::string& authored_sound,
    SoundEffect fallback) {
    if (authored_sound.empty()) {
        Play(fallback);
        return;
    }

    const std::filesystem::path resolved_path = ResolveSoundPath(authored_sound);
    if (!resolved_path.empty()) {
        PlayWavFile(resolved_path.string());
        return;
    }

    // Missing authored sound assets are expected in headless/editor setups;
    // retain an audible Windows fallback instead of dropping the fire event.
    Play(fallback);
}

void AudioSystem::Play(SoundEffect sfx) {
    std::string relative_path;
    switch (sfx) {
        case SoundEffect::Gunshot:
            relative_path = "m16_loop";
            break;
        case SoundEffect::BulletImpact:
            relative_path = "bul_concrete_1";
            break;
        case SoundEffect::Pain:
            relative_path = "player_hit_1";
            break;
        case SoundEffect::Jump:
            relative_path = "jump_1";
            break;
        case SoundEffect::Footstep:
            relative_path = "walk_ground_1";
            break;
        case SoundEffect::Reload:
            relative_path = "m16_reload_1";
            break;
        case SoundEffect::ObjectiveComplete:
            relative_path = "message";
            break;
        case SoundEffect::ProjectileLaunch:
            relative_path = "grenade_shot_1";
            break;
        case SoundEffect::Explosion:
            relative_path = "explo_01_s";
            break;
        case SoundEffect::Flashbang:
            relative_path = "explo_flash";
            break;
    }

    std::filesystem::path resolved_path = ResolveSoundPath(relative_path);
    if (resolved_path.empty() &&
        (sfx == SoundEffect::BulletImpact || sfx == SoundEffect::Pain)) {
        // Keep a loose mission fallback for incomplete/editor-only installs.
        resolved_path = FindExistingPath(
            "missions/location0/level1/sounds/m1_beeps01.wav");
    }
    if (!resolved_path.empty()) {
        PlayWavFile(resolved_path.string());
    } else {
#if defined(_WIN32)
        MessageBeep(MB_OK);
#endif
    }
}

} // namespace igi
