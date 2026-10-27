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

// One model's resolution result, filled by the Windows half.
struct SmokeModelResult {
    SmokeModelRef ref;
    bool meshResolved = false;
    std::string meshSource;                    // "res:<archive>" or a disk path
    std::vector<std::string> textureIds;       // the DAT list for this model
    std::vector<std::string> resolvedTextureIds;
    std::vector<std::string> missingTextureIds;
};

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
};

struct SmokeTotals {
    int objectsExamined = 0;   // placements checked (CollectSmokeModels output)
    int objectsResolved = 0;   // placements whose mesh resolved
    int modelsMissing = 0;     // placements whose mesh did not resolve
    int distinctModels = 0;
    int distinctResolved = 0;
    int distinctMissing = 0;
    int texturesResolved = 0;
    int texturesMissing = 0;
    std::vector<SmokeMissingEntry> missingModelIds;    // distinct, sorted by id
    std::vector<SmokeMissingEntry> missingTextureIds;  // distinct, sorted by id

    bool clean() const { return modelsMissing == 0 && texturesMissing == 0; }
};

// Per-level totals. A level with a non-empty `error` reports clean() == false via
// the caller's exit code (there is nothing to count).
SmokeTotals SummarizeSmokeLevel(const SmokeLevelResult& result);
