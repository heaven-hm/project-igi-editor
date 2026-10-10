#include <gtest/gtest.h>
#include "../source/level/mtp_writer.h"
#include "utils.h"
#include <string>
#include <filesystem>
#include <algorithm>
#include <cctype>

// ============================================================
//  MTP Parser — first .mtp found under game root
// ============================================================

static std::string FindFirstFile(const std::string& root, const std::string& ext) {
    namespace fs = std::filesystem;
    std::error_code ec;
    for (auto& e : fs::recursive_directory_iterator(root, ec)) {
        if (ec) { ec.clear(); continue; }
        if (!e.is_regular_file(ec)) { ec.clear(); continue; }
        std::string fext = e.path().extension().string();
        std::transform(fext.begin(), fext.end(), fext.begin(),
                       [](unsigned char c){ return (char)std::tolower(c); });
        if (fext == ext) return e.path().string();
    }
    return "";
}

class MtpParserTest : public ::testing::Test {
protected:
    MTPFile mtp;
    std::string mtpPath;
    void SetUp() override {
        mtpPath = FindFirstFile(Utils::GetIGIRootPath(), ".mtp");
        ASSERT_FALSE(mtpPath.empty())
            << "No .mtp file found under: " << Utils::GetIGIRootPath();
        mtp = MTP_Parse(mtpPath);
    }
};

TEST_F(MtpParserTest, ParsesValid) {
    ASSERT_TRUE(mtp.valid) << "MTP parse failed: " << mtp.error
                           << "\nPath: " << mtpPath;
}

TEST_F(MtpParserTest, HasAtLeastOneModelOrTexture) {
    bool hasContent = !mtp.models.empty() || !mtp.textures.empty() || !mtp.mappings.empty();
    EXPECT_TRUE(hasContent) << "MTP file appears empty: " << mtpPath;
}
