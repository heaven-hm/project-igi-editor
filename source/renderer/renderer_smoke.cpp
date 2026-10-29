#include "pch.h"
#include "renderer_smoke.h"
#include "gl_helper.h"
#include "../logger.h"

#include <freeglut.h>

// The engine's Smoke particles, drawn from the transcription of
//   Particle2D_Draw  HOOK 0x4FD020  src/graphtask/particle.c:108-208
//   Particle2D_Run   HOOK 0x4FD660  src/graphtask/particle.c:264-305
//   Smoke_Run        HOOK 0x437DB0  src/fxobj/smoke.c:298-339
// See renderer_smoke.h for the tick rule, the quad rule and the one named
// divergence (the .spr texels).

namespace {

// particle.c:194-203 -- `if (t->flags) { if (t->flags & 0x80000) alpha = level;
// else { alpha = kOne; r = colour.x * level; ... } }`. Particle2D_New always
// ORs 0x40000 into the flags (particle.c:515), so the branch is decided by the
// authored "Fade Mode" alone: mode 0 sets 0x80000 (alpha fades), modes 1 and 2
// do not (the colour fades).
constexpr std::uint32_t kLevelIsAlphaFlag = 0x80000u;

const char* SMOKE_VERT_SRC = R"(
#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec4 a_colour;
layout(location = 2) in vec2 a_uv;

layout(std140) uniform Matrices {
    mat4 u_unused1;
    mat4 u_unused2;
    mat4 u_mvp;
};

out vec4 v_colour;
out vec2 v_uv;

void main() {
    gl_Position = u_mvp * vec4(a_pos, 1.0);
    v_colour = a_colour;
    v_uv = a_uv;
}
)";

// The sprite's texels are not loaded (renderer_smoke.h), so the quad is filled
// with a radial falloff: opaque at the centre, gone at the edge. It replaces
// the texture only -- the colour and the alpha still come from the engine's own
// rule above.
const char* SMOKE_FRAG_SRC = R"(
#version 330 core
in vec4 v_colour;
in vec2 v_uv;
out vec4 fragColor;

void main() {
    float r = length(v_uv * 2.0 - 1.0);
    float falloff = clamp(1.0 - r, 0.0, 1.0);
    falloff = falloff * falloff;
    fragColor = vec4(v_colour.rgb, v_colour.a * falloff);
}
)";

// GLSL 1.20 variants for the legacy 2.1 context: identical attributes,
// uniforms, varyings and arithmetic; the shared Matrices block becomes a plain
// u_mvp (see renderer_rain.cpp for the same pair).
const char* SMOKE_VERT_SRC_120 = R"(
#version 120
attribute vec3 a_pos;
attribute vec4 a_colour;
attribute vec2 a_uv;
uniform mat4 u_mvp;
varying vec4 v_colour;
varying vec2 v_uv;
void main() {
    gl_Position = u_mvp * vec4(a_pos, 1.0);
    v_colour = a_colour;
    v_uv = a_uv;
}
)";

const char* SMOKE_FRAG_SRC_120 = R"(
#version 120
varying vec4 v_colour;
varying vec2 v_uv;
void main() {
    float r = length(v_uv * 2.0 - 1.0);
    float falloff = clamp(1.0 - r, 0.0, 1.0);
    falloff = falloff * falloff;
    gl_FragColor = vec4(v_colour.rgb, v_colour.a * falloff);
}
)";

GLuint CompileSmokeShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, 512, nullptr, log);
        Logger::Get().Log(LogLevel::ERR, std::string("[Renderer_Smoke] Shader compile error: ") + log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

constexpr int kFloatsPerVertex = 9;  // pos(3) + colour(4) + uv(2)
constexpr int kVerticesPerParticle = 6;
// The engine's particle quad corner -> uv order (particle.c:185-192 with
// QVertex_DrawQuadEx's corner order): (-,-) (+,+)... the four corners are
// written as (-half, -half), (+half, -half), (-half, +half), (+half, +half) and
// the sprite maps them (0,0) (1,0) (0,1) (1,1) (particle.c:185-192).
const float kCornerUv[4][2] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}};
// Two triangles: 0,1,2 and 2,1,3.
const int kCornerIndex[kVerticesPerParticle] = {0, 1, 2, 2, 1, 3};

void WriteVertex(float* v, const glm::vec3& p, float r, float g, float b, float a, float u, float uv_v) {
    v[0] = p.x;
    v[1] = p.y;
    v[2] = p.z;
    v[3] = r;
    v[4] = g;
    v[5] = b;
    v[6] = a;
    v[7] = u;
    v[8] = uv_v;
}

}  // namespace

bool Renderer_Smoke::Init() {
    const char* vs = g_gl_info.legacy_gl21_ ? SMOKE_VERT_SRC_120 : SMOKE_VERT_SRC;
    const char* fs = g_gl_info.legacy_gl21_ ? SMOKE_FRAG_SRC_120 : SMOKE_FRAG_SRC;
    GLuint vert = CompileSmokeShader(GL_VERTEX_SHADER, vs);
    GLuint frag = CompileSmokeShader(GL_FRAGMENT_SHADER, fs);
    if (!vert || !frag) {
        if (vert) glDeleteShader(vert);
        if (frag) glDeleteShader(frag);
        return false;
    }
    program_ = glCreateProgram();
    glAttachShader(program_, vert);
    glAttachShader(program_, frag);
    if (g_gl_info.legacy_gl21_) GL_LegacyBindInlineAttribLocations(program_);
    glLinkProgram(program_);
    glDeleteShader(vert);
    glDeleteShader(frag);
    GLint linked = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[512];
        glGetProgramInfoLog(program_, 512, nullptr, log);
        Logger::Get().Log(LogLevel::ERR, std::string("[Renderer_Smoke] Link error: ") + log);
        glDeleteProgram(program_);
        program_ = 0;
        return false;
    }

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    const GLsizei stride = kFloatsPerVertex * sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(7 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // The editor draws fixed-function overlays after the scene; leaving the
    // particle VAO/VBO bound lets affected drivers dereference NULL from
    // glBegin (the same reason renderer_rain.cpp restores this).
    GL_UnbindForImmediateMode();
    Logger::Get().Log(LogLevel::INFO, "[Renderer_Smoke] Init OK.");
    return true;
}

void Renderer_Smoke::Shutdown() {
    if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    if (program_) { glDeleteProgram(program_); program_ = 0; }
}

void Renderer_Smoke::Clear() {
    sim_.Clear();
}

void Renderer_Smoke::SetEmitters(const std::vector<igi::SmokeParams>& emitters) {
    sim_.SetEmitters(emitters);
}

void Renderer_Smoke::Draw(GLuint ubo_mats, const glm::vec3& camera_pos, const glm::vec3& camera_right,
                          const glm::vec3& camera_up, const glm::vec3& camera_forward) {
    // ONE engine tick per rendered frame (renderer_smoke.h).
    sim_.Tick();

    if (program_ == 0) return;
    if (sim_.TotalParticles() == 0) return;

    vertices_.clear();
    // Particle2D_Draw walks the pool in order (particle.c:136-207); the engine's
    // depth test decides the overlap, so no sort is needed here either.
    for (std::size_t e = 0; e < sim_.EmitterCount(); ++e) {
        const igi::SmokeEmitterState& emitter = sim_.Emitter(e);
        const igi::SmokeParticleGroup& group = emitter.group;
        if (!group.exists || group.particles.empty()) continue;

        // particle.c:117-133: with no light the quad's colour is the group's,
        // which Smoke_Changed scaled by kScale (smoke.c:398-400). The engine's
        // components are 0..255 (k255, particle.c:75).
        const float base_r = group.colour[0] / 255.0f;
        const float base_g = group.colour[1] / 255.0f;
        const float base_b = group.colour[2] / 255.0f;
        const bool level_is_alpha = (group.flags & kLevelIsAlphaFlag) != 0;

        for (const igi::SmokeParticleState& p : group.particles) {
            // TransContext_Project's gate: particle.c:143 skips a particle whose
            // view depth is not > kNear (2048, particle.c:71) or is past the
            // group's maxDepth (kLife, smoke.c:403).
            const glm::vec3 centre((float)p.pos[0], (float)p.pos[1], (float)p.pos[2]);
            const float depth = glm::dot(centre - camera_pos, camera_forward);
            if (!(depth > 2048.0f) || depth >= group.max_depth) continue;

            // particle.c:145-151: half = size * p.scale (per axis; both axes
            // carry the same `size`, smoke.c:394).
            const float half = group.size * p.scale;
            if (!(half > 0.0f)) continue;

            float cr = base_r;
            float cg = base_g;
            float cb = base_b;
            float ca = 1.0f;
            if (level_is_alpha) {
                ca = p.level;  // particle.c:195-196
            } else {
                cr *= p.level;  // particle.c:198-202
                cg *= p.level;
                cb *= p.level;
            }
            if (!(ca > 0.0f)) continue;

            // particle.c:156-193: the four corners, rotated in the screen plane
            // by p.angle when the particle spins. `camera_up` is the screen's UP
            // axis, while the engine's `half[1]` is measured DOWN the screen
            // (transctx.c:266-267 adds +scaleY*view_y), so the y offset is
            // negated when it is applied to the camera's up axis.
            const float c = std::cos(p.angle);
            const float s = std::sin(p.angle);
            const float t1 = -half * s;
            const float t2 = c * half;
            const float hc = -half * c;
            const float t3 = -s * half;
            const float sh = s * half;
            const float ch = c * half;
            const glm::vec2 corners[4] = {
                {t1 - t2, hc - t3},
                {t2 + t1, t3 + hc},
                {sh - t2, ch - t3},
                {sh + t2, ch + t3},
            };

            for (int i = 0; i < kVerticesPerParticle; ++i) {
                const int corner_index = kCornerIndex[i];
                const glm::vec2& corner = corners[corner_index];
                const glm::vec3 world =
                    centre + camera_right * corner.x - camera_up * corner.y;
                float vertex[kFloatsPerVertex];
                WriteVertex(vertex, world, cr, cg, cb, ca, kCornerUv[corner_index][0],
                            kCornerUv[corner_index][1]);
                vertices_.insert(vertices_.end(), vertex, vertex + kFloatsPerVertex);
            }
        }
    }

    if (vertices_.empty()) return;

    glUseProgram(program_);
    const GLuint blk = glGetUniformBlockIndex(program_, "Matrices");
    if (blk != GL_INVALID_INDEX) glUniformBlockBinding(program_, blk, 0);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo_mats);

    // Particle2D_Draw's state (particle.c:412-429 for the group's flags): the
    // particles blend and are depth-tested but do not write depth, so they do
    // not occlude each other.
    GLboolean depth_mask_was;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask_was);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    const GLboolean cull_was_enabled = glIsEnabled(GL_CULL_FACE);
    if (cull_was_enabled) glDisable(GL_CULL_FACE);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(vertices_.size() * sizeof(float)), vertices_.data(),
                 GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(vertices_.size() / kFloatsPerVertex));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDepthMask(depth_mask_was);
    glDisable(GL_BLEND);
    if (cull_was_enabled) glEnable(GL_CULL_FACE);
    glUseProgram(0);
    GL_UnbindForImmediateMode();

    // IGI_PROFILE-only, deterministic (counts, no time): the editor's GUI cannot
    // run on this host (GL 4.1 vs 2.1), so a headless lane cannot see the
    // particles. This line is how a run PROVES the emitters are spawning:
    //
    //   [SmokeProfile] 30 frames: emitters=3 particles=900/frame max=900 ticks=.. quads=900/frame
    //
    // `particles` is SmokeSimulation::TotalParticles() after the tick, `quads`
    // the number of billboards actually submitted this frame (they differ when
    // a particle is behind the camera).
    if (std::getenv("IGI_PROFILE") != nullptr) {
        static long long acc_particles = 0;
        static long long acc_quads = 0;
        static long long max_particles = 0;
        static int frames = 0;
        const long long particles = sim_.TotalParticles();
        const long long quads = static_cast<long long>(vertices_.size() / kFloatsPerVertex) /
                                kVerticesPerParticle;
        acc_particles += particles;
        acc_quads += quads;
        if (particles > max_particles) max_particles = particles;
        if (++frames >= 30) {
            Logger::Get().Log(LogLevel::INFO,
                "[SmokeProfile] 30 frames: emitters=" + std::to_string(sim_.EmitterCount()) +
                " particles=" + std::to_string(acc_particles / 30) + "/frame max=" +
                std::to_string(max_particles) + " quads=" + std::to_string(acc_quads / 30) +
                "/frame ticks=" + std::to_string(sim_.TickCount()));
            acc_particles = 0;
            acc_quads = 0;
            frames = 0;
        }
    }
}
