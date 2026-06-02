#include "pch.h"
#include "renderer_objects.h"
#include "../config.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "logger.h"
#include "utils.h"
#include "../level/level_common.h"
#include "gl_helper.h"
#include "../parsers/mef_native.h"
#include "../parsers/qvm_parser.h"
#include "../parsers/qvm_decompiler.h"
#include "../parsers/dat_parser.h"
#include "../parsers/res_compiler.h"
#include <sstream>

// Strip pixel-format suffixes that appear in DAT texture IDs but aren't part
// of the actual .tex filename on disk (e.g. "009_09_1_argb8888" → "009_09_1").
static std::string StripTextureFormatSuffix(const std::string& texId) {
    static const char* const kSuffixes[] = {
        "_argb8888", "_rgb565", "_argb1555", "_argb4444",
        "_a8r8g8b8", "_r5g6b5", "_a1r5g5b5", "_a4r4g4b4"
    };
    for (const char* suf : kSuffixes) {
        const size_t sufLen = std::strlen(suf);
        if (texId.size() > sufLen) {
            // Case-insensitive suffix check
            std::string tail = texId.substr(texId.size() - sufLen);
            std::string sufLower(suf);
            for (auto& c : tail)     c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for (auto& c : sufLower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (tail == sufLower) {
                return texId.substr(0, texId.size() - sufLen);
            }
        }
    }
    return texId;
}

// True if a DAT texture id is flagged with an alpha-bearing pixel format
// (e.g. "009_09_1_argb8888"). These textures carry real per-texel transparency
// (sunglasses lenses, visors) that must be alpha-blended; the opaque pass would
// otherwise hard-cutout the lens (shader: discard when texColor.a < 0.5) and
// render it as a distorted black blob. RGB565/_r5g6b5 have no alpha.
static bool TextureIdHasAlpha(const std::string& texId) {
    static const char* const kAlphaSuffixes[] = {
        "_argb8888", "_argb1555", "_argb4444",
        "_a8r8g8b8", "_a1r5g5b5", "_a4r4g4b4"
    };
    for (const char* suf : kAlphaSuffixes) {
        const size_t sufLen = std::strlen(suf);
        if (texId.size() > sufLen) {
            std::string tail = texId.substr(texId.size() - sufLen);
            for (auto& c : tail) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (tail == suf) return true;
        }
    }
    return false;
}


// ─── EnsurePortalDistancesLoaded ──────────────────────────────────────────────
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

    const std::string jsonPath = Utils::GetExeDirectory() + "\\content\\tools\\IGIModels.json";
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

    Logger::Get().Log(LogLevel::INFO,
        "[Renderer_Objects] Loaded " + std::to_string(window_model_ids_.size()) +
        " window/glass model IDs from IGIModels.json");
}

// ─── EnsureAiModelIdsLoaded ───────────────────────────────────────────────────
void Renderer_Objects::EnsureAiModelIdsLoaded() {
    if (ai_ids_loaded_) return;
    ai_ids_loaded_ = true;

    const std::string jsonPath = Utils::GetExeDirectory() + "\\content\\tools\\IGIModels.json";
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
void Renderer_Objects::InitSphereMesh() {
    const int LAT = 12;
    const int LON = 24;
    std::vector<float> verts;
    verts.reserve(LAT * LON * 6 * 8); // 2 triangles per quad, 3 verts each, 8 floats

    auto addVert = [&](float x, float y, float z) {
        verts.push_back(x); verts.push_back(y); verts.push_back(z);
        verts.push_back(x); verts.push_back(y); verts.push_back(z); // normal = position for unit sphere
        verts.push_back(0.0f); verts.push_back(0.0f);
    };

    for (int i = 0; i < LAT; ++i) {
        float lat0 = glm::pi<float>() * (-0.5f + (float)i / LAT);
        float lat1 = glm::pi<float>() * (-0.5f + (float)(i + 1) / LAT);
        float sin0 = std::sin(lat0), cos0 = std::cos(lat0);
        float sin1 = std::sin(lat1), cos1 = std::cos(lat1);

        for (int j = 0; j < LON; ++j) {
            float lon0 = 2.0f * glm::pi<float>() * (float)j / LON;
            float lon1 = 2.0f * glm::pi<float>() * (float)(j + 1) / LON;
            float clon0 = std::cos(lon0), slon0 = std::sin(lon0);
            float clon1 = std::cos(lon1), slon1 = std::sin(lon1);

            float x00 = cos0*clon0, y00 = cos0*slon0, z00 = sin0;
            float x01 = cos0*clon1, y01 = cos0*slon1, z01 = sin0;
            float x10 = cos1*clon0, y10 = cos1*slon0, z10 = sin1;
            float x11 = cos1*clon1, y11 = cos1*slon1, z11 = sin1;

            addVert(x00, y00, z00); addVert(x10, y10, z10); addVert(x11, y11, z11);
            addVert(x00, y00, z00); addVert(x11, y11, z11); addVert(x01, y01, z01);
        }
    }

    sphere_vertex_count_ = (int)(verts.size() / 8);
    glGenVertexArrays(1, &sphere_vao_);
    glGenBuffers(1, &sphere_vbo_);
    glBindVertexArray(sphere_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, sphere_vbo_);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);
}

// ─── DrawMagicObjSpheres ──────────────────────────────────────────────────────
// Draws a solid red sphere at each MagicObject ATTA attachment position.
void Renderer_Objects::DrawMagicObjSpheres(const std::vector<LevelObject>& objects, GLuint ubo_mats) {
    EnsureMagicObjIdsLoaded();
    if (magicobj_ids_.empty()) return;

    if (sphere_vao_ == 0) InitSphereMesh();
    if (sphere_vao_ == 0) return;

    glUseProgram(shader_program_);
    glBindBufferBase(GL_UNIFORM_BUFFER, ubo_binding_point_, ubo_mats);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    GLint loc_model     = glGetUniformLocation(shader_program_, "u_model");
    GLint loc_dirlight  = glGetUniformLocation(shader_program_, "u_dirlight");
    GLint loc_ambient   = glGetUniformLocation(shader_program_, "u_ambient");
    GLint loc_useTex    = glGetUniformLocation(shader_program_, "u_useTexture");
    GLint loc_alpha     = glGetUniformLocation(shader_program_, "u_alpha");
    GLint loc_baseColor = glGetUniformLocation(shader_program_, "u_baseColor");

    glUniform1i(loc_useTex, 0);
    glUniform1f(loc_alpha, 1.0f);
    glUniform3f(loc_dirlight, 0.5f, 0.5f, 0.5f);
    glUniform3f(loc_ambient,  0.5f, 0.5f, 0.5f);
    glUniform4f(loc_baseColor, 1.0f, 0.0f, 0.0f, 1.0f); // red

    // Sphere radius = 0.3 meters in world-unit space
    const float SPHERE_SCALE = WORLD_UNITS_PER_METER * 0.3f;

    auto drawSphere = [&](const glm::vec3& worldPos) {
        glm::mat4 sphereMat = glm::translate(glm::mat4(1.0f), worldPos);
        sphereMat = glm::scale(sphereMat, glm::vec3(SPHERE_SCALE));
        glUniformMatrix4fv(loc_model, 1, GL_FALSE, glm::value_ptr(sphereMat));
        glBindVertexArray(sphere_vao_);
        glDrawArrays(GL_TRIANGLES, 0, sphere_vertex_count_);
    };

    for (const auto& obj : objects) {
        if (obj.deleted || obj.modelId.empty()) continue;

        glm::mat4 parentRot(1.0f);
        parentRot = glm::rotate(parentRot, (float)obj.rot.z, glm::vec3(0, 0, 1));
        parentRot = glm::rotate(parentRot, (float)obj.rot.x, glm::vec3(1, 0, 0));
        parentRot = glm::rotate(parentRot, (float)obj.rot.y, glm::vec3(0, 1, 0));

        glm::mat4 rootWorldMat(1.0f);
        rootWorldMat = glm::translate(rootWorldMat, glm::vec3((float)obj.pos.x, (float)obj.pos.y, (float)obj.pos.z));
        rootWorldMat = rootWorldMat * parentRot;

        // ── ATTA magic objects ───────────────────────────────────────────────
        std::string prefix = obj.isBuilding ? "building:" : "object:";
        std::string attKey = std::to_string(current_level_) + ":" + prefix + obj.modelId;
        auto ait = attachment_cache_.find(attKey);
        if (ait != attachment_cache_.end()) {
            for (const auto& att : ait->second) {
                if (!magicobj_ids_.count(att.modelId)) continue;
                glm::vec3 localOff(att.px, att.py, att.pz);
                glm::vec3 worldPos = glm::vec3(rootWorldMat * glm::vec4(localOff, 1.0f));
                drawSphere(worldPos);
            }
        }

        // ── XTVM magic vertices ──────────────────────────────────────────────
        // magicVertices are stored in mesh-local space (divided by 40.96).
        // Reconstruct world position using the full scaled model matrix.
        std::string meshKey = std::to_string(current_level_) + ":" + prefix + obj.modelId;
        auto mit = mesh_cache_.find(meshKey);
        if (mit != mesh_cache_.end()) {
            const Mesh& mesh = mit->second;
            if (!mesh.magicVertices.empty()) {
                glm::mat4 modelMat = rootWorldMat * glm::scale(glm::mat4(1.0f), glm::vec3(40.96f));
                for (const auto& mv : mesh.magicVertices) {
                    // skip null entries (all-zero position)
                    if (mv.x == 0.0f && mv.y == 0.0f && mv.z == 0.0f) continue;
                    glm::vec3 worldPos = glm::vec3(modelMat * glm::vec4(mv, 1.0f));
                    drawSphere(worldPos);
                }
            }
        }
    }

    glBindVertexArray(0);
    glUseProgram(0);
}

static bool IsWeaponModel(const std::string& modelId) {
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

// METAL_DOOR_SLIDE_UP (model 506_xx) appears in levels 12/13/14 as an EditRigidObj
// carrying a genuine multi-axis Euler tuple. It bypasses the engine's special door
// transform and so needs a different Euler application order than other rigid objects.
static bool IsMetalSlideUpDoorModel(const std::string& modelId) {
    if (modelId.empty()) return false;
    return modelId.rfind("506_", 0) == 0;
}

bool Renderer_Objects::IsSkippedModelId(const std::string& modelId) {
    if (modelId.empty()) return false;
    return modelId == "colbox" || modelId == "colbox2" || modelId == "colbox4" || modelId == "colbox66";
}

// ─── Shader Sources ───────────────────────────────────────────────────────────
static const char* OBJ_VERT_SRC = R"(
#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;

layout(std140) uniform Matrices {
    mat4 u_unused1;
    mat4 u_unused2;
    mat4 u_mvp; // Proj * View * GlobalScale
};


uniform mat4 u_model;

out vec3 v_normal;
out vec2 v_uv;
out vec3 v_fragPos;

void main() {
    vec4 worldPos   = u_model * vec4(a_pos, 1.0);
    v_fragPos       = worldPos.xyz;
    v_normal        = mat3(transpose(inverse(u_model))) * a_normal;
    v_uv            = a_uv;
    gl_Position     = u_mvp * u_model * vec4(a_pos, 1.0);
}

)";

static const char* OBJ_FRAG_SRC = R"(
#version 330 core
in vec3 v_normal;
in vec2 v_uv;
in vec3 v_fragPos;

uniform vec3 u_dirlight;   // directional light RGB
uniform vec3 u_ambient;    // ambient light RGB
uniform sampler2D u_texture;
uniform int u_useTexture;
uniform float u_alpha;     // material alpha (1.0 = opaque, <1.0 = transparent)
uniform vec4 u_baseColor;  // Base color when no texture
uniform float u_glassMin;  // glass sheen floor: clean (low-alpha) glass renders at
                           // least this opaque so the pane is visible. 0 = not glass.

out vec4 fragColor;

void main() {
    vec3 N = normalize(v_normal);
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.5));
    float diff = max(dot(N, lightDir), 0.0);

    vec3 viewDir = normalize(vec3(0.0, 1.0, 1.0));
    vec3 halfVec = normalize(lightDir + viewDir);
    float spec = pow(max(dot(N, halfVec), 0.0), 32.0) * 0.25;

    vec3 light = u_ambient + u_dirlight * (diff + spec);

    vec4 texColor = (u_useTexture != 0) ? texture(u_texture, v_uv) : u_baseColor;

    float finalAlpha = (u_useTexture != 0 ? texColor.a : 1.0) * u_alpha;

    // Glass: even a perfectly clear pane (texture alpha ~0) must show a faint
    // reflective sheen, so floor the alpha. Add a subtle specular highlight so the
    // glass reads as a surface, not an empty hole.
    if (u_glassMin > 0.0) {
        finalAlpha = max(finalAlpha, u_glassMin);
        light += vec3(spec * 1.5);
    }

    fragColor = vec4(light * texColor.rgb, finalAlpha);

    // Alpha-test cutout for foliage / fences / grilles only (alpha >= 0.9). Glass
    // (lower alpha or u_glassMin set) is NEVER cut out — it blends so you can see
    // through it while still seeing the pane.
    if (u_glassMin <= 0.0 && u_alpha >= 0.9 && texColor.a < 0.75) discard;
    if (fragColor.a < 0.01) discard;
}
)";

// ─── Picking Shader Sources ───────────────────────────────────────────────────
static const char* PICK_VERT_SRC = R"(
#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;

layout(std140) uniform Matrices {
    mat4 u_unused1;
    mat4 u_unused2;
    mat4 u_mvp;
};

uniform mat4 u_model;

void main() {
    gl_Position = u_mvp * u_model * vec4(a_pos, 1.0);
}
)";

static const char* PICK_FRAG_SRC = R"(
#version 330 core
uniform int u_object_id;
out vec4 fragColor;
void main() {
    int id = u_object_id;
    fragColor = vec4(
        float((id >> 16) & 0xFF) / 255.0,
        float((id >>  8) & 0xFF) / 255.0,
        float( id        & 0xFF) / 255.0,
        1.0
    );
}
)";

// ─── Shader Compile Helper ────────────────────────────────────────────────────
static GLuint CompileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, 512, nullptr, log);
        std::cerr << "[ObjShader] Compile error: " << log << "\n";
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint BuildShaderProgram() {
    GLuint vert = CompileShader(GL_VERTEX_SHADER,   OBJ_VERT_SRC);
    GLuint frag = CompileShader(GL_FRAGMENT_SHADER, OBJ_FRAG_SRC);
    if (!vert || !frag) return 0;

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);

    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(prog, 512, nullptr, log);
        std::cerr << "[ObjShader] Link error: " << log << "\n";
        glDeleteProgram(prog);
        prog = 0;
    }

    glDeleteShader(vert);
    glDeleteShader(frag);
    return prog;
}

static GLuint BuildPickShaderProgram() {
    GLuint vert = CompileShader(GL_VERTEX_SHADER,   PICK_VERT_SRC);
    GLuint frag = CompileShader(GL_FRAGMENT_SHADER, PICK_FRAG_SRC);
    if (!vert || !frag) return 0;

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);

    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(prog, 512, nullptr, log);
        std::cerr << "[PickShader] Link error: " << log << "\n";
        glDeleteProgram(prog);
        prog = 0;
    }

    glDeleteShader(vert);
    glDeleteShader(frag);
    return prog;
}

// ─── Constructor / Destructor ─────────────────────────────────────────────────
Renderer_Objects::Renderer_Objects() : shader_program_(0), ubo_binding_point_(0), selection_vao_(0), selection_vbo_(0) {}

Renderer_Objects::~Renderer_Objects() {
    Shutdown();
}

// ─── Init ─────────────────────────────────────────────────────────────────────
bool Renderer_Objects::Init() {
    shader_program_ = BuildShaderProgram();
    if (!shader_program_) {
        Logger::Get().Log(LogLevel::ERR, "[Renderer_Objects] Failed to build shader.");
        return false;
    }


    // Bind UBO block "Matrices" to binding point 0
    // This MUST match the binding point used by terrain renderer
    GLuint blockIdx = glGetUniformBlockIndex(shader_program_, "Matrices");
    if (blockIdx != GL_INVALID_INDEX) {
        glUniformBlockBinding(shader_program_, blockIdx, ubo_binding_point_);
        Logger::Get().Log(LogLevel::INFO, "[Renderer_Objects] UBO 'Matrices' bound to point " + std::to_string(ubo_binding_point_));
    } else {
        Logger::Get().Log(LogLevel::WARNING, "[Renderer_Objects] WARNING: UBO 'Matrices' block not found.");
    }


    // Picking shader
    pick_shader_prog_ = BuildPickShaderProgram();
    if (!pick_shader_prog_) {
        Logger::Get().Log(LogLevel::WARNING, "[Renderer_Objects] Failed to build picking shader.");
    } else {
        GLuint blk = glGetUniformBlockIndex(pick_shader_prog_, "Matrices");
        if (blk != GL_INVALID_INDEX)
            glUniformBlockBinding(pick_shader_prog_, blk, ubo_binding_point_);
    }
    // FBO created on first use (size unknown at init time)

    Logger::Get().Log(LogLevel::INFO, "[Renderer_Objects] Init OK.");

    return true;
}

// ─── ClearCaches ──────────────────────────────────────────────────────────────
void Renderer_Objects::ClearCaches() {
    Logger::Get().Log(LogLevel::INFO, "[Renderer_Objects] Clearing all level caches... (meshes=" +
        std::to_string(mesh_cache_.size()) + " textures=" + std::to_string(texture_cache_.size()) +
        " attachments=" + std::to_string(attachment_cache_.size()) +
        " modelTexMap=" + std::to_string(model_texture_map_cache_.size()) +
        " texMapLevel=" + std::to_string(texture_map_level_) +
        " globalTexMapLoaded=" + std::to_string(global_texture_map_loaded_ ? 1 : 0) + ")");
    for (auto& pair : mesh_cache_) {
        destroyModel(pair.second);
    }
    mesh_cache_.clear();
    attachment_cache_.clear();

    for (auto& pair : texture_cache_) {
        if (pair.second) {
            glDeleteTextures(1, &pair.second);
        }
    }
    texture_cache_.clear();
    model_texture_map_cache_.clear();
    
    // We explicitly DO NOT clear global_texture_map_, model_level_map_,
    // texture_level_map_, window_model_ids_, portal_distances_, deathzone_ids_,
    // or magicobj_ids_ here. These are populated from global sources (lod.qvm, names.qvm, 
    // or scanning all 14 levels' DAT files) and their definitions are static across 
    // level switches. Clearing them causes lazy re-loads that might fail if cache 
    // stamps trick the asset extractor into skipping partial directories.
    texture_map_level_ = -1;
    
    logged_draw_buildings_.clear();

}

// ─── Shutdown ─────────────────────────────────────────────────────────────────
void Renderer_Objects::Shutdown() {
    for (auto& pair : mesh_cache_)
        destroyModel(pair.second);
    mesh_cache_.clear();
    attachment_cache_.clear();

    for (auto& pair : texture_cache_) {
        if (pair.second) {
            glDeleteTextures(1, &pair.second);
        }
    }
    texture_cache_.clear();
    model_texture_map_cache_.clear();
    texture_map_level_ = -1;

    if (shader_program_) {
        glDeleteProgram(shader_program_);
        shader_program_ = 0;
    }

    if (selection_vao_) {
        glDeleteVertexArrays(1, &selection_vao_);
        selection_vao_ = 0;
    }
    if (selection_vbo_) {
        glDeleteBuffers(1, &selection_vbo_);
        selection_vbo_ = 0;
    }
    if (sphere_vao_) {
        glDeleteVertexArrays(1, &sphere_vao_);
        sphere_vao_ = 0;
    }
    if (sphere_vbo_) {
        glDeleteBuffers(1, &sphere_vbo_);
        sphere_vbo_ = 0;
    }
    if (pick_fbo_) {
        glDeleteFramebuffers(1, &pick_fbo_);
        pick_fbo_ = 0;
    }
    if (pick_color_tex_) {
        glDeleteTextures(1, &pick_color_tex_);
        pick_color_tex_ = 0;
    }
    if (pick_depth_rb_) {
        glDeleteRenderbuffers(1, &pick_depth_rb_);
        pick_depth_rb_ = 0;
    }
    if (pick_shader_prog_) {
        glDeleteProgram(pick_shader_prog_);
        pick_shader_prog_ = 0;
    }
    pick_fbo_w_ = 0;
    pick_fbo_h_ = 0;
}

// ─── InitPickingFBO ───────────────────────────────────────────────────────────
void Renderer_Objects::InitPickingFBO(int w, int h) {
    // Delete existing resources
    if (pick_fbo_)       { glDeleteFramebuffers(1,  &pick_fbo_);       pick_fbo_ = 0; }
    if (pick_color_tex_) { glDeleteTextures(1,       &pick_color_tex_); pick_color_tex_ = 0; }
    if (pick_depth_rb_)  { glDeleteRenderbuffers(1,  &pick_depth_rb_);  pick_depth_rb_ = 0; }

    // Color texture (RGB8 — ID encoded as RGB)
    glGenTextures(1, &pick_color_tex_);
    glBindTexture(GL_TEXTURE_2D, pick_color_tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Depth renderbuffer
    glGenRenderbuffers(1, &pick_depth_rb_);
    glBindRenderbuffer(GL_RENDERBUFFER, pick_depth_rb_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    // Framebuffer
    glGenFramebuffers(1, &pick_fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, pick_fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pick_color_tex_, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, pick_depth_rb_);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        Logger::Get().Log(LogLevel::ERR, "[Renderer_Objects] Picking FBO incomplete: " + std::to_string(status));
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    pick_fbo_w_ = w;
    pick_fbo_h_ = h;
}

// ─── DrawAttachmentsForPicking ────────────────────────────────────────────────
// Draws ATTA sub-models of parentModelId with the parent's pick ID so that
// clicking anywhere on an ATTA part selects the owning LevelObject.
// Mirrors DrawAttachmentsRecursive's transform calculation; skips transparency,
// lighting, and texture setup since the picking shader only cares about geometry.
std::string Renderer_Objects::AttaOccupancyKey(const std::string& modelId, const glm::vec3& worldPos) {
    char buf[160];
    snprintf(buf, sizeof(buf), "%s@%lld,%lld,%lld", modelId.c_str(),
             (long long)llround(worldPos.x), (long long)llround(worldPos.y), (long long)llround(worldPos.z));
    return buf;
}

bool Renderer_Objects::SuppressAttachmentInMef(const std::string& parentModelId, const std::string& attModelId, const glm::vec3& localPos) {
    std::vector<std::string> paths = {
        Utils::GetExeDirectory() + "\\content\\models\\level" + std::to_string(current_level_) + "\\" + parentModelId + ".mef",
        Utils::GetIGIModelsPath(current_level_) + "\\" + parentModelId + ".mef",
        Utils::GetExeDirectory() + "\\content\\models\\common\\" + parentModelId + ".mef",
        Utils::GetIGIRootPath() + "\\missions\\location0\\common\\models\\" + parentModelId + ".mef",
        Utils::GetIGIRootPath() + "\\content\\models\\" + parentModelId + ".mef"
    };

    bool modifiedAny = false;
    for (const auto& p : paths) {
        if (p.empty() || !std::filesystem::exists(p)) continue;

        std::fstream f(p, std::ios::in | std::ios::out | std::ios::binary);
        if (!f.is_open()) {
            Logger::Get().Log(LogLevel::WARNING, "[Renderer] SuppressAttachmentInMef: Could not open " + p + " for writing");
            continue;
        }

        char header[20];
        if (!f.read(header, 20) || std::memcmp(header, "ILFF", 4) != 0) {
            f.close();
            continue;
        }

        size_t chunkHeaderOffset = 20;
        while (true) {
            f.seekg(chunkHeaderOffset, std::ios::beg);
            char chunkHeader[16];
            if (!f.read(chunkHeader, 16)) break;

            char fourcc[5] = {0};
            std::memcpy(fourcc, chunkHeader, 4);
            uint32_t size = 0, alignment = 0, skip = 0;
            std::memcpy(&size, chunkHeader + 4, 4);
            std::memcpy(&alignment, chunkHeader + 8, 4);
            std::memcpy(&skip, chunkHeader + 12, 4);

            if (std::strcmp(fourcc, "ATTA") == 0) {
                size_t recordsStart = chunkHeaderOffset + 16;
                size_t numRecords = size / 68;
                for (size_t i = 0; i < numRecords; ++i) {
                    f.seekg(recordsStart + i * 68, std::ios::beg);
                    char nameBuf[16];
                    float px = 0.f, py = 0.f, pz = 0.f;
                    f.read(nameBuf, 16);
                    f.read(reinterpret_cast<char*>(&px), 4);
                    f.read(reinterpret_cast<char*>(&py), 4);
                    f.read(reinterpret_cast<char*>(&pz), 4);

                    std::string aname(nameBuf, strnlen(nameBuf, 16));
                    if (aname == attModelId &&
                        std::abs(px - localPos.x) < 0.1f &&
                        std::abs(py - localPos.y) < 0.1f &&
                        std::abs(pz - localPos.z) < 0.1f) {
                        
                        f.seekp(recordsStart + i * 68 + 16, std::ios::beg);
                        float targetX = 0.0f;
                        float targetY = 0.0f;
                        float targetZ = -20000.0f;
                        f.write(reinterpret_cast<char*>(&targetX), 4);
                        f.write(reinterpret_cast<char*>(&targetY), 4);
                        f.write(reinterpret_cast<char*>(&targetZ), 4);
                        Logger::Get().Log(LogLevel::INFO, "[Renderer] SuppressAttachmentInMef: Moved ATTA '" + attModelId + "' underground in " + p);
                        modifiedAny = true;
                    }
                }
            }

            if (skip == 0) break;
            chunkHeaderOffset += skip;
        }
        f.close();
    }

    if (modifiedAny) {
        std::string modelsFolder = Utils::GetExeDirectory() + "\\content\\models\\level" + std::to_string(current_level_);
        std::string resName = "level" + std::to_string(current_level_) + ".res";
        std::string resQscPath = modelsFolder + "\\resource.qsc";
        std::string errorStr;

        Logger::Get().Log(LogLevel::INFO, "[Renderer] ATTA modified, triggering automatic .res pack for " + resName);

        if (RES_GenerateQSC(modelsFolder, resQscPath, resName, errorStr)) {
            if (RES_Compile(resQscPath, errorStr)) {
                std::string compiledRes = modelsFolder + "\\" + resName;
                std::string gameModelsFolder = Utils::GetIGIRootPath() + "\\missions\\location0\\level" + std::to_string(current_level_) + "\\models";
                std::string gameResPath = gameModelsFolder + "\\" + resName;

                try {
                    std::filesystem::create_directories(gameModelsFolder);
                    std::filesystem::copy_file(compiledRes, gameResPath, std::filesystem::copy_options::overwrite_existing);
                    Logger::Get().Log(LogLevel::INFO, "[Renderer] Successfully deployed " + resName + " to game directory.");
                } catch (const std::exception& e) {
                    Logger::Get().Log(LogLevel::ERR, std::string("[Renderer] Failed to copy .res file to game directory: ") + e.what());
                }
            } else {
                Logger::Get().Log(LogLevel::ERR, "[Renderer] Failed to compile generated resource script: " + errorStr);
            }
        } else {
            Logger::Get().Log(LogLevel::ERR, "[Renderer] Failed to generate resource script: " + errorStr);
        }
    }

    return modifiedAny;
}

void Renderer_Objects::DrawAttachmentsForPicking(
    const std::string& parentModelId, bool isBuilding,
    const glm::mat4& parentWorldMat, float parentScale,
    GLint loc_model, GLint loc_id, int parentObjIndex,
    std::unordered_set<std::string>& drawn)
{
    const std::string prefix = isBuilding ? "building:" : "object:";
    const std::string attKey = std::to_string(current_level_) + ":" + prefix + parentModelId;
    auto ait = attachment_cache_.find(attKey);
    if (ait == attachment_cache_.end()) return;

    for (const auto& att : ait->second) {
        // Find the attachment mesh (same cache lookup as DrawAttachmentsRecursive)
        std::string subKey = std::to_string(current_level_) + ":" + prefix + att.modelId;
        auto sit = mesh_cache_.find(subKey);
        if (sit == mesh_cache_.end() || sit->second.vertexCount == 0) {
            subKey = std::to_string(current_level_) + ":object:" + att.modelId;
            sit = mesh_cache_.find(subKey);
        }
        if (sit == mesh_cache_.end()) continue;
        const Mesh& subMesh = sit->second;

        // Build ATTA world transform (identical logic to DrawAttachmentsRecursive)
        glm::mat4 attLocalRot(
            att.r[0], att.r[1], att.r[2], 0.f,
            att.r[3], att.r[4], att.r[5], 0.f,
            att.r[6], att.r[7], att.r[8], 0.f,
            0.f,      0.f,      0.f,      1.f
        );
        glm::vec3 localOff(att.px, att.py, att.pz);
        glm::vec3 worldPos = glm::vec3(parentWorldMat * glm::vec4(localOff, 1.f));
        glm::mat4 parentRot = parentWorldMat;
        parentRot[3] = glm::vec4(0.f, 0.f, 0.f, 1.f);
        glm::mat4 childWorldMat(1.0f);
        childWorldMat = glm::translate(childWorldMat, worldPos);
        childWorldMat = childWorldMat * parentRot * attLocalRot;

        // Recurse regardless of whether this node has pick-able geometry
        std::string childKey = parentModelId + ">" + att.modelId;
        bool recurse = drawn.insert(childKey).second;

        // Skip ATTAs already promoted to / duplicated by a real EditRigidObj task.
        bool occupied = IsAttaPromoted(att.modelId, worldPos);

        if (subMesh.vertexCount > 0 && !occupied) {
            // Record this ATTA as a uniquely-pickable, promotable entry.
            int entry = (int)atta_pick_entries_.size();
            AttaPickEntry e;
            e.parentObjIndex = parentObjIndex;
            e.modelId        = att.modelId;
            e.worldPos       = worldPos;
            e.worldRot       = glm::mat3(childWorldMat); // pure rotation (no scale in childWorldMat)
            e.scale          = parentScale;
            e.localPos       = glm::vec3(att.px, att.py, att.pz);
            atta_pick_entries_.push_back(e);

            glm::mat4 leafModel = glm::scale(childWorldMat, glm::vec3(40.96f * parentScale));
            glUniformMatrix4fv(loc_model, 1, GL_FALSE, glm::value_ptr(leafModel));
            glUniform1i(loc_id, kAttaPickBase + 1 + entry);

            if (!subMesh.subMeshes.empty()) {
                for (const auto& sub : subMesh.subMeshes) {
                    if (sub.VAO == 0 || sub.vertexCount == 0) continue;
                    glBindVertexArray(sub.VAO);
                    glDrawArrays(GL_TRIANGLES, 0, sub.vertexCount);
                }
            } else if (subMesh.VAO) {
                glBindVertexArray(subMesh.VAO);
                glDrawArrays(GL_TRIANGLES, 0, subMesh.vertexCount);
            }
        }

        if (recurse) {
            DrawAttachmentsForPicking(att.modelId, isBuilding, childWorldMat, parentScale,
                                      loc_model, loc_id, parentObjIndex, drawn);
        }
    }
}

// ─── DrawForPicking ───────────────────────────────────────────────────────────
void Renderer_Objects::DrawForPicking(GLuint ubo_mats,
                                      const std::vector<LevelObject>& objects,
                                      int draw_parts,
                                      const glm::vec3& camera_pos)
{
    if (!pick_shader_prog_) return;

    constexpr float BASE_SCALE = 40.96f;
    const int DRAW_OBJECTS   = 4;
    const int DRAW_BUILDINGS = 16;
    const int DRAW_PROPS     = 32;

    // Reset the per-pass ATTA pick capture and rebuild EditRigidObj occupancy so
    // ATTAs already promoted to real tasks aren't offered for promotion again.
    atta_pick_entries_.clear();
    editrigid_occupancy_.clear();
    suppressed_atta_keys_.clear();
    for (const auto& o : objects) {
        if (o.deleted || o.type != "EditRigidObj" || o.modelId.empty()) continue;
        editrigid_occupancy_.insert(AttaOccupancyKey(o.modelId, glm::vec3(o.pos)));
        if (o.name.rfind("ATTA:", 0) == 0) {
            suppressed_atta_keys_.insert(o.name.substr(5));
        }
    }

    // Set picking render state
    glBindFramebuffer(GL_FRAMEBUFFER, pick_fbo_);
    glViewport(0, 0, pick_fbo_w_, pick_fbo_h_);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    // GL_LEQUAL so that QSC child tasks (drawn AFTER the parent hull + ATTA) can
    // overwrite pick IDs written by ATTA sub-models at the same depth. Combined
    // with culling OFF (below), every child EditRigidObj wins over the parent ATTA.
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    // Match the visual pass: culling OFF. With back-face culling ON, child objects
    // whose meshes have reversed/inconsistent winding (consoles, crates, lights in
    // buildings) rendered visibly but were absent from the pick buffer.
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    glUseProgram(pick_shader_prog_);
    glBindBufferBase(GL_UNIFORM_BUFFER, ubo_binding_point_, ubo_mats);

    GLint loc_model = glGetUniformLocation(pick_shader_prog_, "u_model");
    GLint loc_id    = glGetUniformLocation(pick_shader_prog_, "u_object_id");

    for (int i = 0; i < (int)objects.size(); ++i) {
        const auto& obj = objects[i];
        if (obj.deleted || obj.modelId.empty()) continue;
        if (obj.isSplineWaypoint || obj.isSplineContainer) continue;
        if (IsSkippedModelId(obj.modelId)) continue;

        // Selective rendering (mirrors Draw())
        bool shouldDraw = false;
        if (draw_parts & DRAW_OBJECTS) {
            shouldDraw = true;
        } else {
            if ((draw_parts & DRAW_BUILDINGS) && obj.isBuilding)  shouldDraw = true;
            if ((draw_parts & DRAW_PROPS)     && !obj.isBuilding) shouldDraw = true;
        }
        if (!shouldDraw) continue;

        // NOTE: We deliberately do NOT cull objects that sit inside buildings.
        // Every child / sub-object (trucks, soldiers, crates, lights inside a
        // garage or container) must be individually pickable. GPU depth testing
        // already gives correct occlusion: building hull walls and window glass
        // are rendered opaque into the pick buffer and occlude objects behind
        // them, while true openings (open doors) let the cursor reach the
        // interior object underneath. This is geometrically exact and order-
        // independent, so AABB-based interior occlusion is unnecessary.

        Mesh mesh = GetOrLoadMesh(obj.modelId, obj.isBuilding);
        if (mesh.vertexCount == 0) continue;
        // Allow collision-only meshes (fromRenderMesh==false) as fallback pick hitboxes
        // so vehicles, cargo, and any model without render vertices are still clickable.

        // Build model matrix (same convention as Draw())
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(obj.pos));
        model = glm::rotate(model, (float)obj.rot.z, glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::rotate(model, (float)obj.rot.x, glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, (float)obj.rot.y, glm::vec3(0.0f, 1.0f, 0.0f));
        if (IsWeaponModel(obj.modelId) || obj.type == "GunPickup" || obj.type == "AmmoPickup") {
            model = glm::rotate(model, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        }
        model = glm::scale(model, glm::vec3(BASE_SCALE * obj.scale));

        glUniformMatrix4fv(loc_model, 1, GL_FALSE, glm::value_ptr(model));
        glUniform1i(loc_id, i + 1); // ID 0 = background

        // Draw hull submeshes
        if (!mesh.subMeshes.empty()) {
            for (const auto& sub : mesh.subMeshes) {
                if (sub.VAO == 0 || sub.vertexCount == 0) continue;
                glBindVertexArray(sub.VAO);
                glDrawArrays(GL_TRIANGLES, 0, sub.vertexCount);
            }
        } else if (mesh.VAO) {
            glBindVertexArray(mesh.VAO);
            glDrawArrays(GL_TRIANGLES, 0, mesh.vertexCount);
        }

        // Render ATTA sub-models into the pick buffer, each with its OWN unique
        // pick ID (kAttaPickBase + entry). Pure MEF attachments (ceiling lights,
        // wall art, panels, crates that exist only in the building model and NOT
        // in objects.qsc) become individually clickable — the app promotes them
        // into real, editable EditRigidObj tasks. ATTAs that already have a
        // matching EditRigidObj are skipped (occupancy check inside).
        {
            glm::mat4 parentWorldMat(1.0f);
            parentWorldMat = glm::translate(parentWorldMat, glm::vec3(obj.pos));
            parentWorldMat = glm::rotate(parentWorldMat, (float)obj.rot.z, glm::vec3(0.f, 0.f, 1.f));
            parentWorldMat = glm::rotate(parentWorldMat, (float)obj.rot.x, glm::vec3(1.f, 0.f, 0.f));
            parentWorldMat = glm::rotate(parentWorldMat, (float)obj.rot.y, glm::vec3(0.f, 1.f, 0.f));
            std::unordered_set<std::string> drawn;
            DrawAttachmentsForPicking(obj.modelId, obj.isBuilding, parentWorldMat, obj.scale,
                                      loc_model, loc_id, i, drawn);
            glUseProgram(pick_shader_prog_);
            glBindBufferBase(GL_UNIFORM_BUFFER, ubo_binding_point_, ubo_mats);
        }
    }

    glBindVertexArray(0);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Restore state
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDisable(GL_CULL_FACE);
}

// ─── PickObjectAtScreen ───────────────────────────────────────────────────────
int Renderer_Objects::PickObjectAtScreen(int x, int y, int w, int h,
                                          GLuint ubo_mats,
                                          const std::vector<LevelObject>& objects,
                                          int draw_parts,
                                          const glm::vec3& camera_pos)
{
    if (!pick_shader_prog_ || w <= 0 || h <= 0) return -1;

    // Resize FBO if window size changed
    if (w != pick_fbo_w_ || h != pick_fbo_h_) {
        InitPickingFBO(w, h);
    }
    if (!pick_fbo_) return -1;

    DrawForPicking(ubo_mats, objects, draw_parts, camera_pos);

    // Read back the single pixel under the cursor.
    // OpenGL origin is bottom-left; screen coords are top-left → flip Y.
    uint8_t pixel[3] = {0, 0, 0};
    glBindFramebuffer(GL_FRAMEBUFFER, pick_fbo_);
    glReadPixels(x, h - y - 1, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    int id = (static_cast<int>(pixel[0]) << 16) |
             (static_cast<int>(pixel[1]) <<  8) |
              static_cast<int>(pixel[2]);

    return (id == 0) ? -1 : id - 1;
}

// ─── Draw ─────────────────────────────────────────────────────────────────────
void Renderer_Objects::Draw(GLuint ubo_mats, bool overlay_wireframe,
                            const std::vector<LevelObject>& objects, int selected_object_index, int hover_object_index, int draw_parts,
                            const glm::vec3& camera_pos, bool show_magic_obj_spheres)
{
    // Define the flags (must match renderer.h)
    const int DRAW_OBJECTS = 4;
    const int DRAW_BUILDINGS = 16;
    const int DRAW_PROPS = 32;

    if (objects.empty()) {
        static bool logged_once = false;
        if (!logged_once) {
            Logger::Get().Log(LogLevel::WARNING, "[Renderer_Objects] Draw called with EMPTY object list!");
            logged_once = true;
        }
        return;
    }

    EnsureWindowModelIdsLoaded();

    if (!shader_program_) return;

    // Rebuild EditRigidObj occupancy so any ATTA that has been promoted to (or is
    // duplicated by) a real EditRigidObj is suppressed in the attachment render —
    // otherwise the promoted object would draw on top of its original ATTA.
    editrigid_occupancy_.clear();
    suppressed_atta_keys_.clear();
    for (const auto& o : objects) {
        if (o.deleted || o.type != "EditRigidObj" || o.modelId.empty()) continue;
        editrigid_occupancy_.insert(AttaOccupancyKey(o.modelId, glm::vec3(o.pos)));
        if (o.name.rfind("ATTA:", 0) == 0) {
            suppressed_atta_keys_.insert(o.name.substr(5));
        }
    }

    // Bind our object shader
    glUseProgram(shader_program_);

    // Bind the shared UBO (same buffer terrain uses — contains proj + view)
    glBindBufferBase(GL_UNIFORM_BUFFER, ubo_binding_point_, ubo_mats);

    // OpenGL state
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE); // Disable backface culling to fix invisible interior walls
    glDisable(GL_BLEND);

    // Wireframe toggle
    if (overlay_wireframe)
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    else
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // Uniform locations (fetched once per draw call — fast enough)
    GLint loc_model    = glGetUniformLocation(shader_program_, "u_model");
    GLint loc_dirlight = glGetUniformLocation(shader_program_, "u_dirlight");
    GLint loc_ambient  = glGetUniformLocation(shader_program_, "u_ambient");
    GLint loc_useTex   = glGetUniformLocation(shader_program_, "u_useTexture");
    GLint loc_tex      = glGetUniformLocation(shader_program_, "u_texture");
    GLint loc_alpha    = glGetUniformLocation(shader_program_, "u_alpha");
    GLint loc_baseColor = glGetUniformLocation(shader_program_, "u_baseColor");
    loc_glass_min_ = glGetUniformLocation(shader_program_, "u_glassMin");
    glUniform1f(loc_alpha, 1.0f); // default: fully opaque
    glUniform1f(loc_glass_min_, 0.0f); // default: not glass
    glUniform4f(loc_baseColor, 1.0f, 1.0f, 1.0f, 1.0f); // default: white

    EnsurePortalDistancesLoaded();

    for (int pass = 0; pass < 2; ++pass) {
        bool isTransparentPass = (pass == 1);
        if (isTransparentPass) {
            glDepthMask(GL_FALSE);
        } else {
            glDepthMask(GL_TRUE);
        }

        for (const auto& obj : objects) {
            if (obj.deleted) continue;
            
            // Selective rendering logic
        bool shouldDraw = false;
        if (draw_parts & DRAW_OBJECTS) {
            shouldDraw = true; // Draw everything
        } else {
            if ((draw_parts & DRAW_BUILDINGS) && obj.isBuilding) shouldDraw = true;
            if ((draw_parts & DRAW_PROPS) && !obj.isBuilding) shouldDraw = true;
        }

        if (!shouldDraw) continue;

        // SplineObjWaypoints and SplineObj containers are rendered by renderer_splines.cpp
        if (obj.isSplineWaypoint || obj.isSplineContainer) continue;

        // Skip empty modelId and collision-only proxies
        if (obj.modelId.empty()) continue;
        if (obj.modelId == "colbox" || obj.name == "colbox" ||
            obj.modelId == "colbox2" || obj.name == "colbox2" ||
            obj.modelId == "colbox4" || obj.name == "colbox4" ||
            obj.modelId == "colbox66" || obj.name == "colbox66") {
            continue;
        }

        Mesh mesh = GetOrLoadMesh(obj.modelId, obj.isBuilding);
        if (mesh.vertexCount == 0) continue;


        // ── Build model matrix ────────────────────────────────────────────────
        // We now place it exactly at its world position (obj.pos) and apply its own rotation (obj.rot).

        glm::mat4 model = glm::mat4(1.0f);

        // 3. Scale
        float base_scale = 40.96f;
        float total_scale = base_scale * obj.scale;

        // 1. Translate to world position
        // obj.pos.z already includes the terrain snap offset from app.cpp,
        // so we use it directly without adding zOffset again.
        model = glm::translate(model, glm::vec3(obj.pos.x, obj.pos.y, obj.pos.z));

        // 2. Apply IGI rotations (Yaw, Pitch, Roll)
        // IGI rotation order: Yaw (Z), then Pitch (X), then Roll (Y)
        if (IsMetalSlideUpDoorModel(obj.modelId)) {
            // Slide-up metal doors (levels 12/13/14) carry genuine multi-axis Euler
            // tuples and seat correctly only with Z->Y->X order. Yaw-only objects are
            // order-invariant, so scoping this to the door model regresses nothing else.
            // FALLBACK if still wrong: try order X->Y->Z, or append a +/-90° spin about Z.
            model = glm::rotate(model, (float)obj.rot.z, glm::vec3(0.0f, 0.0f, 1.0f)); // Yaw
            model = glm::rotate(model, (float)obj.rot.y, glm::vec3(0.0f, 1.0f, 0.0f)); // Roll
            model = glm::rotate(model, (float)obj.rot.x, glm::vec3(1.0f, 0.0f, 0.0f)); // Pitch
        } else {
            model = glm::rotate(model, (float)obj.rot.z, glm::vec3(0.0f, 0.0f, 1.0f)); // Yaw
            model = glm::rotate(model, (float)obj.rot.x, glm::vec3(1.0f, 0.0f, 0.0f)); // Pitch
            model = glm::rotate(model, (float)obj.rot.y, glm::vec3(0.0f, 1.0f, 0.0f)); // Roll
        }

        // For weapons, they are authored with Y-up (legacy OBJ style), so they stand upright.
        // We rotate them by 90 degrees on Pitch (X axis) to lay them flat on the ground.
        bool isWeapon = IsWeaponModel(obj.modelId) || obj.type == "GunPickup" || obj.type == "AmmoPickup";
        if (isWeapon) {
            model = glm::rotate(model, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        }

        // 3. Scale
        model = glm::scale(model, glm::vec3(total_scale));

        // Native MEF geometry is already authored in the game's coordinate space.
        // Do not apply the legacy OBJ Y-up -> Z-up correction here or buildings/AI
        // will stand on the wrong axis.

        // Upload model matrix
        glUniformMatrix4fv(loc_model, 1, GL_FALSE, glm::value_ptr(model));

        // ── Lighting and Color ────────────────────────────────────────────────
        // Compute a hash-based fallback color for models/submeshes without textures.
        float r = 0.5f, g = 0.5f, b = 0.5f;
        if (!obj.modelId.empty()) {
            size_t hash = std::hash<std::string>{}(obj.modelId);
            r = 0.4f + (float)(hash & 0xFF) / 255.0f * 0.4f;
            g = 0.4f + (float)((hash >> 8) & 0xFF) / 255.0f * 0.4f;
            b = 0.4f + (float)((hash >> 16) & 0xFF) / 255.0f * 0.4f;
        }

        // Hull buildings that have no textures of their own but carry ATTA sub-models are
        // underground container shells — skip rendering them entirely. The ATTA sub-models
        // supply all visible geometry; rendering a hash-colored hull on top obscures them.
        bool skipHullRender = false;
        bool hasAnyTexture = (mesh.textureID > 0);
        if (!hasAnyTexture) {
            for (const auto& sub : mesh.subMeshes) {
                if (sub.textureID > 0) { hasAnyTexture = true; break; }
            }
        }
        
        // Skip any model built from collision fallback geometry (XTVC/ECFC — no XTRV/DNER
        // render vertices). These produce misshapen meshes with fabricated UVs and render
        // as flat-colored or wrongly-tiled boxes. Covers vehicle hulls, train cargo objects,
        // and any other collision-only model regardless of whether it has ATTA children.
        if (!mesh.fromRenderMesh) {
            skipHullRender = true;
        }
        // Underground/hollow building shells (no own textures, ATTA children) are
        // now rendered OPAQUE like the game (the user's reference shows solid walls).
        // The old semi-transparent treatment (0.25 / 0.65) made Level 12 look like a
        // see-through mess. Kept as a flag = false so the transparent-pass routing
        // below treats them as ordinary opaque geometry.
        bool isUndergroundContainer = false;

        // Is this a window/glass model? If so, render the whole mesh semi-transparent.
        const bool isWindowModel = window_model_ids_.count(obj.modelId) > 0;
        const bool isTransparentObject = isWindowModel || isUndergroundContainer;

        // Does this model have any ARGB sub-meshes (alphaMode==2)?  Those must
        // render in the transparent pass so they properly blend over the background
        // with depth-writes disabled. Opaque sub-meshes of the same model still
        // render in the opaque pass — mixed models (guard tower, fence posts) draw
        // in BOTH passes, skipping the wrong-pass sub-meshes each time.
        bool hasArgbSubMeshes = false;
        for (const auto& sub : mesh.subMeshes) {
            if (sub.alphaMode == 2) { hasArgbSubMeshes = true; break; }
        }
        if (current_level_ == 12 && !isWindowModel) {
            hasArgbSubMeshes = false;
        }
        // A model belongs in this pass if: it's a uniform transparent/opaque
        // object that matches the pass, OR it has mixed sub-meshes and we draw
        // it in both passes (filtering per-sub-mesh below).
        const bool drawInThisPass = !skipHullRender && (
            isTransparentObject == isTransparentPass ||
            (hasArgbSubMeshes && !isTransparentObject)
        );
        if (drawInThisPass) {
            if (isTransparentObject) {
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                // Window glass: 0.55 gives visible reflectivity while still see-through.
                // Underground shell: 0.65 makes it visibly semi-transparent.
                glUniform1f(loc_alpha, isUndergroundContainer ? 0.65f : 0.55f);
                // Clean (clear) glass panes get a sheen floor so they never vanish.
                if (isWindowModel) glUniform1f(loc_glass_min_, 0.30f);
            }

            // Pull hull surfaces slightly toward camera to prevent Z-fighting with terrain.
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(-1.0f, -1.0f);

            // Draw each submesh with its own texture and lighting
            if (!mesh.subMeshes.empty()) {
                // For mixed textured/untextured meshes, skip large untextured
                // submeshes that are likely foundations (they should be underground).
                int maxTexturedVerts = 0;
                bool hasTextured = false, hasUntextured = false;
                for (const auto& sub : mesh.subMeshes) {
                    if (sub.textureID > 0) {
                        hasTextured = true;
                        maxTexturedVerts = std::max(maxTexturedVerts, sub.vertexCount);
                    } else {
                        hasUntextured = true;
                    }
                }
                bool mixedMesh = hasTextured && hasUntextured;
                for (const auto& sub : mesh.subMeshes) {
                    if (sub.VAO == 0 || sub.vertexCount == 0) continue;
                    (void)mixedMesh; // render all submeshes — floors/stories must not be skipped

                    // For mixed models: ARGB sub-meshes only in transparent pass,
                    // opaque sub-meshes only in opaque pass (unless the whole model
                    // is a transparent object, in which case all sub-meshes go through).
                    if (!isTransparentObject && hasArgbSubMeshes) {
                        if (isTransparentPass && sub.alphaMode != 2) continue;
                        if (!isTransparentPass && sub.alphaMode == 2) continue;
                    }

                    bool isBlendSub = (sub.alphaMode == 2);
                    if (current_level_ == 12 && !isWindowModel) {
                        isBlendSub = false;
                    }

                    if (isBlendSub) {
                        glEnable(GL_BLEND);
                        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                        float subAlpha = sub.baseColorFactor.a;
                        glUniform1f(loc_alpha, subAlpha);
                        // Per-sub-mesh depth-write in the transparent pass. Only genuinely
                        // see-through glass (low alpha) skips depth writes so it stays clear
                        // from every angle; alpha-tested foliage / near-opaque panes keep
                        // writing depth so leaves stay solid and don't blend into dark blobs.
                        // (A previous opaque frame sub-mesh could otherwise leave depth-writes
                        //  on for a following glass pane, making the front window opaque.)
                        if (isTransparentPass)
                            glDepthMask(subAlpha < 0.6f ? GL_FALSE : GL_TRUE);
                        else if (subAlpha >= 0.99f)
                            glDepthMask(GL_TRUE);
                    }

                    if (sub.textureID > 0) {
                        // Textured submesh: neutral lighting so the texture looks natural.
                        // Windows/glass keep their transparency (alpha 0.4 above) but render
                        // with the SAME normal lighting as everything else, so glass stays
                        // clear and see-through. The earlier flat-gray override (dirlight 0 /
                        // ambient 0.45) darkened panes into murky panels — reverted to the
                        // pre-3986cd9 "before" look the user asked to restore.
                        glUniform3f(loc_dirlight, 0.6f, 0.6f, 0.6f);
                        glUniform3f(loc_ambient,  0.4f, 0.4f, 0.4f);
                        glUniform1i(loc_useTex, 1);
                        glActiveTexture(GL_TEXTURE0);
                        glBindTexture(GL_TEXTURE_2D, sub.textureID);
                        glUniform1i(loc_tex, 0);
                    } else {
                        // Untextured submesh: use material baseColorFactor if available,
                        // otherwise fall back to the hash-based color.
                        glm::vec3 color(sub.baseColorFactor.r, sub.baseColorFactor.g, sub.baseColorFactor.b);
                        if (color.r >= 0.99f && color.g >= 0.99f && color.b >= 0.99f) {
                            color = glm::vec3(r, g, b);
                        }
                        glUniform3f(loc_dirlight, color.r * 0.6f, color.g * 0.6f, color.b * 0.6f);
                        glUniform3f(loc_ambient,  color.r * 0.4f, color.g * 0.4f, color.b * 0.4f);
                        glUniform1i(loc_useTex, 0);
                    }

                    glBindVertexArray(sub.VAO);
                    glDrawArrays(GL_TRIANGLES, 0, sub.vertexCount);

                    if (isBlendSub) {
                        glDisable(GL_BLEND);
                        glUniform1f(loc_alpha, 1.0f);
                    }
                }
                glBindVertexArray(0);
            } else {
                // Legacy single-texture path (e.g. old OBJ models)
                bool hasTexture = (mesh.textureID > 0);
                if (hasTexture) {
                    glUniform3f(loc_dirlight, 0.6f, 0.6f, 0.6f);
                    glUniform3f(loc_ambient,  0.4f, 0.4f, 0.4f);
                } else {
                    glUniform3f(loc_dirlight, 0.7f, 0.7f, 0.7f);
                    glUniform3f(loc_ambient,  r * 0.4f, g * 0.4f, b * 0.4f);
                }
                if (mesh.textureID > 0) {
                    glUniform1i(loc_useTex, 1);
                    GL_BindTexture2D(0, mesh.textureID);
                    glUniform1i(loc_tex, 0);
                } else {
                    glUniform1i(loc_useTex, 0);
                }
                renderModel(mesh);
            }

            if (isTransparentObject) {
                glDisable(GL_BLEND);
                glUniform1f(loc_alpha, 1.0f);
                glUniform1f(loc_glass_min_, 0.0f); // clear glass sheen for next object
            }

            glDisable(GL_POLYGON_OFFSET_FILL);
        }

        // Only buildings with portal/ATTA sub-models need distance-based culling.
        // Non-buildings (AI, static props, containers) never have ATTA records.
        bool isCloseEnough = true;
        if (obj.isBuilding && Config::Get().enableLOD) {
            float distToCamera = glm::distance(camera_pos, glm::vec3(obj.pos));
            float portalDistance = 100.0f;
            if (portal_distances_.count(obj.modelId)) {
                portalDistance = portal_distances_[obj.modelId];
            }
            isCloseEnough = (distToCamera <= (portalDistance * WORLD_UNITS_PER_METER));
        }

        // ── Render ATTA sub-models (recursively) ─────────────────────────────
        // Any object type (Buildings, Vehicles, Magic Models, Doors, Weapons, etc.) can have ATTA sub-models.
        std::string prefix = obj.isBuilding ? "building:" : "object:";
        std::string attCacheKey = std::to_string(current_level_) + ":" + prefix + obj.modelId;
        bool hasAttachments = attachment_cache_.find(attCacheKey) != attachment_cache_.end();
        
        if (hasAttachments && isCloseEnough && !isWeapon) {
            if (attachment_cache_.find(attCacheKey) != attachment_cache_.end()) {
                glEnable(GL_POLYGON_OFFSET_FILL);
                glPolygonOffset(-2.0f, -2.0f); // Prevent z-fighting with hull walls

                // Build the root object's unscaled world matrix (translate + rotate)
                glm::mat4 parentRot(1.0f);
                parentRot = glm::rotate(parentRot, (float)obj.rot.z, glm::vec3(0,0,1));
                parentRot = glm::rotate(parentRot, (float)obj.rot.x, glm::vec3(1,0,0));
                parentRot = glm::rotate(parentRot, (float)obj.rot.y, glm::vec3(0,1,0));

                glm::mat4 rootWorldMat(1.0f);
                rootWorldMat = glm::translate(rootWorldMat, glm::vec3((float)obj.pos.x, (float)obj.pos.y, (float)obj.pos.z));
                rootWorldMat = rootWorldMat * parentRot;

                std::unordered_set<std::string> drawn;
                DrawAttachmentsRecursive(obj.modelId, obj.modelId, obj.isBuilding, rootWorldMat, isTransparentPass,
                                          loc_model, loc_dirlight, loc_ambient,
                                          loc_useTex, loc_tex, loc_alpha, drawn);

                // DrawAttachmentsRecursive may leave GL_BLEND enabled and
                // depth-writes disabled for ARGB (glass/alpha) sub-meshes.
                // Reset both so the next object in the outer loop renders correctly
                // (prevents tree leaves and other alpha-tested geometry from
                //  appearing transparent when a building attachment drew before them).
                glDisable(GL_BLEND);
                glDepthMask(GL_TRUE);
                glUniform1f(loc_alpha, 1.0f);
                glDisable(GL_POLYGON_OFFSET_FILL);
            }
        }

        // Companion parts (_XX_2.._XX_5) are NOT rendered alongside the main body.
        // Main body models (_XX_1) already contain a complete character mesh including
        // the face. Companion parts duplicate the head geometry at a different depth,
        // which produces a visible double-face overdraw artifact.
    }
    }

    // Always reset polygon mode after draw
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDepthFunc(GL_LEQUAL); // Restore global depth func changed above to GL_LESS
    glDisable(GL_POLYGON_OFFSET_FILL);

    // Unbind shader
    glUseProgram(0);

    if (show_magic_obj_spheres) {
        DrawMagicObjSpheres(objects, ubo_mats);
    }
}

float Renderer_Objects::GetMeshZOffset(const std::string& modelId, bool isBuilding) {
    std::string cacheKey = std::to_string(current_level_) + ":" + (isBuilding ? "building:" : "object:") + modelId;
    auto it = mesh_cache_.find(cacheKey);
    Mesh mesh;
    if (it != mesh_cache_.end()) {
        mesh = it->second;
    } else {
        mesh = GetOrLoadMesh(modelId, isBuilding);
    }

    return mesh.mainZOffset;
}

glm::vec3 Renderer_Objects::GetMeshExtents(const std::string& modelId, bool isBuilding) {
    Mesh mesh = GetOrLoadMesh(modelId, isBuilding);
    return mesh.halfExtents;
}

// ─── LoadAttachmentsRecursive ────────────────────────────────────────────────
// Recursively scans the ATTA section of modelId's MEF file, caches each
// sub-model mesh AND its own attachment records, then recurses into children.
// The visited set prevents infinite loops on circular ATTA references.
void Renderer_Objects::LoadAttachmentsRecursive(const std::string& modelId, bool isBuilding,
                                                 std::unordered_set<std::string>& visited) {
    if (!visited.insert(modelId).second) return; // cycle guard

    std::string cacheKey = std::to_string(current_level_) + ":" +
                           (isBuilding ? "building:" : "object:") + modelId;

    if (attachment_cache_.find(cacheKey) != attachment_cache_.end()) return;

    std::string filepath = FindModelFile(modelId, isBuilding);
    if (filepath.empty()) {
        attachment_cache_[cacheKey] = {};
        return;
    }

    std::vector<AttachInfo> attaches;
    try {
        ParsedGeometry geo = ParseMefFile(filepath);
        for (const auto& a : geo.mefAttachments) {
            std::string aname(a.name, strnlen(a.name, 16));
            if (aname.empty()) continue;

            AttachInfo info;
            info.modelId = aname;
            info.px = a.px; info.py = a.py; info.pz = a.pz;
            info.r[0]=a.r00; info.r[1]=a.r01; info.r[2]=a.r02;
            info.r[3]=a.r03; info.r[4]=a.r04; info.r[5]=a.r05;
            info.r[6]=a.r06; info.r[7]=a.r07; info.r[8]=a.r08;
            attaches.push_back(info);

            Logger::Get().Log(LogLevel::INFO,
                "[Renderer_Objects] Attachment '" + aname + "' of '" + modelId +
                "' pos=(" + std::to_string(a.px) + "," + std::to_string(a.py) +
                "," + std::to_string(a.pz) + ")");

            // Pre-warm the sub-model mesh
            std::string subFile = FindModelFile(aname, isBuilding);
            if (subFile.empty()) {
                Logger::Get().Log(LogLevel::WARNING,
                    "[Renderer_Objects] Attachment sub-model NOT FOUND: " + aname);
            } else {
                std::string subKey = std::to_string(current_level_) + ":" +
                                     (isBuilding ? "building:" : "object:") + aname;
                if (mesh_cache_.find(subKey) == mesh_cache_.end()) {
                    try {
                        Mesh subMesh = loadObjModel(subFile, "");
                        ApplyTexturesToMesh(subMesh, aname, modelId);
                        mesh_cache_[subKey] = subMesh;
                        Logger::Get().Log(LogLevel::INFO,
                            "[Renderer_Objects] Attachment sub-model loaded: " + aname +
                            " (" + std::to_string(subMesh.vertexCount) + " verts)");
                    } catch (const std::exception &se) {
                        Logger::Get().Log(LogLevel::ERR,
                            "[Renderer_Objects] Attachment sub-model load FAILED: " + aname + ": " + se.what());
                        Mesh empty; mesh_cache_[subKey] = empty;
                    }
                }
                // Recurse: parse this child's own ATTA section
                LoadAttachmentsRecursive(aname, isBuilding, visited);
            }
        }
    } catch (const std::exception &pe) {
        Logger::Get().Log(LogLevel::WARNING,
            "[Renderer_Objects] Could not parse ATTA from '" + filepath + "': " + pe.what());
    }

    attachment_cache_[cacheKey] = std::move(attaches);
    Logger::Get().Log(LogLevel::INFO,
        "[Renderer_Objects] Attachments for '" + modelId + "': " +
        std::to_string(attachment_cache_[cacheKey].size()));
}

// ─── DrawAttachmentsRecursive ────────────────────────────────────────────────
// Draws all ATTA children of parentModelId, then recurses into each child's
// own attachments. parentWorldMat is the UN-SCALED world transform of the parent
// (translate + rotate only), so children can position themselves relative to it.
// The 40.96 scale is applied only at the leaf draw call.
void Renderer_Objects::DrawAttachmentsRecursive(
    const std::string& topLevelModelId, const std::string& parentModelId, bool isBuilding, const glm::mat4& parentWorldMat,
    bool isTransparentPass, GLint loc_model, GLint loc_dirlight,
    GLint loc_ambient, GLint loc_useTex, GLint loc_tex, GLint loc_alpha,
    std::unordered_set<std::string>& drawn,
    glm::vec3 leafScale)
{
    // Skip rendering attachments for any weapon model
    if (IsWeaponModel(parentModelId)) return;
    std::string prefix = isBuilding ? "building:" : "object:";
    std::string attCacheKey = std::to_string(current_level_) + ":" + prefix + parentModelId;
    auto ait = attachment_cache_.find(attCacheKey);
    if (ait == attachment_cache_.end()) return;

    for (const auto &att : ait->second) {
        // Find the mesh
        std::string subKey = std::to_string(current_level_) + ":" + prefix + att.modelId;
        auto sit = mesh_cache_.find(subKey);
        if (sit == mesh_cache_.end() || sit->second.vertexCount == 0) {
            subKey = std::to_string(current_level_) + ":object:" + att.modelId;
            sit = mesh_cache_.find(subKey);
        }
        if (sit == mesh_cache_.end() || sit->second.vertexCount == 0) continue;
        const Mesh &subMesh = sit->second;

        // Skip ATTA sub-models that are TASKTYPE_DEATHZONE magic objects — they are
        // invisible trigger/boarding zones, not visual geometry.
        EnsureDeathZoneIdsLoaded();
        if (deathzone_ids_.count(att.modelId)) continue;

        // Build the ATTA local rotation matrix (DirectX row-major → GLM column-major)
        glm::mat4 attLocalRot(
            att.r[0], att.r[1], att.r[2], 0.f,
            att.r[3], att.r[4], att.r[5], 0.f,
            att.r[6], att.r[7], att.r[8], 0.f,
            0.f,      0.f,      0.f,      1.f
        );

        // ATTA px/py/pz are raw floats from the MEF file, in the same coordinate
        // space as obj.pos (raw game units). parentWorldMat is also in raw game units
        // (translate=obj.pos, no scale), so no conversion needed.
        glm::vec3 localOff(att.px, att.py, att.pz);
        glm::vec3 worldPos = glm::vec3(parentWorldMat * glm::vec4(localOff, 1.f));

        // Extract parent rotation (upper-left 3x3 of the unscaled parent mat)
        glm::mat4 parentRot = parentWorldMat;
        parentRot[3] = glm::vec4(0.f, 0.f, 0.f, 1.f); // zero out translation

        // Unscaled world matrix for this attachment (used by children)
        glm::mat4 childWorldMat(1.0f);
        childWorldMat = glm::translate(childWorldMat, worldPos);
        childWorldMat = childWorldMat * parentRot * attLocalRot;

        // If this ATTA has been promoted to a real EditRigidObj task, skip drawing
        // it here — the task renders it now — but still recurse into its children
        // so nested attachments keep showing.
        if (IsAttaPromoted(att.modelId, worldPos)) {
            std::string childKey = parentModelId + ">" + att.modelId;
            if (drawn.insert(childKey).second) {
                DrawAttachmentsRecursive(topLevelModelId, att.modelId, isBuilding, childWorldMat, isTransparentPass,
                                         loc_model, loc_dirlight, loc_ambient,
                                         loc_useTex, loc_tex, loc_alpha, drawn, leafScale);
            }
            continue;
        }

        // Skip sub-models that have only collision fallback geometry (no XTRV/DNER render
        // vertices). These render as misshapen boxes with fabricated UVs.  Still recurse
        // into their children — those may have proper render geometry.
        if (!subMesh.fromRenderMesh) {
            std::string childKey = parentModelId + ">" + att.modelId;
            if (drawn.insert(childKey).second) {
                DrawAttachmentsRecursive(topLevelModelId, att.modelId, isBuilding, childWorldMat, isTransparentPass,
                                         loc_model, loc_dirlight, loc_ambient,
                                         loc_useTex, loc_tex, loc_alpha, drawn, leafScale);
            }
            continue;
        }

        // Skip untextured sub-models (trigger zones, boarding areas, collision triggers)
        // — they'd appear as featureless dark/gray slabs with no visual value.
        // Exception: small untextured sub-models (e.g. sunglasses and other character
        // head/body accessories) are legitimate visual geometry — they carry no DAT texture
        // entry but should still draw via the hash-color fallback path below. Trigger/boarding
        // slabs span several metres (half-extent in the ~1000s of raw units); accessory
        // geometry is head-sized (a few hundred units at most), so a max-half-extent threshold
        // separates them cleanly without re-introducing the slabs.
        {
            bool subHasTex = (subMesh.textureID > 0);
            for (const auto& s : subMesh.subMeshes) {
                if (s.textureID > 0) { subHasTex = true; break; }
            }
            const float kSlabHalfExtent = 700.0f; // ~0.17 m: above any accessory, below any slab
            const float subMaxHalfExtent = std::max(subMesh.halfExtents.x,
                std::max(subMesh.halfExtents.y, subMesh.halfExtents.z));
            const bool isLargeSlab = (subMaxHalfExtent >= kSlabHalfExtent);
            EnsureAiModelIdsLoaded();
            const bool isAiModel = (ai_model_ids_.count(topLevelModelId) > 0);
            if (!subHasTex && (!isAiModel || isLargeSlab)) {
                std::string childKey = parentModelId + ">" + att.modelId;
                if (drawn.insert(childKey).second) {
                    DrawAttachmentsRecursive(topLevelModelId, att.modelId, isBuilding, childWorldMat, isTransparentPass,
                                             loc_model, loc_dirlight, loc_ambient,
                                             loc_useTex, loc_tex, loc_alpha, drawn, leafScale);
                }
                continue;
            }
        }

        // Scaled model matrix: X uses leafScale.x (intervalLen for splines, 40.96 for buildings)
        glm::mat4 attModel = glm::scale(childWorldMat, leafScale);

        glUniformMatrix4fv(loc_model, 1, GL_FALSE, glm::value_ptr(attModel));

        // Determine rendering pass requirements:
        //   attIsWindow: named "WINDOW"/"GLASS" → transparent pass only
        //   attHasAlpha: any submesh has alphaMode==2 (DNER rawOpacity non-zero)
        //                → draw in BOTH passes (opaque pass discards α<0.5 pixels,
        //                  transparent pass blends the rest)
        const bool attIsWindow = window_model_ids_.count(att.modelId) > 0;
        bool attHasAlpha = false;
        for (const auto& s : subMesh.subMeshes) {
            if (s.alphaMode == 2) { attHasAlpha = true; break; }
        }
        if (current_level_ == 12 && !attIsWindow) {
            attHasAlpha = false;
        }

        // Skip this sub-model if it doesn't belong in the current pass:
        //   Windows: transparent pass only
        //   Alpha attachments: both passes
        //   Opaque attachments: opaque pass only
        const bool drawInThisPass = attIsWindow
            ? isTransparentPass
            : (!isTransparentPass || attHasAlpha);
        if (!drawInThisPass) {
            // Still recurse into children — they may need a different pass
            std::string childKey = parentModelId + ">" + att.modelId;
            if (drawn.insert(childKey).second) {
                DrawAttachmentsRecursive(topLevelModelId, att.modelId, isBuilding, childWorldMat, isTransparentPass,
                                          loc_model, loc_dirlight, loc_ambient,
                                          loc_useTex, loc_tex, loc_alpha, drawn, leafScale);
            }
            continue;
        }

        if (attIsWindow) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glUniform1f(loc_alpha, 0.55f); // visible reflectivity, still see-through
            glUniform1f(loc_glass_min_, 0.30f); // clean glass sheen floor
        } else {
            glUniform1f(loc_alpha, 1.0f);
            glUniform1f(loc_glass_min_, 0.0f);
        }

        // Draw sub-model submeshes
        if (!subMesh.subMeshes.empty()) {
            for (const auto &sub : subMesh.subMeshes) {
                if (sub.VAO == 0 || sub.vertexCount == 0) continue;

                // In the transparent pass, only draw alpha submeshes (with blending).
                // In the opaque pass, draw everything — the shader discards α<0.5 pixels.
                bool subNeedsBlend = (sub.alphaMode == 2);
                if (current_level_ == 12 && !attIsWindow) {
                    subNeedsBlend = false;
                }
                if (isTransparentPass && !attIsWindow && !subNeedsBlend) continue;

                if (subNeedsBlend) {
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                    glUniform1f(loc_alpha, sub.baseColorFactor.a);
                }

                if (sub.textureID > 0) {
                    // Same normal lighting for window/glass attachments so they stay
                    // clear and see-through (see top-level path — flat-gray reverted).
                    glUniform3f(loc_dirlight, 0.6f, 0.6f, 0.6f);
                    glUniform3f(loc_ambient,  0.4f, 0.4f, 0.4f);
                    glUniform1i(loc_useTex, 1);
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, sub.textureID);
                    glUniform1i(loc_tex, 0);
                } else {
                    // Untextured accessory (e.g. sunglasses): hash-color fallback, matching
                    // the untextured submesh path used for top-level objects above.
                    size_t hash = std::hash<std::string>{}(att.modelId);
                    float hr = 0.4f + (float)(hash & 0xFF) / 255.0f * 0.4f;
                    float hg = 0.4f + (float)((hash >> 8) & 0xFF) / 255.0f * 0.4f;
                    float hb = 0.4f + (float)((hash >> 16) & 0xFF) / 255.0f * 0.4f;
                    glUniform3f(loc_dirlight, hr * 0.6f, hg * 0.6f, hb * 0.6f);
                    glUniform3f(loc_ambient,  hr * 0.4f, hg * 0.4f, hb * 0.4f);
                    glUniform1i(loc_useTex, 0);
                }
                glBindVertexArray(sub.VAO);
                glDrawArrays(GL_TRIANGLES, 0, sub.vertexCount);

                if (subNeedsBlend) {
                    glDisable(GL_BLEND);
                    glUniform1f(loc_alpha, 1.0f);
                }
            }
            glBindVertexArray(0);
        } else if (subMesh.textureID > 0) {
            glUniform3f(loc_dirlight, 0.6f, 0.6f, 0.6f);
            glUniform3f(loc_ambient,  0.4f, 0.4f, 0.4f);
            glUniform1i(loc_useTex, 1);
            GL_BindTexture2D(0, subMesh.textureID);
            glUniform1i(loc_tex, 0);
            renderModel(subMesh);
        }

        if (attIsWindow) {
            glDisable(GL_BLEND);
            glUniform1f(loc_alpha, 1.0f);
        }

        // Recurse into this attachment's own children
        std::string childKey = parentModelId + ">" + att.modelId;
        if (drawn.insert(childKey).second) {
            DrawAttachmentsRecursive(topLevelModelId, att.modelId, isBuilding, childWorldMat, isTransparentPass,
                                      loc_model, loc_dirlight, loc_ambient,
                                      loc_useTex, loc_tex, loc_alpha, drawn, leafScale);
        }
    }
}

bool Renderer_Objects::IsVehicleType(const std::string& type) {
    return type == "Car" || type == "Heli" || type == "Plane" || type == "Train";
}

// ─── GetOrLoadMesh ────────────────────────────────────────────────────────────
Mesh Renderer_Objects::GetOrLoadMesh(const std::string& modelId, bool isBuilding) {
    std::string cacheKey = std::to_string(current_level_) + ":" + (isBuilding ? "building:" : "object:") + modelId;
    Logger::Get().Log(LogLevel::DEBUG,
        "[Renderer_Objects] GetOrLoadMesh request cacheKey=" + cacheKey + " modelId=" + modelId);

    // Return cached mesh if already loaded
    auto it = mesh_cache_.find(cacheKey);
    if (it != mesh_cache_.end()) {
        Logger::Get().Log(LogLevel::DEBUG,
            "[Renderer_Objects] Cache hit for " + cacheKey +
            " vertexCount=" + std::to_string(it->second.vertexCount));
        return it->second;
    }

    // Find the file on disk
    std::string filepath = FindModelFile(modelId, isBuilding);
    if (filepath.empty()) {
        Logger::Get().Log(LogLevel::WARNING, "[Renderer_Objects] Model search FAILED for ID: " + modelId + ". Skipping render.");
        Mesh emptyMesh;
        emptyMesh.vertexCount = 0;
        mesh_cache_[cacheKey] = emptyMesh;
        return mesh_cache_[cacheKey];
    }
    Logger::Get().Log(LogLevel::DEBUG,
        "[Renderer_Objects] Resolved modelId=" + modelId + " to path=" + filepath);


    // Load and cache
    try {
        Mesh mesh = loadObjModel(filepath, "");
        ApplyTexturesToMesh(mesh, modelId);
        mesh_cache_[cacheKey] = mesh;
        Logger::Get().Log(LogLevel::DEBUG, "[Renderer_Objects] Success: Loaded model '" + modelId + "' from " + filepath + " (" + std::to_string(mesh.vertexCount) + " vertices)");

        // Recursively parse ATTA records for this model and all nested children.
        std::unordered_set<std::string> visited;
        LoadAttachmentsRecursive(modelId, isBuilding, visited);

        // If the hull has no texture but ATTA sub-models do, inherit the first available
        // ATTA texture for the hull — this covers vehicles/magic objects whose collision
        // shell has no DAT entry but whose interior parts share the same texture sheet.
        {
            Mesh& cached = mesh_cache_[cacheKey];
            bool hullHasTex = (cached.textureID > 0);
            if (!hullHasTex) {
                for (const auto& sub : cached.subMeshes) {
                    if (sub.textureID > 0) { hullHasTex = true; break; }
                }
            }
            if (!hullHasTex) {
                std::string prefix = isBuilding ? "building:" : "object:";
                std::string attKey = std::to_string(current_level_) + ":" + prefix + modelId;
                GLuint inheritedTex = 0;
                auto ait = attachment_cache_.find(attKey);
                if (ait != attachment_cache_.end()) {
                    for (const auto& att : ait->second) {
                        std::string subKey = std::to_string(current_level_) + ":" + prefix + att.modelId;
                        auto sit = mesh_cache_.find(subKey);
                        if (sit != mesh_cache_.end()) {
                            for (const auto& sub : sit->second.subMeshes) {
                                if (sub.textureID > 0) { inheritedTex = sub.textureID; break; }
                            }
                        }
                        if (inheritedTex) break;
                    }
                }
                if (inheritedTex) {
                    for (auto& sub : cached.subMeshes) sub.textureID = inheritedTex;
                    cached.textureID = inheritedTex;
                    Logger::Get().Log(LogLevel::INFO,
                        "[TEX Native] Hull '" + modelId + "' has no texture; inherited from ATTA sub-model glId=" + std::to_string(inheritedTex));
                }
            }
        }

        return mesh_cache_[cacheKey];

    } catch (const std::exception& e) {
        Logger::Get().Log(LogLevel::ERR, "[Renderer_Objects] Load FAILED for " + modelId + ": " + std::string(e.what()));
        Mesh emptyMesh;
        mesh_cache_[cacheKey] = emptyMesh;
        return mesh_cache_[cacheKey];
    }

}

std::string Renderer_Objects::GetLevelTexturesPath() const {
    return Utils::GetExeDirectory() + "\\content\\textures\\level" + std::to_string(current_level_);
}

std::string Renderer_Objects::GetLevelTextureDatPath() const {
    // Try local extracted DAT first
    const std::string localDat = Utils::GetExeDirectory() +
        "\\content\\textures\\level" + std::to_string(current_level_) +
        "\\level" + std::to_string(current_level_) + ".dat";
    if (std::filesystem::exists(localDat)) return localDat;

    // Fall back to the game's DAT in the IGI root
    const std::string root = Utils::GetIGIRootPath();
    return root + "\\missions\\location0\\level" + std::to_string(current_level_) +
        "\\level" + std::to_string(current_level_) + ".dat";
}

void Renderer_Objects::LoadDatIntoMap(const std::string& datPath, std::map<std::string, std::vector<std::string>>& outMap) {
    DATFile dat = DAT_Parse(datPath);
    if (!dat.valid) {
        Logger::Get().Log(LogLevel::WARNING, "[TEX Native] DAT_Parse failed for: " + datPath + " Error: " + dat.error);
        return;
    }

    for (const auto& m : dat.models) {
        // We use operator[] instead of emplace so that later definitions
        // (which might contain the actual textures) overwrite earlier empty/stale definitions.
        outMap[m.modelName] = m.textures;
    }
}

void Renderer_Objects::EnsureTextureMapLoaded() {
    if (texture_map_level_ == current_level_) return;

    model_texture_map_cache_.clear();
    texture_map_level_ = current_level_;

    const std::string datPath = GetLevelTextureDatPath();
    Logger::Get().Log(LogLevel::INFO, "[TEX Native] Loading DAT map from " + datPath);
    LoadDatIntoMap(datPath, model_texture_map_cache_);
    Logger::Get().Log(LogLevel::INFO, "[TEX Native] DAT map loaded level=" + std::to_string(current_level_) + " models=" + std::to_string(model_texture_map_cache_.size()));
}

void Renderer_Objects::EnsureGlobalTextureMapLoaded() const {
    if (global_texture_map_loaded_) return;
    global_texture_map_loaded_ = true;

    const std::string igiRoot = Utils::GetIGIRootPath();
    const std::string exeDir  = Utils::GetExeDirectory();

    for (int lvl = 1; lvl <= 14; ++lvl) {
        // Try local extracted DAT first, then game path
        std::string localDat = exeDir + "\\content\\textures\\level" + std::to_string(lvl) +
                               "\\level" + std::to_string(lvl) + ".dat";
        std::string gameDat  = igiRoot + "\\missions\\location0\\level" + std::to_string(lvl) +
                               "\\level" + std::to_string(lvl) + ".dat";

        std::string datPath = "";
        if (std::filesystem::exists(localDat)) datPath = localDat;
        else if (std::filesystem::exists(gameDat)) datPath = gameDat;

        if (!datPath.empty()) {
            DATFile dat = DAT_Parse(datPath);
            if (dat.valid) {
                for (const auto& m : dat.models) {
                    global_texture_map_[m.modelName] = m.textures;
                    model_level_map_[m.modelName] = lvl;
                }
                for (const auto& t : dat.allTextures) {
                    texture_level_map_[t] = lvl;
                }
            }
        }
    }

    Logger::Get().Log(LogLevel::INFO,
        "[TEX Native] Global DAT map loaded: " + std::to_string(global_texture_map_.size()) + " models across all levels");
}

std::vector<std::string> Renderer_Objects::GetTextureIdsForModel(const std::string& modelId) {
    EnsureTextureMapLoaded();

    // 1. Current level exact match
    {
        auto it = model_texture_map_cache_.find(modelId);
        if (it != model_texture_map_cache_.end()) {
            return it->second;
        }
    }

    // 2. Global DAT search across all other levels exact match
    EnsureGlobalTextureMapLoaded();
    {
        auto it = global_texture_map_.find(modelId);
        if (it != global_texture_map_.end()) {
            return it->second;
        }
    }

    // 3. Variant fallback within current level: NNN_XX_N -> NNN_01_1
    {
        size_t p1 = modelId.find('_');
        if (p1 != std::string::npos && p1 + 4 < modelId.size()) {
            std::string primary = modelId.substr(0, p1) + "_01_1";
            if (primary != modelId) {
                auto it = model_texture_map_cache_.find(primary);
                if (it != model_texture_map_cache_.end()) {
                    return it->second;
                }
            }
        }
    }

    // 4. Variant fallback in global DAT
    {
        size_t p1 = modelId.find('_');
        if (p1 != std::string::npos && p1 + 4 < modelId.size()) {
            std::string primary = modelId.substr(0, p1) + "_01_1";
            if (primary != modelId) {
                auto it = global_texture_map_.find(primary);
                if (it != global_texture_map_.end()) {
                    return it->second;
                }
            }
        }
    }

    Logger::Get().Log(LogLevel::DEBUG, "[TEX Native] DAT miss (all sources) for modelId=" + modelId);
    return {};
}

std::string Renderer_Objects::FindTextureFile(const std::string& textureId) const {
    // Helper: search one directory for the texture file
    auto searchDir = [&](const std::filesystem::path& texturesPath) -> std::string {
        if (!std::filesystem::exists(texturesPath)) return "";

        const std::filesystem::path exactPath = texturesPath / (textureId + ".tex");
        if (std::filesystem::exists(exactPath)) return exactPath.string();

        for (const auto& entry : std::filesystem::directory_iterator(texturesPath)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".tex") continue;
            const std::string stem = entry.path().stem().string();
            if (stem == textureId ||
                stem.find(textureId) != std::string::npos ||
                textureId.find(stem) != std::string::npos) {
                return entry.path().string();
            }
        }
        return "";
    };

    // 1. Search local extracted textures directory first
    std::string result = searchDir(std::filesystem::path(GetLevelTexturesPath()));
    if (!result.empty()) return result;

    // 2. Fall back to the game's own texture directory for this level
    const std::string igiTexDir = Utils::GetIGIRootPath() +
        "\\missions\\location0\\level" + std::to_string(current_level_) + "\\textures";
    result = searchDir(std::filesystem::path(igiTexDir));
    if (!result.empty()) {
        Logger::Get().Log(LogLevel::DEBUG,
            "[TEX Native] Found texture in IGI root level textures: " + result);
        return result;
    }

    // 3. Extracted common location0 textures (from location0.res)
    const std::string commonLocalTexDir = Utils::GetExeDirectory() + "\\content\\textures\\common";
    result = searchDir(std::filesystem::path(commonLocalTexDir));
    if (!result.empty()) {
        Logger::Get().Log(LogLevel::DEBUG,
            "[TEX Native] Found texture in extracted common folder: " + result);
        return result;
    }

    // 4. Game common/shared textures folder (shared across all levels)
    const std::string igiCommonTexDir = Utils::GetIGIRootPath() + "\\textures";
    result = searchDir(std::filesystem::path(igiCommonTexDir));
    if (!result.empty()) {
        Logger::Get().Log(LogLevel::DEBUG,
            "[TEX Native] Found texture in IGI common folder: " + result);
        return result;
    }

    // 5. Game location0 common folder (shared across all location0 levels)
    const std::string igiLoc0CommonTexDir = Utils::GetIGIRootPath() + "\\missions\\location0\\common\\textures";
    result = searchDir(std::filesystem::path(igiLoc0CommonTexDir));
    if (!result.empty()) {
        Logger::Get().Log(LogLevel::DEBUG,
            "[TEX Native] Found texture in IGI location0 common folder: " + result);
        return result;
    }

    // Lazy On-Demand Extraction for cross-level texture references
    EnsureGlobalTextureMapLoaded();
    auto tit = texture_level_map_.find(textureId);
    if (tit != texture_level_map_.end()) {
        int targetLvl = tit->second;
        if (targetLvl != current_level_) {
            Logger::Get().Log(LogLevel::INFO,
                "[TEX Native] Lazy extracting textures/models on-demand for level " + std::to_string(targetLvl) +
                " to resolve cross-level texture: " + textureId);
            AssetExtractor::EnsureLevelAssets(targetLvl, Utils::GetIGIRootPath(), Utils::GetExeDirectory());
        }
    }

    // 6. Search all other levels' extracted and game texture directories.
    //    Textures like "006_07_1" live in level 6's folder but are referenced by
    //    models appearing in other levels (e.g. 003_02_1 in level 7).
    const std::string exeDir2  = Utils::GetExeDirectory();
    const std::string igiRoot2 = Utils::GetIGIRootPath();
    for (int lvl = 1; lvl <= 14; ++lvl) {
        if (lvl == current_level_) continue;
        result = searchDir(exeDir2 + "\\content\\textures\\level" + std::to_string(lvl));
        if (!result.empty()) return result;
        result = searchDir(igiRoot2 + "\\missions\\location0\\level" + std::to_string(lvl) + "\\textures");
        if (!result.empty()) return result;
    }

    Logger::Get().Log(LogLevel::ERR,
        "[TEX Native] Texture NOT FOUND: '" + textureId +
        "' — searched level " + std::to_string(current_level_) + ", common, and all fallback paths.");
    return "";
}

GLuint Renderer_Objects::GetOrLoadTexture(const std::string& textureId) {
    if (textureId.empty()) {
        return 0;
    }

    // Try the EXACT texture id first. The editor extracts format-suffixed files
    // verbatim (e.g. "405_01_1_argb8888.tex" is a distinct ARGB8888/alpha file that
    // sits alongside the opaque RGB565 "405_01_1.tex"). Loading the stripped name
    // first would grab the opaque sibling and render glass/lattice fully opaque.
    // Only fall back to the stripped id when no exact-suffixed file exists on disk.
    std::string texturePath = FindTextureFile(textureId);
    const std::string strippedId = StripTextureFormatSuffix(textureId);
    if (texturePath.empty() && strippedId != textureId) {
        texturePath = FindTextureFile(strippedId);
    }
    if (texturePath.empty()) {
        Logger::Get().Log(LogLevel::WARNING, "[TEX Native] Texture search FAILED for ID: " + textureId +
            (strippedId != textureId ? " (stripped: " + strippedId + ")" : ""));
        return 0;
    }

    const std::string cacheKey = std::to_string(current_level_) + ":" + texturePath;
    auto it = texture_cache_.find(cacheKey);
    if (it != texture_cache_.end()) {
        Logger::Get().Log(LogLevel::DEBUG, "[TEX Native] Cache hit textureId=" + textureId + " path=" + texturePath);
        return it->second;
    }

    pics_s pics{};
    if (!Tex_Load(texturePath.c_str(), pics) || !pics.pics_ || pics.num_pic_ <= 0) {
        Logger::Get().Log(LogLevel::ERR, "[TEX Native] Failed to decode TEX file: " + texturePath);
        Pic_FreePics(pics);
        return 0;
    }

    const pic_s* pic = pics.pics_;
    Logger::Get().Log(
        LogLevel::INFO,
        "[TEX Native] Loaded textureId=" + textureId +
        " " + std::to_string(pic->width_) + "x" + std::to_string(pic->height_) +
        " frames=" + std::to_string(pics.num_pic_) +
        " path=" + texturePath);

    const GLuint texture = GL_RegisterTexture(pic, GL_REPEAT, GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, true);
    texture_cache_[cacheKey] = texture;
    Pic_FreePics(pics);

    Logger::Get().Log(
        LogLevel::DEBUG,
        "[TEX Native] Uploaded textureId=" + textureId +
        " glId=" + std::to_string(texture));
    return texture;
}

void Renderer_Objects::ApplyTexturesToMesh(Mesh& mesh, const std::string& modelId, const std::string& parentModelId) {
    std::vector<std::string> textureIds = GetTextureIdsForModel(modelId);
    bool isDatMiss = textureIds.empty();

    // When a sub-model has no DAT entry, IGI ATTA sub-models commonly share material slots with their
    // parent building, so inherit the parent's texture list in that case.
    if (!parentModelId.empty() && isDatMiss) {
        const std::vector<std::string> parentIds = GetTextureIdsForModel(parentModelId);
        const bool parentHasRealEntry = !parentIds.empty();
        if (parentHasRealEntry) {
            Logger::Get().Log(LogLevel::INFO,
                "[TEX Native] Sub-model '" + modelId + "' has no DAT entry; inheriting " +
                std::to_string(parentIds.size()) + " texture(s) from parent '" + parentModelId + "'");
            textureIds = parentIds;
            isDatMiss = false;
        }
    }

    // Conversely, some vehicles or magic models (_01_1) have no textures in DAT, but their interior 
    // attachments (_02_1) DO have textures. If we still have a DAT miss, inherit from the typical child.
    if (isDatMiss) {
        size_t p1 = modelId.find("_01_1");
        if (p1 != std::string::npos) {
            std::string childId = modelId.substr(0, p1) + "_02_1";
            std::vector<std::string> childTexIds = GetTextureIdsForModel(childId);
            if (!childTexIds.empty()) {
                Logger::Get().Log(LogLevel::INFO,
                    "[TEX Native] Model '" + modelId + "' has no DAT entry; inheriting " +
                    std::to_string(childTexIds.size()) + " texture(s) from child '" + childId + "'");
                textureIds = childTexIds;
                isDatMiss = false;
            }
        }
    }

    // If it's still a DAT miss (not explicitly in dat), fallback to self name first.
    if (isDatMiss) {
        textureIds = { modelId };
    }

    if (textureIds.empty()) {
        Logger::Get().Log(LogLevel::INFO, "[TEX Native] No textures mapped for modelId=" + modelId);
        return;
    }

    // For AI character models (sunglasses-bearing), load the STRIPPED texture id
    // so "009_09_1_argb8888" → "009_09_1.tex" (opaque body texture → solid black
    // glasses). The _argb8888 variant is a tiny 32×32 semi-transparent overlay
    // used by the game engine; loading it in the editor gives barely-visible lenses.
    // Non-AI models (guard tower, lattice) need the exact _argb8888.tex for
    // transparent rendering and keep the "exact first" preference.
    EnsureAiModelIdsLoaded();
    const bool isAiModel = ai_model_ids_.count(modelId) > 0;

    std::vector<GLuint> textures;
    textures.reserve(textureIds.size());
    for (const auto& textureId : textureIds) {
        const std::string loadId = (isAiModel && TextureIdHasAlpha(textureId))
            ? StripTextureFormatSuffix(textureId)
            : textureId;
        textures.push_back(GetOrLoadTexture(loadId));
    }

    if (!mesh.subMeshes.empty()) {
        size_t assigned = 0;

        // Find the best valid (non-zero) texture to use as fallback for submeshes
        // that fall outside the DAT texture list range.
        GLuint fallbackTexture = 0;
        for (const GLuint t : textures) {
            if (t != 0) { fallbackTexture = t; }
        }

        for (size_t i = 0; i < mesh.subMeshes.size(); ++i) {
            GLuint texture = 0;
            const int matSlot = mesh.subMeshes[i].materialSlot;
            std::string resolvedTexId; // tracks the actual texture id string for ARGB detection

            if (textures.size() == 1) {
                texture = textures[0];
                resolvedTexId = textureIds.empty() ? "" : textureIds[0];
            } else if (matSlot >= 0 && static_cast<size_t>(matSlot) < textures.size()) {
                texture = textures[matSlot];
                resolvedTexId = (static_cast<size_t>(matSlot) < textureIds.size()) ? textureIds[matSlot] : "";
            } else if (matSlot > 0 && !textures.empty()) {
                EnsureAiModelIdsLoaded();
                if (ai_model_ids_.count(modelId) > 0) {
                    // materialSlot is out of range — sunglasses-bearing characters (009_01_1,
                    // 014_01_1, 014_02_1) where the MEF block references a slot beyond the
                    // local DAT texture list. Resolve via global DAT.
                    bool resolved = false;
                    EnsureGlobalTextureMapLoaded();
                    {
                        auto git = global_texture_map_.find(modelId);
                        if (git != global_texture_map_.end() &&
                            static_cast<size_t>(matSlot) < git->second.size()) {
                            const std::string& globalTexId = git->second[static_cast<size_t>(matSlot)];
                            GLuint globalTex = GetOrLoadTexture(globalTexId);
                            if (globalTex) {
                                texture = globalTex;
                                resolvedTexId = globalTexId; // preserve for ARGB detection
                                resolved = true;
                                Logger::Get().Log(LogLevel::INFO,
                                    "[TEX Native] AI materialSlot out of range resolved via global DAT for modelId=" + modelId +
                                    " submeshIndex=" + std::to_string(i) +
                                    " materialSlot=" + std::to_string(matSlot) +
                                    " globalTexId=" + globalTexId);
                            }
                        }
                    }
                    if (!resolved) {
                        texture = textures.back();
                        resolvedTexId = textureIds.empty() ? "" : textureIds.back();
                        Logger::Get().Log(LogLevel::WARNING,
                            "[TEX Native] AI materialSlot out of range, using last texture for modelId=" + modelId +
                            " submeshIndex=" + std::to_string(i) +
                            " materialSlot=" + std::to_string(matSlot) +
                            " textureCount=" + std::to_string(textures.size()));
                    }
                } else {
                    size_t wrapped = static_cast<size_t>(matSlot) % textures.size();
                    texture = textures[wrapped];
                    resolvedTexId = (wrapped < textureIds.size()) ? textureIds[wrapped] : "";
                    Logger::Get().Log(LogLevel::WARNING,
                        "[TEX Native] materialSlot out of range, wrapping for modelId=" + modelId +
                        " submeshIndex=" + std::to_string(i) +
                        " materialSlot=" + std::to_string(matSlot) +
                        " textureCount=" + std::to_string(textures.size()) +
                        " wrappedSlot=" + std::to_string(wrapped));
                }
            } else if (i < textures.size()) {
                texture = textures[i];
                resolvedTexId = (i < textureIds.size()) ? textureIds[i] : "";
            } else {
                texture = fallbackTexture;
            }

            mesh.subMeshes[i].textureID = texture;
            if (texture) {
                ++assigned;
                // ARGB-format textures carry real per-texel alpha (sunglasses lenses,
                // guard tower lattice, wire fences, etc.). Use resolvedTexId which
                // captures the actual texture id in ALL code paths including out-of-range
                // ARGB textures on non-AI models (lattice, fences, decals) need alpha
                // blending via the transparent pass. AI model ARGB sub-meshes (sunglasses)
                // must render OPAQUE with the cutout shader so the black lens pixels show
                // as solid black — blending them makes them see-through.
                if (TextureIdHasAlpha(resolvedTexId) && ai_model_ids_.count(modelId) == 0) {
                    mesh.subMeshes[i].alphaMode = 2;
                    mesh.subMeshes[i].baseColorFactor.a = 0.95f;
                }
            }
        }

        if (!mesh.subMeshes.empty()) {
            mesh.textureID = mesh.subMeshes.front().textureID;
        }

        Logger::Get().Log(
            LogLevel::INFO,
            "[TEX Native] Applied textures to modelId=" + modelId +
            " subMeshes=" + std::to_string(mesh.subMeshes.size()) +
            " datTextures=" + std::to_string(textureIds.size()) +
            " assigned=" + std::to_string(assigned));

        if (textureIds.size() != mesh.subMeshes.size()) {
            Logger::Get().Log(
                LogLevel::WARNING,
                "[TEX Native] WARNING: Texture/submesh count mismatch for modelId=" + modelId +
                " subMeshes=" + std::to_string(mesh.subMeshes.size()) +
                " datTextures=" + std::to_string(textureIds.size()));
        }
        return;
    }

    mesh.textureID = textures.front();
    Logger::Get().Log(
        LogLevel::INFO,
        "[TEX Native] Applied legacy single texture to modelId=" + modelId +
        " textureId=" + textureIds.front() +
        " glId=" + std::to_string(mesh.textureID));
}

Mesh Renderer_Objects::CreateTextMesh(const std::string& text) {
    Mesh m;
    std::vector<float> vertices;
    
    // Simple stroke font - each character defined as line segments
    // Scale factor for text - reduced to be less intrusive
    const float scale = 20.0f;
    const float charSpacing = 25.0f;
    const float startX = -((text.size() * charSpacing) / 2.0f);
    
    float cursorX = startX;
    
    for (char c : text) {
        AddCharacterVertices(vertices, c, cursorX, 0.0f, scale);
        cursorX += charSpacing;
    }
    
    if (vertices.empty()) {
        // Fallback to a simple cross if text generation fails
        vertices = {
            -50.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
             50.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
             0.0f, -50.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
             0.0f,  50.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f
        };
    }
    
    glGenVertexArrays(1, &m.VAO);
    glGenBuffers(1, &m.VBO);
    glBindVertexArray(m.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m.VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
    
    // Position (xyz)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // Normal (xyz) - use as color multiplier
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)12);
    glEnableVertexAttribArray(1);
    // TexCoord (uv) - unused
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)24);
    glEnableVertexAttribArray(2);
    
    glBindVertexArray(0);
    m.vertexCount = vertices.size() / 8;
    m.textureID = 0;
    m.vertexData = nullptr;
    return m;
}

void Renderer_Objects::AddCharacterVertices(std::vector<float>& vertices, char c, float x, float y, float scale) {
    // Simple stroke definitions for common characters - using triangle strips for each line
    // Each line segment becomes a thin quad (2 triangles)
    auto addLineQuad = [&](float x1, float y1, float x2, float y2) {
        float thickness = 0.1f;
        float nx = 1.0f, ny = 0.0f, nz = 0.0f; // Red color
        
        // Calculate perpendicular offset for thickness
        float dx = x2 - x1;
        float dy = y2 - y1;
        float len = sqrt(dx*dx + dy*dy);
        if (len < 0.0001f) return;
        float ox = -dy / len * thickness;
        float oy = dx / len * thickness;
        
        // Quad vertices (2 triangles)
        // Triangle 1
        vertices.push_back(x + x1 * scale + ox); vertices.push_back(y + y1 * scale + oy); vertices.push_back(0.0f);
        vertices.push_back(nx); vertices.push_back(ny); vertices.push_back(nz);
        vertices.push_back(0.0f); vertices.push_back(0.0f);
        
        vertices.push_back(x + x2 * scale + ox); vertices.push_back(y + y2 * scale + oy); vertices.push_back(0.0f);
        vertices.push_back(nx); vertices.push_back(ny); vertices.push_back(nz);
        vertices.push_back(0.0f); vertices.push_back(0.0f);
        
        vertices.push_back(x + x1 * scale - ox); vertices.push_back(y + y1 * scale - oy); vertices.push_back(0.0f);
        vertices.push_back(nx); vertices.push_back(ny); vertices.push_back(nz);
        vertices.push_back(0.0f); vertices.push_back(0.0f);
        
        // Triangle 2
        vertices.push_back(x + x1 * scale - ox); vertices.push_back(y + y1 * scale - oy); vertices.push_back(0.0f);
        vertices.push_back(nx); vertices.push_back(ny); vertices.push_back(nz);
        vertices.push_back(0.0f); vertices.push_back(0.0f);
        
        vertices.push_back(x + x2 * scale + ox); vertices.push_back(y + y2 * scale + oy); vertices.push_back(0.0f);
        vertices.push_back(nx); vertices.push_back(ny); vertices.push_back(nz);
        vertices.push_back(0.0f); vertices.push_back(0.0f);
        
        vertices.push_back(x + x2 * scale - ox); vertices.push_back(y + y2 * scale - oy); vertices.push_back(0.0f);
        vertices.push_back(nx); vertices.push_back(ny); vertices.push_back(nz);
        vertices.push_back(0.0f); vertices.push_back(0.0f);
    };
    
    c = toupper(c);
    
    switch (c) {
        case 'M':
            addLineQuad(0, 1, 0, -1);
            addLineQuad(0, -1, 0.5, 0);
            addLineQuad(0.5, 0, 1, -1);
            addLineQuad(1, -1, 1, 1);
            break;
        case 'I':
            addLineQuad(0, 1, 0, -1);
            addLineQuad(-0.3, 1, 0.3, 1);
            addLineQuad(-0.3, -1, 0.3, -1);
            break;
        case 'S':
            addLineQuad(0.3, 1, 0, 1);
            addLineQuad(0, 1, 0, 0);
            addLineQuad(0, 0, 1, 0);
            addLineQuad(1, 0, 1, -1);
            addLineQuad(1, -1, 0.7, -1);
            break;
        case 'N':
            addLineQuad(0, -1, 0, 1);
            addLineQuad(0, 1, 1, -1);
            addLineQuad(1, -1, 1, 1);
            break;
        case 'G':
            addLineQuad(0.7, 1, 0, 1);
            addLineQuad(0, 1, 0, -1);
            addLineQuad(0, -1, 0.7, -1);
            addLineQuad(0.7, -1, 1, -1);
            addLineQuad(1, -1, 1, 0);
            addLineQuad(1, 0, 0.7, 0);
            break;
        case ':':
            addLineQuad(0, 0.3, 0, 0.3);
            addLineQuad(0, -0.3, 0, -0.3);
            break;
        case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
            // Simple box for numbers with slash
            addLineQuad(0, 1, 0, -1);
            addLineQuad(0, -1, 1, -1);
            addLineQuad(1, -1, 1, 1);
            addLineQuad(1, 1, 0, 1);
            addLineQuad(0.2, -0.8, 0.8, -0.2);
            break;
        case '_':
            addLineQuad(0, -1, 1, -1);
            break;
        default:
            // Default to a box for unknown characters
            addLineQuad(0, 1, 0, -1);
            addLineQuad(0, -1, 1, -1);
            addLineQuad(1, -1, 1, 1);
            addLineQuad(1, 1, 0, 1);
            break;
    }
}

Mesh Renderer_Objects::CreateCubeMesh() {
    Mesh m;
    std::vector<float> v = {
        -500.0f,-500.0f,-500.0f, 0,0,-1, 0,0,  500.0f, 500.0f,-500.0f, 0,0,-1, 1,1,  500.0f,-500.0f,-500.0f, 0,0,-1, 1,0,
        -500.0f,-500.0f,-500.0f, 0,0,-1, 0,0, -500.0f, 500.0f,-500.0f, 0,0,-1, 0,1,  500.0f, 500.0f,-500.0f, 0,0,-1, 1,1,
        -500.0f,-500.0f, 500.0f, 0,0, 1, 0,0,  500.0f,-500.0f, 500.0f, 0,0, 1, 1,0,  500.0f, 500.0f, 500.0f, 0,0, 1, 1,1,
        -500.0f,-500.0f, 500.0f, 0,0, 1, 0,0,  500.0f, 500.0f, 500.0f, 0,0, 1, 1,1, -500.0f, 500.0f, 500.0f, 0,0, 1, 0,1,
        -500.0f, 500.0f, 500.0f,-1,0, 0, 1,0, -500.0f, 500.0f,-500.0f,-1,0, 0, 1,1, -500.0f,-500.0f,-500.0f,-1,0, 0, 0,1,
        -500.0f, 500.0f, 500.0f,-1,0, 0, 1,0, -500.0f,-500.0f,-500.0f,-1,0, 0, 0,1, -500.0f,-500.0f, 500.0f,-1,0, 0, 0,0,
         500.0f, 500.0f, 500.0f, 1,0, 0, 1,0,  500.0f,-500.0f,-500.0f, 1,0, 0, 0,1,  500.0f, 500.0f,-500.0f, 1,0, 0, 1,1,
         500.0f, 500.0f, 500.0f, 1,0, 0, 1,0,  500.0f,-500.0f, 500.0f, 1,0, 0, 0,0,  500.0f,-500.0f,-500.0f, 1,0, 0, 0,1,
        -500.0f,-500.0f,-500.0f, 0,-1,0, 0,1,  500.0f,-500.0f,-500.0f, 0,-1,0, 1,1,  500.0f,-500.0f, 500.0f, 0,-1,0, 1,0,
        -500.0f,-500.0f,-500.0f, 0,-1,0, 0,1,  500.0f,-500.0f, 500.0f, 0,-1,0, 1,0, -500.0f,-500.0f, 500.0f, 0,-1,0, 0,0,
        -500.0f, 500.0f,-500.0f, 0, 1,0, 0,1,  500.0f, 500.0f, 500.0f, 0, 1,0, 1,0,  500.0f, 500.0f,-500.0f, 0, 1,0, 1,1,
        -500.0f, 500.0f,-500.0f, 0, 1,0, 0,1, -500.0f, 500.0f, 500.0f, 0, 1,0, 0,0,  500.0f, 500.0f, 500.0f, 0, 1,0, 1,0
    };
    glGenVertexArrays(1, &m.VAO); glGenBuffers(1, &m.VBO);
    glBindVertexArray(m.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m.VBO);
    glBufferData(GL_ARRAY_BUFFER, v.size()*4, v.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (void*)0); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, (void*)12); glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, (void*)24); glEnableVertexAttribArray(2);
    m.vertexCount = 36; m.textureID = 0; m.vertexData = nullptr;
    glBindVertexArray(0); return m;
}

std::string Renderer_Objects::FindModelFile(const std::string& modelId, bool isBuilding) {
    if (modelId.empty()) return "";

    // Helper: search one directory for exact modelId.mef match.
    auto searchOneDirExact = [&](const std::string& dirStr) -> std::string {
        if (dirStr.empty()) return "";
        std::filesystem::path modelsPath(dirStr);
        if (!std::filesystem::exists(modelsPath)) return "";

        // Exact match
        std::filesystem::path exactPath = modelsPath / (modelId + ".mef");
        if (std::filesystem::exists(exactPath)) return exactPath.string();
        return "";
    };

    // Helper: search one directory for fuzzy match by type prefix.
    auto searchOneDirFuzzy = [&](const std::string& dirStr) -> std::string {
        if (dirStr.empty()) return "";
        std::filesystem::path modelsPath(dirStr);
        if (!std::filesystem::exists(modelsPath)) return "";

        // Companion-part guard: IDs ending in _2 .. _9 (face, hands, legs, etc.)
        // must match exactly or not at all. The fuzzy fallback would otherwise
        // return the main body mesh (_01_1) for any missing companion part,
        // causing double-body / double-face rendering.
        {
            const size_t lastUs = modelId.rfind('_');
            if (lastUs != std::string::npos && lastUs + 1 < modelId.size()) {
                const char lastCh = modelId[lastUs + 1];
                if (lastCh >= '2' && lastCh <= '9') return "";
            }
        }

        // Fuzzy scan
        std::string bestMatch;
        for (const auto& entry : std::filesystem::directory_iterator(modelsPath)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".mef") continue;
            const std::string fname = entry.path().filename().string();
            const std::string stem  = entry.path().stem().string();

            // Full modelId in filename
            if (fname.find(modelId) != std::string::npos) return entry.path().string();

            // Variation fallback: require type prefix at stem start (e.g. "003_" not "1003_")
            size_t firstUnderscore = modelId.find_first_of('_');
            if (firstUnderscore != std::string::npos) {
                const std::string typeId = modelId.substr(0, firstUnderscore);
                if (stem.rfind(typeId + "_", 0) == 0) {
                    if (bestMatch.empty() || stem.find(typeId + "_01") != std::string::npos) {
                        bestMatch = entry.path().string();
                    }
                }
            }
        }
        return bestMatch;
    };

    // ─── PHASE 1: EXACT MATCHES ───────────────────────────────────────────────
    
    // 1. Search local extracted models for current level
    const std::string exeModels = Utils::GetExeDirectory() + "\\content\\models\\level" + std::to_string(current_level_);
    std::string result = searchOneDirExact(exeModels);
    if (!result.empty()) {
        Logger::Get().Log(LogLevel::DEBUG, "[Renderer_Objects] Model found locally (Exact): " + result);
        return result;
    }

    // 2. Fall back to game's native models directory for current level
    const std::string igiModels = Utils::GetIGIModelsPath(current_level_);
    result = searchOneDirExact(igiModels);
    if (!result.empty()) {
        Logger::Get().Log(LogLevel::DEBUG, "[Renderer_Objects] Model found in IGI root (Exact): " + result);
        return result;
    }

    // 3. Search other levels for exact match (cross-level references)
    for (int lvl = 1; lvl <= 14; ++lvl) {
        if (lvl == current_level_) continue;
        const std::string lvlLocal = Utils::GetExeDirectory() + "\\content\\models\\level" + std::to_string(lvl);
        result = searchOneDirExact(lvlLocal);
        if (!result.empty()) {
            Logger::Get().Log(LogLevel::DEBUG, "[Renderer_Objects] Model found in level" + std::to_string(lvl) + " local (Exact): " + result);
            return result;
        }
        const std::string lvlIgi = Utils::GetIGIModelsPath(lvl);
        result = searchOneDirExact(lvlIgi);
        if (!result.empty()) {
            Logger::Get().Log(LogLevel::DEBUG, "[Renderer_Objects] Model found in level" + std::to_string(lvl) + " IGI (Exact): " + result);
            return result;
        }
    }

    // 4. Search common location0 assets
    const std::string commonLocal = Utils::GetExeDirectory() + "\\content\\models\\common";
    result = searchOneDirExact(commonLocal);
    if (!result.empty()) {
        Logger::Get().Log(LogLevel::DEBUG, "[Renderer_Objects] Model found in common local (Exact): " + result);
        return result;
    }
    const std::string commonIgi = Utils::GetIGIRootPath() + "\\missions\\location0\\common\\models";
    result = searchOneDirExact(commonIgi);
    if (!result.empty()) {
        Logger::Get().Log(LogLevel::DEBUG, "[Renderer_Objects] Model found in common IGI (Exact): " + result);
        return result;
    }

    // 5. Search the flat IGI content/models directory produced by --extract-level.
    //    All levels are extracted to a single flat dir (D:/IGI1/content/models/) alongside
    //    the common/ and level1/ subdirs, so this covers levels 2-14 model MEFs.
    const std::string igiContentModels = Utils::GetIGIRootPath() + "\\content\\models";
    result = searchOneDirExact(igiContentModels);
    if (!result.empty()) {
        Logger::Get().Log(LogLevel::DEBUG, "[Renderer_Objects] Model found in IGI content/models flat dir (Exact): " + result);
        return result;
    }

    // ─── PHASE 2: FUZZY FALLBACK (Only run if exact match fails everywhere) ───

    // 1. Current level local
    result = searchOneDirFuzzy(exeModels);
    if (!result.empty()) {
        Logger::Get().Log(LogLevel::DEBUG, "[Renderer_Objects] Model found locally (Fuzzy): " + result);
        return result;
    }

    // 2. Current level IGI root
    result = searchOneDirFuzzy(igiModels);
    if (!result.empty()) {
        Logger::Get().Log(LogLevel::DEBUG, "[Renderer_Objects] Model found in IGI root (Fuzzy): " + result);
        return result;
    }

    // Lazy On-Demand Extraction check
    EnsureGlobalTextureMapLoaded();
    auto mit = model_level_map_.find(modelId);
    if (mit != model_level_map_.end()) {
        int targetLvl = mit->second;
        if (targetLvl != current_level_) {
            Logger::Get().Log(LogLevel::INFO,
                "[Renderer_Objects] Lazy extracting textures/models on-demand for level " + std::to_string(targetLvl) +
                " to resolve cross-level model: " + modelId);
            AssetExtractor::EnsureLevelAssets(targetLvl, Utils::GetIGIRootPath(), Utils::GetExeDirectory());
        }
    }

    // 3. Other levels fuzzy
    for (int lvl = 1; lvl <= 14; ++lvl) {
        if (lvl == current_level_) continue;
        const std::string lvlLocal = Utils::GetExeDirectory() + "\\content\\models\\level" + std::to_string(lvl);
        result = searchOneDirFuzzy(lvlLocal);
        if (!result.empty()) {
            Logger::Get().Log(LogLevel::DEBUG, "[Renderer_Objects] Model found in level" + std::to_string(lvl) + " local (Fuzzy): " + result);
            return result;
        }
        const std::string lvlIgi = Utils::GetIGIModelsPath(lvl);
        result = searchOneDirFuzzy(lvlIgi);
        if (!result.empty()) {
            Logger::Get().Log(LogLevel::DEBUG, "[Renderer_Objects] Model found in level" + std::to_string(lvl) + " IGI (Fuzzy): " + result);
            return result;
        }
    }

    // 4. Common fuzzy
    result = searchOneDirFuzzy(commonLocal);
    if (!result.empty()) {
        Logger::Get().Log(LogLevel::DEBUG, "[Renderer_Objects] Model found in common local (Fuzzy): " + result);
        return result;
    }
    result = searchOneDirFuzzy(commonIgi);
    if (!result.empty()) {
        Logger::Get().Log(LogLevel::DEBUG, "[Renderer_Objects] Model found in common IGI (Fuzzy): " + result);
        return result;
    }

    Logger::Get().Log(LogLevel::ERR,
        "[Renderer_Objects] Model NOT FOUND: '" + modelId +
        "' — searched level " + std::to_string(current_level_) +
        ", all other levels, and common folder.");
    return "";
}


// ─── InitSelectionBox ────────────────────────────────────────────────────────
void Renderer_Objects::InitSelectionBox() {
    // Create a simple cube wireframe
    float boxSize = WORLD_UNITS_PER_METER * 2.0f; // 2m box
    float halfSize = boxSize * 0.5f;
    
    float vertices[] = {
        // Front face
        -halfSize, -halfSize,  halfSize,
         halfSize, -halfSize,  halfSize,
         halfSize,  halfSize,  halfSize,
        -halfSize,  halfSize,  halfSize,
        // Back face
        -halfSize, -halfSize, -halfSize,
         halfSize, -halfSize, -halfSize,
         halfSize,  halfSize, -halfSize,
        -halfSize,  halfSize, -halfSize
    };
    
    glGenVertexArrays(1, &selection_vao_);
    glGenBuffers(1, &selection_vbo_);
    
    glBindVertexArray(selection_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, selection_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    
    glBindVertexArray(0);
}

// ─── DrawSelectionBox ─────────────────────────────────────────────────────────
void Renderer_Objects::DrawSelectionBox(const LevelObject& obj, GLuint ubo_mats, const glm::vec4& color) {
    if (selection_vao_ == 0) {
        InitSelectionBox();
    }
    
    // Simple shader for solid color
    static const char* simple_vert = R"(
#version 330 core
layout(std140) uniform Matrices {
    mat4 u_unused1;
    mat4 u_unused2;
    mat4 u_mvp;
};
uniform mat4 u_model;
layout(location = 0) in vec3 a_pos;
void main() {
    gl_Position = u_mvp * u_model * vec4(a_pos, 1.0);
}
)";

    static const char* simple_frag = R"(
#version 330 core
uniform vec4 u_color;
out vec4 fragColor;
void main() {
    fragColor = u_color;
}
)";
    
    static GLuint simple_shader = 0;
    if (simple_shader == 0) {
        GLuint vert = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vert, 1, &simple_vert, nullptr);
        glCompileShader(vert);
        
        GLuint frag = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(frag, 1, &simple_frag, nullptr);
        glCompileShader(frag);
        
        simple_shader = glCreateProgram();
        glAttachShader(simple_shader, vert);
        glAttachShader(simple_shader, frag);
        glLinkProgram(simple_shader);
        
        glDeleteShader(vert);
        glDeleteShader(frag);
    }
    
    glUseProgram(simple_shader);
    glBindBufferBase(GL_UNIFORM_BUFFER, ubo_binding_point_, ubo_mats);
    
    // Build model matrix for selection box (slightly larger than object)
    // Cast dvec3 to vec3 for rendering
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(obj.pos));
    model = glm::rotate(model, static_cast<float>(obj.rot.z), glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::rotate(model, static_cast<float>(obj.rot.y), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, static_cast<float>(obj.rot.x), glm::vec3(1.0f, 0.0f, 0.0f));

    // For weapons, they are authored with Y-up (legacy OBJ style), so they stand upright.
    // We rotate them by 90 degrees on Pitch (X axis) to lay them flat on the ground.
    bool isWeapon = IsWeaponModel(obj.modelId) || obj.type == "GunPickup" || obj.type == "AmmoPickup";
    if (isWeapon) {
        model = glm::rotate(model, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    }

    model = glm::scale(model, glm::vec3(obj.scale * 1.2f)); // 20% larger
    
    GLint loc_model = glGetUniformLocation(simple_shader, "u_model");
    glUniformMatrix4fv(loc_model, 1, GL_FALSE, glm::value_ptr(model));
    
    GLint loc_color = glGetUniformLocation(simple_shader, "u_color");
    glUniform4fv(loc_color, 1, glm::value_ptr(color));
    
    // Draw wireframe
    glBindVertexArray(selection_vao_);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glDisable(GL_CULL_FACE);
    
    // Draw edges
    GLuint indices[] = {
        0,1, 1,2, 2,3, 3,0, // Front face
        4,5, 5,6, 6,7, 7,4, // Back face
        0,4, 1,5, 2,6, 3,7  // Connecting edges
    };
    
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0); // Unbind any existing EBO
    glDrawElements(GL_LINES, 24, GL_UNSIGNED_INT, indices);
    
    // Reset state
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glEnable(GL_CULL_FACE);
    glBindVertexArray(0);
    glUseProgram(0);
}

// ─── DrawAttachmentsForSpline ─────────────────────────────────────────────────
// Public entry point called by Renderer_Splines once per SEGMENT INTERVAL.
// unscaledWorldMat = translate + rotate at the segment midpoint (no scale).
// leafScale.x = intervalLen (stretch ATTA to cover the full segment in X).
// leafScale.y/z = 40.96 (normal world scale for width/height).
void Renderer_Objects::DrawAttachmentsForSpline(
    const std::string& modelId, bool isBuilding,
    const glm::mat4& unscaledWorldMat, GLuint ubo_mats,
    glm::vec3 leafScale)
{
    EnsureWindowModelIdsLoaded();

    glUseProgram(shader_program_);
    glBindBufferBase(GL_UNIFORM_BUFFER, ubo_binding_point_, ubo_mats);

    GLint loc_model    = glGetUniformLocation(shader_program_, "u_model");
    GLint loc_dirlight = glGetUniformLocation(shader_program_, "u_dirlight");
    GLint loc_ambient  = glGetUniformLocation(shader_program_, "u_ambient");
    GLint loc_useTex   = glGetUniformLocation(shader_program_, "u_useTexture");
    GLint loc_tex      = glGetUniformLocation(shader_program_, "u_texture");
    GLint loc_alpha    = glGetUniformLocation(shader_program_, "u_alpha");

    glUniform1f(loc_alpha, 1.0f);

    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-2.0f, -2.0f);

    // Opaque pass
    {
        std::unordered_set<std::string> drawn;
        DrawAttachmentsRecursive(modelId, modelId, isBuilding, unscaledWorldMat, /*isTransparentPass=*/false,
                                 loc_model, loc_dirlight, loc_ambient, loc_useTex, loc_tex, loc_alpha, drawn, leafScale);
    }

    // Transparent pass (windows / glass)
    {
        std::unordered_set<std::string> drawn;
        DrawAttachmentsRecursive(modelId, modelId, isBuilding, unscaledWorldMat, /*isTransparentPass=*/true,
                                 loc_model, loc_dirlight, loc_ambient, loc_useTex, loc_tex, loc_alpha, drawn, leafScale);
    }

    // Restore blend/depth state — transparent pass may leave them dirty.
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glUniform1f(loc_alpha, 1.0f);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glUseProgram(0);
}




