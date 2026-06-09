#include <gtest/gtest.h>
#include "parsers/mtp_parser.h"
#include "utils.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <map>
#include <vector>
#include <cstdint>

static std::string SrcMtp() {
    return Utils::GetIGIRootPath() + "\\missions\\location0\\level1\\level1.mtp";
}

class MtpWriterTest : public ::testing::Test {
protected:
    std::string tmpIn, tmpOut;
    void SetUp() override {
        // Work on a temp copy so we never touch the real game file.
        auto dir = std::filesystem::temp_directory_path();
        tmpIn  = (dir / "igi_mtp_in.mtp").string();
        tmpOut = (dir / "igi_mtp_out.mtp").string();
        std::error_code ec;
        std::filesystem::copy_file(SrcMtp(), tmpIn,
            std::filesystem::copy_options::overwrite_existing, ec);
    }
    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove(tmpIn, ec);
        std::filesystem::remove(tmpOut, ec);
    }
};

TEST_F(MtpWriterTest, AddsModelAndTexturesAndPreservesExisting) {
    if (!std::filesystem::exists(tmpIn)) GTEST_SKIP() << "level1.mtp not available";
    MTPFile before = MTP_Parse(tmpIn);
    ASSERT_TRUE(before.valid);

    std::string err;
    bool ok = MTP_AddModel(tmpIn, tmpOut, "999_77_1", {"019_03_1", "abc_xy_9"}, err);
    ASSERT_TRUE(ok) << err;

    MTPFile after = MTP_Parse(tmpOut);
    ASSERT_TRUE(after.valid) << after.error;
    // Model appended once.
    EXPECT_EQ(after.models.size(), before.models.size() + 1);
    EXPECT_EQ(after.models.back(), "999_77_1");
    // Existing model order preserved.
    for (size_t i = 0; i < before.models.size(); ++i)
        EXPECT_EQ(after.models[i], before.models[i]);
    // The already-present texture "019_03_1" is NOT duplicated; the new one IS added.
    EXPECT_EQ(after.textures.size(), before.textures.size() + 1); // only abc_xy_9 is new
    EXPECT_NE(std::find(after.textures.begin(), after.textures.end(), "abc_xy_9"), after.textures.end());
    // A mapping for the new model exists with both textures resolved.
    EXPECT_EQ(after.mappings.size(), before.mappings.size() + 1);
    bool found = false;
    for (auto& m : after.mappings)
        if (m.modelName == "999_77_1") {
            found = true;
            EXPECT_EQ(m.textureNames.size(), 2u);
            EXPECT_NE(std::find(m.textureNames.begin(), m.textureNames.end(), "019_03_1"), m.textureNames.end());
            EXPECT_NE(std::find(m.textureNames.begin(), m.textureNames.end(), "abc_xy_9"), m.textureNames.end());
        }
    EXPECT_TRUE(found);
}

TEST_F(MtpWriterTest, PreservesUntouchedChunksByteForByte) {
    if (!std::filesystem::exists(tmpIn)) GTEST_SKIP() << "level1.mtp not available";
    std::string err;
    ASSERT_TRUE(MTP_AddModel(tmpIn, tmpOut, "999_77_1", {"019_03_1", "abc_xy_9"}, err)) << err;

    auto readChunks = [](const std::string& path) {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        std::vector<uint8_t> b((size_t)f.tellg());
        f.seekg(0); f.read((char*)b.data(), b.size());
        std::map<std::string, std::vector<uint8_t>> chunks;
        size_t off = 12; // FORM + size + "MTP "
        while (off + 8 <= b.size()) {
            std::string cc((char*)&b[off], 4);
            uint32_t sz = ((uint32_t)b[off+4]<<24)|((uint32_t)b[off+5]<<16)|((uint32_t)b[off+6]<<8)|b[off+7];
            off += 8;
            if (off + sz > b.size()) break;
            chunks[cc] = std::vector<uint8_t>(b.begin()+off, b.begin()+off+sz);
            off += sz; if (off % 2) off++;
        }
        return chunks;
    };
    auto a = readChunks(tmpIn);
    auto c = readChunks(tmpOut);
    for (auto& kv : a) {
        const std::string& cc = kv.first;
        if (cc == "MODS" || cc == "TEXF" || cc == "INST") continue;
        ASSERT_TRUE(c.count(cc)) << "chunk lost: " << cc;
        EXPECT_EQ(c[cc], kv.second) << "chunk changed: " << cc;
    }
}

TEST_F(MtpWriterTest, IdempotentWhenModelAlreadyPresent) {
    if (!std::filesystem::exists(tmpIn)) GTEST_SKIP() << "level1.mtp not available";
    MTPFile before = MTP_Parse(tmpIn);
    ASSERT_TRUE(before.valid);
    ASSERT_FALSE(before.models.empty());
    std::string existing = before.models.front();
    std::string err;
    bool ok = MTP_AddModel(tmpIn, tmpOut, existing, {"019_03_1"}, err);
    ASSERT_TRUE(ok) << err;
    MTPFile after = MTP_Parse(tmpOut);
    ASSERT_TRUE(after.valid);
    EXPECT_EQ(after.models.size(), before.models.size());      // unchanged
    EXPECT_EQ(after.mappings.size(), before.mappings.size());  // unchanged
}
