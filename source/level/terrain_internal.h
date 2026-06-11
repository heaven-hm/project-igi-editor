/******************************************************************************
 * @file    terrain_internal.h
 * @brief   Private shared declarations for the terrain_*.cpp implementation
 *          modules. NOT part of the public Terrain API (terrain.h). It exists
 *          only so the Terrain implementation can be split across several .cpp
 *          files (core/io/lod/mesh/query) while sharing the same includes,
 *          helper functions, and math constants.
 *****************************************************************************/
#pragma once

#include "pch.h"
#include "terrain_files.h"
#include <filesystem>
#include "logger.h"
#include "parsers/qvm_parser.h"
#include "parsers/qvm_decompiler.h"
#include "utils.h"

// ── shared helpers ──────────────────────────────────────────────────────────
inline std::string GetExeDirectory() {
	char exePath[MAX_PATH];
	GetModuleFileNameA(NULL, exePath, MAX_PATH);
	std::string exeDir(exePath);
	size_t lastSlash = exeDir.find_last_of("\\/");
	if (lastSlash != std::string::npos) {
		exeDir = exeDir.substr(0, lastSlash);
	}
	return exeDir;
}

inline std::string GetTerrainDir(int level_no) {
	return Utils::GetIGIRootPath() + "\\missions\\location0\\level" + std::to_string(level_no) + "\\terrain";
}

// ── shared math constants (used across the lod/mesh/query modules) ──────────
const double	SQRT_3 = 1.7320508075688773;
const double	ONE_OVER_3 = 1.0 / 3.0;
const float		ONE_OVER_256 = 1.0f / 256.0f;

// cube lod control
constexpr	float	CUBE_ACTIVE_COMPARE = 163.0f;
constexpr	int		CUBE_ACTIVE_COMPARE_INT = (int)CUBE_ACTIVE_COMPARE;
constexpr	float	CUBE_START_MERGE_DELTA = 64.0;
constexpr	float	CUBE_ACTIVE_COMPARE_ADD_CUBE_START_MERGE_DELTA = CUBE_ACTIVE_COMPARE + CUBE_START_MERGE_DELTA;
constexpr	float	CUBE_ACTIVE_COMPARE_SUB_CUBE_START_MERGE_DELTA = CUBE_ACTIVE_COMPARE - CUBE_START_MERGE_DELTA;
constexpr	float	CUBE_MORPHING_FACTOR_COMPARE = CUBE_ACTIVE_COMPARE_ADD_CUBE_START_MERGE_DELTA / CUBE_ACTIVE_COMPARE_SUB_CUBE_START_MERGE_DELTA;
