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
                            const std::vector<LevelObject>& objects, int selected_object_index)
{
    if (objects.empty()) {
        static bool logged_once = false;
        if (!logged_once) {
            Logger::Get().Log(LogLevel::WARNING, "[Renderer_Objects] Draw called with EMPTY object list!");
            logged_once = true;
        }
        return;
    }
    
    static bool logged_count = false;
    if (!logged_count) {
        Logger::Get().Log(LogLevel::INFO, "[Renderer_Objects] Draw called with " + std::to_string(objects.size()) + " objects.");
        logged_count = true;
    }
    if (!shader_program_) return;



    // Bind our object shader
    glUseProgram(shader_program_);

    // Bind the shared UBO (same buffer terrain uses — contains proj + view)
    glBindBufferBase(GL_UNIFORM_BUFFER, ubo_binding_point_, ubo_mats);

    // OpenGL state
    glDisable(GL_DEPTH_TEST);
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
        // Logger::Get().Log(LogLevel::INFO, "[Renderer_Objects] Rendering object: " + obj.name + " (" + obj.modelId + ")");
        Mesh mesh = GetOrLoadMesh(obj.modelId);
        if (mesh.vertexCount == 0) continue;


        // ── Build model matrix ────────────────────────────────────────────────
        // Use native IGI positions - u_mvp in UBO contains the 0.001 scale
        // Cast dvec3 to vec3 for rendering
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(obj.pos));

        // Rotate — IGI uses ZYX order (Yaw, Pitch, Roll) with Param 8=Yaw, 7=Pitch, 6=Roll
        model = glm::rotate(model, static_cast<float>(obj.rot.z), glm::vec3(0.0f, 0.0f, 1.0f)); // Yaw (Around Z) - Param 8
        model = glm::rotate(model, static_cast<float>(obj.rot.y), glm::vec3(0.0f, 1.0f, 0.0f)); // Pitch (Around Y) - Param 7
        model = glm::rotate(model, static_cast<float>(obj.rot.x), glm::vec3(1.0f, 0.0f, 0.0f)); // Roll (Around X) - Param 6

        // Scale — OBJ files are in centimeters. 100 cm = 1 meter = WORLD_UNITS_PER_METER (4096 IGI units).
        // So scale factor = (4096 / 100) = 40.96
        model = glm::scale(model, glm::vec3((WORLD_UNITS_PER_METER / 100.0f) * obj.scale));

        // OBJ files are exported Y-up, but IGI is Z-up. Rotate 90 degrees around X to correct the model orientation.
        model = glm::rotate(model, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));

        // Upload model matrix
        glUniformMatrix4fv(loc_model, 1, GL_FALSE, glm::value_ptr(model));

        // Upload lighting
        glUniform3f(loc_dirlight, obj.dirlightR, obj.dirlightG, obj.dirlightB);
        glUniform3f(loc_ambient,  obj.ambientR,  obj.ambientG,  obj.ambientB);

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

// ─── GetOrLoadMesh ────────────────────────────────────────────────────────────
Mesh Renderer_Objects::GetOrLoadMesh(const std::string& modelId) {
    // Return cached mesh if already loaded
    auto it = mesh_cache_.find(modelId);
    if (it != mesh_cache_.end())
        return it->second;

    // Find the file on disk
    std::string filepath = FindModelFile(modelId);
    if (filepath.empty()) {
        Logger::Get().Log(LogLevel::WARNING, "[Renderer_Objects] Model search FAILED for ID: " + modelId + ". Checked objects/ and common/ folders.");
        mesh_cache_[modelId] = {0, 0, 0}; // Mark as failed so we don't retry
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

// ─── FindModelFile ────────────────────────────────────────────────────────────
std::string Renderer_Objects::FindModelFile(const std::string& modelId) {
    // Search priority:
    // 1. objects/{modelId}.obj       (converted OBJ)
    // 2. objects/{modelId}.mef       (raw MEF binary)
    // 3. Fuzzy search inside objects/ folder by modelId substring

    const std::vector<std::string> searchPaths = {
        "objects/" + modelId + ".obj",
        "objects/" + modelId + ".mef"
    };

    for (const auto& path : searchPaths) {
        Logger::Get().Log(LogLevel::INFO, "[Renderer_Objects] Checking path: " + path);
        if (std::filesystem::exists(path)) {
            Logger::Get().Log(LogLevel::INFO, "[Renderer_Objects] Found exact match: " + path);
            return path;
        }
    }



    // Fuzzy fallback — search objects/ folder for filename containing modelId
    if (std::filesystem::exists("objects")) {
        for (const auto& entry : std::filesystem::directory_iterator("objects")) {
            if (!entry.is_regular_file()) continue;
            std::string fname = entry.path().filename().string();
            std::string ext   = entry.path().extension().string();
            if (fname.find(modelId) != std::string::npos &&
               (ext == ".obj" || ext == ".mef"))
            {
                Logger::Get().Log(LogLevel::INFO, "[Renderer_Objects] Found fuzzy match: " + entry.path().string());
                return entry.path().string();
            }
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