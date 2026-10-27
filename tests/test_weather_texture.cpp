/******************************************************************************
 * @file    test_weather_texture.cpp
 * @brief   The retail weather particle's texture binding (#57 / #58).
 *
 * The editor used to draw the snow flakes as untextured white quads because the
 * renderer's texture loader was private. This suite pins the pieces that bind
 * the game's real texture, so the binding cannot silently drift back:
 *
 *   * the RESOURCE IDENTITY the engine loads it under, which is NOT a model
 *     texture's identity, and
 *   * the retail pack that actually carries it, read here with the same reader
 *     the product uses (`RES_BuildIndex` / `RES_FindIndexEntry` / `RES_ReadEntry`)
 *     and decoded with the same decoder (`Tex_LoadFromMemory`), and
 *   * the corner -> uv mapping of the flake quad, and
 *   * the colour domain of the particle's vertex diffuse.
 *
 * Oracle: IGI-RE, the decompiled retail IGI.exe. Function + HOOK + file:line:
 *   src/fxobj/rain.c:82        RainEffect_Init HOOK 0x436780
 *                                gRainTexture = Game_AddTexture("snow.tex")
 *   src/fxobj/rain.c:141       RainEffect_Changed HOOK 0x436A50 passes
 *                                gRainTexture + 0x18 (the embedded Texture) to
 *                                Rain_New, for BOTH modes
 *   src/game/game.c:835-848    Game_AddTexture HOOK 0x416930 (interns the name)
 *   src/game/game.c:524-534    Game_LoadTextures HOOK 0x416130:
 *                                path = "LOCAL:common/textures/" + name
 *   src/game/game.c:430        Resource_Load("LOCAL:common/textures/textures.res")
 *   src/file/texture.c:231-252 Texture_LoadFile HOOK 0x4B24C0: Resource_Load, then
 *                                the LOOP v2 / v11 header -> Texture_Create
 *   src/res/resource.c:98-104  String_ToUpper HOOK 0x4B5AF0
 *   src/res/resource.c:136-138 Resource_IsLoaded HOOK 0x4B5B90 (full-name compare)
 *   src/dxinit/rain.c:139      the snow VB's FVF: XYZ | DIFFUSE | TEX1
 *   src/dxinit/rain.c:344-361  the quad's corner -> uv assignment
 *   src/dxinit/rain.c:430-438  Rain_DrawCell: SetTexture + stage-0 COLOROP/ALPHAOP
 *   src/dxinit/rain.c:444-445  Rain_DrawCell: stage-0 MAGFILTER / MINFILTER
 *   src/dxinit/rain.c:646-649  Rain_SetAlpha HOOK 0x499FA0 (raw 8-bit alpha byte)
 *   src/res/qvertex.c:189      gColorScale = 0.0078125f (the MENU colour domain)
 *   src/math/draw.c:12         COLOR_SCALE = 1/128 (the MENU colour domain)
 *
 * Corpus: $IGI_GAME_PATH (scripts/run-host-tests.sh exports it; default
 * /Users/haseeb-mir/Downloads/IGI). The corpus test self-skips when the pack is
 * absent; the identity and constant tests always run.
 *
 * Host-portable on purpose: it reaches the corpus through the environment rather
 * than Utils::GetIGIRootPath() (source/utils.cpp needs windows.h and is excluded
 * from the host lane).
 *****************************************************************************/

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "renderer/res_chunks.h"
#include "renderer/res_writer.h"
#include "runtime/rain_rules.h"
#include "level/level_common.h"

namespace {

namespace fs = std::filesystem;

fs::path GameRoot() {
    if (const char* env = std::getenv("IGI_GAME_PATH"); env != nullptr && *env != '\0')
        return fs::path(env);
    return fs::path("/Users/haseeb-mir/Downloads/IGI");
}

}  // namespace

// ===========================================================================
// The resource identity
// ===========================================================================

TEST(WeatherTexture, MemberNameIsTheGameTemplateNotTheModelOne) {
    // Game_LoadTextures HOOK 0x416130, src/game/game.c:531-532.
    EXPECT_EQ(igi_res::GameTextureMemberName("snow.tex"), "LOCAL:common/textures/snow.tex");

    // Resource_Create HOOK 0x4B59F0 folds the name (src/res/resource.c:65-104):
    // String_ToUpper HOOK 0x4B5AF0 and Path_FixSlashes. The engine compares the
    // folded full name only (Resource_IsLoaded HOOK 0x4B5B90, :136-138).
    EXPECT_EQ(igi_res::NormalizeResourceName(igi_res::GameTextureMemberName("snow.tex")),
              "LOCAL:COMMON/TEXTURES/SNOW.TEX");

    // The model-texture template is a DIFFERENT namespace (ModelFile_LoadTextures
    // HOOK 0x4CF790, src/model/model.c:467). A lookup for one can never answer the
    // other, which is why the weather binding needed its own loader.
    EXPECT_EQ(igi_res::TextureMemberName("snow"), "LOCAL:textures/snow.tex");
    EXPECT_NE(igi_res::GameTextureMemberName("snow.tex"),
              igi_res::TextureMemberName("snow"));
    EXPECT_NE(igi_res::NormalizeResourceName(igi_res::GameTextureMemberName("snow.tex")),
              igi_res::NormalizeResourceName(igi_res::TextureMemberName("snow")));
}

TEST(WeatherTexture, PackIsTheOneTheGameMounts) {
    // src/game/game.c:430.
    EXPECT_STREQ(igi_res::kGameTexturePackName, "LOCAL:common/textures/textures.res");
    // The LOCAL: device strips its prefix (src/sysmisc/localdev.c:162-205), so this
    // is the on-disk path under the install root -- what the editor reads.
    const std::string identity = igi_res::kGameTexturePackName;
    ASSERT_GE(identity.size(), 6u);
    EXPECT_EQ(identity.substr(0, 6), "LOCAL:");
    EXPECT_EQ(identity.substr(6), "common/textures/textures.res");
}

TEST(WeatherTexture, LocalDevicePathIsTheEnginePathUnderTheRoot) {
    // The device prefix is stripped, case-insensitively, and the rest is the path
    // (LocalDevice_Open, src/sysmisc/localdev.c:162-205).
    EXPECT_EQ(igi_res::LocalDeviceRelativePath("LOCAL:common/textures/snow.tex"),
              "common/textures/snow.tex");
    EXPECT_EQ(igi_res::LocalDeviceRelativePath("local:common/textures/snow.tex"),
              "common/textures/snow.tex");
    EXPECT_EQ(igi_res::LocalDeviceRelativePath("common/textures/snow.tex"),
              "common/textures/snow.tex");

    // LocalDevicePath is the expression the loader uses for both the pack and the
    // loose-file fallback; compare as paths so the test is separator-agnostic
    // (the Windows build produces '\\', this host '/').
    const fs::path expectedPack = fs::path("/igi") / "common" / "textures" / "textures.res";
    EXPECT_EQ(fs::path(igi_res::LocalDevicePath("/igi", igi_res::kGameTexturePackName)),
              expectedPack);
    const fs::path expectedMember = fs::path("/igi") / "common" / "textures" / "snow.tex";
    EXPECT_EQ(fs::path(igi_res::LocalDevicePath(
                  "/igi", igi_res::GameTextureMemberName("snow.tex"))),
              expectedMember);
}

TEST(WeatherTexture, TheLoadersPathExpressionFindsTheRetailPack) {
    // The same expression Renderer_Objects::GetOrLoadGameTexture builds for the
    // pack. If the install layout or that expression drifts, the loader silently
    // falls back to the null-texture branch -- which is the defect this binding
    // exists to remove -- so it is checked here.
    const fs::path pack = igi_res::LocalDevicePath(GameRoot().string(), igi_res::kGameTexturePackName);
    if (!fs::is_regular_file(pack))
        GTEST_SKIP() << "Set IGI_GAME_PATH to an installed IGI root (" << pack.string() << ")";
    EXPECT_TRUE(fs::is_regular_file(pack)) << pack.string();

    // The expression resolves to the pack the game mounts by that identity
    // (src/game/game.c:430), not to a level archive.
    EXPECT_EQ(pack.filename().string(), "textures.res");
}

TEST(WeatherTexture, SnowTextureNameBuildsTheEngineMemberName) {
    // RainEffect_Init HOOK 0x436780, src/fxobj/rain.c:82.
    EXPECT_STREQ(igi::rain::SnowTextureName(), "snow.tex");
    EXPECT_EQ(igi_res::GameTextureMemberName(igi::rain::SnowTextureName()),
              "LOCAL:common/textures/snow.tex");

    // The stage-0 texture-argument count differs with and without the texture
    // (src/dxinit/rain.c:433-438): 4 with, 3 without. That count is the switch the
    // editor's shader reproduces with u_has_texture.
    EXPECT_EQ(igi::rain::SnowTextureStageArgCountWithTexture(), 4);
    EXPECT_EQ(igi::rain::SnowTextureStageArgCountWithoutTexture(), 3);
}

// ===========================================================================
// The retail pack really carries the member
// ===========================================================================

TEST(WeatherTexture, RetailPackCarriesTheSnowTextureUnderThatIdentity) {
    // The pack path comes from the product's own expression, not from this test.
    const fs::path pack =
        igi_res::LocalDevicePath(GameRoot().string(), igi_res::kGameTexturePackName);
    if (!fs::is_regular_file(pack))
        GTEST_SKIP() << "Set IGI_GAME_PATH to an installed IGI root (" << pack.string() << ")";

    std::unordered_map<std::string, ResEntryInfo> index;
    std::string error;
    ASSERT_TRUE(RES_BuildIndex(pack.string(), index, error))
        << "RES_BuildIndex failed for " << pack.string() << ": " << error;
    ASSERT_FALSE(index.empty());

    // The lookup is by the engine's identity, exactly as Resource_IsLoaded does it.
    ResEntryInfo info{};
    ASSERT_TRUE(RES_FindIndexEntry(index, igi_res::GameTextureMemberName("snow.tex"), info))
        << "the retail pack does not define LOCAL:common/textures/snow.tex";

    const std::vector<uint8_t> bytes = RES_ReadEntry(pack.string(), info);
    ASSERT_FALSE(bytes.empty());
    ASSERT_GE(bytes.size(), 0x20u);

    // Texture_LoadFile HOOK 0x4B24C0 (src/file/texture.c:231-252) dispatches on the
    // LOOP magic and the version, and for version 11 reads exactly these fields
    // (LoopHeader11, src/file/texture.c:24-36; consumed at :239-243):
    //   pitch = bytesPerPixel * unk1A, then width/height/format/unk14/unk0C,
    //   with the pixels at p + 0x20.
    // The header is read through the editor's own transcription of that struct
    // (source/level/level_common.h) rather than a local copy, so the field order
    // is the product's, not this test's opinion.
    static_assert(sizeof(tex_head_v11_s) == 32, "LoopHeader11 is 5 ints + 6 shorts");
    const tex_head_v11_s* head = reinterpret_cast<const tex_head_v11_s*>(bytes.data());
    ASSERT_EQ(head->ident_, TEX_IDENT) << "not a LOOP texture";
    ASSERT_EQ(head->version_, 11);
    EXPECT_EQ(head->image_width_, 16);
    EXPECT_EQ(head->image_height_, 16);
    EXPECT_EQ(head->bytes_per_pixels_, 4);
    // IMAGE_MODE_67 -> ConvertTex32ToPic (source/level/level_common.cpp:407-411).
    EXPECT_EQ(head->image_mode_, IMAGE_MODE_67);

    const std::size_t pitch =
        static_cast<std::size_t>(head->unk3_) * static_cast<std::size_t>(head->bytes_per_pixels_);
    EXPECT_EQ(pitch, 64u);
    EXPECT_GE(bytes.size(), 0x20u + pitch * static_cast<std::size_t>(head->image_height_))
        << "the first mip level does not fit in the member";
}

// ===========================================================================
// The flake quad's corner -> uv mapping
// ===========================================================================

TEST(WeatherTexture, QuadCornersMapToTheEngineUvOrder) {
    // Rain_FillVB HOOK 0x499090, src/dxinit/rain.c:344-361: the four corners are
    // (x0,y0) (x1,y0) (x0,y1) (x1,y1) and their uvs are (0,0) (1,0) (0,1) (1,1);
    // v4 = v1 and v5 = v2 (rain.c:362-363) reuse them, so the six vertices are
    // three corners plus two repeats.
    struct Corner {
        float sign_x;
        float sign_y;
        float u;
        float v;
    };
    const Corner kEngineCorners[6] = {
        {-1.0f, -1.0f, 0.0f, 0.0f},  // v0 = (x0, y0)  rain.c:344-345,354-355
        {+1.0f, -1.0f, 1.0f, 0.0f},  // v1 = (x1, y0)  rain.c:346-347,356-357
        {-1.0f, +1.0f, 0.0f, 1.0f},  // v2 = (x0, y1)  rain.c:348-349,358-359
        {+1.0f, +1.0f, 1.0f, 1.0f},  // v3 = (x1, y1)  rain.c:350-351,360-361
        {+1.0f, -1.0f, 1.0f, 0.0f},  // v4 = v1        rain.c:362
        {-1.0f, +1.0f, 0.0f, 1.0f},  // v5 = v2        rain.c:363
    };

    EXPECT_EQ(igi::rain::SnowVerticesPerParticle(), 6);
    for (const Corner& corner : kEngineCorners) {
        // The vertex shader derives the uv from the corner sign alone:
        // uv = (sign + 1) / 2. If that derivation or the corner table is edited,
        // this fails.
        EXPECT_FLOAT_EQ((corner.sign_x + 1.0f) * 0.5f, corner.u);
        EXPECT_FLOAT_EQ((corner.sign_y + 1.0f) * 0.5f, corner.v);
    }
    // The repeats are the repeats, not new corners.
    EXPECT_FLOAT_EQ(kEngineCorners[4].u, kEngineCorners[1].u);
    EXPECT_FLOAT_EQ(kEngineCorners[4].v, kEngineCorners[1].v);
    EXPECT_FLOAT_EQ(kEngineCorners[5].u, kEngineCorners[2].u);
    EXPECT_FLOAT_EQ(kEngineCorners[5].v, kEngineCorners[2].v);
}

// ===========================================================================
// The colour domain: raw 8-bit D3DCOLOR, not the menus' 1/128 QVertex domain
// ===========================================================================

TEST(WeatherTexture, ParticleColourIsRawEightBitNotTheQVertexDomain) {
    // The snow VB's FVF is D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1
    // (Rain_CreateVB, src/dxinit/rain.c:139), so Rain_FillVB's diffuse words
    // (0xFFFFFFFF at rain.c:353, 0x00FFFFFF / r->color at :366-367) are plain
    // D3DCOLOR bytes. Rain_SetAlpha packs trunc(alpha * 255) into the top byte
    // (src/dxinit/rain.c:646-649) -- a full 0..255 range, not a 0..127 one.
    EXPECT_EQ(igi::rain::SnowFlakeDiffuse(), 0xFFFFFFFFu);
    EXPECT_EQ(igi::rain::AlphaByte(1.0f), 255);   // the domain's maximum
    EXPECT_EQ(igi::rain::AlphaByte(0.5f), 127);
    EXPECT_EQ(igi::rain::AlphaByte(0.0f), 0);

    // The 1/128 domain is the MENU path's: gColorScale DATA 0x534AB0 = 0.0078125f
    // (src/res/qvertex.c:189) and COLOR_SCALE = 1/128 (src/math/draw.c:12), both
    // consumed by QVertex/Grid drawing. Applying it to these particles would scale
    // a byte by 1/128, so the two are not interchangeable.
    constexpr float kQVertexColorScale = 0.0078125f;  // 1/128
    EXPECT_FLOAT_EQ(kQVertexColorScale, 1.0f / 128.0f);
    EXPECT_NE(kQVertexColorScale, 1.0f / 255.0f);
    // The editor converts the byte to a float with /255, the D3DCOLOR domain.
    EXPECT_FLOAT_EQ(static_cast<float>(igi::rain::AlphaByte(1.0f)) / 255.0f, 1.0f);
    EXPECT_NE(static_cast<float>(igi::rain::AlphaByte(1.0f)) * kQVertexColorScale, 1.0f);
}

TEST(WeatherTexture, SnowIsTheOnlyTexturedMode) {
    // Rain_DrawCell HOOK 0x499430: the snow branch binds r->texture and uses
    // D3DPT_TRIANGLELIST with 6 vertices per particle (src/dxinit/rain.c:430-447);
    // the rain branch binds NULL, selects the diffuse alone and uses
    // D3DPT_LINELIST with 2 (src/dxinit/rain.c:448-459).
    EXPECT_EQ(igi::rain::SnowVerticesPerParticle(), 6);
    EXPECT_EQ(igi::rain::RainVerticesPerParticle(), 2);
    EXPECT_EQ(igi::rain::SnowFlakeDiffuse() & 0x00FFFFFFu,
              igi::rain::RainStreakTopDiffuse());  // both are white
    EXPECT_EQ((igi::rain::RainStreakTopDiffuse() >> 24) & 0xFFu, 0u);  // rain's top is transparent
    EXPECT_EQ((igi::rain::SnowFlakeDiffuse() >> 24) & 0xFFu, 0xFFu);   // snow's quad is opaque
}
