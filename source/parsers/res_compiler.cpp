#include "pch.h"
#include "res_compiler.h"
#include "../logger.h"
#include "qsc_lexer.h"
#include "qsc_parser.h"
#include <fstream>
#include <filesystem>
#include <iostream>
#include <vector>
#include <cstring>

static const uint32_t FOURCC_ILFF = 0x46464C49; // "ILFF"
static const uint32_t FOURCC_IRES = 0x53455249; // "IRES"
static const uint32_t FOURCC_NAME = 0x454D414E; // "NAME"
static const uint32_t FOURCC_BODY = 0x59444F42; // "BODY"

static void WriteU32LE(std::ostream& os, uint32_t val) {
    uint8_t buf[4] = {
        static_cast<uint8_t>(val & 0xFF),
        static_cast<uint8_t>((val >> 8) & 0xFF),
        static_cast<uint8_t>((val >> 16) & 0xFF),
        static_cast<uint8_t>((val >> 24) & 0xFF)
    };
    os.write(reinterpret_cast<const char*>(buf), 4);
}

static void WriteFourCC(std::ostream& os, uint32_t val) {
    WriteU32LE(os, val);
}

bool RES_GenerateQSC(const std::string& inputDir, const std::string& outQscPath, const std::string& outResName, std::string& error) {
    std::ofstream out(outQscPath);
    if (!out) {
        error = "Failed to create " + outQscPath;
        return false;
    }

    out << "BeginResource(\"" << outResName << "\");\n";

    namespace fs = std::filesystem;
    try {
        if (!fs::exists(inputDir)) {
            error = "Input directory does not exist: " + inputDir;
            return false;
        }

        for (const auto& entry : fs::recursive_directory_iterator(inputDir)) {
            if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                for (auto& c : ext) c = std::tolower(c);

                if (ext == ".mef") {
                    std::string relPath = fs::relative(entry.path(), inputDir).string();
                    for (auto& c : relPath) if (c == '\\') c = '/';
                    out << "AddResource(\"" << relPath << "\", \"LOCAL:" << relPath << "\");\n";
                }
            }
        }
    } catch(const std::exception& e) {
        error = "Filesystem error: " + std::string(e.what());
        return false;
    }

    out << "EndResource();\n";
    return true;
}

bool RES_Compile(const std::string& scriptPath, std::string& error) {
    std::ifstream f(scriptPath, std::ios::binary);
    if (!f) {
        error = "Failed to open script: " + scriptPath;
        return false;
    }
    std::string src((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto lr = qsc::Lex(src);
    if (!lr.ok) { error = "Lex error: " + lr.error; return false; }
    auto pr = qsc::Parse(lr.tokens);
    if (!pr.ok) { error = "Parse error: " + pr.error; return false; }

    if (!pr.program || pr.program->kind != qsc::NodeKind::Program) {
        error = "Invalid script format";
        return false;
    }

    std::string outResName;
    std::vector<std::pair<std::string, std::string>> resources;

    for (const auto& stmt : pr.program->children) {
        if (stmt->kind == qsc::NodeKind::ExprStmt && !stmt->children.empty()) {
            auto call = stmt->children[0].get();
            if (call->kind == qsc::NodeKind::Call) {
                if (call->s_val == "BeginResource" && call->children.size() == 1) {
                    if (call->children[0]->kind == qsc::NodeKind::StringLit) {
                        outResName = call->children[0]->s_val;
                    }
                } else if (call->s_val == "AddResource" && call->children.size() == 2) {
                    if (call->children[0]->kind == qsc::NodeKind::StringLit && call->children[1]->kind == qsc::NodeKind::StringLit) {
                        resources.push_back({call->children[0]->s_val, call->children[1]->s_val});
                    }
                }
            }
        }
    }

    if (outResName.empty()) {
        error = "BeginResource(\"out\") not found in script";
        return false;
    }

    namespace fs = std::filesystem;
    fs::path baseDir = fs::path(scriptPath).parent_path();
    fs::path outPath = baseDir / outResName;

    std::ofstream os(outPath, std::ios::binary);
    if (!os) {
        error = "Failed to create output file: " + outPath.string();
        return false;
    }

    // Write dummy ILFF header (20 bytes)
    os.write("ILFF\0\0\0\0\0\0\0\0\0\0\0\0IRES", 20);

    for (const auto& res : resources) {
        std::string internalName = res.first;
        std::string localPath = res.second;
        
        if (localPath.substr(0, 6) == "LOCAL:") {
            localPath = localPath.substr(6);
        }
        fs::path fullLocalPath = baseDir / localPath;
        
        std::ifstream srcF(fullLocalPath, std::ios::binary | std::ios::ate);
        if (!srcF) {
            Logger::Get().Log(LogLevel::WARNING, "[RES] Could not read file, skipping: " + fullLocalPath.string());
            continue;
        }
        std::streamsize dataSize = srcF.tellg();
        srcF.seekg(0, std::ios::beg);
        
        // Chunk NAME
        uint32_t nameSize = internalName.length() + 1; // include null
        uint32_t namePadding = (4 - (nameSize % 4)) % 4;
        uint32_t nameSkip = 16 + nameSize + namePadding;
        
        WriteFourCC(os, FOURCC_NAME);
        WriteU32LE(os, nameSize);
        WriteU32LE(os, 0); // align
        WriteU32LE(os, nameSkip);
        os.write(internalName.c_str(), nameSize);
        if (namePadding > 0) {
            char pad[3] = {0};
            os.write(pad, namePadding);
        }

        // Chunk BODY
        uint32_t bodySize = dataSize;
        uint32_t bodyPadding = (4 - (bodySize % 4)) % 4;
        uint32_t bodySkip = 16 + bodySize + bodyPadding;

        WriteFourCC(os, FOURCC_BODY);
        WriteU32LE(os, bodySize);
        WriteU32LE(os, 0); // align
        WriteU32LE(os, bodySkip);
        
        char buf[8192];
        while (srcF.read(buf, sizeof(buf))) {
            os.write(buf, srcF.gcount());
        }
        os.write(buf, srcF.gcount());
        
        if (bodyPadding > 0) {
            char pad[3] = {0};
            os.write(pad, bodyPadding);
        }
    }

    uint32_t finalSize = static_cast<uint32_t>(os.tellp());
    os.seekp(4, std::ios::beg);
    uint32_t ilffSize = finalSize - 8;
    WriteU32LE(os, ilffSize);

    return true;
}
