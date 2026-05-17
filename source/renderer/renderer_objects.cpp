#include "pch.h"
#include "renderer_objects.h"
#include <iostream>
#include <filesystem>
#include <unordered_set>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "logger.h"


bool Renderer_Objects::IsSkippedModelId(const std::string& modelId) {
    if (modelId.empty()) return false;

    // Use a static set for O(1) exact matches
    static const std::unordered_set<std::string> skippedIds = {
        // ── Fences & Gates (terrain-snapping glitches) ──
        "303_01_1", "303_02_1", "303_03_1", "304_01_1",
        "302_01_1", "331_01_1",
        "341_01_1", "341_02_1", "341_03_1", "341_04_1",
        "341_05_1", "341_06_1", "341_07_1",
        "366_01_1", "370_01_1", "370_02_1", "370_03_1", "370_04_1",

        // ── Wires & Poles ──
        "320", "338", "355", "307", "308", "312", "203",

        // ── Holders / Brackets ──
        "373", "615", "252",

        // ── Collision / Invisible Objects ──
        "colbox", "colbox2","colbox4" "colbox66"
    };

    if (skippedIds.count(modelId) > 0) return true;

    // Robust prefix matching: skip if modelId starts with any skip ID
    for (const auto& sid : skippedIds) {
        if (modelId.find(sid) == 0) return true; 
    }
    return false;
}

// ─── Helpers ──────────────────────────────────────────────────────────────────
static bool IsFenceModel(const std::string& nameOrId) {
    std::string upper = nameOrId;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    return upper.find("FENCE") != std::string::npos ||
           upper.find("GATE") != std::string::npos ||
           upper.find("POLE_WIRED") != std::string::npos ||
           upper.find("WIRE") != std::string::npos;
}

// Known fence/gate model IDs from IGIModelsLevel.json
static bool IsFenceModelId(const std::string& modelId) {
    static const std::unordered_set<std::string> fenceIds = {
        "303_01_1", "303_02_1", "303_03_1", "303_04_1", "304_01_1",
        "302_01_1", "331_01_1",
        "338_01_1",
        "341_01_1", "341_02_1", "341_03_1", "341_04_1",
        "341_05_1", "341_06_1", "341_07_1",
        "366_01_1"
    };
    if (fenceIds.count(modelId) > 0) return true;
    // Handle partial IDs from QSC (e.g. "303", "303_01")
    for (const auto& fid : fenceIds) {
        if (fid.find(modelId) == 0 || modelId.find(fid) == 0) return true;
    }
    return false;
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

out vec4 fragColor;

void main() {
    vec3 N = normalize(v_normal);
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.5));
    float diff = max(dot(N, lightDir), 0.0);

    vec3 viewDir = normalize(vec3(0.0, 1.0, 1.0));
    vec3 halfVec = normalize(lightDir + viewDir);
    float spec = pow(max(dot(N, halfVec), 0.0), 32.0) * 0.25;

    vec3 light = u_ambient + u_dirlight * (diff + spec);

    vec4 texColor = (u_useTexture != 0) ? texture(u_texture, v_uv) : vec4(1.0, 1.0, 1.0, 1.0);

    if (u_useTexture == 0) {
        fragColor = vec4(light * texColor.rgb, 1.0);
    } else {
        fragColor = vec4(light * texColor.rgb, texColor.a);
    }

    if (fragColor.a < 0.1) discard;
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

// ─── Shutdown ─────────────────────────────────────────────────────────────────
void Renderer_Objects::Shutdown() {
    for (auto& pair : mesh_cache_)
        destroyModel(pair.second);
    mesh_cache_.clear();

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
                            const std::vector<LevelObject>& objects, int selected_object_index, int hover_object_index, int draw_parts)
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
    
    if (!shader_program_) return;

    // Bind our object shader
    glUseProgram(shader_program_);

    // Bind the shared UBO (same buffer terrain uses — contains proj + view)
    glBindBufferBase(GL_UNIFORM_BUFFER, ubo_binding_point_, ubo_mats);

    // OpenGL state
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
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

        // TODO: Temporarily skip loading and rendering fence/gate/wire objects and colbox66 due to snapping issues.
        // Fences are floating or misaligned on terrain. Need to implement proper Z-offset calculation
        // for fence models that accounts for their unique geometry (posts vs wires).
        // colbox66 is a collision box model that should not be rendered.
        // GitHub issue to be created for tracking this fix.
        if (IsFenceModelId(obj.modelId) || IsFenceModel(obj.name) || IsFenceModel(obj.modelId) ||
            obj.modelId == "colbox" || obj.name == "colbox" || 
            obj.modelId == "colbox2" || obj.name == "colbox2" ||
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

        const bool isFence = IsFenceModelId(obj.modelId) || IsFenceModel(obj.name) || IsFenceModel(obj.modelId);

        // 2. Apply IGI rotations (Yaw, Pitch, Roll)
        // IGI rotation order: Yaw (Z), then Pitch (X), then Roll (Y)
        model = glm::rotate(model, (float)obj.rot.z, glm::vec3(0.0f, 0.0f, 1.0f)); // Yaw
        model = glm::rotate(model, (float)obj.rot.x, glm::vec3(1.0f, 0.0f, 0.0f)); // Pitch
        model = glm::rotate(model, (float)obj.rot.y, glm::vec3(0.0f, 1.0f, 0.0f)); // Roll

        // 3. Scale
        model = glm::scale(model, glm::vec3(total_scale));

        // 4. Convert OBJ Y-up to IGI Z-up (90 degree X rotation)
        model = glm::rotate(model, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)); 

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

                // Skip large untextured foundations in mixed meshes (but NOT fences/gates)
                if (!isFence && mixedMesh && sub.textureID == 0 && sub.vertexCount > maxTexturedVerts) {
                    continue;
                }

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

                // Enable blending for alpha BLEND materials
                bool blendEnabled = false;
                if (sub.alphaMode == 2) { // BLEND
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                    blendEnabled = true;
                }
                glBindVertexArray(sub.VAO);
                glDrawArrays(GL_TRIANGLES, 0, sub.vertexCount);
                if (blendEnabled) {
                    glDisable(GL_BLEND);
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
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, mesh.textureID);
                glUniform1i(loc_tex, 0);
            } else {
                glUniform1i(loc_useTex, 0);
            }
            renderModel(mesh);
        }
    }

    // Always reset polygon mode after draw
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    
    // Unbind shader
    glUseProgram(0);
}

float Renderer_Objects::GetMeshZOffset(const std::string& modelId, bool isBuilding) {
    // TODO: Temporarily skip fence/gate/wire objects and colbox66 due to snapping issues.
    // Fences are floating or misaligned on terrain. Need to implement proper Z-offset calculation.
    // colbox66 is a collision box model that should not be rendered.
    // See TODO in Draw function for more details.
    if (IsFenceModelId(modelId) || modelId == "colbox66") {
        return 0.0f;
    }

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

// ─── GetOrLoadMesh ────────────────────────────────────────────────────────────
Mesh Renderer_Objects::GetOrLoadMesh(const std::string& modelId, bool isBuilding) {
    std::string cacheKey = std::to_string(current_level_) + ":" + (isBuilding ? "building:" : "object:") + modelId;

    // Return cached mesh if already loaded
    auto it = mesh_cache_.find(cacheKey);
    if (it != mesh_cache_.end())
        return it->second;

    // Find the file on disk
    std::string filepath = FindModelFile(modelId, isBuilding);
    if (filepath.empty()) {
        Logger::Get().Log(LogLevel::WARNING, "[Renderer_Objects] Model search FAILED for ID: " + modelId + ". Skipping render.");
        Mesh emptyMesh;
        emptyMesh.vertexCount = 0;
        mesh_cache_[cacheKey] = emptyMesh;
        return mesh_cache_[cacheKey];
    }


    // Load and cache
    try {
        Mesh mesh = loadObjModel(filepath, "");
        mesh_cache_[cacheKey] = mesh;
        Logger::Get().Log(LogLevel::INFO, "[Renderer_Objects] Success: Loaded model '" + modelId + "' from " + filepath + " (" + std::to_string(mesh.vertexCount) + " vertices)");
        return mesh;

    } catch (const std::exception& e) {
        Logger::Get().Log(LogLevel::ERR, "[Renderer_Objects] Load FAILED for " + modelId + ": " + std::string(e.what()));
        Mesh emptyMesh;
        mesh_cache_[cacheKey] = emptyMesh;
        return mesh_cache_[cacheKey];
    }

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
    std::string levelDir = "level" + std::to_string(current_level_);
    
    // Try AI folder first if this might be an AI model (AI models have IDs starting with 000-019)
    bool isAIModel = false;
    if (modelId.size() >= 3) {
        std::string prefix = modelId.substr(0, 3);
        int prefixNum = 0;
        try {
            prefixNum = std::stoi(prefix);
            if (prefixNum >= 0 && prefixNum <= 19) {
                isAIModel = true;
            }
        } catch (...) {
            // Not a number, not an AI model
        }
    }
    
    // Search in AI folder if it's an AI model
    if (isAIModel) {
        std::string aiBase = g_folders.ai_folder_;
        for (char& c : aiBase) {
            if (c == '/') c = '\\';
        }
        
        std::filesystem::path aiBasePath(aiBase);
        std::filesystem::path aiLevelPath = aiBasePath / levelDir;
        
        // Try exact match in AI folder
        std::vector<std::filesystem::path> aiSearchPaths = { 
            aiLevelPath / (modelId + ".glb"),
            aiLevelPath / (modelId + ".obj")
        };
        for (const auto& path : aiSearchPaths) {
            std::string pathStr = path.string();
            if (std::filesystem::exists(pathStr)) {
                return pathStr;
            }
        }
        
        // Try partial match in AI folder
        if (std::filesystem::exists(aiLevelPath)) {
            for (const auto& entry : std::filesystem::directory_iterator(aiLevelPath)) {
                if (!entry.is_regular_file()) continue;
                std::string fname = entry.path().filename().string();
                std::string ext = entry.path().extension().string();
                if (ext != ".glb" && ext != ".obj") continue;

                if (fname.find(modelId) != std::string::npos) {
                    return entry.path().string();
                }
            }
        }
    }
    
    // Fall back to buildings/objects folder
    std::string objectsBase = isBuilding ? g_folders.buildings_folder_ : g_folders.objects_folder_;
    
    // Convert forward slashes to backslashes for Windows
    for (char& c : objectsBase) {
        if (c == '/') c = '\\';
    }
    
    // Use std::filesystem::path for proper path handling
    std::filesystem::path basePath(objectsBase);
    std::filesystem::path levelPath = basePath / levelDir;
    
    // 1. Try exact match in level-specific folder (.glb first, then .obj fallback)
    std::vector<std::filesystem::path> searchPaths = { 
        levelPath / (modelId + ".glb"),
        levelPath / (modelId + ".obj")
    };
    for (const auto& path : searchPaths) {
        if (std::filesystem::exists(path)) return path.string();
    }
    
    // 1b. Fallback to OTHER folder (buildings <-> objects)
    std::string otherBase = isBuilding ? g_folders.objects_folder_ : g_folders.buildings_folder_;
    for (char& c : otherBase) if (c == '/') c = '\\';
    std::filesystem::path otherLevelPath = std::filesystem::path(otherBase) / levelDir;
    
    std::vector<std::filesystem::path> otherSearchPaths = { 
        otherLevelPath / (modelId + ".glb"),
        otherLevelPath / (modelId + ".obj")
    };
    for (const auto& path : otherSearchPaths) {
        if (std::filesystem::exists(path)) {
            Logger::Get().Log(LogLevel::INFO, "[Renderer_Objects] Found " + modelId + " in ALTERNATE folder: " + path.string());
            return path.string();
        }
    }
    
    // 2. Try partial match (wildcard search in level-specific folder)
    if (std::filesystem::exists(levelPath)) {
        // Try BaseID (stripped of _1) if applicable
        std::string baseId = modelId;
        if (baseId.size() > 2 && baseId.substr(baseId.size() - 2) == "_1") {
            baseId = baseId.substr(0, baseId.size() - 2);
        }

        std::string bestMatch = "";
        for (const auto& entry : std::filesystem::directory_iterator(levelPath)) {
            if (!entry.is_regular_file()) continue;
            std::string fname = entry.path().filename().string();
            std::string ext = entry.path().extension().string();
            if (ext != ".glb" && ext != ".obj") continue;

            // Exact match in filename
            if (fname.find(modelId) != std::string::npos) return entry.path().string();
            
            // Fuzzy match: if specific variation missing, look for variation 01
            // e.g. 300_03_1 -> 300_01_1
            size_t firstUnderscore = modelId.find_first_of('_');
            if (firstUnderscore != std::string::npos) {
                std::string typeId = modelId.substr(0, firstUnderscore);
                if (fname.find(typeId + "_01") != std::string::npos) {
                    bestMatch = entry.path().string();
                }
                if (bestMatch.empty() && fname.rfind(typeId + "_", 0) == 0) {
                    bestMatch = entry.path().string();
                }
            }
            
            // Last resort: any file containing the type prefix
            if (bestMatch.empty() && fname.find(baseId) != std::string::npos) {
                bestMatch = entry.path().string();
            }
        }
        if (!bestMatch.empty()) {
            return bestMatch;
        }
    }
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