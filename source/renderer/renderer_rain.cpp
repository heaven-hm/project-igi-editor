#include "pch.h"
#include "renderer_rain.h"
#include "gl_helper.h"
#include "weather_math.h"
#include "../runtime/weather_visibility.h"
#include "../runtime/weather_particle_style.h"
#include "../logger.h"
#include <freeglut.h>
#include <vector>
#include <random>

static const char* RAIN_VERT_SRC = R"(
#version 330 core
layout(location = 0) in vec3 a_seed;   // per-drop random seed, x/y/z in [0,1)
layout(location = 1) in vec2 a_corner; // x = width side, y = bottom/top

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
uniform float u_halfWidth;   // world units

void main() {
    float fallRange = max(u_heightStart - u_heightEnd, 1.0);
    float speed = (0.08 + a_seed.z * 0.10) * fallRange;
    float z = u_heightStart - mod(u_time * speed + a_seed.y * fallRange + u_cameraPos.z, fallRange);
    vec2 cell = mod(a_seed.xy * u_boxSize - u_cameraPos.xy, u_boxSize) - u_boxSize * 0.5;
    vec3 worldPos = vec3(u_cameraPos.x + cell.x, u_cameraPos.y + cell.y,
                         z + a_corner.y * u_streakLen);
    worldPos.xy += vec2(a_corner.x * u_halfWidth);
    gl_Position = u_mvp * vec4(worldPos, 1.0);
}
)";

static const char* RAIN_FRAG_SRC = R"(
#version 330 core
uniform float u_alpha;
out vec4 fragColor;

void main() {
    fragColor = vec4(0.8, 0.85, 0.9, clamp(u_alpha * 1.25, 0.0, 0.28));
}
)";

static const char* SNOW_VERT_SRC = R"(
#version 330 core
layout(location = 0) in vec3 a_seed;
layout(location = 1) in vec2 a_corner;
layout(std140) uniform Matrices { mat4 u_unused1; mat4 u_unused2; mat4 u_mvp; };
uniform vec3 u_cameraPos;
uniform float u_time;
uniform float u_boxSize;
uniform float u_heightStart;
uniform float u_heightEnd;
uniform float u_halfExtent;
uniform float u_driftUnits;
void main() {
    float fallRange = max(u_heightStart - u_heightEnd, 1.0);
    float speed = (0.025 + a_seed.z * 0.040) * fallRange;
    float z = u_heightStart - mod(u_time * speed + a_seed.z * fallRange + u_cameraPos.z, fallRange);
    float halfBox = u_boxSize * 0.5;
    float cellX = mod(a_seed.x * u_boxSize - u_cameraPos.x, u_boxSize) - halfBox +
                  sin(u_time * 0.45 + a_seed.x * 17.0) * u_driftUnits;
    float cellY = mod(a_seed.y * u_boxSize - u_cameraPos.y, u_boxSize) - halfBox +
                  cos(u_time * 0.35 + a_seed.y * 19.0) * u_driftUnits;
    // OpenIGI builds a square flake quad from a 0.045m half-extent. Its
    // diagonal Z ordering is retained so the physical 0.09m footprint and
    // depth relationship match the reference renderer.
    vec3 worldPos = vec3(u_cameraPos.x + cellX + a_corner.x * u_halfExtent,
                         u_cameraPos.y + cellY + a_corner.y * u_halfExtent,
                         z + a_corner.x * u_halfExtent);
    gl_Position = u_mvp * vec4(worldPos, 1.0);
}
)";

static const char* SNOW_FRAG_SRC = R"(
#version 330 core
uniform float u_alpha;
out vec4 fragColor;
void main() {
    fragColor = vec4(0.92, 0.97, 1.0, clamp(u_alpha * 1.75, 0.10, 0.42));
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

static GLuint LinkWeatherProgram(const char* vertex_source, const char* fragment_source,
                                 GLuint uniform_block_binding) {
    GLuint vert = CompileRainShader(GL_VERTEX_SHADER, vertex_source);
    GLuint frag = CompileRainShader(GL_FRAGMENT_SHADER, fragment_source);
    if (!vert || !frag) {
        if (vert) glDeleteShader(vert);
        if (frag) glDeleteShader(frag);
        return 0;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);
    glDeleteShader(vert);
    glDeleteShader(frag);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[512];
        glGetProgramInfoLog(program, 512, nullptr, log);
        Logger::Get().Log(LogLevel::ERR, std::string("[Renderer_Rain] Link error: ") + log);
        glDeleteProgram(program);
        return 0;
    }
    const GLuint block_idx = glGetUniformBlockIndex(program, "Matrices");
    if (block_idx != GL_INVALID_INDEX) glUniformBlockBinding(program, block_idx, uniform_block_binding);
    return program;
}

bool Renderer_Rain::Init() {
    shader_program_ = LinkWeatherProgram(RAIN_VERT_SRC, RAIN_FRAG_SRC, ubo_binding_point_);
    snow_program_ = LinkWeatherProgram(SNOW_VERT_SRC, SNOW_FRAG_SRC, ubo_binding_point_);
    if (!shader_program_ || !snow_program_) {
        Shutdown();
        return false;
    }

    // Rain drop buffer: 1200 drops, seed 12345 (matches OpenIGI RainRenderer)
    num_rain_drops_ = igi::weather::kRainDrops;
    std::vector<float> rain_verts;
    rain_verts.reserve(num_rain_drops_ * 6 * 5);
    std::mt19937 rng_rain(12345);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    for (int i = 0; i < num_rain_drops_; ++i) {
        float sx = dist(rng_rain), sy = dist(rng_rain), sz = dist(rng_rain);
        // Two triangles matching OpenIGI's 0.012m-wide streak quad.
        const float corners[][2] = {
            {-1.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f},
            {-1.0f, 0.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f},
        };
        for (const auto& corner : corners) {
            rain_verts.insert(rain_verts.end(), {sx, sy, sz, corner[0], corner[1]});
        }
    }

    glGenVertexArrays(1, &vao_rain_);
    glGenBuffers(1, &vbo_rain_);
    glBindVertexArray(vao_rain_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_rain_);
    glBufferData(GL_ARRAY_BUFFER, rain_verts.size() * sizeof(float), rain_verts.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    // Snow flake buffer: 900 flakes, seed 271828 (matches OpenIGI SnowRenderer)
    num_snow_flakes_ = igi::weather::kSnowFlakes;
    std::vector<float> snow_verts;
    snow_verts.reserve(num_snow_flakes_ * 6 * 5);
    std::mt19937 rng_snow(271828);
    for (int i = 0; i < num_snow_flakes_; ++i) {
        float sx = dist(rng_snow), sy = dist(rng_snow), sz = dist(rng_snow);
        const float corners[][2] = {
            {-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f},
            {-1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f},
        };
        for (const auto& corner : corners) {
            snow_verts.insert(snow_verts.end(), {sx, sy, sz, corner[0], corner[1]});
        }
    }

    glGenVertexArrays(1, &vao_snow_);
    glGenBuffers(1, &vbo_snow_);
    glBindVertexArray(vao_snow_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_snow_);
    glBufferData(GL_ARRAY_BUFFER, snow_verts.size() * sizeof(float), snow_verts.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
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
    if (snow_program_) { glDeleteProgram(snow_program_); snow_program_ = 0; }
}

void Renderer_Rain::SetParams(bool active, bool is_snow, float startMeters, float endMeters, float alpha) {
    active_ = active;
    is_snow_ = is_snow;
    start_meters_ = startMeters;
    end_meters_ = endMeters;
    alpha_ = alpha;
    if (active_) {
        if (is_snow_) {
            Logger::Get().Log(LogLevel::INFO,
                "[Renderer_Rain] SNOW parity: flakes=900 diameter=0.090m speed=0.025..0.065-band/s "
                "alpha=clamp(authored*1.75,0.10,0.42) drift=0.350m");
        } else {
            Logger::Get().Log(LogLevel::INFO,
                "[Renderer_Rain] RAIN parity: drops=1200 length=0.080m width=0.012m speed=0.08..0.18-band/s "
                "alpha=clamp(authored*1.25,0.00,0.28)");
        }
    }
}

void Renderer_Rain::Draw(GLuint ubo_mats, const glm::vec3& cameraPos, bool cameraIsSheltered) {
    const bool selected_renderer_ready = is_snow_ ? snow_program_ != 0 : shader_program_ != 0;
    if (!igi::ShouldDrawWeatherForFrame(active_ && weather_enabled_, selected_renderer_ready,
                                        cameraIsSheltered)) return;

    // RainEffect's Traceline start/end are raycast-occlusion heights (sky-to-ground
    // probe), not absolute world Y — re-anchor them to the camera each frame so the
    // rain band always surrounds wherever the player actually is in the level.
    float heightStart = cameraPos.z + start_meters_ * WORLD_UNITS_PER_METER;
    float heightEnd = cameraPos.z - end_meters_ * WORLD_UNITS_PER_METER;
    if (heightStart <= heightEnd) return;

    const float time_sec = glutGet(GLUT_ELAPSED_TIME) / 1000.0f;
    if (is_snow_) {
        DrawSnow(ubo_mats, cameraPos, time_sec);
        return;
    }

    glUseProgram(shader_program_);
    glBindBufferBase(GL_UNIFORM_BUFFER, ubo_binding_point_, ubo_mats);

    glUniform3f(glGetUniformLocation(shader_program_, "u_cameraPos"), cameraPos.x, cameraPos.y, cameraPos.z);
    glUniform1f(glGetUniformLocation(shader_program_, "u_time"), time_sec);
    glUniform1f(glGetUniformLocation(shader_program_, "u_boxSize"), igi::weather::kBoxMeters * WORLD_UNITS_PER_METER);
    glUniform1f(glGetUniformLocation(shader_program_, "u_heightStart"), heightStart);
    glUniform1f(glGetUniformLocation(shader_program_, "u_heightEnd"), heightEnd);
    glUniform1f(glGetUniformLocation(shader_program_, "u_streakLen"),
                igi::weather::kRainStreakMeters * WORLD_UNITS_PER_METER);
    glUniform1f(glGetUniformLocation(shader_program_, "u_halfWidth"),
                igi::weather::kRainStreakWidthMeters * 0.5f * WORLD_UNITS_PER_METER);
    glUniform1f(glGetUniformLocation(shader_program_, "u_alpha"), alpha_);

    GLboolean depthMaskWas;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMaskWas);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);

    glBindVertexArray(vao_rain_);
    glDrawArrays(GL_TRIANGLES, 0, num_rain_drops_ * 6);
    glBindVertexArray(0);

    glDepthMask(depthMaskWas);
    // Weather is the final 3D scene pass before graph/HUD overlays. Restore the
    // blend state it temporarily enabled so those overlays and the next frame
    // start from the same opaque-scene state regardless of weather activity.
    glDisable(GL_BLEND);
    glUseProgram(0);
    GL_UnbindForImmediateMode();
}

void Renderer_Rain::DrawSnow(GLuint ubo_mats, const glm::vec3& cameraPos, float time_sec) {
    const float height_start = cameraPos.z + start_meters_ * WORLD_UNITS_PER_METER;
    const float height_end = cameraPos.z - end_meters_ * WORLD_UNITS_PER_METER;
    if (height_start <= height_end) return;

    glUseProgram(snow_program_);
    glBindBufferBase(GL_UNIFORM_BUFFER, ubo_binding_point_, ubo_mats);
    glUniform3f(glGetUniformLocation(snow_program_, "u_cameraPos"), cameraPos.x, cameraPos.y, cameraPos.z);
    glUniform1f(glGetUniformLocation(snow_program_, "u_time"), time_sec);
    glUniform1f(glGetUniformLocation(snow_program_, "u_boxSize"), igi::weather::kBoxMeters * WORLD_UNITS_PER_METER);
    glUniform1f(glGetUniformLocation(snow_program_, "u_heightStart"), height_start);
    glUniform1f(glGetUniformLocation(snow_program_, "u_heightEnd"), height_end);
    glUniform1f(glGetUniformLocation(snow_program_, "u_halfExtent"),
                igi::weather::kSnowFlakeMeters * WORLD_UNITS_PER_METER);
    glUniform1f(glGetUniformLocation(snow_program_, "u_driftUnits"),
                igi::weather::kSnowDriftMeters * WORLD_UNITS_PER_METER);
    glUniform1f(glGetUniformLocation(snow_program_, "u_alpha"), alpha_);

    GLboolean depth_mask_was;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask_was);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(vao_snow_);
    glDrawArrays(GL_TRIANGLES, 0, num_snow_flakes_ * 6);
    glBindVertexArray(0);
    glDepthMask(depth_mask_was);
    glDisable(GL_BLEND);
    glUseProgram(0);
    GL_UnbindForImmediateMode();
}
