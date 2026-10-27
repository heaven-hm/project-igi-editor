/******************************************************************************
 * @file    renderer_objects_smoke.cpp
 * @brief   Renderer_Objects: headless (no-OpenGL) model/texture resolution probe
 *          used by the CLI `--smoke-level` loader check.
 *
 * WHY THIS EXISTS
 *   `--verify-level` proves the QSC parse (QVM -> QSC vs a game-run log) but never
 *   touches the model/texture loaders, so a resolution regression can pass every
 *   gate and still leave the viewport empty.  That is what happened when
 *   FindMeshData looked up a bare "<id>.mef" instead of the engine's resource
 *   identity "LOCAL:models/<id>.mef": 61 models resolved to nothing and no
 *   command noticed.
 *
 *   This file adds the resolution half of the load path with NO OpenGL work at
 *   all — no mesh parse into VAO/VBO, no texture upload — so the CLI can run it
 *   headlessly.  Everything it calls is an existing resolution function
 *   (FindMeshData / FindModelFile / GetTextureIdsForModel / FindTextureData /
 *   FindTextureFile); the only new code is the cross-level walk and the reporting,
 *   which are the same steps GetOrLoadMesh / GetOrExtractMefTemp / GetOrLoadTexture
 *   already perform inline.
 *
 * RESOLUTION RULE (IGI-RE, the only authority):
 *   Mesh_Load            HOOK 0x4CF390  asks for "LOCAL:models/%s.mef"
 *                                       (src/model/model.c:364)
 *   ModelFile_LoadTextures HOOK 0x4CF790 asks for "LOCAL:textures/%s.tex"
 *                                       (src/model/model.c:467)
 *   the resource system folds the full name (String_ToUpper 0x4B5AF0,
 *   src/res/resource.c:98-104) and matches on that folded string
 *   (Resource_IsLoaded 0x4B5B90, src/res/resource.c:136-138).
 *   A model's textures come from the single archive of the level that owns the
 *   model file — ModelFile_Setup loads "%stextures/%s.res" for that level
 *   (src/model/model.c:261, HOOK 0x4CF080) and the MTP_TEXF table of that same
 *   resource is the model's texture table (src/model/model.c:450-461) — with no
 *   cross-level search.  The editor additionally falls back to other levels'
 *   archives and to loose files on disk (GetOrLoadTexture / FindTextureFile); this
 *   probe mirrors the editor, which is the thing being tested.
 *****************************************************************************/
#include "renderer_objects_internal.h"

// The mesh-bytes lookup the draw path performs, in its own order: the in-memory
// .res indexes of the current level and the shared common archive (FindMeshData),
// then the level the global DAT map names as the model's owner, then every other
// level — exactly the walk GetOrLoadMesh / GetOrExtractMefTemp /
// GetOrLoadParsedGeometry / GetOrLoadSkinGeometry each perform inline.  Reads
// archive bytes only; nothing is parsed or uploaded.
//
// It calls FindMeshData itself (rather than repeating the lookup) so that a
// regression inside FindMeshData is what this probe detects — which is the whole
// point of the command.
ModelEntryHit Renderer_Objects::FindMeshEntryAcrossLevels(const std::string& modelId) {
    auto lookup = [&]() -> ModelEntryHit {
        ModelEntryHit hit;
        hit.bytes = FindMeshData(modelId, &hit.resPath);
        return hit;
    };

    ModelEntryHit hit = lookup();
    if (!hit.bytes.empty()) return hit;

    EnsureGlobalTextureMapLoaded();
    int targetLvl = -1;
    auto mit = model_level_map_.find(modelId);
    if (mit != model_level_map_.end() && mit->second != current_level_) {
        targetLvl = mit->second;
        LoadResCache(targetLvl, Utils::GetIGIRootPath());
        hit = lookup();
        if (!hit.bytes.empty()) return hit;
    }
    for (int lvl = 1; lvl <= 14 && hit.bytes.empty(); ++lvl) {
        if (lvl == current_level_ || lvl == targetLvl) continue;
        LoadResCache(lvl, Utils::GetIGIRootPath());
        hit = lookup();
    }
    return hit;
}

// Provenance for the non-obvious mesh case.  GetOrLoadMesh / GetOrExtractMefTemp /
// GetOrLoadParsedGeometry / GetOrLoadSkinGeometry all fall back across levels, and
// that fallback used to be silent: the only line was a generic "Loaded 'X' from
// ResCache" that named neither the archive nor the level.  The engine has no such
// walk — Mesh_Load (HOOK 0x4CF390) resolves "LOCAL:models/%s.mef"
// (src/model/model.c:364) from the mounted archive of the level that owns the model
// — so when the editor answers from a different level's archive the log says so.
void Renderer_Objects::LogCrossLevelModelResolution(const std::string& modelId,
                                                    int resolvedFromLevel,
                                                    const std::string& resPath) const {
    if (modelId.empty()) return;
    if (resolvedFromLevel <= 0 || resolvedFromLevel == current_level_) return;
    Logger::Get().Log(LogLevel::DEBUG,
        "[Renderer_Objects] '" + modelId + "' resolved from level" +
        std::to_string(resolvedFromLevel) + "'s archive " + resPath +
        ", not the level" + std::to_string(current_level_) +
        " archive — engine rule is the owner level's archive (Mesh_Load HOOK 0x4CF390, src/model/model.c:364)");
}

// Every .res archive currently indexed, with the member count each index holds.
// LoadResCache is append-only within a level pass, so diffing this before and after
// the resolution walk shows exactly which foreign archives a fallback pulled in.
std::vector<Renderer_Objects::SmokeArchiveInfo> Renderer_Objects::SmokeIndexedArchives() const {
    std::vector<SmokeArchiveInfo> out;
    out.reserve(res_tex_indexes_.size() + res_model_indexes_.size());
    for (const auto& index : res_tex_indexes_) {
        out.push_back({index.res_path, true, index.index.size()});
    }
    for (const auto& index : res_model_indexes_) {
        out.push_back({index.res_path, false, index.index.size()});
    }
    return out;
}

// The level the global DAT map names as this model's owner (0 = no entry).  The
// smoke check uses it to tell "the archive that owns the model answered" from "the
// editor's cross-level fallback answered".
int Renderer_Objects::SmokeModelOwnerLevel(const std::string& modelId) {
    if (modelId.empty()) return 0;
    EnsureGlobalTextureMapLoaded();
    auto it = model_level_map_.find(modelId);
    return it == model_level_map_.end() ? 0 : it->second;
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
bool Renderer_Objects::TextureResolvesForSmoke(const std::string& textureId) {
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

Renderer_Objects::SmokeModelResolution
Renderer_Objects::ResolveModelForSmoke(const std::string& modelId, bool isBuilding) {
    SmokeModelResolution out;
    if (modelId.empty()) return out;

    // 1. Mesh — the draw path's order: the level's own (and common) .res by the
    //    engine's resource identity, then the owning level, then every other
    //    level, then a loose .mef on disk (FindModelFile).
    ModelEntryHit hit = FindMeshEntryAcrossLevels(modelId);
    if (!hit.bytes.empty()) {
        out.meshResolved = true;
        out.meshSource = "res:" + hit.resPath;
    } else {
        const std::string path = FindModelFile(modelId, isBuilding);
        if (!path.empty()) {
            out.meshResolved = true;
            out.meshSource = path;
            out.meshFromLooseFile = true;
        }
    }

    // 2. Textures — exactly the list the real path builds.  ApplyTexturesToMesh calls
    //    GetTextureIdsForModel(modelId) (renderer_objects_texture.cpp:510) and does NOT
    //    substitute anything when that list is empty: the engine binds
    //    l->textures[g->texture] and an empty list simply means no group has a texture
    //    (src/render/render.c:524-525).  This probe used to push the MODEL id as a
    //    texture id when the list was empty, which invented a lookup the renderer never
    //    performs and reported real models as missing textures (e.g. 430_01_1, a Building
    //    model in level 12/13/14 that is not a texture anywhere in the install).
    out.textureIds = GetTextureIdsForModel(modelId);
    for (const auto& textureId : out.textureIds) {
        if (TextureResolvesForSmoke(textureId)) out.resolvedTextureIds.push_back(textureId);
        else                                     out.missingTextureIds.push_back(textureId);
    }
    return out;
}
