#include <gtest/gtest.h>

#include "../source/renderer/res_writer.h"
#include "../source/renderer/res_compiler.h"
#include "../source/runtime/audio_asset_resolver.h"
#include "../source/runtime/audio_system.h"
#include "../source/weapon_system.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace {

class AudioAssetResolverTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_root_directory_ = std::filesystem::temp_directory_path() /
            "igi_audio_asset_resolver_test";
        std::error_code error_code;
        std::filesystem::remove_all(test_root_directory_, error_code);
        std::filesystem::create_directories(test_root_directory_, error_code);
        ASSERT_FALSE(error_code) << error_code.message();
    }

    void TearDown() override {
        std::error_code error_code;
        std::filesystem::remove_all(test_root_directory_, error_code);
    }

    std::filesystem::path WriteArchive(
        const std::filesystem::path& archive_path,
        const std::string& resource_name,
        const std::vector<uint8_t>& resource_data) {
        std::filesystem::create_directories(archive_path.parent_path());
        std::string error_message;
        EXPECT_TRUE(RES_WriteEntries(
            {RESEntry{resource_name, resource_data}},
            archive_path.string(),
            error_message)) << error_message;
        return archive_path;
    }

    std::vector<uint8_t> ReadFile(const std::filesystem::path& file_path) {
        std::ifstream input_stream(file_path, std::ios::binary);
        return std::vector<uint8_t>(
            std::istreambuf_iterator<char>(input_stream),
            std::istreambuf_iterator<char>());
    }

    std::filesystem::path test_root_directory_;
};

} // namespace

TEST_F(AudioAssetResolverTest, PrefersLooseLevelSoundBeforePackedCommonSound) {
    const std::filesystem::path loose_sound_path =
        test_root_directory_ / "missions/location0/level3/sounds/m16_loop.wav";
    std::filesystem::create_directories(loose_sound_path.parent_path());
    {
        std::ofstream output_stream(loose_sound_path, std::ios::binary);
        output_stream << "loose-level-sound";
    }

    WriteArchive(
        test_root_directory_ / "COMMON/SOUNDS/SOUNDS.RES",
        "LOCAL:common/sounds/m16_loop.wav",
        {0x01, 0x02, 0x03});

    igi::AudioAssetResolver resolver(
        test_root_directory_,
        test_root_directory_ / "cache");
    resolver.SetActiveLevel(3);

    EXPECT_EQ(resolver.ResolveWavPath("m16_loop"), loose_sound_path);
}

TEST_F(AudioAssetResolverTest, ExtractsCaseInsensitiveAuthoredSoundFromPackedArchive) {
    const std::vector<uint8_t> expected_sound_bytes = {0x52, 0x49, 0x46, 0x46, 0x11};
    WriteArchive(
        test_root_directory_ / "COMMON/SOUNDS/SOUNDS.RES",
        "LOCAL:common/sounds/m16_loop.wav",
        expected_sound_bytes);

    igi::AudioAssetResolver resolver(
        test_root_directory_,
        test_root_directory_ / "cache");

    const std::filesystem::path resolved_sound_path =
        resolver.ResolveWavPath("LOCAL:COMMON/SOUNDS/M16_LOOP.WAV");

    ASSERT_FALSE(resolved_sound_path.empty());
    EXPECT_NE(resolved_sound_path, test_root_directory_ / "COMMON/SOUNDS/SOUNDS.RES");
    EXPECT_EQ(ReadFile(resolved_sound_path), expected_sound_bytes);
    EXPECT_EQ(resolver.ResolveWavPath("m16_loop"), resolved_sound_path);
}

TEST_F(AudioAssetResolverTest, PrefersActiveLevelPackedSoundBeforeCommonPackedSound) {
    const std::vector<uint8_t> expected_level_sound_bytes = {0x10, 0x20};
    WriteArchive(
        test_root_directory_ / "missions/location0/level7/sounds/sounds.res",
        "LOCAL:level7/sounds/ak47_loop.wav",
        expected_level_sound_bytes);
    WriteArchive(
        test_root_directory_ / "COMMON/SOUNDS/SOUNDS.RES",
        "LOCAL:common/sounds/ak47_loop.wav",
        {0x30, 0x40});

    igi::AudioAssetResolver resolver(
        test_root_directory_,
        test_root_directory_ / "cache");
    resolver.SetActiveLevel(7);

    const std::filesystem::path resolved_sound_path =
        resolver.ResolveWavPath("ak47_loop");

    ASSERT_FALSE(resolved_sound_path.empty());
    EXPECT_EQ(ReadFile(resolved_sound_path), expected_level_sound_bytes);
}

TEST_F(AudioAssetResolverTest, ReturnsEmptyPathWhenSoundIsUnavailable) {
    igi::AudioAssetResolver resolver(
        test_root_directory_,
        test_root_directory_ / "cache");

    EXPECT_TRUE(resolver.ResolveWavPath("missing_weapon_sound").empty());
}

TEST(AudioSystemTest, ConditionalSoundChannelCanBeStoppedWithoutNativeAudio) {
    igi::AudioSystem::Initialize({});
    igi::AudioSystem::PlayConditionalSound(
        "conditional-test",
        "missing_conditional_sound",
        igi::SoundEffect::ObjectiveComplete);
    igi::AudioSystem::StopConditionalSound("conditional-test");
}

TEST(VanillaWeaponSoundCatalogTest, UsesSoundNamesPresentInVanillaWeaponQvms) {
    const std::vector<std::pair<uint32_t, std::string>> expected_sound_names = {
        {1, "glock_shot_1"},
        {3, "deagle_shot_1"},
        {4, "m16_loop"},
        {5, "ak47_loop"},
        {6, "uzi_loop"},
        {7, "mp5sd_loop"},
        {8, "spas12_shot_1"},
        {9, "jackh_loop"},
        {10, "minimi_loop"},
        {11, "svddrag_shot_1"},
        {12, "rpg_launch_1"},
        {13, "uzix2_loop"},
        {14, "grenade_shot_1"},
        {15, "grenade_shot_1"},
        {16, "grenade_shot_1"},
        {18, ""},
        {19, "glock_shot_1"},
        {20, "knife_1"},
        {21, "colt_shot_1"},
    };

    igi::WeaponSystem weapon_system;
    for (const auto& [weapon_id, expected_sound_name] : expected_sound_names) {
        ASSERT_TRUE(weapon_system.SelectWeapon(weapon_id)) << weapon_id;
        EXPECT_EQ(
            weapon_system.GetActiveWeapon().fire_sound,
            expected_sound_name) << "weapon id " << weapon_id;
    }
}
