#pragma once

#include <string>

class CLIHandler {
public:
    // Checks if the given arguments contain any of the headless CLI flags
    static bool IsCLICommand(int argc, char** argv);
    
    // Processes the arguments and executes the corresponding parser
    // Returns 0 on success, non-zero on failure
    static int Process(int argc, char** argv);

private:
    static int ParseMEF(const std::string& filepath);
    static int ExportMEFToObj(const std::string& filepath, const std::string& outpath);
    static int ExportMEFToAscii(const std::string& filepath, const std::string& outpath);
    static int ParseQVM(const std::string& filepath, bool decompile, const std::string& outpath);
    static int CompileQSC(const std::string& inpath, const std::string& outpath);
    static int LexQSC(const std::string& inpath);
    static int ParseQSC(const std::string& inpath);
    static int CompileQSCNative(const std::string& inpath, const std::string& outpath);
    static int ParseRES(const std::string& filepath, const std::string& extract_name, const std::string& outpath);
    static int ExtractAllRES(const std::string& filepath, const std::string& outdir);
    static int ParseMTP(const std::string& filepath);
    static int ParseTerrain(const std::string& filepath);
    static int ParseTEX(const std::string& filepath, const std::string& exportDir);
    static int ParseGraph(const std::string& filepath);
    static int ExtractLevelResources(int levelNo, const std::string& outDir);
    static void PrintHelp();
};