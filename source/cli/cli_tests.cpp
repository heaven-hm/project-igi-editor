#include "pch.h"
#include "cli_tests.h"
#include "cli_handler.h"
#include "renderer/res_writer.h"
#include "level/mtp_writer.h"
#include "renderer/tex_writer.h"
#include "level/qvm_parser.h"
#include "level/qvm_decompiler.h"
#include "renderer/graph_writer.h"
#include "level/terrain_files.h"
#include "renderer/fnt_parser.h"
#include "utils.h"
#include "common.h"
#include "logger.h"
#include "config.h"
#include "level/qsc_lexer.h"
#include "level/qsc_parser.h"
#include "level/qvm_compiler.h"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

static int g_testCount = 0;
static int g_failCount = 0;

#define RUN_SUB_TEST(name, func) \
    std::cout << "[SUITE] " << name << " ...\n"; \
    func();

#define ASSERT_TRUE(cond, msg) \
    g_testCount++; \
    if (!(cond)) { \
        std::cerr << "  [FAIL] " << msg << " (Assertion '" << #cond << "' failed at " << __FILE__ << ":" << __LINE__ << ")\n"; \
        g_failCount++; \
    } else { \
        std::cout << "  [PASS] " << msg << "\n"; \
    }

#define ASSERT_FALSE(cond, msg) ASSERT_TRUE(!(cond), msg)

// Helper: check if file has any lines with "//"
static bool FileContainsComments(const std::string& filepath) {
    std::ifstream f(filepath);
    if (!f.is_open()) return true; // Treat unopenable as failure
    std::string line;
    while (std::getline(f, line)) {
        if (line.find("//") != std::string::npos) {
            return true;
        }
    }
    return false;
}

// Helper: verify Task_New parameters start with a pure integer parent ID
static bool VerifyTaskNewLines(const std::string& filepath) {
    std::ifstream f(filepath);
    if (!f.is_open()) return false;
    std::string line;
    while (std::getline(f, line)) {
        size_t pos = line.find("Task_New(");
        if (pos != std::string::npos) {
            std::string args = line.substr(pos + 9);
            size_t firstNonSpace = args.find_first_not_of(" \t");
            if (firstNonSpace != std::string::npos) {
                args = args.substr(firstNonSpace);
            }
            if (args.empty()) {
                std::cerr << "  [DEBUG] Task_New has empty arguments in: " << line << "\n";
                return false;
            }
            char firstChar = args[0];
            if (firstChar != '-' && !std::isdigit(static_cast<unsigned char>(firstChar))) {
                std::cerr << "  [DEBUG] Task_New first argument starts with illegal character '" << firstChar << "' in line: " << line << "\n";
                return false;
            }
        }
    }
    return true;
}

static void TestRES() {
    std::string resPath = Utils::GetIGIRootPath() + "\\language\\ENGLISH\\MESSAGES.RES";
    std::string scratchExt = "scratch\\cpp_tests\\res_extracted";

    ASSERT_TRUE(fs::exists(resPath), "MESSAGES.RES exists");
    if (!fs::exists(resPath)) return;
    
    RESFile res = RES_Parse(resPath);
    ASSERT_TRUE(res.valid, "RES parser successfully parsed archive");
    ASSERT_TRUE(res.entries.size() > 0, "RES has at least one entry");

    fs::create_directories(scratchExt);
    int extracted = 0;
    for (const auto& entry : res.entries) {
        std::string safeName = entry.name;
        for (char& c : safeName) {
            if (c == ':' || c == '*' || c == '?' || c == '\"' || c == '<' || c == '>' || c == '|') {
                c = '_';
            }
        }
        std::string outpath = scratchExt + "\\" + safeName;
        fs::create_directories(fs::path(outpath).parent_path());
        if (File_SaveBinary(outpath.c_str(), entry.data.data(), entry.data.size())) {
            extracted++;
        }
    }
    ASSERT_TRUE(extracted > 0, "Extracted files from RES successfully");

    // Test nonexistent RES file
    RESFile missing = RES_Parse("scratch\\cpp_tests\\missing_res_file.res");
    ASSERT_FALSE(missing.valid, "Nonexistent RES file should be invalid");

    // Test models RES archive parsing and extraction
    std::string modelsResPath = Utils::GetIGIRootPath() + "\\missions\\location0\\level1\\models\\level1.res";
    ASSERT_TRUE(fs::exists(modelsResPath), "level1.res exists");
    if (fs::exists(modelsResPath)) {
        RESFile modelsRes = RES_Parse(modelsResPath);
        ASSERT_TRUE(modelsRes.valid, "RES parser successfully parsed models archive");
        ASSERT_TRUE(modelsRes.entries.size() > 0, "Models RES has at least one entry");
        
        std::string modelsScratch = "scratch\\cpp_tests\\models_extracted";
        fs::create_directories(modelsScratch);
        int extractedMefs = 0;
        for (const auto& entry : modelsRes.entries) {
            std::string name = entry.name;
            size_t slash = name.find_last_of("/\\");
            std::string filename = (slash != std::string::npos) ? name.substr(slash + 1) : name;
            if (filename.find(".mef") != std::string::npos) {
                std::string outpath = modelsScratch + "\\" + filename;
                if (File_SaveBinary(outpath.c_str(), entry.data.data(), entry.data.size())) {
                    extractedMefs++;
                }
            }
        }
        ASSERT_TRUE(extractedMefs > 0, "Successfully extracted MEF files from models RES");
    }
}

static void TestMTP() {
    std::string mtpPath = Utils::GetIGIRootPath() + "\\missions\\location0\\level1\\level1.mtp";
    ASSERT_TRUE(fs::exists(mtpPath), "level1.mtp exists");
    if (fs::exists(mtpPath)) {
        MTPFile mtp = MTP_Parse(mtpPath);
        ASSERT_TRUE(mtp.valid, "MTP parser successfully parsed level1.mtp");
        ASSERT_TRUE(mtp.models.size() > 0, "MTP contains texture models");
    }

    MTPFile missingMtp = MTP_Parse("scratch\\cpp_tests\\missing.mtp");
    ASSERT_FALSE(missingMtp.valid, "Nonexistent MTP file should be invalid");
}

static void TestTEX() {
    std::string texPath = Utils::GetIGIRootPath() + "\\missions\\location0\\level1\\terrain\\terrain.tex";
    std::string scratchTex = "scratch\\cpp_tests\\tex_tga";

    ASSERT_TRUE(fs::exists(texPath), "terrain.tex exists");
    if (fs::exists(texPath)) {
        TEXFile tex = TEX_Parse(texPath);
        ASSERT_TRUE(tex.valid, "TEX parser successfully parsed terrain.tex");
        ASSERT_TRUE(tex.images.size() > 0, "TEX contains texture images");

        fs::create_directories(scratchTex);
        int written = TEX_ExportTGA(tex, texPath, scratchTex);
        ASSERT_TRUE(written > 0, "TEX exported TGA files to scratch folder successfully");
    }

    TEXFile missingTex = TEX_Parse("scratch\\cpp_tests\\missing.tex");
    ASSERT_FALSE(missingTex.valid, "Nonexistent TEX file should be invalid");
}

static void TestQVM() {
    std::string baseDir = Utils::GetIGIRootPath() + "\\missions\\location0\\level1\\";
    std::string missionPath = baseDir + "mission.qvm";
    std::string objectsPath = baseDir + "objects.qvm";
    std::string soundsPath  = baseDir + "sounds\\sounds.qvm";
    std::string terrainPath = baseDir + "terrain\\terrain.qvm";
    std::string aiPath      = baseDir + "ai\\1205.qvm";

    ASSERT_TRUE(fs::exists(missionPath), "mission.qvm exists");
    ASSERT_TRUE(fs::exists(objectsPath), "objects.qvm exists");
    if (!fs::exists(missionPath) || !fs::exists(objectsPath)) return;

    QVMFile qvmMission = QVM_Parse(missionPath);
    ASSERT_TRUE(qvmMission.valid, "Parsed mission.qvm successfully");

    QVMFile qvmObjects = QVM_Parse(objectsPath);
    ASSERT_TRUE(qvmObjects.valid, "Parsed objects.qvm successfully");

    std::string outMission = "scratch\\cpp_tests\\mission.qsc";
    std::string outObjects = "scratch\\cpp_tests\\objects.qsc";

    fs::create_directories("scratch\\cpp_tests");

    // Test mission decompile
    bool decompMission = QVM_Decompile(qvmMission, outMission);
    ASSERT_TRUE(decompMission, "Decompiled mission.qvm to QSC successfully");
    ASSERT_FALSE(FileContainsComments(outMission), "mission.qsc contains NO comments (//)");
    ASSERT_TRUE(VerifyTaskNewLines(outMission), "mission.qsc Task_New first parameter is correct integer format");

    // Test objects decompile
    bool decompObjects = QVM_Decompile(qvmObjects, outObjects);
    ASSERT_TRUE(decompObjects, "Decompiled objects.qvm to QSC successfully");
    ASSERT_FALSE(FileContainsComments(outObjects), "objects.qsc contains NO comments (//)");
    ASSERT_TRUE(VerifyTaskNewLines(outObjects), "objects.qsc Task_New first parameter is correct integer format");

    // Test objects compiler round-trip!
    std::string recompiledObjectsPath = "scratch\\cpp_tests\\objects_recompiled.qvm";
    std::ifstream qscIn(outObjects);
    std::string qscSrc((std::istreambuf_iterator<char>(qscIn)), std::istreambuf_iterator<char>());
    auto lexR   = qsc::Lex(qscSrc);
    auto parseR = lexR.ok ? qsc::Parse(lexR.tokens) : qsc::ParseResult{};
    std::string compErr;
    bool compileSuccess = lexR.ok && parseR.ok &&
                          qvm::CompileToFile(*parseR.program, recompiledObjectsPath, &compErr);
    ASSERT_TRUE(compileSuccess, "Recompiled objects.qsc back to objects.qvm successfully");
    if (compileSuccess) {
        QVMFile qvmRecompiled = QVM_Parse(recompiledObjectsPath);
        ASSERT_TRUE(qvmRecompiled.valid, "Parsed recompiled objects.qvm successfully");
    }

    if (fs::exists(soundsPath)) {
        QVMFile qvmSounds = QVM_Parse(soundsPath);
        ASSERT_TRUE(qvmSounds.valid, "Parsed sounds.qvm successfully");
        std::string outSounds = "scratch\\cpp_tests\\sounds.qsc";
        bool decompSounds = QVM_Decompile(qvmSounds, outSounds);
        ASSERT_TRUE(decompSounds, "Decompiled sounds.qvm to QSC successfully");
        ASSERT_FALSE(FileContainsComments(outSounds), "sounds.qsc contains NO comments (//)");
    }

    if (fs::exists(terrainPath)) {
        QVMFile qvmTerrain = QVM_Parse(terrainPath);
        ASSERT_TRUE(qvmTerrain.valid, "Parsed terrain.qvm successfully");
        std::string outTerrain = "scratch\\cpp_tests\\terrain.qsc";
        bool decompTerrain = QVM_Decompile(qvmTerrain, outTerrain);
        ASSERT_TRUE(decompTerrain, "Decompiled terrain.qvm to QSC successfully");
        ASSERT_FALSE(FileContainsComments(outTerrain), "terrain.qsc contains NO comments (//)");
    }

    if (fs::exists(aiPath)) {
        QVMFile qvmAI = QVM_Parse(aiPath);
        ASSERT_TRUE(qvmAI.valid, "Parsed ai/1205.qvm successfully");
        std::string outAI = "scratch\\cpp_tests\\ai_1205.qsc";
        bool decompAI = QVM_Decompile(qvmAI, outAI);
        ASSERT_TRUE(decompAI, "Decompiled ai/1205.qvm to QSC successfully");
        ASSERT_FALSE(FileContainsComments(outAI), "ai_1205.qsc contains NO comments (//)");
    }

    // Test nonexistent and corrupted QVM files
    QVMFile missingQvm = QVM_Parse("scratch\\cpp_tests\\missing.qvm");
    ASSERT_FALSE(missingQvm.valid, "Nonexistent QVM file should be invalid");

    std::string corruptPath = "scratch\\cpp_tests\\corrupt.qvm";
    std::vector<uint8_t> corruptBytes(32, 0xff);
    fs::create_directories("scratch\\cpp_tests");
    if (File_SaveBinary(corruptPath.c_str(), corruptBytes.data(), corruptBytes.size())) {
        QVMFile corruptQvm = QVM_Parse(corruptPath);
        ASSERT_FALSE(corruptQvm.valid, "Corrupt QVM file should fail to parse");
    }
}

static void TestLevelLoad() {
    std::string levelBase = Utils::GetIGIRootPath() + "\\missions\\location0";
    std::vector<std::string> levels = { "level1", "level2", "level3" };

    for (const auto& lvl : levels) {
        std::string ldir = levelBase + "\\" + lvl;
        if (!fs::exists(ldir)) {
            std::cout << "  [SKIP] Level load for " << lvl << " (Directory not found: " << ldir << ")\n";
            continue;
        }

        std::cout << "  [INFO] Verifying loader for level: " << lvl << "\n";

        // 1. Verify mission.qvm
        std::string missionPath = ldir + "\\mission.qvm";
        if (fs::exists(missionPath)) {
            QVMFile qvm = QVM_Parse(missionPath);
            ASSERT_TRUE(qvm.valid, lvl + ": Parse mission.qvm");
            
            std::string outQsc = "scratch\\cpp_tests\\" + lvl + "_mission.qsc";
            bool decomp = QVM_Decompile(qvm, outQsc);
            ASSERT_TRUE(decomp, lvl + ": Decompile mission.qvm");
            ASSERT_FALSE(FileContainsComments(outQsc), lvl + ": decompiled mission.qsc contains NO comments");
            ASSERT_TRUE(VerifyTaskNewLines(outQsc), lvl + ": decompiled mission.qsc Task_New integer task parent IDs");
        }

        // 2. Verify terrain.qvm if present
        std::string terrainPath = ldir + "\\terrain\\terrain.qvm";
        if (fs::exists(terrainPath)) {
            QVMFile qvm = QVM_Parse(terrainPath);
            ASSERT_TRUE(qvm.valid, lvl + ": Parse terrain.qvm");
        }

        // 3. Verify terrain.tex if present
        std::string texPath = ldir + "\\terrain\\terrain.tex";
        if (fs::exists(texPath)) {
            TEXFile tex = TEX_Parse(texPath);
            ASSERT_TRUE(tex.valid, lvl + ": Parse terrain.tex");
        }

        // 4. Verify terrain LMP lightmaps
        std::string lmpPath = ldir + "\\terrain\\terrain.lmp";
        if (fs::exists(lmpPath)) {
            pics_s pics;
            bool lmpLoad = LMP_Load(lmpPath.c_str(), pics);
            ASSERT_TRUE(lmpLoad, lvl + ": Parse terrain.lmp (Lightmaps)");
            if (lmpLoad) {
                Pic_FreePics(pics);
            }
        }

        // 5. Verify navigation graph
        std::string graphsDir = ldir + "\\graphs";
        if (fs::exists(graphsDir)) {
            for (const auto& entry : fs::directory_iterator(graphsDir)) {
                if (entry.path().extension() == ".dat") {
                    GraphFile graph = GRAPH_Parse(entry.path().string());
                    ASSERT_TRUE(graph.valid, lvl + ": Parse navigation graph " + entry.path().filename().string());
                    break; // Just verify the first navigation graph
                }
            }
        }
    }
}

static void TestExtractLevel() {
    std::string outDir = "scratch\\cpp_tests\\extracted_level1";
    if (fs::exists(outDir)) {
        fs::remove_all(outDir);
    }

    // Prepare arguments for Process: igi1ed.exe --extract-level 1 <outDir>
    char* argv[] = {
        (char*)"igi1ed.exe",
        (char*)"--extract-level",
        (char*)"1",
        (char*)outDir.c_str()
    };
    int argc = sizeof(argv) / sizeof(argv[0]);
    int code = CLIHandler::Process(argc, argv);
    ASSERT_TRUE(code == 0, "CLIHandler Process --extract-level 1 succeeded");

    std::string modelsDir = outDir + "\\models";
    std::string texturesDir = outDir + "\\textures";

    ASSERT_TRUE(fs::exists(modelsDir), "Extracted models directory exists");
    ASSERT_TRUE(fs::exists(texturesDir), "Extracted textures directory exists");

    int modelCount = 0;
    int mefCount = 0;
    if (fs::exists(modelsDir)) {
        for (const auto& entry : fs::directory_iterator(modelsDir)) {
            modelCount++;
            if (entry.path().extension() == ".mef") {
                mefCount++;
            }
        }
    }

    int textureCount = 0;
    if (fs::exists(texturesDir)) {
        for (const auto& entry : fs::directory_iterator(texturesDir)) {
            textureCount++;
        }
    }

    ASSERT_TRUE(modelCount > 100, "Extracted more than 100 models");
    ASSERT_TRUE(mefCount > 0, "Extracted at least one MEF model file");
    ASSERT_TRUE(textureCount > 100, "Extracted more than 100 textures");
}

// Write an RGBA buffer to an uncompressed 32-bit TGA (top-down).
static void WriteTGA(const std::string& path, int w, int h, const std::vector<uint8_t>& rgba) {
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) return;
    uint8_t hdr[18] = {0};
    hdr[2] = 2;                       // uncompressed true-color
    hdr[12] = (uint8_t)(w & 0xff); hdr[13] = (uint8_t)((w >> 8) & 0xff);
    hdr[14] = (uint8_t)(h & 0xff); hdr[15] = (uint8_t)((h >> 8) & 0xff);
    hdr[16] = 32;                     // bits per pixel
    hdr[17] = 0x28;                   // top-down, 8-bit alpha
    f.write(reinterpret_cast<char*>(hdr), 18);
    // TGA is BGRA.
    for (int i = 0; i < w * h; ++i) {
        uint8_t bgra[4] = { rgba[i*4+2], rgba[i*4+1], rgba[i*4+0], rgba[i*4+3] };
        f.write(reinterpret_cast<char*>(bgra), 4);
    }
}

// Software-render a string with a parsed FntFont onto an opaque RGBA canvas,
// mirroring the renderer's per-glyph advance so the on-screen spacing can be
// inspected as a TGA without launching the GL editor.
static void RenderSampleToTGA(const FntFont& font, const std::string& text, const std::string& outPath) {
    if (!font.valid) return;
    const int pad = 4;
    const int canvasH = font.lineHeight + pad * 2;
    // Measure width.
    int penW = pad;
    for (char ch : text) {
        auto it = font.glyphs.find((int)(unsigned char)ch);
        if (it == font.glyphs.end()) { penW += (font.lineHeight > 0 ? font.lineHeight / 2 : 4); continue; }
        penW += it->second.advance;
    }
    const int canvasW = penW + pad;
    std::vector<uint8_t> canvas((size_t)canvasW * canvasH * 4, 0);
    // Dark blue background so white glyph mask is visible.
    for (size_t i = 0; i < (size_t)canvasW * canvasH; ++i) {
        canvas[i*4+0] = 18; canvas[i*4+1] = 18; canvas[i*4+2] = 32; canvas[i*4+3] = 255;
    }
    int penX = pad;
    for (char ch : text) {
        auto it = font.glyphs.find((int)(unsigned char)ch);
        if (it == font.glyphs.end()) { penX += (font.lineHeight > 0 ? font.lineHeight / 2 : 4); continue; }
        const FntGlyph& g = it->second;
        int ax = (int)(g.u0 * font.texWidth + 0.5f);
        int ay = (int)(g.v0 * font.texHeight + 0.5f);
        for (int gy = 0; gy < g.height; ++gy) {
            int sy = ay + gy;
            int dy = pad + gy;
            if (sy < 0 || sy >= font.texHeight || dy < 0 || dy >= canvasH) continue;
            for (int gx = 0; gx < g.width; ++gx) {
                int sx = ax + gx;
                int dx = penX + gx;
                if (sx < 0 || sx >= font.texWidth || dx < 0 || dx >= canvasW) continue;
                uint8_t a = font.rgba[((size_t)sy * font.texWidth + sx) * 4 + 3];
                if (a == 0) continue;
                size_t di = ((size_t)dy * canvasW + dx) * 4;
                // Alpha-blend white glyph over background.
                canvas[di+0] = (uint8_t)((255 * a + canvas[di+0] * (255 - a)) / 255);
                canvas[di+1] = (uint8_t)((255 * a + canvas[di+1] * (255 - a)) / 255);
                canvas[di+2] = (uint8_t)((255 * a + canvas[di+2] * (255 - a)) / 255);
            }
        }
        penX += g.advance;
    }
    WriteTGA(outPath, canvasW, canvasH, canvas);
}

static void TestOneFNT(const std::string& path, const std::string& tag) {
    ASSERT_TRUE(fs::exists(path), tag + ": file exists (" + path + ")");
    if (!fs::exists(path)) return;

    FntFont font = FNT_Parse(path);
    ASSERT_TRUE(font.valid, tag + ": parsed successfully");
    if (!font.valid) return;

    ASSERT_TRUE(font.lineHeight > 0, tag + ": positive line height");
    ASSERT_TRUE(font.texWidth > 0 && font.texHeight > 0, tag + ": non-empty atlas");

    // Printable ASCII coverage and metric sanity.
    int covered = 0, badAdvance = 0;
    for (int c = 0x20; c <= 0x7e; ++c) {
        auto it = font.glyphs.find(c);
        if (it == font.glyphs.end()) continue;
        ++covered;
        const FntGlyph& g = it->second;
        // Advance is derived as a tight, proportional width+1 (see fnt_parser).
        // A constant advance across glyphs of differing widths is the "excessive
        // spacing" bug; this band enforces proportional spacing.
        if (c != 0x20 && g.width > 0) {
            if (g.advance != g.width + 1) ++badAdvance;
        }
        // UVs in range.
        ASSERT_TRUE(g.u0 >= 0.0f && g.u1 <= 1.001f && g.v0 >= 0.0f && g.v1 <= 1.001f,
                    tag + ": glyph '" + std::string(1, (char)c) + "' UVs in range");
    }
    ASSERT_TRUE(covered >= 90, tag + ": covers printable ASCII (" + std::to_string(covered) + "/95)");
    ASSERT_TRUE(badAdvance == 0, tag + ": all advances within [width, width+4] (" +
                std::to_string(badAdvance) + " out of band)");

    // Dump a few representative glyph metrics for eyeballing.
    std::cout << "  [INFO] " << tag << " metrics (char: w/h/adv):";
    for (char c : std::string("AWMilg")) {
        auto it = font.glyphs.find((int)c);
        if (it != font.glyphs.end())
            std::cout << " " << c << ":" << it->second.width << "/" << it->second.height
                      << "/" << it->second.advance;
    }
    std::cout << "\n";

    // Emit atlas + a rendered sample line for visual inspection.
    fs::create_directories("scratch\\cpp_tests\\fnt");
    WriteTGA("scratch\\cpp_tests\\fnt\\" + tag + "_atlas.tga", font.texWidth, font.texHeight, font.rgba);
    RenderSampleToTGA(font, "The quick brown fox 0123456789",
                      "scratch\\cpp_tests\\fnt\\" + tag + "_sample.tga");
    std::cout << "  [INFO] Wrote scratch\\cpp_tests\\fnt\\" << tag << "_atlas.tga and "
              << tag << "_sample.tga\n";
}

static void TestFNT() {
    TestOneFNT("assets\\editor\\qed\\editor.fnt", "editor");
    TestOneFNT("assets\\editor\\qed\\editorsm.fnt", "editorsm");

    FntFont missing = FNT_Parse("scratch\\cpp_tests\\nope.fnt");
    ASSERT_FALSE(missing.valid, "Nonexistent FNT file should be invalid");
}

int RunAllTests() {
    std::cout << "\n==================================================\n";
    std::cout << "          IGI Editor Native C++ Unit Tests         \n";
    std::cout << "==================================================\n\n";

    g_testCount = 0;
    g_failCount = 0;

    // Load config dynamically
    Config::Init();
    std::cout << "[CONFIG] Loaded dynamic IGIGamePath: " << Utils::GetIGIRootPath() << "\n";

    RUN_SUB_TEST("RES Archive Parser Tests", TestRES);
    RUN_SUB_TEST("MTP Animation Parser Tests", TestMTP);
    RUN_SUB_TEST("TEX Texture Parser Tests", TestTEX);
    RUN_SUB_TEST("QVM Bytecode Parser & Decompiler Tests", TestQVM);
    RUN_SUB_TEST("Level Resource Extractor Tests", TestExtractLevel);
    RUN_SUB_TEST("Level load end-to-end (levels 1-3) Verification", TestLevelLoad);
    RUN_SUB_TEST("FNT Bitmap Font Parser Tests", TestFNT);

    std::cout << "\n==================================================\n";
    std::cout << "Test Suite Results Summary:\n";
    std::cout << "  Total Assertions Checked: " << g_testCount << "\n";
    std::cout << "  Passed Assertions       : " << (g_testCount - g_failCount) << "\n";
    std::cout << "  Failed Assertions       : " << g_failCount << "\n";
    std::cout << "==================================================\n";

    if (g_failCount > 0) {
        std::cout << "  [RESULT] FAILED - some assertions did not pass.\n\n";
        return 1;
    }
    std::cout << "  [RESULT] ALL PASSED SUCCESSFULLY!\n\n";
    return 0;
}
