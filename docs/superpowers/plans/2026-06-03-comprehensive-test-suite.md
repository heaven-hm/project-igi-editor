# Comprehensive Test Suite Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add 9 new test files covering the verify-level pipeline (unit + integration) and all six file-format parsers, after first extracting the pure verify logic into a linkable core file.

**Architecture:** `verify_level.cpp` currently can't link into `igi_tests` because it includes `pch.h` (pulls in OpenGL/renderer). We extract the pure parsing logic into `verify_level_core.h/.cpp` (no GL dependency), keep Win32 launch + report output in `verify_level.cpp`, then add the core + parser sources to `igi_tests`. Parser tests use game files co-located with the exe via `Utils::GetExeDirectory()`. The integration test shells out to `igi1ed.exe --verify-level 1` via `CreateProcess` with a 15-second timeout.

**Tech Stack:** C++17, Google Test (`gtest_main`), Win32 API, CMake, MSVC

---

## File Map

| Action | Path | Responsibility |
|--------|------|----------------|
| Create | `source/cli/verify_level_core.h` | Structs + public function declarations |
| Create | `source/cli/verify_level_core.cpp` | Extracted pure logic (no pch, no GL) |
| Modify | `source/cli/verify_level.cpp` | Include core header, remove extracted code |
| Modify | `source/cli/cli_handler.h` | Move `VerifyLevel` to `public`; change `timeout` default to `15` |
| Modify | `CMakeLists.txt` | Add new sources + 9 test files to `igi_tests` |
| Create | `tests/fixtures/verify_log_l1.txt` | Synthetic log for unit tests |
| Create | `tests/test_verify_unit.cpp` | ParseLog / CrossRef / PosMatch unit tests |
| Create | `tests/test_verify_level.cpp` | Integration: launch editor, verify level 1 |
| Create | `tests/test_dat_parser.cpp` | DAT parser tests against level1.dat |
| Create | `tests/test_graph_parser.cpp` | Graph parser tests against graph1.dat |
| Create | `tests/test_res_parser.cpp` | RES parser tests against level1.res |
| Create | `tests/test_tex_parser.cpp` | TEX parser tests via RES extraction |
| Create | `tests/test_mtp_parser.cpp` | MTP parser tests via filesystem scan |
| Create | `tests/test_fnt_parser.cpp` | FNT parser tests via filesystem scan |

---

## Task 1: Create `verify_level_core.h`

**Files:**
- Create: `source/cli/verify_level_core.h`

- [ ] **Step 1: Create the header with all public structs and function declarations**

```cpp
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
};

// ---------------------------------------------------------------------------
// Helpers (exposed for testing)
// ---------------------------------------------------------------------------

bool PosMatch(const VerifyObj& a, const VerifyObj& b);
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
void CrossRef(LevelReport::Category& cat,
              const std::vector<VerifyObj>& logged,
              bool matchOri);

// Full single-level pipeline: decompile QVM → parse QSC → parse log → cross-ref.
LevelReport VerifyOneLevel(const std::string& igiPath,
                           const std::string& exeDir,
                           const std::string& logPath,
                           int levelNo,
                           const std::map<std::string, std::string>& modelNames);
```

- [ ] **Step 2: Confirm the file exists**

```powershell
Test-Path D:\Code\project-igi-editor\source\cli\verify_level_core.h
```
Expected: `True`

---

## Task 2: Create `verify_level_core.cpp`

**Files:**
- Create: `source/cli/verify_level_core.cpp`
- Reference: `source/cli/verify_level.cpp` (lines 39–413, 700–1015 — source of truth for the moved code)

This file gets the implementations that were previously `static` inside `verify_level.cpp`. Copy them **exactly** as they appear in `verify_level.cpp` (they are already correct and tested), then remove the `static` keyword only from the four functions declared in the header (`ParseLog`, `ParseQscObjects`, `CrossRef`, `VerifyOneLevel`). All other helpers stay `static`.

- [ ] **Step 1: Create the file**

```cpp
// source/cli/verify_level_core.cpp
#include "verify_level_core.h"
#include "level/task_schema.h"
#include "parsers/qvm_parser.h"
#include "parsers/qvm_decompiler.h"
#include "utils.h"
#include "logger.h"
#include "common.h"

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
// Internal helpers (static — not part of public API)
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
        int off = 3;
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
// Public helpers
// ---------------------------------------------------------------------------

bool PosMatch(const VerifyObj& a, const VerifyObj& b) {
    return a.px == b.px && a.py == b.py && a.pz == b.pz;
}

bool OriMatch(const VerifyObj& a, const VerifyObj& b) {
    const double EPS = 0.05;
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

// ---------------------------------------------------------------------------
// ParseLog
// ---------------------------------------------------------------------------

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

    int lastIdx = -1;
    for (int i = (int)lines.size() - 1; i >= 0; --i) {
        if (std::regex_search(lines[i], exactStart)) { lastIdx = i; break; }
    }
    if (lastIdx == -1) {
        errorOut = true;
        errorMsg = "No 'LoadLevel() START for level " + std::to_string(levelNo) + "' in log";
        return result;
    }

    int startIdx = lastIdx;
    for (int i = lastIdx - 1; i >= std::max(0, lastIdx - 2000); --i) {
        if (std::regex_search(lines[i], exactStart)) { startIdx = i; break; }
        if (lines[i].find(anyStart) != std::string::npos) break;
    }

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

// ---------------------------------------------------------------------------
// CrossRef
// ---------------------------------------------------------------------------

void CrossRef(LevelReport::Category& cat,
              const std::vector<VerifyObj>& logged,
              bool matchOri) {
    std::vector<bool> consumed(logged.size(), false);

    for (const auto& exp : cat.expected) {
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

        auto it = std::find_if(logged.begin(), logged.end(), [&](const VerifyObj& q) {
            size_t idx = &q - &logged[0];
            return !consumed[idx] && q.modelId == exp.modelId && PosMatch(q, exp) &&
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

        if (matchOri) {
            auto it2 = std::find_if(logged.begin(), logged.end(), [&](const VerifyObj& q) {
                size_t idx = &q - &logged[0];
                return !consumed[idx] && q.modelId == exp.modelId && PosMatch(q, exp);
            });
            if (it2 != logged.end()) {
                consumed[&*it2 - &logged[0]] = true;
                cat.ori_mismatch.push_back({exp, *it2});
                continue;
            }
        }

        std::vector<VerifyObj*> byModel;
        for (size_t i = 0; i < logged.size(); ++i) {
            if (!consumed[i] && logged[i].modelId == exp.modelId)
                byModel.push_back(const_cast<VerifyObj*>(&logged[i]));
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

// ---------------------------------------------------------------------------
// ParseQscObjects
// ---------------------------------------------------------------------------

std::vector<VerifyObj> ParseQscObjects(
        const std::string& qscPath,
        const std::map<std::string, std::string>& modelNames) {
    std::vector<VerifyObj> result;
    std::ifstream f(qscPath);
    if (!f.is_open()) return result;
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    auto schemas = ParseSchemas(text);
    for (auto& [type, schema] : GetBuiltinSchemas())
        if (schemas.find(type) == schemas.end())
            schemas[type] = schema;

    const std::string marker = "Task_New(";
    size_t pos = 0;
    while ((pos = text.find(marker, pos)) != std::string::npos) {
        size_t aPos = pos + marker.size();
        auto args = ExtractArgs(text, aPos);
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
            if (modelId.empty() &&
                (fd.typeName == "String16" || fd.typeName == "String256" || fd.typeName == "VarString") &&
                fd.argOffset < (int)args.size() && !args[fd.argOffset].empty()) {
                std::string val = UnquoteStr(args[fd.argOffset]);
                if (IsModelId(val)) modelId = val;
            }
            (void)isRailPos;
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

// ---------------------------------------------------------------------------
// VerifyOneLevel
// ---------------------------------------------------------------------------

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

        auto allObjs = ParseQscObjects(qscPath, modelNames);

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

    bool logErr = false;
    std::string logErrMsg;
    std::vector<VerifyObj> logged = ParseLog(logPath, levelNo, logErr, logErrMsg);

    if (logErr) {
        report.logError    = true;
        report.logErrorMsg = logErrMsg;
        std::cerr << "  [WARN] " << logErrMsg << "\n";
    }

    ApplyModelNames(logged, modelNames);
    report.logEntries = (int)logged.size();
    std::cout << "  Log: " << logged.size() << " entries parsed\n";

    CrossRef(report.buildings, logged, true);
    CrossRef(report.objects,   logged, true);
    CrossRef(report.ai,        logged, false);

    return report;
}
```

- [ ] **Step 2: Confirm the file exists**

```powershell
Test-Path D:\Code\project-igi-editor\source\cli\verify_level_core.cpp
```
Expected: `True`

---

## Task 3: Slim Down `verify_level.cpp`

**Files:**
- Modify: `source/cli/verify_level.cpp`

`verify_level.cpp` keeps: `#include "pch.h"`, `LaunchEditor`, all print/JSON/MD functions, and `CLIHandler::VerifyLevel`. It removes: the struct definitions, helper implementations, `ParseLog`, `CrossRef`, `ParseQscObjects`, `VerifyOneLevel`, and the TaskSchemaNS `using` declarations (those move to `verify_level_core.cpp`).

- [ ] **Step 1: Replace the opening section of `verify_level.cpp`**

The file currently starts at line 1 with `#include "pch.h"`. Replace everything from line 1 up to and including the `}` closing `VerifyOneLevel` (line ~1015) with the following trimmed header block. The section from `CLIHandler::VerifyLevel` onward (line 1021+) stays **unchanged**.

Replace lines 1–1019 with:

```cpp
#include "pch.h"
#include "cli_handler.h"
#include "verify_level_core.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <regex>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <chrono>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Local display helpers (only used for console/JSON/MD output, not testable)
// ---------------------------------------------------------------------------

static std::string FmtPos(long long x, long long y, long long z) {
    return "(" + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + ")";
}
static std::string FmtOri(double x, double y, double z) {
    auto fmt = [](double v) -> std::string {
        if (v == 0.0) return "0";
        char buf[32];
        snprintf(buf, sizeof(buf), "%.4f", v);
        std::string s(buf);
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
        return s;
    };
    return "(" + fmt(x) + ", " + fmt(y) + ", " + fmt(z) + ")";
}
static std::string JsonEsc(const std::string& s) {
    std::string o; o.reserve(s.size() + 4);
    for (char c : s) {
        if (c == '"')  { o += "\\\""; }
        else if (c == '\\') { o += "\\\\"; }
        else if (c == '\n') { o += "\\n"; }
        else if (c == '\r') { o += "\\r"; }
        else o += c;
    }
    return o;
}
static void PrintSep() { std::cout << std::string(64, '=') << "\n"; }
```

After this block, keep the rest of verify_level.cpp **exactly as-is** starting from `static void PrintTable(...)` through to the end of `CLIHandler::VerifyLevel`. (The Print/JSON/MD/SaveLevel functions at lines ~421–700 and the `LaunchEditor` function at lines ~167–216 are unaffected.)

- [ ] **Step 2: Verify the file compiles cleanly (Release build)**

```powershell
cd D:\Code\project-igi-editor
cmake --build build --config Release -j 1 2>&1 | Select-String "error C|error :" | Select-Object -First 10
```
Expected: no output (zero errors)

- [ ] **Step 3: Commit**

```powershell
cd D:\Code\project-igi-editor
git add source/cli/verify_level_core.h source/cli/verify_level_core.cpp source/cli/verify_level.cpp
git commit -m "refactor: extract verify_level_core.h/.cpp for testability"
```

---

## Task 4: Expose `VerifyLevel` and Fix Default Timeout

**Files:**
- Modify: `source/cli/cli_handler.h`

- [ ] **Step 1: Move `VerifyLevel` to `public` and change `timeout` default**

In `source/cli/cli_handler.h`:

1. Move `static int VerifyLevel(const VerifyLevelParams& params);` from the `private:` section to the `public:` section.

2. In `struct VerifyLevelParams` (lines 6–16), change `int timeout = 0;` → `int timeout = 15;`

The struct should look like:
```cpp
struct VerifyLevelParams {
    std::vector<int> levels;
    int  timeout    = 15;   // 15-second cap per level
    bool skipLaunch = false;
    std::string gamePath;
    std::string logPath;
    std::string reportJson;
    std::string reportMd;
    std::string reportDir;
    int  delay      = 5;
};
```

The `CLIHandler` class should look like:
```cpp
class CLIHandler {
public:
  static bool IsCLICommand(int argc, char **argv);
  static int Process(int argc, char **argv);
  static int VerifyLevel(const VerifyLevelParams& params);   // moved to public

private:
  static int ParseMEF(const std::string &filepath);
  // ... (all other private methods unchanged)
};
```

- [ ] **Step 2: Build to confirm no errors**

```powershell
cd D:\Code\project-igi-editor
cmake --build build --config Release -j 1 2>&1 | Select-String "error C|error :" | Select-Object -First 10
```
Expected: no output

- [ ] **Step 3: Commit**

```powershell
cd D:\Code\project-igi-editor
git add source/cli/cli_handler.h
git commit -m "feat: expose VerifyLevel as public; default timeout 15s"
```

---

## Task 5: Update CMakeLists.txt

**Files:**
- Modify: `CMakeLists.txt` (lines 199–233)

- [ ] **Step 1: Replace the `add_executable(igi_tests ...)` and `target_sources` blocks**

Find the block starting at `add_executable(igi_tests` and replace through `gtest_discover_tests(...)` with:

```cmake
add_executable(igi_tests
    tests/test_qsc_lexer.cpp
    tests/test_qsc_parser.cpp
    tests/test_qvm_roundtrip.cpp
    tests/test_config.cpp
    tests/test_utils.cpp
    tests/test_verify_unit.cpp
    tests/test_verify_level.cpp
    tests/test_dat_parser.cpp
    tests/test_graph_parser.cpp
    tests/test_res_parser.cpp
    tests/test_tex_parser.cpp
    tests/test_mtp_parser.cpp
    tests/test_fnt_parser.cpp
)

# Explicit runtime so igi_tests always matches igi-editor (/MDd Debug, /MD Release).
if (MSVC)
    set_target_properties(igi_tests PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
    )
endif()

target_include_directories(igi_tests PRIVATE source)
target_include_directories(igi_tests PRIVATE ${PROJECT_SOURCE_DIR}/third_party/glm-1.0.1)
target_include_directories(igi_tests PRIVATE ${PROJECT_SOURCE_DIR}/third_party/GL/include)
target_include_directories(igi_tests PRIVATE ${PROJECT_SOURCE_DIR}/third_party)
target_link_libraries(igi_tests PRIVATE gtest_main)
target_sources(igi_tests PRIVATE
    source/parsers/qsc_lexer.cpp
    source/parsers/qsc_parser.cpp
    source/parsers/qvm_parser.cpp
    source/parsers/qvm_compiler.cpp
    source/parsers/qvm_decompiler.cpp
    source/config.cpp
    source/utils.cpp
    source/common.cpp
    source/logger.cpp
    source/level/task_schema.cpp
    source/cli/verify_level_core.cpp
    source/parsers/dat_parser.cpp
    source/parsers/graph_parser.cpp
    source/parsers/res_parser.cpp
    source/parsers/tex_parser.cpp
    source/parsers/mtp_parser.cpp
    source/parsers/fnt_parser.cpp
)

include(GoogleTest)
gtest_discover_tests(igi_tests WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
```

- [ ] **Step 2: Commit the CMakeLists change** (before adding test files — CMake will error on missing files until they exist; that is expected and will be fixed in Task 7+)

```powershell
cd D:\Code\project-igi-editor
git add CMakeLists.txt
git commit -m "build: add parser + verify-core sources to igi_tests"
```

---

## Task 6: Create Fixture `tests/fixtures/verify_log_l1.txt`

**Files:**
- Create: `tests/fixtures/verify_log_l1.txt`

This file is parsed by `ParseLog` in unit tests. It must contain a `LoadLevel() START for level 1` marker, object lines in the exact format the `objRe` regex expects, and a second occurrence of the marker to exercise "last occurrence" logic.

- [ ] **Step 1: Create the fixture**

```
LoadLevel() START for level 1
[LevelLoader] Object Loaded: ModelID=100_01_1, Type=Building, Name=Fence, Pos=(1000, 2000, 3000), Ori=(0.0000, 1.5708, 0.0000)
[LevelLoader] Object Loaded: ModelID=200_02_1, Type=Building, Name=Wall, Pos=(5000, 6000, 7000)
[LevelLoader] Object Loaded: ModelID=300_03_1, Type=HumanSoldier, Name=Guard, Pos=(100, 200, 300), Ori=(0.0000, 0.7854, 0.0000)
[LevelLoader] Object Loaded: ModelID=400_04_1, Type=Prop, Name=Crate, Pos=(9000, 8000, 7000), Tex=tex_001, Model=crate_01_1
LoadLevel() START for level 1
[LevelLoader] Object Loaded: ModelID=100_01_1, Type=Building, Name=Fence, Pos=(1000, 2000, 3000), Ori=(0.0000, 1.5708, 0.0000)
[LevelLoader] Object Loaded: ModelID=200_02_1, Type=Building, Name=Wall, Pos=(5000, 6000, 7000)
[LevelLoader] Object Loaded: ModelID=300_03_1, Type=HumanSoldier, Name=Guard, Pos=(100, 200, 300), Ori=(0.0000, 0.7854, 0.0000)
[LevelLoader] Object Loaded: ModelID=400_04_1, Type=Prop, Name=Crate, Pos=(9000, 8000, 7000), Tex=tex_001, Model=crate_01_1
LoadLevel() START for level 2
[LevelLoader] Object Loaded: ModelID=999_99_9, Type=Building, Name=Other, Pos=(1, 2, 3), Ori=(0.0, 0.0, 0.0)
```

- [ ] **Step 2: Commit the fixture**

```powershell
cd D:\Code\project-igi-editor
git add tests/fixtures/verify_log_l1.txt
git commit -m "test: add verify_log_l1.txt fixture for unit tests"
```

---

## Task 7: Write `tests/test_verify_unit.cpp`

**Files:**
- Create: `tests/test_verify_unit.cpp`

- [ ] **Step 1: Create the test file**

```cpp
#include <gtest/gtest.h>
#include "cli/verify_level_core.h"
#include <filesystem>

// ============================================================
//  verify_level_core — unit tests (no game files, no launch)
//
//  Fixture path: tests/fixtures/verify_log_l1.txt
//  ParseQscObjects fixture: tests/fixtures/level01_simple.qsc
// ============================================================

static const std::string kLogFixture    = "tests/fixtures/verify_log_l1.txt";
static const std::string kQscFixture    = "tests/fixtures/level01_simple.qsc";
static const std::string kMissingPath   = "tests/fixtures/nonexistent_file.txt";

// ---------------------------------------------------------------------------
//  PosMatch
// ---------------------------------------------------------------------------

TEST(PosMatchTest, IdenticalPositionsMatch) {
    VerifyObj a, b;
    a.px = b.px = 1000; a.py = b.py = 2000; a.pz = b.pz = 3000;
    EXPECT_TRUE(PosMatch(a, b));
}

TEST(PosMatchTest, DifferentXDoesNotMatch) {
    VerifyObj a, b;
    a.px = 1000; b.px = 1001; a.py = b.py = 0; a.pz = b.pz = 0;
    EXPECT_FALSE(PosMatch(a, b));
}

TEST(PosMatchTest, DifferentYDoesNotMatch) {
    VerifyObj a, b;
    a.px = b.px = 0; a.py = 500; b.py = 501; a.pz = b.pz = 0;
    EXPECT_FALSE(PosMatch(a, b));
}

TEST(PosMatchTest, DifferentZDoesNotMatch) {
    VerifyObj a, b;
    a.px = b.px = 0; a.py = b.py = 0; a.pz = 100; b.pz = 101;
    EXPECT_FALSE(PosMatch(a, b));
}

// ---------------------------------------------------------------------------
//  OriMatch
// ---------------------------------------------------------------------------

TEST(OriMatchTest, IdenticalAnglesMatch) {
    VerifyObj a, b;
    a.ox = b.ox = 0.0; a.oy = b.oy = 1.5708; a.oz = b.oz = 0.0;
    EXPECT_TRUE(OriMatch(a, b));
}

TEST(OriMatchTest, SmallDifferenceWithinEpsilonMatches) {
    VerifyObj a, b;
    a.ox = 0.0; b.ox = 0.04;   // < 0.05
    a.oy = b.oy = 0.0; a.oz = b.oz = 0.0;
    EXPECT_TRUE(OriMatch(a, b));
}

TEST(OriMatchTest, DifferenceAtEpsilonDoesNotMatch) {
    VerifyObj a, b;
    a.ox = 0.0; b.ox = 0.05;   // == 0.05, not strictly less
    a.oy = b.oy = 0.0; a.oz = b.oz = 0.0;
    EXPECT_FALSE(OriMatch(a, b));
}

TEST(OriMatchTest, LargeDifferenceDoesNotMatch) {
    VerifyObj a, b;
    a.ox = 0.0; b.ox = 1.0;
    a.oy = b.oy = 0.0; a.oz = b.oz = 0.0;
    EXPECT_FALSE(OriMatch(a, b));
}

// ---------------------------------------------------------------------------
//  ParseLog — basic field extraction
// ---------------------------------------------------------------------------

TEST(ParseLogTest, ExtractsModelIdAndType) {
    bool err = false; std::string msg;
    auto objs = ParseLog(kLogFixture, 1, err, msg);
    ASSERT_FALSE(err) << msg;
    ASSERT_FALSE(objs.empty());
    EXPECT_EQ(objs[0].modelId, "100_01_1");
    EXPECT_EQ(objs[0].type,    "Building");
    EXPECT_EQ(objs[0].name,    "Fence");
}

TEST(ParseLogTest, ExtractsPosition) {
    bool err = false; std::string msg;
    auto objs = ParseLog(kLogFixture, 1, err, msg);
    ASSERT_FALSE(err) << msg;
    ASSERT_FALSE(objs.empty());
    EXPECT_EQ(objs[0].px, 1000LL);
    EXPECT_EQ(objs[0].py, 2000LL);
    EXPECT_EQ(objs[0].pz, 3000LL);
}

TEST(ParseLogTest, SetsOriLoggedTrueWhenOriPresent) {
    bool err = false; std::string msg;
    auto objs = ParseLog(kLogFixture, 1, err, msg);
    ASSERT_FALSE(err) << msg;
    ASSERT_FALSE(objs.empty());
    EXPECT_TRUE(objs[0].ori_logged);
    EXPECT_NEAR(objs[0].oy, 1.5708, 0.001);
}

TEST(ParseLogTest, SetsOriLoggedFalseWhenOriAbsent) {
    bool err = false; std::string msg;
    auto objs = ParseLog(kLogFixture, 1, err, msg);
    ASSERT_FALSE(err) << msg;
    // Second object (Wall) has no Ori= field
    ASSERT_GE(objs.size(), 2u);
    EXPECT_FALSE(objs[1].ori_logged);
}

TEST(ParseLogTest, SetsTexAndMeshLoggedWhenPresent) {
    bool err = false; std::string msg;
    auto objs = ParseLog(kLogFixture, 1, err, msg);
    ASSERT_FALSE(err) << msg;
    // Fourth object (Crate) has Tex= and Model=
    ASSERT_GE(objs.size(), 4u);
    EXPECT_TRUE(objs[3].tex_logged);
    EXPECT_EQ(objs[3].texId,  "tex_001");
    EXPECT_TRUE(objs[3].mesh_logged);
    EXPECT_EQ(objs[3].meshId, "crate_01_1");
}

TEST(ParseLogTest, ErrorOnMissingFile) {
    bool err = false; std::string msg;
    ParseLog(kMissingPath, 1, err, msg);
    EXPECT_TRUE(err);
    EXPECT_FALSE(msg.empty());
}

TEST(ParseLogTest, ErrorWhenLevelMarkerAbsent) {
    bool err = false; std::string msg;
    // Level 99 is not in the fixture
    ParseLog(kLogFixture, 99, err, msg);
    EXPECT_TRUE(err);
    EXPECT_NE(msg.find("99"), std::string::npos);
}

TEST(ParseLogTest, UsesLastOccurrenceOfLevelMarker) {
    // The fixture has level 1 marker twice; objects after the SECOND marker should be used.
    // Level 2 marker comes after that, so we expect exactly 4 objects (from the second run),
    // not 8 (which would be both runs combined).
    bool err = false; std::string msg;
    auto objs = ParseLog(kLogFixture, 1, err, msg);
    ASSERT_FALSE(err) << msg;
    EXPECT_EQ(objs.size(), 4u);
}

TEST(ParseLogTest, DoesNotParseObjectsForOtherLevel) {
    // Level 2 has one object (999_99_9) in the fixture; it must not appear in level 1 results.
    bool err = false; std::string msg;
    auto objs = ParseLog(kLogFixture, 1, err, msg);
    ASSERT_FALSE(err) << msg;
    for (const auto& o : objs)
        EXPECT_NE(o.modelId, "999_99_9");
}

// ---------------------------------------------------------------------------
//  CrossRef
// ---------------------------------------------------------------------------

static VerifyObj MakeObj(const std::string& id, long long x, long long y, long long z,
                          double ox = 0, double oy = 0, double oz = 0, bool oriLogged = false) {
    VerifyObj v;
    v.modelId = id; v.px = x; v.py = y; v.pz = z;
    v.ox = ox; v.oy = oy; v.oz = oz;
    v.ori_logged = oriLogged;
    return v;
}

TEST(CrossRefTest, ExactMatchGoesToFound) {
    LevelReport::Category cat;
    cat.label = "TEST";
    cat.expected.push_back(MakeObj("100_01_1", 1000, 2000, 3000, 0, 1.5708, 0, true));
    std::vector<VerifyObj> logged;
    logged.push_back(MakeObj("100_01_1", 1000, 2000, 3000, 0, 1.5708, 0, true));

    CrossRef(cat, logged, true);

    EXPECT_EQ(cat.found.size(),        1u);
    EXPECT_EQ(cat.missing.size(),      0u);
    EXPECT_EQ(cat.pos_mismatch.size(), 0u);
    EXPECT_EQ(cat.ori_mismatch.size(), 0u);
}

TEST(CrossRefTest, MissingWhenNotInLog) {
    LevelReport::Category cat;
    cat.label = "TEST";
    cat.expected.push_back(MakeObj("100_01_1", 1000, 2000, 3000));
    std::vector<VerifyObj> logged; // empty

    CrossRef(cat, logged, false);

    EXPECT_EQ(cat.missing.size(), 1u);
    EXPECT_EQ(cat.found.size(),   0u);
}

TEST(CrossRefTest, PosMismatchWhenSameIdDifferentPos) {
    LevelReport::Category cat;
    cat.label = "TEST";
    cat.expected.push_back(MakeObj("100_01_1", 1000, 2000, 3000));
    std::vector<VerifyObj> logged;
    logged.push_back(MakeObj("100_01_1", 9999, 9999, 9999)); // same ID, wrong pos

    CrossRef(cat, logged, false);

    EXPECT_EQ(cat.pos_mismatch.size(), 1u);
    EXPECT_EQ(cat.found.size(),        0u);
    EXPECT_EQ(cat.missing.size(),      0u);
}

TEST(CrossRefTest, OriMismatchWhenSamePosOriBad) {
    LevelReport::Category cat;
    cat.label = "TEST";
    cat.expected.push_back(MakeObj("100_01_1", 1000, 2000, 3000, 0, 1.5708, 0, true));
    std::vector<VerifyObj> logged;
    // Same pos, but ori differs > EPS (0.05)
    logged.push_back(MakeObj("100_01_1", 1000, 2000, 3000, 0, 0.0, 0, true));

    CrossRef(cat, logged, true);  // matchOri=true

    EXPECT_EQ(cat.ori_mismatch.size(), 1u);
    EXPECT_EQ(cat.found.size(),        0u);
}

TEST(CrossRefTest, OriNotCheckedWhenMatchOriFalse) {
    LevelReport::Category cat;
    cat.label = "TEST";
    cat.expected.push_back(MakeObj("100_01_1", 1000, 2000, 3000, 0, 1.5708, 0, true));
    std::vector<VerifyObj> logged;
    logged.push_back(MakeObj("100_01_1", 1000, 2000, 3000, 0, 99.0, 0, true)); // huge ori diff

    CrossRef(cat, logged, false);  // matchOri=false (AI category)

    EXPECT_EQ(cat.found.size(),        1u);  // still matches
    EXPECT_EQ(cat.ori_mismatch.size(), 0u);
}

TEST(CrossRefTest, RailObjectMatchedByModelIdOnly) {
    LevelReport::Category cat;
    cat.label = "TEST";
    VerifyObj rail = MakeObj("322_01_1", 2630, 0, 0);
    rail.posIsRail = true;
    cat.expected.push_back(rail);

    std::vector<VerifyObj> logged;
    // Logged at completely different world position — still matches because posIsRail
    logged.push_back(MakeObj("322_01_1", 9999, 8888, 7777));

    CrossRef(cat, logged, true);

    EXPECT_EQ(cat.found.size(),   1u);
    EXPECT_EQ(cat.missing.size(), 0u);
}

TEST(CrossRefTest, TexMismatch) {
    LevelReport::Category cat;
    cat.label = "TEST";
    VerifyObj exp = MakeObj("400_04_1", 9000, 8000, 7000, 0, 0, 0, true);
    exp.texId = "tex_001"; exp.tex_logged = true;
    cat.expected.push_back(exp);

    std::vector<VerifyObj> logged;
    VerifyObj got = MakeObj("400_04_1", 9000, 8000, 7000, 0, 0, 0, true);
    got.texId = "tex_999"; got.tex_logged = true;
    logged.push_back(got);

    CrossRef(cat, logged, true);

    EXPECT_EQ(cat.tex_mismatch.size(), 1u);
    EXPECT_EQ(cat.found.size(),        0u);
}

TEST(CrossRefTest, MeshMismatch) {
    LevelReport::Category cat;
    cat.label = "TEST";
    VerifyObj exp = MakeObj("400_04_1", 9000, 8000, 7000, 0, 0, 0, true);
    exp.meshId = "crate_01_1"; exp.mesh_logged = true;
    cat.expected.push_back(exp);

    std::vector<VerifyObj> logged;
    VerifyObj got = MakeObj("400_04_1", 9000, 8000, 7000, 0, 0, 0, true);
    got.meshId = "wrong_01_1"; got.mesh_logged = true;
    logged.push_back(got);

    CrossRef(cat, logged, true);

    EXPECT_EQ(cat.mesh_mismatch.size(), 1u);
}

// ---------------------------------------------------------------------------
//  ParseQscObjects
// ---------------------------------------------------------------------------

TEST(ParseQscObjectsTest, ParsesSplineObjWaypointFromFixture) {
    std::map<std::string, std::string> noNames;
    auto objs = ParseQscObjects(kQscFixture, noNames);
    // level01_simple.qsc has one SplineObjWaypoint with model 322_01_1
    ASSERT_FALSE(objs.empty());
    bool found = false;
    for (const auto& o : objs)
        if (o.modelId == "322_01_1") { found = true; break; }
    EXPECT_TRUE(found) << "322_01_1 not found in parsed objects";
}

TEST(ParseQscObjectsTest, ReturnsEmptyForMissingFile) {
    std::map<std::string, std::string> noNames;
    auto objs = ParseQscObjects(kMissingPath, noNames);
    EXPECT_TRUE(objs.empty());
}
```

- [ ] **Step 2: Build (will fail to link until all other test files exist — create stubs)**

Create empty stub files for all not-yet-written tests so CMake can compile:

```cpp
// tests/test_verify_level.cpp  (temporary stub)
// tests/test_dat_parser.cpp    (temporary stub)
// tests/test_graph_parser.cpp  (temporary stub)
// tests/test_res_parser.cpp    (temporary stub)
// tests/test_tex_parser.cpp    (temporary stub)
// tests/test_mtp_parser.cpp    (temporary stub)
// tests/test_fnt_parser.cpp    (temporary stub)
```

Each stub file contains only:
```cpp
// stub — filled in later
```

```powershell
foreach ($f in @("test_verify_level","test_dat_parser","test_graph_parser","test_res_parser","test_tex_parser","test_mtp_parser","test_fnt_parser")) {
    "// stub" | Out-File -Encoding utf8 "D:\Code\project-igi-editor\tests\$f.cpp"
}
```

- [ ] **Step 3: Build and run the verify unit tests**

```powershell
cd D:\Code\project-igi-editor
cmake --build build --config Release -j 1 2>&1 | Select-String "error C|error :" | Select-Object -First 10
.\bin\Release\igi_tests.exe --gtest_filter="PosMatchTest.*:OriMatchTest.*:ParseLogTest.*:CrossRefTest.*:ParseQscObjectsTest.*" 2>&1 | Select-Object -Last 10
```
Expected last line: `[  PASSED  ] N tests.` (N ≥ 20)

- [ ] **Step 4: Commit**

```powershell
cd D:\Code\project-igi-editor
git add tests/test_verify_unit.cpp tests/test_verify_level.cpp tests/test_dat_parser.cpp tests/test_graph_parser.cpp tests/test_res_parser.cpp tests/test_tex_parser.cpp tests/test_mtp_parser.cpp tests/test_fnt_parser.cpp
git commit -m "test: add test_verify_unit.cpp with ParseLog/CrossRef/PosMatch tests"
```

---

## Task 8: Write `tests/test_verify_level.cpp`

**Files:**
- Modify: `tests/test_verify_level.cpp` (replace the stub)

This test launches `igi1ed.exe --verify-level 1` via Win32 `CreateProcess`, waits up to 15 seconds, and asserts exit code 0.

- [ ] **Step 1: Replace the stub with the integration test**

```cpp
#include <gtest/gtest.h>
#include "utils.h"
#include <string>
#include <filesystem>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// ============================================================
//  Verify Level — integration test
//
//  Launches igi1ed.exe --verify-level 1 from the exe directory.
//  Waits up to 15 seconds. Asserts exit code 0.
//  Requires the full game deployment (missions/ co-located with exe).
// ============================================================

TEST(VerifyLevelIntegration, Level1PassesVerification) {
    const std::string exeDir  = Utils::GetExeDirectory();
    const std::string exePath = exeDir + "\\igi1ed.exe";

    ASSERT_TRUE(std::filesystem::exists(exePath))
        << "igi1ed.exe not found at: " << exePath
        << "\nMake sure the editor is built and game files are co-located.";

    std::string cmdLine = "\"" + exePath + "\" --verify-level 1";
    std::vector<char> buf(cmdLine.begin(), cmdLine.end());
    buf.push_back('\0');

    STARTUPINFOA si = {};
    si.cb           = sizeof(si);
    si.dwFlags      = STARTF_USESHOWWINDOW;
    si.wShowWindow  = SW_SHOWMINNOACTIVE;

    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessA(
        nullptr, buf.data(),
        nullptr, nullptr, FALSE,
        CREATE_NEW_CONSOLE,
        nullptr, exeDir.c_str(),
        &si, &pi);

    ASSERT_TRUE(ok) << "CreateProcess failed, GetLastError=" << GetLastError();

    const DWORD kTimeoutMs = 15000;
    DWORD waitResult = WaitForSingleObject(pi.hProcess, kTimeoutMs);

    if (waitResult == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 5000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        FAIL() << "igi1ed.exe --verify-level 1 timed out after 15 seconds.";
    }

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    EXPECT_EQ(exitCode, 0u)
        << "Verify level 1 failed (exit code " << exitCode << ").\n"
        << "Check igi1ed.log in " << exeDir << " for details.";
}
```

- [ ] **Step 2: Build**

```powershell
cd D:\Code\project-igi-editor
cmake --build build --config Release -j 1 2>&1 | Select-String "error C|error :" | Select-Object -First 10
```
Expected: no errors

- [ ] **Step 3: Run the integration test**

```powershell
cd D:\Code\project-igi-editor
.\bin\Release\igi_tests.exe --gtest_filter="VerifyLevelIntegration.*" 2>&1 | Select-Object -Last 10
```
Expected: `[  PASSED  ] 1 test.`

- [ ] **Step 4: Commit**

```powershell
cd D:\Code\project-igi-editor
git add tests/test_verify_level.cpp
git commit -m "test: add VerifyLevelIntegration test (launches editor, 15s timeout)"
```

---

## Task 9: Write `tests/test_dat_parser.cpp`

**Files:**
- Modify: `tests/test_dat_parser.cpp` (replace stub)

DAT path: `GetIGIRootPath() + "\\missions\\location0\\level1\\level1.dat"`

- [ ] **Step 1: Replace the stub**

```cpp
#include <gtest/gtest.h>
#include "parsers/dat_parser.h"
#include "utils.h"
#include <string>

// ============================================================
//  DAT Parser — level1.dat structural tests
//
//  Path: <exe_dir>\missions\location0\level1\level1.dat
//  Game files must be co-located with igi_tests.exe.
// ============================================================

static std::string DatPath() {
    return Utils::GetIGIRootPath() + "\\missions\\location0\\level1\\level1.dat";
}

class DatParserTest : public ::testing::Test {
protected:
    DATFile dat;
    void SetUp() override {
        dat = DAT_Parse(DatPath());
    }
};

TEST_F(DatParserTest, FileExistsAndParsesValid) {
    ASSERT_TRUE(dat.valid) << "DAT parse failed: " << dat.error
                           << "\nPath: " << DatPath();
}

TEST_F(DatParserTest, DeclaredModelCountIsPositive) {
    EXPECT_GT(dat.declaredModelCount, 0);
}

TEST_F(DatParserTest, ModelsCountMatchesDeclared) {
    EXPECT_EQ((int)dat.models.size(), dat.declaredModelCount);
}

TEST_F(DatParserTest, AllModelNamesNonEmpty) {
    for (const auto& m : dat.models)
        EXPECT_FALSE(m.modelName.empty()) << "Empty model name found in DAT";
}

TEST_F(DatParserTest, AllTexturesNonEmpty) {
    EXPECT_GT(dat.allTextures.size(), 0u);
    for (const auto& t : dat.allTextures)
        EXPECT_FALSE(t.empty()) << "Empty texture name found in DAT";
}

TEST_F(DatParserTest, JsonOutputIsWellFormed) {
    std::string json = DAT_FormatJSON(dat);
    ASSERT_FALSE(json.empty());
    EXPECT_EQ(json.front(), '[');
    EXPECT_EQ(json.back(),  ']');
}
```

- [ ] **Step 2: Build and run**

```powershell
cd D:\Code\project-igi-editor
cmake --build build --config Release -j 1 2>&1 | Select-String "error C|error :" | Select-Object -First 10
.\bin\Release\igi_tests.exe --gtest_filter="DatParserTest.*" 2>&1 | Select-Object -Last 5
```
Expected: all `DatParserTest` cases pass

- [ ] **Step 3: Commit**

```powershell
cd D:\Code\project-igi-editor
git add tests/test_dat_parser.cpp
git commit -m "test: add DatParserTest suite against level1.dat"
```

---

## Task 10: Write `tests/test_graph_parser.cpp`

**Files:**
- Modify: `tests/test_graph_parser.cpp` (replace stub)

Graph path: `GetIGIRootPath() + "\\missions\\location0\\level1\\graph1.dat"`

- [ ] **Step 1: Replace the stub**

```cpp
#include <gtest/gtest.h>
#include "parsers/graph_parser.h"
#include "utils.h"
#include <string>
#include <cmath>

// ============================================================
//  Graph Parser — graph1.dat structural tests
//
//  Path: <exe_dir>\missions\location0\level1\graph1.dat
// ============================================================

static std::string GraphPath() {
    return Utils::GetIGIRootPath() + "\\missions\\location0\\level1\\graph1.dat";
}

class GraphParserTest : public ::testing::Test {
protected:
    GraphFile graph;
    void SetUp() override {
        graph = GRAPH_Parse(GraphPath());
    }
};

TEST_F(GraphParserTest, FileExistsAndParsesValid) {
    ASSERT_TRUE(graph.valid) << "Graph parse failed\nPath: " << GraphPath();
}

TEST_F(GraphParserTest, HasNodes) {
    EXPECT_GT(graph.nodes.size(), 0u);
}

TEST_F(GraphParserTest, AllNodeIdsNonNegative) {
    for (const auto& n : graph.nodes)
        EXPECT_GE(n.id, 0) << "Negative node ID: " << n.id;
}

TEST_F(GraphParserTest, AllCoordinatesAreFinite) {
    for (const auto& n : graph.nodes) {
        EXPECT_TRUE(std::isfinite(n.x)) << "Non-finite X for node " << n.id;
        EXPECT_TRUE(std::isfinite(n.y)) << "Non-finite Y for node " << n.id;
        EXPECT_TRUE(std::isfinite(n.z)) << "Non-finite Z for node " << n.id;
    }
}

TEST_F(GraphParserTest, AllMaterialValuesInRange) {
    for (const auto& n : graph.nodes)
        EXPECT_TRUE(n.material >= 0 && n.material <= 23)
            << "Material " << n.material << " out of 0-23 range for node " << n.id;
}
```

- [ ] **Step 2: Build and run**

```powershell
cd D:\Code\project-igi-editor
cmake --build build --config Release -j 1 2>&1 | Select-String "error C|error :" | Select-Object -First 10
.\bin\Release\igi_tests.exe --gtest_filter="GraphParserTest.*" 2>&1 | Select-Object -Last 5
```
Expected: all `GraphParserTest` cases pass

- [ ] **Step 3: Commit**

```powershell
cd D:\Code\project-igi-editor
git add tests/test_graph_parser.cpp
git commit -m "test: add GraphParserTest suite against graph1.dat"
```

---

## Task 11: Write `tests/test_res_parser.cpp`

**Files:**
- Modify: `tests/test_res_parser.cpp` (replace stub)

RES path: `GetIGIRootPath() + "\\missions\\location0\\level1\\models\\level1.res"`

- [ ] **Step 1: Replace the stub**

```cpp
#include <gtest/gtest.h>
#include "parsers/res_parser.h"
#include "utils.h"
#include <string>

// ============================================================
//  RES Parser — level1.res structural tests
//
//  Path: <exe_dir>\missions\location0\level1\models\level1.res
// ============================================================

static std::string ResPath() {
    return Utils::GetIGIRootPath() +
           "\\missions\\location0\\level1\\models\\level1.res";
}

class ResParserTest : public ::testing::Test {
protected:
    RESFile res;
    void SetUp() override {
        res = RES_Parse(ResPath());
    }
};

TEST_F(ResParserTest, FileExistsAndParsesValid) {
    ASSERT_TRUE(res.valid) << "RES parse failed: " << res.error
                           << "\nPath: " << ResPath();
}

TEST_F(ResParserTest, HasEntries) {
    EXPECT_GT(res.entries.size(), 0u);
}

TEST_F(ResParserTest, AllEntryNamesNonEmpty) {
    for (const auto& e : res.entries)
        EXPECT_FALSE(e.name.empty()) << "Empty entry name in RES";
}

TEST_F(ResParserTest, AllEntryDataNonEmpty) {
    for (const auto& e : res.entries)
        EXPECT_GT(e.data.size(), 0u) << "Empty data for entry: " << e.name;
}

TEST_F(ResParserTest, ForEachCallbackFires) {
    int count = 0;
    std::string err;
    RES_ForEachEntry(ResPath(),
        [&](const std::string&, const uint8_t*, size_t) { ++count; },
        err);
    EXPECT_GT(count, 0) << "ForEachEntry fired 0 times; error: " << err;
}

TEST_F(ResParserTest, ExtractFirstEntryReturnsData) {
    ASSERT_FALSE(res.entries.empty());
    const std::string firstName = res.entries[0].name;
    auto data = RES_Extract(ResPath(), firstName);
    EXPECT_GT(data.size(), 0u) << "RES_Extract returned empty for: " << firstName;
}
```

- [ ] **Step 2: Build and run**

```powershell
cd D:\Code\project-igi-editor
cmake --build build --config Release -j 1 2>&1 | Select-String "error C|error :" | Select-Object -First 10
.\bin\Release\igi_tests.exe --gtest_filter="ResParserTest.*" 2>&1 | Select-Object -Last 5
```
Expected: all `ResParserTest` cases pass

- [ ] **Step 3: Commit**

```powershell
cd D:\Code\project-igi-editor
git add tests/test_res_parser.cpp
git commit -m "test: add ResParserTest suite against level1.res"
```

---

## Task 12: Write `tests/test_tex_parser.cpp`

**Files:**
- Modify: `tests/test_tex_parser.cpp` (replace stub)

Strategy: open `level1.res`, find the first `.tex` entry, write to a temp file, then parse with `TEX_Parse`.

- [ ] **Step 1: Replace the stub**

```cpp
#include <gtest/gtest.h>
#include "parsers/tex_parser.h"
#include "parsers/res_parser.h"
#include "utils.h"
#include <string>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cctype>

// ============================================================
//  TEX Parser — first .tex extracted from level1.res
//
//  Extracts the first .tex entry from level1.res into a temp
//  file then parses it with TEX_Parse.
// ============================================================

static std::string ResPath() {
    return Utils::GetIGIRootPath() +
           "\\missions\\location0\\level1\\models\\level1.res";
}
static const std::string kTempTex = "tests/fixtures/_tmp_test.tex";

static bool ExtractFirstTex(const std::string& resPath, const std::string& outPath) {
    RESFile res = RES_Parse(resPath);
    if (!res.valid) return false;
    for (const auto& e : res.entries) {
        std::string name = e.name;
        std::string ext = name.size() >= 4 ? name.substr(name.size() - 4) : "";
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c){ return (char)std::tolower(c); });
        if (ext == ".tex") {
            std::ofstream f(outPath, std::ios::binary);
            if (!f) return false;
            f.write(reinterpret_cast<const char*>(e.data.data()), (std::streamsize)e.data.size());
            return true;
        }
    }
    return false;
}

class TexParserTest : public ::testing::Test {
protected:
    TEXFile tex;
    void SetUp() override {
        bool ok = ExtractFirstTex(ResPath(), kTempTex);
        ASSERT_TRUE(ok) << "Could not extract a .tex entry from " << ResPath();
        tex = TEX_Parse(kTempTex);
    }
    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove(kTempTex, ec);
    }
};

TEST_F(TexParserTest, ParsesValid) {
    ASSERT_TRUE(tex.valid) << "TEX parse failed: " << tex.error;
}

TEST_F(TexParserTest, VersionIsKnown) {
    EXPECT_TRUE(tex.version == 2 || tex.version == 7 ||
                tex.version == 9 || tex.version == 11)
        << "Unexpected TEX version: " << tex.version;
}

TEST_F(TexParserTest, HasImages) {
    EXPECT_GT(tex.images.size(), 0u);
}

TEST_F(TexParserTest, FirstImageHasPositiveDimensions) {
    ASSERT_FALSE(tex.images.empty());
    EXPECT_GT(tex.images[0].width,  0u);
    EXPECT_GT(tex.images[0].height, 0u);
}

TEST_F(TexParserTest, PixelDataSizeMatchesDimensions) {
    ASSERT_FALSE(tex.images.empty());
    const auto& img = tex.images[0];
    size_t bytesPerPixel = (img.mode == 2) ? 2u : 4u;
    size_t expected = (size_t)img.width * img.height * bytesPerPixel;
    EXPECT_EQ(img.pixels.size(), expected)
        << "mode=" << img.mode << " w=" << img.width << " h=" << img.height;
}
```

- [ ] **Step 2: Build and run**

```powershell
cd D:\Code\project-igi-editor
cmake --build build --config Release -j 1 2>&1 | Select-String "error C|error :" | Select-Object -First 10
.\bin\Release\igi_tests.exe --gtest_filter="TexParserTest.*" 2>&1 | Select-Object -Last 5
```
Expected: all `TexParserTest` cases pass

- [ ] **Step 3: Commit**

```powershell
cd D:\Code\project-igi-editor
git add tests/test_tex_parser.cpp
git commit -m "test: add TexParserTest suite via RES extraction"
```

---

## Task 13: Write `tests/test_mtp_parser.cpp`

**Files:**
- Modify: `tests/test_mtp_parser.cpp` (replace stub)

Strategy: scan `GetIGIRootPath()` recursively for the first `.mtp` file.

- [ ] **Step 1: Replace the stub**

```cpp
#include <gtest/gtest.h>
#include "parsers/mtp_parser.h"
#include "utils.h"
#include <string>
#include <filesystem>
#include <algorithm>
#include <cctype>

// ============================================================
//  MTP Parser — first .mtp found under game root
// ============================================================

static std::string FindFirstFile(const std::string& root, const std::string& ext) {
    namespace fs = std::filesystem;
    std::error_code ec;
    for (auto& e : fs::recursive_directory_iterator(root, ec)) {
        if (ec) { ec.clear(); continue; }
        if (!e.is_regular_file(ec)) { ec.clear(); continue; }
        std::string fext = e.path().extension().string();
        std::transform(fext.begin(), fext.end(), fext.begin(),
                       [](unsigned char c){ return (char)std::tolower(c); });
        if (fext == ext) return e.path().string();
    }
    return "";
}

class MtpParserTest : public ::testing::Test {
protected:
    MTPFile mtp;
    std::string mtpPath;
    void SetUp() override {
        mtpPath = FindFirstFile(Utils::GetIGIRootPath(), ".mtp");
        ASSERT_FALSE(mtpPath.empty())
            << "No .mtp file found under: " << Utils::GetIGIRootPath();
        mtp = MTP_Parse(mtpPath);
    }
};

TEST_F(MtpParserTest, ParsesValid) {
    ASSERT_TRUE(mtp.valid) << "MTP parse failed: " << mtp.error
                           << "\nPath: " << mtpPath;
}

TEST_F(MtpParserTest, HasAtLeastOneModelOrTexture) {
    bool hasContent = !mtp.models.empty() || !mtp.textures.empty() || !mtp.mappings.empty();
    EXPECT_TRUE(hasContent) << "MTP file appears empty: " << mtpPath;
}
```

- [ ] **Step 2: Build and run**

```powershell
cd D:\Code\project-igi-editor
cmake --build build --config Release -j 1 2>&1 | Select-String "error C|error :" | Select-Object -First 10
.\bin\Release\igi_tests.exe --gtest_filter="MtpParserTest.*" 2>&1 | Select-Object -Last 5
```
Expected: all `MtpParserTest` cases pass

- [ ] **Step 3: Commit**

```powershell
cd D:\Code\project-igi-editor
git add tests/test_mtp_parser.cpp
git commit -m "test: add MtpParserTest suite via filesystem scan"
```

---

## Task 14: Write `tests/test_fnt_parser.cpp`

**Files:**
- Modify: `tests/test_fnt_parser.cpp` (replace stub)

Strategy: scan `GetIGIRootPath()` recursively for the first `.fnt` file.

- [ ] **Step 1: Replace the stub**

```cpp
#include <gtest/gtest.h>
#include "parsers/fnt_parser.h"
#include "utils.h"
#include <string>
#include <filesystem>
#include <algorithm>
#include <cctype>

// ============================================================
//  FNT Parser — first .fnt found under game root
// ============================================================

static std::string FindFirstFile(const std::string& root, const std::string& ext) {
    namespace fs = std::filesystem;
    std::error_code ec;
    for (auto& e : fs::recursive_directory_iterator(root, ec)) {
        if (ec) { ec.clear(); continue; }
        if (!e.is_regular_file(ec)) { ec.clear(); continue; }
        std::string fext = e.path().extension().string();
        std::transform(fext.begin(), fext.end(), fext.begin(),
                       [](unsigned char c){ return (char)std::tolower(c); });
        if (fext == ext) return e.path().string();
    }
    return "";
}

class FntParserTest : public ::testing::Test {
protected:
    FntFont font;
    std::string fntPath;
    void SetUp() override {
        fntPath = FindFirstFile(Utils::GetIGIRootPath(), ".fnt");
        ASSERT_FALSE(fntPath.empty())
            << "No .fnt file found under: " << Utils::GetIGIRootPath();
        font = FNT_Parse(fntPath);
    }
};

TEST_F(FntParserTest, ParsesValid) {
    ASSERT_TRUE(font.valid) << "FNT parse failed\nPath: " << fntPath;
}

TEST_F(FntParserTest, LineHeightIsPositive) {
    EXPECT_GT(font.lineHeight, 0);
}

TEST_F(FntParserTest, TextureDimensionsArePositive) {
    EXPECT_GT(font.texWidth,  0);
    EXPECT_GT(font.texHeight, 0);
}

TEST_F(FntParserTest, HasGlyphs) {
    EXPECT_GT(font.glyphs.size(), 0u);
}

TEST_F(FntParserTest, AtlasPixelDataSizeMatchesDimensions) {
    size_t expected = (size_t)font.texWidth * font.texHeight * 4;
    EXPECT_EQ(font.rgba.size(), expected)
        << "texWidth=" << font.texWidth << " texHeight=" << font.texHeight;
}
```

- [ ] **Step 2: Build and run**

```powershell
cd D:\Code\project-igi-editor
cmake --build build --config Release -j 1 2>&1 | Select-String "error C|error :" | Select-Object -First 10
.\bin\Release\igi_tests.exe --gtest_filter="FntParserTest.*" 2>&1 | Select-Object -Last 5
```
Expected: all `FntParserTest` cases pass

- [ ] **Step 3: Commit**

```powershell
cd D:\Code\project-igi-editor
git add tests/test_fnt_parser.cpp
git commit -m "test: add FntParserTest suite via filesystem scan"
```

---

## Task 15: Full Suite Build and Run

**Files:** None new

- [ ] **Step 1: Build Release from clean state**

```powershell
cd D:\Code\project-igi-editor
cmake --build build --config Release -j 1 2>&1 | Select-String "error C|error :" | Select-Object -First 10
```
Expected: no output (zero errors)

- [ ] **Step 2: Run all 158+ tests**

```powershell
cd D:\Code\project-igi-editor
.\bin\Release\igi_tests.exe 2>&1 | Select-Object -Last 5
```
Expected last line: `[  PASSED  ] N tests.` where N ≥ 175 (158 existing + ≥ 17 new unit tests; integration + parser tests add more depending on game deployment)

- [ ] **Step 3: Final commit**

```powershell
cd D:\Code\project-igi-editor
git add -A
git commit -m "test: comprehensive test suite — verify-core unit + parser integration tests"
```
