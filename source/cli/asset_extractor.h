/******************************************************************************
 * @file    asset_extractor.h
 * @brief   Extracts textures and models from IGI .res archives to the editor
 *          working directory, with per-level file-timestamp caching.
 *****************************************************************************/

#pragma once
#include <string>

class AssetExtractor {
public:
    // Ensure textures and models for the given level are extracted to output_dir.
    // Returns true if assets are available (freshly extracted or cache hit).
    static bool EnsureLevelAssets(int level_no,
                                  const std::string& igi_path,
                                  const std::string& output_dir);

    // Ensure common assets (missions/location0/common) are extracted once.
    static bool EnsureCommonAssets(const std::string& igi_path,
                                   const std::string& output_dir);

    // Extract textures from ALL 14 level .res archives (not models).
    // Called once per session; subsequent calls are instant cache hits.
    static void EnsureAllLevelTextures(const std::string& igi_path,
                                       const std::string& output_dir);

    // Remove all extracted models and textures folders from output_dir.
    static void CleanupExtractedAssets(const std::string& output_dir);

private:
    static bool ExtractResIfNeeded(const std::string& res_path,
                                   const std::string& out_dir,
                                   const std::string& stamp_path);
    static bool ExtractRes(const std::string& res_path, const std::string& out_dir);
    static std::string ReadFileTimestamp(const std::string& path);
};
