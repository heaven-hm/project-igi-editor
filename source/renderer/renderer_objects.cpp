#include "pch.h"
#include "renderer_objects.h"
#include <iostream>
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "logger.h"


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

out vec4 fragColor;

void main() {
    vec3 lightDir  = normalize(vec3(0.5, 1.0, 0.5));
    float diff     = max(dot(normalize(v_normal), lightDir), 0.0);
    vec3 color     = u_ambient + u_dirlight * diff;
    fragColor      = vec4(color, 1.0);
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
                            const std::vector<LevelObject>& objects, int selected_object_index, int draw_parts)
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

    for (const auto& obj : objects) {
        // Selective rendering logic
        bool shouldDraw = false;
        if (draw_parts & DRAW_OBJECTS) {
            shouldDraw = true; // Draw everything
        } else {
            if ((draw_parts & DRAW_BUILDINGS) && obj.isBuilding) shouldDraw = true;
            if ((draw_parts & DRAW_PROPS) && !obj.isBuilding) shouldDraw = true;
        }

        if (!shouldDraw) continue;

        Mesh mesh = GetOrLoadMesh(obj.modelId);
        if (mesh.vertexCount == 0) continue;


        // ── Build model matrix ────────────────────────────────────────────────
        // Each mesh is centered around (0,0,0) during load.
        // We now place it exactly at its world position (obj.pos) and apply its own rotation (obj.rot).

        glm::mat4 model = glm::mat4(1.0f);
        
        // 1. Translate to world position
        model = glm::translate(model, glm::vec3(obj.pos.x, obj.pos.y, obj.pos.z));

        // 2. Apply IGI rotations (Yaw, Pitch, Roll)
        // IGI rotation order: Yaw (Z), then Pitch (X), then Roll (Y)
        model = glm::rotate(model, (float)obj.rot.z, glm::vec3(0.0f, 0.0f, 1.0f)); // Yaw
        model = glm::rotate(model, (float)obj.rot.x, glm::vec3(1.0f, 0.0f, 0.0f)); // Pitch
        model = glm::rotate(model, (float)obj.rot.y, glm::vec3(0.0f, 1.0f, 0.0f)); // Roll

        // 3. Scale 
        float base_scale = 40.96f; 
        model = glm::scale(model, glm::vec3(base_scale * obj.scale));

        // 4. Convert OBJ Y-up to IGI Z-up (90 degree X rotation)
        model = glm::rotate(model, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)); 

        // Upload model matrix
        glUniformMatrix4fv(loc_model, 1, GL_FALSE, glm::value_ptr(model));

        // ── Lighting and Color ────────────────────────────────────────────────
        // Use a hash of the modelId to generate a unique but consistent color
        // This helps visually distinguish different buildings without textures.
        float r = 0.5f, g = 0.5f, b = 0.5f;
        if (!obj.modelId.empty()) {
            size_t hash = std::hash<std::string>{}(obj.modelId);
            r = 0.4f + (float)(hash & 0xFF) / 255.0f * 0.4f;
            g = 0.4f + (float)((hash >> 8) & 0xFF) / 255.0f * 0.4f;
            b = 0.4f + (float)((hash >> 16) & 0xFF) / 255.0f * 0.4f;
        }

        glUniform3f(loc_dirlight, 0.7f, 0.7f, 0.7f);
        glUniform3f(loc_ambient,  r * 0.4f, g * 0.4f, b * 0.4f);

        // Draw
        renderModel(mesh);
    }

    // Always reset polygon mode after draw
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // Draw selection box for selected object (DISABLED for now)
    // if (selected_object_index >= 0 && selected_object_index < (int)objects.size()) {
    //     DrawSelectionBox(objects[selected_object_index], ubo_mats);
    // }

    // Unbind shader
    glUseProgram(0);
}

glm::vec3 Renderer_Objects::GetMeshExtents(const std::string& modelId) {
    Mesh mesh = GetOrLoadMesh(modelId);
    return mesh.halfExtents;
}

// ─── GetOrLoadMesh ────────────────────────────────────────────────────────────
Mesh Renderer_Objects::GetOrLoadMesh(const std::string& modelId) {
    // Return cached mesh if already loaded
    auto it = mesh_cache_.find(modelId);
    if (it != mesh_cache_.end())
        return it->second;

    // Find the file on disk
    std::string filepath = FindModelFile(modelId);
    if (filepath.empty()) {
        Logger::Get().Log(LogLevel::WARNING, "[Renderer_Objects] Model search FAILED for ID: " + modelId + ". Using fallback cube.");
        mesh_cache_[modelId] = CreateCubeMesh();
        return mesh_cache_[modelId];
    }


    // Load and cache
    try {
        Mesh mesh = loadObjModel(filepath);
        mesh_cache_[modelId] = mesh;
        Logger::Get().Log(LogLevel::INFO, "[Renderer_Objects] Success: Loaded model '" + modelId + "' from " + filepath + " (" + std::to_string(mesh.vertexCount) + " vertices)");
        return mesh;

    } catch (const std::exception& e) {
        Logger::Get().Log(LogLevel::ERR, "[Renderer_Objects] Load FAILED for " + modelId + ": " + std::string(e.what()));
        mesh_cache_[modelId] = {0, 0, 0};
        return mesh_cache_[modelId];
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

std::string Renderer_Objects::FindModelFile(const std::string& modelId) {
    const std::vector<std::string> searchPaths = { "objects/" + modelId + ".obj", "objects/" + modelId + ".mef" };
    for (const auto& path : searchPaths) if (std::filesystem::exists(path)) return path;
    
    // 2. Try partial match (wildcard search in 'objects' folder)
    if (std::filesystem::exists("objects")) {
        // Try BaseID (stripped of _1) if applicable
        std::string baseId = modelId;
        if (baseId.size() > 2 && baseId.substr(baseId.size() - 2) == "_1") {
            baseId = baseId.substr(0, baseId.size() - 2);
        }

        std::string bestMatch = "";
        for (const auto& entry : std::filesystem::directory_iterator("objects")) {
            if (!entry.is_regular_file()) continue;
            std::string fname = entry.path().filename().string();
            std::string ext = entry.path().extension().string();
            if (ext != ".obj" && ext != ".mef") continue;

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
            }
            
            // Last resort: any file containing the type prefix
            if (bestMatch.empty() && fname.find(baseId) != std::string::npos) {
                bestMatch = entry.path().string();
            }
        }
        if (!bestMatch.empty()) {
            Logger::Get().Log(LogLevel::INFO, "[Renderer_Objects] Fuzzy match found for '" + modelId + "': " + bestMatch);
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
void Renderer_Objects::DrawSelectionBox(const LevelObject& obj, GLuint ubo_mats) {
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
out vec4 fragColor;
void main() {
    fragColor = vec4(1.0, 1.0, 0.0, 1.0); // Yellow
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