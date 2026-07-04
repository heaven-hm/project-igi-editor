/******************************************************************************
 * @file    utils_igi1conv.cpp
 * @brief   Shared runner for the bundled igi1conv.exe asset converter.
 *          All file conversion in the editor MUST go through this helper
 *          instead of in-process parsers.
 *****************************************************************************/

#include "pch.h"
#include "utils_igi1conv.h"
#include "utils.h"
#include "logger.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <random>
#include <cstdio>

namespace fs = std::filesystem;

namespace igi1conv {

std::string GetExePath() {
    return Utils::GetExeDirectory() + "\\editor\\tools\\igi1conv\\igi1conv.exe";
}

std::string MakeTempPath(const std::string& suffix) {
    // thread_local: animation resolution now runs this concurrently across worker
    // threads at level load — a shared/static RNG would be a data race.
    thread_local std::mt19937_64 rng(std::random_device{}());
    std::ostringstream ss;
    ss << std::filesystem::temp_directory_path().string()
       << "\\igi1conv_" << std::hex << rng() << suffix;
    return ss.str();
}

bool Run(const std::string& args, std::string& err, DWORD timeoutMs) {
    const std::string exe = GetExePath();
    if (!fs::exists(exe)) {
        err = "igi1conv.exe not found: " + exe;
        return false;
    }
    std::string cmdLine = "\"" + exe + "\" " + args;
    Logger::Get().Log(LogLevel::INFO, "[igi1conv] " + cmdLine);

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    std::vector<char> buf(cmdLine.begin(), cmdLine.end());
    buf.push_back('\0');

    if (!CreateProcessA(nullptr, buf.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        err = "CreateProcess failed (" + std::to_string(GetLastError()) + ") for " + cmdLine;
        Logger::Get().Log(LogLevel::ERR, "[igi1conv] " + err);
        return false;
    }
    WaitForSingleObject(pi.hProcess, timeoutMs);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (code != 0) {
        err = "igi1conv exit code " + std::to_string(code) + " for: " + args;
        Logger::Get().Log(LogLevel::ERR, "[igi1conv] " + err);
        return false;
    }
    return true;
}

// Like Run(), but captures the child process's stdout (and stderr, merged)
// into `stdoutOut`. Needed for `lightmap resolve`/`lightmap list`, which
// have no -o flag — their only output is stdout text.
static bool RunCaptureStdout(const std::string& args, std::string& stdoutOut,
                             std::string& err, DWORD timeoutMs = 120000) {
    const std::string exe = GetExePath();
    if (!fs::exists(exe)) {
        err = "igi1conv.exe not found: " + exe;
        return false;
    }
    std::string cmdLine = "\"" + exe + "\" " + args;
    Logger::Get().Log(LogLevel::INFO, "[igi1conv] " + cmdLine);

    SECURITY_ATTRIBUTES saAttr{};
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = nullptr;

    HANDLE hReadPipe = nullptr, hWritePipe = nullptr;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &saAttr, 0)) {
        err = "CreatePipe failed (" + std::to_string(GetLastError()) + ")";
        return false;
    }
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWritePipe;
    si.hStdError  = hWritePipe;
    PROCESS_INFORMATION pi = {};
    std::vector<char> buf(cmdLine.begin(), cmdLine.end());
    buf.push_back('\0');

    BOOL ok = CreateProcessA(nullptr, buf.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(hWritePipe);
    if (!ok) {
        err = "CreateProcess failed (" + std::to_string(GetLastError()) + ") for " + cmdLine;
        Logger::Get().Log(LogLevel::ERR, "[igi1conv] " + err);
        CloseHandle(hReadPipe);
        return false;
    }

    std::string output;
    char chunk[4096];
    DWORD nRead = 0;
    while (ReadFile(hReadPipe, chunk, sizeof(chunk), &nRead, nullptr) && nRead > 0) {
        output.append(chunk, nRead);
    }
    CloseHandle(hReadPipe);

    WaitForSingleObject(pi.hProcess, timeoutMs);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    stdoutOut = output;
    if (code != 0) {
        err = "igi1conv exit code " + std::to_string(code) + " for: " + args +
              (output.empty() ? "" : ("\n" + output));
        Logger::Get().Log(LogLevel::ERR, "[igi1conv] " + err);
        return false;
    }
    return true;
}

// ─── lightmap / olm ───────────────────────────────────────────────────────

std::vector<std::string> ParseLightmapResolveStdout(const std::string& stdoutText, std::string& err) {
    std::vector<std::string> paths;
    std::istringstream iss(stdoutText);
    std::string line;
    bool inFileList = false;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!inFileList) {
            if (line.find(".olm file(s):") != std::string::npos) {
                inFileList = true;
            }
            continue;
        }
        if (line.empty()) continue;
        if (line[0] != ' ' && line[0] != '\t') break;
        size_t start = line.find_first_not_of(" \t");
        if (start != std::string::npos) paths.push_back(line.substr(start));
    }
    if (paths.empty()) {
        err = "no .olm file paths found in lightmap resolve output: " +
              (stdoutText.empty() ? std::string("(empty output)") : stdoutText);
    }
    return paths;
}

std::vector<std::string> LightmapResolve(const std::string& modelId, const std::string& qscPath,
                                         const std::string& taskId, std::string& err) {
    std::string args = "lightmap resolve --model \"" + modelId + "\" --qsc \"" + qscPath +
                       "\" --task-id " + taskId;
    std::string out;
    if (!RunCaptureStdout(args, out, err)) return {};
    return ParseLightmapResolveStdout(out, err);
}

std::vector<std::string> LightmapResolveByPos(const std::string& modelId, const std::string& qscPath,
                                              double x, double y, double z, std::string& err) {
    char pos[96];
    std::snprintf(pos, sizeof(pos), "%.6f,%.6f,%.6f", x, y, z);
    std::string args = "lightmap resolve --model \"" + modelId + "\" --qsc \"" + qscPath +
                       "\" --pos " + pos;
    std::string out;
    if (!RunCaptureStdout(args, out, err)) return {};
    return ParseLightmapResolveStdout(out, err);
}

bool OlmToPng(const std::string& olmPath, const std::string& outPng, std::string& err) {
    return Run("olm to-png \"" + olmPath + "\" -o \"" + outPng + "\"", err);
}

bool LightmapRecalc(const std::string& modelId, const std::string& qscPath,
                    const std::string& taskId, const std::string& mefPath,
                    const glm::dvec3& rotOrig, const glm::dvec3& rotNew,
                    const glm::vec3& sunDir, const glm::vec3& sunColor,
                    const glm::vec3& ambient, std::string& err) {
    // Fixed-precision, locale-independent "x,y,z" formatting for the CLI flags.
    auto vec3 = [](double x, double y, double z) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%.6f,%.6f,%.6f", x, y, z);
        return std::string(buf);
    };
    std::string args =
        "lightmap recalc --model \"" + modelId + "\" --qsc \"" + qscPath +
        "\" --task-id " + taskId + " --mef \"" + mefPath + "\"" +
        " --rot-orig " + vec3(rotOrig.x, rotOrig.y, rotOrig.z) +
        " --rot-new "  + vec3(rotNew.x, rotNew.y, rotNew.z) +
        " --sun-dir "  + vec3(sunDir.x, sunDir.y, sunDir.z) +
        " --sun-color " + vec3(sunColor.x, sunColor.y, sunColor.z) +
        " --ambient "  + vec3(ambient.x, ambient.y, ambient.z);
    return Run(args, err);
}

// ─── res ───────────────────────────────────────────────────────────────────

std::vector<std::string> ResList(const std::string& resPath, std::string& err) {
    std::vector<std::string> names;
    const std::string out = MakeTempPath(".reslist.txt");
    std::string args = "res list \"" + resPath + "\" -o \"" + out + "\"";
    if (!Run(args, err)) return names;
    std::ifstream f(out);
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty()) names.push_back(line);
    }
    std::error_code ec;
    fs::remove(out, ec);
    return names;
}

bool ResExtract(const std::string& resPath, const std::string& outDir, std::string& err) {
    std::error_code ec;
    fs::create_directories(outDir, ec);
    return Run("res extract \"" + resPath + "\" -o \"" + outDir + "\"", err);
}

bool ResExtractOne(const std::string& resPath, const std::string& entryName,
                   const std::string& outDir, std::string& err) {
    std::error_code ec;
    fs::create_directories(outDir, ec);
    return Run("res extract \"" + resPath + "\" --file \"" + entryName +
               "\" -o \"" + outDir + "\"", err);
}

bool ResAppend(const std::string& srcRes,
               const std::vector<std::string>& newFiles,
               const std::string& dstRes,
               const std::string& prefix,
               std::string& err) {
    if (newFiles.empty()) {
        err = "ResAppend: no files to append";
        return false;
    }
    std::string args = "res append \"" + srcRes + "\"";
    for (const auto& f : newFiles) args += " \"" + f + "\"";
    args += " -o \"" + dstRes + "\"";
    if (!prefix.empty()) args += " --prefix \"" + prefix + "\"";
    return Run(args, err);
}

bool ResPack(const std::string& dir, const std::string& outRes, std::string& err) {
    return Run("res pack \"" + dir + "\" \"" + outRes + "\"", err);
}

bool ResRepack(const std::string& origRes, const std::string& dir,
               const std::string& outRes, std::string& err) {
    return Run("res repack \"" + origRes + "\" \"" + dir + "\" -o \"" + outRes + "\"", err);
}

// ─── dat / mtp / graph ────────────────────────────────────────────────────

std::string DatExportJson(const std::string& datPath, const std::string& outJson,
                          std::string& err, bool asText) {
    std::string out = outJson;
    if (out.empty()) out = MakeTempPath(".dat.json");
    std::string args = "dat export \"" + datPath + "\" -o \"" + out + "\"";
    if (asText) args += " --text";
    if (!Run(args, err)) return {};
    return out;
}

bool DatToMtp(const std::string& datPath, const std::string& outMtp, std::string& err) {
    return Run("dat to-mtp \"" + datPath + "\" -o \"" + outMtp + "\"", err);
}

std::string MtpDumpJson(const std::string& mtpPath, const std::string& outJson,
                        std::string& err) {
    std::string out = outJson;
    if (out.empty()) out = MakeTempPath(".mtp.json");
    if (!Run("mtp dump \"" + mtpPath + "\" -o \"" + out + "\"", err)) return {};
    return out;
}

std::string MtpInfo(const std::string& mtpPath, std::string& err) {
    std::string out = MakeTempPath(".mtp.txt");
    if (!Run("mtp info \"" + mtpPath + "\" -o \"" + out + "\"", err)) return {};
    std::ifstream f(out);
    std::stringstream ss; ss << f.rdbuf();
    std::error_code ec; fs::remove(out, ec);
    return ss.str();
}

std::string GraphExportJson(const std::string& datPath, const std::string& outJson,
                            std::string& err) {
    std::string out = outJson;
    if (out.empty()) out = MakeTempPath(".graph.json");
    if (!Run("graph export \"" + datPath + "\" -o \"" + out + "\"", err)) return {};
    return out;
}

std::string GraphInfo(const std::string& datPath, std::string& err) {
    std::string out = MakeTempPath(".graph.txt");
    if (!Run("graph info \"" + datPath + "\" -o \"" + out + "\"", err)) return {};
    std::ifstream f(out);
    std::stringstream ss; ss << f.rdbuf();
    std::error_code ec; fs::remove(out, ec);
    return ss.str();
}

// ─── tex / fnt ────────────────────────────────────────────────────────────

bool TexDecode(const std::string& texPath, const std::string& outDir, std::string& err) {
    std::error_code ec;
    fs::create_directories(outDir, ec);
    return Run("tex decode \"" + texPath + "\" -o \"" + outDir + "\"", err);
}

bool TexToPng(const std::string& texPath, const std::string& outPng, std::string& err) {
    return Run("tex to-png \"" + texPath + "\" -o \"" + outPng + "\"", err);
}

bool TexToTga(const std::string& texPath, const std::string& outTga, std::string& err) {
    return Run("tex to-tga \"" + texPath + "\" -o \"" + outTga + "\"", err);
}

std::string TexInfo(const std::string& texPath, std::string& err) {
    std::string out = MakeTempPath(".tex.txt");
    if (!Run("tex info \"" + texPath + "\" -o \"" + out + "\"", err)) return {};
    std::ifstream f(out);
    std::stringstream ss; ss << f.rdbuf();
    std::error_code ec; fs::remove(out, ec);
    return ss.str();
}

bool FntExportPng(const std::string& fntPath, const std::string& outPng, std::string& err) {
    return Run("fnt export \"" + fntPath + "\" -o \"" + outPng + "\"", err);
}

std::string FntInfo(const std::string& fntPath, std::string& err) {
    std::string out = MakeTempPath(".fnt.txt");
    if (!Run("fnt info \"" + fntPath + "\" -o \"" + out + "\"", err)) return {};
    std::ifstream f(out);
    std::stringstream ss; ss << f.rdbuf();
    std::error_code ec; fs::remove(out, ec);
    return ss.str();
}

// ─── qvm / qsc ────────────────────────────────────────────────────────────

bool QvmDecompile(const std::string& qvmPath, const std::string& outQsc, std::string& err) {
    return Run("qvm decompile \"" + qvmPath + "\" -o \"" + outQsc + "\"", err);
}

std::string QvmInfo(const std::string& qvmPath, std::string& err) {
    std::string out = MakeTempPath(".qvm.txt");
    if (!Run("qvm info \"" + qvmPath + "\" -o \"" + out + "\"", err)) return {};
    std::ifstream f(out);
    std::stringstream ss; ss << f.rdbuf();
    std::error_code ec; fs::remove(out, ec);
    return ss.str();
}

bool QvmDisasm(const std::string& qvmPath, const std::string& outTxt, std::string& err) {
    return Run("qvm disasm \"" + qvmPath + "\" -o \"" + outTxt + "\"", err);
}

bool QscCompile(const std::string& qscPath, const std::string& outQvm, std::string& err) {
    return Run("qsc compile \"" + qscPath + "\" -o \"" + outQvm + "\"", err);
}

bool QscValidate(const std::string& qscPath, std::string& err) {
    return Run("qsc validate \"" + qscPath + "\"", err);
}

// ─── terrain / mef ────────────────────────────────────────────────────────

bool TerrainExportLmp(const std::string& lmpPath, const std::string& outPgm, std::string& err) {
    return Run("terrain export-lmp \"" + lmpPath + "\" -o \"" + outPgm + "\"", err);
}

std::string TerrainExportCtr(const std::string& ctrPath, const std::string& outJson,
                             std::string& err) {
    std::string out = outJson;
    if (out.empty()) out = MakeTempPath(".ctr.json");
    if (!Run("terrain export-ctr \"" + ctrPath + "\" -o \"" + out + "\"", err)) return {};
    return out;
}

std::string TerrainInfo(const std::string& path, std::string& err) {
    std::string out = MakeTempPath(".terrain.txt");
    if (!Run("terrain info \"" + path + "\" -o \"" + out + "\"", err)) return {};
    std::ifstream f(out);
    std::stringstream ss; ss << f.rdbuf();
    std::error_code ec; fs::remove(out, ec);
    return ss.str();
}

bool MefExportObj(const std::string& mefPath, const std::string& outObj,
                  const std::string& datFile, const std::string& texDir,
                  std::string& err) {
    std::string args = "mef export \"" + mefPath + "\" -o \"" + outObj + "\"";
    if (!datFile.empty()) args += " --dat \"" + datFile + "\"";
    if (!texDir.empty()) args += " --texdir \"" + texDir + "\"";
    return Run(args, err);
}

std::string MefInfo(const std::string& mefPath, std::string& err) {
    std::string out = MakeTempPath(".mef.txt");
    if (!Run("mef info \"" + mefPath + "\" -o \"" + out + "\"", err)) return {};
    std::ifstream f(out);
    std::stringstream ss; ss << f.rdbuf();
    std::error_code ec; fs::remove(out, ec);
    return ss.str();
}

// ─── iff ────────────────────────────────────────────────────────────────────

bool IffConvert(const std::string& iffPath, const std::string& outDir, std::string& err) {
    std::error_code ec;
    fs::create_directories(outDir, ec);
    return Run("iff convert \"" + iffPath + "\" \"" + outDir + "\"", err);
}

// ─── wav ────────────────────────────────────────────────────────────────────

bool WavConvert(const std::string& srcWav, const std::string& outWav, std::string& err) {
    std::error_code ec;
    fs::create_directories(fs::path(outWav).parent_path(), ec);
    return Run("wav convert \"" + srcWav + "\" -o \"" + outWav + "\"", err);
}

} // namespace igi1conv
