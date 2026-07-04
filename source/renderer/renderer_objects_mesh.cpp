/******************************************************************************
 * @file    renderer_objects_mesh.cpp
 * @brief   Renderer_Objects: mesh load/caching, model-file lookup, extents
 *          Split from renderer_objects.cpp; shares renderer_objects_internal.h.
 *****************************************************************************/
#include "renderer_objects_internal.h"

float Renderer_Objects::GetMeshZOffset(const std::string& modelId, bool isBuilding) {
    std::string cacheKey = std::to_string(current_level_) + ":" + (isBuilding ? "building:" : "object:") + modelId;
    auto it = mesh_cache_.find(cacheKey);
    Mesh mesh;
    if (it != mesh_cache_.end()) {
        mesh = it->second;
    } else {
        mesh = GetOrLoadMesh(modelId, isBuilding);
    }

    return mesh.mainZOffset;
}

glm::vec3 Renderer_Objects::GetMeshExtents(const std::string& modelId, bool isBuilding) {
    Mesh mesh = GetOrLoadMesh(modelId, isBuilding);
    return mesh.halfExtents;
}

glm::vec3 Renderer_Objects::GetMeshCenter(const std::string& modelId, bool isBuilding) {
    Mesh mesh = GetOrLoadMesh(modelId, isBuilding);
    return mesh.center;
}

Mesh Renderer_Objects::GetOrLoadMesh(const std::string& modelId, bool isBuilding) {
    std::string cacheKey = std::to_string(current_level_) + ":" + (isBuilding ? "building:" : "object:") + modelId;
    Logger::Get().Log(LogLevel::DEBUG,
        "[Renderer_Objects] GetOrLoadMesh request cacheKey=" + cacheKey + " modelId=" + modelId);

    // Return cached mesh if already loaded
    auto it = mesh_cache_.find(cacheKey);
    if (it != mesh_cache_.end()) {
        Logger::Get().Log(LogLevel::DEBUG,
            "[Renderer_Objects] Cache hit for " + cacheKey +
            " vertexCount=" + std::to_string(it->second.vertexCount));
        return it->second;
    }

    // ── 1. Try in-memory .res index first (no disk extraction needed) ────────────
    {
        std::vector<uint8_t> meshBytes = FindMeshData(modelId);
        if (!meshBytes.empty()) {
            try {
                // Pre-populate ATTA cache from the bytes we already have so
                // LoadAttachmentsRecursive doesn't need to call FindMeshData again
                // for this model (avoids a second costly .res read under memory pressure).
                try {
                    ParsedGeometry geoForAtta = ParseMefFileFromMemory(meshBytes);
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
    // Try disk first (fast path).
    std::string diskPath = FindModelFile(modelId, isBuilding);
    if (!diskPath.empty()) return diskPath;

    // Check if already extracted this session.
    std::string tmpPath = (std::filesystem::temp_directory_path() / ("igi1ed_" + modelId + ".mef")).string();
    if (std::filesystem::exists(tmpPath)) return tmpPath;

    // Extract from res cache to temp.
    std::vector<uint8_t> bytes = FindMeshData(modelId);
    if (bytes.empty()) return "";

    std::ofstream f(tmpPath, std::ios::binary);
    if (!f) return "";
    f.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    f.close();
    tmp_mef_paths_.push_back(tmpPath);
    Logger::Get().Log(LogLevel::INFO, "[Renderer_Objects] Extracted MEF to temp: " + tmpPath);
    return tmpPath;
}

const ParsedGeometry* Renderer_Objects::GetOrLoadSkinGeometry(const std::string& modelId, bool isBuilding) {
    auto it = skin_geometry_cache_.find(modelId);
    if (it != skin_geometry_cache_.end()) return &it->second;

    ParsedGeometry geo;
    {
        std::vector<uint8_t> mefBytes = FindMeshData(modelId);
        if (!mefBytes.empty()) {
            try { geo = ParseMefFileFromMemory(mefBytes); } catch (...) {}
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
    if (geo.vertices.empty() || geo.bones.empty()) {
        Logger::Get().Log(LogLevel::WARNING, "[Anim] " + modelId + " has no skeletal vertex/bone data for skinning");
        return nullptr;
    }

    Logger::Get().Log(LogLevel::INFO, "[Anim] Loaded skin geometry for " + modelId + ": " +
        std::to_string(geo.vertices.size()) + " vertices, " + std::to_string(geo.bones.size()) + " bones");
    return &(skin_geometry_cache_[modelId] = std::move(geo));
}


// ─── InitSelectionBox ────────────────────────────────────────────────────────
