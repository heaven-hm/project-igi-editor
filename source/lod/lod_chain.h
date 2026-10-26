#ifndef IGI_LOD_LOD_CHAIN_H
#define IGI_LOD_LOD_CHAIN_H

// LOD virtual-model chains — the engine's rule, verbatim.
//
// IGI-RE (faithful decompilation of retail IGI.exe):
//
//   void VirModel_InitLods(VirModel *m)                       src/model/model.c:145-163
//   {
//       char name[32];
//       int i, n = 0;
//       m->unk24 = 0;
//       strcpy(name, m->name);
//       for (i = 1; i <= VIRMODEL_LODS; i++) {
//           m->lod_dist[i - 1] = (uint16_t)(100 / (i * i));
//           m->lods[i - 1] = ModelLod_Find(name);
//           if (!m->lods[i - 1]) break;
//           name[strlen(name) - 1]++;
//           n++;
//       }
//       m->lod_count = (uint16_t)n;
//   }
//   HOOK(0x4CED50, VirModel_InitLods);
//
// The four rules that follow from it, each with its oracle line:
//
//   1. LOD 1 is the model's own name; LOD i+1 is the previous name with its
//      LAST CHARACTER incremented by one — character arithmetic, not a numeric
//      parse (model.c:160 `name[strlen(name) - 1]++`). So `435_01_1` chains
//      through `435_01_2`, `435_01_3`, ... and a name that does not end in a
//      digit increments that character too (`waypoint` -> `waypoinu`).
//   2. The loop stops at the first name that does not resolve (model.c:157-158
//      `if (!m->lods[i - 1]) break;`). A missing variant ends the chain; it is
//      never skipped over.
//   3. At most VIRMODEL_LODS = 5 levels (model.c:155, src/model/model.h:11).
//   4. The per-level distance is 100 / (i * i) in integer arithmetic
//      (model.c:154): 100, 25, 11, 6, 4 for levels 1..5.
//
// IMPORTANT: `lod_dist` is written by VirModel_InitLods and then never read —
// `grep -rn "lod_dist" src/` in IGI-RE matches only the write at model.c:154 and
// the field declaration at modeltypes.h:57. The LOD actually drawn is chosen by
// `lod_select` (src/lod/mesh3d.c:160-208) from one of two other sources:
//   - if VirModel.unk24 is set, the AUTHORED per-model distance table
//     VirModel.unk28[5], which `ModelLODSettings_Changed`
//     (src/animtrig/lodsettings.c, HOOK(0x4EDB60)) fills from the level's
//     `ModelLODSettings` tasks (read from lod.qsc) after clamping the table to be
//     non-decreasing and scaling each value by 4096; `dists[4]` acts as the cull
//     distance (`d > dists[4]` -> -1);
//   - otherwise complexity ratios against LOD 0 combined with the level-wide
//     `LODSettings` detail/blend/range/cull values (mesh3d.c:180-207).
// LodDistance() below is therefore fidelity to the engine's *data*, not to the
// drawn LOD; keep it only for parity with `VirModel.lod_dist`.
//
// Resolution is by exact name: `ModelLod_Find` strcmps the name against the
// `ModelLod.name` of every LOD of every loaded ModelFile (model.c:165-178,
// HOOK(0x4CEE30, ModelLod_Find)). Those names come from the file's MTP `VNAM`
// string table, one per INST record (ModelFile_Setup, model.c:274-289), so a
// chain links sibling entries of a loaded bundle — measured: every retail level
// bundle's VNAM set equals its MODS set, and the level archive carries exactly
// one `<name>.mef` per bundle name.
//
// A model whose own name does not resolve has lod_count 0 and VirModel_Get
// reports it as not found (model.c:118-139); VirModel_Find then falls back to
// the model literally named "missing" (model.c:107-116).
//
// Consumers of the chain length, which is why it matters:
//   - lightmaps: `fill_refs` creates one LightmapRef per LOD, so a lightmapped
//     object contributes lod_count refs to its LightmapSet
//     (src/sector/sector.c:546-562), and each ref writes its own
//     `<info->name>_<refIndex>.olm` (src/dirlight/lightmapbuild.c:59) with
//     `mesh->texCount` pages (lightmapbuild.c:85), for format-3 meshes only
//     (lightmapbuild.c:45-46).
//   - the distance-based detail switch and the complexity warnings
//     (src/lod/mesh3d.c:160-208) — see the IMPORTANT note on rule 4 above for
//     where the distances actually come from.

#include <cstddef>
#include <string>

namespace igi {
namespace lod {

// src/model/model.h:11
constexpr int kVirModelLods = 5;

// model.c:154 — integer division, exactly as the engine stores it.
constexpr int LodDistance(int level) {
    return level > 0 ? 100 / (level * level) : 0;
}

// model.c:160 — `name[strlen(name) - 1]++`, including for non-digit last
// characters. An empty name is left alone: the engine would index name[-1],
// which is unreachable in retail (no empty VNAM name exists).
inline void IncrementLodName(std::string& name) {
    if (name.empty()) return;
    name[name.size() - 1] = static_cast<char>(name[name.size() - 1] + 1);
}

struct LodChain {
    int count = 0;                        // VirModel.lod_count
    std::string names[kVirModelLods];     // names[i] is LOD i+1
    int distance[kVirModelLods] = {0};    // VirModel.lod_dist
};

// Builds the chain for `modelName`. `exists` must answer whether a model name
// resolves in the loaded set — the equivalent of ModelLod_Find returning
// non-NULL. Every step follows the oracle lines cited above.
//
// This is a port of VirModel_InitLods (src/model/model.c:145-163):
//
//     void VirModel_InitLods(VirModel *m) {
//         char name[32];
//         int i, n = 0;
//         m->unk24 = 0;
//         strcpy(name, m->name);
//         for (i = 1; i <= VIRMODEL_LODS; i++) {
//             m->lod_dist[i - 1] = (uint16_t)(100 / (i * i));
//             m->lods[i - 1] = ModelLod_Find(name);
//             if (!m->lods[i - 1]) break;
//             name[strlen(name) - 1]++;
//             n++;
//         }
//         m->lod_count = (uint16_t)n;
//     }
//
// The ORDER matters and is easy to get wrong: `lod_dist[i - 1]` is assigned
// BEFORE the find, so the distance at the index that terminated the loop is
// still written. Assigning the distance only after a successful find leaves
// `distance[count]` at 0 where the engine has 100/((count+1)^2).
template <class ExistsFn>
LodChain BuildLodChain(const std::string& modelName, ExistsFn&& exists) {
    LodChain chain;                        // VirModel is zeroed by Model_Init
    std::string name = modelName;          // strcpy(name, m->name)
    int n = 0;
    for (int i = 1; i <= kVirModelLods; ++i) {
        chain.distance[i - 1] = LodDistance(i);   // model.c:154, before the find
        if (!exists(name)) break;                 // model.c:157-158
        chain.names[i - 1] = name;                // m->lods[i-1] = ModelLod_Find(name)
        IncrementLodName(name);                   // model.c:160
        ++n;
    }
    chain.count = n;                       // m->lod_count = n
    return chain;
}

} // namespace lod
} // namespace igi

#endif // IGI_LOD_LOD_CHAIN_H
