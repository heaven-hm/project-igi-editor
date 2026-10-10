#include "audio_asset_resolver.h"

#include "../renderer/res_writer.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace igi {

namespace {

std::string ToLowerAscii(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
    return value;
}

std::string TrimAuthoredSound(std::string authored_sound) {
    const auto is_whitespace = [](unsigned char character) {
        return std::isspace(character) != 0;
    };

    const auto first_non_whitespace = std::find_if_not(
        authored_sound.begin(),
        authored_sound.end(),
        is_whitespace);
    const auto last_non_whitespace = std::find_if_not(
        authored_sound.rbegin(),
        authored_sound.rend(),
        is_whitespace).base();
    if (first_non_whitespace >= last_non_whitespace) {
        return {};
    }

    authored_sound = std::string(first_non_whitespace, last_non_whitespace);
    if (authored_sound.size() >= 2 &&
        authored_sound.front() == '"' &&
        authored_sound.back() == '"') {
        authored_sound = authored_sound.substr(
            1,
            authored_sound.size() - 2);
    }
    return authored_sound;
}

std::string FileNameFromResourceName(std::string resource_name) {
    std::replace(resource_name.begin(), resource_name.end(), '\\', '/');
    const size_t final_separator = resource_name.rfind('/');
    if (final_separator != std::string::npos) {
        resource_name = resource_name.substr(final_separator + 1);
    }

    // A few resource names are presented as LOCAL:<path> without a slash.
    const size_t namespace_separator = resource_name.rfind(':');
    if (namespace_separator != std::string::npos) {
        resource_name = resource_name.substr(namespace_separator + 1);
    }
    return resource_name;
}

bool HasWavExtension(const std::string& file_name) {
    constexpr std::string_view wav_extension = ".wav";
    if (file_name.size() < wav_extension.size()) {
        return false;
    }
    return ToLowerAscii(file_name.substr(
        file_name.size() - wav_extension.size())) == wav_extension;
}

std::vector<std::filesystem::path> BuildSoundArchiveCandidates(
    const std::filesystem::path& game_root,
    int active_level_number) {
    if (game_root.empty()) {
        return {};
    }

    const std::string level_directory_name =
        "level" + std::to_string(active_level_number);
    const std::filesystem::path level_sound_directory =
        game_root / "missions/location0" / level_directory_name / "sounds";

    return {
        level_sound_directory / "sounds.res",
        game_root / "MISSIONS/location0" /
            ("LEVEL" + std::to_string(active_level_number)) /
            "SOUNDS/SOUNDS.RES",
        game_root / "missions/location0/common/sounds/sounds.res",
        game_root / "MISSIONS/location0/COMMON/SOUNDS/SOUNDS.RES",
        game_root / "common/sounds/sounds.res",
        game_root / "COMMON/SOUNDS/SOUNDS.RES",
        game_root / "menusystem/sound/sounds.res",
        game_root / "menusystem/SOUND/SOUNDS.RES",
    };
}

std::string BuildArchiveCacheDirectoryName(
    const std::filesystem::path& archive_path,
    size_t archive_index) {
    std::error_code error_code;
    const auto archive_write_time = std::filesystem::last_write_time(
        archive_path,
        error_code);
    const std::string write_time = error_code
        ? "unknown"
        : std::to_string(static_cast<long long>(
            archive_write_time.time_since_epoch().count()));
    const std::string archive_identity = std::to_string(
        std::hash<std::string>{}(archive_path.string()));
    return "archive_" + std::to_string(archive_index) + "_" +
        archive_identity + "_" + write_time;
}

} // namespace

AudioAssetResolver::AudioAssetResolver(
    std::filesystem::path game_root,
    std::filesystem::path cache_directory)
    : game_root_(std::move(game_root)),
      cache_directory_(std::move(cache_directory)) {}

void AudioAssetResolver::Configure(
    std::filesystem::path game_root,
    std::filesystem::path cache_directory) {
    game_root_ = std::move(game_root);
    cache_directory_ = std::move(cache_directory);
    archive_index_cache_.clear();
}

void AudioAssetResolver::SetActiveLevel(int level_number) {
    active_level_number_ = std::max(1, level_number);
}

std::string AudioAssetResolver::NormalizeSoundFileName(
    const std::string& authored_sound) {
    std::string file_name = FileNameFromResourceName(
        TrimAuthoredSound(authored_sound));
    if (file_name.empty()) {
        return {};
    }
    if (!HasWavExtension(file_name)) {
        file_name += ".wav";
    }
    return file_name;
}

bool AudioAssetResolver::MatchesSoundFileName(
    const std::string& archive_entry_name,
    const std::string& requested_file_name) {
    const std::string archive_file_name = NormalizeSoundFileName(
        archive_entry_name);
    const std::string normalized_archive_file_name = ToLowerAscii(
        archive_file_name);
    const std::string normalized_requested_file_name = ToLowerAscii(
        requested_file_name);
    if (normalized_archive_file_name == normalized_requested_file_name) {
        return true;
    }

    // Two shipped entries contain the historical `name.wav.wav` spelling.
    constexpr std::string_view wav_extension = ".wav";
    if (normalized_archive_file_name.size() > wav_extension.size() &&
        normalized_archive_file_name.ends_with(
            std::string(wav_extension) + std::string(wav_extension))) {
        return normalized_archive_file_name.substr(
            0,
            normalized_archive_file_name.size() - wav_extension.size()) ==
            normalized_requested_file_name;
    }
    return false;
}

AudioAssetResolver::ResourceIndex* AudioAssetResolver::GetOrBuildArchiveIndex(
    const std::filesystem::path& archive_path) {
    const std::string archive_key = archive_path.string();
    const auto cached_index = archive_index_cache_.find(archive_key);
    if (cached_index != archive_index_cache_.end()) {
        return &cached_index->second;
    }

    std::unordered_map<std::string, ResEntryInfo> parsed_resource_index;
    std::string error_message;
    if (!RES_BuildIndex(
            archive_key,
            parsed_resource_index,
            error_message)) {
        return nullptr;
    }

    ResourceIndex resource_index;
    for (const auto& [resource_name, resource_location] : parsed_resource_index) {
        resource_index.emplace(
            resource_name,
            ResourceLocation{
                resource_location.data_offset,
                resource_location.data_size});
    }

    auto [inserted_index, inserted] = archive_index_cache_.emplace(
        archive_key,
        std::move(resource_index));
    (void)inserted;
    return &inserted_index->second;
}

std::filesystem::path AudioAssetResolver::FindLooseSound(
    const std::string& file_name) const {
    if (file_name.empty()) {
        return {};
    }

    std::vector<std::filesystem::path> candidates;
    if (!game_root_.empty()) {
        const std::string level_directory_name =
            "level" + std::to_string(active_level_number_);
        candidates = {
            game_root_ / "missions/location0" / level_directory_name /
                "sounds" / file_name,
            game_root_ / "MISSIONS/location0" /
                ("LEVEL" + std::to_string(active_level_number_)) /
                "SOUNDS" / file_name,
            game_root_ / "missions/location0/common/sounds" / file_name,
            game_root_ / "MISSIONS/location0/COMMON/SOUNDS" / file_name,
            game_root_ / "common/sounds" / file_name,
            game_root_ / "COMMON/SOUNDS" / file_name,
            game_root_ / "sounds" / file_name,
            game_root_ / "SOUNDS" / file_name,
            game_root_ / file_name,
        };
    }
    candidates.emplace_back(file_name);

    std::error_code error_code;
    for (const auto& candidate : candidates) {
        if (std::filesystem::is_regular_file(candidate, error_code)) {
            return candidate;
        }
        error_code.clear();
    }
    return {};
}

std::filesystem::path AudioAssetResolver::ExtractPackedSound(
    const std::string& file_name) {
    if (file_name.empty() || cache_directory_.empty()) {
        return {};
    }

    const std::vector<std::filesystem::path> archive_candidates =
        BuildSoundArchiveCandidates(game_root_, active_level_number_);
    for (size_t archive_index = 0;
         archive_index < archive_candidates.size();
         ++archive_index) {
        const std::filesystem::path& archive_path = archive_candidates[archive_index];
        std::error_code error_code;
        if (!std::filesystem::is_regular_file(archive_path, error_code)) {
            continue;
        }

        ResourceIndex* resource_index = GetOrBuildArchiveIndex(archive_path);
        if (resource_index == nullptr) {
            continue;
        }

        auto matching_entry = std::find_if(
            resource_index->begin(),
            resource_index->end(),
            [&file_name](const auto& indexed_entry) {
                return MatchesSoundFileName(indexed_entry.first, file_name);
            });
        if (matching_entry == resource_index->end()) {
            continue;
        }

        const ResEntryInfo resource_location{
            matching_entry->second.data_offset,
            matching_entry->second.data_size};
        const std::vector<uint8_t> resource_data = RES_ReadEntry(
            archive_path.string(),
            resource_location);
        if (resource_data.empty()) {
            continue;
        }

        // Cache under the archive entry's canonical file name so differently
        // cased requests resolve to one stable path on every filesystem.
        std::string canonical_entry_file_name = NormalizeSoundFileName(
            matching_entry->first);
        if (canonical_entry_file_name.empty()) {
            canonical_entry_file_name = file_name;
        }
        const std::filesystem::path cache_file_path =
            cache_directory_ / "audio" /
            BuildArchiveCacheDirectoryName(archive_path, archive_index) /
            canonical_entry_file_name;
        std::filesystem::create_directories(
            cache_file_path.parent_path(),
            error_code);
        if (error_code) {
            continue;
        }

        if (std::filesystem::is_regular_file(cache_file_path, error_code)) {
            return cache_file_path;
        }
        error_code.clear();

        std::ofstream output_stream(
            cache_file_path,
            std::ios::binary | std::ios::trunc);
        if (!output_stream) {
            continue;
        }
        output_stream.write(
            reinterpret_cast<const char*>(resource_data.data()),
            static_cast<std::streamsize>(resource_data.size()));
        if (!output_stream.good()) {
            std::filesystem::remove(cache_file_path, error_code);
            continue;
        }
        return cache_file_path;
    }
    return {};
}

std::filesystem::path AudioAssetResolver::ResolveWavPath(
    const std::string& authored_sound) {
    const std::filesystem::path supplied_path(authored_sound);
    std::error_code error_code;
    if (supplied_path.is_absolute() &&
        std::filesystem::is_regular_file(supplied_path, error_code)) {
        return supplied_path;
    }

    const std::string file_name = NormalizeSoundFileName(authored_sound);
    const std::filesystem::path loose_sound_path = FindLooseSound(file_name);
    if (!loose_sound_path.empty()) {
        return loose_sound_path;
    }
    return ExtractPackedSound(file_name);
}

} // namespace igi
