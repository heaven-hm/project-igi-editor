#include "audio_system.h"
#include <filesystem>
#include <vector>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#endif

namespace igi {

void AudioSystem::Initialize() {
    // Initialized
}

void AudioSystem::PlayWavFile(const std::string& path) {
#if defined(_WIN32)
    if (std::filesystem::exists(path)) {
        PlaySoundA(path.c_str(), NULL, SND_ASYNC | SND_FILENAME | SND_NODEFAULT);
    }
#endif
}

void AudioSystem::Play(SoundEffect sfx) {
    std::string path;
    switch (sfx) {
        case SoundEffect::Gunshot:
        case SoundEffect::BulletImpact:
            path = "Assets\\sounds\\bullet-impact.wav";
            if (!std::filesystem::exists(path)) path = "D:\\IGI1\\Assets\\sounds\\bullet-impact.wav";
            break;
        case SoundEffect::Jump:
        case SoundEffect::Footstep:
        case SoundEffect::Reload:
        case SoundEffect::Pain:
            path = "Assets\\sounds\\bullet-impact.wav";
            if (!std::filesystem::exists(path)) path = "D:\\IGI1\\Assets\\sounds\\bullet-impact.wav";
            break;
    }

    PlayWavFile(path);
}

} // namespace igi
