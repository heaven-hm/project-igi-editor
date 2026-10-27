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
// WHAT IT DOES NOT PROVE
//   Resolution only.  It does not parse the mesh, bind a texture, upload anything,
//   or draw a frame — a model that resolves but fails to parse still passes here.
//   It also mirrors the *editor's* search (which falls back across levels and to
//   loose files on disk), not the game's single-archive lookup.
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

#include <filesystem>
#include <iomanip>
#include <iostream>
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
        std::cout << "\n";
    }
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
    std::cout.flush();

    // No Init(): the smoke path must not need a GL context.  Every call below is a
    // resolution/read helper (FindMeshData / FindModelFile / GetTextureIdsForModel /
    // FindTextureData / FindTextureFile), never a mesh parse or a texture upload.
    Renderer_Objects robj;

    int failedLevels = 0;
    for (size_t i = 0; i < params.levels.size(); ++i) {
        const int levelNo = params.levels[i];

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

        result.models.reserve(refs.size());
        for (const auto& ref : refs) {
            SmokeModelResult model;
            model.ref = ref;
            try {
                const Renderer_Objects::SmokeModelResolution resolved =
                    robj.ResolveModelForSmoke(ref.modelId, ref.isBuilding);
                model.meshResolved = resolved.meshResolved;
                model.meshSource = resolved.meshSource;
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
            result.models.push_back(std::move(model));
        }

        const SmokeTotals totals = SummarizeSmokeLevel(result);

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
        std::cout << "  Textures resolved : " << totals.texturesResolved << "\n";
        std::cout << "  Textures MISSING  : " << totals.texturesMissing << "\n";
        PrintMissing("MISSING MODELS", totals.missingModelIds);
        PrintMissing("MISSING TEXTURES", totals.missingTextureIds);

        if (totals.clean()) {
            std::cout << "\n  Result: PASS\n";
        } else {
            std::cout << "\n  Result: FAIL (" << totals.distinctMissing
                      << " distinct missing model(s), "
                      << totals.missingTextureIds.size()
                      << " distinct missing texture(s))\n";
            ++failedLevels;
        }
        std::cout.flush();
    }

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
