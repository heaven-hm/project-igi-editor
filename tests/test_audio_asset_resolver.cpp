#include <gtest/gtest.h>

#include "../source/renderer/res_writer.h"
#include "../source/renderer/res_compiler.h"
#include "../source/runtime/audio_asset_resolver.h"
#include "../source/runtime/igi_sound_bank.h"
#include "../source/runtime/audio_system.h"
#include "../source/weapon_system.h"

#include <cstdint>
#include <cstring>
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

    // A real packed sound: the 20-byte ILSF header (d3d/sound.c:548 checks the
    // magic) followed by raw PCM (format 1, d3d/sound.c:576).
    static std::vector<uint8_t> MakeIlsfPcm(std::int16_t channels,
                                            std::int32_t rate,
                                            std::int32_t samples) {
        std::vector<uint8_t> out = {'I', 'L', 'S', 'F'};
        const auto push_u16 = [&out](std::uint16_t value) {
            out.push_back(static_cast<uint8_t>(value & 0xFF));
            out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        };
        const auto push_u32 = [&out](std::uint32_t value) {
            out.push_back(static_cast<uint8_t>(value & 0xFF));
            out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
            out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
            out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
        };
        push_u16(1);                      // format: raw PCM
        push_u16(16);                     // bits
        push_u16(static_cast<uint16_t>(channels));
        push_u16(0);                      // unk1A
        push_u32(static_cast<uint32_t>(rate));
        push_u32(static_cast<uint32_t>(samples));
        const std::size_t payload =
            static_cast<std::size_t>(samples) * static_cast<std::size_t>(channels) * 2;
        for (std::size_t index = 0; index < payload; ++index) {
            out.push_back(static_cast<uint8_t>(index & 0xFF));
        }
        return out;
    }

    static bool StartsWithRiff(const std::vector<uint8_t>& bytes) {
        return bytes.size() >= 12 && bytes[0] == 'R' && bytes[1] == 'I' &&
               bytes[2] == 'F' && bytes[3] == 'F' && bytes[8] == 'W' &&
               bytes[9] == 'A' && bytes[10] == 'V' && bytes[11] == 'E';
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
    // A packed member that is already a RIFF/WAVE is passed through untouched.
    const std::vector<uint8_t> expected_sound_bytes = {
        'R', 'I', 'F', 'F', 0x11, 0, 0, 0, 'W', 'A', 'V', 'E'};
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

TEST_F(AudioAssetResolverTest, ConvertsAPackedIlsfSampleIntoAPlayableRiffWav) {
    // The shipped packs store ILSF samples, not RIFF files. Returning those
    // bytes under a .wav name produced a path that PlaySoundA and MCI cannot
    // play; the resolver must hand back a real RIFF/WAVE.
    const std::vector<uint8_t> packed_sample = MakeIlsfPcm(1, 22050, 64);
    ASSERT_EQ(std::memcmp(packed_sample.data(), "ILSF", 4), 0);
    WriteArchive(
        test_root_directory_ / "COMMON/SOUNDS/SOUNDS.RES",
        "LOCAL:common/sounds/m16_loop.wav",
        packed_sample);

    igi::AudioAssetResolver resolver(
        test_root_directory_,
        test_root_directory_ / "cache");

    const std::filesystem::path resolved_sound_path =
        resolver.ResolveWavPath("m16_loop");

    ASSERT_FALSE(resolved_sound_path.empty());
    const std::vector<uint8_t> extracted = ReadFile(resolved_sound_path);
    EXPECT_TRUE(StartsWithRiff(extracted)) << "the cached file is not a RIFF/WAVE";
    EXPECT_NE(std::memcmp(extracted.data(), "ILSF", 4), 0);
    // 44-byte RIFF header plus 64 frames * 1 channel * 2 bytes.
    EXPECT_EQ(extracted.size(), 44u + 64u * 2u);
    EXPECT_EQ(extracted[22], 1);  // channels
    EXPECT_EQ(extracted[34], 16); // bits
    EXPECT_EQ(extracted[36], 'd');  // data chunk
    EXPECT_EQ(extracted[40], static_cast<uint8_t>(64u * 2u));
}

TEST_F(AudioAssetResolverTest, ConvertsAPackedAdpcmSampleInsteadOfCachingRawBytes) {
    // Format 2 is 4-bit IMA ADPCM (voice.c:440-441). Two frames come out of one
    // byte; 0x44 0x44 decodes to 7, 17, 29, 43 (see tests/test_igi_sound_bank.cpp).
    std::vector<uint8_t> sample = MakeIlsfPcm(1, 22050, 0);
    sample.resize(0x14);
    sample[4] = 2;  // format: streamed ADPCM
    sample[5] = 0;
    sample[16] = 4;  // samples = 4
    sample[17] = 0;
    sample[18] = 0;
    sample[19] = 0;
    sample.push_back(0x44);
    sample.push_back(0x44);
    WriteArchive(
        test_root_directory_ / "COMMON/SOUNDS/SOUNDS.RES",
        "LOCAL:common/sounds/m16_loop.wav",
        sample);

    igi::AudioAssetResolver resolver(
        test_root_directory_,
        test_root_directory_ / "cache");

    const std::filesystem::path resolved_sound_path =
        resolver.ResolveWavPath("m16_loop");
    ASSERT_FALSE(resolved_sound_path.empty());
    const std::vector<uint8_t> extracted = ReadFile(resolved_sound_path);
    ASSERT_TRUE(StartsWithRiff(extracted));
    ASSERT_EQ(extracted.size(), 44u + 4u * 2u);
    const auto sample_at = [&extracted](std::size_t index) {
        return static_cast<std::int16_t>(
            static_cast<std::uint16_t>(extracted[44 + index * 2]) |
            (static_cast<std::uint16_t>(extracted[44 + index * 2 + 1]) << 8));
    };
    EXPECT_EQ(sample_at(0), 7);
    EXPECT_EQ(sample_at(1), 17);
    EXPECT_EQ(sample_at(2), 29);
    EXPECT_EQ(sample_at(3), 43);
}

TEST_F(AudioAssetResolverTest, ReportsNoPathForAPackedSampleItCannotConvert) {
    // Format 3's payload layout is not established (see igi_sound_bank.h), so
    // the resolver must not cache something it cannot vouch for.
    std::vector<uint8_t> sample = MakeIlsfPcm(1, 22050, 0);
    sample.resize(0x14);
    sample[4] = 3;  // format: ADPCM decoded at load
    sample[5] = 0;
    WriteArchive(
        test_root_directory_ / "COMMON/SOUNDS/SOUNDS.RES",
        "LOCAL:common/sounds/m16_loop.wav",
        sample);

    igi::AudioAssetResolver resolver(
        test_root_directory_,
        test_root_directory_ / "cache");
    EXPECT_TRUE(resolver.ResolveWavPath("m16_loop").empty());
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
        // Binoculars: WEAPONS/BINOCULARS/WEAPON.QVM passes the SAME string-pool
        // entry for DefineWeaponType arguments 44 and 45, and that entry is
        // "bin_zoom_1".  This expectation used to be the empty string, which no
        // shipping weapon file contains — the corpus check in
        // tests/test_weapon_config.cpp (WeaponConfigCorpus.
        // TheRetailRosterMatchesTheParsedWeaponTypes) reads the argument out of
        // the file and is what found it.  The name is referenced but has no
        // sample in any .res in the shipping install, so
        // AudioAssetResolver::ResolveWavPath returns empty for it and the
        // editor stays silent exactly as before.
        {18, "bin_zoom_1"},
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
