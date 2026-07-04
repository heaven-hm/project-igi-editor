#include "pch.h"
#include "renderer_rain.h"
#include "../logger.h"
#include <freeglut.h>
#include <vector>
#include <random>

static const char* RAIN_VERT_SRC = R"(
#version 330 core
layout(location = 0) in vec3 a_seed;   // per-drop random seed, x/y/z in [0,1)
layout(location = 1) in float a_isTop; // 0 = bottom vertex of the streak, 1 = top

layout(std140) uniform Matrices {
    mat4 u_unused1;
    mat4 u_unused2;
    mat4 u_mvp;
};

uniform vec3  u_cameraPos;
uniform float u_time;
uniform float u_boxSize;     // footprint around the camera that drops are scattered in
uniform float u_heightStart; // world units, where drops spawn
uniform float u_heightEnd;   // world units, where drops disappear
uniform float u_streakLen;   // world units

void main() {
    float fallRange = max(u_heightStart - u_heightEnd, 1.0);
    float speed = (0.6 + a_seed.z * 0.8) * fallRange; // world units / second
    float z = u_heightStart - mod(u_time * speed + a_seed.y * fallRange + u_cameraPos.z, fallRange);

    vec2 cell = mod(a_seed.xy * u_boxSize - u_cameraPos.xy, u_boxSize) - u_boxSize * 0.5;
    vec3 worldPos = vec3(u_cameraPos.x + cell.x, u_cameraPos.y + cell.y, z + a_isTop * u_streakLen);

    gl_Position = u_mvp * vec4(worldPos, 1.0);
}
)";

static const char* RAIN_FRAG_SRC = R"(
#version 330 core
uniform float u_alpha;
out vec4 fragColor;
void main() {
    // RainEffect's "Rain Alpha" (e.g. 0.13) is the per-droplet sample weight the
    // game accumulates over many overlapping raycast samples — a single thin
    // streak at that alpha is invisible, so boost it into a visible streak alpha
    // while keeping it scaled by the level's own value (0 alpha = no rain at all).
    fragColor = vec4(0.8, 0.85, 0.9, clamp(u_alpha * 4.0, 0.0, 0.85));
}
)";

static GLuint CompileRainShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, 512, nullptr, log);
        Logger::Get().Log(LogLevel::ERR, std::string("[Renderer_Rain] Shader compile error: ") + log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

bool Renderer_Rain::Init() {
    GLuint vert = CompileRainShader(GL_VERTEX_SHADER, RAIN_VERT_SRC);
    GLuint frag = CompileRainShader(GL_FRAGMENT_SHADER, RAIN_FRAG_SRC);
    if (!vert || !frag) return false;

    shader_program_ = glCreateProgram();
    glAttachShader(shader_program_, vert);
    glAttachShader(shader_program_, frag);
    glLinkProgram(shader_program_);
    GLint linked = 0;
    glGetProgramiv(shader_program_, GL_LINK_STATUS, &linked);
    glDeleteShader(vert);
    glDeleteShader(frag);
    if (!linked) {
        char log[512];
        glGetProgramInfoLog(shader_program_, 512, nullptr, log);
        Logger::Get().Log(LogLevel::ERR, std::string("[Renderer_Rain] Link error: ") + log);
        return false;
    }

    GLuint blockIdx = glGetUniformBlockIndex(shader_program_, "Matrices");
    if (blockIdx != GL_INVALID_INDEX) {
        glUniformBlockBinding(shader_program_, blockIdx, ubo_binding_point_);
    }

    num_drops_ = 4000;
    std::vector<float> verts;
    verts.reserve(num_drops_ * 2 * 4);
    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    for (int i = 0; i < num_drops_; ++i) {
        float sx = dist(rng), sy = dist(rng), sz = dist(rng);
        verts.insert(verts.end(), { sx, sy, sz, 0.0f }); // bottom
        verts.insert(verts.end(), { sx, sy, sz, 1.0f }); // top
    }

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    Logger::Get().Log(LogLevel::INFO, "[Renderer_Rain] Init OK.");
    return true;
}

void Renderer_Rain::Shutdown() {
    if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    if (shader_program_) { glDeleteProgram(shader_program_); shader_program_ = 0; }
}

void Renderer_Rain::SetParams(bool active, float startMeters, float endMeters, float alpha) {
    active_ = active;
    start_meters_ = startMeters;
    end_meters_ = endMeters;
    alpha_ = alpha;
}

void Renderer_Rain::Draw(GLuint ubo_mats, const glm::vec3& cameraPos) {
    if (!active_ || !shader_program_ || indoors_) return;

    // RainEffect's Traceline start/end are raycast-occlusion heights (sky-to-ground
    // probe), not absolute world Y — re-anchor them to the camera each frame so the
    // rain band always surrounds wherever the player actually is in the level.
    float heightStart = cameraPos.z + start_meters_ * WORLD_UNITS_PER_METER;
    float heightEnd = cameraPos.z - end_meters_ * WORLD_UNITS_PER_METER;
    if (heightStart <= heightEnd) return;

    glUseProgram(shader_program_);
    glBindBufferBase(GL_UNIFORM_BUFFER, ubo_binding_point_, ubo_mats);

    float timeSec = glutGet(GLUT_ELAPSED_TIME) / 1000.0f;
    glUniform3f(glGetUniformLocation(shader_program_, "u_cameraPos"), cameraPos.x, cameraPos.y, cameraPos.z);
    glUniform1f(glGetUniformLocation(shader_program_, "u_time"), timeSec);
    glUniform1f(glGetUniformLocation(shader_program_, "u_boxSize"), 50.0f * WORLD_UNITS_PER_METER);
    glUniform1f(glGetUniformLocation(shader_program_, "u_heightStart"), heightStart);
    glUniform1f(glGetUniformLocation(shader_program_, "u_heightEnd"), heightEnd);
    glUniform1f(glGetUniformLocation(shader_program_, "u_streakLen"), 0.35f * WORLD_UNITS_PER_METER);
    glUniform1f(glGetUniformLocation(shader_program_, "u_alpha"), alpha_);

    GLboolean depthMaskWas;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMaskWas);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);

    glLineWidth(1.5f);
    glBindVertexArray(vao_);
    glDrawArrays(GL_LINES, 0, num_drops_ * 2);
    glBindVertexArray(0);
    glLineWidth(1.0f);

    glDepthMask(depthMaskWas);
    glUseProgram(0);
}
