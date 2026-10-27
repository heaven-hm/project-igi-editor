/******************************************************************************
 * @file    renderer_objects_texture.cpp
 * @brief   Renderer_Objects: texture/DAT mapping, lookup, and application
 *          Split from renderer_objects.cpp; shares renderer_objects_internal.h.
 *****************************************************************************/
#include "renderer_objects_internal.h"

// True if a DAT texture id is flagged with an alpha-bearing pixel format
// (e.g. "009_09_1_argb8888"). These textures carry real per-texel transparency
// (sunglasses lenses, visors) that must be alpha-blended; the opaque pass would
// otherwise hard-cutout the lens (shader: discard when texColor.a < 0.5) and
// render it as a distorted black blob. RGB565/_r5g6b5 have no alpha.
//
// One definition of the suffix list lives in model_texture_resolution.cpp so the
// draw path and the (host-portable) policy helper cannot drift apart.
static bool TextureIdHasAlpha(const std::string& texId) {
    return TextureIdHasAlphaSuffix(texId);
}

std::string Renderer_Objects::GetLevelTexturesPath() const {
    return Utils::GetExeDirectory() + "\\editor\\textures\\level" + std::to_string(current_level_);
}

std::string Renderer_Objects::GetLevelTextureDatPath() const {
    // Try local extracted DAT first
    const std::string localDat = Utils::GetExeDirectory() +
        "\\editor\\textures\\level" + std::to_string(current_level_) +
        "\\level" + std::to_string(current_level_) + ".dat";
    if (std::filesystem::exists(localDat)) return localDat;

    // Fall back to the game's DAT in the IGI root
    const std::string root = Utils::GetIGIRootPath();
    return root + "\\missions\\location0\\level" + std::to_string(current_level_) +
        "\\level" + std::to_string(current_level_) + ".dat";
}

void Renderer_Objects::LoadDatIntoMap(const std::string& datPath, std::map<std::string, std::vector<std::string>>& outMap) {
    DATFile dat = DAT_Parse(datPath);
    if (!dat.valid) {
        Logger::Get().Log(LogLevel::WARNING, "[TEX Native] DAT_Parse failed for: " + datPath + " Error: " + dat.error);
        return;
    }

    for (const auto& m : dat.models) {
        // We use operator[] instead of emplace so that later definitions
        // (which might contain the actual textures) overwrite earlier empty/stale definitions.
        outMap[m.modelName] = m.textures;
    }
}

void Renderer_Objects::EnsureTextureMapLoaded() {
    if (texture_map_level_ == current_level_) return;

    model_texture_map_cache_.clear();
    texture_map_level_ = current_level_;

    const std::string datPath = GetLevelTextureDatPath();
    Logger::Get().Log(LogLevel::INFO, "[TEX Native] Loading DAT map from " + datPath);
    LoadDatIntoMap(datPath, model_texture_map_cache_);
    Logger::Get().Log(LogLevel::INFO, "[TEX Native] DAT map loaded level=" + std::to_string(current_level_) + " models=" + std::to_string(model_texture_map_cache_.size()));
}

void Renderer_Objects::EnsureGlobalTextureMapLoaded() const {
    if (global_texture_map_loaded_) return;
    global_texture_map_loaded_ = true;

    const std::string igiRoot = Utils::GetIGIRootPath();
    const std::string exeDir  = Utils::GetExeDirectory();

    for (int lvl = 1; lvl <= 14; ++lvl) {
        // Try local extracted DAT first, then game path
        std::string localDat = exeDir + "\\editor\\textures\\level" + std::to_string(lvl) +
                               "\\level" + std::to_string(lvl) + ".dat";
        std::string gameDat  = igiRoot + "\\missions\\location0\\level" + std::to_string(lvl) +
                               "\\level" + std::to_string(lvl) + ".dat";

        std::string datPath = "";
        if (std::filesystem::exists(localDat)) datPath = localDat;
        else if (std::filesystem::exists(gameDat)) datPath = gameDat;

        if (!datPath.empty()) {
            DATFile dat = DAT_Parse(datPath);
            if (dat.valid) {
                for (const auto& m : dat.models) {
                    texture_sources_.push_back(ModelTextureSource{lvl, m.modelName, m.textures});
                    if (global_texture_map_.find(m.modelName) == global_texture_map_.end()) {
                        global_texture_map_[m.modelName] = m.textures;
                        model_level_map_[m.modelName] = lvl;
                    }
                }
                for (const auto& t : dat.allTextures) {
                    texture_level_map_.emplace(t, lvl);
                }
            }
        }
    }

    // Also scan the common package (weapons, shared models not tied to any level)
    std::string commonDat = igiRoot + "\\missions\\location0\\common\\location0.dat";
    if (std::filesystem::exists(commonDat)) {
        DATFile dat = DAT_Parse(commonDat);
        if (dat.valid) {
            for (const auto& m : dat.models) {
                if (global_texture_map_.find(m.modelName) == global_texture_map_.end()) {
                    global_texture_map_[m.modelName] = m.textures;
                    texture_sources_.push_back(ModelTextureSource{0, m.modelName, m.textures});
                    model_level_map_.emplace(m.modelName, 0);
                }
            }
            for (const auto& t : dat.allTextures) {
                if (texture_level_map_.find(t) == texture_level_map_.end())
                    texture_level_map_[t] = 0;
            }
        }
    }

    Logger::Get().Log(LogLevel::INFO,
        "[TEX Native] Global DAT map loaded: " + std::to_string(global_texture_map_.size()) + " models across all levels");
}

std::vector<std::string> Renderer_Objects::GetTextureIdsForModel(const std::string& modelId,
                                                                 bool* foundInDat) {
    if (foundInDat != nullptr) *foundInDat = false;
    EnsureTextureMapLoaded();

    // 1. Current level exact match
    {
        auto it = model_texture_map_cache_.find(modelId);
        if (it != model_texture_map_cache_.end()) {
            // The DAT answered for this model. The list may be empty, and an empty
            // list is an ANSWER, not a miss: ModelFile_Setup gives a model exactly
            // the GTT-list of its own INST record (HOOK 0x4CF080, src/model/model.c:
            // 285-289,296-314), so n == 0 means the model has no textures and the
            // engine draws it with the texture stage off (src/render/render.c:524-531).
            if (foundInDat != nullptr) *foundInDat = true;
            return it->second;
        }
    }

    // 2. Global DAT search across all other levels exact match
    EnsureGlobalTextureMapLoaded();
    {
        auto levelIt = model_level_map_.find(modelId);
        if (levelIt != model_level_map_.end()) {
            const auto* source = FindExactTextureSource(texture_sources_, levelIt->second, modelId);
            if (source != nullptr) {
                if (foundInDat != nullptr) *foundInDat = true;
                return source->textures;
            }
        }
        auto it = global_texture_map_.find(modelId);
        if (it != global_texture_map_.end()) {
            if (foundInDat != nullptr) *foundInDat = true;
            return it->second;
        }
    }

    // 3. Sibling LOD fallback within same variant: NNN_VV_N -> NNN_VV_1 (e.g. 001_02_2 -> 001_02_1)
    {
        size_t p2 = modelId.rfind('_');
        if (p2 != std::string::npos && p2 > 0) {
            std::string sameVariantPrimary = modelId.substr(0, p2) + "_1";
            if (sameVariantPrimary != modelId) {
                auto it = model_texture_map_cache_.find(sameVariantPrimary);
                if (it != model_texture_map_cache_.end()) {
                    return it->second;
                }
                auto levelIt = model_level_map_.find(sameVariantPrimary);
                if (levelIt != model_level_map_.end()) {
                    const auto* source = FindExactTextureSource(
                        texture_sources_, levelIt->second, sameVariantPrimary);
                    if (source != nullptr) return source->textures;
                }
                auto git = global_texture_map_.find(sameVariantPrimary);
                if (git != global_texture_map_.end()) {
                    return git->second;
                }
            }
        }
    }

    // 4. Variant fallback within current level: NNN_XX_N -> NNN_01_1
    {
        size_t p1 = modelId.find('_');
        if (p1 != std::string::npos && p1 + 4 < modelId.size()) {
            std::string primary = modelId.substr(0, p1) + "_01_1";
            if (primary != modelId) {
                auto it = model_texture_map_cache_.find(primary);
                if (it != model_texture_map_cache_.end()) {
                    return it->second;
                }
            }
        }
    }

    // 5. Variant fallback in global DAT
    {
        size_t p1 = modelId.find('_');
        if (p1 != std::string::npos && p1 + 4 < modelId.size()) {
            std::string primary = modelId.substr(0, p1) + "_01_1";
            if (primary != modelId) {
                auto levelIt = model_level_map_.find(primary);
                if (levelIt != model_level_map_.end()) {
                    const auto* source = FindExactTextureSource(texture_sources_, levelIt->second, primary);
                    if (source != nullptr) return source->textures;
                }
                auto it = global_texture_map_.find(primary);
                if (it != global_texture_map_.end()) {
                    return it->second;
                }
            }
        }
    }

    // 5. Fallback to just the prefix (e.g. NNN_XX_X -> NNN)
    // AI models like "004_02_1" only have "004" mapped in the global DAT.
    {
        size_t p1 = modelId.find('_');
        if (p1 != std::string::npos) {
            std::string prefix = modelId.substr(0, p1);
            if (prefix != modelId) {
                auto it = model_texture_map_cache_.find(prefix);
                if (it != model_texture_map_cache_.end()) return it->second;
                
                auto git = global_texture_map_.find(prefix);
                if (git != global_texture_map_.end()) return git->second;
            }
        }
    }

    Logger::Get().Log(LogLevel::DEBUG, "[TEX Native] DAT miss (all sources) for modelId=" + modelId);
    return {};
}

std::vector<std::string> Renderer_Objects::GetTextureIdsForSourceModel(
    const std::string& modelId, int sourceLevel) {
    EnsureTextureMapLoaded();
    if (sourceLevel == current_level_) {
        const auto local = model_texture_map_cache_.find(modelId);
        if (local != model_texture_map_cache_.end()) return local->second;
    }
    EnsureGlobalTextureMapLoaded();
    const auto* source = FindExactTextureSource(texture_sources_, sourceLevel, modelId);
    return source != nullptr ? source->textures : std::vector<std::string>{};
}

std::string Renderer_Objects::FindTextureFile(const std::string& textureId) const {
    // Helper: search one directory for the texture file
    auto searchDir = [&](const std::filesystem::path& texturesPath) -> std::string {
        if (!std::filesystem::exists(texturesPath)) return "";

        const std::filesystem::path exactPath = texturesPath / (textureId + ".tex");
        if (std::filesystem::exists(exactPath)) return exactPath.string();

        for (const auto& entry : std::filesystem::directory_iterator(texturesPath)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".tex") continue;
            const std::string stem = entry.path().stem().string();
            // Match the exact id or one of the explicitly recognized pixel-format
            // suffixes. Numeric/model suffixes are part of the texture identity;
            // a loose underscore match can silently bind a different texture.
            const bool match = stem == textureId ||
                StripTextureFormatSuffix(stem) == textureId;
            if (match) {
                return entry.path().string();
            }
        }
        return "";
    };

    // 1. Search local extracted textures directory first
    std::string result = searchDir(std::filesystem::path(GetLevelTexturesPath()));
    if (!result.empty()) return result;

    // 2. Fall back to the game's own texture directory for this level
    const std::string igiTexDir = Utils::GetIGIRootPath() +
        "\\missions\\location0\\level" + std::to_string(current_level_) + "\\textures";
    result = searchDir(std::filesystem::path(igiTexDir));
    if (!result.empty()) {
        Logger::Get().Log(LogLevel::DEBUG,
            "[TEX Native] Found texture in IGI root level textures: " + result);
        return result;
    }

    // 3. Extracted common location0 textures (from location0.res)
    const std::string commonLocalTexDir = Utils::GetExeDirectory() + "\\editor\\textures\\common";
    result = searchDir(std::filesystem::path(commonLocalTexDir));
    if (!result.empty()) {
        Logger::Get().Log(LogLevel::DEBUG,
            "[TEX Native] Found texture in extracted common folder: " + result);
        return result;
    }

    // 4. Game common/shared textures folder (shared across all levels)
    const std::string igiCommonTexDir = Utils::GetIGIRootPath() + "\\textures";
    result = searchDir(std::filesystem::path(igiCommonTexDir));
    if (!result.empty()) {
        Logger::Get().Log(LogLevel::DEBUG,
            "[TEX Native] Found texture in IGI common folder: " + result);
        return result;
    }

    // 5. Game location0 common folder (shared across all location0 levels)
    const std::string igiLoc0CommonTexDir = Utils::GetIGIRootPath() + "\\missions\\location0\\common\\textures";
    result = searchDir(std::filesystem::path(igiLoc0CommonTexDir));
    if (!result.empty()) {
        Logger::Get().Log(LogLevel::DEBUG,
            "[TEX Native] Found texture in IGI location0 common folder: " + result);
        return result;
    }

    // Lazy on-demand index for cross-level texture references: extend the in-memory
    // .res index to include the target level's archives without any disk extraction.
    // Note: EnsureGlobalTextureMapLoaded/texture_level_map_ lookup is done by the
    // non-const GetOrLoadTexture caller before calling FindTextureFile for fallback.
    EnsureGlobalTextureMapLoaded();

    // 6. Search all other levels' extracted and game texture directories.
    //    Textures like "006_07_1" live in level 6's folder but are referenced by
    //    models appearing in other levels (e.g. 003_02_1 in level 7).
    const std::string exeDir2  = Utils::GetExeDirectory();
    const std::string igiRoot2 = Utils::GetIGIRootPath();
    for (int lvl = 1; lvl <= 14; ++lvl) {
        if (lvl == current_level_) continue;
        result = searchDir(exeDir2 + "\\editor\\textures\\level" + std::to_string(lvl));
        if (!result.empty()) return result;
        result = searchDir(igiRoot2 + "\\missions\\location0\\level" + std::to_string(lvl) + "\\textures");
        if (!result.empty()) return result;
    }

    // This helper is also a speculative loose-file probe used before packed
    // .res lookup. Do not report a hard error here: callers may still resolve
    // the texture from an archive and the final GetOrLoadTexture warning is
    // the authoritative missing-texture diagnostic.
    return "";
}

std::string Renderer_Objects::FindTextureFileInLevel(const std::string& textureId, int levelNo) const {
    auto searchDir = [&](const std::filesystem::path& texturesPath) -> std::string {
        if (!std::filesystem::exists(texturesPath)) return "";

        const std::filesystem::path exactPath = texturesPath / (textureId + ".tex");
        if (std::filesystem::exists(exactPath)) return exactPath.string();

        for (const auto& entry : std::filesystem::directory_iterator(texturesPath)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".tex") continue;
            const std::string stem = entry.path().stem().string();
            if (stem == textureId || StripTextureFormatSuffix(stem) == textureId) {
                return entry.path().string();
            }
        }
        return "";
    };

    if (levelNo > 0) {
        const std::string level = std::to_string(levelNo);
        std::string result = searchDir(Utils::GetExeDirectory() + "\\editor\\textures\\level" + level);
        if (!result.empty()) return result;
        result = searchDir(Utils::GetIGIRootPath() + "\\missions\\location0\\level" + level + "\\textures");
        if (!result.empty()) return result;
    } else {
        std::string result = searchDir(Utils::GetExeDirectory() + "\\editor\\textures\\common");
        if (!result.empty()) return result;
        result = searchDir(Utils::GetIGIRootPath() + "\\textures");
        if (!result.empty()) return result;
        result = searchDir(Utils::GetIGIRootPath() + "\\missions\\location0\\common\\textures");
        if (!result.empty()) return result;
    }
    return "";
}

// Resolution-only half of GetOrLoadTexture (its steps 1, 1b and 2): does this id
// resolve to texture bytes, from the archive the owning level's DAT entry points
// at, from any other indexed archive, or from a loose .tex on disk?  No decode,
// no GL upload.
//
// Existence is what this answers, and the archive set searched here is the one
// GetOrLoadTexture searches: step 1 orders the archives owner-first and calls
// FindPreviewTextureEntry over all of them, step 1b calls FindTextureData over all
// of them after lazily indexing the owner level, and step 2 falls back to
// FindTextureFile.  Order changes which copy wins, never whether one exists.
//
// Two callers ask this question: the smoke lane (through TextureResolvesForSmoke,
// which reports a level's missing ids) and ApplyTexturesToMesh, which must know
// whether the editor's opaque-body substitution for an AI character can actually
// be satisfied before it commits to that id.
bool Renderer_Objects::TextureIdResolves(const std::string& textureId) {
    if (textureId.empty()) return false;

    EnsureGlobalTextureMapLoaded();
    auto ownerIt = texture_level_map_.find(textureId);
    if (ownerIt == texture_level_map_.end()) {
        const std::string stripped = StripTextureFormatSuffix(textureId);
        if (stripped != textureId) ownerIt = texture_level_map_.find(stripped);
    }
    int targetLvl = -1;
    if (ownerIt != texture_level_map_.end() && ownerIt->second > 0 &&
        ownerIt->second != current_level_) {
        targetLvl = ownerIt->second;
        LoadResCache(targetLvl, Utils::GetIGIRootPath());
    }

    if (!FindTextureData(textureId).empty()) return true;
    for (int lvl = 1; lvl <= 14; ++lvl) {
        if (lvl == current_level_ || lvl == targetLvl) continue;
        LoadResCache(lvl, Utils::GetIGIRootPath());
        if (!FindTextureData(textureId).empty()) return true;
    }

    const std::string stripped = StripTextureFormatSuffix(textureId);
    if (!FindTextureFile(textureId).empty()) return true;
    if (stripped != textureId && !FindTextureFile(stripped).empty()) return true;
    return false;
}

GLuint Renderer_Objects::GetOrLoadTexture(const std::string& textureId) {
    if (textureId.empty()) return 0;

    const std::string cacheKey = std::to_string(current_level_) + ":" + textureId;
    auto it = texture_cache_.find(cacheKey);
    if (it != texture_cache_.end()) {
        return it->second;
    }

    // ── 1. Try in-memory .res index (preferred: no disk extraction) ───────────
    //
    // IGI-RE resolves a model's textures from the archive of the level that owns
    // the model file, and from no other archive: ModelFile_Setup loads
    // "%stextures/%s.res" for that level (src/model/model.c:261, HOOK(0x4CF080)),
    // ModelFile_LoadTextures turns the MTP_TEXF table of that same resource into
    // the model's texture table (src/model/model.c:450-461), and the GTT index is
    // applied straight to it (src/model/model.c:300-306). There is no cross-level
    // search and no name fallback in the game.
    //
    // Retail archives really do carry different images under one id, so the
    // owning level's archive is indexed and searched FIRST; an unrelated archive
    // that happens to be indexed earlier (the current level, the always-indexed
    // shared common archive) must not win. Measured on the retail install:
    //   001_02_1.tex  level7 = 128x128 (32800 B)   common = 128x32 (8224 B)
    //   001_05_1.tex  level7 =  8224 B             common = 4128 B
    //   001_04_1.tex  level7 = 32800 B             common = 4128 B
    //   018_03_1.tex  level7 = 65568 B             level4 = 4128 B
    {
        EnsureGlobalTextureMapLoaded();
        auto ownerIt = texture_level_map_.find(textureId);
        if (ownerIt == texture_level_map_.end()) {
            const std::string strippedOwner = StripTextureFormatSuffix(textureId);
            if (strippedOwner != textureId) ownerIt = texture_level_map_.find(strippedOwner);
        }
        std::string ownerRes;
        if (ownerIt != texture_level_map_.end() && ownerIt->second > 0) {
            if (ownerIt->second != current_level_) {
                LoadResCache(ownerIt->second, Utils::GetIGIRootPath());
            }
            ownerRes = Utils::GetIGIRootPath() + "\\missions\\location0\\level" +
                       std::to_string(ownerIt->second) + "\\textures\\level" +
                       std::to_string(ownerIt->second) + ".res";
        }

        std::vector<ModelSourceArchiveIndex> archives;
        archives.reserve(res_tex_indexes_.size());
        for (const auto& index : res_tex_indexes_) {
            archives.push_back({index.res_path, index.index});
        }
        archives = OrderTextureArchivesByOwner(archives, ownerRes);

        std::string srcRes;
        std::vector<uint8_t> bytes = FindPreviewTextureEntry(archives, textureId,
            [](const std::string& resPath, const ResEntryInfo& info) {
                return RES_ReadEntry(resPath, info);
            },
            &srcRes);
        if (!bytes.empty()) {
            pics_s pics{};
            if (Tex_LoadFromMemory(bytes.data(), bytes.size(), pics) && pics.pics_ && pics.num_pic_ > 0) {
                const pic_s* pic = pics.pics_;
                // Provenance only for the non-obvious case: the bytes came from an
                // archive that is not this level's own texture archive. The engine
                // resolves a model's textures from exactly one archive — the one of
                // the level that owns the model file (ModelFile_LoadTextures HOOK
                // 0x4CF790, src/model/model.c:467) — so a hit anywhere else is the
                // editor's fallback and has to be nameable from the log.
                const std::string levelTexRes = Utils::GetIGIRootPath() +
                    "\\missions\\location0\\level" + std::to_string(current_level_) +
                    "\\textures\\level" + std::to_string(current_level_) + ".res";
                if (!srcRes.empty() && !SameArchivePath(srcRes, levelTexRes)) {
                    Logger::Get().Log(LogLevel::DEBUG,
                        "[TEX] '" + textureId + "' resolved from archive " + srcRes +
                        ", not this level's own archive " + levelTexRes +
                        (ownerRes.empty() ? std::string() : (" (owner archive " + ownerRes + ")")) +
                        " — engine rule is the owner level's archive (ModelFile_LoadTextures HOOK 0x4CF790, src/model/model.c:467)");
                }
                Logger::Get().Log(LogLevel::INFO,
                    "[TEX] ResCache loaded " + textureId +
                    " " + std::to_string(pic->width_) + "x" + std::to_string(pic->height_));
                const GLuint tex = GL_RegisterTexture(pic, GL_REPEAT, GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, true);
                texture_cache_[cacheKey] = tex;
                Pic_FreePics(pics);
                return tex;
            }
            Pic_FreePics(pics);
        }
    }

    // ── 1b. Lazy cross-level .res index if texture not found in current level ────
    {
        EnsureGlobalTextureMapLoaded();
        int targetLvl = -1;
        auto tit = texture_level_map_.find(textureId);
        if (tit == texture_level_map_.end()) {
            const std::string stripped = StripTextureFormatSuffix(textureId);
            tit = texture_level_map_.find(stripped);
        }
        if (tit != texture_level_map_.end() && tit->second != current_level_) {
            targetLvl = tit->second;
            Logger::Get().Log(LogLevel::INFO, "[TEX] Lazy indexing level " +
                std::to_string(targetLvl) + " .res for cross-level: " + textureId);
            LoadResCache(targetLvl, Utils::GetIGIRootPath());
        }

        std::string xSrcRes;
        std::vector<uint8_t> xBytes = FindTextureData(textureId, &xSrcRes);
        if (xBytes.empty()) {
            // DAT files are not guaranteed to provide a complete owner map.
            // Search all other packed texture archives using the indexed reader.
            for (int lvl = 1; lvl <= 14 && xBytes.empty(); ++lvl) {
                if (lvl == current_level_ || lvl == targetLvl) continue;
                LoadResCache(lvl, Utils::GetIGIRootPath());
                xBytes = FindTextureData(textureId, &xSrcRes);
            }
        }

        // Provenance for the non-obvious case: the bytes came from an archive that
        // is not this level's own texture archive. The engine never does that — a
        // model's textures come from the archive of the level that owns the model
        // file (ModelFile_LoadTextures HOOK 0x4CF790, src/model/model.c:467;
        // ModelFile_Setup loads "%stextures/%s.res" for that level, :261, HOOK
        // 0x4CF080) — so name the archive that answered instead of leaving the
        // fallback silent.
        if (!xBytes.empty()) {
            const std::string levelTexRes = Utils::GetIGIRootPath() +
                "\\missions\\location0\\level" + std::to_string(current_level_) +
                "\\textures\\level" + std::to_string(current_level_) + ".res";
            if (!xSrcRes.empty() && !SameArchivePath(xSrcRes, levelTexRes)) {
                Logger::Get().Log(LogLevel::DEBUG,
                    "[TEX] '" + textureId + "' resolved from archive " + xSrcRes +
                    ", not this level's own archive " + levelTexRes +
                    " — engine rule is the owner level's archive (ModelFile_LoadTextures HOOK 0x4CF790, src/model/model.c:467)");
            }
        }

        if (!xBytes.empty()) {
            pics_s pics2{};
            if (Tex_LoadFromMemory(xBytes.data(), xBytes.size(), pics2) && pics2.pics_ && pics2.num_pic_ > 0) {
                const GLuint tex = GL_RegisterTexture(pics2.pics_, GL_REPEAT, GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, true);
                texture_cache_[cacheKey] = tex;
                Pic_FreePics(pics2);
                return tex;
            }
            Pic_FreePics(pics2);
        }
    }

    // ── 2. Fall back to on-disk search (game dir loose files) ─────────────────
    const std::string strippedId = StripTextureFormatSuffix(textureId);
    std::string texturePath = FindTextureFile(textureId);
    if (texturePath.empty() && strippedId != textureId) {
        texturePath = FindTextureFile(strippedId);
    }
    if (texturePath.empty()) {
        Logger::Get().Log(LogLevel::WARNING, "[TEX] Texture NOT FOUND: " + textureId);
        return 0;
    }

    pics_s pics{};
    if (!Tex_Load(texturePath.c_str(), pics) || !pics.pics_ || pics.num_pic_ <= 0) {
        Logger::Get().Log(LogLevel::ERR, "[TEX] Failed to decode: " + texturePath);
        Pic_FreePics(pics);
        return 0;
    }

    const pic_s* pic = pics.pics_;
    Logger::Get().Log(LogLevel::INFO,
        "[TEX] Disk loaded " + textureId +
        " " + std::to_string(pic->width_) + "x" + std::to_string(pic->height_) +
        " path=" + texturePath);

    const GLuint texture = GL_RegisterTexture(pic, GL_REPEAT, GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, true);
    texture_cache_[cacheKey] = texture;
    Pic_FreePics(pics);
    return texture;
}

// ─── GetOrLoadGameTexture ─────────────────────────────────────────────────────
//
// The game's SHARED textures — the ones registered by name with Game_AddTexture
// (src/game/game.c:835-848, HOOK 0x416930). RainEffect_Init registers "snow.tex"
// this way, once, for both weather modes (src/fxobj/rain.c:82); Game_LoadTextures
// then walks every registered name and loads it as "LOCAL:common/textures/<name>"
// (src/game/game.c:524-534, HOOK 0x416130); Texture_LoadFile resolves that name
// through the resource list (src/file/texture.c:231-252, HOOK 0x4B24C0). The list
// was filled when the game mounted the pack
//   Resource_Load("LOCAL:common/textures/textures.res")  (src/game/game.c:430)
// whose members carry exactly that identity — measured on the retail install, the
// pack holds the member "LOCAL:common/textures/snow.tex" (1396 bytes).
//
// This is NOT the model-texture path. A model's texture is
// "LOCAL:textures/<id>.tex" (ModelFile_LoadTextures, src/model/model.c:467), which
// GetOrLoadTexture above owns, and the two namespaces are disjoint: no member of
// this pack can be reached by a model-texture lookup and vice versa. So this
// method has its own cache and its own archive index instead of joining
// res_tex_indexes_, whose owner-archive ordering the model path depends on.
GLuint Renderer_Objects::GetOrLoadGameTexture(const std::string& name) {
    if (name.empty()) return 0;

    // A cached 0 is a cached failure: the pack is a global, level-independent
    // archive, so a miss is not expected to become a hit later in the session.
    // ClearCaches() drops the cache, which is what a level switch or a game-path
    // change goes through.
    auto cached = game_texture_cache_.find(name);
    if (cached != game_texture_cache_.end()) return cached->second;

    // The path the engine's own "LOCAL:" device would open (LocalDevice_Open strips
    // the prefix, src/sysmisc/localdev.c:162-205); LocalDevicePath keeps that
    // expression in one place so a test can check it against the retail install.
    const std::string packPath =
        igi_res::LocalDevicePath(Utils::GetIGIRootPath(), igi_res::kGameTexturePackName);
    if (packPath != game_texture_pack_path_) {
        game_texture_pack_index_.clear();
        game_texture_pack_indexed_ = false;
        game_texture_pack_path_ = packPath;
    }
    if (!game_texture_pack_indexed_) {
        std::string error;
        if (std::filesystem::exists(packPath) &&
            RES_BuildIndex(packPath, game_texture_pack_index_, error)) {
            Logger::Get().Log(LogLevel::INFO, "[TEX] Game texture pack indexed: " + packPath +
                " (" + std::to_string(game_texture_pack_index_.size()) + " members)");
        } else if (!error.empty()) {
            Logger::Get().Log(LogLevel::WARNING, "[TEX] Game texture pack index failed: " + error);
        }
        game_texture_pack_indexed_ = true;
    }

    // 1. the mounted pack, by the engine's own member identity
    const std::string member = igi_res::GameTextureMemberName(name);
    ResEntryInfo info{};
    if (RES_FindIndexEntry(game_texture_pack_index_, member, info)) {
        std::vector<uint8_t> bytes = RES_ReadEntry(packPath, info);
        pics_s pics{};
        if (!bytes.empty() && Tex_LoadFromMemory(bytes.data(), bytes.size(), pics) &&
            pics.pics_ && pics.num_pic_ > 0) {
            const pic_s* pic = pics.pics_;
            Logger::Get().Log(LogLevel::INFO, "[TEX] Game texture " + name + " loaded as " +
                member + " from " + packPath + " " + std::to_string(pic->width_) + "x" +
                std::to_string(pic->height_));
            const GLuint tex =
                GL_RegisterTexture(pic, GL_REPEAT, GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, true);
            Pic_FreePics(pics);
            game_texture_cache_[name] = tex;
            return tex;
        }
        Pic_FreePics(pics);
        Logger::Get().Log(LogLevel::WARNING, "[TEX] Game texture " + name + " is present as " +
            member + " in " + packPath + " but failed to decode");
    }

    // 2. a loose file at the engine's own LOCAL path. Resource_Load falls back to
    //    File_Load when the resource list does not carry the name (src/res/resource.c:259-266)
    //    and the LOCAL device strips its prefix before the CRT open
    //    (LocalDevice_Open, src/sysmisc/localdev.c:162-205), so the on-disk path is
    //    <IGI>/common/textures/<name>.
    const std::string loosePath =
        igi_res::LocalDevicePath(Utils::GetIGIRootPath(), member);
    pics_s pics{};
    if (Tex_Load(loosePath.c_str(), pics) && pics.pics_ && pics.num_pic_ > 0) {
        const pic_s* pic = pics.pics_;
        Logger::Get().Log(LogLevel::INFO, "[TEX] Game texture " + name +
            " loaded from loose file " + loosePath + " " + std::to_string(pic->width_) + "x" +
            std::to_string(pic->height_));
        const GLuint tex =
            GL_RegisterTexture(pic, GL_REPEAT, GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, true);
        Pic_FreePics(pics);
        game_texture_cache_[name] = tex;
        return tex;
    }
    Pic_FreePics(pics);

    // The engine treats this as fatal (Texture_LoadFile -> Error_Show, texture.c:245-249);
    // the editor reports it and lets the caller use the engine's null-texture branch.
    Logger::Get().Log(LogLevel::WARNING,
        "[TEX] Game texture NOT FOUND: " + name + " (" + member + "; pack " + packPath + ")");
    game_texture_cache_[name] = 0;
    return 0;
}

void Renderer_Objects::ApplyTexturesToMesh(Mesh& mesh, const std::string& modelId, const std::string& parentModelId) {
    bool modelFoundInDat = false;
    std::vector<std::string> textureIds = GetTextureIdsForModel(modelId, &modelFoundInDat);
    bool isDatMiss = textureIds.empty();

    // The model whose DAT entry supplied the list above. Inherited lists belong
    // to the model they were copied from, and that model's level owns the
    // archive that must supply the images (see the owner-archive block below).
    std::string textureSourceModelId = modelId;

    // When a sub-model has no DAT entry, IGI ATTA sub-models commonly share material slots with their
    // parent building, so inherit the parent's texture list in that case.
    if (!parentModelId.empty() && isDatMiss) {
        const std::vector<std::string> parentIds = GetTextureIdsForModel(parentModelId);
        const bool parentHasRealEntry = !parentIds.empty();
        if (parentHasRealEntry) {
            Logger::Get().Log(LogLevel::INFO,
                "[TEX Native] Sub-model '" + modelId + "' has no DAT entry; inheriting " +
                std::to_string(parentIds.size()) + " texture(s) from parent '" + parentModelId + "'");
            textureIds = parentIds;
            textureSourceModelId = parentModelId;
            isDatMiss = false;
        }
    }

    // Conversely, some vehicles or magic models (_01_1) have no textures in DAT, but their interior 
    // attachments (_02_1) DO have textures. If we still have a DAT miss, inherit from the typical child.
    if (isDatMiss) {
        size_t p1 = modelId.find("_01_1");
        if (p1 != std::string::npos) {
            std::string childId = modelId.substr(0, p1) + "_02_1";
            std::vector<std::string> childTexIds = GetTextureIdsForModel(childId);
            if (!childTexIds.empty()) {
                Logger::Get().Log(LogLevel::INFO,
                    "[TEX Native] Model '" + modelId + "' has no DAT entry; inheriting " +
                    std::to_string(childTexIds.size()) + " texture(s) from child '" + childId + "'");
                textureIds = childTexIds;
                textureSourceModelId = childId;
                isDatMiss = false;
            }
        }
    }

    // If it's still a DAT miss (not explicitly in dat), fallback to self name first.
    //
    // That guess is only allowed when the DAT did NOT answer for this model. A DAT
    // entry with an empty list is the engine's own "this model has no textures"
    // statement — the model's MTP INST record has n == 0 (ModelFile_Setup,
    // HOOK 0x4CF080, src/model/model.c:285-289,296-314) and every one of its REND
    // groups carries g->texture == 0xFFFF, which the engine draws with the texture
    // stage off (src/render/render.c:524-531). Guessing "<model name>" as a texture
    // id in that case invents a lookup the game never performs and reported, for
    // example, "[TEX] Texture NOT FOUND: waypoint" for every level's marker model.
    if (isDatMiss && ModelNameIsTextureIdFallbackAllowed(modelFoundInDat)) {
        textureIds = { modelId };
    }

    if (textureIds.empty()) {
        if (modelFoundInDat) {
            // Correct outcome, not a gap: the engine binds nothing for a model whose
            // texture table is empty (see above). Logged at INFO so it is
            // distinguishable from a texture that really is absent from the data.
            Logger::Get().Log(LogLevel::INFO,
                "[TEX Native] Model '" + modelId + "' has no textures in the DAT "
                "(engine: empty MTP INST record) — binding nothing");
        } else {
            Logger::Get().Log(LogLevel::INFO, "[TEX Native] No textures mapped for modelId=" + modelId);
        }
        return;
    }

    // Index the archive of the level that owns this model before any lookup.
    //
    // IGI-RE: a model's textures come from exactly one archive, the one of the
    // level the model file belongs to. ModelFile_Setup loads
    // "%stextures/%s.res" (src/model/model.c:261, HOOK(0x4CF080)), the MTP_TEXF
    // table read from that same resource is the model's texture table
    // (src/model/model.c:450-461), and the GTT index is applied directly to it
    // (src/model/model.c:300-306). There is no cross-level search in the game.
    //
    // The editor searches every archive it has indexed, and the shared common
    // archive is always indexed (LoadResCache). Retail archives really do carry
    // different images under one id: measured, 001_02_1.tex is 128x32 in
    // common/textures/location0.res and 128x128 in level7/textures/level7.res,
    // so a preview of the level-7 model from any other level used to bind the
    // common strip. Indexing the owning level here makes the level's copy win
    // (FindPreviewTextureEntry searches non-common archives before common).
    {
        EnsureGlobalTextureMapLoaded();
        auto ownerIt = model_level_map_.find(textureSourceModelId);
        if (ownerIt != model_level_map_.end() && ownerIt->second > 0 &&
            ownerIt->second != current_level_) {
            LoadResCache(ownerIt->second, Utils::GetIGIRootPath());
        }
    }

    // For AI character models (sunglasses-bearing), load the STRIPPED texture id
    // so "009_09_1_argb8888" → "009_09_1.tex" (opaque body texture → solid black
    // glasses). The _argb8888 variant is a tiny 32×32 semi-transparent overlay
    // used by the game engine; loading it in the editor gives barely-visible lenses.
    // Non-AI models (guard tower, lattice) need the exact _argb8888.tex for
    // transparent rendering and keep the "exact first" preference.
    //
    // The strip is an editor-only look, never an engine mapping: the engine loads
    // exactly the id the model's own MTP TEXF table names
    // (ModelFile_LoadTextures, HOOK 0x4CF790, src/model/model.c:452-470). It is
    // therefore applied ONLY when the opaque id actually exists. When it does not,
    // the engine's own suffixed id is the one texture that model can be drawn
    // with — retail 015_01_1 (AITYPE_ANYA) names "000_11_1_argb8888", which
    // exists, while "000_11_1" exists nowhere in the install; stripping
    // unconditionally asked for the missing one ("[TEX] Texture NOT FOUND:
    // 000_11_1") and left that model's 8th render group untextured.
    EnsureAiModelIdsLoaded();
    const bool isAiModel = ai_model_ids_.count(modelId) > 0;

    std::vector<GLuint> textures;
    textures.reserve(textureIds.size());
    for (const auto& textureId : textureIds) {
        const std::string loadId = ResolveAiModelTextureId(textureId, isAiModel,
            [this](const std::string& candidate) { return TextureIdResolves(candidate); });
        textures.push_back(GetOrLoadTexture(loadId));
    }

    if (!mesh.subMeshes.empty()) {
        size_t assigned = 0;
        size_t untextured = 0;   // groups whose engine slot is 0xFFFF (no texture)
        size_t outOfRange = 0;   // groups whose engine slot has no texture in the list

        // Find the best valid (non-zero) texture to use as fallback for submeshes
        // that fall outside the DAT texture list range.
        GLuint fallbackTexture = 0;
        for (const GLuint t : textures) {
            if (t != 0) { fallbackTexture = t; }
        }

        for (size_t i = 0; i < mesh.subMeshes.size(); ++i) {
            GLuint texture = 0;
            const int matSlot = mesh.subMeshes[i].materialSlot;
            std::string resolvedTexId; // tracks the actual texture id string for ARGB detection

            if (matSlot >= 0 && static_cast<size_t>(matSlot) >= textures.size())
                ++outOfRange;

            // ── The engine selects a group's texture by the group's OWN index ──
            //
            // IGI-RE: a render group carries its own texture index and the engine
            // binds the LOD texture at that index — never the group's ordinal
            // position in the group chain:
            //   lod_texture(l, i) => ((Texture *)l->textures[i])->surface
            //       src/render/render.c:216-219
            //   draw_group: if (g->texture != 0xFFFF)
            //                   SetTexture(DEV, 0, lod_texture(ml, g->texture));
            //       src/render/render.c:524-525 (spline :733-734, lightmapped
            //       :1029-1033 and :1061-1062, sorted spline :1210-1211)
            // The index word is MeshGroup.texture for formats 0/1
            // (src/render/rendertypes.h:10) and LmGroup.texture for format 3
            // (src/render/render.c:44); mef_native.cpp:1053 surfaces it unchanged as
            // SubMesh::materialSlot.
            //
            // The list it indexes is ModelLod::textures — the per-mesh list
            // ModelFile_Setup built from that mesh's MTP INST record and its GTT
            // entries (src/model/model.c:286-314, HOOK 0x4CF080; modeltypes.h:44-45).
            // The editor's DAT list is that same list in that same order: verified
            // against the retail MTPs, all 1334 INST records of LEVEL1/4/7 resolve,
            // through the engine's own VNAM+INST+GTT walk, to the DAT order.
            //
            // 0xFFFF is NOT "no index given, take the next one": it is the engine's
            // untextured group, drawn with the texture stage off —
            //   SetTexture(DEV, 0, NULL); TSS(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
            //   src/render/render.c:524-531 (spline :1210-1213)
            // so such a group must stay untextured here. mef_native.cpp:1053 maps the
            // 0xFFFF word to materialSlot == -1, and the draw path renders
            // textureID == 0 untextured (renderer_objects.cpp:1314-1328).
            if (matSlot < 0) {
                if (mesh.fromRenderMesh) {
                    // Engine-untextured group (g->texture == 0xFFFF): bind nothing.
                    ++untextured;
                } else {
                    // Editor-only collision fallback: this mesh has no REND groups at
                    // all, the geometry was fabricated from the collision chunks
                    // (mef_native.cpp:1136-1143) and the engine draws nothing for it.
                    // There is no g->texture to honour, so keep the preview visible
                    // the way it has always been. This is the only place a positional
                    // pick can happen and it is not an engine mapping (measured: 8 of
                    // 559 retail MEFs, all one submesh, e.g. 700_08_1).
                    const size_t preview = std::min(i, textures.size() - 1);
                    texture = textures[preview];
                    resolvedTexId = (preview < textureIds.size()) ? textureIds[preview] : "";
                }
            } else if (textures.size() == 1) {
                texture = textures[0];
                resolvedTexId = textureIds.empty() ? "" : textureIds[0];
            } else if (matSlot >= 0 && static_cast<size_t>(matSlot) < textures.size()) {
                texture = textures[matSlot];
                resolvedTexId = (static_cast<size_t>(matSlot) < textureIds.size()) ? textureIds[matSlot] : "";
            } else if (matSlot > 0 && !textures.empty()) {
                EnsureAiModelIdsLoaded();
                if (ai_model_ids_.count(modelId) > 0) {
                    // materialSlot is out of range — sunglasses-bearing characters (009_01_1,
                    // 014_01_1, 014_02_1) where the MEF block references a slot beyond the
                    // local DAT texture list. Resolve via global DAT.
                    bool resolved = false;
                    EnsureGlobalTextureMapLoaded();
                    {
                        auto git = global_texture_map_.find(modelId);
                        if (git != global_texture_map_.end() &&
                            static_cast<size_t>(matSlot) < git->second.size()) {
                            const std::string& globalTexId = git->second[static_cast<size_t>(matSlot)];
                            GLuint globalTex = GetOrLoadTexture(globalTexId);
                            if (globalTex) {
                                texture = globalTex;
                                resolvedTexId = globalTexId; // preserve for ARGB detection
                                resolved = true;
                                Logger::Get().Log(LogLevel::INFO,
                                    "[TEX Native] AI materialSlot out of range resolved via global DAT for modelId=" + modelId +
                                    " submeshIndex=" + std::to_string(i) +
                                    " materialSlot=" + std::to_string(matSlot) +
                                    " globalTexId=" + globalTexId);
                            }
                        }
                    }
                    if (!resolved) {
                        texture = textures.back();
                        resolvedTexId = textureIds.empty() ? "" : textureIds.back();
                        Logger::Get().Log(LogLevel::WARNING,
                            "[TEX Native] AI materialSlot out of range, using last texture for modelId=" + modelId +
                            " submeshIndex=" + std::to_string(i) +
                            " materialSlot=" + std::to_string(matSlot) +
                            " textureCount=" + std::to_string(textures.size()));
                    }
                } else {
                    size_t wrapped = static_cast<size_t>(matSlot) % textures.size();
                    texture = textures[wrapped];
                    resolvedTexId = (wrapped < textureIds.size()) ? textureIds[wrapped] : "";
                    Logger::Get().Log(LogLevel::WARNING,
                        "[TEX Native] materialSlot out of range, wrapping for modelId=" + modelId +
                        " submeshIndex=" + std::to_string(i) +
                        " materialSlot=" + std::to_string(matSlot) +
                        " textureCount=" + std::to_string(textures.size()) +
                        " wrappedSlot=" + std::to_string(wrapped));
                }
            } else {
                // The engine index has no texture in the editor's list, so the DAT
                // list is shorter than the mesh's MTP INST/GTT list. Counted above;
                // keep the last valid texture instead of binding an unrelated one.
                texture = fallbackTexture;
            }

            mesh.subMeshes[i].textureID = texture;
            mesh.subMeshes[i].textureName = resolvedTexId;
            if (texture) {
                ++assigned;
                // ARGB-format textures carry real per-texel alpha (sunglasses lenses,
                // guard tower lattice, wire fences, etc.). Use resolvedTexId which
                // captures the actual texture id in ALL code paths including out-of-range
                // ARGB textures on non-AI models (lattice, fences, decals) need alpha
                // blending via the transparent pass. AI model ARGB sub-meshes (sunglasses)
                // must render OPAQUE with the cutout shader so the black lens pixels show
                // as solid black — blending them makes them see-through.
                if (TextureIdHasAlpha(resolvedTexId) && ai_model_ids_.count(modelId) == 0) {
                    mesh.subMeshes[i].alphaMode = 2;
                    mesh.subMeshes[i].baseColorFactor.a = 0.95f;
                }
            }
        }

        if (!mesh.subMeshes.empty()) {
            mesh.textureID = mesh.subMeshes.front().textureID;
            mesh.textureName = mesh.subMeshes.front().textureName;
        }

        Logger::Get().Log(
            LogLevel::INFO,
            "[TEX Native] Applied textures to modelId=" + modelId +
            " subMeshes=" + std::to_string(mesh.subMeshes.size()) +
            " datTextures=" + std::to_string(textureIds.size()) +
            " assigned=" + std::to_string(assigned) +
            " untextured=" + std::to_string(untextured) +
            " outOfRange=" + std::to_string(outOfRange));

        // IGI-RE: a model's render-group count and its texture-list length are
        // independent, so a difference between them is NOT an error.
        // ModelFile_Setup gives one ModelLod per MTP INST record, and that record's
        // `n` is the number of GTT-indexed textures of the mesh
        // (src/model/model.c:285-289,296-314, HOOK 0x4CF080); the MEF's REND chain is
        // a free-form group list that shares those slots and carries no count at all
        // — it is walked link by link until g->next == 0xFFFF, formats 0/1 at
        // src/render/render.c:1009-1011 and format 3 at :1045-1047. Measured
        // on the retail install: 472_02_1 has 142 groups over 34 slots, 700_08_1 has
        // 1 group over 15 slots, and 114 of 551 extracted models differ — every one of
        // them renders correctly in the game. The engine-impossible case is a group
        // whose slot is outside the model's texture list (the engine cannot express
        // it: ModelLod::count bounds the index), which in the editor means the DAT
        // list is shorter than the mesh's INST/GTT list. Only that is reported.
        if (outOfRange > 0) {
            Logger::Get().Log(
                LogLevel::WARNING,
                "[TEX Native] WARNING: materialSlot outside the model's texture list for modelId=" + modelId +
                " subMeshes=" + std::to_string(mesh.subMeshes.size()) +
                " datTextures=" + std::to_string(textureIds.size()) +
                " outOfRange=" + std::to_string(outOfRange));
        }
        return;
    }

    mesh.textureID = textures.front();
    if (mesh.textureName.empty() && !textureIds.empty()) mesh.textureName = textureIds.front();
    Logger::Get().Log(
        LogLevel::INFO,
        "[TEX Native] Applied legacy single texture to modelId=" + modelId +
        " textureId=" + textureIds.front() +
        " glId=" + std::to_string(mesh.textureID));
}
