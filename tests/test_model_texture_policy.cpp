/******************************************************************************
 * @file    test_model_texture_policy.cpp
 * @brief   Texture-id policy of ApplyTexturesToMesh: the AI opaque-body
 *          preference and the model-name-as-texture-id guess.
 *
 * IGI-RE is the oracle for both rules. A model's texture list IS its own MTP
 * INST record's GTT list, and the engine loads exactly the ids that list names:
 *
 *   src/model/model.c:271-314  ModelFile_Setup, HOOK(0x4CF080):
 *                              inst = MTP_INST; per record `n` = *inst++;
 *                              l->textures[i] = gtt-indexed TEXF entry
 *   src/model/model.c:452-470  ModelFile_LoadTextures, HOOK(0x4CF790):
 *                              sprintf("LOCAL:textures/%s.tex", s) per TEXF name
 *   src/render/render.c:216-219 lod_texture(l,i) => l->textures[i]
 *   src/render/render.c:524-531 draw_group: if (g->texture != 0xFFFF)
 *                                  SetTexture(dev, 0, lod_texture(ml, g->texture));
 *                              else SetTexture(dev, 0, NULL) + COLOROP=SELECTARG2
 *
 * Two consequences the editor used to get wrong, and which this suite pins:
 *
 *  1. An INST record with n == 0 is a model that HAS no textures, not a model
 *     whose texture list is unknown. Retail "waypoint" (every level's marker
 *     model, placed by AlarmControl / SCameraControl / Terminal) is exactly
 *     that: MTP INST nTex=0 and its only REND group carries 0xFFFF. The engine
 *     binds nothing; the editor guessed the MODEL NAME was a texture id and
 *     reported "[TEX] Texture NOT FOUND: waypoint".
 *  2. The engine never strips a pixel-format suffix, so the editor's opaque-body
 *     preference for AI characters may only be taken when the opaque id exists.
 *     Retail 015_01_1 (AITYPE_ANYA) names "000_11_1_argb8888" (which exists)
 *     while "000_11_1" exists nowhere in the install.
 *
 * The unit cases are host-native and need no install. The retail cases read a
 * real tree through IGI_GAME_PATH / IGI_TEX_ROOT / IGI_MEF_CORPUS and self-skip
 * otherwise. The MTP/DAT reading cases are guarded to _WIN32 because DAT_Parse
 * and MTP_Parse log through Logger, and Logger::Log is the only reference to
 * Config::Get() — whose translation unit does not compile on the host, so the
 * host lane cannot link them. The archive-level retail cases below are
 * host-portable and do run in the host lane.
 *****************************************************************************/
#include "../source/renderer/model_texture_resolution.h"
#include "../source/renderer/res_chunks.h"
#include "../source/renderer/res_writer.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#include "../source/level/mtp_writer.h"
#include "../source/renderer/dat_writer.h"
#endif

namespace {

// Records the ids a policy helper asks about, so the tests can prove the
// resolver is consulted exactly when the rule says it is. Always pass a lambda
// that captures this by reference: the helper takes a const std::function&, and
// handing it the struct itself would copy it and lose the recording.
struct Probe {
    bool answer = false;
    std::vector<std::string> asked;

    bool operator()(const std::string& id) {
        asked.push_back(id);
        return answer;
    }
};

const char* InstallRoot() {
    for (const char* name : {"IGI_GAME_PATH", "IGI_TEX_ROOT", "IGI_MEF_CORPUS"}) {
        const char* root = std::getenv(name);
        if (root != nullptr && *root != '\0') return root;
    }
    return nullptr;
}

std::string LevelDir(const std::string& root, int level) {
    return root + "/missions/location0/level" + std::to_string(level);
}

// A texture id resolves the way the editor's loader would: as the engine's own
// resource identity "LOCAL:textures/<id>.tex" inside any indexed archive
// (Resource_IsLoaded compares the folded full name, src/res/resource.c:124-147),
// or as a loose .tex in a level's texture directory.
class RetailTextureSearch {
public:
    explicit RetailTextureSearch(const std::string& root) : root_(root) {
        std::vector<std::string> archives;
        for (int level = 1; level <= 14; ++level) {
            const std::string dir = LevelDir(root_, level);
            archives.push_back(dir + "/textures/level" + std::to_string(level) + ".res");
            looseDirs_.push_back(dir + "/textures");
        }
        archives.push_back(root_ + "/missions/location0/common/textures/location0.res");
        looseDirs_.push_back(root_ + "/missions/location0/common/textures");

        for (const auto& archive : archives) {
            if (!std::filesystem::exists(archive)) continue;
            std::unordered_map<std::string, ResEntryInfo> index;
            std::string error;
            if (!RES_BuildIndex(archive, index, error)) continue;
            indexes_.push_back(std::move(index));
        }
    }

    std::size_t archiveCount() const { return indexes_.size(); }

    bool resolves(const std::string& textureId) const {
        const std::string member =
            igi_res::NormalizeResourceName(igi_res::TextureMemberName(textureId));
        for (const auto& index : indexes_) {
            if (index.find(member) != index.end()) return true;
        }
        for (const auto& dir : looseDirs_) {
            if (std::filesystem::exists(dir + "/" + textureId + ".tex")) return true;
        }
        return false;
    }

private:
    std::string root_;
    std::vector<std::unordered_map<std::string, ResEntryInfo>> indexes_;
    std::vector<std::string> looseDirs_;
};

// True when the engine's own model identity "LOCAL:models/<name>.mef" is a
// member of the level's model archive (Mesh_Load, src/model/model.c:364).
bool ModelIsInArchive(const std::string& resPath, const std::string& modelName) {
    if (!std::filesystem::exists(resPath)) return false;
    std::unordered_map<std::string, ResEntryInfo> index;
    std::string error;
    if (!RES_BuildIndex(resPath, index, error)) return false;
    const std::string member =
        igi_res::NormalizeResourceName(igi_res::ModelMemberName(modelName));
    return index.find(member) != index.end();
}

#if defined(_WIN32)
const MTPModelTexture* FindMapping(const MTPFile& mtp, const std::string& modelName) {
    for (const auto& mapping : mtp.mappings) {
        if (mapping.modelName == modelName) return &mapping;
    }
    return nullptr;
}

const DATModelEntry* FindDatModel(const DATFile& dat, const std::string& modelName) {
    for (const auto& model : dat.models) {
        if (model.modelName == modelName) return &model;
    }
    return nullptr;
}
#endif

}  // namespace

// ── the AI opaque-body preference ─────────────────────────────────────────────

// Retail 015_01_1: the engine's id is "000_11_1_argb8888" and the opaque
// "000_11_1" exists nowhere, so the suffixed id is the one that must be loaded.
TEST(ModelTexturePolicy, AiOpaquePreferenceKeepsSuffixedIdWhenOpaqueIsMissing) {
    Probe probe;
    probe.answer = false;
    const std::string id = ResolveAiModelTextureId(
        "000_11_1_argb8888", true,
        [&probe](const std::string& id) { return probe(id); });
    EXPECT_EQ(id, "000_11_1_argb8888");
    ASSERT_EQ(probe.asked.size(), 1u);
    EXPECT_EQ(probe.asked[0], "000_11_1");
}

// Retail 009_09_1 (the sunglasses overlay of 009_01_1 / 014_01_1 / 014_02_1):
// the opaque body texture does exist, so the editor's preference still applies.
TEST(ModelTexturePolicy, AiOpaquePreferenceTakesOpaqueWhenItExists) {
    Probe probe;
    probe.answer = true;
    const std::string id = ResolveAiModelTextureId(
        "009_09_1_argb8888", true,
        [&probe](const std::string& id) { return probe(id); });
    EXPECT_EQ(id, "009_09_1");
    ASSERT_EQ(probe.asked.size(), 1u);
    EXPECT_EQ(probe.asked[0], "009_09_1");
}

// Non-AI models (guard tower, lattice, fences) need the exact suffixed texture
// for transparent rendering: the preference must not touch them at all.
TEST(ModelTexturePolicy, AiOpaquePreferenceNotAppliedToNonAiModels) {
    Probe probe;
    probe.answer = true;
    EXPECT_EQ(ResolveAiModelTextureId(
                  "000_11_1_argb8888", false,
                  [&probe](const std::string& id) { return probe(id); }),
              "000_11_1_argb8888");
    EXPECT_TRUE(probe.asked.empty());
}

// Only alpha-bearing formats are "the overlay". The opaque formats the strip
// list also knows (_rgb565, _r5g6b5) are not, and an id with no suffix is
// already the engine's own name.
TEST(ModelTexturePolicy, AiOpaquePreferenceIgnoresOpaqueAndUnsuffixedIds) {
    Probe probe;
    probe.answer = true;
    auto resolver = [&probe](const std::string& id) { return probe(id); };
    EXPECT_EQ(ResolveAiModelTextureId("001_05_1_rgb565", true, resolver), "001_05_1_rgb565");
    EXPECT_EQ(ResolveAiModelTextureId("001_05_1_r5g6b5", true, resolver), "001_05_1_r5g6b5");
    EXPECT_EQ(ResolveAiModelTextureId("015_01_1", true, resolver), "015_01_1");
    EXPECT_TRUE(probe.asked.empty());
}

TEST(ModelTexturePolicy, AlphaSuffixClassification) {
    EXPECT_TRUE(TextureIdHasAlphaSuffix("009_09_1_argb8888"));
    EXPECT_TRUE(TextureIdHasAlphaSuffix("x_argb1555"));
    EXPECT_TRUE(TextureIdHasAlphaSuffix("x_argb4444"));
    EXPECT_TRUE(TextureIdHasAlphaSuffix("x_a8r8g8b8"));
    EXPECT_TRUE(TextureIdHasAlphaSuffix("x_a1r5g5b5"));
    EXPECT_TRUE(TextureIdHasAlphaSuffix("x_a4r4g4b4"));
    // The suffix test is case-insensitive, like StripTextureFormatSuffix.
    EXPECT_TRUE(TextureIdHasAlphaSuffix("X_ARGB8888"));

    EXPECT_FALSE(TextureIdHasAlphaSuffix("x_rgb565"));
    EXPECT_FALSE(TextureIdHasAlphaSuffix("x_r5g6b5"));
    EXPECT_FALSE(TextureIdHasAlphaSuffix("015_01_1"));
    EXPECT_FALSE(TextureIdHasAlphaSuffix("_argb8888"));
}

// ── the model-name-as-texture-id guess ───────────────────────────────────────

// The guess exists for a model whose texture list the editor could not find. A
// DAT entry — including an empty one — IS that list (ModelFile_Setup,
// HOOK 0x4CF080, src/model/model.c:271-314), so the guess must stand down.
TEST(ModelTexturePolicy, ModelNameGuessAllowedOnlyWhenDatDidNotAnswer) {
    EXPECT_FALSE(ModelNameIsTextureIdFallbackAllowed(true));
    EXPECT_TRUE(ModelNameIsTextureIdFallbackAllowed(false));
}

// ── retail evidence (host-portable: archives only) ───────────────────────────

// "waypoint" is a real model in every level's model archive, so it is a model
// with no textures — not a model the level cannot supply.
TEST(ModelTexturePolicyRetail, WaypointModelIsInEveryProbedLevelArchive) {
    const char* root = InstallRoot();
    if (root == nullptr) {
        GTEST_SKIP() << "set IGI_GAME_PATH to an IGI install to run the retail cases";
    }
    const std::string igi = root;
    std::size_t probed = 0;
    for (int level : {1, 12}) {
        const std::string res = LevelDir(igi, level) + "/MODELS/LEVEL" +
                                std::to_string(level) + ".RES";
        if (!std::filesystem::exists(res)) continue;
        ++probed;
        EXPECT_TRUE(ModelIsInArchive(res, "waypoint"))
            << "LOCAL:models/waypoint.mef is not in " << res;
    }
    if (probed == 0) {
        GTEST_SKIP() << "no retail model archives under " << igi;
    }
}

// The id the engine names for AITYPE_ANYA's 8th texture exists, and the id the
// editor used to derive from it exists nowhere in the install.
TEST(ModelTexturePolicyRetail, AnyaOpaqueIdIsTheOneThatDoesNotExist) {
    const char* root = InstallRoot();
    if (root == nullptr) {
        GTEST_SKIP() << "set IGI_GAME_PATH to an IGI install to run the retail cases";
    }
    const std::string igi = root;
    const RetailTextureSearch search(igi);
    if (search.archiveCount() == 0) {
        GTEST_SKIP() << "no readable retail texture archives under " << igi;
    }

    EXPECT_TRUE(search.resolves("000_11_1_argb8888"))
        << "the engine's own id does not resolve; the install is incomplete";
    EXPECT_FALSE(search.resolves("000_11_1"))
        << "000_11_1 unexpectedly resolves; the premise of this defect changed";

    // What the draw path must therefore load for 015_01_1.
    EXPECT_EQ(ResolveAiModelTextureId("000_11_1_argb8888", /*isAiModel=*/true,
                                      [&search](const std::string& id) {
                                          return search.resolves(id);
                                      }),
              "000_11_1_argb8888");

    // The other alpha id in the install keeps its opaque preference, which is why
    // the rule is "prefer the opaque id when it exists", not "never strip".
    ASSERT_TRUE(search.resolves("009_09_1"))
        << "009_09_1 should resolve; the opaque preference has no retail case left";
    EXPECT_EQ(ResolveAiModelTextureId("009_09_1_argb8888", /*isAiModel=*/true,
                                      [&search](const std::string& id) {
                                          return search.resolves(id);
                                      }),
              "009_09_1");
}

// ── retail evidence (Windows suite: MTP/DAT parsing) ─────────────────────────
#if defined(_WIN32)
// "waypoint" is a model with no textures: the engine's own MTP INST record for
// it has n == 0, and the level DAT — the editor's transcription of exactly those
// records — agrees.
TEST(ModelTexturePolicyRetail, WaypointIsAModelWithNoTextures) {
    const char* root = InstallRoot();
    if (root == nullptr) {
        GTEST_SKIP() << "set IGI_GAME_PATH to an IGI install to run the retail cases";
    }
    const std::string igi = root;
    const std::string mtpPath = LevelDir(igi, 1) + "/level1.mtp";
    const std::string datPath = LevelDir(igi, 1) + "/level1.dat";
    if (!std::filesystem::exists(mtpPath) || !std::filesystem::exists(datPath)) {
        GTEST_SKIP() << "no retail level-1 MTP/DAT under " << igi;
    }

    const MTPFile mtp = MTP_Parse(mtpPath);
    ASSERT_TRUE(mtp.valid) << mtp.error;
    const MTPModelTexture* mapping = FindMapping(mtp, "waypoint");
    ASSERT_NE(mapping, nullptr) << "waypoint is not an INST record of " << mtpPath;
    EXPECT_TRUE(mapping->textureNames.empty())
        << "engine MTP gives waypoint " << mapping->textureNames.size() << " textures";

    const DATFile dat = DAT_Parse(datPath);
    ASSERT_TRUE(dat.valid) << dat.error;
    const DATModelEntry* entry = FindDatModel(dat, "waypoint");
    ASSERT_NE(entry, nullptr) << "waypoint has no DAT entry";
    EXPECT_TRUE(entry->textures.empty());
}

// AITYPE_ANYA's texture list, in the engine's own order, ends with the suffixed
// id — and the DAT order the editor loads from is that same list.
TEST(ModelTexturePolicyRetail, AnyaTextureListMatchesTheEngineOrder) {
    const char* root = InstallRoot();
    if (root == nullptr) {
        GTEST_SKIP() << "set IGI_GAME_PATH to an IGI install to run the retail cases";
    }
    const std::string igi = root;
    const std::string mtpPath = LevelDir(igi, 1) + "/level1.mtp";
    const std::string datPath = LevelDir(igi, 1) + "/level1.dat";
    if (!std::filesystem::exists(mtpPath) || !std::filesystem::exists(datPath)) {
        GTEST_SKIP() << "no retail level-1 MTP/DAT under " << igi;
    }

    const MTPFile mtp = MTP_Parse(mtpPath);
    ASSERT_TRUE(mtp.valid) << mtp.error;
    const MTPModelTexture* mapping = FindMapping(mtp, "015_01_1");
    ASSERT_NE(mapping, nullptr) << "015_01_1 is not an INST record of " << mtpPath;
    ASSERT_EQ(mapping->textureNames.size(), 8u);
    EXPECT_EQ(mapping->textureNames.back(), "000_11_1_argb8888");

    // 015_01_1.mef render group 7 addresses slot 7, so this order decides the id
    // that group is drawn with.
    const DATFile dat = DAT_Parse(datPath);
    ASSERT_TRUE(dat.valid) << dat.error;
    const DATModelEntry* entry = FindDatModel(dat, "015_01_1");
    ASSERT_NE(entry, nullptr) << "015_01_1 has no DAT entry";
    EXPECT_EQ(entry->textures, mapping->textureNames);
}
#endif  // _WIN32
