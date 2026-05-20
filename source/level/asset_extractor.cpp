/******************************************************************************
 * @file    asset_extractor.cpp
 * @brief   Extracts textures and models from IGI .res archives to the editor
 *          working directory, with per-level file-timestamp caching.
 *
 * Source layout (per level N):
 *   {IGIPath}\missions\location0\level{N}\textures\level{N}.res
 *   {IGIPath}\missions\location0\level{N}\models\level{N}.res
 *
 * Output layout (under output_dir, typically the exe directory):
 *   output_dir\textures\<entry-filename>
 *   output_dir\models\<entry-filename>
 *
 * Cache stamps (invalidated when .res modification time changes):
 *   output_dir\cache\level{N}_textures.stamp
 *   output_dir\cache\level{N}_models.stamp
 *****************************************************************************/

#include "asset_extractor.h"
#include "res_parser.h"
#include "../logger.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

// Return the last-write-time of path as a decimal string, or "" on failure.
std::string AssetExtractor::ReadFileTimestamp(const std::string& path) {
    std::error_code ec;
    auto t = fs::last_write_time(path, ec);
    if (ec) return "";
    return std::to_string(t.time_since_epoch().count());
}

// Extract all entries from res_path into out_dir.
// Entry names that contain path separators are flattened to just the filename
// so they land directly in out_dir without creating unexpected sub-trees.
bool AssetExtractor::ExtractRes(const std::string& res_path, const std::string& out_dir) {
    RESFile res = RES_Parse(res_path);
    if (!res.valid) {
        Logger::Get().Log(LogLevel::ERR, "[AssetExtractor] Failed to parse: " + res_path + " — " + res.error);
        return false;
    }

    std::error_code ec;
    fs::create_directories(out_dir, ec);
    if (ec) {
        Logger::Get().Log(LogLevel::ERR, "[AssetExtractor] Cannot create output dir: " + out_dir);
        return false;
    }

    int written = 0;
    for (const auto& entry : res.entries) {
        if (entry.data.empty()) continue;

        // Use only the filename portion so we stay flat under out_dir.
        std::string filename = fs::path(entry.name).filename().string();
        if (filename.empty()) continue;

        std::string dest = out_dir + "\\" + filename;
        std::ofstream ofs(dest, std::ios::binary | std::ios::trunc);
        if (!ofs) {
            Logger::Get().Log(LogLevel::WARNING, "[AssetExtractor] Cannot write: " + dest);
            continue;
        }
        ofs.write(reinterpret_cast<const char*>(entry.data.data()),
                  static_cast<std::streamsize>(entry.data.size()));
        ++written;
    }

    Logger::Get().Log(LogLevel::INFO, "[AssetExtractor] Extracted " + std::to_string(written) +
                      " entries from " + res_path + " -> " + out_dir);
    return written > 0 || res.entries.empty(); // empty archive is still valid
}

// Check stamp vs current .res timestamp; extract only when stale.
// Also fast-path: if out_dir already has files AND the stamp matches, skip entirely.
bool AssetExtractor::ExtractResIfNeeded(const std::string& res_path,
                                        const std::string& out_dir,
                                        const std::string& stamp_path) {
    std::error_code ec;
    if (!fs::exists(res_path, ec)) {
        Logger::Get().Log(LogLevel::WARNING, "[AssetExtractor] .res not found: " + res_path);
        // If the output directory already has files from a previous extraction, reuse them.
        if (fs::exists(out_dir, ec)) {
            for (const auto& e : fs::directory_iterator(out_dir, ec)) {
                if (e.is_regular_file()) {
                    Logger::Get().Log(LogLevel::INFO, "[AssetExtractor] Using previously extracted files in: " + out_dir);
                    return true;
                }
            }
        }
        return false;
    }

    std::string current_ts = ReadFileTimestamp(res_path);

    // Fast cache hit: stamp matches AND output directory is non-empty.
    if (fs::exists(stamp_path, ec)) {
        std::ifstream sf(stamp_path);
        std::string cached_ts;
        std::getline(sf, cached_ts);
        if (cached_ts == current_ts && !current_ts.empty()) {
            // Verify at least one file exists in the output directory.
            bool has_files = false;
            if (fs::exists(out_dir, ec)) {
                for (const auto& e : fs::directory_iterator(out_dir, ec)) {
                    if (e.is_regular_file()) { has_files = true; break; }
                }
            }
            if (has_files) {
                Logger::Get().Log(LogLevel::INFO, "[AssetExtractor] Cache hit (stamp+files): " + res_path);
                return true;
            }
            // Stamp matched but output was empty — re-extract.
        }
    }

    Logger::Get().Log(LogLevel::INFO, "[AssetExtractor] Extracting: " + res_path);
    if (!ExtractRes(res_path, out_dir))
        return false;

    // Write updated stamp
    fs::create_directories(fs::path(stamp_path).parent_path(), ec);
    std::ofstream sf(stamp_path, std::ios::trunc);
    if (sf) sf << current_ts;

    return true;
}

bool AssetExtractor::EnsureLevelAssets(int level_no,
                                       const std::string& igi_path,
                                       const std::string& output_dir) {
    std::string levelName = "level" + std::to_string(level_no);
    std::string missionDir = igi_path + "\\missions\\location0\\" + levelName;

    std::string texRes   = missionDir + "\\textures\\" + levelName + ".res";
    std::string modelRes = missionDir + "\\models\\"   + levelName + ".res";

    std::string texOut   = output_dir + "\\textures\\" + levelName;
    std::string modelOut = output_dir + "\\models\\" + levelName;

    std::string cacheDir   = output_dir + "\\cache";
    std::string texStamp   = cacheDir + "\\" + levelName + "_textures.stamp";
    std::string modelStamp = cacheDir + "\\" + levelName + "_models.stamp";

    Logger::Get().Log(LogLevel::INFO, "[AssetExtractor] EnsureLevelAssets level=" + std::to_string(level_no));

    bool texOk   = ExtractResIfNeeded(texRes,   texOut,   texStamp);
    bool modelOk = ExtractResIfNeeded(modelRes, modelOut, modelStamp);

    return texOk && modelOk;
}
