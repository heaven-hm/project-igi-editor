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
