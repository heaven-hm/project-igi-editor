// Corpus smoke suite for the binary MEF parser.
//
// Walks a real IGI install and asserts the structural invariants the renderer
// relies on, so a future change to the DNER walk (or to any other parser in
// mef_native.cpp) cannot silently produce blocks bound to the wrong geometry.
//
// Enabled by pointing IGI_MEF_CORPUS at the install root, e.g.
//   IGI_MEF_CORPUS=/path/to/IGI <test binary>
// Without it the suite skips, so the cross-build and the host lane stay portable.

#include <gtest/gtest.h>
#include "renderer/mef_native.h"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

const char* CorpusRoot() {
    // IGI_GAME_PATH is what scripts/run-host-tests.sh exports for the detected
    // install, so the corpus suite runs in the standard gate without extra setup.
    for (const char* name : {"IGI_MEF_CORPUS", "IGI_GAME_PATH"}) {
        const char* p = std::getenv(name);
        if (p != nullptr && *p != '\0') return p;
    }
    return nullptr;
}

std::vector<std::string> FindMefFiles(const fs::path& root) {
    std::vector<std::string> out;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        std::string ext = it->path().extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".mef") out.push_back(it->path().string());
    }
    return out;
}

} // namespace

// Every retail .mef must parse, and every triangle/block range it reports must be
// addressable inside the vertex and triangle arrays the renderer is handed.
TEST(MefCorpusTest, EveryRetailModelParsesWithAddressableRanges) {
    const char* root = CorpusRoot();
    if (root == nullptr || !fs::exists(root)) {
        GTEST_SKIP() << "Set IGI_MEF_CORPUS to an IGI install root to enable this suite";
    }

    const std::vector<std::string> files = FindMefFiles(root);
    ASSERT_FALSE(files.empty()) << "no .mef files under " << root;

    size_t typeCounts[4] = {0, 0, 0, 0};
    size_t badType = 0, badBlockRange = 0, badTriangleIndex = 0;
    size_t blocksWithoutTriangles = 0, emptyBlocksWithTriangles = 0;
    size_t type1CountMismatch = 0;
    std::string firstBad;

    for (const std::string& path : files) {
        ParsedGeometry geo;
        ASSERT_NO_THROW(geo = ParseMefFile(path)) << path;

        if (geo.modelType > 3) { ++badType; if (firstBad.empty()) firstBad = "type " + path; continue; }
        ++typeCounts[geo.modelType];

        // Format 1 splits XTRV into base vertices + extra influences; the two runs
        // must exactly cover the vertex array (BoneMesh_Skin output VB size is the
        // base count, extras accumulate into it).
        if (geo.modelType == 1 &&
            geo.baseVertexCount + geo.extraInfluenceCount != geo.vertices.size()) {
            ++type1CountMismatch;
            if (firstBad.empty()) firstBad = "type1 counts " + path;
        }

        for (const auto& block : geo.renderBlocks) {
            if (block.triangleStart + block.triangleCount > geo.triangles.size()) {
                ++badBlockRange;
                if (firstBad.empty()) firstBad = "block range " + path;
                break;
            }
        }

        for (const auto& tri : geo.triangles) {
            if (tri[0] >= geo.vertices.size() || tri[1] >= geo.vertices.size() ||
                tri[2] >= geo.vertices.size()) {
                ++badTriangleIndex;
                if (firstBad.empty()) firstBad = "triangle index " + path;
                break;
            }
        }

        if (!geo.renderBlocks.empty() && geo.triangles.empty()) ++blocksWithoutTriangles;
        // "collision fallback" is a legitimate layout: 605_08_1, 605_09_1, 610_03_1,
        // 610_04_1, 700_08_1 and 700_19_1 ship an EMPTY DNER chunk (size 0) and no
        // XTRV, so their only geometry is the collision mesh. Any other layout that
        // produced triangles must also have produced render blocks.
        if (geo.renderBlocks.empty() && !geo.triangles.empty() &&
            geo.renderLayout != "collision fallback") {
            ++emptyBlocksWithTriangles;
            if (firstBad.empty()) firstBad = "triangles without blocks " + path;
        }
    }

    EXPECT_EQ(badType, 0u);
    EXPECT_EQ(badBlockRange, 0u) << firstBad;
    EXPECT_EQ(badTriangleIndex, 0u) << firstBad;
    EXPECT_EQ(type1CountMismatch, 0u) << firstBad;
    EXPECT_EQ(blocksWithoutTriangles, 0u) << firstBad;
    EXPECT_EQ(emptyBlocksWithTriangles, 0u) << firstBad;

    // A real install exercises all three mesh formats.
    EXPECT_GT(typeCounts[0], 0u) << "no format-0 (rigid) models found";
    EXPECT_GT(typeCounts[1], 0u) << "no format-1 (skinned) models found";
    EXPECT_GT(typeCounts[3], 0u) << "no format-3 (lightmapped) models found";

    std::printf("[corpus] %zu .mef parsed: format0=%zu format1=%zu format3=%zu\n",
                files.size(), typeCounts[0], typeCounts[1], typeCounts[3]);
}
