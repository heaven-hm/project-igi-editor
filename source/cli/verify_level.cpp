#include "pch.h"
#include "cli_handler.h"
#include "verify_level_core.h"
#include "utils.h"

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
              << "  tex_mismatch=" << cat.tex_mismatch.size()
              << "  mesh_mismatch=" << cat.mesh_mismatch.size()
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
    if (!cat.tex_mismatch.empty()) {
        std::vector<std::vector<std::string>> rows;
        for (const auto& [e,g] : cat.tex_mismatch)
            rows.push_back({e.modelName, e.modelId, e.texId, g.texId});
        PrintTable("TEXTURE MISMATCH " + cat.label,
                   {"Model Name","Model ID","Expected Tex","Found Tex"}, rows);
    }
    if (!cat.mesh_mismatch.empty()) {
        std::vector<std::vector<std::string>> rows;
        for (const auto& [e,g] : cat.mesh_mismatch)
            rows.push_back({e.modelName, e.modelId, e.meshId, g.meshId});
        PrintTable("MESH MISMATCH " + cat.label,
                   {"Model Name","Model ID","Expected Mesh","Found Mesh"}, rows);
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
        for (const auto& m : cat.missing) {
            std::string oriStr = m.ori_logged ? FmtOri(m.ox, m.oy, m.oz) : "N/A";
            rows.push_back({m.modelName, m.modelId, FmtPos(m.px,m.py,m.pz), oriStr});
        }
        o << "#### " << missingKey << "\n" << MdTable({"Model Name","Model ID","Position","Orientation"}, rows) << "\n";
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
// Main entry point
// ---------------------------------------------------------------------------

int CLIHandler::VerifyLevel(const VerifyLevelParams& params) {
    std::string igiPath = params.gamePath.empty() ? Utils::GetIGIRootPath() : params.gamePath;
    std::string exeDir  = Utils::GetExeDirectory();
    std::string logPath = params.logPath.empty()  ? (exeDir + "\\igi1ed.log") : params.logPath;
    std::string exePath = exeDir + "\\igi1ed.exe";

    // Export IGI_GAME_PATH environment variable so spawned child processes use the correct IGI path.
    if (!igiPath.empty()) {
#if defined(_WIN32)
        _putenv_s("IGI_GAME_PATH", igiPath.c_str());
#else
        setenv("IGI_GAME_PATH", igiPath.c_str(), 1);
#endif
    }

    std::string modelNamesPath = igiPath + "\\editor\\tools\\IGIModels.json";
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
