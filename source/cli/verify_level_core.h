// source/cli/verify_level_core.h
#pragma once

#include <string>
#include <vector>
#include <map>

// VerifyObj — one object entry (from QSC or from editor log)
struct VerifyObj {
    std::string modelId;
    std::string modelName;  // from IGIModels.json
    std::string name;
    std::string type;
    long long px = 0, py = 0, pz = 0;
    double ox = 0, oy = 0, oz = 0;  // radians, as stored in QSC
    bool ori_logged = false;          // false → editor did not emit Ori= in log
    bool posIsRail  = false;          // true → Train: 1D rail pos, skip position cross-ref
    std::string texId;
    std::string meshId;
    bool tex_logged  = false;
    bool mesh_logged = false;
};

// LevelReport — full cross-reference result for one level
struct LevelReport {
    int levelNo = 0;

    struct Category {
        std::string label;
        std::vector<VerifyObj> expected;
        std::vector<VerifyObj> found;
        std::vector<std::pair<VerifyObj,VerifyObj>> pos_mismatch;
        std::vector<std::pair<VerifyObj,VerifyObj>> ori_mismatch;
        std::vector<std::pair<VerifyObj,VerifyObj>> tex_mismatch;
        std::vector<std::pair<VerifyObj,VerifyObj>> mesh_mismatch;
        std::vector<VerifyObj> missing;
    };

    Category buildings;
    Category objects;
    Category ai;

    bool logError = false;
    std::string logErrorMsg;
    int logEntries = 0;
    bool logChecksSkipped = false;
};

// ---------------------------------------------------------------------------
// Helpers (exposed for testing)
// ---------------------------------------------------------------------------

// tol=0 → exact; tol>0 → allow ±tol on each axis (used for AI terrain-snap tolerance)
bool PosMatch(const VerifyObj& a, const VerifyObj& b, long long tol = 0);
bool OriMatch(const VerifyObj& a, const VerifyObj& b);

std::map<std::string, std::string> LoadModelNames(const std::string& jsonPath);
void ApplyModelNames(std::vector<VerifyObj>& objs,
                     const std::map<std::string, std::string>& names);

// ---------------------------------------------------------------------------
// Core pipeline (exposed for testing)
// ---------------------------------------------------------------------------

// Parse the editor log at logPath, extract all objects emitted for levelNo.
// Sets errorOut=true and fills errorMsg on failure; still returns whatever was parsed.
std::vector<VerifyObj> ParseLog(const std::string& logPath, int levelNo,
                                bool& errorOut, std::string& errorMsg);

// Parse every Task_New with a model ID from the QSC file at qscPath.
// Returns empty vector (does NOT crash) if the file does not exist.
std::vector<VerifyObj> ParseQscObjects(
    const std::string& qscPath,
    const std::map<std::string, std::string>& modelNames);

// Cross-reference cat.expected against logged.
// matchOri=true for Buildings/Objects, false for AI.
// posTol: position tolerance in game units (0=exact; 50 recommended for AI).
void CrossRef(LevelReport::Category& cat,
              const std::vector<VerifyObj>& logged,
              bool matchOri,
              long long posTol = 0);

// Full single-level pipeline: decompile QVM → parse QSC → optionally parse log and cross-ref.
LevelReport VerifyOneLevel(const std::string& igiPath,
                           const std::string& exeDir,
                           const std::string& logPath,
                           int levelNo,
                           const std::map<std::string, std::string>& modelNames,
                           bool requireLog = true);
