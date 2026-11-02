// source/cli/smoke_level_core.cpp
#include "smoke_level_core.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>

namespace {

char LowerAscii(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

// True when `text` starts at `pos` with "level" in any case.
bool MatchesLevelWord(const std::string& text, std::size_t pos) {
    static const char kWord[] = "level";
    if (pos + 5 > text.size()) return false;
    for (std::size_t i = 0; i < 5; ++i) {
        if (LowerAscii(text[pos + i]) != kWord[i]) return false;
    }
    return true;
}

bool IsPathSeparator(char c) { return c == '\\' || c == '/' || c == ':'; }

}  // namespace

const char* SmokeMeshSourceLabel(SmokeMeshSource source) {
    switch (source) {
        case SmokeMeshSource::ThisLevelArchive:  return "this level";
        case SmokeMeshSource::OwnerLevelArchive: return "owner level";
        case SmokeMeshSource::OtherLevelArchive: return "other level";
        case SmokeMeshSource::CommonArchive:     return "common archive";
        case SmokeMeshSource::LooseFile:         return "loose file";
        case SmokeMeshSource::Missing:
        default:                                 return "unresolved";
    }
}

std::string SmokeArchivePathFromSource(const std::string& source) {
    static const std::string kPrefix = "res:";
    if (source.size() <= kPrefix.size()) return std::string();
    if (source.compare(0, kPrefix.size(), kPrefix) != 0) return std::string();
    return source.substr(kPrefix.size());
}

int SmokeSourceLevelFromPath(const std::string& path) {
    // Last "level<digits>" component wins: an archive path repeats its level in
    // both the directory and the file name ("...\level7\models\level7.res"), and
    // the file name is the authority for which archive answered.
    int level = 0;
    for (std::size_t i = 0; i + 6 <= path.size(); ++i) {
        if (!MatchesLevelWord(path, i)) continue;
        // The word must start a path component, so "sublevel3" is not a match.
        if (i != 0 && !IsPathSeparator(path[i - 1])) continue;
        std::size_t digit = i + 5;
        if (digit >= path.size() || !std::isdigit(static_cast<unsigned char>(path[digit]))) continue;
        int value = 0;
        std::size_t k = digit;
        while (k < path.size() && std::isdigit(static_cast<unsigned char>(path[k]))) {
            value = value * 10 + (path[k] - '0');
            ++k;
        }
        level = value;
        i = k - 1;
    }
    return level;
}

SmokeMeshSource ClassifySmokeMeshSource(bool meshResolved,
                                        bool fromLooseFile,
                                        int sourceLevel,
                                        int ownerLevel,
                                        int currentLevel) {
    if (!meshResolved) return SmokeMeshSource::Missing;
    if (fromLooseFile) return SmokeMeshSource::LooseFile;
    if (sourceLevel <= 0) return SmokeMeshSource::CommonArchive;  // shared location0 archive
    // Keyed by the archive that answered.  The level's own archive is the engine's
    // path (Mesh_Load HOOK 0x4CF390, src/model/model.c:364, resolves against the
    // mounted level); the DAT-owner and any other level are the editor's fallbacks.
    if (sourceLevel == currentLevel) return SmokeMeshSource::ThisLevelArchive;
    if (ownerLevel > 0 && sourceLevel == ownerLevel) return SmokeMeshSource::OwnerLevelArchive;
    return SmokeMeshSource::OtherLevelArchive;
}

std::string FormatSmokeDurationMs(long long milliseconds) {
    if (milliseconds < 0) milliseconds = 0;
    std::ostringstream out;
    if (milliseconds < 1000) {
        out << milliseconds << " ms";
    } else if (milliseconds < 60000) {
        out << std::fixed << std::setprecision(2)
            << (static_cast<double>(milliseconds) / 1000.0) << " s";
    } else {
        const long long minutes = milliseconds / 60000;
        const double seconds = static_cast<double>(milliseconds % 60000) / 1000.0;
        out << minutes << "m " << std::fixed << std::setprecision(1) << seconds << "s";
    }
    return out.str();
}

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

// Accumulate `id` into a distinct list, counting the referencing placements,
// keeping the first referencing object as the example, and recording the level.
void Accumulate(std::map<std::string, SmokeMissingEntry>& byId,
                const std::string& id,
                const std::string& type,
                const std::string& name,
                int levelNo) {
    auto it = byId.find(id);
    if (it == byId.end()) {
        SmokeMissingEntry entry;
        entry.id = id;
        entry.placements = 1;
        entry.exampleType = type;
        entry.exampleName = name;
        if (levelNo > 0) entry.levels.push_back(levelNo);
        byId.emplace(id, std::move(entry));
        return;
    }
    ++it->second.placements;
    if (levelNo > 0 &&
        std::find(it->second.levels.begin(), it->second.levels.end(), levelNo) ==
            it->second.levels.end()) {
        it->second.levels.push_back(levelNo);
        std::sort(it->second.levels.begin(), it->second.levels.end());
    }
}

std::vector<SmokeMissingEntry> ToSortedList(const std::map<std::string, SmokeMissingEntry>& byId) {
    std::vector<SmokeMissingEntry> out;
    out.reserve(byId.size());
    for (const auto& [id, entry] : byId) out.push_back(entry);
    return out;
}

// Union one level's distinct-missing entry into the run's map: placements add up,
// the first level's example object is kept, and the level list merges.
void MergeMissingEntry(std::map<std::string, SmokeMissingEntry>& byId,
                       const SmokeMissingEntry& entry) {
    auto it = byId.find(entry.id);
    if (it == byId.end()) {
        byId.emplace(entry.id, entry);
        return;
    }
    it->second.placements += entry.placements;
    for (int level : entry.levels) {
        if (std::find(it->second.levels.begin(), it->second.levels.end(), level) ==
            it->second.levels.end()) {
            it->second.levels.push_back(level);
        }
    }
    std::sort(it->second.levels.begin(), it->second.levels.end());
}

void MergeIds(std::set<std::string>& into, const std::vector<std::string>& ids) {
    into.insert(ids.begin(), ids.end());
}

}  // namespace

SmokeTotals SummarizeSmokeLevel(const SmokeLevelResult& result) {
    SmokeTotals totals;

    std::set<std::string> distinctResolved;
    std::map<std::string, SmokeMissingEntry> missingModels;
    std::map<std::string, SmokeMissingEntry> missingTextures;
    std::set<std::string> allModels;
    std::set<std::string> allTextures;
    std::set<std::string> allResolvedTextures;

    for (const auto& model : result.models) {
        ++totals.objectsExamined;
        allModels.insert(model.ref.modelId);
        if (model.meshResolved) {
            ++totals.objectsResolved;
            distinctResolved.emplace(model.ref.modelId);
            switch (model.meshSourceKind) {
                case SmokeMeshSource::ThisLevelArchive:  ++totals.resolvedFromThisLevel; break;
                case SmokeMeshSource::OwnerLevelArchive: ++totals.resolvedFromOwnerLevel; break;
                case SmokeMeshSource::OtherLevelArchive: ++totals.resolvedFromOtherLevel; break;
                case SmokeMeshSource::CommonArchive:     ++totals.resolvedFromCommonArchive; break;
                case SmokeMeshSource::LooseFile:         ++totals.resolvedFromLooseFile; break;
                case SmokeMeshSource::Missing:
                default:                                 break;
            }
        } else {
            ++totals.modelsMissing;
            Accumulate(missingModels, model.ref.modelId, model.ref.type, model.ref.name,
                       result.levelNo);
        }

        for (const auto& textureId : model.textureIds) allTextures.insert(textureId);
        for (const auto& textureId : model.resolvedTextureIds) {
            allResolvedTextures.insert(textureId);
        }
        totals.texturesResolved += static_cast<int>(model.resolvedTextureIds.size());
        for (const auto& textureId : model.missingTextureIds) {
            ++totals.texturesMissing;
            Accumulate(missingTextures, textureId, model.ref.type, model.ref.name,
                       result.levelNo);
        }
    }

    totals.distinctModelIds.assign(allModels.begin(), allModels.end());
    totals.distinctResolvedModelIds.assign(distinctResolved.begin(), distinctResolved.end());
    totals.distinctTextureIds.assign(allTextures.begin(), allTextures.end());
    totals.distinctResolvedTextureIds.assign(allResolvedTextures.begin(), allResolvedTextures.end());
    totals.distinctModels = static_cast<int>(allModels.size());
    totals.distinctResolved = static_cast<int>(distinctResolved.size());
    totals.distinctMissing = static_cast<int>(missingModels.size());
    totals.distinctTextures = static_cast<int>(allTextures.size());
    totals.distinctTexturesResolved = static_cast<int>(allResolvedTextures.size());
    totals.distinctTexturesMissing = static_cast<int>(missingTextures.size());
    totals.missingModelIds = ToSortedList(missingModels);
    totals.missingTextureIds = ToSortedList(missingTextures);
    return totals;
}

SmokeRunTotals SummarizeSmokeRun(const std::vector<SmokeTotals>& perLevel) {
    SmokeRunTotals run;
    run.levelsCounted = static_cast<int>(perLevel.size());

    std::set<std::string> allModels;
    std::set<std::string> allResolvedModels;
    std::set<std::string> allTextures;
    std::set<std::string> allResolvedTextures;
    std::map<std::string, SmokeMissingEntry> missingModels;
    std::map<std::string, SmokeMissingEntry> missingTextures;

    for (const auto& level : perLevel) {
        run.objectsExamined += level.objectsExamined;
        run.objectsResolved += level.objectsResolved;
        run.modelsMissing += level.modelsMissing;
        run.texturesResolved += level.texturesResolved;
        run.texturesMissing += level.texturesMissing;
        run.resolvedFromThisLevel += level.resolvedFromThisLevel;
        run.resolvedFromOwnerLevel += level.resolvedFromOwnerLevel;
        run.resolvedFromOtherLevel += level.resolvedFromOtherLevel;
        run.resolvedFromCommonArchive += level.resolvedFromCommonArchive;
        run.resolvedFromLooseFile += level.resolvedFromLooseFile;

        MergeIds(allModels, level.distinctModelIds);
        MergeIds(allResolvedModels, level.distinctResolvedModelIds);
        MergeIds(allTextures, level.distinctTextureIds);
        MergeIds(allResolvedTextures, level.distinctResolvedTextureIds);
        for (const auto& entry : level.missingModelIds) MergeMissingEntry(missingModels, entry);
        for (const auto& entry : level.missingTextureIds) MergeMissingEntry(missingTextures, entry);
    }

    run.distinctModels = static_cast<int>(allModels.size());
    run.distinctResolved = static_cast<int>(allResolvedModels.size());
    run.distinctMissing = static_cast<int>(missingModels.size());
    run.distinctTextures = static_cast<int>(allTextures.size());
    run.distinctTexturesResolved = static_cast<int>(allResolvedTextures.size());
    run.distinctTexturesMissing = static_cast<int>(missingTextures.size());
    run.missingModelIds = ToSortedList(missingModels);
    run.missingTextureIds = ToSortedList(missingTextures);
    return run;
}

std::vector<SmokeSnapSample> CollectHumanSnapSamples(
    const std::vector<LevelObject>& objects,
    const std::function<bool(double x, double y, double& z)>& terrainZAt) {
    std::vector<SmokeSnapSample> out;
    for (const LevelObject& obj : objects) {
        // The same two types App::SnapObjectsToTerrain treats as human
        // (source/app_view.cpp: isHuman).
        if (obj.type != "HumanSoldier" && obj.type != "HumanSoldierFemale") continue;
        if (obj.modelId.empty()) continue;
        SmokeSnapSample s;
        s.modelId = obj.modelId;
        s.type = obj.type;
        s.authoredZ = obj.original_pos.z;
        double z = 0.0;
        if (terrainZAt != nullptr && terrainZAt(obj.pos.x, obj.pos.y, z)) {
            s.terrainFound = true;
            s.terrainZ = z;
            s.zDelta = s.authoredZ - z;
        }
        out.push_back(std::move(s));
    }
    return out;
}

std::string FormatSnapSample(const SmokeSnapSample& s) {
    std::ostringstream os;
    os << s.modelId << " (" << s.type << ") authoredZ=" << std::fixed
       << std::setprecision(1) << s.authoredZ;
    if (s.terrainFound) {
        os << " terrainZ=" << s.terrainZ << " zDelta=" << s.zDelta;
    } else {
        os << " terrainZ=NONE (query missed -> the rule keeps the authored Z;"
              " the engine would drop onto COLLISION, which the editor cannot"
              " query -- see terrain_snap_rules.h)";
    }
    return os.str();
}
