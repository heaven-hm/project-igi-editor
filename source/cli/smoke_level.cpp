// source/cli/smoke_level.cpp
//
// `--smoke-level` — headless loader smoke check.
//
// WHY
//   `--verify-level --skip-launch` verifies the QSC parse (it decompiles
//   objects.qvm and cross-references a game-run log) but never exercises the model
//   or texture loaders, so a resolution regression passes every gate while the
//   viewport renders empty.  That is what happened when
//   Renderer_Objects::FindMeshData looked up a bare "<id>.mef" instead of the
//   engine's resource identity "LOCAL:models/<id>.mef" (Mesh_Load HOOK 0x4CF390,
//   src/model/model.c:364; matched on the folded full name, String_ToUpper
//   0x4B5AF0, src/res/resource.c:98-104, Resource_IsLoaded 0x4B5B90, :136-138):
//   61 models resolved to nothing and no command noticed.
//
// WHAT
//   For each requested level: load the level's objects the way the editor does at
//   runtime (objects.qvm -> QSC -> LevelObjects::Load, i.e. Level::Load minus the
//   terrain and the renderer), then resolve every object's model through
//   Renderer_Objects' own resolution path (no OpenGL context, no Init()) and report
//   per-level totals.  Missing models/textures are printed by name and the command
//   exits non-zero.
//
//   Beyond pass/fail it answers WHY: which .res archives were indexed for the
//   level, which branch of the resolution path answered each model (the level's own
//   archive, the owner level's archive, another level's archive, the shared common
//   archive, or a loose file on disk), how many distinct textures were involved,
//   how long the level took, and a whole-run total whose distinct counts are
//   unions across levels.  Per-model provenance goes to the log at DEBUG (and to
//   stdout under --smoke-verbose), deduplicated per model so the default output
//   stays readable.
//
// WHAT IT DOES NOT PROVE
//   Resolution only.  It does not parse the mesh, bind a texture, upload anything,
//   or draw a frame — a model that resolves but fails to parse still passes here.
//   It also mirrors the *editor's* search (which falls back across levels and to
//   loose files on disk), not the game's single-archive lookup: that is exactly why
//   the per-model source breakdown is printed instead of one "resolved" number.
#include "pch.h"
#include "cli_handler.h"
#include "config.h"
#include "smoke_level_core.h"
#include "renderer/renderer_objects.h"
#include "utils.h"
#include "level/level_common.h"
#include "level/level_objects.h"
#include "level/qvm_parser.h"
#include "level/qvm_decompiler.h"
#include "logger.h"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

void PrintSep() { std::cout << std::string(64, '=') << "\n"; }

// Decompile the level's objects.qvm and load it into LevelObjects exactly the way
// Level::Load does: Level::DecompileObjects (source/level/level.cpp:174-210) runs
// QVM_Parse + QVM_Decompile into a QSC file, then Level::Load QSC::Loads it
// (level.cpp:131-136) and hands it to LevelObjects::Load (level.cpp:156).
bool LoadLevelObjects(const std::string& igiPath, int levelNo, const std::string& qscOutPath,
                      LevelObjects& out, std::string& error) {
    const std::string qvmPath = igiPath + "\\missions\\location0\\level" +
                                std::to_string(levelNo) + "\\objects.qvm";
    if (!fs::exists(qvmPath)) {
        error = "objects.qvm not found: " + qvmPath;
        return false;
    }

    QVMFile qvm;
    try {
        qvm = QVM_Parse(qvmPath);
    } catch (const std::exception& ex) {
        error = std::string("QVM_Parse threw: ") + ex.what();
        return false;
    }
    if (!qvm.valid) {
        error = "cannot parse QVM: " + qvm.error;
        return false;
    }

    std::error_code ec;
    fs::create_directories(fs::path(qscOutPath).parent_path(), ec);
    if (!QVM_Decompile(qvm, qscOutPath)) {
        error = "QVM decompile failed for level " + std::to_string(levelNo);
        return false;
    }

    QSC qsc;
    qsc.Load(qscOutPath.c_str());
    if (qsc.HadOverflow()) {
        error = "QSC parse overflowed (incomplete parse): " + qscOutPath;
        return false;
    }
    if (qsc.GetRootFuncCount() <= 0) {
        error = "QSC produced no root tasks: " + qscOutPath;
        return false;
    }

    // LevelObjects::Load's first parameter is an ILevelDynCube* that Load never
    // reads (source/level/level_objects.cpp:21-139); it exists for its other
    // callers.  The headless path therefore passes nullptr instead of constructing
    // a Level, which would load terrain and need the renderer.
    out.Load(nullptr, &qsc);
    return true;
}

void PrintMissing(const char* title, const std::vector<SmokeMissingEntry>& entries) {
    if (entries.empty()) return;
    std::cout << "  " << title << " (" << entries.size() << " distinct):\n";
    for (const auto& entry : entries) {
        std::cout << "    - " << std::left << std::setw(16) << entry.id
                  << " x" << entry.placements
                  << "  e.g. " << entry.exampleType;
        if (!entry.exampleName.empty()) std::cout << " \"" << entry.exampleName << "\"";
        if (!entry.levels.empty()) {
            std::cout << "  [level" << (entry.levels.size() > 1 ? "s " : " ");
            for (size_t i = 0; i < entry.levels.size(); ++i) {
                if (i) std::cout << ",";
                std::cout << entry.levels[i];
            }
            std::cout << "]";
        }
        std::cout << std::right << "\n";
    }
}

// The mesh-source breakdown, keyed by the archive that answered.  The engine only
// ever resolves from the level being played's archive (Mesh_Load HOOK 0x4CF390,
// src/model/model.c:364), so a non-zero "other level" or "loose file" is the
// editor's fallback answering where the game would find nothing.
std::string DescribeMeshSources(int thisLevel, int ownerLevel, int otherLevel,
                                int common, int loose) {
    return "this level " + std::to_string(thisLevel) +
           ", owner level " + std::to_string(ownerLevel) +
           ", other level " + std::to_string(otherLevel) +
           ", common archive " + std::to_string(common) +
           ", loose file " + std::to_string(loose);
}

// "level7 textures (1234)" / "common models (800)" — which .res archive, the level
// it belongs to, and how many members it contributed.
std::string DescribeArchive(const Renderer_Objects::SmokeArchiveInfo& archive) {
    const int level = SmokeSourceLevelFromPath(archive.resPath);
    const std::string where = level > 0 ? ("level" + std::to_string(level)) : "common";
    return where + (archive.isTexture ? " textures (" : " models (") +
           std::to_string(archive.entries) + ")";
}

std::string JoinArchives(const std::vector<Renderer_Objects::SmokeArchiveInfo>& archives) {
    std::string out;
    for (size_t i = 0; i < archives.size(); ++i) {
        if (i) out += ", ";
        out += DescribeArchive(archives[i]);
    }
    return out;
}

// Archives the resolution walk indexed that were not there before it ran — i.e. the
// foreign levels a cross-level fallback had to pull in.
std::vector<Renderer_Objects::SmokeArchiveInfo> ArchivesAddedByResolution(
    const std::vector<Renderer_Objects::SmokeArchiveInfo>& before,
    const std::vector<Renderer_Objects::SmokeArchiveInfo>& after) {
    std::vector<Renderer_Objects::SmokeArchiveInfo> added;
    for (const auto& candidate : after) {
        bool seen = false;
        for (const auto& existing : before) {
            if (SameArchivePath(existing.resPath, candidate.resPath)) { seen = true; break; }
        }
        if (!seen) added.push_back(candidate);
    }
    return added;
}

// One line naming the branch that answered, for the per-model DEBUG detail.  The
// DAT owner is shown too: it is the renderer's "first level whose .dat lists the
// model" (EnsureGlobalTextureMapLoaded, renderer_objects_texture.cpp:88-91), which
// is a different fact from which archive holds the bytes.
std::string DescribeModelSource(const SmokeModelResult& model) {
    if (!model.meshResolved) return "UNRESOLVED";
    std::string out = model.meshSource + " [";
    out += SmokeMeshSourceLabel(model.meshSourceKind);
    if (model.meshSourceLevel > 0) out += ", level " + std::to_string(model.meshSourceLevel);
    out += "]";
    if (model.meshOwnerLevel > 0 && model.meshOwnerLevel != model.meshSourceLevel) {
        out += " (DAT owner level " + std::to_string(model.meshOwnerLevel) + ")";
    }
    return out;
}

}  // namespace

int CLIHandler::SmokeLevel(const SmokeLevelParams& params) {
    std::string igiPath = params.gamePath.empty() ? Utils::GetIGIRootPath() : params.gamePath;

    // The renderer's resolution helpers read Utils::GetIGIRootPath(), so --game-path
    // has to be exported before any of them runs (same as VerifyLevel).
    if (!igiPath.empty()) {
#if defined(_WIN32)
        _putenv_s("IGI_GAME_PATH", igiPath.c_str());
#else
        setenv("IGI_GAME_PATH", igiPath.c_str(), 1);
#endif
        if (!params.gamePath.empty()) Config::Init();
    }

    std::cout << "\n";
    PrintSep();
    std::cout << " IGI HEADLESS LOADER SMOKE (--smoke-level)\n";
    PrintSep();
    std::cout << "  IGI path  : " << igiPath << "\n";
    std::cout << "  Levels    :";
    for (int n : params.levels) std::cout << " " << n;
    std::cout << "\n";
    std::cout << "  Renderer  : resolution only, no OpenGL context (Init() is never called)\n";
    std::cout << "  Detail    : " << (params.verbose
        ? "per-model provenance on stdout (--smoke-verbose)"
        : "per-level summaries; --smoke-verbose for per-model provenance")
              << "\n";
    std::cout.flush();

    // No Init(): the smoke path must not need a GL context.  Every call below is a
    // resolution/read helper (FindMeshData / FindModelFile / GetTextureIdsForModel /
    // FindTextureData / FindTextureFile), never a mesh parse or a texture upload.
    Renderer_Objects robj;

    int failedLevels = 0;
    int cleanLevels = 0;
    std::vector<SmokeTotals> perLevel;
    perLevel.reserve(params.levels.size());

    for (size_t i = 0; i < params.levels.size(); ++i) {
        const int levelNo = params.levels[i];
        const auto levelStart = std::chrono::steady_clock::now();

        std::cout << "\n";
        PrintSep();
        std::cout << " SMOKE LEVEL " << levelNo << "\n";
        PrintSep();
        std::cout.flush();

        // Fresh per level: LoadResCache is append-only, so a level switch has to
        // clear first — the same sequence the editor's level switch performs.
        robj.ClearCaches();
        robj.SetLevel(levelNo);
        robj.LoadResCache(levelNo, igiPath);

        // The level's own four archives (textures+models for the level and for the
        // shared common archive).  A cross-level fallback appends more later.
        const std::vector<Renderer_Objects::SmokeArchiveInfo> archivesBefore =
            robj.SmokeIndexedArchives();
        std::cout << "  .res indexed      : " << archivesBefore.size() << " archive(s) — "
                  << JoinArchives(archivesBefore) << "\n";
        std::cout.flush();

        SmokeLevelResult result;
        result.levelNo = levelNo;

        LevelObjects levelObjects;
        std::string error;
        std::string qscPath;
        try {
            qscPath = (fs::temp_directory_path() /
                       ("igi1ed_smoke_l" + std::to_string(levelNo) + ".qsc")).string();
        } catch (const std::exception&) {
            // Tolerate a host with no usable temp directory.
            qscPath = Utils::GetExeDirectory() + "\\igi1ed_smoke_l" +
                      std::to_string(levelNo) + ".qsc";
        }
        bool loaded = false;
        try {
            loaded = LoadLevelObjects(igiPath, levelNo, qscPath, levelObjects, error);
        } catch (const std::exception& ex) {
            error = std::string("exception while loading level objects: ") + ex.what();
        } catch (...) {
            error = "unknown exception while loading level objects";
        }
        if (!loaded) {
            std::cerr << "  [ERROR] " << error << "\n";
            const long long elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - levelStart).count();
            std::cout << "  Time              : " << FormatSmokeDurationMs(elapsedMs) << "\n";
            std::cout << "\n  Result: FAIL (" << error << ")\n";
            std::cout.flush();
            ++failedLevels;
            continue;
        }
        {
            std::error_code ec;
            fs::remove(qscPath, ec);  // the QSC is a temp artifact of the check
        }

        SmokeCollectStats stats;
        const std::vector<SmokeModelRef> refs = CollectSmokeModels(
            levelObjects.GetObjects(),
            [](const std::string& id) { return Renderer_Objects::IsSkippedModelId(id); },
            &stats);

        // Per-model detail is deduplicated per level: the same model placed forty
        // times answers the same question once.  (118 identical per-object warnings
        // were already deleted from the renderer for exactly this reason.)
        std::set<std::string> detailedModels;
        std::vector<std::string> detailLines;

        result.models.reserve(refs.size());
        for (const auto& ref : refs) {
            SmokeModelResult model;
            model.ref = ref;
            try {
                const Renderer_Objects::SmokeModelResolution resolved =
                    robj.ResolveModelForSmoke(ref.modelId, ref.isBuilding);
                model.meshResolved = resolved.meshResolved;
                model.meshSource = resolved.meshSource;
                model.meshFromLooseFile = resolved.meshFromLooseFile;
                model.textureIds = resolved.textureIds;
                model.resolvedTextureIds = resolved.resolvedTextureIds;
                model.missingTextureIds = resolved.missingTextureIds;
            } catch (const std::exception& ex) {
                // A read failure on one model must not abort the level.
                model.meshResolved = false;
                model.meshSource = std::string("exception: ") + ex.what();
            } catch (...) {
                model.meshResolved = false;
                model.meshSource = "unknown exception";
            }

            // Which branch answered: archive hits carry a "res:<archive>" source,
            // the disk fallback a path.  The owner level comes from the global DAT
            // map the renderer itself uses for its cross-level walk.
            const int sourceLevel = model.meshFromLooseFile
                ? SmokeSourceLevelFromPath(model.meshSource)
                : SmokeSourceLevelFromPath(SmokeArchivePathFromSource(model.meshSource));
            const int ownerLevel = robj.SmokeModelOwnerLevel(ref.modelId);
            model.meshSourceLevel = sourceLevel;
            model.meshOwnerLevel = ownerLevel;
            model.meshSourceKind = ClassifySmokeMeshSource(
                model.meshResolved, model.meshFromLooseFile, sourceLevel, ownerLevel, levelNo);

            if (detailedModels.insert(ref.modelId).second) {
                const std::string detail = ref.modelId + " (" +
                    std::string(ref.isBuilding ? "building" : "object") + ") <- " +
                    DescribeModelSource(model);
                // Log gets every distinct model at DEBUG (the default INFO threshold
                // drops it, so a normal run's log is unchanged).
                Logger::Get().Log(LogLevel::DEBUG, "[SmokeLevel " + std::to_string(levelNo) +
                    "] " + detail);
                if (params.verbose) detailLines.push_back("    " + detail);
            }

            result.models.push_back(std::move(model));
        }

        const SmokeTotals totals = SummarizeSmokeLevel(result);
        perLevel.push_back(totals);

        const std::vector<Renderer_Objects::SmokeArchiveInfo> archivesAfter =
            robj.SmokeIndexedArchives();
        const std::vector<Renderer_Objects::SmokeArchiveInfo> archivesAdded =
            ArchivesAddedByResolution(archivesBefore, archivesAfter);

        const long long elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - levelStart).count();

        std::cout << "  Objects loaded    : " << stats.totalObjects
                  << " (deleted " << stats.deleted
                  << ", no model id " << stats.withoutModelId
                  << ", spline " << stats.splineRendered
                  << ", collision proxy " << stats.rendererSkipped << ")\n";
        std::cout << "  Objects examined  : " << totals.objectsExamined << "\n";
        std::cout << "  Models resolved   : " << totals.objectsResolved << " placement(s), "
                  << totals.distinctResolved << " distinct\n";
        std::cout << "  Models MISSING    : " << totals.modelsMissing << " placement(s), "
                  << totals.distinctMissing << " distinct\n";
        std::cout << "  Textures resolved : " << totals.texturesResolved << " placement(s), "
                  << totals.distinctTexturesResolved << " distinct\n";
        std::cout << "  Textures MISSING  : " << totals.texturesMissing << " placement(s), "
                  << totals.distinctTexturesMissing << " distinct\n";
        std::cout << "  Mesh sources      : "
                  << DescribeMeshSources(totals.resolvedFromThisLevel,
                                         totals.resolvedFromOwnerLevel,
                                         totals.resolvedFromOtherLevel,
                                         totals.resolvedFromCommonArchive,
                                         totals.resolvedFromLooseFile) << "\n";
        if (!archivesAdded.empty()) {
            std::cout << "  .res added by walk: " << archivesAdded.size() << " archive(s) — "
                      << JoinArchives(archivesAdded) << "\n";
        }
        if (!detailLines.empty()) {
            std::cout << "  Per-model         : " << detailLines.size() << " distinct\n";
            for (const auto& line : detailLines) std::cout << line << "\n";
        }
        PrintMissing("MISSING MODELS", totals.missingModelIds);
        PrintMissing("MISSING TEXTURES", totals.missingTextureIds);
        std::cout << "  Time              : " << FormatSmokeDurationMs(elapsedMs) << "\n";

        if (totals.clean()) {
            std::cout << "\n  Result: PASS\n";
            ++cleanLevels;
        } else {
            std::cout << "\n  Result: FAIL (" << totals.distinctMissing
                      << " distinct missing model(s), "
                      << totals.distinctTexturesMissing
                      << " distinct missing texture(s))\n";
            ++failedLevels;
        }
        std::cout.flush();
    }

    // Whole-run picture: distinct counts are unions across levels, so one model
    // missing on three levels reads as one missing model, not three.
    const SmokeRunTotals run = SummarizeSmokeRun(perLevel);

    PrintSep();
    std::cout << " SMOKE TOTALS — " << params.levels.size() << " level(s) requested\n";
    PrintSep();
    std::cout << "  Levels            : " << params.levels.size() << " requested, "
              << run.levelsCounted << " counted, " << cleanLevels << " clean, "
              << failedLevels << " failed\n";
    std::cout << "  Objects examined  : " << run.objectsExamined << "\n";
    std::cout << "  Models resolved   : " << run.objectsResolved << " placement(s), "
              << run.distinctResolved << " distinct\n";
    std::cout << "  Models MISSING    : " << run.modelsMissing << " placement(s), "
              << run.distinctMissing << " distinct\n";
    std::cout << "  Textures resolved : " << run.texturesResolved << " placement(s), "
              << run.distinctTexturesResolved << " distinct\n";
    std::cout << "  Textures MISSING  : " << run.texturesMissing << " placement(s), "
              << run.distinctTexturesMissing << " distinct\n";
    std::cout << "  Distinct models   : " << run.distinctModels << " examined ("
              << run.distinctResolved << " resolved, " << run.distinctMissing << " missing)\n";
    std::cout << "  Distinct textures : " << run.distinctTextures << " examined ("
              << run.distinctTexturesResolved << " resolved, "
              << run.distinctTexturesMissing << " missing)\n";
    std::cout << "  Mesh sources      : "
              << DescribeMeshSources(run.resolvedFromThisLevel,
                                     run.resolvedFromOwnerLevel,
                                     run.resolvedFromOtherLevel,
                                     run.resolvedFromCommonArchive,
                                     run.resolvedFromLooseFile) << "\n";
    PrintMissing("MISSING MODELS", run.missingModelIds);
    PrintMissing("MISSING TEXTURES", run.missingTextureIds);

    PrintSep();
    if (failedLevels == 0) {
        std::cout << " SMOKE RESULT: PASS (all " << params.levels.size() << " level(s))\n";
    } else {
        std::cout << " SMOKE RESULT: FAIL (" << failedLevels << " of "
                  << params.levels.size() << " level(s))\n";
    }
    PrintSep();
    return failedLevels == 0 ? 0 : 1;
}
