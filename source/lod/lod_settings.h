#pragma once
/******************************************************************************
 * @file    lod_settings.h
 * @brief   The engine's authored per-model LOD switch distances.
 *
 * Ports the two natives the editor needs for LOD parity. Nothing from the
 * game-runtime path is included: the fov/cull/complexity branch of lod_select
 * exists to pick a LOD when a model has NO authored table, and the editor
 * always has one (it reads lod.qvm), so that branch is not reachable here.
 *
 * Oracle: IGI-RE (decompilation of retail IGI.exe).
 *   ModelLODSettings_Changed  HOOK 0x4EDB60  src/animtrig/lodsettings.c:157-173
 *   lod_select (unk24 branch)                src/lod/mesh3d.c:160-184
 *   ModelLODSettings_Create   HOOK 0x4EDB30  src/animtrig/lodsettings.c:146-154
 *****************************************************************************/

#include <cstdint>

namespace igi {
namespace lod {

// src/math/mathvars.h:6 — world units per metre.
constexpr float k4096 = 4096.0f;

// src/animtrig/lodsettings.c:146-154 — Create zeroes dist[0..3]. dist[4] is NOT
// written by Create, so it holds whatever the task allocator left there until a
// QVM assignment fills it. Callers here always supply all five, so the
// uninitialised case cannot arise; it is recorded because it is why a
// ModelLODSettings with four authored distances still has a fifth.
inline void LodSettingsCreate(float dist[5]) {
    dist[0] = 0.0f;
    dist[1] = 0.0f;
    dist[2] = 0.0f;
    dist[3] = 0.0f;
    // dist[4] intentionally untouched (see above)
}

// src/animtrig/lodsettings.c:157-171 — "makes the distances ascending and hands
// them to the model":
//
//     void ModelLODSettings_Changed(ModelLODSettings *s) {
//         for (i = 0; i < 5; i++) {
//             if (i && s->dist[i] < s->dist[i - 1])
//                 s->dist[i] = s->dist[i - 1];
//         }
//         model = VirModel_Find(s->name);
//         if (model) {
//             *(int *)(model + 0x24) = 1;
//             for (i = 0; i < 5; i++)
//                 ((float *)(model + 0x28))[i] = s->dist[i] * k4096;
//         }
//     }
//
// The monotonize loop is the part an editor is most likely to omit: an authored
// file may hold distances that decrease, and the engine silently raises each one
// to its predecessor rather than rejecting the data. dist[] is modified in place
// and the scaled result is returned separately, because the engine keeps the
// authored values in the task and the scaled copies on the VirModel.
inline void MonotonizeDistances(float dist[5]) {
    for (int i = 0; i < 5; ++i) {
        if (i != 0 && dist[i] < dist[i - 1]) dist[i] = dist[i - 1];
    }
}

// The `((float *)(model + 0x28))[i] = s->dist[i] * k4096` half of the above.
inline void ScaleDistancesToWorldUnits(const float dist[5], float out[5]) {
    for (int i = 0; i < 5; ++i) out[i] = dist[i] * k4096;
}

// src/lod/mesh3d.c:172-184 — the authored-table branch of lod_select:
//
//     if (vm->unk24) {
//         d = fov * ((dist - r) * k);
//         if (d > dists[4])
//             return -1;
//         i = 0;
//         if (!(dists[0] < 0.0f || dists[0] > 0.0f)) {
//             i = n - 1;
//             if (i > 1)
//                 i = 1;
//         }
//         while (d > dists[i] && i < n - 1)
//             i++;
//         return i;
//     }
//
// `dists` is the k4096-scaled table and `d` is in the same units. Returns -1 for
// "too far to draw", otherwise a LOD index in [0, lod_count).
//
// The `!(dists[0] < 0.0f || dists[0] > 0.0f)` test is written exactly as the
// oracle has it. It is not `dists[0] == 0.0f`: the double negation is also true
// for NaN, so a NaN first distance takes the same path. Written this way rather
// than "simplified" so it stays comparable to the decompilation line by line.
inline int SelectAuthoredLod(const float dists[5], int lod_count, float fov,
                             float dist, float radius, float k) {
    if (lod_count <= 0) return -1;  // no chain: VirModel_Get reports not-found
    const float d = fov * ((dist - radius) * k);
    if (d > dists[4]) return -1;

    int i = 0;
    if (!(dists[0] < 0.0f || dists[0] > 0.0f)) {
        i = lod_count - 1;
        if (i > 1) i = 1;
    }
    while (d > dists[i] && i < lod_count - 1) ++i;
    return i;
}

} // namespace lod
} // namespace igi
