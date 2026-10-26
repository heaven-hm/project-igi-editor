#ifndef IGI_LOD_LOD_QVM_TABLE_H
#define IGI_LOD_LOD_QVM_TABLE_H

// The authored per-model LOD distance table, read out of a level's `lod.qvm`.
//
// IGI-RE (faithful decompilation of retail IGI.exe):
//
//   `ModelLODSettings` is a task type `{ char name[16]; float dist[5]; }` whose
//   six parameters are the model name plus five distances
//   (`gModelLODSettingsParams`, src/animtrig/lodsettings.c:95,104-109). The
//   level-wide `LODSettings` type is a different task (`:19-28`, `:96-103`).
//
//   `ModelLODSettings_Changed`  HOOK(0x4EDB60)  src/animtrig/lodsettings.c:157-171
//   clamps the table to be non-decreasing and writes the scaled copy onto the
//   model:
//       for (i = 0; i < 5; i++)
//           if (i && s->dist[i] < s->dist[i-1]) s->dist[i] = s->dist[i-1];
//       model = VirModel_Find(s->name);
//       if (model) { *(int *)(model + 0x24) = 1;
//                    ((float *)(model + 0x28))[i] = s->dist[i] * 4096; }
//
//   `lod_select`'s authored branch (src/lod/mesh3d.c:172-184) is the only reader
//   of that table; `VirModel.lod_dist[5] = 100/(i*i)` from VirModel_InitLods is
//   dead data (no reader in IGI-RE).
//
// The decompiled line shape this parses is
//
//   Task_New(-1, "ModelLODSettings", "300_01_1", "300_01_1", 70.0, 170.0, 300.0, 350.0, 500.0);
//
// i.e. three leading arguments (`tokens[0..2]`, the model name being `tokens[2]`)
// followed by the five distances in `tokens[4..8]` -- the same 6-parameter
// layout, in the same order, as `gModelLODSettingsParams`.
//
// This is a header-only, dependency-free primitive so the parse is host-testable
// and has exactly one implementation: `Renderer_Objects::EnsurePortalDistancesLoaded`
// (source/renderer/renderer_objects_metadata.cpp) used to carry a private copy of
// this tokenizer.

#include <algorithm>
#include <array>
#include <cstddef>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace igi {
namespace lod {

// One authored `ModelLODSettings` task.
struct AuthoredLodEntry {
    std::string model;                              // tokens[2], unquoted
    std::array<float, 5> distance{{0.f, 0.f, 0.f, 0.f, 0.f}};  // tokens[4..8], metres
};

// Every `ModelLODSettings` entry in a decompiled `lod.qvm`, in file order.
// Entries with fewer than nine arguments or a non-numeric distance are skipped
// (the previous inline loop skipped them via its surrounding try/catch).
inline std::vector<AuthoredLodEntry> ParseAuthoredLodEntries(const std::string& decompiled) {
    std::vector<AuthoredLodEntry> entries;
    std::stringstream ss(decompiled);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.find("Task_New") == std::string::npos) continue;
        if (line.find("\"ModelLODSettings\"") == std::string::npos) continue;

        const size_t start = line.find('(');
        const size_t end = line.rfind(')');
        if (start == std::string::npos || end == std::string::npos || end <= start) continue;

        std::vector<std::string> tokens;
        std::stringstream argStream(line.substr(start + 1, end - start - 1));
        std::string token;
        while (std::getline(argStream, token, ',')) {
            const size_t first = token.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) {
                tokens.push_back(std::string());
                continue;
            }
            const size_t last = token.find_last_not_of(" \t\r\n");
            tokens.push_back(token.substr(first, last - first + 1));
        }
        // Three leading args + five distances (gModelLODSettingsParams,
        // src/animtrig/lodsettings.c:104-109).
        if (tokens.size() < 9) continue;

        AuthoredLodEntry entry;
        entry.model = tokens[2];
        if (entry.model.size() >= 2 && entry.model.front() == '"' && entry.model.back() == '"')
            entry.model = entry.model.substr(1, entry.model.size() - 2);

        bool ok = true;
        for (int i = 0; i < 5 && ok; ++i) {
            try {
                entry.distance[static_cast<size_t>(i)] =
                    std::stof(tokens[static_cast<size_t>(4 + i)]);
            } catch (...) {
                ok = false;  // same outcome as the old try/catch around the whole entry
            }
        }
        if (!ok) continue;
        entries.push_back(std::move(entry));
    }
    return entries;
}

// The "Distance to cutoff" the editor culls portal attachments by: `tokens[8]`,
// or the largest of the five distances when it is not positive. This is the
// behaviour `EnsurePortalDistancesLoaded` already had; kept here so the one
// parser owns it.
inline float AuthoredCutoffDistance(const AuthoredLodEntry& entry) {
    float d = entry.distance[4];
    if (d <= 0.0f) {
        for (float v : entry.distance) d = std::max(d, v);
    }
    return d;
}

// ---------------------------------------------------------------------------
// The level-wide `LODSettings` task, which is a DIFFERENT task type from
// `ModelLODSettings` (src/animtrig/lodsettings.c:19-28, :96-103):
//
//   Task_New(-1, "LODSettings", "",
//            <splineDetail>, <rigidDetail>, <boneDetail>,
//            <splineCull>, <rigidCull>, <boneCull>, <blend>, <range>);
//
// `LODSettings_Changed` (HOOK 0x4EDAA0, src/animtrig/lodsettings.c:129-137) turns
// the four detail/cull percentages into factors (`* 0.02`, `:52`), keeps `blend`
// raw and scales `range` by 4096 into `gLodRange` — the values `lod_select`'s
// complexity branch reads (src/lod/mesh3d.c:187,196,205). Returned through
// `igi::lod::ScaleLevelLodSettings` so the scaling has one implementation.
//
// `false` when the level's lod.qvm has no LODSettings task; the caller then keeps
// the globals' own defaults (BSS 0 with `gLodBlend = 1.0f`, and
// `LODSettings_Create`'s blend 1.0 / range 4.0 — src/animtrig/lodsettings.c:117-127).
inline bool ParseLevelLodSettings(const std::string& decompiled, float out[8]) {
    std::stringstream ss(decompiled);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.find("Task_New") == std::string::npos) continue;
        if (line.find("\"LODSettings\"") == std::string::npos) continue;

        const size_t start = line.find('(');
        const size_t end = line.rfind(')');
        if (start == std::string::npos || end == std::string::npos || end <= start) continue;

        std::vector<std::string> tokens;
        std::stringstream argStream(line.substr(start + 1, end - start - 1));
        std::string token;
        while (std::getline(argStream, token, ',')) {
            const size_t first = token.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) {
                tokens.push_back(std::string());
                continue;
            }
            const size_t last = token.find_last_not_of(" \t\r\n");
            tokens.push_back(token.substr(first, last - first + 1));
        }

        // The type name is the second argument; the task name follows it and the
        // eight parameters come after that (gLODSettingsParams, :96-103).
        size_t typeAt = tokens.size();
        for (size_t i = 0; i < tokens.size(); ++i) {
            if (tokens[i] == "\"LODSettings\"") { typeAt = i; break; }
        }
        if (typeAt == tokens.size() || typeAt + 2 + 8 > tokens.size()) continue;

        bool ok = true;
        for (int i = 0; i < 8 && ok; ++i) {
            try {
                out[i] = std::stof(tokens[typeAt + 2 + static_cast<size_t>(i)]);
            } catch (...) {
                ok = false;
            }
        }
        if (ok) return true;
    }
    return false;
}

} // namespace lod
} // namespace igi

#endif // IGI_LOD_LOD_QVM_TABLE_H
