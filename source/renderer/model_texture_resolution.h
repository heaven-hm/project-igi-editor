#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "res_writer.h"

struct ModelTextureSource {
    int level;
    std::string modelId;
    std::vector<std::string> textures;
};

struct ModelPickerEntry {
    std::string modelId;
    int sourceLevel;
    std::string label;
};

// Build the exact model list used by picker drawing and input handling.
std::vector<ModelPickerEntry> FilterModelPickerEntries(
    const std::vector<ModelPickerEntry>& entries,
    const std::string& filter);

// Collapse per-source picker rows to one row per unique model ID. The kept
// row carries the current level when it owns the model, otherwise the
// smallest owning level, so Enter imports a deterministic source bundle.
std::vector<ModelPickerEntry> DedupeModelPickerEntries(
    const std::vector<ModelPickerEntry>& entries,
    int currentLevel);

// The model ID and source level committed by Enter on the filtered picker
// list. Empty when the selection is outside the filtered list.
struct ModelPickerCommit {
    std::string modelId;
    int sourceLevel;
};
std::optional<ModelPickerCommit> ResolveModelPickerCommit(
    const std::vector<ModelPickerEntry>& entries,
    const std::string& filter,
    int selected);

// Import a missing model when the destination inventory is known, and always
// honor an explicit foreign source even when it has the same model ID.
bool ModelSourceRequiresImport(bool destinationInventoryLoaded,
                               bool destinationContainsModel,
                               int destinationLevel,
                               int selectedSourceLevel);

// Return the filtered-list index for a model-picker row click, or -1 when the
// click is outside the item area. Coordinates are top-down screen pixels.
int ModelPickerSelectionAt(
    const std::vector<ModelPickerEntry>& entries,
    const std::string& filter,
    int mouseX,
    int mouseY,
    int viewportWidth,
    int viewportHeight,
    int scrollOffset,
    int rowHeight);

// Move a picker selection within the filtered-list bounds. The list holds one
// row per unique model ID, so every step changes the previewed model.
int MoveModelPickerSelection(int selected, int count, int delta);

struct ModelImportMetadataStatus {
    bool datPublished;
    bool mtpPublished;
};

struct ModelSourceBundle {
    int level;
    std::vector<std::string> textureIds;
    std::vector<uint8_t> meshBytes;
    std::vector<std::vector<uint8_t>> textureBytes;
};

bool IsCompleteModelImportMetadata(const ModelImportMetadataStatus& status);

// Find the ordered material mapping for one model variant in one source level.
// A missing exact source is intentionally not replaced by a prefix match here.
const ModelTextureSource* FindExactTextureSource(
    const std::vector<ModelTextureSource>& sources,
    int sourceLevel,
    const std::string& modelId);

// Remove only known pixel-format tags. Numeric suffixes are part of texture
// identity and must remain untouched.
std::string StripTextureFormatSuffix(const std::string& textureId);

// True when the id carries a pixel-format tag that actually holds alpha
// (_argb8888, _argb1555, _argb4444, _a8r8g8b8, _a1r5g5b5, _a4r4g4b4). The opaque
// formats StripTextureFormatSuffix also knows (_rgb565, _r5g6b5) are excluded:
// an opaque variant carries no per-texel transparency, so it is not an overlay.
//
// The renderer uses this to decide alpha blending for a resolved texture and to
// decide whether the AI opaque-body preference applies at all.
bool TextureIdHasAlphaSuffix(const std::string& textureId);

// Which id to load for one entry of a model's texture list.
//
// IGI-RE: the engine loads exactly the ids the model's own texture table names —
// ModelFile_LoadTextures walks the MTP TEXF names of that model file and calls
// Texture_LoadFile("LOCAL:textures/<name>.tex") for each, pixel-format suffix and
// all (HOOK 0x4CF790, src/model/model.c:452-470); ModelFile_Setup is what binds
// those entries to a lod by GTT index (HOOK 0x4CF080, src/model/model.c:271-314).
// There is no suffix stripping anywhere in the game.
//
// The editor nonetheless prefers the OPAQUE body texture over the small
// alpha-bearing overlay for AI characters ("009_09_1_argb8888" -> "009_09_1.tex"),
// which is an editor-only look, not an engine mapping. That preference must never
// turn an id the engine can resolve into one it cannot: when the stripped id
// resolves to nothing the engine's own (suffixed) id is the only texture the
// model can be drawn with, so it is the one that gets loaded. Measured on the
// retail install, this is the 015_01_1 (AITYPE_ANYA) case: its 8th texture is
// "000_11_1_argb8888", which exists, while "000_11_1" exists nowhere in the
// install.
//
// `resolves` is asked only about the stripped candidate, and only when there is
// one. Returns `textureId` when the strip does not apply.
std::string ResolveAiModelTextureId(
    const std::string& textureId,
    bool isAiModel,
    const std::function<bool(const std::string&)>& resolves);

// Whether the editor may fall back to "the model's own name is a texture id".
//
// IGI-RE: a model's texture list IS its own MTP INST record's GTT list
// (ModelFile_Setup, HOOK 0x4CF080, src/model/model.c:271-314) and the engine
// binds l->textures[g->texture] only for a group whose index is not 0xFFFF
// (lod_texture, src/render/render.c:216-219; draw_group, :524-531). An INST
// record with n == 0 therefore describes a model that HAS no textures — it is
// not a model whose texture list is unknown — and the engine draws it with the
// texture stage off. Retail examples: "waypoint" (every level's marker model,
// INST nTex=0, its only REND group 0xFFFF) and the collision-only shells
// 700_08_1 / 700_19_1.
//
// So the guess is allowed only when the DAT did NOT answer for this model. When
// it did answer — even with an empty list — the guess would invent a texture id
// out of a model name, which is a lookup the game never performs.
bool ModelNameIsTextureIdFallbackAllowed(bool modelFoundInDat);

// Whether the editor may fill a model's EMPTY texture list with ANOTHER model's
// list — its ATTA parent hull's, or a "_02_1" sibling variant's.
//
// IGI-RE: it may not, and an empty list the DAT answered for is not a gap at all.
//
//  * A model's textures are its own MTP INST record's GTT list and nothing else:
//    ModelFile_Setup walks the INST chunk one record per ModelLod, `n = *inst++`,
//    and fills that LOD's own array with the GTT-indexed TEXF entries
//    (HOOK 0x4CF080, src/model/model.c:275-289,296-314). ModelFile_LoadTextures
//    turns each TEXF name into "LOCAL:textures/<name>.tex" (HOOK 0x4CF790,
//    src/model/model.c:452-470).
//  * The draw binds that array by the group's own index and nothing else:
//    lod_texture(l,i) => ((Texture *)l->textures[i])->surface
//    (src/render/render.c:216-219), used as
//    `if (g->texture != 0xFFFF) SetTexture(dev,0,lod_texture(ml,g->texture))`
//    (src/render/render.c:524-525; the same in the spline, lightmapped and sorted
//    paths at :733-734, :1029-1033/:1061-1062, :1210-1211). 0xFFFF means
//    UNTEXTURED: SetTexture(NULL) + COLOROP=SELECTARG2 (:526-531).
//  * An ATTA child is spawned as its own task and resolved with its own
//    VirModel_Find(def->model) (src/shadow/sector.c:667-672;
//    src/magicobj/rigidcoll.c:484-485), so it owns its own ModelLod and its own
//    INST record. No engine code assigns one ModelLod's textures from another's:
//    the only writes to a LOD's texture array are ModelFile_Setup
//    (src/model/model.c:312) and the NAXT texture-animation swap inside a single
//    LOD (src/lod/mesh3d.c:558-581). The one cross-file convention that exists is
//    data-driven, not parent->child: an index >= 0x2000 addresses the FIRST loaded
//    ModelFile (src/model/model.c:281-306), which is the common model file
//    (src/game/game.c:414-418).
//
// So an INST record with n == 0 is the engine stating "this model has no
// textures" — every one of its REND groups carries 0xFFFF and it is drawn with
// the texture stage off — and the editor must bind nothing there (at INFO)
// instead of borrowing a parent's list.
//
// Returns true only when the DAT did NOT answer for this model (a genuine miss,
// where the editor has no list at all and its historical guesses may run).
bool ModelTextureInheritanceAllowed(bool modelFoundInDat);

// True for the shared location0 texture archive.
bool IsSharedCommonTextureArchive(const std::string& resPath);

// Every material slot emitted by a MEF must address the selected ordered
// mapping. Slot numbers are indices, not texture IDs, so accepting an
// out-of-range slot would make the importer silently bind the wrong material.
bool IsTextureMappingCompatible(const std::vector<int>& materialSlots,
                                std::size_t orderedMappingSize);

// Untagged imports are safe only when every exact source is byte-equivalent.
// A zero result means the caller must request explicit source provenance.
int SelectUnambiguousModelSourceLevel(
    const std::vector<ModelSourceBundle>& candidates);

// One indexed entry inside a source-bundle archive: the archive file plus the
// name->offset/size table built once with RES_BuildIndex. Level archives are
// searched before the shared common archive, so same-name bytes from an
// unrelated archive can never override the selected source bundle.
struct ModelSourceArchiveIndex {
    std::string resPath;
    std::unordered_map<std::string, ResEntryInfo> entries;
};

// One resolved archive entry: the archive it was read from plus its bytes.
// `bytes` empty (and `resPath` then meaningless) means "not in any of these
// archives".
struct ModelEntryHit {
    std::string resPath;
    std::vector<uint8_t> bytes;
};

// Resolve a model's .mef bytes from indexed archives by the engine's own resource
// identity, returning the archive they came from.
//
// IGI-RE: Mesh_Load (HOOK 0x4CF390) asks the resource system for
// "LOCAL:models/%s.mef" (src/model/model.c:364) and the match is on the folded
// full name — String_ToUpper 0x4B5AF0 folds it (src/res/resource.c:98-104) and
// Resource_IsLoaded compares the folded string (:136-138). A bare "<id>.mef" is
// NOT that key, so a lookup with one never matches: that is exactly the bug that
// made 61 models resolve to nothing while the viewport stayed empty.
//
// Host-portable on purpose: Renderer_Objects::FindMeshData is a thin wrapper over
// this, so the identity rule is unit-testable without the Windows GL target.
ModelEntryHit FindModelEntryByIdentity(
    const std::vector<ModelSourceArchiveIndex>& archives,
    const std::string& modelId,
    const std::function<std::vector<uint8_t>(const std::string& resPath,
                                              const ResEntryInfo& info)>& readEntry);

// Resolve a scene-preview texture. Level entries are authoritative over
// same-named common entries; explicit source imports use FindModelSourceEntry.
//
// `sourceRes` (optional) receives the archive the bytes came from, so a caller can
// report provenance — the editor's cross-level fallback must be visible, because
// the engine resolves a model's textures from exactly one archive, the one of the
// level the model file belongs to (ModelFile_LoadTextures HOOK 0x4CF790,
// src/model/model.c:467; ModelFile_Setup loads "%stextures/%s.res" for that level,
// :261, HOOK 0x4CF080).
std::vector<uint8_t> FindPreviewTextureEntry(
    const std::vector<ModelSourceArchiveIndex>& archives,
    const std::string& textureId,
    const std::function<std::vector<uint8_t>(const std::string& resPath,
                                              const ResEntryInfo& info)>& readEntry,
    std::string* sourceRes = nullptr);

// Case-insensitive equality for two archive paths, the way the editor compares
// them ("...\LEVEL7\textures\level7.res" == "...\level7\textures\level7.res").
bool SameArchivePath(const std::string& a, const std::string& b);

// Resolve one entry from a source bundle. Callers pass bundle[0] = selected
// level archive and bundle[1] = shared common archive. The level archive is
// registered after common by the game and replaces same-name entries; common
// supplies only missing entries. Mesh ids use the same precedence.
// Format-suffixed texture ids fall back to the stripped name in the same
// archive before the search continues to the next archive.
std::vector<uint8_t> FindModelSourceEntry(
    const std::array<ModelSourceArchiveIndex, 2>& bundle,
    const std::string& entryId,
    bool isTexture,
    const std::function<std::vector<uint8_t>(const std::string& resPath,
                                              const ResEntryInfo& info)>& readEntry);

// Search order for a texture lookup whose owning archive is known: that archive
// first, every other archive after it in its existing order.
//
// IGI-RE: the game resolves a model's textures from exactly one archive, the one
// of the level the model file belongs to. ModelFile_Setup loads
// "%stextures/%s.res" for that level (src/model/model.c:261, HOOK(0x4CF080)),
// ModelFile_LoadTextures turns the MTP_TEXF table of that resource into the
// model's texture table (src/model/model.c:450-461), and the GTT index is applied
// straight to it (src/model/model.c:300-306). There is no cross-level search, so
// an archive indexed earlier in the session must not win.
//
// Position matters because retail archives carry different images under one id:
// 001_02_1.tex is 128x128 in level7/textures/level7.res and 128x32 in
// common/textures/location0.res. FindPreviewTextureEntry still searches the
// shared common archive last as a group, so hoisting the owner here is enough.
std::vector<ModelSourceArchiveIndex> OrderTextureArchivesByOwner(
    const std::vector<ModelSourceArchiveIndex>& archives,
    const std::string& ownerResPath);

// The level whose archive supplied a model's bytes, read back out of the archive
// path the editor resolved them from: "...\level7\models\level7.res" -> 7,
// "...\common\models\location0.res" -> 0. 0 also means "not a level archive", which
// is the only answer a caller may treat as unknown.
//
// IGI-RE: a model file and its textures always come from ONE level directory.
// `Model_SetDir` copies the level's directory into `gModelDir` before that level's
// models are loaded (src/game/game.c:411-418; HOOK(0x4CFBD0) at
// src/lod/modelfile.c:153-157), and `ModelFile_Load` / `ModelFile_Setup` read the
// model's `.mtp`, its `models/<name>.res` and its `textures/<name>.res` out of that
// same directory (src/model/model.c:233, :261, :267, HOOK(0x4CF080)). So the archive
// a model was actually read from is the archive its textures must be searched in —
// there is no cross-level search in the engine at all.
int ModelArchiveOwnerLevel(const std::string& resPath);

// Which level's texture archive a model's textures must be searched in first:
// the level whose archive supplied the MODEL's bytes (`meshOwnerLevel`, from
// ModelArchiveOwnerLevel), falling back to the editor's texture-id owner
// (`textureIdOwnerLevel`: the first level whose DAT lists that texture id) only
// when the model's archive is not a level archive (0).
//
// IGI-RE: the model file and its textures come from one directory, so the model's
// own archive decides — never the first level whose DAT happens to list the
// texture id, and never the level being previewed. The texture-id owner is the
// editor's fallback for a model read from the shared/common archive, where the
// engine has no level answer to reproduce. Levels are 1..14; 0 means none.
int TextureOwnerLevelForModel(int meshOwnerLevel, int textureIdOwnerLevel);
