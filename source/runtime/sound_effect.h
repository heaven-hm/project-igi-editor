// sound_effect.h - Shared sound identifiers crossing the runtime/presentation boundary
#pragma once

namespace igi {

// The simulation emits stable semantic effects. The Windows audio adapter owns
// the mapping from these identifiers to loose or packed vanilla WAV assets.
enum class SoundEffect {
    Gunshot,
    Reload,
    BulletImpact,
    Jump,
    Footstep,
    Pain,
    ObjectiveComplete,
    ProjectileLaunch,
    Explosion,
    Flashbang
};

} // namespace igi
