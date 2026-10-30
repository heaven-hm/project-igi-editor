#include "pch.h"
#include "renderer_smoke.h"
#include "gl_helper.h"
#include "../logger.h"
#include "../utils.h"
#include "res_chunks.h"
#include "res_writer.h"

#include <freeglut.h>

#include <algorithm>
#include <cstdlib>

// The engine's Smoke particles, drawn from the transcription of
//   Particle2D_Draw  HOOK 0x4FD020  src/graphtask/particle.c:108-208
//   Particle2D_Run   HOOK 0x4FD660  src/graphtask/particle.c:264-305
//   Smoke_Run        HOOK 0x437DB0  src/fxobj/smoke.c:298-339
//   Sprite_Load      HOOK 0x4B6720  src/res/sprite.c:162-262
//   D3DQVertex_*     HOOK 0x50DCC0/0x50DE00/0x50E000  src/qvertex/d3d.c
// See renderer_smoke.h for the tick rule, the quad rule, the texel source and
// the blend states.

namespace {

// Particle2D_Draw ORs this into the flags before the quad is submitted
// (particle.c:207): it selects LINEAR texture filtering
// (D3DQVertex_SetTextureStates 0x50DE00, src/qvertex/d3d.c:592-598).
constexpr std::uint32_t kSmoothFlag = 0x1000000u;
// D3DQVertex_SetRenderStates 0x50DCC0 (src/qvertex/d3d.c:567-573): ALPHATEST
// is on when any of these bits is set, with ALPHAREF 10 and ALPHAFUNC GREATER.
// The reference value lives in the fragment shader (kAlphaRef is not a uniform:
// D3D's -1..1 source-alpha range becomes `c.a * 255.0 <= 10.0` there).
constexpr std::uint32_t kAlphaTestMask = 0x8000003Cu;
// D3DQVertex_SetRenderStates' z branch (d3d.c:574-585). Smoke's groups ALWAYS
// carry 0x40000 because Particle2D_New ORs it into every group (particle.c:515),
// so every smoke quad is z-tested LEQUAL and never writes z -- including the
// invalid-fade-mode case, whose SmokeBlendFlags is 0 and which Particle2D_New
// still turns into 0x40000. The draw below therefore sets that one state
// unconditionally instead of switching on the bits.
constexpr std::uint32_t kZTestFlag = 0x40000u;     // z test, no z write

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

// The engine's texture stage is
//   COLOROP = MODULATE(TEXTURE, DIFFUSE), ALPHAOP = MODULATE(TEXTURE, DIFFUSE)
// (D3DQVertex_SetTextureStates 0x50DE00, src/qvertex/d3d.c:605-610), so the
// fragment is the sprite texel multiplied by the vertex colour/alpha that
// particle.c:194-203 derived. u_textured == 0 is the no-pack fallback: the
// quad is filled with the procedural radial falloff the emitter used before the
// sprite loader existed (renderer_smoke.h, FALLBACK).
const char* SMOKE_FRAG_SRC = R"(
#version 330 core
in vec4 v_colour;
in vec2 v_uv;
uniform sampler2D u_tex;
uniform int u_textured;
uniform int u_alpha_test;
out vec4 fragColor;

void main() {
    vec4 c = v_colour;
    if (u_textured != 0) {
        vec4 t = texture(u_tex, v_uv);
        c = vec4(v_colour.rgb * t.rgb, v_colour.a * t.a);
    } else {
        float r = length(v_uv * 2.0 - 1.0);
        float falloff = clamp(1.0 - r, 0.0, 1.0);
        c.a *= falloff * falloff;
    }
    // D3DRENDERSTATE_ALPHATESTENABLE + ALPHAREF 10 + D3DCMP_GREATER
    // (D3DQVertex_SetRenderStates 0x50DCC0, src/qvertex/d3d.c:567-573).
    if (u_alpha_test != 0 && c.a * 255.0 <= 10.0) discard;
    fragColor = c;
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
uniform sampler2D u_tex;
uniform int u_textured;
uniform int u_alpha_test;
void main() {
    vec4 c = v_colour;
    if (u_textured != 0) {
        vec4 t = texture2D(u_tex, v_uv);
        c = vec4(v_colour.rgb * t.rgb, v_colour.a * t.a);
    } else {
        float r = length(v_uv * 2.0 - 1.0);
        float falloff = clamp(1.0 - r, 0.0, 1.0);
        c.a *= falloff * falloff;
    }
    if (u_alpha_test != 0 && c.a * 255.0 <= 10.0) discard;
    gl_FragColor = c;
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
// Corner order here is the engine's screen order: 0 top-left, 1 top-right,
// 2 bottom-left, 3 bottom-right. D3D samples v = 0 at the TOP of a surface, and
// the uploaded RGBA's first row is the sprite's first (top) row, so the same
// numbers map the sprite upright with no vertical flip.
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

// D3DQVertex_SetBlendStates 0x50E000 (src/qvertex/d3d.c:633-646). The last arm
// is the fade-mode 0 case and is the standard alpha blend the editor always
// used; the other two are what the engine does for fade modes 1 and 2.
void BlendForFlags(std::uint32_t flags, GLenum* src, GLenum* dst) {
    if (flags & 8u) {
        *src = GL_SRC_ALPHA;
        *dst = GL_ONE;                 // D3DBLEND_SRCALPHA / D3DBLEND_ONE
    } else if (flags & 0x10u) {
        *src = GL_ZERO;
        *dst = GL_ONE_MINUS_SRC_COLOR; // D3DBLEND_ZERO / D3DBLEND_INVSRCCOLOR
    } else if (flags & 0x80000000u) {
        *src = GL_ZERO;
        *dst = GL_SRC_COLOR;           // D3DBLEND_ZERO / D3DBLEND_SRCCOLOR
    } else {
        *src = GL_SRC_ALPHA;
        *dst = GL_ONE_MINUS_SRC_ALPHA;
    }
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
    u_tex_ = glGetUniformLocation(program_, "u_tex");
    u_textured_ = glGetUniformLocation(program_, "u_textured");
    u_alpha_test_ = glGetUniformLocation(program_, "u_alpha_test");

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
    for (int slot = 0; slot < kSpriteSlots; ++slot) {
        for (int i = 0; i < sprites_[slot].frame_count; ++i) {
            if (sprites_[slot].textures[i]) glDeleteTextures(1, &sprites_[slot].textures[i]);
            sprites_[slot].textures[i] = 0;
        }
        sprites_[slot].frame_count = 0;
        sprites_[slot].sprite = igi::SmokeSprite{};
    }
    loaded_sprites_ = 0;
    sprites_attempted_ = false;
    sprites_root_.clear();
    if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    if (program_) { glDeleteProgram(program_); program_ = 0; }
}

void Renderer_Smoke::Clear() {
    sim_.Clear();
}

int Renderer_Smoke::SpriteSlotForIndex(int sprite_index) const {
    // Smoke_Changed's switch (smoke.c:380-393): 0 fireball, 1 blood, 2 smoke2,
    // anything else smoke1. The slot numbers here are the same order as
    // igi::SmokeSpriteFile's file names, so slot == (0..2) or 3 for the default.
    if (sprite_index >= 0 && sprite_index <= 2) return sprite_index;
    return 3;
}

const igi::SmokeSprite* Renderer_Smoke::Sprite(int slot) const {
    if (slot < 0 || slot >= kSpriteSlots) return nullptr;
    return sprites_[slot].sprite.valid ? &sprites_[slot].sprite : nullptr;
}

std::vector<std::string> Renderer_Smoke::SpriteReport() const {
    std::vector<std::string> out;
    static const char* kNames[4] = {"fireball.spr", "blood1.spr", "smoke2.spr", "smoke1.spr"};
    for (int slot = 0; slot < kSpriteSlots; ++slot) {
        const igi::SmokeSprite& s = sprites_[slot].sprite;
        if (!s.valid) continue;
        int w = 0, h = 0;
        if (!s.frames.empty()) {
            w = s.frames.front().width;
            h = s.frames.front().height;
        }
        out.push_back(std::string(kNames[slot]) + " " + std::to_string(w) + "x" + std::to_string(h) +
                      " fmt=" + std::to_string(s.format) +
                      " frames=" + std::to_string(s.frames.size()) +
                      " cols=" + std::to_string(s.charmap_cols));
    }
    return out;
}

int Renderer_Smoke::LoadSprites() {
    static const char* kFiles[4] = {"fireball.spr", "blood1.spr", "smoke2.spr", "smoke1.spr"};

    // The game root the engine's "LOCAL:" device would resolve against
    // (LocalDevice_Open strips the prefix, src/sysmisc/localdev.c:162-205).
    const std::string root = Utils::GetIGIRootPath();
    const std::string pack = igi_res::LocalDevicePath(root, igi::kSmokeSpritePackName);
    sprites_root_ = root;
    sprites_attempted_ = true;
    loaded_sprites_ = 0;
    for (int slot = 0; slot < kSpriteSlots; ++slot) {
        sprites_[slot].sprite = igi::SmokeSprite{};
        sprites_[slot].frame_count = 0;
    }

    std::error_code ec;
    if (!std::filesystem::exists(pack, ec)) {
        Logger::Get().Log(LogLevel::WARNING,
            "[Renderer_Smoke] sprite pack NOT FOUND: " + pack +
            " -- falling back to the procedural radial falloff (the .spr texels are the game's)");
        return 0;
    }

    int cols[4] = {0, 0, 0, 0};
    for (int slot = 0; slot < kSpriteSlots; ++slot) {
        // Game_LoadSprites builds "LOCAL:common/sprites/<name>" (game.c:536-547)
        // and Resource_Load matches it as the engine's resource identity, so the
        // member name is built the same way here.
        const std::string member = igi::SmokeSpriteMemberName(kFiles[slot]);
        const std::vector<uint8_t> bytes = RES_Extract(pack, member);
        if (bytes.empty()) {
            Logger::Get().Log(LogLevel::WARNING, "[Renderer_Smoke] sprite missing from pack: " +
                member + " in " + pack);
            continue;
        }
        igi::SmokeSprite parsed;
        if (!igi::SmokeSpriteParse(bytes.data(), bytes.size(), kFiles[slot], &parsed)) {
            Logger::Get().Log(LogLevel::WARNING, "[Renderer_Smoke] sprite decode failed: " +
                member + ": " + parsed.error);
            continue;
        }
        sprites_[slot].sprite = std::move(parsed);
        sprites_[slot].frame_count = static_cast<int>(sprites_[slot].sprite.frames.size());
        // Particle2D_New's `animated` and Particle2D_Run's `last` come from the
        // sprite's char map (particle.c:519-520, :271-272); hand the simulation
        // the column count so a particle's frame advances exactly when the
        // engine's does (never for smoke1/smoke2/fireball, once per tick for
        // blood1.spr).
        cols[slot] = sprites_[slot].sprite.Animated() ? sprites_[slot].sprite.charmap_cols : 0;
        ++loaded_sprites_;
    }

    sim_.SetSpriteColumns(cols);
    // The evidence line: one row per slot, straight from the shipped archive.
    for (const std::string& line : SpriteReport()) {
        Logger::Get().Log(LogLevel::INFO, "[Renderer_Smoke] sprite " + line + " from " + pack);
    }
    if (loaded_sprites_ == 0) {
        Logger::Get().Log(LogLevel::WARNING,
            "[Renderer_Smoke] no retail sprite could be loaded -- falling back to the procedural "
            "radial falloff; the quads' texels are NOT the game's");
    }
    return loaded_sprites_;
}

void Renderer_Smoke::SetEmitters(const std::vector<igi::SmokeParams>& emitters) {
    // Load the retail sprites before the groups are built, so each group carries
    // the engine's frame count (smoke_sim.h "SetSpriteColumns"). One attempt per
    // game root: a re-run against a different install re-reads the pack.
    const std::string root = Utils::GetIGIRootPath();
    if (!sprites_attempted_ || root != sprites_root_) LoadSprites();
    sim_.SetEmitters(emitters);
}

GLuint Renderer_Smoke::UploadFrame(const igi::SmokeSpriteFrame& frame) {
    if (frame.width <= 0 || frame.height <= 0 || frame.rgba.empty()) return 0;
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    // D3DQVertex_SetTextureStates 0x50DE00 (src/qvertex/d3d.c:592-603): the
    // particles' 0x1000000 flag selects LINEAR min/mag, MIPFILTER is NONE, and
    // D3DTADDRESS_CLAMP unless 0x400000 is set (it never is for Smoke:
    // Smoke_Changed's three fade-mode constants are 0x1080000/0x1000008/
    // 0x1000010 and Particle2D_New adds 0x40000, particle.c:515).
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, frame.width, frame.height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, frame.rgba.data());
    return tex;
}

Renderer_Smoke::Batch* Renderer_Smoke::BatchFor(int slot, int frame, std::uint32_t flags,
                                                std::size_t first) {
    for (Batch& b : batches_) {
        if (b.slot == slot && b.frame == frame && b.flags == flags) return &b;
    }
    Batch b;
    b.slot = slot;
    b.frame = frame;
    b.flags = flags;
    b.first = first;
    b.count = 0;
    batches_.push_back(b);
    return &batches_.back();
}

void Renderer_Smoke::Draw(GLuint ubo_mats, const glm::vec3& camera_pos, const glm::vec3& camera_right,
                          const glm::vec3& camera_up, const glm::vec3& camera_forward) {
    // ONE engine tick per rendered frame (renderer_smoke.h).
    sim_.Tick();

    if (program_ == 0) return;

    // The textures are uploaded on the first draw that has a GL context; the
    // parse itself happens in SetEmitters/LoadSprites and needs no context.
    if (!sprites_attempted_) LoadSprites();
    for (int slot = 0; slot < kSpriteSlots; ++slot) {
        LoadedSprite& ls = sprites_[slot];
        if (!ls.sprite.valid || ls.frame_count != static_cast<int>(ls.sprite.frames.size())) continue;
        for (int i = 0; i < ls.frame_count; ++i) {
            if (ls.textures[i] == 0) ls.textures[i] = UploadFrame(ls.sprite.frames[static_cast<std::size_t>(i)]);
        }
    }

    // ── Pass 1: the quads, in the engine's SORT ORDER ───────────────────────
    //
    // Particle2D_Draw submits ONE QVertex_DrawQuadEx per particle
    // (particle.c:205-206), so one particle is one strip. D3DQVertex_Emit sees
    // the group's 0x40000 bit, so it pushes the strip into the sort renderer
    // keyed on the strip's first z (`SortRender_Add(..., QV_Bits(&s->verts[0].z))`,
    // src/qvertex/d3d.c:373-376). SortRender_Sort's radix sort puts positive
    // keys out LARGEST first (src/res/sortrender.c:55-87), and the projected z
    // grows with distance (Particle2D_Draw's own gate is `depth > kNear` and
    // `depth < maxDepth`, particle.c:143), so the engine draws its particles
    // BACK TO FRONT. The editor drew them in pool order, which made
    // alpha-blended puffs composite in birth order instead of by depth.
    //
    // The sort is per-quad and STABLE, like the radix sort: quads at equal
    // depth keep the pool order. Only the particles are sorted among themselves
    // (the engine's list also carries the frame's opaque strips, which the depth
    // buffer already orders correctly here).
    quads_.clear();
    vertices_.clear();
    batches_.clear();
    for (std::size_t e = 0; e < sim_.EmitterCount(); ++e) {
        const igi::SmokeEmitterState& emitter = sim_.Emitter(e);
        const igi::SmokeParticleGroup& group = emitter.group;
        if (!group.exists || group.particles.empty()) continue;

        // Smoke_Changed's sprite slot for this emitter (smoke.c:380-393).
        const int slot = SpriteSlotForIndex(emitter.params.sprite_index);
        const igi::SmokeSprite* sprite = Sprite(slot);
        // Particle2D_Draw ORs 0x1000000 into the flags before the quad is
        // submitted (particle.c:207); the group's own flags came from
        // Smoke_Changed + Particle2D_New (smoke.c:366-379, particle.c:515).
        const std::uint32_t flags = group.flags | kSmoothFlag;

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

            // particle.c:194-203 through QVertex_DrawQuadEx's gColorScale:
            // SmokeParticleColour (source/runtime/smoke.cpp) owns the whole rule,
            // including the level-is-alpha branch and the 1/128 scale.
            float rgba[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            igi::SmokeParticleColour(group.colour, flags, p.level, rgba);
            if (!(rgba[3] > 0.0f)) continue;

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

            Quad quad;
            quad.depth = depth;
            quad.centre = centre;
            quad.slot = slot;
            // Particle2D_Draw's frame (particle.c:200-205): the char map's
            // 1-based cell for this particle, else 0.
            quad.frame = sprite ? sprite->FrameForParticle(p.frame) : 0;
            quad.flags = flags;
            quad.rgba[0] = rgba[0];
            quad.rgba[1] = rgba[1];
            quad.rgba[2] = rgba[2];
            quad.rgba[3] = rgba[3];
            quad.corners[0] = glm::vec2(t1 - t2, hc - t3);
            quad.corners[1] = glm::vec2(t2 + t1, t3 + hc);
            quad.corners[2] = glm::vec2(sh - t2, ch - t3);
            quad.corners[3] = glm::vec2(sh + t2, ch + t3);
            quads_.push_back(quad);
        }
    }

    // Back to front: SortRender_Sort's positive keys come out largest first.
    std::stable_sort(quads_.begin(), quads_.end(),
                     [](const Quad& a, const Quad& b) { return a.depth > b.depth; });

    // ── Pass 2: vertices, batched over CONSECUTIVE quads that share a texture
    // and a blend state (the engine rebinds per strip; a run of equal strips is
    // the same sequence of draws with fewer state changes). ──────────────────
    for (const Quad& quad : quads_) {
        const std::size_t firstVertex = vertices_.size() / kFloatsPerVertex;
        Batch* batch = batches_.empty() ? nullptr : &batches_.back();
        if (batch == nullptr || batch->slot != quad.slot || batch->frame != quad.frame ||
            batch->flags != quad.flags) {
            batch = BatchFor(quad.slot, quad.frame, quad.flags, firstVertex);
        }
        for (int i = 0; i < kVerticesPerParticle; ++i) {
            const int corner_index = kCornerIndex[i];
            const glm::vec2& corner = quad.corners[corner_index];
            const glm::vec3 world = quad.centre + camera_right * corner.x - camera_up * corner.y;
            float vertex[kFloatsPerVertex];
            WriteVertex(vertex, world, quad.rgba[0], quad.rgba[1], quad.rgba[2], quad.rgba[3],
                        kCornerUv[corner_index][0], kCornerUv[corner_index][1]);
            vertices_.insert(vertices_.end(), vertex, vertex + kFloatsPerVertex);
            batch->count++;
        }
    }

    if (vertices_.empty()) return;

    glUseProgram(program_);
    const GLuint blk = glGetUniformBlockIndex(program_, "Matrices");
    if (blk != GL_INVALID_INDEX) glUniformBlockBinding(program_, blk, 0);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo_mats);
    if (u_tex_ >= 0) glUniform1i(u_tex_, 0);

    // Particle2D_Draw's state: the particles blend and are depth-tested with
    // ZFUNC LEQUAL but do not write depth (flags 0x40000, D3DQVertex_SetRenderStates
    // 0x50DCC0, src/qvertex/d3d.c:579-583), so they do not occlude each other.
    GLboolean depth_mask_was;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask_was);
    GLint depth_func_was = GL_LESS;
    glGetIntegerv(GL_DEPTH_FUNC, &depth_func_was);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    const GLboolean cull_was_enabled = glIsEnabled(GL_CULL_FACE);
    if (cull_was_enabled) glDisable(GL_CULL_FACE);
    glActiveTexture(GL_TEXTURE0);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(vertices_.size() * sizeof(float)), vertices_.data(),
                 GL_STREAM_DRAW);

    // One draw per (sprite, frame, flags): the engine binds one texture per
    // Particle2D group and sets the group's blend/render states once
    // (particle.c:205-207 -> QVertex_DrawQuadEx -> D3DQVertex_Strip).
    for (const Batch& b : batches_) {
        GLenum src = GL_SRC_ALPHA;
        GLenum dst = GL_ONE_MINUS_SRC_ALPHA;
        BlendForFlags(b.flags, &src, &dst);
        glBlendFunc(src, dst);
        const bool alpha_test = (b.flags & kAlphaTestMask) != 0;
        if (u_alpha_test_ >= 0) glUniform1i(u_alpha_test_, alpha_test ? 1 : 0);
        // D3DQVertex_SetRenderStates' z branch (src/qvertex/d3d.c:574-585). A
        // Smoke group always has 0x40000 (particle.c:515), so this is on for
        // every quad the emitter draws; the else arm is the engine's own state
        // for a group without it.
        if (b.flags & kZTestFlag) {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_FALSE);
        } else {
            glDisable(GL_DEPTH_TEST);
        }

        GLuint texture = 0;
        if (b.slot >= 0 && b.slot < kSpriteSlots) {
            LoadedSprite& ls = sprites_[b.slot];
            if (b.frame >= 0 && b.frame < ls.frame_count) texture = ls.textures[b.frame];
        }
        glBindTexture(GL_TEXTURE_2D, texture);
        if (u_textured_ >= 0) glUniform1i(u_textured_, texture != 0 ? 1 : 0);
        if (b.count > 0) glDrawArrays(GL_TRIANGLES, (GLint)b.first, (GLsizei)b.count);
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDepthFunc((GLenum)depth_func_was);
    glDepthMask(depth_mask_was);
    glDisable(GL_BLEND);
    if (cull_was_enabled) glEnable(GL_CULL_FACE);
    glUseProgram(0);
    GL_UnbindForImmediateMode();

    // IGI_PROFILE-only, deterministic (counts, no time): the editor's GUI cannot
    // run on this host (GL 4.1 vs 2.1), so a headless lane cannot see the
    // particles. This line is how a run PROVES the emitters are spawning:
    //
    //   [SmokeProfile] 30 frames: emitters=3 particles=900/frame max=900 ticks=.. quads=900/frame sprites=4
    //
    // `particles` is SmokeSimulation::TotalParticles() after the tick, `quads`
    // the number of billboards actually submitted this frame (they differ when
    // a particle is behind the camera), and `sprites` how many of the four
    // retail .spr slots carry texels.
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
                "/frame ticks=" + std::to_string(sim_.TickCount()) +
                " sprites=" + std::to_string(loaded_sprites_));
            acc_particles = 0;
            acc_quads = 0;
            frames = 0;
        }
    }
}
