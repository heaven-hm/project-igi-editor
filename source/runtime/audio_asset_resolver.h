#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <cstddef>

namespace igi {

// Resolves vanilla WAV names from loose mission files or packed IRES archives.
// Packed entries are extracted lazily into a caller-owned cache directory so the
// game installation remains read-only during gameplay.
class AudioAssetResolver {
public:
    AudioAssetResolver(
        std::filesystem::path game_root = {},
        std::filesystem::path cache_directory = {});

    void Configure(
        std::filesystem::path game_root,
        std::filesystem::path cache_directory);

    void SetActiveLevel(int level_number);

    // Returns an existing loose path or a cached extraction. An empty path means
    // that no matching sound asset was found or could be materialized.
    std::filesystem::path ResolveWavPath(const std::string& authored_sound);

private:
    static std::string NormalizeSoundFileName(const std::string& authored_sound);
    static bool MatchesSoundFileName(
        const std::string& archive_entry_name,
        const std::string& requested_file_name);

    std::filesystem::path FindLooseSound(
        const std::string& file_name) const;
    std::filesystem::path ExtractPackedSound(
        const std::string& file_name);

    struct ResourceLocation {
        size_t data_offset = 0;
        size_t data_size = 0;
    };
    using ResourceIndex = std::unordered_map<std::string, ResourceLocation>;
    ResourceIndex* GetOrBuildArchiveIndex(
        const std::filesystem::path& archive_path);

    std::filesystem::path game_root_;
    std::filesystem::path cache_directory_;
    int active_level_number_ = 1;
    std::unordered_map<std::string, ResourceIndex> archive_index_cache_;
};

} // namespace igi
