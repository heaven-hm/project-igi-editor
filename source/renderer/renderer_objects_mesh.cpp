/******************************************************************************
 * @file    renderer_objects_mesh.cpp
 * @brief   Renderer_Objects: mesh load/caching, model-file lookup, extents
 *          Split from renderer_objects.cpp; shares renderer_objects_internal.h.
 *****************************************************************************/
#include "renderer_objects_internal.h"
#include "../lod/lod_qvm_table.h"
#include "../lod/lod_settings.h"
#include "../lod/lod_select.h"
#include "../runtime/weather_visibility.h"

// ─── LOD virtual-model chains (issue #62) ────────────────────────────────────
// Four pieces, all of them the engine's:
//   1. an exact-name existence predicate over the editor's loaded model set
//      (ModelLod_Find's strcmp, IGI-RE src/model/model.c:165-178);
//   2. the chain itself (VirModel_InitLods' rule, src/model/model.c:145-163);
//   3. the per-frame switch — BOTH branches of `lod_select`
//      (src/lod/mesh3d.c:161-208), ported in source/lod/lod_select.h, which
//      needs the LOD meshes' MeshHeader center/sphere/complexity
//      (source/lod/mef_mesh_header.h) and the level-wide LODSettings table
//      (source/lod/lod_qvm_table.h);
//   4. `ModelObj_GetLOD`'s view-depth and bounding-radius inputs
//      (src/lod/mesh3d.c:209-227) as `igi::lod::RigidViewDepth` /
//      `igi::lod::MaxScale`.

bool Renderer_Objects::MeshNameResolves(const std::string& modelId) const {
    if (modelId.empty()) return false;
    const std::string fname = modelId + ".mef";

    // 1. The in-memory .res indexes — the same exact key FindMeshData reads, and
    //    the editor's equivalent of the engine's loaded ModelFile LOD lists.
    for (const auto& ri : res_model_indexes_) {
        if (ri.index.find(fname) != ri.index.end()) return true;
    }

    // 2. Exact on-disk `<name>.mef`, mirroring FindModelFile's EXACT search
    //    order only (level local, level IGI, other levels, common, flat).
    //    FindModelFile's fuzzy pass is deliberately not used here: it would
    //    resolve a name the engine's strcmp cannot.
    auto existsExact = [&](const std::string& dirStr) {
        if (dirStr.empty()) return false;
        std::error_code ec;
        return std::filesystem::exists(std::filesystem::path(dirStr) / fname, ec);
    };

    if (existsExact(Utils::GetExeDirectory() + "\\editor\\models\\level" +
                    std::to_string(current_level_))) return true;
    if (existsExact(Utils::GetIGIModelsPath(current_level_))) return true;
    for (int lvl = 1; lvl <= 14; ++lvl) {
        if (lvl == current_level_) continue;
        if (existsExact(Utils::GetExeDirectory() + "\\editor\\models\\level" +
                        std::to_string(lvl))) return true;
        if (existsExact(Utils::GetIGIModelsPath(lvl))) return true;
    }
    if (existsExact(Utils::GetExeDirectory() + "\\editor\\models\\common")) return true;
    if (existsExact(Utils::GetIGIRootPath() + "\\missions\\location0\\common\\models")) return true;
    if (existsExact(Utils::GetIGIRootPath() + "\\editor\\models")) return true;
    return false;
}

const igi::lod::LodChain& Renderer_Objects::GetLodChain(const std::string& modelId, bool isBuilding) {
    (void)isBuilding;  // existence is kind-independent; one chain per level+name
    const std::string key = std::to_string(current_level_) + ":" + modelId;
    auto it = lod_chain_cache_.find(key);
    if (it != lod_chain_cache_.end()) return it->second;

    igi::lod::LodChain chain = igi::lod::BuildLodChain(
        modelId, [this](const std::string& n) { return MeshNameResolves(n); });
    if (chain.count > 1) {
        std::string names;
        for (int i = 0; i < chain.count; ++i) {
            if (i) names += ", ";
            names += chain.names[i];
        }
        Logger::Get().Log(LogLevel::DEBUG, "[Renderer_Objects] LOD chain for '" + modelId +
            "' has " + std::to_string(chain.count) + " levels: " + names);
    }
    return lod_chain_cache_.emplace(key, std::move(chain)).first->second;
}

std::string Renderer_Objects::GetLodMeshName(const std::string& modelId, bool isBuilding, int level) {
    const igi::lod::LodChain& chain = GetLodChain(modelId, isBuilding);
    if (level < 0 || level >= chain.count) return std::string();
    return chain.names[level];
}

bool Renderer_Objects::GetAuthoredLodDistances(const std::string& modelId, float out[5]) {
    EnsurePortalDistancesLoaded();
    auto it = authored_lod_distances_.find(modelId);
    if (it == authored_lod_distances_.end()) return false;
    for (int i = 0; i < 5; ++i) out[i] = it->second[static_cast<size_t>(i)];
    return true;
}

namespace {

// The level-wide `LODSettings` task after `LODSettings_Changed`
// (IGI-RE src/animtrig/lodsettings.c:129-137): gLodRange, gLodBlend and the six
// gLod*Detail / gLod*Cull globals that `lod_select`'s complexity branch reads
// (src/lod/mesh3d.c:187,196,205). Read from <IGI>/lod.qvm, the same file
// EnsurePortalDistancesLoaded parses for the per-model tables.
//
// File-local because `Renderer_Objects` has no member for it yet — see the
// report: `renderer_objects.h` wants
// `igi::lod::LevelLodSettings level_lod_settings_;` plus
// `bool level_lod_settings_found_ = false;`. Re-parsed only when the resolved
// path changes, so the per-frame cost is one string compare.
struct LevelLodSettingsState {
    igi::lod::LevelLodSettings settings;
    bool found = false;  // a LODSettings task was present in lod.qvm
};

const LevelLodSettingsState& LevelLodSettingsForCurrentLevel() {
    static std::string loadedFrom;
    static LevelLodSettingsState state;

    const std::string qvmPath = Utils::GetIGIRootPath() + "\\lod.qvm";
    if (qvmPath == loadedFrom) return state;
    loadedFrom = qvmPath;
    state = LevelLodSettingsState{};

    if (!std::filesystem::exists(qvmPath)) return state;
    QVMFile qvm = QVM_Parse(qvmPath);
    if (!qvm.valid) return state;
    const std::string decompiled = QVM_DecompileToString(qvm);
    if (decompiled.empty()) return state;

    float raw[8] = {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
    if (!igi::lod::ParseLevelLodSettings(decompiled, raw)) {
        Logger::Get().Log(LogLevel::WARNING,
            "[Renderer_Objects] lod.qvm has no LODSettings task; unauthored models keep LOD 0");
        return state;
    }
    state.settings = igi::lod::ScaleLevelLodSettings(raw[0], raw[1], raw[2], raw[3],
                                                    raw[4], raw[5], raw[6], raw[7]);
    state.found = true;
    Logger::Get().Log(LogLevel::INFO,
        "[Renderer_Objects] Level LODSettings: rigidDetail=" + std::to_string(state.settings.rigidDetail) +
        " rigidCull=" + std::to_string(state.settings.rigidCull) +
        " blend=" + std::to_string(state.settings.blend) +
        " range=" + std::to_string(state.settings.range));
    return state;
}

std::vector<uint8_t> ReadFileBytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    const std::streamsize n = f.tellg();
    if (n <= 0) return {};
    std::vector<uint8_t> bytes(static_cast<size_t>(n));
    f.seekg(0, std::ios::beg);
    if (!f.read(reinterpret_cast<char*>(bytes.data()), n)) return {};
    return bytes;
}

}  // namespace

int Renderer_Objects::SelectLodForDistance(const std::string& modelId, bool isBuilding,
                                           float fov, float dist, float radius, float k) {
    // The chain is what `ModelObj_GetLOD` walks (IGI-RE src/lod/mesh3d.c:209-227).
    //
    // A model with NO chain must still be DRAWN, at LOD 0.  lod_select
    // (src/lod/mesh3d.c:161-208) with `lod_count == 0` computes n = 0, so both loops are
    // skipped, i is 0, and the cull test `i == n - 1` is `0 == -1`, i.e. false -- it falls
    // through to `return i`, which is 0.  ModelObj_GetLOD then hands 0 to the caller and
    // the draw uses vm->lods[0], the model's own mesh (:209-227).
    //
    // Returning -1 here instead culled every model the level's lod.qvm does not name.
    // That is most of them -- lod.qvm covers a few hundred of the corpus -- so it made the
    // viewport empty apart from the UI.  The -1 cases below are real (they need n >= 1),
    // but "no chain" is not one of them.
    GetLodChain(modelId, isBuilding);
    auto it = lod_chain_cache_.find(std::to_string(current_level_) + ":" + modelId);
    if (it == lod_chain_cache_.end() || it->second.count <= 0) return 0;
    igi::lod::LodChain& chain = it->second;

    // `lod_header(vm->lods[i])` — the three MeshHeader words both branches need.
    // Filled once per (level, model): the MESH header is not part of the editor's
    // ParsedGeometry, so it is read here (source/lod/mef_mesh_header.h).
    if (!chain.headersLoaded) {
        chain.headersLoaded = true;
        for (int i = 0; i < chain.count; ++i) {
            std::vector<uint8_t> bytes = FindMeshData(chain.names[i]);
            if (bytes.empty()) {
                const std::string path = GetOrExtractMefTemp(chain.names[i], isBuilding);
                if (!path.empty()) bytes = ReadFileBytes(path);
            }
            chain.header[i] = igi::lod::ReadMefMeshHeader(bytes);
            if (!chain.header[i].valid) {
                Logger::Get().Log(LogLevel::DEBUG, "[Renderer_Objects] LOD " + std::to_string(i) +
                    " of '" + modelId + "' (" + chain.names[i] + ") has no readable MESH header");
            }
        }
    }

    igi::lod::LodSelectInput vm;
    vm.lodCount = chain.count;

    // VirModel.unk24 / unk28: the authored ModelLODSettings table, monotonized
    // and x4096 by GetAuthoredLodDistances. A model the level's lod.qvm does not
    // name keeps unk24 == 0 and takes the complexity branch — which is the
    // common case, not an edge case.
    float authored[igi::lod::kVirModelLods] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    vm.hasAuthoredTable = GetAuthoredLodDistances(modelId, authored);
    if (vm.hasAuthoredTable) {
        for (int i = 0; i < igi::lod::kVirModelLods; ++i) vm.authoredDistance[i] = authored[i];
    }

    int complexity[igi::lod::kVirModelLods] = {0, 0, 0, 0, 0};
    const char* names[igi::lod::kVirModelLods] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    for (int i = 0; i < chain.count; ++i) {
        if (chain.header[i].valid) complexity[i] = chain.header[i].complexity;
        names[i] = chain.names[i].c_str();
    }
    vm.complexity = complexity;
    vm.lodNames = names;

    const LevelLodSettingsState& level = LevelLodSettingsForCurrentLevel();
    if (!vm.hasAuthoredTable && !level.found) {
        // `lod_select`'s complexity branch needs gLodRange, gLodBlend and
        // gLod*Rigid*Detail/Cull, which only exist once the level's LODSettings
        // task has been applied (LODSettings_Changed). With no such task the
        // engine's globals are 0, `thr[0] = kOne / (detail * e)` is +inf and
        // every unauthored rigid model is culled — reproducing that would blank
        // the level, so an unauthored model keeps LOD 0 instead. Reported.
        return 0;
    }
    const igi::lod::LodSelectResult r = igi::lod::LodSelect(
        vm, static_cast<long double>(dist), static_cast<long double>(radius),
        static_cast<long double>(k), level.settings.detail(igi::lod::LodKind::Rigid),
        level.settings.cull(igi::lod::LodKind::Rigid), fov, true, level.settings);

    if (!r.warning.empty()) {
        Logger::Get().Log(LogLevel::WARNING, "[Renderer_Objects] " + r.warning);
    }
    return r.lod;
}

// The mesh side of the engine's `.olm` accept test for one LOD
// (lightmapbuild.c:63-64). Every word comes out of the LOD's own `.mef`, read
// with the same resolution the LOD switch uses above: the archive entry
// Mesh_Load asks for -- "LOCAL:models/%s.mef" (src/model/model.c:364,
// FindMeshData) -- then the on-disk or extracted-to-temp copy.
//
//   MeshHeader.version       +0x00   (src/model/modeltypes.h:10)
//   MeshHeader.time[7]       +0x04   (src/model/modeltypes.h:11)
//   LightmapMesh.texCount    RD3D + 0x04, the format-3 `mh->data` view
//                                    (src/litmap/litmaptypes.h:139;
//                                    lightmapbuild.c:55, d3drender.c:225-230)
//
// A mesh that is not format 3, or that carries no RD3D chunk, has no lightmaps at
// all in the engine (`if (mh->format != 3) return NULL;`, lightmapbuild.c:53-54),
// so there is no file to accept and this returns false.
bool Renderer_Objects::GetLodOlmAcceptInput(const std::string& modelId, bool isBuilding, int level,
                                            igi::lightmap_rules::OlmAcceptInput* out) {
    if (out == nullptr) return false;
    const igi::lod::LodChain& chain = GetLodChain(modelId, isBuilding);
    if (level < 0 || level >= chain.count) return false;

    const std::string& lodName = chain.names[level];
    std::vector<uint8_t> bytes = FindMeshData(lodName);
    if (bytes.empty()) {
        const std::string path = GetOrExtractMefTemp(lodName, isBuilding);
        if (!path.empty()) bytes = ReadFileBytes(path);
    }
    if (bytes.empty()) {
        Logger::Get().Log(LogLevel::WARNING, "[Lightmap] LOD " + std::to_string(level) + " of '" +
            modelId + "' (" + lodName + ") has no readable .mef; the engine's .olm accept test "
            "cannot be run for it");
        return false;
    }

    const igi::lod::MefMeshHeader header = igi::lod::ReadMefMeshHeader(bytes);
    if (!header.valid || header.format != igi::lightmap_rules::kMeshFormatLightmapped ||
        !header.has_lightmap_mesh) {
        return false;
    }
    // `full` is NOT part of the accept rule: it is the caller's own rebuild flag,
    // and `return file;` sits inside `if (!full)` (lightmapbuild.c:65, :83). The
    // editor's load path is that `!full` branch -- it is loading the file, not
    // rebuilding it -- so it passes false.
    *out = igi::lightmap_rules::MakeOlmAcceptInput(header.version, header.time,
                                                   header.lightmap_tex_count, /*full=*/false);
    return true;
}

float Renderer_Objects::GetMeshZOffset(const std::string& modelId, bool isBuilding) {
    return GetOrLoadMesh(modelId, isBuilding).mainZOffset;
}

glm::vec3 Renderer_Objects::GetMeshExtents(const std::string& modelId, bool isBuilding) {
    return GetOrLoadMesh(modelId, isBuilding).halfExtents;
}

float Renderer_Objects::GetMeshRadius(const std::string& modelId, bool isBuilding) {
    return glm::length(GetMeshExtents(modelId, isBuilding));
}

float Renderer_Objects::GetMeshHeaderRadius(const std::string& modelId, bool isBuilding) {
    // VirModel_GetRadius 0x4D0950 (src/lod/mesh3d.c:381-387):
    //     if (vm && vm->lods[0]) return lod_header(vm->lods[0])->radius;
    //     return kOne;                                        /* mesh3d.c:88 */
    // `vm->lods[0]` is the REQUESTED name (VirModel_InitLods, src/model/model.c:145-163),
    // so the lookup key is the model id itself, and a model that never resolved
    // reports kOne = 1.0f. `radius` is MeshHeader + 0x78 (src/model/modeltypes.h:21),
    // a different word from the `sphere` the LOD distance test uses
    // (src/lod/mesh3d.c:224) -- see source/lod/mef_mesh_header.h.
    //
    // This is the value the pickup touch range is built from
    // (comprun/pickup.c:172-177 `VirModel_GetRadius(o->model) * max_scale`), so it
    // is cached per (level, model): the lookup reads the .res entry off disk and
    // the pickup pass asks once per placed pickup per tick.
    FillLevelModelKey(level_model_key_scratch_, current_level_, isBuilding, modelId);
    auto cached = mesh_header_radius_cache_.find(level_model_key_scratch_);
    if (cached != mesh_header_radius_cache_.end()) {
        return cached->second;
    }

    // Same two-step read the LOD header pass uses (GetLodMeshIndex above): the
    // in-memory .res index first, then the on-disk/temp-extracted .mef.
    std::vector<uint8_t> bytes = FindMeshData(modelId);
    if (bytes.empty()) {
        const std::string path = GetOrExtractMefTemp(modelId, isBuilding);
        if (!path.empty()) bytes = ReadFileBytes(path);
    }
    const igi::lod::MefMeshHeader header = igi::lod::ReadMefMeshHeader(bytes);
    const float radius = header.valid ? header.radius : 1.0f;  // kOne, mesh3d.c:88
    mesh_header_radius_cache_[level_model_key_scratch_] = radius;
    return radius;
}

const igi::lod::MefMeshHeader& Renderer_Objects::GetLod0MeshHeader(const std::string& modelId) {
    // The per-frame LOD switch in Renderer_Objects::Draw needs three words out of
    // LOD 0's MeshHeader -- `valid`, `center` and `sphere` (the reference point
    // and bounding radius of ModelObj_GetLOD, IGI-RE src/lod/mesh3d.c:220-224).
    // Reading them is a .res entry read: FindMeshData builds the archive-identity
    // key, walks the loaded indexes and RES_ReadEntry seeks and reads the whole
    // .mef off disk.  The draw path did that once per object per pass per frame,
    // so a level with 1416 placements paid ~2832 disk reads a frame for a value
    // that cannot change while the level is loaded.  Memoize it per (level,
    // model), exactly like the radius cache above and with the same lifetime.
    FillLevelModelKey(level_model_key_scratch_, current_level_, /*isBuilding=*/false, modelId);
    auto cached = lod0_mesh_header_cache_.find(level_model_key_scratch_);
    if (cached != lod0_mesh_header_cache_.end()) {
        return cached->second;
    }
    // Deliberately the same single-step read the draw path performed (no temp
    // .mef extraction fallback): this is pure memoization, so the returned words
    // are bit-identical to what the draw computed before.
    const igi::lod::MefMeshHeader header = igi::lod::ReadMefMeshHeader(FindMeshData(modelId));
    return lod0_mesh_header_cache_.emplace(level_model_key_scratch_, header).first->second;
}

glm::vec3 Renderer_Objects::GetMeshCenter(const std::string& modelId, bool isBuilding) {
    return GetOrLoadMesh(modelId, isBuilding).center;
}

const Mesh& Renderer_Objects::GetOrLoadMesh(const std::string& modelId, bool isBuilding) {
    // Resolve and cache this model's LOD chain (issue #62) as part of the mesh
    // lookup. The draw path reads it through GetLodChain / GetLodMeshName /
    // SelectLodForDistance; resolving it here means any model the editor can
    // load carries its chain (count 1 when it has no lower-detail sibling).
    (void)GetLodChain(modelId, isBuilding);

    FillLevelModelKey(level_model_key_scratch_, current_level_, isBuilding, modelId);
    auto it = mesh_cache_.find(level_model_key_scratch_);
    if (it != mesh_cache_.end()) {
        return it->second;
    }
    const std::string cacheKey = level_model_key_scratch_;

    // ── 1. Try in-memory .res index first (no disk extraction needed) ────────────
    {
        std::string meshRes;
        int meshResLevel = 0;
        std::vector<uint8_t> meshBytes = FindMeshData(modelId, &meshRes);
        if (meshBytes.empty()) {
            EnsureGlobalTextureMapLoaded();
            int targetLvl = -1;
            auto mit = model_level_map_.find(modelId);
            if (mit != model_level_map_.end() && mit->second != current_level_) {
                targetLvl = mit->second;
                LoadResCache(targetLvl, Utils::GetIGIRootPath());
                meshBytes = FindMeshData(modelId, &meshRes);
                if (!meshBytes.empty()) meshResLevel = targetLvl;
            }
            if (meshBytes.empty()) {
                for (int lvl = 1; lvl <= 14; ++lvl) {
                    if (lvl == current_level_ || lvl == targetLvl) continue;
                    LoadResCache(lvl, Utils::GetIGIRootPath());
                    meshBytes = FindMeshData(modelId, &meshRes);
                    if (!meshBytes.empty()) { meshResLevel = lvl; break; }
                }
            }
        }
        // The cross-level fallback is the editor's, not the engine's: name it.
        LogCrossLevelModelResolution(modelId, meshResLevel, meshRes);
        if (!meshBytes.empty()) {
            try {
                // Pre-populate ATTA cache from the bytes we already have so
                // LoadAttachmentsRecursive doesn't need to call FindMeshData again
                // for this model (avoids a second costly .res read under memory pressure).
                try {
                    ParsedGeometry geoForAtta = ParseMefFileFromMemory(meshBytes, modelId);
                    PrePopulateAttaFromParsed(modelId, isBuilding, geoForAtta.mefAttachments);
                } catch (...) {}

                Mesh mesh = loadObjModelFromMemory(meshBytes, modelId);
                ApplyTexturesToMesh(mesh, modelId);
                mesh_cache_[cacheKey] = mesh;
                Logger::Get().Log(LogLevel::DEBUG, "[Renderer_Objects] Loaded '" + modelId +
                    "' from ResCache (" + std::to_string(mesh.vertexCount) + " vertices)");
                std::unordered_set<std::string> visited;
                LoadAttachmentsRecursive(modelId, isBuilding, visited);
                return mesh_cache_[cacheKey];
            } catch (const std::exception& e) {
                Logger::Get().Log(LogLevel::WARNING, "[Renderer_Objects] ResCache parse failed for " +
                    modelId + ": " + e.what() + " — falling back to disk");
            }
        }
    }

    // ── 2. Fall back to disk file ─────────────────────────────────────────────
    std::string filepath = FindModelFile(modelId, isBuilding);
    if (filepath.empty()) {
        Logger::Get().Log(LogLevel::WARNING, "[Renderer_Objects] Model search FAILED for ID: " + modelId + ". Skipping render.");
        Mesh emptyMesh;
        emptyMesh.vertexCount = 0;
        mesh_cache_[cacheKey] = emptyMesh;
        return mesh_cache_[cacheKey];
    }
    Logger::Get().Log(LogLevel::DEBUG,
        "[Renderer_Objects] Resolved modelId=" + modelId + " to path=" + filepath);

    // Load and cache
    try {
        Mesh mesh = loadObjModel(filepath, "");
        ApplyTexturesToMesh(mesh, modelId);
        mesh_cache_[cacheKey] = mesh;
        Logger::Get().Log(LogLevel::DEBUG, "[Renderer_Objects] Success: Loaded model '" + modelId +
            "' from disk " + filepath + " (" + std::to_string(mesh.vertexCount) + " vertices)");

        // Recursively parse ATTA records for this model and all nested children.
        std::unordered_set<std::string> visited;
        LoadAttachmentsRecursive(modelId, isBuilding, visited);

        // If the hull has no texture but ATTA sub-models do, inherit the first available
        // ATTA texture for the hull — this covers vehicles/magic objects whose collision
        // shell has no DAT entry but whose interior parts share the same texture sheet.
        {
            Mesh& cached = mesh_cache_[cacheKey];
            bool hullHasTex = (cached.textureID > 0);
            if (!hullHasTex) {
                for (const auto& sub : cached.subMeshes) {
                    if (sub.textureID > 0) { hullHasTex = true; break; }
                }
            }
            if (!hullHasTex) {
                std::string prefix = isBuilding ? "building:" : "object:";
                std::string attKey = std::to_string(current_level_) + ":" + prefix + modelId;
                GLuint inheritedTex = 0;
                auto ait = attachment_cache_.find(attKey);
                if (ait != attachment_cache_.end()) {
                    for (const auto& att : ait->second) {
                        std::string subKey = std::to_string(current_level_) + ":" + prefix + att.modelId;
                        auto sit = mesh_cache_.find(subKey);
                        if (sit != mesh_cache_.end()) {
                            for (const auto& sub : sit->second.subMeshes) {
                                if (sub.textureID > 0) { inheritedTex = sub.textureID; break; }
                            }
                        }
                        if (inheritedTex) break;
                    }
                }
                if (inheritedTex) {
                    for (auto& sub : cached.subMeshes) sub.textureID = inheritedTex;
                    cached.textureID = inheritedTex;
                    Logger::Get().Log(LogLevel::INFO,
                        "[TEX Native] Hull '" + modelId + "' has no texture; inherited from ATTA sub-model glId=" + std::to_string(inheritedTex));
                }
            }
        }

        return mesh_cache_[cacheKey];

    } catch (const std::exception& e) {
        Logger::Get().Log(LogLevel::ERR, "[Renderer_Objects] Load FAILED for " + modelId + ": " + std::string(e.what()));
        Mesh emptyMesh;
        mesh_cache_[cacheKey] = emptyMesh;
        return mesh_cache_[cacheKey];
    }

}


std::string Renderer_Objects::FindModelFile(const std::string& modelId, bool isBuilding) {
    if (modelId.empty()) return "";

    // Helper: search one directory for exact modelId.mef match.
    auto searchOneDirExact = [&](const std::string& dirStr) -> std::string {
        if (dirStr.empty()) return "";
        std::filesystem::path modelsPath(dirStr);
        if (!std::filesystem::exists(modelsPath)) return "";

        // Exact match
        std::filesystem::path exactPath = modelsPath / (modelId + ".mef");
        if (std::filesystem::exists(exactPath)) return exactPath.string();
        return "";
    };

    // Helper: search one directory for fuzzy match by type prefix.
    auto searchOneDirFuzzy = [&](const std::string& dirStr) -> std::string {
        if (dirStr.empty()) return "";
        std::filesystem::path modelsPath(dirStr);
        if (!std::filesystem::exists(modelsPath)) return "";

        // Companion-part guard: IDs ending in _2 .. _9 (face, hands, legs, etc.)
        // must match exactly or not at all. The fuzzy fallback would otherwise
        // return the main body mesh (_01_1) for any missing companion part,
        // causing double-body / double-face rendering.
        {
            const size_t lastUs = modelId.rfind('_');
            if (lastUs != std::string::npos && lastUs + 1 < modelId.size()) {
                const char lastCh = modelId[lastUs + 1];
                if (lastCh >= '2' && lastCh <= '9') return "";
            }
        }

        // Fuzzy scan
        std::string bestMatch;
        for (const auto& entry : std::filesystem::directory_iterator(modelsPath)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".mef") continue;
            const std::string fname = entry.path().filename().string();
            const std::string stem  = entry.path().stem().string();

            // Exact id or "<id>_<lod/variant>" — boundary-aware so "009_01_1" never
            // matches "1009_01_1.mef" (a different model whose name contains the id).
            if (stem == modelId ||
                (stem.rfind(modelId, 0) == 0 && stem.size() > modelId.size() &&
                 stem[modelId.size()] == '_'))
                return entry.path().string();

            // Variation fallback: require type prefix at stem start (e.g. "003_" not "1003_")
            size_t firstUnderscore = modelId.find_first_of('_');
            if (firstUnderscore != std::string::npos) {
                const std::string typeId = modelId.substr(0, firstUnderscore);
                if (stem.rfind(typeId + "_", 0) == 0) {
                    if (bestMatch.empty() || stem.find(typeId + "_01") != std::string::npos) {
                        bestMatch = entry.path().string();
                    }
                }
            }
        }
        return bestMatch;
    };

    // ─── PHASE 1: EXACT MATCHES ───────────────────────────────────────────────
    
    // 1. Search local extracted models for current level
    const std::string exeModels = Utils::GetExeDirectory() + "\\editor\\models\\level" + std::to_string(current_level_);
    std::string result = searchOneDirExact(exeModels);
    if (!result.empty()) {
        Logger::Get().Log(LogLevel::DEBUG, "[Renderer_Objects] Model found locally (Exact): " + result);
        return result;
    }

    // 2. Fall back to game's native models directory for current level
    const std::string igiModels = Utils::GetIGIModelsPath(current_level_);
    result = searchOneDirExact(igiModels);
    if (!result.empty()) {
        Logger::Get().Log(LogLevel::DEBUG, "[Renderer_Objects] Model found in IGI root (Exact): " + result);
        return result;
    }

    // 3. Search other levels for exact match (cross-level references)
    for (int lvl = 1; lvl <= 14; ++lvl) {
        if (lvl == current_level_) continue;
        const std::string lvlLocal = Utils::GetExeDirectory() + "\\editor\\models\\level" + std::to_string(lvl);
        result = searchOneDirExact(lvlLocal);
        if (!result.empty()) {
            Logger::Get().Log(LogLevel::DEBUG, "[Renderer_Objects] Model found in level" + std::to_string(lvl) + " local (Exact): " + result);
            return result;
        }
        const std::string lvlIgi = Utils::GetIGIModelsPath(lvl);
        result = searchOneDirExact(lvlIgi);
        if (!result.empty()) {
            Logger::Get().Log(LogLevel::DEBUG, "[Renderer_Objects] Model found in level" + std::to_string(lvl) + " IGI (Exact): " + result);
            return result;
        }
    }

    // 4. Search common location0 assets
    const std::string commonLocal = Utils::GetExeDirectory() + "\\editor\\models\\common";
    result = searchOneDirExact(commonLocal);
    if (!result.empty()) {
        Logger::Get().Log(LogLevel::DEBUG, "[Renderer_Objects] Model found in common local (Exact): " + result);
        return result;
    }
    const std::string commonIgi = Utils::GetIGIRootPath() + "\\missions\\location0\\common\\models";
    result = searchOneDirExact(commonIgi);
    if (!result.empty()) {
        Logger::Get().Log(LogLevel::DEBUG, "[Renderer_Objects] Model found in common IGI (Exact): " + result);
        return result;
    }

    // 5. Search the flat IGI editor/models directory produced by --extract-level.
    //    All levels are extracted to a single flat dir (D:/IGI1/editor/models/) alongside
    //    the common/ and level1/ subdirs, so this covers levels 2-14 model MEFs.
    const std::string igiContentModels = Utils::GetIGIRootPath() + "\\editor\\models";
    result = searchOneDirExact(igiContentModels);
    if (!result.empty()) {
        Logger::Get().Log(LogLevel::DEBUG, "[Renderer_Objects] Model found in IGI editor/models flat dir (Exact): " + result);
        return result;
    }

    // ─── PHASE 2: FUZZY FALLBACK (Only run if exact match fails everywhere) ───

    // 1. Current level local
    result = searchOneDirFuzzy(exeModels);
    if (!result.empty()) {
        Logger::Get().Log(LogLevel::DEBUG, "[Renderer_Objects] Model found locally (Fuzzy): " + result);
        return result;
    }

    // 2. Current level IGI root
    result = searchOneDirFuzzy(igiModels);
    if (!result.empty()) {
        Logger::Get().Log(LogLevel::DEBUG, "[Renderer_Objects] Model found in IGI root (Fuzzy): " + result);
        return result;
    }

    // Lazy cross-level .res index: index that level's archives in memory so the
    // caller can try FindMeshData() before falling through to other-level disk scan.
    {
        EnsureGlobalTextureMapLoaded();
        auto mit = model_level_map_.find(modelId);
        if (mit != model_level_map_.end() && mit->second != current_level_) {
            int targetLvl = mit->second;
            Logger::Get().Log(LogLevel::INFO, "[Renderer_Objects] Lazy indexing level " +
                std::to_string(targetLvl) + " .res for cross-level model: " + modelId);
            LoadResCache(targetLvl, Utils::GetIGIRootPath());
        }
    }

    // 3. Other levels fuzzy
    for (int lvl = 1; lvl <= 14; ++lvl) {
        if (lvl == current_level_) continue;
        const std::string lvlLocal = Utils::GetExeDirectory() + "\\editor\\models\\level" + std::to_string(lvl);
        result = searchOneDirFuzzy(lvlLocal);
        if (!result.empty()) {
            Logger::Get().Log(LogLevel::DEBUG, "[Renderer_Objects] Model found in level" + std::to_string(lvl) + " local (Fuzzy): " + result);
            return result;
        }
        const std::string lvlIgi = Utils::GetIGIModelsPath(lvl);
        result = searchOneDirFuzzy(lvlIgi);
        if (!result.empty()) {
            Logger::Get().Log(LogLevel::DEBUG, "[Renderer_Objects] Model found in level" + std::to_string(lvl) + " IGI (Fuzzy): " + result);
            return result;
        }
    }

    // 4. Common fuzzy
    result = searchOneDirFuzzy(commonLocal);
    if (!result.empty()) {
        Logger::Get().Log(LogLevel::DEBUG, "[Renderer_Objects] Model found in common local (Fuzzy): " + result);
        return result;
    }
    result = searchOneDirFuzzy(commonIgi);
    if (!result.empty()) {
        Logger::Get().Log(LogLevel::DEBUG, "[Renderer_Objects] Model found in common IGI (Fuzzy): " + result);
        return result;
    }

    Logger::Get().Log(LogLevel::ERR,
        "[Renderer_Objects] Model NOT FOUND: '" + modelId +
        "' — searched level " + std::to_string(current_level_) +
        ", all other levels, and common folder.");
    return "";
}

std::string Renderer_Objects::GetOrExtractMefTemp(const std::string& modelId, bool isBuilding) {
    // The game resolves a model from the MOUNTED ARCHIVE, by resource identity: Mesh_Load
    // (HOOK 0x4CF390) asks for "LOCAL:models/%s.mef" (src/model/model.c:364), and the
    // resource system matches the folded full name (Resource_IsLoaded 0x4B5B90,
    // src/res/resource.c:136-138).  The engine has no directory scan at all.
    //
    // So the archive lookup runs FIRST.  The on-disk search is a convenience for content
    // extracted by --extract-level and must not decide whether a model exists: it used to
    // run first and logged an ERROR for every model the archive resolved perfectly well
    // (the run log shows "Model NOT FOUND: 001_02_1" immediately followed by
    // "Extracted MEF to temp: igi1ed_001_02_1.mef" -- the same model).

    // 1. The engine's path: the member out of the level's own archive, then cross-level.
    std::string tmpPath = (std::filesystem::temp_directory_path() / ("igi1ed_" + modelId + ".mef")).string();
    if (std::filesystem::exists(tmpPath)) return tmpPath;

    std::vector<uint8_t> bytes = FindMeshData(modelId);
    if (bytes.empty()) {
        EnsureGlobalTextureMapLoaded();
        int targetLvl = -1;
        std::string meshRes;
        int meshResLevel = 0;
        auto mit = model_level_map_.find(modelId);
        if (mit != model_level_map_.end() && mit->second != current_level_) {
            targetLvl = mit->second;
            LoadResCache(targetLvl, Utils::GetIGIRootPath());
            bytes = FindMeshData(modelId, &meshRes);
            if (!bytes.empty()) meshResLevel = targetLvl;
        }
        if (bytes.empty()) {
            for (int lvl = 1; lvl <= 14; ++lvl) {
                if (lvl == current_level_ || lvl == targetLvl) continue;
                LoadResCache(lvl, Utils::GetIGIRootPath());
                bytes = FindMeshData(modelId, &meshRes);
                if (!bytes.empty()) { meshResLevel = lvl; break; }
            }
        }
        LogCrossLevelModelResolution(modelId, meshResLevel, meshRes);
    }

    // 2. Only if no archive has it, fall back to a loose .mef on disk.  That
    //    fallback is not something the engine can do at all (Mesh_Load HOOK
    //    0x4CF390 resolves "LOCAL:models/%s.mef" out of the mounted archive,
    //    src/model/model.c:364), so say which file answered.
    if (bytes.empty()) {
        const std::string loose = FindModelFile(modelId, isBuilding);
        if (!loose.empty()) {
            Logger::Get().Log(LogLevel::DEBUG,
                "[Renderer_Objects] '" + modelId + "' not in any indexed archive; resolved from loose file " +
                loose + " (the engine has no disk lookup: Mesh_Load HOOK 0x4CF390, src/model/model.c:364)");
        }
        return loose;
    }

    std::ofstream f(tmpPath, std::ios::binary);
    if (!f) return "";
    f.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    f.close();
    tmp_mef_paths_.push_back(tmpPath);
    Logger::Get().Log(LogLevel::DEBUG, "[Renderer_Objects] Extracted MEF to temp: " + tmpPath);
    return tmpPath;
}

const ParsedGeometry* Renderer_Objects::GetOrLoadParsedGeometry(const std::string& modelId, bool isBuilding) {
    FillLevelModelKey(level_model_key_scratch_, current_level_, isBuilding, modelId);
    auto it = parsed_geometry_cache_.find(level_model_key_scratch_);
    if (it != parsed_geometry_cache_.end()) return &it->second;
    const std::string cacheKey = level_model_key_scratch_;

    ParsedGeometry geo;
    std::vector<uint8_t> meshBytes = FindMeshData(modelId);
    if (meshBytes.empty()) {
        EnsureGlobalTextureMapLoaded();
        int targetLvl = -1;
        std::string meshRes;
        int meshResLevel = 0;
        auto mit = model_level_map_.find(modelId);
        if (mit != model_level_map_.end() && mit->second != current_level_) {
            targetLvl = mit->second;
            LoadResCache(targetLvl, Utils::GetIGIRootPath());
            meshBytes = FindMeshData(modelId, &meshRes);
            if (!meshBytes.empty()) meshResLevel = targetLvl;
        }
        if (meshBytes.empty()) {
            for (int lvl = 1; lvl <= 14; ++lvl) {
                if (lvl == current_level_ || lvl == targetLvl) continue;
                LoadResCache(lvl, Utils::GetIGIRootPath());
                meshBytes = FindMeshData(modelId, &meshRes);
                if (!meshBytes.empty()) { meshResLevel = lvl; break; }
            }
        }
        LogCrossLevelModelResolution(modelId, meshResLevel, meshRes);
    }
    if (!meshBytes.empty()) {
        try { geo = ParseMefFileFromMemory(meshBytes, modelId); } catch (...) {}
    }
    if (geo.vertices.empty()) {
        const std::string filepath = FindModelFile(modelId, isBuilding);
        if (!filepath.empty()) {
            try { geo = ParseMefFile(filepath); } catch (...) {}
        }
    }
    if (geo.vertices.empty()) return nullptr;
    return &(parsed_geometry_cache_[cacheKey] = std::move(geo));
}

const ParsedGeometry* Renderer_Objects::GetOrLoadSkinGeometry(const std::string& modelId, bool isBuilding) {
    auto it = skin_geometry_cache_.find(modelId);
    if (it != skin_geometry_cache_.end()) return &it->second;

    ParsedGeometry geo;
    {
        std::vector<uint8_t> mefBytes = FindMeshData(modelId);
        if (mefBytes.empty()) {
            EnsureGlobalTextureMapLoaded();
            int targetLvl = -1;
            std::string meshRes;
            int meshResLevel = 0;
            auto mit = model_level_map_.find(modelId);
            if (mit != model_level_map_.end() && mit->second != current_level_) {
                targetLvl = mit->second;
                LoadResCache(targetLvl, Utils::GetIGIRootPath());
                mefBytes = FindMeshData(modelId, &meshRes);
                if (!mefBytes.empty()) meshResLevel = targetLvl;
            }
            if (mefBytes.empty()) {
                for (int lvl = 1; lvl <= 14; ++lvl) {
                    if (lvl == current_level_ || lvl == targetLvl) continue;
                    LoadResCache(lvl, Utils::GetIGIRootPath());
                    mefBytes = FindMeshData(modelId, &meshRes);
                    if (!mefBytes.empty()) { meshResLevel = lvl; break; }
                }
            }
            LogCrossLevelModelResolution(modelId, meshResLevel, meshRes);
        }
        if (!mefBytes.empty()) {
            try { geo = ParseMefFileFromMemory(mefBytes, modelId); } catch (...) {}
        }
        if (geo.vertices.empty()) {
            std::string filepath = FindModelFile(modelId, isBuilding);
            if (filepath.empty()) {
                Logger::Get().Log(LogLevel::WARNING, "[Anim] No .mef found for skin geometry: " + modelId);
                return nullptr;
            }
            try { geo = ParseMefFile(filepath); } catch (...) {}
        }
    }
    const size_t renderableTriangles = CountRenderableSkinnedTriangles(geo);
    if (renderableTriangles == 0) {
        Logger::Get().Log(LogLevel::WARNING, "[Anim] " + modelId +
            " has no complete skeletal triangles for skinning");
        return nullptr;
    }

    Logger::Get().Log(LogLevel::INFO, "[Anim] Loaded skin geometry for " + modelId + ": " +
        std::to_string(geo.vertices.size()) + " vertices, " + std::to_string(geo.bones.size()) +
        " bones, " + std::to_string(renderableTriangles) + " renderable triangles");
    return &(skin_geometry_cache_[modelId] = std::move(geo));
}

bool Renderer_Objects::IsCameraInsideBuildingBounds(const std::vector<LevelObject>& objects,
                                                     const glm::vec3& cameraPos) {
    for (const auto& obj : objects) {
        if (obj.deleted || !obj.isBuilding || obj.modelId.empty()) continue;
        const Mesh& mesh = GetOrLoadMesh(obj.modelId, true);
        if (mesh.vertexCount == 0) continue;

        const glm::vec3 delta = cameraPos - glm::vec3(obj.pos);
        const float cosZ = std::cos(-static_cast<float>(obj.rot.z));
        const float sinZ = std::sin(-static_cast<float>(obj.rot.z));
        glm::vec3 localPos(delta.x * cosZ - delta.y * sinZ,
                           delta.x * sinZ + delta.y * cosZ, delta.z);
        float totalScale = 40.96f * obj.scale;
        if (totalScale <= 0.0f) totalScale = 40.96f;
        localPos /= totalScale;

        const glm::vec3 minBound = mesh.center - mesh.halfExtents;
        const glm::vec3 maxBound = mesh.center + mesh.halfExtents;
        if (igi::IsWithinWeatherShelterFootprint(localPos.x, localPos.y,
                                                  minBound.x, maxBound.x,
                                                  minBound.y, maxBound.y)) {
            return true;
        }
    }
    return false;
}

// ─── InitSelectionBox ────────────────────────────────────────────────────────
