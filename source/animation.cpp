#include "animation.h"
#include "utils_igi1conv.h"
#include "logger.h"
#include "utils.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

// ── Parsing helpers ──────────────────────────────────────────────────────────

static std::string Trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Extract a string between quotes: "value"
static std::string ParseQuotedString(const std::string& s, size_t& pos) {
    size_t q1 = s.find('"', pos);
    if (q1 == std::string::npos) return {};
    size_t q2 = s.find('"', q1 + 1);
    if (q2 == std::string::npos) return {};
    pos = q2 + 1;
    // Mirror ParseToken: consume the separating comma so the next ParseInt/
    // ParseFloat call doesn't see an empty token (pos sitting right on ',').
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t')) pos++;
    if (pos < s.size() && s[pos] == ',') pos++;
    return s.substr(q1 + 1, q2 - q1 - 1);
}

// Extract next comma-separated token (int or float)
static std::string ParseToken(const std::string& s, size_t& pos) {
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t')) pos++;
    size_t start = pos;
    while (pos < s.size() && s[pos] != ',' && s[pos] != ')' && s[pos] != ';') pos++;
    std::string tok = s.substr(start, pos - start);
    tok = Trim(tok);
    if (pos < s.size() && s[pos] == ',') pos++;
    return tok;
}

static int ParseInt(const std::string& s, size_t& pos) {
    return std::stoi(ParseToken(s, pos));
}

static float ParseFloat(const std::string& s, size_t& pos) {
    return std::stof(ParseToken(s, pos));
}

// Zero-pad to 3 digits, matching igi1conv's <NNN>.IFF / "_anim_<NNN>" convention.
static std::string Pad3(int n) {
    std::string s = std::to_string(n < 0 ? 0 : n);
    while (s.size() < 3) s = "0" + s;
    return s;
}

// ── AI script animation id discovery ─────────────────────────────────────────

std::vector<int> FindAiScriptAnimationIds(const std::string& qvmPath) {
    std::vector<int> ids;
    if (qvmPath.empty() || !fs::exists(qvmPath)) {
        Logger::Get().Log(LogLevel::DEBUG, "[Anim] AI script not found: " + qvmPath);
        return ids;
    }

    std::string tempQsc = igi1conv::MakeTempPath(".aianim.qsc");
    std::string err;
    if (!igi1conv::QvmDecompile(qvmPath, tempQsc, err)) {
        Logger::Get().Log(LogLevel::WARNING, "[Anim] Failed to decompile AI script " + qvmPath + ": " + err);
        return ids;
    }

    std::ifstream f(tempQsc);
    if (f.is_open()) {
        std::string line;
        const std::string needle = "AIAction_PlayAnimation(";
        while (std::getline(f, line)) {
            size_t pos = line.find(needle);
            if (pos == std::string::npos) continue;
            pos += needle.size();
            try {
                int id = std::stoi(line.substr(pos));
                if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
            } catch (...) {}
        }
        f.close();
    } else {
        Logger::Get().Log(LogLevel::WARNING, "[Anim] Could not open decompiled AI script: " + tempQsc);
    }

    std::error_code ec;
    fs::remove(tempQsc, ec);

    Logger::Get().Log(LogLevel::INFO, "[Anim] Found " + std::to_string(ids.size()) +
                     " AIAction_PlayAnimation id(s) in " + qvmPath);
    return ids;
}

// ── AnimPlayback::Update ─────────────────────────────────────────────────────

void AnimPlayback::Update(float dtMs) {
    if (!clip || !playing) return;
    currentTimeMs += dtMs * speed;
    int dur = clip->duration_ms();
    if (dur > 0) {
        if (clip->loop() || forceLoop) {
            // Loop: wrap around. forceLoop keeps editor auto-play running
            // continuously even for clips whose own tp_flag is non-looping —
            // so every AI animates forever instead of freezing after one cycle.
            while (currentTimeMs >= (float)dur) currentTimeMs -= (float)dur;
        } else {
            // Clamp and stop
            if (currentTimeMs >= (float)dur) {
                currentTimeMs = (float)dur;
                playing = false;
            }
        }
    }
}

// ── AnimationRegistry ────────────────────────────────────────────────────────

AnimationRegistry::AnimationRegistry() {}

std::string AnimationRegistry::GetCacheDir(int boneHierarchy) const {
    return Utils::GetExeDirectory() + "\\cache\\anims\\" + Pad3(boneHierarchy);
}

std::string AnimationRegistry::FindIffFile(int boneHierarchy) const {
    std::string name = Pad3(boneHierarchy) + ".IFF";
    std::string root = Utils::GetIGIRootPath();

    std::string path = root + "\\common\\ANIMS\\" + name;
    if (fs::exists(path)) return path;

    path = root + "\\common\\" + name;
    if (fs::exists(path)) return path;

    return {};
}

bool AnimationRegistry::ImportAnimations(int boneHierarchy) {
    if (boneHierarchy < 0) return false;

    // Check if already imported
    auto it = registry_.find(boneHierarchy);
    if (it != registry_.end() && !it->second.clips.empty())
        return true;

    std::string iffPath = FindIffFile(boneHierarchy);
    if (iffPath.empty()) {
        Logger::Get().Log(LogLevel::DEBUG, "[Anim] No .IFF found for bone hierarchy " + Pad3(boneHierarchy));
        return false;
    }

    std::string cacheDir = GetCacheDir(boneHierarchy);

    // Run igi1conv iff convert
    std::string err;
    if (!igi1conv::IffConvert(iffPath, cacheDir, err)) {
        Logger::Get().Log(LogLevel::WARNING, "[Anim] igi1conv iff convert failed for bone hierarchy " +
                         Pad3(boneHierarchy) + ": " + err);
        return false;
    }

    // Parse Anims.qsc for clip names
    std::string qscPath = cacheDir + "\\Anims.qsc";
    std::vector<std::string> clipNames = ParseAnimsQsc(qscPath);
    if (clipNames.empty()) {
        Logger::Get().Log(LogLevel::DEBUG, "[Anim] No clips in Anims.qsc for bone hierarchy " + Pad3(boneHierarchy));
        return false;
    }

    BoneHierarchyAnimSet set;
    set.boneHierarchy = boneHierarchy;
    set.cacheDir = cacheDir;
    set.clipNames = clipNames;

    // Parse each .BEF file
    for (const auto& clipName : clipNames) {
        // Extract filename from anim path (e.g. "anims_003\003_anim_004" -> last part)
        std::string befName = clipName;
        size_t sep = befName.rfind('\\');
        if (sep != std::string::npos) befName = befName.substr(sep + 1);

        std::string befPath = cacheDir + "\\" + befName + ".BEF";
        if (!fs::exists(befPath)) {
            // Try alternate naming
            befPath = cacheDir + "\\" + befName + ".bef";
            if (!fs::exists(befPath)) continue;
        }

        AnimationClip clip;
        if (ParseBefFile(befPath, clip)) {
            // Use the parsed clip name if it was successfully set from AnimInit
            if (clip.name.empty()) clip.name = befName;
            // animation_id is encoded as the trailing "_anim_<NNN>" suffix of the name.
            size_t animPos = clip.name.rfind("_anim_");
            if (animPos != std::string::npos) {
                try { clip.animId = std::stoi(clip.name.substr(animPos + 6)); } catch (...) {}
            }
            set.clips[clip.name] = std::move(clip);
        }
    }

    if (set.clips.empty()) {
        Logger::Get().Log(LogLevel::WARNING, "[Anim] No .BEF files parsed for bone hierarchy " + Pad3(boneHierarchy));
        return false;
    }

    registry_[boneHierarchy] = std::move(set);
    Logger::Get().Log(LogLevel::INFO, "[Anim] Imported " + std::to_string(registry_[boneHierarchy].clips.size()) +
                     " animations for bone hierarchy " + Pad3(boneHierarchy));
    return true;
}

std::vector<std::string> AnimationRegistry::GetClipNames(int boneHierarchy) const {
    auto it = registry_.find(boneHierarchy);
    if (it == registry_.end()) return {};
    return it->second.clipNames;
}

const AnimationClip* AnimationRegistry::GetClip(int boneHierarchy, const std::string& clipName) const {
    auto it = registry_.find(boneHierarchy);
    if (it == registry_.end()) return nullptr;
    auto cit = it->second.clips.find(clipName);
    if (cit == it->second.clips.end()) return nullptr;
    return &cit->second;
}

const AnimationClip* AnimationRegistry::GetClipByAnimId(int boneHierarchy, int animId) const {
    auto it = registry_.find(boneHierarchy);
    if (it == registry_.end()) return nullptr;
    for (const auto& [name, clip] : it->second.clips) {
        if (clip.animId == animId) return &clip;
    }
    return nullptr;
}

const AnimationClip* AnimationRegistry::GetFirstClip(int boneHierarchy) const {
    auto it = registry_.find(boneHierarchy);
    if (it == registry_.end() || it->second.clips.empty()) return nullptr;
    return &it->second.clips.begin()->second;
}

const AnimationClip* AnimationRegistry::GetDefaultClip(int boneHierarchy) const {
    auto it = registry_.find(boneHierarchy);
    if (it == registry_.end() || it->second.clips.empty()) return nullptr;
    const AnimationClip* best = nullptr;
    for (const auto& [name, clip] : it->second.clips) {
        if (!best || clip.animId < best->animId) best = &clip;
    }
    return best;
}

bool AnimationRegistry::HasAnimations(int boneHierarchy) const {
    auto it = registry_.find(boneHierarchy);
    return it != registry_.end() && !it->second.clips.empty();
}

void AnimationRegistry::Clear() {
    registry_.clear();
}

// ── BEF Parser ────────────────────────────────────────────────────────────────

bool AnimationRegistry::ParseBefFile(const std::string& path, AnimationClip& out) {
    std::ifstream f(path);
    if (!f.is_open()) return false;

    std::string line;
    int lineNum = 0;
    bool inSkeleton = false;
    bool hierarchyBuilt = false;

    while (std::getline(f, line)) {
        lineNum++;
        line = Trim(line);

        // Skip comments and empty lines
        if (line.empty() || line.rfind("//", 0) == 0) continue;

        // Remove trailing semicolon
        if (!line.empty() && line.back() == ';')
            line.pop_back();

        // Remove trailing whitespace
        line = Trim(line);

        if (line.empty()) continue;

        // AnimInit("<name>", <flags>, <length_ms>, <tp_flag>)
        if (line.rfind("AnimInit(", 0) == 0) {
            size_t pos = 9; // after "AnimInit("
            out.name = ParseQuotedString(line, pos);
            // skip comma
            out.flags = ParseInt(line, pos);
            out.length_ms = ParseInt(line, pos);
            out.tp_flag = ParseInt(line, pos);
            continue;
        }

        // Bone(<idx>, "<name>", <parent>, <px>, <py>, <pz>)
        if (line.rfind("Bone(", 0) == 0) {
            size_t pos = 5;
            AnimBone bone;
            bone.index = ParseInt(line, pos);
            bone.name = ParseQuotedString(line, pos);
            bone.parent = ParseInt(line, pos);
            bone.restPos.x = ParseFloat(line, pos);
            bone.restPos.y = ParseFloat(line, pos);
            bone.restPos.z = ParseFloat(line, pos);
            out.bones.push_back(bone);
            inSkeleton = true;
            continue;
        }

        // BuildHierarchy()
        if (line.rfind("BuildHierarchy", 0) == 0) {
            hierarchyBuilt = true;
            inSkeleton = false;
            continue;
        }

        // BreakScript()
        if (line.rfind("BreakScript", 0) == 0) continue;

        // TranslationKeyFrameData(<track>, <flag>, <time_ms>, <px>, <py>, <pz>)
        if (line.rfind("TranslationKeyFrameData(", 0) == 0) {
            size_t pos = 24;
            AnimTranslationKey key;
            key.track = ParseInt(line, pos);
            /*flag*/ ParseInt(line, pos);
            key.time_ms = ParseInt(line, pos);
            key.pos.x = ParseFloat(line, pos);
            key.pos.y = ParseFloat(line, pos);
            key.pos.z = ParseFloat(line, pos);
            out.translationKeys.push_back(key);
            continue;
        }

        // RotationKeyFrameData(<bone>, <flag>, <time_ms>, <q0x>,<q0y>,<q0z>,<q0w>, <q1x>,...)
        if (line.rfind("RotationKeyFrameData(", 0) == 0) {
            size_t pos = 21;
            AnimRotationKey key;
            key.bone = ParseInt(line, pos);
            /*flag*/ ParseInt(line, pos);
            key.time_ms = ParseInt(line, pos);
            key.q0.x = ParseFloat(line, pos); key.q0.y = ParseFloat(line, pos); key.q0.z = ParseFloat(line, pos); key.q0.w = ParseFloat(line, pos);
            key.q1.x = ParseFloat(line, pos); key.q1.y = ParseFloat(line, pos); key.q1.z = ParseFloat(line, pos); key.q1.w = ParseFloat(line, pos);
            key.q2.x = ParseFloat(line, pos); key.q2.y = ParseFloat(line, pos); key.q2.z = ParseFloat(line, pos); key.q2.w = ParseFloat(line, pos);
            out.rotationKeys.push_back(key);
            continue;
        }

        // TriggerData(<idx>, <event_id>, <time_ms>, <bone_id>, <px>, <py>, <pz>)
        if (line.rfind("TriggerData(", 0) == 0) {
            size_t pos = 12;
            AnimEvent ev;
            ev.index = ParseInt(line, pos);
            ev.event_id = ParseInt(line, pos);
            ev.time_ms = ParseInt(line, pos);
            ev.bone_id = ParseInt(line, pos);
            ev.pos.x = ParseFloat(line, pos);
            ev.pos.y = ParseFloat(line, pos);
            ev.pos.z = ParseFloat(line, pos);
            out.events.push_back(ev);
            continue;
        }
    }

    return !out.bones.empty();
}

std::vector<std::string> AnimationRegistry::ParseAnimsQsc(const std::string& path) {
    std::vector<std::string> names;
    std::ifstream f(path);
    if (!f.is_open()) return names;

    std::string line;
    while (std::getline(f, line)) {
        line = Trim(line);
        if (line.rfind("CreateAnim(", 0) == 0) {
            size_t pos = 11;
            std::string arg = ParseQuotedString(line, pos);
            if (!arg.empty()) names.push_back(arg);
        }
    }
    return names;
}

// ── Animation evaluation ──────────────────────────────────────────────────────

// Samples a value at `keys[i]` interpolated to `keys[i+1]` for the given time.
// `getTime`/`getVal` extract the time/value from a key; `lerp` interpolates
// two values. Returns false if `keys` is empty (caller should use a default).
template <typename Key, typename Val, typename GetTime, typename GetVal, typename Lerp>
static bool SampleTrack(const std::vector<Key>& keys, float timeMs, Val& out,
                         GetTime getTime, GetVal getVal, Lerp lerp) {
    if (keys.empty()) return false;
    const float eps = 0.001f;
    size_t idx = 0;
    for (size_t i = 0; i < keys.size(); ++i) {
        if (getTime(keys[i]) <= timeMs + eps) idx = i;
        else break;
    }
    out = getVal(keys[idx]);
    if (idx + 1 < keys.size()) {
        float t0 = getTime(keys[idx]), t1 = getTime(keys[idx + 1]);
        float range = t1 - t0;
        if (range > eps) {
            float t = glm::clamp((timeMs - t0) / range, 0.f, 1.f);
            out = lerp(getVal(keys[idx]), getVal(keys[idx + 1]), t);
        }
    }
    return true;
}

// Builds each bone's LOCAL transform: Translate(restOffset) * Rotate(animatedRot),
// where restOffset is the bone's parent-relative rest position (overridden by the
// clip's animated root-translation track for bone 0). EvaluateWorld composes these
// through the parent chain — at rest (identity rotations) this reduces to the
// cumulative sum of rest offsets, i.e. exactly the bone's rest world position.
void AnimationRegistry::Evaluate(const AnimationClip* clip, float timeMs,
                                 std::vector<glm::mat4>& boneTransforms) const
{
    if (!clip || clip->bones.empty()) return;

    size_t numBones = clip->bones.size();
    boneTransforms.assign(numBones, glm::mat4(1.f));

    // Group rotation keys by bone once.
    std::unordered_map<int, std::vector<const AnimRotationKey*>> keysByBone;
    for (const auto& k : clip->rotationKeys) keysByBone[k.bone].push_back(&k);
    for (auto& [boneIdx, keys] : keysByBone) {
        std::sort(keys.begin(), keys.end(), [](const AnimRotationKey* a, const AnimRotationKey* b) {
            return a->time_ms < b->time_ms;
        });
    }

    // BEF Bone() px/py/pz = iff.skeleton.translations[i*3] / Sc (Sc=40.96).
    // They are already in the same unit space that gui_main.cpp uses for its
    // bone transforms (trans / IGI_SCALE).  Do NOT divide by 40.96 again.
    for (size_t i = 0; i < numBones; ++i) {
        glm::vec3 trans = clip->bones[i].restPos;  // already in meters

        // Root bone: animated translation track overrides the rest offset.
        // TranslationKeyFrameData values are also pre-divided by Sc in the BEF.
        if (i == 0 && !clip->translationKeys.empty()) {
            glm::vec3 rawTrans;
            SampleTrack(clip->translationKeys, timeMs, rawTrans,
                [](const AnimTranslationKey& k) { return (float)k.time_ms; },
                [](const AnimTranslationKey& k) { return k.pos; },
                [](glm::vec3 a, glm::vec3 b, float t) { return glm::mix(a, b, t); });
            trans = rawTrans;  // already in meters
        }

        glm::quat rot(1.f, 0.f, 0.f, 0.f);
        auto it = keysByBone.find(clip->bones[i].index);
        if (it != keysByBone.end() && !it->second.empty()) {
            const auto& keys = it->second;
            SampleTrack(keys, timeMs, rot,
                [](const AnimRotationKey* k) { return (float)k->time_ms; },
                [](const AnimRotationKey* k) { return k->q0; },
                [](glm::quat a, glm::quat b, float t) { return glm::slerp(a, b, t); });
        }

        boneTransforms[i] = glm::translate(glm::mat4(1.f), trans) * glm::mat4_cast(rot);
    }
}

void AnimationRegistry::EvaluateWorld(const AnimationClip* clip, float timeMs,
                                      std::vector<glm::mat4>& worldTransforms) const
{
    if (!clip || clip->bones.empty()) return;

    std::vector<glm::mat4> local;
    Evaluate(clip, timeMs, local);

    int maxId = -1;
    for (const auto& b : clip->bones) {
        if (b.index > maxId) maxId = b.index;
    }
    if (maxId < 0) return;

    worldTransforms.assign(maxId + 1, glm::mat4(1.f));

    size_t numBones = clip->bones.size();
    for (size_t i = 0; i < numBones; ++i) {
        int id = clip->bones[i].index;
        int p = clip->bones[i].parent;
        if (p < 0 || p >= (int)worldTransforms.size()) {
            worldTransforms[id] = local[i];
        } else {
            worldTransforms[id] = worldTransforms[p] * local[i];
        }
    }
}
