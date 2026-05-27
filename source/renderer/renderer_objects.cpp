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
#include <sstream>


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
    fragColor = vec4(light * texColor.rgb, finalAlpha);

    if (u_alpha >= 0.99 && texColor.a < 0.5) discard;
    if (fragColor.a < 0.05) discard;
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


    Logger::Get().Log(LogLevel::INFO, "[Renderer_Objects] Init OK.");

    return true;
}

// ─── ClearCaches ──────────────────────────────────────────────────────────────
void Renderer_Objects::ClearCaches() {
    Logger::Get().Log(LogLevel::INFO, "[Renderer_Objects] Clearing all level caches...");
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
    
    global_texture_map_.clear();
    global_texture_map_loaded_ = false;
    texture_map_level_ = -1;
    window_model_ids_.clear();
    window_ids_loaded_ = false;
    portal_distances_.clear();
    portal_distances_loaded_ = false;
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
}

// ─── Draw ─────────────────────────────────────────────────────────────────────
void Renderer_Objects::Draw(GLuint ubo_mats, bool overlay_wireframe,
                            const std::vector<LevelObject>& objects, int selected_object_index, int hover_object_index, int draw_parts,
                            const glm::vec3& camera_pos)
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
    glUniform1f(loc_alpha, 1.0f); // default: fully opaque
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
        model = glm::rotate(model, (float)obj.rot.z, glm::vec3(0.0f, 0.0f, 1.0f)); // Yaw
        model = glm::rotate(model, (float)obj.rot.x, glm::vec3(1.0f, 0.0f, 0.0f)); // Pitch
        model = glm::rotate(model, (float)obj.rot.y, glm::vec3(0.0f, 1.0f, 0.0f)); // Roll

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
        if (obj.isBuilding) {
            bool hasAnyTexture = (mesh.textureID > 0);
            if (!hasAnyTexture) {
                for (const auto& sub : mesh.subMeshes) {
                    if (sub.textureID > 0) { hasAnyTexture = true; break; }
                }
            }
            if (!hasAnyTexture) {
                const std::string attKey = std::to_string(current_level_) + ":building:" + obj.modelId;
                skipHullRender = attachment_cache_.count(attKey) > 0;
            }
        }

        // Is this a window/glass model? If so, render the whole mesh semi-transparent.
        const bool isWindowModel = window_model_ids_.count(obj.modelId) > 0;
        if (!skipHullRender && isWindowModel == isTransparentPass) {
            if (isWindowModel) {
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glUniform1f(loc_alpha, 0.4f);
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

                    if (sub.textureID > 0) {
                        // Textured submesh: neutral lighting so texture looks natural
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

            if (isWindowModel) {
                glDisable(GL_BLEND);
                glUniform1f(loc_alpha, 1.0f);
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
        if (obj.isBuilding && isCloseEnough) {
            std::string attCacheKey = std::to_string(current_level_) + ":building:" + obj.modelId;
            if (attachment_cache_.find(attCacheKey) != attachment_cache_.end()) {
                glEnable(GL_POLYGON_OFFSET_FILL);
                glPolygonOffset(-2.0f, -2.0f); // Prevent z-fighting with hull walls

                // Build the root building's unscaled world matrix (translate + rotate)
                glm::mat4 parentRot(1.0f);
                parentRot = glm::rotate(parentRot, (float)obj.rot.z, glm::vec3(0,0,1));
                parentRot = glm::rotate(parentRot, (float)obj.rot.x, glm::vec3(1,0,0));
                parentRot = glm::rotate(parentRot, (float)obj.rot.y, glm::vec3(0,1,0));

                glm::mat4 rootWorldMat(1.0f);
                rootWorldMat = glm::translate(rootWorldMat, glm::vec3((float)obj.pos.x, (float)obj.pos.y, (float)obj.pos.z));
                rootWorldMat = rootWorldMat * parentRot;

                std::unordered_set<std::string> drawn;
                DrawAttachmentsRecursive(obj.modelId, rootWorldMat, isTransparentPass,
                                          loc_model, loc_dirlight, loc_ambient,
                                          loc_useTex, loc_tex, loc_alpha, drawn);

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
    const std::string& parentModelId, const glm::mat4& parentWorldMat,
    bool isTransparentPass, GLint loc_model, GLint loc_dirlight,
    GLint loc_ambient, GLint loc_useTex, GLint loc_tex, GLint loc_alpha,
    std::unordered_set<std::string>& drawn)
{
    std::string attCacheKey = std::to_string(current_level_) + ":building:" + parentModelId;
    auto ait = attachment_cache_.find(attCacheKey);
    if (ait == attachment_cache_.end()) return;

    for (const auto &att : ait->second) {
        // Find the mesh
        std::string subKey = std::to_string(current_level_) + ":building:" + att.modelId;
        auto sit = mesh_cache_.find(subKey);
        if (sit == mesh_cache_.end() || sit->second.vertexCount == 0) {
            subKey = std::to_string(current_level_) + ":object:" + att.modelId;
            sit = mesh_cache_.find(subKey);
        }
        if (sit == mesh_cache_.end() || sit->second.vertexCount == 0) continue;
        const Mesh &subMesh = sit->second;

        // Build the ATTA local rotation matrix (DirectX row-major → GLM column-major)
        glm::mat4 attLocalRot(
            att.r[0], att.r[1], att.r[2], 0.f,
            att.r[3], att.r[4], att.r[5], 0.f,
            att.r[6], att.r[7], att.r[8], 0.f,
            0.f,      0.f,      0.f,      1.f
        );

        // ATTA offset is relative to parent — transform through parent's world matrix
        glm::vec3 localOff(att.px, att.py, att.pz);
        glm::vec3 worldPos = glm::vec3(parentWorldMat * glm::vec4(localOff, 1.f));

        // Extract parent rotation (upper-left 3x3 of the unscaled parent mat)
        glm::mat4 parentRot = parentWorldMat;
        parentRot[3] = glm::vec4(0.f, 0.f, 0.f, 1.f); // zero out translation

        // Unscaled world matrix for this attachment (used by children)
        glm::mat4 childWorldMat(1.0f);
        childWorldMat = glm::translate(childWorldMat, worldPos);
        childWorldMat = childWorldMat * parentRot * attLocalRot;

        // Scaled model matrix for actual GL draw
        glm::mat4 attModel = glm::scale(childWorldMat, glm::vec3(40.96f));

        glUniformMatrix4fv(loc_model, 1, GL_FALSE, glm::value_ptr(attModel));

        // Window/glass transparency check
        const bool attIsWindow = window_model_ids_.count(att.modelId) > 0;
        if (attIsWindow != isTransparentPass) {
            // Still recurse into children — they may be non-window
            std::string childKey = parentModelId + ">" + att.modelId;
            if (drawn.insert(childKey).second) {
                DrawAttachmentsRecursive(att.modelId, childWorldMat, isTransparentPass,
                                          loc_model, loc_dirlight, loc_ambient,
                                          loc_useTex, loc_tex, loc_alpha, drawn);
            }
            continue;
        }

        if (attIsWindow) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glUniform1f(loc_alpha, 0.4f);
        } else {
            glUniform1f(loc_alpha, 1.0f);
        }

        // Draw sub-model submeshes
        if (!subMesh.subMeshes.empty()) {
            for (const auto &sub : subMesh.subMeshes) {
                if (sub.VAO == 0 || sub.vertexCount == 0) continue;
                if (sub.textureID > 0) {
                    glUniform3f(loc_dirlight, 0.6f, 0.6f, 0.6f);
                    glUniform3f(loc_ambient,  0.4f, 0.4f, 0.4f);
                    glUniform1i(loc_useTex, 1);
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, sub.textureID);
                    glUniform1i(loc_tex, 0);
                } else {
                    glUniform3f(loc_dirlight, 0.6f, 0.6f, 0.6f);
                    glUniform3f(loc_ambient,  0.4f, 0.4f, 0.4f);
                    glUniform1i(loc_useTex, 0);
                }
                glBindVertexArray(sub.VAO);
                glDrawArrays(GL_TRIANGLES, 0, sub.vertexCount);
            }
            glBindVertexArray(0);
        } else if (subMesh.textureID > 0) {
            glUniform3f(loc_dirlight, 0.6f, 0.6f, 0.6f);
            glUniform3f(loc_ambient,  0.4f, 0.4f, 0.4f);
            glUniform1i(loc_useTex, 1);
            GL_BindTexture2D(0, subMesh.textureID);
            glUniform1i(loc_tex, 0);
            renderModel(subMesh);
        } else {
            glUniform3f(loc_dirlight, 0.6f, 0.6f, 0.6f);
            glUniform3f(loc_ambient,  0.4f, 0.4f, 0.4f);
            glUniform1i(loc_useTex, 0);
            renderModel(subMesh);
        }

        if (attIsWindow) {
            glDisable(GL_BLEND);
            glUniform1f(loc_alpha, 1.0f);
        }

        // Recurse into this attachment's own children
        std::string childKey = parentModelId + ">" + att.modelId;
        if (drawn.insert(childKey).second) {
            DrawAttachmentsRecursive(att.modelId, childWorldMat, isTransparentPass,
                                      loc_model, loc_dirlight, loc_ambient,
                                      loc_useTex, loc_tex, loc_alpha, drawn);
        }
    }
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

    const std::string texturePath = FindTextureFile(textureId);
    if (texturePath.empty()) {
        Logger::Get().Log(LogLevel::WARNING, "[TEX Native] Texture search FAILED for ID: " + textureId);
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
        LogLevel::DEBUG,
        "[TEX Native] Decoded textureId=" + textureId +
        " path=" + texturePath +
        " width=" + std::to_string(pic->width_) +
        " height=" + std::to_string(pic->height_) +
        " frames=" + std::to_string(pics.num_pic_));

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

    // If it's a DAT miss (not explicitly in dat), fallback to self name first.
    bool isDatMiss = textureIds.empty();
    if (isDatMiss) {
        textureIds = { modelId };
    }

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
        }
    }

    if (textureIds.empty()) {
        Logger::Get().Log(LogLevel::INFO, "[TEX Native] No textures mapped for modelId=" + modelId);
        return;
    }

    std::vector<GLuint> textures;
    textures.reserve(textureIds.size());
    for (const auto& textureId : textureIds) {
        textures.push_back(GetOrLoadTexture(textureId));
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

            if (textures.size() == 1) {
                // Single-texture model: apply the same texture to every submesh
                // (covers building floors, bone model parts, etc.)
                texture = textures[0];
            } else if (matSlot >= 0 && static_cast<size_t>(matSlot) < textures.size()) {
                // Defer to materialSlot lookup from MEF render block data
                texture = textures[matSlot];
            } else if (matSlot > 0 && !textures.empty()) {
                // materialSlot is out of range — wrap it (handles 1-based MEF slots and
                // sub-models whose slots reference the parent's texture list by index).
                texture = textures[static_cast<size_t>(matSlot) % textures.size()];
                Logger::Get().Log(
                    LogLevel::WARNING,
                    "[TEX Native] materialSlot out of range, wrapping for modelId=" + modelId +
                    " submeshIndex=" + std::to_string(i) +
                    " materialSlot=" + std::to_string(matSlot) +
                    " textureCount=" + std::to_string(textures.size()) +
                    " wrappedSlot=" + std::to_string(static_cast<size_t>(matSlot) % textures.size()));
            } else if (i < textures.size()) {
                // Sequential index fallback for materialSlot == -1 (not assigned)
                texture = textures[i];
            } else {
                texture = fallbackTexture;
            }

            mesh.subMeshes[i].textureID = texture;
            if (texture) {
                ++assigned;
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

    // Helper: search one directory for modelId.mef (exact, then fuzzy by type prefix).
    auto searchOneDir = [&](const std::string& dirStr) -> std::string {
        if (dirStr.empty()) return "";
        std::filesystem::path modelsPath(dirStr);
        if (!std::filesystem::exists(modelsPath)) return "";

        // Exact match
        std::filesystem::path exactPath = modelsPath / (modelId + ".mef");
        if (std::filesystem::exists(exactPath)) return exactPath.string();

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

    // 1. Search local extracted models ({exeDir}\models\level{N})
    const std::string exeModels = Utils::GetExeDirectory() + "\\content\\models\\level" + std::to_string(current_level_);
    std::string result = searchOneDir(exeModels);
    if (!result.empty()) {
        Logger::Get().Log(LogLevel::DEBUG, "[Renderer_Objects] Model found locally: " + result);
        return result;
    }

    // 2. Fall back to game's native models directory for current level
    const std::string igiModels = Utils::GetIGIModelsPath(current_level_);
    result = searchOneDir(igiModels);
    if (!result.empty()) {
        Logger::Get().Log(LogLevel::DEBUG, "[Renderer_Objects] Model found in IGI root: " + result);
        return result;
    }

    // Lazy On-Demand Extraction for cross-level model references
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

    // 3. Search all other level directories (local extracted, then IGI root) —
    //    sub-models referenced by ATTA may live in a different level's directory.
    for (int lvl = 1; lvl <= 14; ++lvl) {
        if (lvl == current_level_) continue;
        const std::string lvlLocal = Utils::GetExeDirectory() + "\\content\\models\\level" + std::to_string(lvl);
        result = searchOneDir(lvlLocal);
        if (!result.empty()) {
            Logger::Get().Log(LogLevel::DEBUG,
                "[Renderer_Objects] Model found in level" + std::to_string(lvl) + " local: " + result);
            return result;
        }
        const std::string lvlIgi = Utils::GetIGIModelsPath(lvl);
        result = searchOneDir(lvlIgi);
        if (!result.empty()) {
            Logger::Get().Log(LogLevel::DEBUG,
                "[Renderer_Objects] Model found in level" + std::to_string(lvl) + " IGI: " + result);
            return result;
        }
    }

    // 4. Search common location0 assets — extracted cache then raw game dir.
    const std::string commonLocal = Utils::GetExeDirectory() + "\\content\\models\\common";
    result = searchOneDir(commonLocal);
    if (!result.empty()) {
        Logger::Get().Log(LogLevel::DEBUG,
            "[Renderer_Objects] Model found in common (local): " + result);
        return result;
    }
    const std::string commonIgi = Utils::GetIGIRootPath() + "\\missions\\location0\\common\\models";
    result = searchOneDir(commonIgi);
    if (!result.empty()) {
        Logger::Get().Log(LogLevel::DEBUG,
            "[Renderer_Objects] Model found in common (IGI): " + result);
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
