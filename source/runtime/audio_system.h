#pragma once
#include <string>
#include <vector>
#include <unordered_map>

namespace igi {

enum class SoundEffect {
    Gunshot,
    Reload,
    BulletImpact,
    Jump,
    Footstep,
    Pain
};

class AudioSystem {
public:
    static void Initialize();
    static void Play(SoundEffect sfx);
    static void PlayWavFile(const std::string& path);
};

} // namespace igi
