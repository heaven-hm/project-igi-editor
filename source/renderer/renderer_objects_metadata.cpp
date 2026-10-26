/******************************************************************************
 * @file    renderer_objects_metadata.cpp
 * @brief   Renderer_Objects: JSON/QVM metadata loaders (portal/window/AI/deathzone/magic ids)
 *          Split from renderer_objects.cpp; shares renderer_objects_internal.h.
 *****************************************************************************/
#include "renderer_objects_internal.h"
#include "../lod/lod_qvm_table.h"
#include "../lod/lod_settings.h"
#include "../runtime/magic_object_registry.h"

void Renderer_Objects::EnsurePortalDistancesLoaded() {
    if (portal_distances_loaded_) return;
    portal_distances_loaded_ = true;

    std::string qvmPath = Utils::GetIGIRootPath() + "\\lod.qvm";
    if (!std::filesystem::exists(qvmPath)) {
        Logger::Get().Log(LogLevel::WARNING, "[Renderer_Objects] lod.qvm not found at: " + qvmPath);
        return;
    }

    QVMFile qvm = QVM_Parse(qvmPath);
    if (!qvm.valid) {
        Logger::Get().Log(LogLevel::ERR, "[Renderer_Objects] Failed to parse lod.qvm");
        return;
    }

    std::string decompiled = QVM_DecompileToString(qvm);
    if (decompiled.empty()) return;

    // Task_New(-1, "ModelLODSettings", "300_01_1", "300_01_1", 70.0, 170.0, 300.0, 350.0, 500.0);
    // Three leading args then the five distances (gModelLODSettingsParams,
    // IGI-RE src/animtrig/lodsettings.c:95,104-109). The tokenizer lives in
    // source/lod/lod_qvm_table.h so it is host-testable and single-sourced.
    for (const igi::lod::AuthoredLodEntry& entry : igi::lod::ParseAuthoredLodEntries(decompiled)) {
        // Portal/attachment culling distance, in metres: the authored "Distance
        // to cutoff" (tokens[8]), or the largest distance when it is not
        // positive. Unchanged behaviour from the previous inline parse.
        portal_distances_[entry.model] = igi::lod::AuthoredCutoffDistance(entry);

        // The engine's copy of the table: non-decreasing clamp, then x4096
        // (ModelLODSettings_Changed, IGI-RE src/animtrig/lodsettings.c:157-171).
        // This is VirModel.unk28, the input to lod_select's authored branch.
        float raw[5];
        for (int i = 0; i < 5; ++i) raw[i] = entry.distance[static_cast<size_t>(i)];
        igi::lod::MonotonizeDistances(raw);
        std::array<float, 5> scaled{};
        igi::lod::ScaleDistancesToWorldUnits(raw, scaled.data());
        authored_lod_distances_[entry.model] = scaled;
    }

    Logger::Get().Log(LogLevel::INFO, "[Renderer_Objects] Loaded " + std::to_string(portal_distances_.size()) +
        " LOD distances from lod.qvm (" + std::to_string(authored_lod_distances_.size()) +
        " authored distance tables)");
}


// ─── EnsureWindowModelIdsLoaded ───────────────────────────────────────────────
// Reads IGIModels.json once and collects ModelIds whose ModelName contains
// "WINDOW" or "GLASS". These models are rendered semi-transparent.
void Renderer_Objects::EnsureWindowModelIdsLoaded() {
    if (window_ids_loaded_) return;
    window_ids_loaded_ = true;

    const std::string jsonPath = Utils::GetExeDirectory() + "\\editor\\tools\\IGIModels.json";
    if (!std::filesystem::exists(jsonPath)) {
        Logger::Get().Log(LogLevel::WARNING, "[Renderer_Objects] IGIModels.json not found at: " + jsonPath);
        return;
    }

    std::ifstream f(jsonPath);
    if (!f.is_open()) return;

    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    // Manual parse: find every {"ModelName":"...WINDOW.../GLASS...","ModelId":"..."}
    size_t pos = 0;
    while (pos < content.size()) {
        // Find ModelName value
        size_t nameKey = content.find("\"ModelName\"", pos);
        if (nameKey == std::string::npos) break;
        size_t nameStart = content.find('"', nameKey + 11);
        if (nameStart == std::string::npos) break;
        ++nameStart;
        size_t nameEnd = content.find('"', nameStart);
        if (nameEnd == std::string::npos) break;
        std::string modelName = content.substr(nameStart, nameEnd - nameStart);

        // Find ModelId value (must appear after the ModelName within same object)
        size_t idKey = content.find("\"ModelId\"", nameEnd);
        if (idKey == std::string::npos) break;
        size_t idStart = content.find('"', idKey + 9);
        if (idStart == std::string::npos) break;
        ++idStart;
        size_t idEnd = content.find('"', idStart);
        if (idEnd == std::string::npos) break;
        std::string modelId = content.substr(idStart, idEnd - idStart);

        // Check if this is a window/glass model
        auto toUpper = [](std::string s) {
            for (auto& c : s) c = (char)toupper((unsigned char)c);
            return s;
        };
        const std::string upper = toUpper(modelName);
        if (upper.find("WINDOW") != std::string::npos || upper.find("GLASS") != std::string::npos) {
            window_model_ids_.insert(modelId);
        }

        pos = idEnd + 1;
    }

    // NOTE: 463_03_1 / 463_04_1 (level-12 winch-house glass-room shells) used to
    // be erased here to force them opaque. That sent their real-alpha texels to
    // the opaque pass's alpha cutout, which discarded them — the glass building
    // was invisible until an ATTA promotion redraw. They keep their normal
    // window/glass classification now.

    Logger::Get().Log(LogLevel::INFO,
        "[Renderer_Objects] Loaded " + std::to_string(window_model_ids_.size()) +
        " window/glass model IDs from IGIModels.json");
}

// ─── EnsureAiModelIdsLoaded ───────────────────────────────────────────────────
void Renderer_Objects::EnsureAiModelIdsLoaded() {
    if (ai_ids_loaded_) return;
    ai_ids_loaded_ = true;

    const std::string jsonPath = Utils::GetExeDirectory() + "\\editor\\tools\\IGIModels.json";
    if (!std::filesystem::exists(jsonPath)) return;

    std::ifstream f(jsonPath);
    if (!f.is_open()) return;

    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    size_t pos = 0;
    while (pos < content.size()) {
        size_t nameKey = content.find("\"ModelName\"", pos);
        if (nameKey == std::string::npos) break;
        size_t nameStart = content.find('"', nameKey + 11);
        if (nameStart == std::string::npos) break;
        ++nameStart;
        size_t nameEnd = content.find('"', nameStart);
        if (nameEnd == std::string::npos) break;
        std::string modelName = content.substr(nameStart, nameEnd - nameStart);

        size_t idKey = content.find("\"ModelId\"", nameEnd);
        if (idKey == std::string::npos) break;
        size_t idStart = content.find('"', idKey + 9);
        if (idStart == std::string::npos) break;
        ++idStart;
        size_t idEnd = content.find('"', idStart);
        if (idEnd == std::string::npos) break;
        std::string modelId = content.substr(idStart, idEnd - idStart);

        auto toUpper = [](std::string s) {
            for (auto& c : s) c = (char)toupper((unsigned char)c);
            return s;
        };
        const std::string upper = toUpper(modelName);
        if (upper.find("AITYPE_") == 0) {
            ai_model_ids_.insert(modelId);
        }

        pos = idEnd + 1;
    }

    // Also manually include specific hardcoded AI types known to have sunglasses but maybe missing prefix
    ai_model_ids_.insert("009_01_1"); // Jach Priboi
    ai_model_ids_.insert("014_01_1"); // Mafia Patrol
    ai_model_ids_.insert("014_02_1"); // Mafia2 Patrol

    Logger::Get().Log(LogLevel::INFO,
        "[Renderer_Objects] Loaded " + std::to_string(ai_model_ids_.size()) +
        " AI model IDs from IGIModels.json");
}

// ─── EnsureDeathZoneIdsLoaded ──────────────────────────────────────────────────
// Parses magicobj.qvm (via the QVM decompiler) and collects model IDs registered
// as TASKTYPE_DEATHZONE. These are invisible trigger zones attached to vehicles/
// trains via ATTA; they must not be rendered as visual sub-meshes.
void Renderer_Objects::EnsureDeathZoneIdsLoaded() {
    if (deathzone_ids_loaded_) return;
    deathzone_ids_loaded_ = true;

    const std::string qvmPath = Utils::GetIGIRootPath() + "\\magicobj\\magicobj.qvm";
    if (!std::filesystem::exists(qvmPath)) {
        Logger::Get().Log(LogLevel::WARNING,
            "[Renderer_Objects] magicobj.qvm not found at: " + qvmPath);
        return;
    }

    QVMFile qvm = QVM_Parse(qvmPath);
    if (!qvm.valid) {
        Logger::Get().Log(LogLevel::WARNING,
            "[Renderer_Objects] Failed to parse magicobj.qvm: " + qvm.error);
        return;
    }

    const std::string src = QVM_DecompileToString(qvm);

    // Parse lines of the form:
    //   DefineMagicObj("model_id", "model_id", TASKTYPE_DEATHZONE);
    std::istringstream ss(src);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.find("TASKTYPE_DEATHZONE") == std::string::npos) continue;
        // Extract the first quoted string (the model ID)
        const size_t q1 = line.find('"');
        if (q1 == std::string::npos) continue;
        const size_t q2 = line.find('"', q1 + 1);
        if (q2 == std::string::npos) continue;
        deathzone_ids_.insert(line.substr(q1 + 1, q2 - q1 - 1));
    }

    Logger::Get().Log(LogLevel::INFO,
        "[Renderer_Objects] Loaded " + std::to_string(deathzone_ids_.size()) +
        " TASKTYPE_DEATHZONE model IDs from magicobj.qvm");
}

// ─── EnsureMagicObjIdsLoaded ──────────────────────────────────────────────────
void Renderer_Objects::EnsureMagicObjIdsLoaded() {
    if (magicobj_ids_loaded_) return;
    magicobj_ids_loaded_ = true;

    const std::string qvmPath = Utils::GetIGIRootPath() + "\\magicobj\\magicobj.qvm";
    if (!std::filesystem::exists(qvmPath)) return;

    QVMFile qvm = QVM_Parse(qvmPath);
    if (!qvm.valid) return;

    igi::MagicObjectRegistry registry;
    if (!registry.LoadDecompiledSource(QVM_DecompileToString(qvm))) {
        return;
    }
    for (const igi::MagicObjectDefinition& definition : registry.GetDefinitions()) {
        magicobj_ids_.insert(definition.attachment_name);
        if (definition.task_type_name == "TASKTYPE_LADDER") {
            ladder_magicobj_ids_.insert(definition.attachment_name);
        }
    }
}

bool Renderer_Objects::IsLadderMagicObject(const std::string& modelId) {
    EnsureMagicObjIdsLoaded();
    return ladder_magicobj_ids_.count(modelId) > 0;
}

// ─── InitSphereMesh ───────────────────────────────────────────────────────────
// Builds a solid unit UV sphere using GL_TRIANGLES. Normal == position on a unit
// sphere, which gives correct Phong shading without extra math.
