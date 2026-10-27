// source/cli/smoke_level_core.cpp
#include "smoke_level_core.h"

#include <algorithm>
#include <map>

std::vector<SmokeModelRef> CollectSmokeModels(
    const std::vector<LevelObject>& objects,
    const std::function<bool(const std::string&)>& isRendererSkipped,
    SmokeCollectStats* stats) {
    SmokeCollectStats local;
    local.totalObjects = static_cast<int>(objects.size());

    std::vector<SmokeModelRef> refs;
    refs.reserve(objects.size());
    for (const auto& obj : objects) {
        // The object draw loop's own order (renderer_objects.cpp:941-976).
        if (obj.deleted) { ++local.deleted; continue; }
        if (obj.isSplineWaypoint || obj.isSplineContainer) { ++local.splineRendered; continue; }
        if (obj.modelId.empty()) { ++local.withoutModelId; continue; }
        if (isRendererSkipped && isRendererSkipped(obj.modelId)) { ++local.rendererSkipped; continue; }

        SmokeModelRef ref;
        ref.modelId = obj.modelId;
        ref.isBuilding = obj.isBuilding;
        ref.type = obj.type;
        ref.name = obj.name;
        ref.taskId = obj.taskId;
        refs.push_back(std::move(ref));
    }

    if (stats) *stats = local;
    return refs;
}

namespace {

// Accumulate `id` into a distinct list, counting the referencing placements and
// keeping the first referencing object as the example.
void Accumulate(std::map<std::string, SmokeMissingEntry>& byId,
                const std::string& id,
                const std::string& type,
                const std::string& name) {
    auto it = byId.find(id);
    if (it == byId.end()) {
        SmokeMissingEntry entry;
        entry.id = id;
        entry.placements = 1;
        entry.exampleType = type;
        entry.exampleName = name;
        byId.emplace(id, std::move(entry));
        return;
    }
    ++it->second.placements;
}

std::vector<SmokeMissingEntry> ToSortedList(const std::map<std::string, SmokeMissingEntry>& byId) {
    std::vector<SmokeMissingEntry> out;
    out.reserve(byId.size());
    for (const auto& [id, entry] : byId) out.push_back(entry);
    return out;
}

}  // namespace

SmokeTotals SummarizeSmokeLevel(const SmokeLevelResult& result) {
    SmokeTotals totals;

    std::map<std::string, int> distinctResolved;
    std::map<std::string, SmokeMissingEntry> missingModels;
    std::map<std::string, SmokeMissingEntry> missingTextures;

    for (const auto& model : result.models) {
        ++totals.objectsExamined;
        if (model.meshResolved) {
            ++totals.objectsResolved;
            distinctResolved.emplace(model.ref.modelId, 0);
        } else {
            ++totals.modelsMissing;
            Accumulate(missingModels, model.ref.modelId, model.ref.type, model.ref.name);
        }

        totals.texturesResolved += static_cast<int>(model.resolvedTextureIds.size());
        for (const auto& textureId : model.missingTextureIds) {
            ++totals.texturesMissing;
            Accumulate(missingTextures, textureId, model.ref.type, model.ref.name);
        }
    }

    totals.distinctModels = static_cast<int>(distinctResolved.size() + missingModels.size());
    totals.distinctResolved = static_cast<int>(distinctResolved.size());
    totals.distinctMissing = static_cast<int>(missingModels.size());
    totals.missingModelIds = ToSortedList(missingModels);
    totals.missingTextureIds = ToSortedList(missingTextures);
    return totals;
}
