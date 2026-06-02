#pragma once

#include <string>
#include <vector>

struct VerifyLevelParams {
    std::vector<int> levels;
    int  timeout    = 0;    // 0 = wait indefinitely
    bool skipLaunch = false;
    std::string gamePath;   // empty = Utils::GetIGIRootPath()
    std::string logPath;    // empty = gamePath\igi1ed.log
    std::string reportJson; // empty = no JSON report
    std::string reportMd;   // empty = no Markdown report
    std::string reportDir;  // empty = no per-level files
    int  delay      = 5;    // seconds between levels
};

class CLIHandler {
public:
  // Checks if the given arguments contain any of the headless CLI flags
  static bool IsCLICommand(int argc, char **argv);

  // Processes the arguments and executes the corresponding parser
  // Returns 0 on success, non-zero on failure
  static int Process(int argc, char **argv);

private:
  static int ParseMEF(const std::string &filepath);
  static int ExportMEFToObj(const std::string &filepath,
                            const std::string &outpath);
  static int ExportMEFToAscii(const std::string &filepath,
                              const std::string &outpath);
  static int ExportMEFBundle(const std::string &filepath,
                             const std::string &outDir,
                             const std::string &datPath,
                             const std::string &texDir);
  static int ParseQVM(const std::string &filepath, bool decompile,
                      const std::string &outpath);
  static int LexQSC(const std::string &inpath);
  static int ParseQSC(const std::string &inpath);
  static int CompileQSC(const std::string &inpath, const std::string &outpath);
  static int ParseRES(const std::string &filepath,
                      const std::string &extract_name,
                      const std::string &outpath);
  static int ExtractAllRES(const std::string &filepath,
                           const std::string &outdir);
  static int ParseMTP(const std::string &filepath);
  static int ParseDAT(const std::string &filepath, const std::string &outPath,
                      const std::string &modelFilter, bool textMode = false);
  static int ParseTerrain(const std::string &filepath);
  static int ParseTEX(const std::string &filepath,
                      const std::string &exportDir);
  static int ParseFNT(const std::string &filepath);
  static int ParseGraph(const std::string &filepath);
  static int ExtractLevelResources(int levelNo, const std::string &outDir);
  static int VerifyLevel(const VerifyLevelParams &params);
  static void PrintHelp();
};