/******************************************************************************
 * @file    renderer_objects_internal.h
 * @brief   Private shared declarations for the renderer_objects_*.cpp modules.
 *          Carries the common include set plus cross-module file-local helpers
 *          so Renderer_Objects can be split across several .cpp files.
 *****************************************************************************/
#pragma once

#include "pch.h"
#include "renderer_objects.h"
#include "../config.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <map>
#include <set>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "logger.h"
#include "utils.h"
#include "../level/level_common.h"
#include "gl_helper.h"
#include "atta_children.h"
#include "mef_native.h"
#include "model_texture_resolution.h"
#include "../level/qvm_parser.h"
#include "../level/qvm_decompiler.h"
#include "dat_writer.h"
#include "res_compiler.h"
#include "../level/mtp_writer.h"
#include "../runtime/editor_hover_policy.h"
#include <sstream>
#include <cstdio>

inline void FillLevelModelKey(std::string& out, int level, bool isBuilding, const std::string& modelId) {
    out.clear();
    out.reserve(modelId.size() + 24);
    char prefix[24];
    const int n = std::snprintf(prefix, sizeof(prefix), "%d:%s",
                                level, isBuilding ? "building:" : "object:");
    if (n > 0) out.append(prefix, static_cast<size_t>(n));
    out.append(modelId);
}

// Used across draw/picking/atta/visual modules.
inline bool IsWeaponModel(const std::string& modelId) {
    if (modelId.empty()) return false;
    if (modelId.size() >= 4 && modelId[0] == '1' && 
        modelId[1] >= '0' && modelId[1] <= '9' && 
        modelId[2] >= '0' && modelId[2] <= '9' && 
        modelId[3] == '_') {
        return true;
    }
    if (modelId.rfind("WEAPON_ID_", 0) == 0 || modelId.rfind("AMMO_ID_", 0) == 0) {
        return true;
    }
    return false;
}

namespace igi {

// Renderer_Objects::DrawLadderGizmos (renderer_objects_visual.cpp) is opt-out.
// The editor has no render menu — it is GLUT-based and its overlays are keyboard
// toggles in app_input_keyboard.cpp — so this is the process-wide switch a UI
// hook can flip. Default: on. See docs/igi-re-parity/LADDER_PLACEMENT.md.
void SetLadderGizmosEnabled(bool enabled);
bool LadderGizmosEnabled();

// The rail polyline overlay of GitHub issue #59 (DrawRailPathGizmos,
// renderer_objects_visual.cpp). Same opt-out shape as the ladder gizmos above,
// and for the same reason: the engine draws no rail gizmo, so there is no engine
// state to read — only an editor view preference. Default: on.
void SetRailPathGizmosEnabled(bool enabled);
bool RailPathGizmosEnabled();

}  // namespace igi
