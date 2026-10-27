// source/cli/smoke_level_core.h
//
// Host-portable core of the `--smoke-level` loader check (no windows.h, no GL, no
// Renderer_Objects), so the parts that decide PASS/FAIL can be unit-tested in the
// host lane.  The Windows half (source/cli/smoke_level.cpp) decompiles the level,
// loads its objects, and resolves each model through Renderer_Objects.
//
// WHAT THIS CATCHES
//   `--verify-level` proves the QSC parse but never exercises the model/texture
//   loaders.  A resolution regression — e.g. FindMeshData looking up a bare
//   "<id>.mef" instead of the engine's resource identity "LOCAL:models/<id>.mef"
//   (Mesh_Load HOOK 0x4CF390, src/model/model.c:364; matched on the folded full
//   name, String_ToUpper 0x4B5AF0, src/res/resource.c:98-104, Resource_IsLoaded
//   :136-138) — made 61 models resolve to nothing and no command noticed.  The
//   smoke check reports those names and exits non-zero.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "level/level_objects.h"

// One placement the renderer would resolve a mesh for.
struct SmokeModelRef {
    std::string modelId;
    bool isBuilding = false;
    std::string type;
    std::string name;
    std::string taskId;
};

// Objects the collector deliberately did not turn into a check, so the printed
// totals can be explained rather than silently narrowed.
struct SmokeCollectStats {
    int totalObjects = 0;      // every object LevelObjects::Load produced
    int deleted = 0;           // obj.deleted (the draw loop skips these first)
    int withoutModelId = 0;    // no model to resolve (containers, lights, ...)
    int splineRendered = 0;    // isSplineWaypoint/isSplineContainer: drawn by
                               // renderer_splines.cpp, skipped by the object loop
    int rendererSkipped = 0;   // dropped by the caller's renderer skip predicate
};

// Every object the object draw loop would call GetOrLoadMesh for.
//
// The filters mirror the draw loop in renderer_objects.cpp:941-976: skip
// `deleted`, skip empty `modelId`, and skip SplineObjWaypoint/SplineObj (they are
// rendered by renderer_splines.cpp, not by the object pass).  `isRendererSkipped`
// carries the renderer's own id predicate
// (Renderer_Objects::IsSkippedModelId, renderer_objects.cpp:14-17: colbox /
// colbox2 / colbox4 / colbox66 collision proxies are never resolved) so this core
// does not have to know those names; pass an empty std::function to keep every id.
std::vector<SmokeModelRef> CollectSmokeModels(
    const std::vector<LevelObject>& objects,
    const std::function<bool(const std::string&)>& isRendererSkipped,
    SmokeCollectStats* stats = nullptr);

// Where a resolved model's mesh bytes came from.
//
// The editor's order is: the level's own (and shared common) .res index, then the
// archive of the level the global DAT map names as the model's owner, then every
// other level's archive, then a loose .mef on disk
// (Renderer_Objects::GetOrLoadMesh, source/renderer/renderer_objects_mesh.cpp:269-332).
// The engine has no such walk: Mesh_Load (HOOK 0x4CF390) resolves
// "LOCAL:models/%s.mef" (src/model/model.c:364) from the mounted archive of the
// level being played, matched on the folded full resource name (String_ToUpper
// 0x4B5AF0, src/res/resource.c:98-104; Resource_IsLoaded 0x4B5B90, :136-138).
//
// So the buckets are keyed by the archive that ANSWERED, not by which branch of the
// editor's code asked: a hit in the level's own archive is what the game would do,
// a hit in another level's archive is the editor's fallback and would be a miss in
// game.  The DAT-owner bucket is kept separate because the renderer's owner map is
// "the first level whose .dat lists the model" (EnsureGlobalTextureMapLoaded,
// source/renderer/renderer_objects_texture.cpp:88-91), which is a different fact
// from "this level's archive contains the bytes".
enum class SmokeMeshSource {
    Missing,            // nothing resolved
    ThisLevelArchive,   // the archive of the level being examined (the engine's path)
    OwnerLevelArchive,  // the archive of the level the global DAT map names as owner
    OtherLevelArchive,  // some other level's archive (editor-only fallback)
    CommonArchive,      // the shared location0 archive
    LooseFile,          // a .mef on disk (the engine has no disk lookup at all)
};

// Short label for the per-level/per-run source summary lines.
const char* SmokeMeshSourceLabel(SmokeMeshSource source);

// One model's resolution result, filled by the Windows half.
struct SmokeModelResult {
    SmokeModelRef ref;
    bool meshResolved = false;
    std::string meshSource;                    // "res:<archive>" or a disk path
    bool meshFromLooseFile = false;            // meshSource is a disk path
    SmokeMeshSource meshSourceKind = SmokeMeshSource::Missing;
    int meshSourceLevel = 0;                   // level of the answering archive/file, 0 = common/unknown
    int meshOwnerLevel = 0;                    // level the global DAT map names as owner, 0 = none
    std::vector<std::string> textureIds;       // the DAT list for this model
    std::vector<std::string> resolvedTextureIds;
    std::vector<std::string> missingTextureIds;
};

// The archive path out of a "res:<archive>" source string; "" for a disk path (or
// anything without the prefix).  ResolveModelForSmoke marks archive hits that way.
std::string SmokeArchivePathFromSource(const std::string& source);

// The level a path names when it contains a "level<digits>" component:
// "...\missions\location0\level7\models\level7.res" -> 7.  0 for the shared common
// archive (location0.res), a path with no level component, and anything unparseable.
int SmokeSourceLevelFromPath(const std::string& path);

// Classify one resolution.  `sourceLevel` is the level of the answering archive
// (0 = shared common); `ownerLevel` is the level the global DAT map names as the
// model's owner (0 = the map has no entry); `currentLevel` is the level being
// examined.  Pure, so the summary the CLI prints is unit-testable.
SmokeMeshSource ClassifySmokeMeshSource(bool meshResolved,
                                        bool fromLooseFile,
                                        int sourceLevel,
                                        int ownerLevel,
                                        int currentLevel);

// Per-level wall-clock time, human readable: "812 ms", "1.23 s", "2m 3.4s".
std::string FormatSmokeDurationMs(long long milliseconds);

// One level's raw results. `error` non-empty means the level could not be loaded
// at all (missing/corrupt objects.qvm), which is a failure too.
struct SmokeLevelResult {
    int levelNo = 0;
    std::string error;
    std::vector<SmokeModelResult> models;
};

// A distinct missing id plus the placements that reference it, so the failure can
// be located instead of just named.
struct SmokeMissingEntry {
    std::string id;
    int placements = 0;
    std::string exampleType;
    std::string exampleName;
    // Levels that reference this id, ascending and deduplicated.  Filled by the
    // per-level summary (one level) and by the run aggregate (every level that
    // missed the same id), so the totals line can say "on level 7" instead of
    // making the reader scan fourteen level blocks.
    std::vector<int> levels;
};

struct SmokeTotals {
    int objectsExamined = 0;   // placements checked (CollectSmokeModels output)
    int objectsResolved = 0;   // placements whose mesh resolved
    int modelsMissing = 0;     // placements whose mesh did not resolve
    int distinctModels = 0;
    int distinctResolved = 0;
    int distinctMissing = 0;
    int texturesResolved = 0;  // resolved texture placements (sum over models)
    int texturesMissing = 0;   // missing texture placements (sum over models)
    int distinctTextures = 0;
    int distinctTexturesResolved = 0;
    int distinctTexturesMissing = 0;
    // Where the resolved meshes came from, counted per placement.  Split out
    // because the engine only ever resolves from the level being played's archive;
    // a hit in another level's archive is the editor's fallback and is what a
    // packing gap looks like.
    int resolvedFromThisLevel = 0;
    int resolvedFromOwnerLevel = 0;
    int resolvedFromOtherLevel = 0;
    int resolvedFromCommonArchive = 0;
    int resolvedFromLooseFile = 0;
    std::vector<SmokeMissingEntry> missingModelIds;    // distinct, sorted by id
    std::vector<SmokeMissingEntry> missingTextureIds;  // distinct, sorted by id
    // Every distinct id the level examined, so a run-level total can be a UNION
    // across levels rather than a sum of per-level distinct counts (one model
    // placed on five levels is one model, not five).
    std::vector<std::string> distinctModelIds;
    std::vector<std::string> distinctResolvedModelIds;
    std::vector<std::string> distinctTextureIds;
    std::vector<std::string> distinctResolvedTextureIds;

    bool clean() const { return modelsMissing == 0 && texturesMissing == 0; }
};

// Per-level totals. A level with a non-empty `error` reports clean() == false via
// the caller's exit code (there is nothing to count).
SmokeTotals SummarizeSmokeLevel(const SmokeLevelResult& result);

// Whole-run totals over every level that produced a result.  Placement counts are
// sums; every distinct count is a UNION across levels, which is what makes "one
// missing model in fourteen levels" readable as one.  A level that failed to load
// contributes nothing here — the caller counts those separately.
struct SmokeRunTotals {
    int levelsCounted = 0;
    int objectsExamined = 0;
    int objectsResolved = 0;
    int modelsMissing = 0;
    int distinctModels = 0;
    int distinctResolved = 0;
    int distinctMissing = 0;
    int texturesResolved = 0;
    int texturesMissing = 0;
    int distinctTextures = 0;
    int distinctTexturesResolved = 0;
    int distinctTexturesMissing = 0;
    int resolvedFromThisLevel = 0;
    int resolvedFromOwnerLevel = 0;
    int resolvedFromOtherLevel = 0;
    int resolvedFromCommonArchive = 0;
    int resolvedFromLooseFile = 0;
    std::vector<SmokeMissingEntry> missingModelIds;    // union, placements summed
    std::vector<SmokeMissingEntry> missingTextureIds;  // union, placements summed
};

SmokeRunTotals SummarizeSmokeRun(const std::vector<SmokeTotals>& perLevel);
