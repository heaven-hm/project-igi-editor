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
    std::error_code ec;
    fs::create_directories(out_dir, ec);
    if (ec) {
        Logger::Get().Log(LogLevel::ERR, "[AssetExtractor] Cannot create output dir: " + out_dir);
        return false;
    }

    // Stream each entry straight to disk. This avoids holding the entire (200+ MB)
    // archive in memory, which fails to allocate in the 32-bit process after the
    // address space is fragmented by an in-session level switch.
    int written = 0;
    int totalEntries = 0;
    std::string error;
    bool ok = RES_ForEachEntry(res_path,
        [&](const std::string& name, const uint8_t* data, size_t size) {
            ++totalEntries;
            if (size == 0) return;

            // Use only the filename portion so we stay flat under out_dir.
            std::string filename = fs::path(name).filename().string();
            if (filename.empty()) return;

            std::string dest = out_dir + "\\" + filename;
            std::ofstream ofs(dest, std::ios::binary | std::ios::trunc);
            if (!ofs) {
                Logger::Get().Log(LogLevel::WARNING, "[AssetExtractor] Cannot write: " + dest);
                return;
            }
            ofs.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
            ++written;
        },
        error);

    if (!ok) {
        Logger::Get().Log(LogLevel::ERR, "[AssetExtractor] Failed to parse: " + res_path + " — " + error);
        return false;
    }

    Logger::Get().Log(LogLevel::INFO, "[AssetExtractor] Extracted " + std::to_string(written) +
                      " entries from " + res_path + " -> " + out_dir);
    return written > 0 || totalEntries == 0; // empty archive is still valid
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

    std::string texOut   = output_dir + "\\editor\\textures\\" + levelName;
    std::string modelOut = output_dir + "\\editor\\models\\" + levelName;

    std::string cacheDir   = output_dir + "\\editor\\cache";
    std::string texStamp   = cacheDir + "\\" + levelName + "_textures.stamp";
    std::string modelStamp = cacheDir + "\\" + levelName + "_models.stamp";

    Logger::Get().Log(LogLevel::INFO, "[AssetExtractor] EnsureLevelAssets level=" + std::to_string(level_no));

    bool texOk   = ExtractResIfNeeded(texRes,   texOut,   texStamp);
    bool modelOk = ExtractResIfNeeded(modelRes, modelOut, modelStamp);

    // Also ensure common assets are available (once per session).
    EnsureCommonAssets(igi_path, output_dir);

    return texOk && modelOk;
}

bool AssetExtractor::EnsureCommonAssets(const std::string& igi_path,
                                        const std::string& output_dir) {
    static bool s_done = false;
    if (s_done) return true;

    const std::string commonDir = igi_path + "\\missions\\location0\\common";
    const std::string texRes    = commonDir + "\\textures\\location0.res";
    const std::string modelRes  = commonDir + "\\models\\location0.res";
    const std::string texOut    = output_dir + "\\editor\\textures\\common";
    const std::string modelOut  = output_dir + "\\editor\\models\\common";
    const std::string cacheDir  = output_dir + "\\editor\\cache";
    const std::string texStamp  = cacheDir + "\\common_textures.stamp";
    const std::string modelStamp= cacheDir + "\\common_models.stamp";

    Logger::Get().Log(LogLevel::INFO, "[AssetExtractor] EnsureCommonAssets from " + commonDir);
    const bool texOk = ExtractResIfNeeded(texRes,   texOut,   texStamp);
    ExtractResIfNeeded(modelRes, modelOut, modelStamp);
    // Only mark done when texture extraction succeeded so a failed first attempt
    // (e.g. location0.res not yet accessible) retries on the next level load.
    if (texOk) s_done = true;
    return texOk;
}

void AssetExtractor::EnsureAllLevelTextures(const std::string& igi_path,
                                             const std::string& output_dir) {
    static bool s_done = false;
    if (s_done) return;
    s_done = true;

    const std::string cacheDir = output_dir + "\\editor\\cache";
    for (int lvl = 1; lvl <= 14; ++lvl) {
        const std::string levelName = "level" + std::to_string(lvl);
        const std::string texRes  = igi_path + "\\missions\\location0\\" + levelName +
                                    "\\textures\\" + levelName + ".res";
        const std::string texOut   = output_dir + "\\editor\\textures\\" + levelName;
        const std::string texStamp = cacheDir + "\\" + levelName + "_textures.stamp";
        ExtractResIfNeeded(texRes, texOut, texStamp);
    }
}

void AssetExtractor::ClearLevelAssets(int level_no, const std::string& output_dir) {
    std::error_code ec;
    std::string levelName = "level" + std::to_string(level_no);
    const std::string modelsDir   = output_dir + "\\editor\\models\\" + levelName;
    const std::string texturesDir = output_dir + "\\editor\\textures\\" + levelName;
    const std::string cacheDir    = output_dir + "\\editor\\cache";
    const std::string texStamp    = cacheDir + "\\" + levelName + "_textures.stamp";
    const std::string modelStamp  = cacheDir + "\\" + levelName + "_models.stamp";

    if (fs::exists(modelsDir, ec)) {
        fs::remove_all(modelsDir, ec);
        Logger::Get().Log(LogLevel::INFO, "[AssetExtractor] Removed extracted models for level " + std::to_string(level_no));
    }
    if (fs::exists(texturesDir, ec)) {
        fs::remove_all(texturesDir, ec);
        Logger::Get().Log(LogLevel::INFO, "[AssetExtractor] Removed extracted textures for level " + std::to_string(level_no));
    }
    if (fs::exists(texStamp, ec)) fs::remove(texStamp, ec);
    if (fs::exists(modelStamp, ec)) fs::remove(modelStamp, ec);
}

void AssetExtractor::CleanupExtractedAssets(const std::string& output_dir) {
    std::error_code ec;
    const std::string modelsDir   = output_dir + "\\editor\\models";
    const std::string texturesDir = output_dir + "\\editor\\textures";
    const std::string terrainDir  = output_dir + "\\editor\\terrains";
    const std::string cacheDir    = output_dir + "\\editor\\cache";

    if (fs::exists(modelsDir, ec)) {
        fs::remove_all(modelsDir, ec);
        Logger::Get().Log(LogLevel::INFO, "[AssetExtractor] Removed extracted models: " + modelsDir);
    }
    if (fs::exists(texturesDir, ec)) {
        fs::remove_all(texturesDir, ec);
        Logger::Get().Log(LogLevel::INFO, "[AssetExtractor] Removed extracted textures: " + texturesDir);
    }
    if (fs::exists(terrainDir, ec)) {
        fs::remove_all(terrainDir, ec);
        Logger::Get().Log(LogLevel::INFO, "[AssetExtractor] Removed extracted terrain: " + terrainDir);
    }
    if (fs::exists(cacheDir, ec)) {
        fs::remove_all(cacheDir, ec);
        Logger::Get().Log(LogLevel::INFO, "[AssetExtractor] Removed cache stamps: " + cacheDir);
    }
}
