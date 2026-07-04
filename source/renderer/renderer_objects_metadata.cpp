/******************************************************************************
 * @file    renderer_objects_metadata.cpp
 * @brief   Renderer_Objects: JSON/QVM metadata loaders (portal/window/AI/deathzone/magic ids)
 *          Split from renderer_objects.cpp; shares renderer_objects_internal.h.
 *****************************************************************************/
#include "renderer_objects_internal.h"

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

    std::stringstream ss(decompiled);
    std::string line;
    while (std::getline(ss, line)) {
        // Look for: Task_New(-1, "ModelLODSettings", "300_01_1", "300_01_1", 70.0, 170.0, 300.0, 350.0, 500.0);
        if (line.find("Task_New") == std::string::npos) continue;
        if (line.find("\"ModelLODSettings\"") == std::string::npos) continue;

        size_t start = line.find('(');
        size_t end = line.rfind(')');
        if (start == std::string::npos || end == std::string::npos) continue;

        std::string args = line.substr(start + 1, end - start - 1);
        
        std::vector<std::string> tokens;
        std::stringstream argStream(args);
        std::string token;
        while (std::getline(argStream, token, ',')) {
            token.erase(0, token.find_first_not_of(" \t\r\n"));
            token.erase(token.find_last_not_of(" \t\r\n") + 1);
            tokens.push_back(token);
        }

        if (tokens.size() >= 9) {
            std::string modelId = tokens[2];
            if (modelId.size() >= 2 && modelId.front() == '"' && modelId.back() == '"') {
                modelId = modelId.substr(1, modelId.size() - 2);
            }
            try {
                float portalDist = std::stof(tokens[4]);
                portal_distances_[modelId] = portalDist;
            } catch (...) {}
        }
    }
    
    Logger::Get().Log(LogLevel::INFO, "[Renderer_Objects] Loaded " + std::to_string(portal_distances_.size()) + " LOD distances from lod.qvm");
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

    // These glass-room shells in level 12 must render opaque (solid walls, not see-through).
    window_model_ids_.erase("463_03_1");
    window_model_ids_.erase("463_04_1");

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

    const std::string src = QVM_DecompileToString(qvm);
    std::istringstream ss(src);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.find("DefineMagicObj") == std::string::npos) continue;
        const size_t q1 = line.find('"');
        if (q1 == std::string::npos) continue;
        const size_t q2 = line.find('"', q1 + 1);
        if (q2 == std::string::npos) continue;
        magicobj_ids_.insert(line.substr(q1 + 1, q2 - q1 - 1));
    }
}

// ─── InitSphereMesh ───────────────────────────────────────────────────────────
// Builds a solid unit UV sphere using GL_TRIANGLES. Normal == position on a unit
// sphere, which gives correct Phong shading without extra math.
