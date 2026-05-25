#include "pch.h"
#include "cli_handler.h"
#include "level/level_common.h"
#include "level/level_objects.h"
#include "parsers/qvm_parser.h"
#include "parsers/qvm_decompiler.h"
#include "utils.h"
#include "logger.h"
#include "common.h"

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
// Data structures
// ---------------------------------------------------------------------------

struct VerifyObj {
    std::string modelId;
    std::string modelName;  // from IGIModels.json
    std::string name;
    std::string type;
    long long px = 0, py = 0, pz = 0;
    double ox = 0, oy = 0, oz = 0;  // radians, as stored in QSC
    bool ori_logged = false;          // false → editor did not emit Ori= in log
};

struct LevelReport {
    int levelNo = 0;

    struct Category {
        std::string label;
        std::vector<VerifyObj> expected;
        std::vector<VerifyObj> found;
        std::vector<std::pair<VerifyObj,VerifyObj>> pos_mismatch;
        std::vector<std::pair<VerifyObj,VerifyObj>> ori_mismatch;
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
// Helpers
// ---------------------------------------------------------------------------

static long long ToInt(double v) { return (long long)std::llround(v); }

static bool PosMatch(const VerifyObj& a, const VerifyObj& b) {
    return a.px == b.px && a.py == b.py && a.pz == b.pz;
}
static bool OriMatch(const VerifyObj& a, const VerifyObj& b) {
    const double EPS = 0.01;  // ~0.57 degrees
    return std::fabs(a.ox - b.ox) < EPS &&
           std::fabs(a.oy - b.oy) < EPS &&
           std::fabs(a.oz - b.oz) < EPS;
}
static double DistSq2D(const VerifyObj& a, const VerifyObj& b) {
    double dx = (double)(a.px - b.px), dy = (double)(a.py - b.py);
    return dx*dx + dy*dy;
}
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
static std::string Trim(std::string s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(0, 1);
    while (!s.empty() && (s.back()  == ' ' || s.back()  == '\t' ||
                          s.back()  == '\r' || s.back() == '\n')) s.pop_back();
    return s;
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

// ---------------------------------------------------------------------------
// Load ModelId -> ModelName map from IGIModels.json
// ---------------------------------------------------------------------------

static std::map<std::string, std::string> LoadModelNames(const std::string& path) {
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

static void ApplyModelNames(std::vector<VerifyObj>& objs,
                             const std::map<std::string, std::string>& names) {
    for (auto& obj : objs) {
        auto it = names.find(obj.modelId);
        if (it != names.end()) obj.modelName = it->second;
    }
}

// ---------------------------------------------------------------------------
// Launch editor, wait for exit or timeout
// ---------------------------------------------------------------------------

static bool LaunchEditor(const std::string& exePath, const std::string& workDir,
                         int levelNo, int timeoutSec) {
    if (!fs::exists(exePath)) {
        std::cerr << "  [ERROR] Editor not found: " << exePath << "\n";
        return false;
    }

    std::string cmdLine = "\"" + exePath + "\" -level " + std::to_string(levelNo);
    std::cout << "  Launching: " << cmdLine << "\n";
    if (timeoutSec > 0)
        std::cout << "  Will kill after " << timeoutSec << "s ...\n";
    else
        std::cout << "  Waiting for editor to exit ...\n";

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    // CREATE_NEW_CONSOLE isolates the child's console handles so the child's
    // GLUT/freeglut init (which calls FreeConsole internally) cannot detach
    // the parent's stdout and silently kill our subsequent output.
    si.dwFlags     = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOWMINNOACTIVE;

    std::vector<char> buf(cmdLine.begin(), cmdLine.end());
    buf.push_back('\0');

    if (!CreateProcessA(nullptr, buf.data(), nullptr, nullptr, FALSE,
                        CREATE_NEW_CONSOLE, nullptr, workDir.c_str(), &si, &pi)) {
        std::cerr << "  [ERROR] CreateProcess failed (err=" << GetLastError() << ")\n";
        return false;
    }

    DWORD waitMs = (timeoutSec > 0) ? (DWORD)(timeoutSec * 1000) : INFINITE;
    DWORD res = WaitForSingleObject(pi.hProcess, waitMs);
    if (res == WAIT_TIMEOUT) {
        std::cout << "  Timeout reached (" << timeoutSec << "s), killing editor ...\n";
        TerminateProcess(pi.hProcess, 0);
        WaitForSingleObject(pi.hProcess, 5000);
    }

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    std::cout << "\n  Editor exited (code " << exitCode << ")\n";
    std::cout.flush();

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}

// ---------------------------------------------------------------------------
// Parse igi1ed.log for a specific level
// ---------------------------------------------------------------------------

static std::vector<VerifyObj> ParseLog(const std::string& logPath, int levelNo,
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
        R"(\[LevelLoader\] Object Loaded: ModelID=([^,]+), Type=([^,]+), Name=([^,]*), Pos=\(([^,]+),\s*([^,]+),\s*([^)]+)\)(?:,\s*Ori=\(([^,]+),\s*([^,]+),\s*([^)]+)\))?)"
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
        result.push_back(obj);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Categorise QSC objects into buildings / objects / AI
// ---------------------------------------------------------------------------

static void CategoriseQscObjects(const std::vector<LevelObject>& objs,
                                  std::vector<VerifyObj>& buildings,
                                  std::vector<VerifyObj>& objects,
                                  std::vector<VerifyObj>& ai,
                                  const std::map<std::string, std::string>& modelNames) {
    for (const auto& lo : objs) {
        // Nested leaf objects (e.g. a Building inside a Container) don't have a
        // semicolon in their rawLine slice, so isContainer is set true for them too.
        // Use modelId as the real discriminator: true grouping containers never have one.
        if (lo.modelId.empty() || lo.modelId == "waypoint") continue;

        VerifyObj v;
        v.modelId = lo.modelId;
        v.name    = lo.name;
        v.type    = lo.type;
        auto mnIt = modelNames.find(lo.modelId);
        if (mnIt != modelNames.end()) v.modelName = mnIt->second;
        v.px = ToInt(lo.pos.x); v.py = ToInt(lo.pos.y); v.pz = ToInt(lo.pos.z);
        v.ox = lo.rot.x; v.oy = lo.rot.y; v.oz = lo.rot.z;
        v.ori_logged = true;

        if (lo.isBuilding || lo.type == "Building") {
            buildings.push_back(v);
        } else if (lo.type == "HumanSoldier" || lo.type == "HumanSoldierFemale" || lo.type == "HumanPlayer") {
            ai.push_back(v);
        } else {
            objects.push_back(v);
        }
    }
}

// ---------------------------------------------------------------------------
// Cross-reference expected (from QSC) vs logged (from log)
// ---------------------------------------------------------------------------

static void CrossRef(LevelReport::Category& cat,
                     const std::vector<VerifyObj>& logged,
                     bool matchOri) {
    std::vector<bool> consumed(logged.size(), false);

    for (const auto& exp : cat.expected) {
        // Full match: same model, same pos, and either ori not checked, not logged, or matches
        auto it = std::find_if(logged.begin(), logged.end(), [&](const VerifyObj& q) {
            size_t idx = &q - &logged[0];
            return !consumed[idx] && q.modelId == exp.modelId && PosMatch(q, exp) &&
                   (!matchOri || !q.ori_logged || OriMatch(q, exp));
        });
        if (it != logged.end()) { 
            consumed[&*it - &logged[0]] = true;
            cat.found.push_back(exp); 
            continue; 
        }

        // Pos match, ori was logged but differs -> real mismatch
        if (matchOri) {
            auto it2 = std::find_if(logged.begin(), logged.end(), [&](const VerifyObj& q) {
                size_t idx = &q - &logged[0];
                return !consumed[idx] && q.modelId == exp.modelId && PosMatch(q, exp) && q.ori_logged;
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
            continue;
        }

        cat.missing.push_back(exp);
    }
}

// ---------------------------------------------------------------------------
// Console report
// ---------------------------------------------------------------------------

static void PrintSep() { std::cout << std::string(64, '=') << "\n"; }

static void PrintTable(const std::string& title,
                       const std::vector<std::string>& hdr,
                       const std::vector<std::vector<std::string>>& rows,
                       size_t maxRows = 10) {
    if (rows.empty()) return;
    std::vector<size_t> w(hdr.size());
    for (size_t i = 0; i < hdr.size(); ++i) w[i] = hdr[i].size();
    size_t show = std::min(rows.size(), maxRows);
    for (size_t r = 0; r < show; ++r)
        for (size_t c = 0; c < hdr.size() && c < rows[r].size(); ++c)
            w[c] = std::max(w[c], rows[r][c].size());

    std::cout << "\n" << title << ":\n";
    for (size_t c = 0; c < hdr.size(); ++c) {
        if (c) std::cout << " | ";
        std::cout << std::left << std::setw((int)w[c]) << hdr[c];
    }
    std::cout << "\n";
    for (size_t c = 0; c < hdr.size(); ++c) {
        if (c) std::cout << "-+-";
        std::cout << std::string(w[c], '-');
    }
    std::cout << "\n";
    for (size_t r = 0; r < show; ++r) {
        for (size_t c = 0; c < hdr.size(); ++c) {
            if (c) std::cout << " | ";
            std::string cell = (c < rows[r].size()) ? rows[r][c] : "";
            std::cout << std::left << std::setw((int)w[c]) << cell;
        }
        std::cout << "\n";
    }
    if (rows.size() > maxRows)
        std::cout << "    ... and " << rows.size() - maxRows << " more.\n";
}

static void PrintCategory(const LevelReport::Category& cat) {
    std::cout << "\n[" << cat.label << "]"
              << "  expected=" << cat.expected.size()
              << "  matching=" << cat.found.size()
              << "  missing="  << cat.missing.size()
              << "  pos_mismatch=" << cat.pos_mismatch.size()
              << "  ori_mismatch=" << cat.ori_mismatch.size()
              << "\n";

    {
        std::vector<std::vector<std::string>> rows;
        for (const auto& f : cat.found)
            rows.push_back({f.modelName, f.modelId, FmtPos(f.px,f.py,f.pz), FmtOri(f.ox,f.oy,f.oz)});
        PrintTable("MATCHING " + cat.label, {"Model Name","Model ID","Position","Orientation"}, rows);
    }
    if (!cat.ori_mismatch.empty()) {
        std::vector<std::vector<std::string>> rows;
        for (const auto& [e,g] : cat.ori_mismatch)
            rows.push_back({e.modelName, e.modelId, FmtOri(e.ox,e.oy,e.oz), FmtOri(g.ox,g.oy,g.oz)});
        PrintTable("ORIENTATION MISMATCH " + cat.label,
                   {"Model Name","Model ID","Expected Ori","Found Ori"}, rows);
    }
    if (!cat.pos_mismatch.empty()) {
        std::vector<std::vector<std::string>> rows;
        for (const auto& [e,g] : cat.pos_mismatch)
            rows.push_back({e.modelName, e.modelId, FmtPos(e.px,e.py,e.pz), FmtPos(g.px,g.py,g.pz)});
        PrintTable("POSITION MISMATCH " + cat.label,
                   {"Model Name","Model ID","Expected Pos","Found Pos"}, rows);
    }
    if (!cat.missing.empty()) {
        std::vector<std::vector<std::string>> rows;
        for (const auto& m : cat.missing)
            rows.push_back({m.modelName, m.modelId, FmtPos(m.px,m.py,m.pz)});
        PrintTable("MISSING " + cat.label, {"Model Name","Model ID","Position"}, rows);
    }
}

static void PrintReport(const LevelReport& r) {
    PrintSep();
    std::cout << " LEVEL " << r.levelNo << " VERIFICATION REPORT\n";
    PrintSep();
    if (r.logError) { std::cout << "\n[LOG]  " << r.logErrorMsg << "\n"; }
    PrintCategory(r.buildings);
    PrintCategory(r.objects);
    PrintCategory(r.ai);

    int issues = (int)(r.buildings.missing.size() + r.buildings.pos_mismatch.size() + r.buildings.ori_mismatch.size() +
                       r.objects.missing.size()   + r.objects.pos_mismatch.size()   + r.objects.ori_mismatch.size()   +
                       r.ai.missing.size()        + r.ai.pos_mismatch.size());
    std::cout << "\n  Result: "
              << (issues == 0 ? "PASS" : "FAIL (" + std::to_string(issues) + " issue(s))")
              << "\n";
    PrintSep();
}

// ---------------------------------------------------------------------------
// JSON serialisation
// ---------------------------------------------------------------------------

static void WriteJsonObj(std::ostream& o, const VerifyObj& v, int indent) {
    std::string pad(indent * 2, ' ');
    o << pad << "{\n"
      << pad << "  \"model_id\": \""   << JsonEsc(v.modelId)   << "\",\n"
      << pad << "  \"model_name\": \"" << JsonEsc(v.modelName) << "\",\n"
      << pad << "  \"name\": \""       << JsonEsc(v.name)      << "\",\n"
      << pad << "  \"type\": \""       << JsonEsc(v.type)      << "\",\n"
      << pad << "  \"pos\": [" << v.px << ", " << v.py << ", " << v.pz << "],\n"
      << pad << "  \"ori\": [" << v.ox << ", " << v.oy << ", " << v.oz << "],\n"
      << pad << "  \"ori_logged\": " << (v.ori_logged ? "true" : "false") << "\n"
      << pad << "}";
}

static void WriteJsonPair(std::ostream& o, const std::pair<VerifyObj,VerifyObj>& p,
                          const std::string& expKey, const std::string& gotKey, int indent) {
    std::string pad(indent * 2, ' ');
    o << pad << "{\n"
      << pad << "  \"" << expKey << "\": ";
    WriteJsonObj(o, p.first, indent + 2);
    o << ",\n"
      << pad << "  \"" << gotKey << "\": ";
    WriteJsonObj(o, p.second, indent + 2);
    o << "\n" << pad << "}";
}

static void WriteJsonCategory(std::ostream& o, const LevelReport::Category& cat,
                              const std::string& foundKey, const std::string& missingKey,
                              int indent) {
    std::string pad(indent * 2, ' ');
    auto writeObjArr = [&](const std::vector<VerifyObj>& arr) {
        o << "[\n";
        for (size_t i = 0; i < arr.size(); ++i) {
            WriteJsonObj(o, arr[i], indent + 2);
            o << (i + 1 < arr.size() ? ",\n" : "\n");
        }
        o << pad << "  ]";
    };
    auto writePairArr = [&](const std::vector<std::pair<VerifyObj,VerifyObj>>& arr,
                            const std::string& ek, const std::string& gk) {
        o << "[\n";
        for (size_t i = 0; i < arr.size(); ++i) {
            WriteJsonPair(o, arr[i], ek, gk, indent + 2);
            o << (i + 1 < arr.size() ? ",\n" : "\n");
        }
        o << pad << "  ]";
    };

    o << pad << "{\n"
      << pad << "  \"total_expected\": " << cat.expected.size() << ",\n"
      << pad << "  \"" << foundKey   << "\": "; writeObjArr(cat.found);    o << ",\n"
      << pad << "  \"" << missingKey << "\": "; writeObjArr(cat.missing);  o << ",\n"
      << pad << "  \"pos_mismatch\": "; writePairArr(cat.pos_mismatch, "expected","found"); o << ",\n"
      << pad << "  \"ori_mismatch\": "; writePairArr(cat.ori_mismatch, "expected","found"); o << "\n"
      << pad << "}";
}

static void WriteJsonReport(std::ostream& o, const LevelReport& r) {
    o << "{\n"
      << "  \"level\": " << r.levelNo << ",\n"
      << "  \"buildings\": ";
    WriteJsonCategory(o, r.buildings, "found", "missing", 1);
    o << ",\n  \"objects\": ";
    WriteJsonCategory(o, r.objects,   "found", "missing", 1);
    o << ",\n  \"ai\": ";
    WriteJsonCategory(o, r.ai,        "found_models", "missing_models", 1);
    o << "\n}";
}

// ---------------------------------------------------------------------------
// Markdown report
// ---------------------------------------------------------------------------

static std::string MdTable(const std::vector<std::string>& hdr,
                           const std::vector<std::vector<std::string>>& rows) {
    if (rows.empty()) return "";
    std::string out = "| " + hdr[0];
    for (size_t i = 1; i < hdr.size(); ++i) out += " | " + hdr[i];
    out += " |\n| ";
    for (size_t i = 0; i < hdr.size(); ++i) out += (i ? " | " : "") + std::string("---");
    out += " |\n";
    for (const auto& row : rows) {
        out += "| ";
        for (size_t i = 0; i < hdr.size(); ++i) {
            if (i) out += " | ";
            std::string cell = (i < row.size()) ? row[i] : "";
            for (char& c : cell) if (c == '|') c = '\\';
            out += cell;
        }
        out += " |\n";
    }
    return out;
}

static void WriteMdCategory(std::ostream& o, const LevelReport::Category& cat,
                            const std::string& foundKey, const std::string& missingKey) {
    o << "### " << cat.label << "\n"
      << "- **Expected**: " << cat.expected.size() << "\n"
      << "- **Matching**: " << cat.found.size()    << "\n"
      << "- **Missing**:  " << cat.missing.size()  << "\n"
      << "- **Pos mismatch**: " << cat.pos_mismatch.size() << "\n"
      << "- **Ori mismatch**: " << cat.ori_mismatch.size() << "\n\n";

    if (!cat.found.empty()) {
        std::vector<std::vector<std::string>> rows;
        for (size_t i = 0; i < std::min((size_t)50, cat.found.size()); ++i) {
            const auto& f = cat.found[i];
            rows.push_back({f.modelName, f.modelId, FmtPos(f.px,f.py,f.pz), FmtOri(f.ox,f.oy,f.oz)});
        }
        o << "#### " << foundKey << "\n" << MdTable({"Model Name","Model ID","Position","Orientation"}, rows) << "\n";
    }
    if (!cat.ori_mismatch.empty()) {
        std::vector<std::vector<std::string>> rows;
        for (const auto& [e,g] : cat.ori_mismatch)
            rows.push_back({e.modelName, e.modelId, FmtOri(e.ox,e.oy,e.oz), FmtOri(g.ox,g.oy,g.oz)});
        o << "#### Orientation Mismatch\n" << MdTable({"Model Name","Model ID","Expected Ori","Found Ori"}, rows) << "\n";
    }
    if (!cat.pos_mismatch.empty()) {
        std::vector<std::vector<std::string>> rows;
        for (const auto& [e,g] : cat.pos_mismatch)
            rows.push_back({e.modelName, e.modelId, FmtPos(e.px,e.py,e.pz), FmtPos(g.px,g.py,g.pz)});
        o << "#### Position Mismatch\n" << MdTable({"Model Name","Model ID","Expected Pos","Found Pos"}, rows) << "\n";
    }
    if (!cat.missing.empty()) {
        std::vector<std::vector<std::string>> rows;
        for (const auto& m : cat.missing)
            rows.push_back({m.modelName, m.modelId, FmtPos(m.px,m.py,m.pz)});
        o << "#### " << missingKey << "\n" << MdTable({"Model Name","Model ID","Position"}, rows) << "\n";
    }
}

static void WriteMdReport(std::ostream& o, const std::vector<LevelReport>& reports) {
    // Get current time for header
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    char timebuf[32];
    std::strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));

    o << "# IGI Level Object Verification Report\n\n"
      << "Generated: " << timebuf << "\n\n";

    for (const auto& r : reports) {
        o << "## Level " << r.levelNo << " Verification\n\n";
        WriteMdCategory(o, r.buildings, "Matching",     "Missing");
        WriteMdCategory(o, r.objects,   "Matching",     "Missing");
        WriteMdCategory(o, r.ai,        "Found Models", "Missing Models");
        o << "---\n\n";
    }
}

// ---------------------------------------------------------------------------
// Per-level file saves
// ---------------------------------------------------------------------------

static void SaveLevelFiles(const std::string& dir, const LevelReport& r) {
    std::string base = dir + "\\level_" + std::to_string(r.levelNo) + "_report";

    // JSON
    std::ofstream jf(base + ".json");
    if (jf) { WriteJsonReport(jf, r); jf.close(); }

    // Markdown
    std::ofstream mf(base + ".md");
    if (mf) { WriteMdReport(mf, {r}); mf.close(); }
}

// ---------------------------------------------------------------------------
// Schema-based direct QSC text parser
// ---------------------------------------------------------------------------

// How many Task_New args one field of a given type consumes (QSC wire format).
// Real32x9 is a 3x3 rotation matrix at runtime but is serialised as 3 Euler
// angles in QSC; all other multi-float types follow their obvious count.
static int TypeArgCount(const std::string& t) {
    if (t == "ObjectPos" || t == "Real32x9" || t == "RGB" ||
        t == "Real32x3"  || t == "Colour")   return 3;
    return 1;   // String16, VarString, Real32, Int32, Bool, …
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

struct FieldDef {
    std::string name;
    std::string typeName;
    int argOffset = 0;  // absolute index in the Task_New arg list (0 = taskId)
    int argCount  = 1;
};
using TaskSchema = std::vector<FieldDef>;

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

// Hardcoded schemas for all known IGI task types.
// Task_DeclareParameters is a compile-time directive stripped from QVM, so
// QVM-decompiled files never contain it.  These schemas mirror what the game
// engine defines, verified against the actual QSC argument layouts.
static std::map<std::string, TaskSchema> GetBuiltinSchemas() {
    std::map<std::string, TaskSchema> s;

    auto add = [](TaskSchema& sc, const char* name, const char* type, int off) {
        FieldDef f; f.name = name; f.typeName = type;
        f.argOffset = off; f.argCount = TypeArgCount(type);
        sc.push_back(f);
    };

    // Building / EditRigidObj / Terminal: pos@3, ori@6, model@9
    for (const char* t : {"Building","EditRigidObj","Terminal"}) {
        add(s[t], "Position",    "ObjectPos", 3);
        add(s[t], "Orientation", "Real32x9",  6);
        add(s[t], "Model",       "String16",  9);
    }
    // HumanSoldier types: pos@3, heading(1 float)@6, model@7
    for (const char* t : {"HumanSoldier","HumanSoldierFemale","HumanPlayer"}) {
        add(s[t], "Position", "ObjectPos", 3);
        add(s[t], "Heading",  "Real32",    6);
        add(s[t], "Model",    "String16",  7);
    }
    // Door: pos@3, (3 unknown physics args @6), ori@9, model@12
    { auto& sc = s["Door"];
      add(sc, "Position",    "ObjectPos", 3);
      add(sc, "Orientation", "Real32x9",  9);
      add(sc, "Model",       "String16",  12); }
    // Car / Heli: pos@3, (2 unknowns @6), gamma@8, (4 unknowns @9), model@13
    for (const char* t : {"Car","Heli"}) {
        add(s[t], "Position", "ObjectPos", 3);
        add(s[t], "Heading",  "Real32",    8);
        add(s[t], "Model",    "String16",  13);
    }
    // ExplodeObject: pos@3, ori@6, model@9, destroyed_model@10, ...
    { auto& sc = s["ExplodeObject"];
      add(sc, "Position",        "ObjectPos", 3);
      add(sc, "Orientation",     "Real32x9",  6);
      add(sc, "Model",           "String16",  9);
      add(sc, "Destroyed model", "String16",  10); }
    // Switch: pos@3, ori@6, count@9, reverse@10, models@11..15, loop@16
    { auto& sc = s["Switch"];
      add(sc, "Position",    "ObjectPos", 3);
      add(sc, "Orientation", "Real32x9",  6);
      add(sc, "Model",       "String16",  11); }
    // Wire: start pos@3, end pos@6, model@9
    { auto& sc = s["Wire"];
      add(sc, "Position", "ObjectPos", 3);
      add(sc, "Model",    "String16",  9); }
    // AlarmControl / SCameraControl: pos@3, ori@6, model@9 (usually "waypoint")
    for (const char* t : {"AlarmControl","SCameraControl"}) {
        add(s[t], "Position",    "ObjectPos", 3);
        add(s[t], "Orientation", "Real32x9",  6);
        add(s[t], "Model",       "String16",  9);
    }
    // SCamera: pos@3, (4 unknowns @6), model@10
    { auto& sc = s["SCamera"];
      add(sc, "Position", "ObjectPos", 3);
      add(sc, "Model",    "String16",  10); }
    // SplineObjWaypoint: ori first@3, pos@6, model@9
    { auto& sc = s["SplineObjWaypoint"];
      add(sc, "Orientation", "Real32x9",  3);
      add(sc, "Position",    "ObjectPos", 6);
      add(sc, "Model",       "String16",  9); }
    // AmbientArea / ExplodeObject already above
    { auto& sc = s["AmbientArea"];
      add(sc, "Position",    "ObjectPos", 3);
      add(sc, "Orientation", "Real32x9",  6); }

    return s;
}

// Parse every Task_New call in the QSC text.  Uses the schema derived from
// Task_DeclareParameters to locate Position, Orientation, and Model fields.
// Only tasks that have a valid model ID (XXX_XX_X pattern) are returned.
static std::vector<VerifyObj> ParseQscObjects(
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
            // Position: first ObjectPos field (or field named "Position")
            if (!hasPos && (fd.typeName == "ObjectPos" ||
                            fd.name.find("Position") != std::string::npos)) {
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
            // Orientation: Real32x9 (3 Euler angles) or single-float Heading
            if (!hasOri && (fd.typeName == "Real32x9" ||
                            fd.name.find("Orientation") != std::string::npos ||
                            fd.name.find("Heading")     != std::string::npos)) {
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
            // Model: first String16/VarString value matching XXX_XX_X
            if (modelId.empty() &&
                (fd.typeName == "String16" || fd.typeName == "VarString") &&
                fd.argOffset < (int)args.size() && !args[fd.argOffset].empty()) {
                std::string val = UnquoteStr(args[fd.argOffset]);
                if (IsModelId(val)) modelId = val;
            }
        }

        if (!hasPos || modelId.empty()) continue;

        VerifyObj v;
        v.modelId   = modelId;
        v.type      = typeStr;
        v.name      = UnquoteStr(args[2]);
        v.px = (long long)std::llround(px);
        v.py = (long long)std::llround(py);
        v.pz = (long long)std::llround(pz);
        v.ox = ox; v.oy = oy; v.oz = oz;
        v.ori_logged = hasOri;
        auto mnIt = modelNames.find(modelId);
        if (mnIt != modelNames.end()) v.modelName = mnIt->second;
        result.push_back(v);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Single level verify (decompile QVM, parse QSC, cross-ref log)
// ---------------------------------------------------------------------------

static LevelReport VerifyOneLevel(const std::string& igiPath,
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
    CrossRef(report.buildings, logged, true);
    CrossRef(report.objects,   logged, true);
    CrossRef(report.ai,        logged, false);

    return report;
}

// ---------------------------------------------------------------------------
// Main entry point
// ---------------------------------------------------------------------------

int CLIHandler::VerifyLevel(const VerifyLevelParams& params) {
    std::string igiPath = params.gamePath.empty() ? Utils::GetIGIRootPath() : params.gamePath;
    std::string exeDir  = Utils::GetExeDirectory();
    std::string logPath = params.logPath.empty()  ? (igiPath + "\\igi1ed.log") : params.logPath;
    std::string exePath = exeDir + "\\igi1ed.exe";

    // Export IGI_GAME_PATH environment variable so spawned child processes use the correct IGI path.
    if (!igiPath.empty()) {
#if defined(_WIN32)
        _putenv_s("IGI_GAME_PATH", igiPath.c_str());
#else
        setenv("IGI_GAME_PATH", igiPath.c_str(), 1);
#endif
    }

    std::string modelNamesPath = igiPath + "\\content\\tools\\IGIModels.json";
    std::map<std::string, std::string> modelNames = LoadModelNames(modelNamesPath);
    if (modelNames.empty())
        std::cerr << "[WARN] IGIModels.json not found or empty: " << modelNamesPath << "\n";
    else
        std::cout << "  Model names : " << modelNames.size() << " entries loaded from IGIModels.json\n";

    std::cout << "\n";
    PrintSep();
    std::cout << " IGI LEVEL VERIFICATION\n";
    PrintSep();
    std::cout << "  IGI path  : " << igiPath  << "\n";
    std::cout << "  Log file  : " << logPath   << "\n";
    std::cout << "  Levels    :";
    for (int n : params.levels) std::cout << " " << n;
    std::cout << "\n";
    if (params.skipLaunch) std::cout << "  Mode      : SKIP-LAUNCH (existing log)\n";
    else                   std::cout << "  Mode      : LAUNCH editor, timeout="
                                     << (params.timeout == 0 ? "none" : std::to_string(params.timeout) + "s") << "\n";

    // Resolve report paths to absolute so they work regardless of cwd changes
    auto absPath = [](const std::string& p) -> std::string {
        if (p.empty()) return p;
        try { return fs::absolute(p).string(); } catch (...) { return p; }
    };
    std::string reportDir  = absPath(params.reportDir);
    std::string reportJson = absPath(params.reportJson);
    std::string reportMd   = absPath(params.reportMd);

    // Create report dir if requested
    if (!reportDir.empty()) {
        try {
            fs::create_directories(reportDir);
            std::cout << "  Report dir: " << reportDir << "\n";
        } catch (const std::exception& ex) {
            std::cerr << "[WARN] Cannot create report dir '" << reportDir << "': " << ex.what() << "\n";
            reportDir.clear();
        }
    }
    std::cout.flush();

    std::vector<LevelReport> allReports;

    for (size_t i = 0; i < params.levels.size(); ++i) {
        int levelNo = params.levels[i];

        // Delay between levels
        if (i > 0 && params.delay > 0) {
            std::cout << "\n[WAIT] Sleeping " << params.delay << "s before next level ...\n";
            std::cout.flush();
            std::this_thread::sleep_for(std::chrono::seconds(params.delay));
        }

        std::cout << "\n";
        PrintSep();
        std::cout << " Processing Level " << levelNo << "\n";
        PrintSep();
        std::cout.flush();

        // Launch editor unless --skip-launch
        if (!params.skipLaunch) {
            bool ok = LaunchEditor(exePath, igiPath, levelNo, params.timeout);
            if (!ok) std::cout << "  [WARN] Editor launch failed for level " << levelNo << "\n";
            // Small flush delay for log file to be flushed by child
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        } else {
            std::cout << "  [SKIP-LAUNCH] Using existing log\n";
        }
        std::cout.flush();

        LevelReport rep;
        try {
            rep = VerifyOneLevel(igiPath, exeDir, logPath, levelNo, modelNames);
        } catch (const std::exception& ex) {
            rep.levelNo = levelNo;
            rep.buildings.label = "BUILDINGS"; rep.objects.label = "OBJECTS"; rep.ai.label = "AI";
            rep.logError = true;
            rep.logErrorMsg = std::string("Fatal exception: ") + ex.what();
            std::cerr << "  [ERROR] " << rep.logErrorMsg << "\n";
        } catch (...) {
            rep.levelNo = levelNo;
            rep.buildings.label = "BUILDINGS"; rep.objects.label = "OBJECTS"; rep.ai.label = "AI";
            rep.logError = true;
            rep.logErrorMsg = "Fatal unknown exception in VerifyOneLevel";
            std::cerr << "  [ERROR] " << rep.logErrorMsg << "\n";
        }

        PrintReport(rep);
        std::cout.flush();
        allReports.push_back(rep);

        // Per-level files
        if (!reportDir.empty()) SaveLevelFiles(reportDir, rep);
    }

    // Aggregated JSON
    if (!reportJson.empty()) {
        std::ofstream jf(reportJson);
        if (jf) {
            jf << "[\n";
            for (size_t i = 0; i < allReports.size(); ++i) {
                WriteJsonReport(jf, allReports[i]);
                if (i + 1 < allReports.size()) jf << ",";
                jf << "\n";
            }
            jf << "]\n";
            std::cout << "\n[INFO] JSON report written to: " << reportJson << "\n";
        } else {
            std::cerr << "[WARN] Cannot write JSON report to: " << reportJson << "\n";
        }
    }

    // Aggregated Markdown
    if (!reportMd.empty()) {
        std::ofstream mf(reportMd);
        if (mf) {
            WriteMdReport(mf, allReports);
            std::cout << "[INFO] Markdown report written to: " << reportMd << "\n";
        } else {
            std::cerr << "[WARN] Cannot write Markdown report to: " << reportMd << "\n";
        }
    }

    // Overall exit code
    bool anyFail = std::any_of(allReports.begin(), allReports.end(), [](const LevelReport& r) {
        return !r.buildings.missing.empty() || !r.buildings.pos_mismatch.empty() || !r.buildings.ori_mismatch.empty() ||
               !r.objects.missing.empty()   || !r.objects.pos_mismatch.empty()   || !r.objects.ori_mismatch.empty()   ||
               !r.ai.missing.empty()        || !r.ai.pos_mismatch.empty()        || r.logError;
    });
    return anyFail ? 1 : 0;
}
