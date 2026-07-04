#pragma once

#include <string>
#include <vector>

struct VerifyLevelParams {
    std::vector<int> levels;
    int  timeout    = 15;   // 15-second cap per level
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

  // Processes the arguments and executes the corresponding command
  // Returns 0 on success, non-zero on failure
  static int Process(int argc, char **argv);
  static int VerifyLevel(const VerifyLevelParams &params);

private:
  static int ExtractLevelResources(int levelNo, const std::string &outDir);
  static void PrintHelp();
};
