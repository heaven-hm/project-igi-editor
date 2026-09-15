#include "pch.h"
#include "renderer_rain.h"
#include "gl_helper.h"
#include "../runtime/weather_visibility.h"
#include "../runtime/weather_particle_style.h"
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
uniform float u_isSnow;      // 0.0 = rain, 1.0 = snow

void main() {
    float fallRange = max(u_heightStart - u_heightEnd, 1.0);
    vec3 worldPos;

    if (u_isSnow > 0.5) {
        // Matches OpenIGI SnowRenderer:
        // CalculateFallSpeed = (0.025 + seed.z * 0.040) * fallRange
        float speed = (0.025 + a_seed.z * 0.040) * fallRange;
        float z = u_heightStart - mod(u_time * speed + a_seed.z * fallRange, fallRange);

        // DriftMeters = 0.35 * 4096.0 world units
        float driftScale = 0.35 * 4096.0;
        float driftX = sin(u_time * 0.45 + a_seed.x * 17.0) * driftScale;
        float driftY = cos(u_time * 0.35 + a_seed.y * 19.0) * driftScale;

        vec2 cell = mod(a_seed.xy * u_boxSize - u_cameraPos.xy, u_boxSize) - u_boxSize * 0.5;
        cell += vec2(driftX, driftY);

        worldPos = vec3(u_cameraPos.x + cell.x, u_cameraPos.y + cell.y, z);
        gl_PointSize = 4.5;
    } else {
        // Matches OpenIGI RainRenderer:
        // CalculateFallSpeed = (0.08 + seed.z * 0.10) * fallRange
        float speed = (0.08 + a_seed.z * 0.10) * fallRange;
        float z = u_heightStart - mod(u_time * speed + a_seed.y * fallRange + u_cameraPos.z, fallRange);

        vec2 cell = mod(a_seed.xy * u_boxSize - u_cameraPos.xy, u_boxSize) - u_boxSize * 0.5;
        worldPos = vec3(u_cameraPos.x + cell.x, u_cameraPos.y + cell.y, z + a_isTop * u_streakLen);
        gl_PointSize = 1.0;
    }

    gl_Position = u_mvp * vec4(worldPos, 1.0);
}
)";

static const char* RAIN_FRAG_SRC = R"(
#version 330 core
uniform float u_alpha;
uniform float u_isSnow;
out vec4 fragColor;

void main() {
    if (u_isSnow > 0.5) {
        if (length(gl_PointCoord - vec2(0.5)) > 0.5) discard;
        // Matches OpenIGI SnowRenderer:
        // alpha = Math.Clamp(_alpha * 1.75f, 0.10f, 0.42f)
        // color: (0.92, 0.97, 1.0, alpha)
        float snowAlpha = clamp(u_alpha * 1.75, 0.10, 0.42);
        fragColor = vec4(0.92, 0.97, 1.0, snowAlpha);
    } else {
        // Matches OpenIGI RainRenderer:
        // streakAlpha = Math.Clamp(_alpha * 1.25f, 0.0f, 0.28f)
        // color: (0.8, 0.85, 0.9, streakAlpha)
        float rainAlpha = clamp(u_alpha * 1.25, 0.0, 0.28);
        fragColor = vec4(0.8, 0.85, 0.9, rainAlpha);
    }
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

    // Rain drop buffer: 1200 drops, seed 12345 (matches OpenIGI RainRenderer)
    num_rain_drops_ = 1200;
    std::vector<float> rain_verts;
    rain_verts.reserve(num_rain_drops_ * 2 * 4);
    std::mt19937 rng_rain(12345);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    for (int i = 0; i < num_rain_drops_; ++i) {
        float sx = dist(rng_rain), sy = dist(rng_rain), sz = dist(rng_rain);
        rain_verts.insert(rain_verts.end(), { sx, sy, sz, 0.0f }); // bottom
        rain_verts.insert(rain_verts.end(), { sx, sy, sz, 1.0f }); // top
    }

    glGenVertexArrays(1, &vao_rain_);
    glGenBuffers(1, &vbo_rain_);
    glBindVertexArray(vao_rain_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_rain_);
    glBufferData(GL_ARRAY_BUFFER, rain_verts.size() * sizeof(float), rain_verts.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    // Snow flake buffer: 900 flakes, seed 271828 (matches OpenIGI SnowRenderer)
    num_snow_flakes_ = 900;
    std::vector<float> snow_verts;
    snow_verts.reserve(num_snow_flakes_ * 4);
    std::mt19937 rng_snow(271828);
    for (int i = 0; i < num_snow_flakes_; ++i) {
        float sx = dist(rng_snow), sy = dist(rng_snow), sz = dist(rng_snow);
        snow_verts.insert(snow_verts.end(), { sx, sy, sz, 0.0f });
    }

    glGenVertexArrays(1, &vao_snow_);
    glGenBuffers(1, &vbo_snow_);
    glBindVertexArray(vao_snow_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_snow_);
    glBufferData(GL_ARRAY_BUFFER, snow_verts.size() * sizeof(float), snow_verts.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    // The editor draws fixed-function overlays after the scene. Leaving either
    // particle upload VBO bound lets affected drivers dereference NULL+0x444
    // from glBegin, so restore the immediate-mode-safe binding state here.
    GL_UnbindForImmediateMode();

    Logger::Get().Log(LogLevel::INFO, "[Renderer_Rain] Init OK.");
    return true;
}

void Renderer_Rain::Shutdown() {
    if (vbo_rain_) { glDeleteBuffers(1, &vbo_rain_); vbo_rain_ = 0; }
    if (vao_rain_) { glDeleteVertexArrays(1, &vao_rain_); vao_rain_ = 0; }
    if (vbo_snow_) { glDeleteBuffers(1, &vbo_snow_); vbo_snow_ = 0; }
    if (vao_snow_) { glDeleteVertexArrays(1, &vao_snow_); vao_snow_ = 0; }
    if (shader_program_) { glDeleteProgram(shader_program_); shader_program_ = 0; }
}

void Renderer_Rain::SetParams(bool active, bool is_snow, float startMeters, float endMeters, float alpha) {
    active_ = active;
    is_snow_ = is_snow;
    start_meters_ = startMeters;
    end_meters_ = endMeters;
    alpha_ = alpha;
}

void Renderer_Rain::Draw(GLuint ubo_mats, const glm::vec3& cameraPos, bool cameraIsSheltered) {
    if (!igi::ShouldDrawWeatherForFrame(active_ && weather_enabled_, shader_program_ != 0,
                                        cameraIsSheltered)) return;

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
    float streakLen = igi::WeatherParticleLengthMeters(is_snow_) * WORLD_UNITS_PER_METER;
    glUniform1f(glGetUniformLocation(shader_program_, "u_streakLen"), streakLen);
    glUniform1f(glGetUniformLocation(shader_program_, "u_alpha"), alpha_);
    glUniform1f(glGetUniformLocation(shader_program_, "u_isSnow"), is_snow_ ? 1.0f : 0.0f);

    GLboolean depthMaskWas;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMaskWas);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);

    if (is_snow_) {
        glEnable(GL_PROGRAM_POINT_SIZE);
        glBindVertexArray(vao_snow_);
        glDrawArrays(GL_POINTS, 0, num_snow_flakes_);
        glBindVertexArray(0);
        glDisable(GL_PROGRAM_POINT_SIZE);
    } else {
        glLineWidth(1.5f);
        glBindVertexArray(vao_rain_);
        glDrawArrays(GL_LINES, 0, num_rain_drops_ * 2);
        glBindVertexArray(0);
        glLineWidth(1.0f);
    }

    glDepthMask(depthMaskWas);
    // Weather is the final 3D scene pass before graph/HUD overlays. Restore the
    // blend state it temporarily enabled so those overlays and the next frame
    // start from the same opaque-scene state regardless of weather activity.
    glDisable(GL_BLEND);
    glUseProgram(0);
    GL_UnbindForImmediateMode();
}
