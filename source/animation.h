#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// ── Animation data structures (editor-side, read-only) ─────────────────────
// These are populated by parsing the .BEF text files produced by igi1conv.exe.
// NO converter code is duplicated — we only parse the text format.

struct AnimBone {
    int    index = 0;
    std::string name;
    int    parent = -1;
    glm::vec3 restPos{0.f};  // rest position in RAW IGI units (NOT yet divided by 40.96 — see Evaluate())
};

struct AnimRotationKey {
    int    bone = 0;
    int    time_ms = 0;
    glm::quat q0{1.f,0.f,0.f,0.f};
    glm::quat q1{1.f,0.f,0.f,0.f};
    glm::quat q2{1.f,0.f,0.f,0.f};
};

struct AnimTranslationKey {
    int    track = 0;
    int    time_ms = 0;
    glm::vec3 pos{0.f};
};

struct AnimEvent {
    int    index = 0;
    int    event_id = 0;
    int    time_ms = 0;
    int    bone_id = -1;
    glm::vec3 pos{0.f};
};

struct AnimationClip {
    std::string name;
    int    animId = -1;      // animation_id, parsed from the "<NNN>_anim_<animId>" name suffix
    int    flags = 0;
    int    length_ms = 0;    // stored as duration+1 (engine subtracts 1)
    int    tp_flag = 0;      // loop flag (0 = no loop, 1 = loop)
    int    duration_ms() const { return length_ms > 0 ? length_ms : 0; }
    bool   loop() const { return tp_flag != 0; }

    std::vector<AnimBone> bones;
    std::vector<AnimRotationKey> rotationKeys;
    std::vector<AnimTranslationKey> translationKeys;
    std::vector<AnimEvent> events;
};

// ── Animation registry ──────────────────────────────────────────────────────
// Manages imported animation sets per bone hierarchy. A "bone hierarchy" is
// the index (HumanSoldier-family arg@10) into the shared, model-independent
// common/ANIMS/<NNN>.IFF skeleton+animation file. Multiple models can (and
// do) share the same bone hierarchy. Auto-imports via igi1conv.exe and caches
// the resulting .BEF files in cache/anims/<NNN>/.

class AnimationRegistry {
public:
    AnimationRegistry();

    // Import animations for a bone hierarchy from its .IFF file. Returns true
    // if animations were imported or already up-to-date.
    bool ImportAnimations(int boneHierarchy);

    // Get all clip names for a bone hierarchy (sorted).
    std::vector<std::string> GetClipNames(int boneHierarchy) const;

    // Get clip by bone hierarchy + clip name. Returns nullptr if not found.
    const AnimationClip* GetClip(int boneHierarchy, const std::string& clipName) const;

    // Get the clip whose animation_id matches standAnimation. Returns nullptr if not found.
    const AnimationClip* GetClipByAnimId(int boneHierarchy, int animId) const;

    // Get the first available clip for a bone hierarchy (for auto-play). Returns nullptr if none.
    const AnimationClip* GetFirstClip(int boneHierarchy) const;

    // Get the DEFAULT clip for a bone hierarchy: the one with the lowest animId.
    // Used as the auto-play fallback for AI that reference no specific animation,
    // so "every AI animates" without depending on unordered_map iteration order
    // (which is arbitrary). Returns nullptr if the hierarchy has no clips.
    const AnimationClip* GetDefaultClip(int boneHierarchy) const;

    // Evaluate bone transforms at a given time (ms). Fills boneTransforms with
    // local-space transforms for each bone in the clip's skeleton.
    void Evaluate(const AnimationClip* clip, float timeMs, std::vector<glm::mat4>& boneTransforms) const;

    // Evaluate bone transforms in world space (parent-relative chain resolved).
    void EvaluateWorld(const AnimationClip* clip, float timeMs, std::vector<glm::mat4>& worldTransforms) const;

    // Clear all cached animation data.
    void Clear();

    // Returns true if the bone hierarchy has any imported animations.
    bool HasAnimations(int boneHierarchy) const;

private:
    struct BoneHierarchyAnimSet {
        int boneHierarchy = -1;
        std::string cacheDir;
        std::vector<std::string> clipNames;
        std::unordered_map<std::string, AnimationClip> clips;
    };

    std::unordered_map<int, BoneHierarchyAnimSet> registry_;

    // Parse a single .BEF text file into an AnimationClip.
    bool ParseBefFile(const std::string& path, AnimationClip& out);

    // Parse Anims.qsc to get sorted clip names.
    std::vector<std::string> ParseAnimsQsc(const std::string& path);

    // Get cache directory path for a bone hierarchy.
    std::string GetCacheDir(int boneHierarchy) const;

    // Find the common/ANIMS/<NNN>.IFF file for a bone hierarchy.
    std::string FindIffFile(int boneHierarchy) const;
};

// Decompiles ai/<aiTaskId>.qvm to a temp .qsc (via igi1conv), scans it for
// AIAction_PlayAnimation(<id>, ...) calls, and deletes the temp file. Returns
// the unique animation ids in first-seen order (empty on any failure).
std::vector<int> FindAiScriptAnimationIds(const std::string& qvmPath);

// ── Per-object animation playback state ─────────────────────────────────────
struct AnimPlayback {
    const AnimationClip* clip = nullptr;
    float currentTimeMs = 0.f;
    bool  playing = false;
    float speed = 1.0f;
    bool  forceLoop = false;  // editor: keep looping even if clip's tp_flag is non-looping

    void Reset() { currentTimeMs = 0.f; playing = false; }
    void Start(const AnimationClip* c) { clip = c; currentTimeMs = 0.f; playing = true; }
    void Stop() { playing = false; }
    void Pause() { playing = false; }
    void Resume() { if (clip) playing = true; }
    void Update(float dtMs);
    bool IsFinished() const {
        if (!clip) return true;
        return !playing && currentTimeMs >= (float)clip->duration_ms();
    }
};
