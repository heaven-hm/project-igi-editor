#include "verify_level_core.h"
#include "level/task_schema.h"
#include "level/qvm_parser.h"
#include "level/qvm_decompiler.h"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <regex>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <iostream>

namespace fs = std::filesystem;

using TaskSchemaNS::FieldDef;
using TaskSchemaNS::TaskSchema;
using TaskSchemaNS::TypeArgCount;
using TaskSchemaNS::GetBuiltinSchemas;

// ---------------------------------------------------------------------------
// Internal static helpers
// ---------------------------------------------------------------------------

static long long ToInt(double v) { return (long long)std::llround(v); }

static double DistSq2D(const VerifyObj& a, const VerifyObj& b) {
    double dx = (double)(a.px - b.px), dy = (double)(a.py - b.py);
    return dx*dx + dy*dy;
}

static std::string Trim(std::string s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(0, 1);
    while (!s.empty() && (s.back()  == ' ' || s.back()  == '\t' ||
                          s.back()  == '\r' || s.back() == '\n')) s.pop_back();
    return s;
}

static std::string UnquoteStr(const std::string& token) {
    std::string s = Trim(token);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        return s.substr(1, s.size() - 2);
    return s;
}

// Returns true for model IDs in the canonical XXX_XX_X format (digits only).
static bool IsModelId(const std::string& s) {
    size_t p1 = s.find('_');
    if (p1 == 0 || p1 == std::string::npos) return false;
    size_t p2 = s.find('_', p1 + 1);
    if (p2 == std::string::npos || p2 == p1 + 1) return false;
    if (s.size() <= p2 + 1) return false;
    for (size_t i = 0;      i < p1;       ++i) if (!std::isdigit((unsigned char)s[i])) return false;
    for (size_t i = p1 + 1; i < p2;       ++i) if (!std::isdigit((unsigned char)s[i])) return false;
    for (size_t i = p2 + 1; i < s.size(); ++i) if (!std::isdigit((unsigned char)s[i])) return false;
    return true;
}

// Walk text from pos (right after the opening '(' of a call) and collect one
// token per top-level comma-separated argument.  Nested function calls (contain
// an unmatched '(') are returned as "".  pos is advanced past the closing ')'.
static std::vector<std::string> ExtractArgs(const std::string& text, size_t& pos) {
    std::vector<std::string> args;
    std::string cur;
    int depth = 0;
    bool inQ = false, esc = false, isFn = false;
    while (pos < text.size()) {
        char c = text[pos++];
        if (inQ) {
            cur += c;
            if (esc) esc = false;
            else if (c == '\\') esc = true;
            else if (c == '"')  inQ = false;
        } else if (c == '"') {
            inQ = true; cur += c;
        } else if (c == '(') {
            if (depth == 0) isFn = true;
            depth++; cur += c;
        } else if (c == ')') {
            if (depth == 0) {
                args.push_back(isFn ? "" : Trim(cur));
                return args;
            }
            depth--; cur += c;
        } else if (c == ',' && depth == 0) {
            args.push_back(isFn ? "" : Trim(cur));
            cur.clear(); isFn = false;
        } else {
            cur += c;
        }
    }
    args.push_back(isFn ? "" : Trim(cur));
    return args;
}

// Parse Task_DeclareParameters from QSC text (only present in source .qsc files,
// not in QVM-decompiled output).
static std::map<std::string, TaskSchema> ParseSchemas(const std::string& text) {
    std::map<std::string, TaskSchema> schemas;
    const std::string marker = "Task_DeclareParameters(";
    size_t pos = 0;
    while ((pos = text.find(marker, pos)) != std::string::npos) {
        size_t aPos = pos + marker.size();
        auto args = ExtractArgs(text, aPos);
        pos = aPos;
        if (args.size() < 3 || args[0].empty()) continue;
        std::string typeName = UnquoteStr(args[0]);
        TaskSchema schema;
        int off = 3;  // 0=taskId, 1=type, 2=note; first field starts at 3
        for (size_t i = 1; i + 1 < args.size(); i += 2) {
            FieldDef f;
            f.name      = UnquoteStr(args[i]);
            f.typeName  = UnquoteStr(args[i + 1]);
            f.argCount  = TypeArgCount(f.typeName);
            f.argOffset = off;
            off += f.argCount;
            schema.push_back(f);
        }
        schemas[typeName] = std::move(schema);
    }
    return schemas;
}

// ---------------------------------------------------------------------------
// Public functions
// ---------------------------------------------------------------------------

bool PosMatch(const VerifyObj& a, const VerifyObj& b, long long tol) {
    return std::abs(a.px - b.px) <= tol &&
           std::abs(a.py - b.py) <= tol &&
           std::abs(a.pz - b.pz) <= tol;
}

bool OriMatch(const VerifyObj& a, const VerifyObj& b) {
    const double EPS = 0.05;  // ~2.8 degrees; tolerates float->string->float roundtrip loss
    return std::fabs(a.ox - b.ox) < EPS &&
           std::fabs(a.oy - b.oy) < EPS &&
           std::fabs(a.oz - b.oz) < EPS;
}

std::map<std::string, std::string> LoadModelNames(const std::string& path) {
    std::map<std::string, std::string> result;
    std::ifstream f(path);
    if (!f.is_open()) return result;

    std::regex nameRe(R"re("ModelName"\s*:\s*"([^"]+)")re");
    std::regex idRe  (R"re("ModelId"\s*:\s*"([^"]+)")re");

    std::string line, curName, curId;
    while (std::getline(f, line)) {
        std::smatch m;
        if      (std::regex_search(line, m, nameRe)) curName = m[1].str();
        else if (std::regex_search(line, m, idRe))   curId   = m[1].str();

        if (!curName.empty() && !curId.empty()) {
            result.emplace(curId, curName);
            curName.clear(); curId.clear();
        }
    }
    return result;
}

void ApplyModelNames(std::vector<VerifyObj>& objs,
                     const std::map<std::string, std::string>& names) {
    for (auto& obj : objs) {
        auto it = names.find(obj.modelId);
        if (it != names.end()) obj.modelName = it->second;
    }
}

std::vector<VerifyObj> ParseLog(const std::string& logPath, int levelNo,
                                bool& errorOut, std::string& errorMsg) {
    errorOut = false;
    std::vector<VerifyObj> result;

    std::ifstream f(logPath);
    if (!f.is_open()) {
        errorOut = true;
        errorMsg = "Cannot open log: " + logPath;
        return result;
    }

    std::vector<std::string> lines;
    { std::string line; while (std::getline(f, line)) lines.push_back(line); }

    std::regex exactStart("LoadLevel\\(\\) START for level " +
                          std::to_string(levelNo) + "(?!\\d)");
    const std::string anyStart = "LoadLevel() START for level";

    // Find last exact start for this level
    int lastIdx = -1;
    for (int i = (int)lines.size() - 1; i >= 0; --i) {
        if (std::regex_search(lines[i], exactStart)) { lastIdx = i; break; }
    }
    if (lastIdx == -1) {
        errorOut = true;
        errorMsg = "No 'LoadLevel() START for level " + std::to_string(levelNo) + "' in log";
        return result;
    }

    // Walk back to find the earlier start of the paired load
    int startIdx = lastIdx;
    for (int i = lastIdx - 1; i >= std::max(0, lastIdx - 2000); --i) {
        if (std::regex_search(lines[i], exactStart)) { startIdx = i; break; }
        if (lines[i].find(anyStart) != std::string::npos) break;
    }

    // End at the next level-start boundary
    int endIdx = (int)lines.size();
    for (int i = startIdx + 1; i < (int)lines.size(); ++i) {
        if (lines[i].find(anyStart) != std::string::npos) { endIdx = i; break; }
    }

    std::regex objRe(
        R"(\[LevelLoader\] Object Loaded: ModelID=([^,]+), Type=([^,]+), Name=([^,]*), Pos=\(([^,]+),\s*([^,]+),\s*([^)]+)\)(?:,\s*Ori=\(([^,]+),\s*([^,]+),\s*([^)]+)\))?(?:,\s*Tex=([^,]+))?(?:,\s*Model=([^,\r\n]+))?)"
    );

    for (int i = startIdx; i < endIdx; ++i) {
        std::smatch m;
        if (!std::regex_search(lines[i], m, objRe)) {
            if (lines[i].find("[LevelLoader] Object Loaded:") != std::string::npos) {
                std::cout << "REGEX FAILED ON: " << lines[i] << "\n";
            }
            continue;
        }

        VerifyObj obj;
        obj.modelId = Trim(m[1].str());
        obj.type    = Trim(m[2].str());
        obj.name    = Trim(m[3].str());
        try {
            obj.px = (long long)std::llround(std::stod(m[4].str()));
            obj.py = (long long)std::llround(std::stod(m[5].str()));
            obj.pz = (long long)std::llround(std::stod(m[6].str()));
            if (m[7].matched) {
                obj.ox = std::stod(m[7].str());
                obj.oy = std::stod(m[8].str());
                obj.oz = std::stod(m[9].str());
                obj.ori_logged = true;
            }
        } catch (...) {}
        if (m[10].matched) { obj.texId  = Trim(m[10].str()); obj.tex_logged  = true; }
        if (m[11].matched) { obj.meshId = Trim(m[11].str()); obj.mesh_logged = true; }
        result.push_back(obj);
    }
    return result;
}

void CrossRef(LevelReport::Category& cat,
              const std::vector<VerifyObj>& logged,
              bool matchOri,
              long long posTol) {
    std::vector<bool> consumed(logged.size(), false);

    for (const auto& exp : cat.expected) {
        // Train (posIsRail): the QSC position is a 1D rail coordinate, not a world XYZ.
        // The editor converts it to a 3D world position via EvaluateTrainTrackPositions.
        // We can only verify that the model ID appears somewhere in the log.
        if (exp.posIsRail) {
            auto it = std::find_if(logged.begin(), logged.end(), [&](const VerifyObj& q) {
                size_t idx = &q - &logged[0];
                return !consumed[idx] && q.modelId == exp.modelId;
            });
            if (it != logged.end()) {
                consumed[&*it - &logged[0]] = true;
                cat.found.push_back(exp);
            } else {
                cat.missing.push_back(exp);
            }
            continue;
        }

        // Full match: same model, same pos (within tolerance), ori checked only when logged AND matches
        auto it = std::find_if(logged.begin(), logged.end(), [&](const VerifyObj& q) {
            size_t idx = &q - &logged[0];
            return !consumed[idx] && q.modelId == exp.modelId && PosMatch(q, exp, posTol) &&
                   (!matchOri || (q.ori_logged && OriMatch(q, exp)));
        });
        if (it != logged.end()) {
            size_t fidx = &*it - &logged[0];
            consumed[fidx] = true;
            bool texOk  = !(exp.tex_logged && it->tex_logged && exp.texId  != it->texId);
            bool meshOk = !(exp.mesh_logged && it->mesh_logged && exp.meshId != it->meshId);
            if (!texOk)  cat.tex_mismatch.push_back({exp, *it});
            if (!meshOk) cat.mesh_mismatch.push_back({exp, *it});
            if (texOk && meshOk) cat.found.push_back(exp);
            continue;
        }

        // Pos match but ori missing or differs -> ori_mismatch
        if (matchOri) {
            auto it2 = std::find_if(logged.begin(), logged.end(), [&](const VerifyObj& q) {
                size_t idx = &q - &logged[0];
                return !consumed[idx] && q.modelId == exp.modelId && PosMatch(q, exp, posTol);
            });
            if (it2 != logged.end()) {
                consumed[&*it2 - &logged[0]] = true;
                cat.ori_mismatch.push_back({exp, *it2});
                continue;
            }
        }

        // Model match, find closest by XY
        std::vector<VerifyObj*> byModel;
        for (size_t i = 0; i < logged.size(); ++i) {
            if (!consumed[i] && logged[i].modelId == exp.modelId) byModel.push_back(const_cast<VerifyObj*>(&logged[i]));
        }
        if (!byModel.empty()) {
            auto cl = std::min_element(byModel.begin(), byModel.end(), [&](VerifyObj* a, VerifyObj* b) {
                return DistSq2D(*a, exp) < DistSq2D(*b, exp);
            });
            consumed[*cl - &logged[0]] = true;
            cat.pos_mismatch.push_back({exp, **cl});
            if (matchOri && (**cl).ori_logged && !OriMatch(**cl, exp)) {
                cat.ori_mismatch.push_back({exp, **cl});
            }
            continue;
        }

        cat.missing.push_back(exp);
    }
}

std::vector<VerifyObj> ParseQscObjects(
        const std::string& qscPath,
        const std::map<std::string, std::string>& modelNames) {
    std::vector<VerifyObj> result;
    std::ifstream f(qscPath);
    if (!f.is_open()) return result;
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    // Merge schemas: source QSC text first, hardcoded builtins as fallback for
    // types not declared in text (QVM-decompiled files lack Task_DeclareParameters).
    auto schemas = ParseSchemas(text);
    for (auto& [type, schema] : GetBuiltinSchemas())
        if (schemas.find(type) == schemas.end())
            schemas[type] = schema;

    const std::string marker = "Task_New(";
    size_t pos = 0;
    while ((pos = text.find(marker, pos)) != std::string::npos) {
        size_t aPos = pos + marker.size();
        auto args = ExtractArgs(text, aPos);
        // Advance only past the marker, NOT past aPos — if we jumped to aPos we
        // would skip all nested Task_New calls that live inside the arg list of a
        // Container or similar wrapper.
        pos += marker.size();
        if (args.size() < 3) continue;

        std::string typeStr = UnquoteStr(args[1]);
        auto sit = schemas.find(typeStr);
        if (sit == schemas.end()) continue;
        const TaskSchema& schema = sit->second;

        double px = 0, py = 0, pz = 0; bool hasPos = false;
        double ox = 0, oy = 0, oz = 0; bool hasOri = false;
        std::string modelId;

        for (const auto& fd : schema) {
            // Position: 3D ObjectPos field
            if (!hasPos && fd.typeName == "ObjectPos") {
                if (fd.argCount == 3 && fd.argOffset + 2 < (int)args.size() &&
                    !args[fd.argOffset].empty() && !args[fd.argOffset+1].empty() && !args[fd.argOffset+2].empty()) {
                    try {
                        px = std::stod(args[fd.argOffset]);
                        py = std::stod(args[fd.argOffset+1]);
                        pz = std::stod(args[fd.argOffset+2]);
                        hasPos = true;
                    } catch (...) {}
                }
            }
            // Train 1D rail position — mark hasPos so object isn't filtered out,
            // store the raw rail coordinate in px and flag posIsRail.
            bool isRailPos = false;
            if (!hasPos && fd.typeName == "TrainPos1D") {
                if (fd.argOffset < (int)args.size() && !args[fd.argOffset].empty()) {
                    try {
                        px = std::stod(args[fd.argOffset]);
                        hasPos  = true;
                        isRailPos = true;
                    } catch (...) {}
                }
            }
            // Orientation: Real32x9 (3 Euler angles) or single-float Heading/Gamma
            if (!hasOri && (fd.typeName == "Real32x9" ||
                            fd.name.find("Orientation") != std::string::npos ||
                            fd.name.find("Heading")     != std::string::npos ||
                            fd.name.find("Gamma")       != std::string::npos)) {
                if (fd.argCount == 3 && fd.argOffset + 2 < (int)args.size() &&
                    !args[fd.argOffset].empty() && !args[fd.argOffset+1].empty() && !args[fd.argOffset+2].empty()) {
                    try {
                        ox = std::stod(args[fd.argOffset]);
                        oy = std::stod(args[fd.argOffset+1]);
                        oz = std::stod(args[fd.argOffset+2]);
                        hasOri = true;
                    } catch (...) {}
                } else if (fd.argCount == 1 && fd.argOffset < (int)args.size() &&
                           !args[fd.argOffset].empty()) {
                    try { oz = std::stod(args[fd.argOffset]); hasOri = true; } catch (...) {}
                }
            }
            // Model: first String16/String256/VarString value matching XXX_XX_X
            if (modelId.empty() &&
                (fd.typeName == "String16" || fd.typeName == "String256" || fd.typeName == "VarString") &&
                fd.argOffset < (int)args.size() && !args[fd.argOffset].empty()) {
                std::string val = UnquoteStr(args[fd.argOffset]);
                if (IsModelId(val)) modelId = val;
            }
            (void)isRailPos; // will be captured via lambda below
        }

        if (typeStr == "SCamera" && args.size() > 10) {
            try {
                oz = std::stod(args[6]);
                ox = std::stod(args[8]);
                oy = std::stod(args[9]);
                hasOri = true;
            } catch (...) {}
            std::string val = UnquoteStr(args[10]);
            if (IsModelId(val)) modelId = val;
        }

        if (!hasPos || modelId.empty()) continue;

        // Determine if this is a rail-position type
        bool isRailType = (typeStr == "Train");

        VerifyObj v;
        v.modelId   = modelId;
        v.type      = typeStr;
        v.name      = UnquoteStr(args[2]);
        v.px = (long long)std::llround(px);
        v.py = (long long)std::llround(py);
        v.pz = (long long)std::llround(pz);
        v.ox = ox; v.oy = oy; v.oz = oz;
        v.ori_logged = hasOri;
        v.posIsRail  = isRailType;
        auto mnIt = modelNames.find(modelId);
        if (mnIt != modelNames.end()) v.modelName = mnIt->second;
        result.push_back(v);
    }
    return result;
}

LevelReport VerifyOneLevel(const std::string& igiPath,
                           const std::string& exeDir,
                           const std::string& logPath,
                           int levelNo,
                           const std::map<std::string, std::string>& modelNames) {
    LevelReport report;
    report.levelNo = levelNo;
    report.buildings.label = "BUILDINGS";
    report.objects.label   = "OBJECTS";
    report.ai.label        = "AI";

    // 1. Decompile objects.qvm
    std::string qvmPath = igiPath + "\\missions\\location0\\level" +
                          std::to_string(levelNo) + "\\objects.qvm";
    std::string qscPath = exeDir + "\\objects_verify_l" + std::to_string(levelNo) + ".qsc";

    std::cout << "  Decompiling: " << qvmPath << "\n";
    std::cout.flush();

    if (!fs::exists(qvmPath)) {
        report.logError    = true;
        report.logErrorMsg = "objects.qvm not found: " + qvmPath;
        return report;
    }

    try {
        QVMFile qvm = QVM_Parse(qvmPath);
        if (!qvm.valid) {
            report.logError    = true;
            report.logErrorMsg = "Cannot parse QVM: " + qvm.error;
            return report;
        }
        if (!QVM_Decompile(qvm, qscPath)) {
            report.logError    = true;
            report.logErrorMsg = "QVM decompile failed for level " + std::to_string(levelNo);
            return report;
        }
        std::cout << "  Decompiled OK -> " << qscPath << "\n";
        std::cout.flush();

        // 2. Parse all Task_New objects directly from the QSC text using
        //    Task_DeclareParameters schemas — handles every type generically.
        auto allObjs = ParseQscObjects(qscPath, modelNames);


        // try { fs::remove(qscPath); } catch (...) {}

        // 3. Categorise into buildings / objects / AI by task type
        for (const auto& v : allObjs) {
            if (v.type == "Building")
                report.buildings.expected.push_back(v);
            else if (v.type == "HumanSoldier" ||
                     v.type == "HumanSoldierFemale" ||
                     v.type == "HumanPlayer")
                report.ai.expected.push_back(v);
            else
                report.objects.expected.push_back(v);
        }
    } catch (const std::exception& ex) {
        report.logError    = true;
        report.logErrorMsg = std::string("Exception during QSC parse: ") + ex.what();
        std::cerr << "  [ERROR] " << report.logErrorMsg << "\n";
        std::cerr.flush();
        return report;
    } catch (...) {
        report.logError    = true;
        report.logErrorMsg = "Unknown exception during QSC parse";
        std::cerr << "  [ERROR] " << report.logErrorMsg << "\n";
        std::cerr.flush();
        return report;
    }

    std::cout << "  QSC: " << report.buildings.expected.size() << " buildings, "
              << report.objects.expected.size()                << " objects, "
              << report.ai.expected.size()                     << " AI\n";
    std::cout.flush();

    // 4. Parse log
    bool logErr = false;
    std::string logErrMsg;
    std::vector<VerifyObj> logged = ParseLog(logPath, levelNo, logErr, logErrMsg);

    if (logErr) {
        report.logError   = true;
        report.logErrorMsg = logErrMsg;
        std::cerr << "  [WARN] " << logErrMsg << "\n";
        // Still run cross-ref with empty logged list so we get full missing lists
    }

    ApplyModelNames(logged, modelNames);
    report.logEntries = (int)logged.size();
    std::cout << "  Log: " << logged.size() << " entries parsed\n";

    // 5. Cross-reference
    CrossRef(report.buildings, logged, true,  0);
    CrossRef(report.objects,   logged, true,  0);
    CrossRef(report.ai,        logged, false, 50); // AI are terrain-snapped on load; allow ±50 units

    return report;
}
