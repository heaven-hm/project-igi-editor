#include "pch.h"
#include "renderer_rain.h"
#include "gl_helper.h"
#include "../runtime/weather_visibility.h"
#include "../runtime/rain_rules.h"
#include "../logger.h"
#include <freeglut.h>
#include <vector>
#include <random>

// The engine's weather particle system, transcribed from IGI-RE
// (/Users/haseeb-mir/Documents/Code/IGI-RE), function + HOOK + file:line:
//
//   Rain_Init       0x498AB0  src/dxinit/rain.c:169-178  (seeds gDxRainRandom)
//   Rain_New        0x498960  rain.c:144-167             (snow = rain == 0, pool, alpha)
//   Rain_Render     0x498B80  rain.c:207-217             (the fade gate)
//   Rain_UpdateGrid 0x498BD0  rain.c:220-278             (tile lattice, ground probe)
//   Rain_Draw       0x498F10  rain.c:280-303             (snapped z, 9x9 window, sort)
//   Rain_FillVB     0x499090  rain.c:307-393             (2-vertex line / 6-vertex quad)
//   Rain_DrawCell   0x499430  rain.c:397-486             (state, layer clip, primitive)
//   Rain_ClipCells  0x499910  rain.c:493-554             (the cell outcodes)
//   Rain_Update     0x499CF0  rain.c:565-637             (fade, fall, sway, spawn)
//   Rain_SetFalling 0x499F90  rain.c:643-645
//   Rain_SetAlpha   0x499FA0  rain.c:646-649             (trunc(alpha*255) in the top byte)
//   RainEffect_Run  0x436990  src/fxobj/rain.c:98-105    (SetFalling + Update per tick)
//   RainEffect_Changed 0x436A50 fxobj/rain.c:131-146     (rebuild unless only alpha moved)
//
// There is no separate snow path: `r->snow` (rain.c:153) selects the branch and
// every constant below comes from source/runtime/rain_rules.h.

// ─── shaders ──────────────────────────────────────────────────────────────────
// The engine's vertex format is D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1
// (Rain_CreateVB, rain.c:132-141), i.e. RainVertex { x, y, z, diffuse, u, v }
// (rain.c:70-75). Rain_FillVB writes the vertices in the space its world matrix
// maps back to world: a pure scale for rain (rain.c:389-391) and the camera
// basis for snow (rain.c:382-388). `u_tile_origin` is Rain_DrawCell's
// translation (rain.c:462-464) and `u_local_basis` that rotation.
static const char* WEATHER_VERT_SRC = R"(
#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec4 a_diffuse;
layout(location = 2) in vec2 a_uv;

layout(std140) uniform Matrices {
    mat4 u_unused1;
    mat4 u_unused2;
    mat4 u_mvp;
};

uniform vec3 u_tile_origin;   // world x/y of the drawn cell + that layer's base z
uniform mat3 u_local_basis;   // identity for rain, the camera basis for snow

out vec4 v_diffuse;
out vec2 v_uv;

void main() {
    gl_Position = u_mvp * vec4(u_tile_origin + u_local_basis * a_pos, 1.0);
    v_diffuse = a_diffuse;
    v_uv = a_uv;
}
)";

// Rain_DrawCell's rain branch (rain.c:448-459): SetTexture(device, 0, NULL),
// COLOROP/ALPHAOP = SELECTARG2 (`k = 3`), i.e. the vertex diffuse alone, and
// SET_TSS(1, 1, 1) disables stage 1. The 2-vertex line's colours are
// 0x00FFFFFF on top and r->color below (rain.c:366-367).
static const char* RAIN_FRAG_SRC = R"(
#version 330 core
in vec4 v_diffuse;
in vec2 v_uv;
out vec4 fragColor;

void main() {
    fragColor = v_diffuse;
}
)";

// Rain_DrawCell's snow branch (rain.c:430-447): the game's snow.tex at stage 0
// with COLOROP/ALPHAOP = MODULATE of TEXTURE x DIFFUSE (k = 4), or SELECTARG2
// (k = 3) when the texture pointer is NULL. Every quad vertex carries
// 0xFFFFFFFF (rain.c:353), so the diffuse factor is the vertex colour.
static const char* SNOW_FRAG_SRC = R"(
#version 330 core
uniform sampler2D u_texture;
uniform int u_has_texture;
in vec4 v_diffuse;
in vec2 v_uv;
out vec4 fragColor;

void main() {
    vec4 texel = u_has_texture != 0 ? texture(u_texture, v_uv) : vec4(1.0);
    fragColor = v_diffuse * texel;
}
)";

// ─── GLSL 1.20 variants (legacy 2.1 context) ──────────────────────────────────
// Language ports only: identical attributes, uniforms, varyings and arithmetic.
// The one structural difference is the shared `Matrices` uniform block, which
// GLSL 1.20 does not have; its third matrix becomes the plain uniform `u_mvp`,
// matching what gl_helper.cpp's UBO emulation uploads. Attribute indices come
// from GL_LegacyBindInlineAttribLocations() before glLinkProgram.
static const char* WEATHER_VERT_SRC_120 = R"(
#version 120
attribute vec3 a_pos;
attribute vec4 a_diffuse;
attribute vec2 a_uv;
uniform mat4 u_mvp;
uniform vec3 u_tile_origin;
uniform mat3 u_local_basis;
varying vec4 v_diffuse;
varying vec2 v_uv;
void main() {
    gl_Position = u_mvp * vec4(u_tile_origin + u_local_basis * a_pos, 1.0);
    v_diffuse = a_diffuse;
    v_uv = a_uv;
}
)";

static const char* RAIN_FRAG_SRC_120 = R"(
#version 120
varying vec4 v_diffuse;
varying vec2 v_uv;
void main() {
    gl_FragColor = v_diffuse;
}
)";

static const char* SNOW_FRAG_SRC_120 = R"(
#version 120
uniform sampler2D u_texture;
uniform int u_has_texture;
varying vec4 v_diffuse;
varying vec2 v_uv;
void main() {
    vec4 texel = u_has_texture != 0 ? texture2D(u_texture, v_uv) : vec4(1.0);
    gl_FragColor = v_diffuse * texel;
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
    // GLSL 1.20 has no layout(location = N), so the attribute indices the 330
    // originals declare must be re-bound by name before linking.
    if (g_gl_info.legacy_gl21_) {
        GL_LegacyBindInlineAttribLocations(program);
    }
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

namespace {

// ─── the Rain task's state ────────────────────────────────────────────────────
// One RainDrop (rain.c:40-46): x/y are tile coordinates Rain_Update wraps inside
// [0, kTileF) (rain.c:603-616) and z is the position inside the 6 m band that
// wraps at kLayer (rain.c:587-591). `kind` is both the vertex group Rain_FillVB
// writes into (rain.c:333-334,378) and the parity bits the x/y wraps toggle.
struct WeatherDrop {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    int timer = 0;
    float dx = 0.0f;
    float dy = 0.0f;
    int kind = 0;
};

// One RainCell as Rain_ClipCells reads and writes it (rain.c:497-551): the world
// position Rain_UpdateGrid probed, the mean view depth Rain_Draw sorts by and
// the outcode word. `x == 0 && y == 0 && flags == 0` is "never probed".
struct WeatherCell {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double depth = 0.0;
    unsigned flags = 0;
};

// Rain_ClipCells walks a 10x10 window from the same origin as Rain_Draw's 9x9
// one (rain.c:497,502-503) because the block test reads one cell past it.
constexpr int kWindowStride = igi::rain::kClipWindowTiles;

// The engine's single vertex buffer holds four groups of 480 vertices
// (Rain_CreateVB: `dwNumVertices = 0x780`, rain.c:139; the groups are
// Rain_FillVB's `p[i] = p[i-1] + 480`, rain.c:327-330) and the draw call offsets
// by `q * 480` (rain.c:447,458). 480 is 80 drops x the SNOW vertex count, so the
// group block is the same for both modes in the engine; the editor keeps the
// same 480-vertex block and simply draws 2 or 6 vertices per drop inside it.
constexpr int kGroupStrideVertices = igi::rain::kDropCapacity * 6;
constexpr int kVertexFloats = 9;  // RainVertex: x, y, z, diffuse, u, v (rain.c:70-75)
constexpr int kVertexCount = kGroupStrideVertices * igi::rain::kDropKindCount;

struct WeatherState {
    WeatherDrop drops[igi::rain::kDropCapacity];  // rain.c:65 `RainDrop drops[80]`
    int drop_count = 0;                           // Rain::dropCount
    float spawn = 0.0f;                           // Rain::spawn (rain.c:619-636)
    float fade = 0.0f;                            // Rain::fade, 0 at Rain_New (rain.c:155)
    int snow = 0;                                 // rain.c:153
    // The editor's preview multiplier on the engine's fall speed. 1.0 == the
    // engine's own kSnowSpeed/kRainSpeed (rain.c:122-123); not a Rain_New field.
    float speed_scale = 1.0f;
    unsigned color = 0;                           // Rain::color, Rain_SetAlpha (rain.c:648)
    bool falling = false;                         // Rain::falling (Rain_SetFalling)
    // Rain::cells, re-indexed to the 10x10 window the clip and the draw use
    // instead of the engine's 24x24 mirrored array. The window is x-major with a
    // stride of 10, so c+1 is +y and c+10 is +x, exactly as the engine's c+1 and
    // c+24 are (rain.c:288-292).
    WeatherCell cells[kWindowStride * kWindowStride];
    bool has_last = false;                        // Rain::hasLast
    double last_x = 0.0;                          // Rain::last
    double last_y = 0.0;
    // RainList::z[80] per vertex group (rain.c:48-51,378), the per-layer clip's
    // input (rain.c:479-483).
    float list_z[igi::rain::kDropKindCount][igi::rain::kDropCapacity];
    int list_count[igi::rain::kDropKindCount] = {0, 0, 0, 0};
    std::vector<float> vertices;                  // 4 groups x 480 RainVertex
    // Rain_Init seeds gDxRainRandom once (rain.c:174). The editor uses its own
    // generator; the draw ORDER and the ranges are the engine's (rain.c:596-598,
    // 627-632), the stream is not.
    std::mt19937 rng{204800};
};

WeatherState& Weather() {
    static WeatherState state;
    return state;
}

float RandomUnit(WeatherState& s) {
    return std::uniform_real_distribution<float>(0.0f, 1.0f)(s.rng);
}

int RandomInt(WeatherState& s, int n) {
    return std::uniform_int_distribution<int>(0, n - 1)(s.rng);
}

void WriteVertex(float* v, float x, float y, float z, std::uint32_t diffuse,
                 float u, float uv_v) {
    v[0] = x;
    v[1] = y;
    v[2] = z;
    v[3] = static_cast<float>((diffuse >> 16) & 0xFFu) / 255.0f;
    v[4] = static_cast<float>((diffuse >> 8) & 0xFFu) / 255.0f;
    v[5] = static_cast<float>(diffuse & 0xFFu) / 255.0f;
    v[6] = static_cast<float>((diffuse >> 24) & 0xFFu) / 255.0f;
    v[7] = u;
    v[8] = uv_v;
}

// Rain_New's field initialisation (rain.c:146-166): an empty pool at fade 0, a
// cleared cell array and the alpha the object is created with. The caller then
// applies the authored alpha through Rain_SetAlpha, exactly as
// RainEffect_Changed does (fxobj/rain.c:141-143).
void ResetWeather(WeatherState& s, int snow) {
    s.drop_count = 0;
    s.spawn = 0.0f;
    s.fade = 0.0f;
    s.snow = snow;
    s.has_last = false;
    s.last_x = 0.0;
    s.last_y = 0.0;
    for (WeatherCell& c : s.cells) c = WeatherCell{};
    for (int k = 0; k < igi::rain::kDropKindCount; ++k) s.list_count[k] = 0;
    s.color = igi::rain::RainStreakBottomDiffuse(0.3125f);  // Rain_New, rain.c:164
}

// Rain_SetAlpha HOOK 0x499FA0 (rain.c:646-649).
void SetWeatherAlpha(WeatherState& s, float alpha) {
    s.color = igi::rain::RainStreakBottomDiffuse(alpha);
}

// Rain_Update HOOK 0x499CF0 (rain.c:565-637): the fade ramp, the fall, the snow
// sway/wrap and the spawn accumulator. RainEffect_Run calls it once per tick
// whether or not the effect is falling (fxobj/rain.c:104).
//
// `speed_scale` is the EDITOR's preview multiplier and has no engine
// counterpart: the engine's speed is the pair of constants at rain.c:122-123
// selected by `r->snow` at rain.c:572. Scaling that single value scales the
// fall, the wrap, the spawn accumulator and the spawn z together, which is the
// only way the preview stays internally consistent.
void UpdateWeather(WeatherState& s) {
    const float speed =
        igi::rain::SpeedUnitsPerTick(s.snow != 0) * s.speed_scale;  // rain.c:572

    // rain.c:573-585 - one kFadeStep per tick, clamped, and the update stops
    // dead at zero (Rain_Render skips the draw there too, rain.c:209-212).
    float f = s.falling ? s.fade + igi::rain::kFadeStep : s.fade - igi::rain::kFadeStep;
    if (s.falling) {
        if (!(f < 1.0f)) f = 1.0f;  // `if (!(f < kOne)) f = kOne`
    } else if (!(f > 0.0f)) {
        f = 0.0f;                   // `if (!(f > kZero)) f = kZero`
    }
    s.fade = f;
    if (s.fade == 0.0f) return;     // x87_eq(r->fade, kZero), rain.c:584-585

    for (int i = 0; i < s.drop_count; ++i) {
        WeatherDrop& d = s.drops[i];

        // rain.c:587-591 - `z = d->z - speed; if (z < 0) d->z = z + kLayer;`
        d.z = igi::rain::WrapZ(d.z, speed);
        if (!s.snow) continue;

        // rain.c:592-617, the snow-only branch.
        // rain.c:595-602 - `if (d->timer-- < 0) { ... }`: the decrement happens
        // before the test, so the reload lands on an already-decremented timer
        // (SnowSwayTimerAfterReload).
        const int before_decrement = d.timer;
        d.timer = before_decrement - 1;
        if (before_decrement < 0) {
            d.timer += igi::rain::SnowSwayReloadTicks(
                RandomInt(s, igi::rain::kSwayTicksMin));  // Random_Int(r, 30)
            d.dx = igi::rain::SnowSwayDisplacementUnits(RandomUnit(s));  // rain.c:597
            d.dy = igi::rain::SnowSwayDisplacementUnits(RandomUnit(s));  // rain.c:598
        }
        const float x = d.dx + d.x;
        d.x = x;                     // rain.c:600-601
        d.y = d.dy + d.y;            // rain.c:602

        // rain.c:603-616 - one kTileF step either way plus the kind bit that
        // keeps the drop in the tile parity class it is drawn with.
        const igi::rain::SnowTileWrap wrap_x = igi::rain::SnowWrapTileX(x);
        if (wrap_x.wrapped) {
            d.x = wrap_x.value;
            d.kind ^= igi::rain::kSnowWrapKindBitX;
        }
        const igi::rain::SnowTileWrap wrap_y = igi::rain::SnowWrapTileY(d.y);
        if (wrap_y.wrapped) {
            d.y = wrap_y.value;
            d.kind ^= igi::rain::kSnowWrapKindBitY;
        }
    }

    // rain.c:619-636 - the accumulator emits one drop per whole kSpawnStep and
    // the pool is never compacted: at 80 drops the engine returns early.
    const int spawned = igi::rain::SpawnCountForTick(speed, s.spawn, s.drop_count);
    for (int i = 0; i < spawned && s.drop_count < igi::rain::kDropCapacity; ++i) {
        WeatherDrop& d = s.drops[s.drop_count];

        const float x = RandomUnit(s) * igi::rain::kTileUnitsF;  // rain.c:627
        d.y = RandomUnit(s) * igi::rain::kTileUnitsF;            // rain.c:628
        d.x = x;
        d.timer = 0;                                             // rain.c:630
        // rain.c:631 - `d->z = kLayer - speed`, the SAME `speed` the fall and
        // the accumulator use, so the preview multiplier applies here too.
        d.z = igi::rain::kLayerUnits - speed;
        d.kind = RandomInt(s, igi::rain::DropKindCount());       // rain.c:632
        // rain.c:626-632 never initialises dx/dy; the engine reads whatever the
        // slot held. The port pins them to 0 instead of reading uninitialised
        // memory. The first roll replaces them on the drop's second tick
        // (SnowSwayFirstRollTick).
        d.dx = 0.0f;
        d.dy = 0.0f;
        ++s.drop_count;
    }
}

// The engine's rain vertices, written by Rain_FillVB HOOK 0x499090
// (rain.c:307-393), into the group block of the drop's kind.
void FillWeatherVertices(WeatherState& s, const Renderer_Rain::Camera& camera) {
    const float size = s.fade * igi::rain::SnowFlakeSizeUnits();      // rain.c:309
    const float len = s.fade * igi::rain::RainStreakLengthUnits();    // rain.c:311
    const unsigned color = s.color;                                   // rain.c:310
    const int verts_per = s.snow ? igi::rain::SnowVerticesPerParticle()
                                 : igi::rain::RainVerticesPerParticle();
    const int group_stride = igi::rain::kDropCapacity * verts_per;

    // rain.c:313-321 - the drops are written starting at the HIGHEST one and
    // walking backwards through the array (cyclically). The scan compares
    // drops[0..dropCount-2] only, the engine's own loop bounds.
    int top = 0;
    if (s.drop_count > 1) {
        for (int e = 0; e < s.drop_count - 1; ++e)
            if (s.drops[e].z > s.drops[top].z) top = e;
    }

    for (int k = 0; k < igi::rain::kDropKindCount; ++k) s.list_count[k] = 0;
    int used[igi::rain::kDropKindCount] = {0, 0, 0, 0};

    int d = top;
    for (int i = 0; i < s.drop_count; ++i) {
        const WeatherDrop& drop = s.drops[d];
        const int k = drop.kind;
        float* v = &s.vertices[static_cast<std::size_t>(k * group_stride + used[k]) * kVertexFloats];

        if (s.snow) {
            // rain.c:337-364 - the flake is a camera-facing quad of six vertices
            // built in the camera's own axes (`vx/vy/vz` are gTransContext.matrix
            // rows 0-2 dotted with the drop), with v4 = v1 and v5 = v2. The
            // corner -> uv order is (0,0) (1,0) (0,1) (1,1) (1,0) (0,1).
            const glm::vec3 view = camera.ToView(glm::vec3(drop.x, drop.y, drop.z));
            const float x0 = view.x - size, x1 = view.x + size;
            const float y0 = view.y - size, y1 = view.y + size;
            const float corners[6][3] = {
                {x0, y0, view.z}, {x1, y0, view.z}, {x0, y1, view.z},
                {x1, y1, view.z}, {x1, y0, view.z}, {x0, y1, view.z},
            };
            const float uvs[6][2] = {
                {0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f},
                {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 1.0f},
            };
            for (int c = 0; c < igi::rain::SnowVerticesPerParticle(); ++c) {
                // rain.c:353 - all four corners (and the two copies) carry
                // 0xFFFFFFFF, the raw 8-bit D3DCOLOR of a D3DFVF_DIFFUSE vertex.
                WriteVertex(v + c * kVertexFloats, corners[c][0], corners[c][1], corners[c][2],
                            igi::rain::SnowFlakeDiffuse(), uvs[c][0], uvs[c][1]);
            }
            used[k] += igi::rain::SnowVerticesPerParticle();
        } else {
            // rain.c:366-372 - two vertices: the top one is the literal
            // 0x00FFFFFF (white, alpha 0) one streak length above the bottom,
            // which carries r->color (white with trunc(alpha * 255)).
            WriteVertex(v, drop.x, drop.y, len + drop.z,
                        igi::rain::RainStreakTopDiffuse(), 0.0f, 0.0f);
            WriteVertex(v + kVertexFloats, drop.x, drop.y, drop.z, color, 0.0f, 0.0f);
            used[k] += igi::rain::RainVerticesPerParticle();
        }

        // rain.c:374-378 - the z list is filled AFTER the cyclic decrement, so
        // the entry recorded for this vertex is the NEXT drop's z. That shift is
        // the engine's, and Rain_DrawCell's per-layer clip reads this list
        // (rain.c:479-483).
        d = (d == 0) ? s.drop_count - 1 : d - 1;
        s.list_z[k][s.list_count[k]++] = s.drops[d].z;
    }
}

}  // namespace

bool Renderer_Rain::Init() {
    const char* vs = g_gl_info.legacy_gl21_ ? WEATHER_VERT_SRC_120 : WEATHER_VERT_SRC;
    const char* rain_fs = g_gl_info.legacy_gl21_ ? RAIN_FRAG_SRC_120 : RAIN_FRAG_SRC;
    const char* snow_fs = g_gl_info.legacy_gl21_ ? SNOW_FRAG_SRC_120 : SNOW_FRAG_SRC;
    rain_program_ = LinkWeatherProgram(vs, rain_fs, ubo_binding_point_);
    snow_program_ = LinkWeatherProgram(vs, snow_fs, ubo_binding_point_);
    if (!rain_program_ || !snow_program_) {
        Shutdown();
        return false;
    }

    WeatherState& s = Weather();
    s.vertices.assign(static_cast<std::size_t>(kVertexCount) * kVertexFloats, 0.0f);
    ResetWeather(s, 0);

    // One vertex buffer with the engine's four 480-vertex groups
    // (Rain_CreateVB, rain.c:132-141). Rain_FillVB rewrites the whole buffer
    // every tick (rain.c:307-393), so it is GL_DYNAMIC_DRAW.
    glGenVertexArrays(1, &vao_weather_);
    glGenBuffers(1, &vbo_weather_);
    glBindVertexArray(vao_weather_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_weather_);
    glBufferData(GL_ARRAY_BUFFER, s.vertices.size() * sizeof(float), s.vertices.data(),
                 GL_DYNAMIC_DRAW);
    const GLsizei stride = kVertexFloats * sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(7 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);

    // fxobj/rain.c:82 registers "snow.tex" once, for both modes, and
    // Rain_DrawCell's snow branch binds it (rain.c:430-434) while the rain branch
    // binds NULL (rain.c:449). The handle is resolved per frame by the caller
    // through the engine's own identity and pack
    // (Renderer_Objects::GetOrLoadGameTexture over igi_res::GameTextureMemberName
    // -> "LOCAL:common/textures/snow.tex", mounted at src/game/game.c:430); a
    // failed resolution passes 0, which is the engine's null-texture branch
    // (rain.c:435-438).
    Logger::Get().Log(LogLevel::INFO, std::string("[Renderer_Rain] Snow texture (fxobj/rain.c:82): ") +
                                          igi::rain::SnowTextureName() +
                                          " as LOCAL:common/textures/" + igi::rain::SnowTextureName());

    // The editor draws fixed-function overlays after the scene. Leaving the
    // particle VBO bound lets affected drivers dereference NULL+0x444 from
    // glBegin, so restore the immediate-mode-safe binding state here.
    GL_UnbindForImmediateMode();

    Logger::Get().Log(LogLevel::INFO, "[Renderer_Rain] Init OK.");
    return true;
}

void Renderer_Rain::Shutdown() {
    if (vbo_weather_) { glDeleteBuffers(1, &vbo_weather_); vbo_weather_ = 0; }
    if (vao_weather_) { glDeleteVertexArrays(1, &vao_weather_); vao_weather_ = 0; }
    if (rain_program_) { glDeleteProgram(rain_program_); rain_program_ = 0; }
    if (snow_program_) { glDeleteProgram(snow_program_); snow_program_ = 0; }
}

void Renderer_Rain::SetParams(bool active, bool is_snow, float startMeters, float endMeters,
                              float alpha) {
    // RainEffect_Changed HOOK 0x436A50 (fxobj/rain.c:131-146) frees and rebuilds
    // the Rain object whenever anything but the level (the alpha) changed;
    // fxobj/rain.c:138-139 scales the two authored heights by 4096 and
    // Rain_New stores height = start, fallDist = start + end (rain.c:159-160).
    const float height = startMeters * igi::rain::kUnitsPerMetre;
    const float fall_dist = igi::rain::FallDistanceUnits(startMeters, endMeters);
    const bool snow = is_snow;
    const bool rebuild = (snow != is_snow_) || (height != height_units_) ||
                         (fall_dist != fall_dist_units_) || !object_exists_;

    WeatherState& s = Weather();
    if (rebuild) {
        ResetWeather(s, snow ? 1 : 0);   // Rain_New, rain.c:144-167
        object_exists_ = true;
    }
    SetWeatherAlpha(s, alpha);           // Rain_SetAlpha, rain.c:646-649

    height_units_ = height;
    fall_dist_units_ = fall_dist;
    active_ = active;
    is_snow_ = snow;

    if (active_) {
        Logger::Get().Log(LogLevel::INFO,
            std::string("[Renderer_Rain] ") + (is_snow_ ? "SNOW" : "RAIN") +
            " parity: IGI-RE Rain task - 80-drop pool, tile lattice, " +
            (is_snow_ ? "6-vertex camera-facing quad" : "2-vertex LINE") +
            " (rain.c:565-637,307-393)");
    }
}

// Rain_UpdateGrid HOOK 0x498BD0 (rain.c:220-278). The engine walks the 24x24
// mirrored array and drops a ray through at most `budget` cells; the editor
// walks the 10x10 window the clip and the draw consume (a re-indexing of the
// same world cells, x-major with a stride of 10) and asks the level for the
// ground height instead of calling Coll_DropPointsMat (rain.c:258-265): the
// engine's drop is a ray over `fallDist`, the editor's query is a height lookup
// that also stops on static collision geometry the engine's ray would hit.
void Renderer_Rain::UpdateGrid(const Camera& camera) {
    WeatherState& s = Weather();
    // rain.c:223-236 - the camera's snapped tile and the window origin.
    const igi::rain::GridPlacement grid = igi::rain::PlaceGrid(camera.pos.x, camera.pos.y);
    int remaining = igi::rain::ProbeBudgetForFrame(s.has_last, camera.pos.x, camera.pos.y,
                                                   s.last_x, s.last_y);
    s.last_x = camera.pos.x;   // rain.c:254-255
    s.last_y = camera.pos.y;
    s.has_last = true;

    for (int i = 0; i < kWindowStride; ++i) {
        const double x = igi::rain::DrawTileX(grid, i);
        for (int j = 0; j < kWindowStride; ++j) {
            const double y = igi::rain::DrawTileY(grid, j);
            WeatherCell& c = s.cells[i * kWindowStride + j];

            // rain.c:250 - a cell keeps its probed flag only while its world
            // position is unchanged; `if (!c->flags && budget-- > 0)` spends one
            // unit of the budget per unflagged cell, probed or not.
            c.flags &= (c.x == x && c.y == y) ? 0x20u : 0u;
            if (c.flags == 0 && remaining-- > 0) {
                c.x = x;
                c.y = y;
                float ground_z = 0.0f;
                if (ground_query_ && ground_query_(x, y, ground_z)) {
                    // rain.c:263-266 - `c->z = hit.pos.z; c->flags = 0x20;`
                    c.z = ground_z;
                    c.flags = 0x20;
                }
                // Without a ground answer the cell stays unprobed, which is the
                // state Rain_UpdateGrid leaves a cell it skipped (rain.c:255)
                // and which Rain_ClipCells turns into a word whose bit 5 is
                // clear (rain.c:504,506) - the block test at rain.c:290 then
                // rejects it.
            }
        }
    }
}

void Renderer_Rain::ClipCells(double z, const Camera& camera) {
    WeatherState& s = Weather();
    // gTransContext.matrix rows (rain.c:508-534): right, down, forward.
    igi::rain::ViewBasis basis;
    const glm::vec3 axes[3] = {camera.right, -camera.up, camera.forward};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            basis.m[i * 3 + j] = static_cast<double>(axes[i][j]);

    igi::rain::ClipViewport viewport;
    viewport.scale_x = camera.scale_x;
    viewport.scale_y = camera.scale_y;
    viewport.center_x = camera.center_x;
    viewport.center_y = camera.center_y;
    viewport.width = camera.viewport_width;
    viewport.height = camera.viewport_height;

    for (int i = 0; i < kWindowStride; ++i) {
        for (int j = 0; j < kWindowStride; ++j) {
            WeatherCell& c = s.cells[i * kWindowStride + j];
            const igi::rain::CellClip clip = igi::rain::ClipCell(
                (c.flags & 0x20u) != 0, c.x, c.y, z, camera.pos.x, camera.pos.y,
                camera.pos.z, basis, viewport);
            c.flags = clip.flags;
            c.depth = clip.depth;
        }
    }
}

void Renderer_Rain::DrawCells(GLuint ubo_mats, const Camera& camera, double z,
                              GLuint snowTexture) {
    WeatherState& s = Weather();
    const bool snow = s.snow != 0;
    GLuint program = snow ? snow_program_ : rain_program_;
    const GLenum primitive = snow ? GL_TRIANGLES : GL_LINES;   // rain.c:446,457
    const int mult = snow ? igi::rain::SnowVerticesPerParticle()
                          : igi::rain::RainVerticesPerParticle();  // rain.c:447,458
    const int group_stride = igi::rain::kDropCapacity * mult;

    glUseProgram(program);
    glBindBufferBase(GL_UNIFORM_BUFFER, ubo_binding_point_, ubo_mats);

    const GLint tile_origin_loc = glGetUniformLocation(program, "u_tile_origin");
    const GLint local_basis_loc = glGetUniformLocation(program, "u_local_basis");

    // Rain_DrawCell's stage-0 setup (rain.c:430-445). The rain branch binds NULL
    // and SELECTARG2 (rain.c:448-456), so it needs no texture at all.
    if (snow) {
        // Rain_DrawCell's stage-0 sampler states: SET_TSS(0, 0x10, 2) MAGFILTER =
        // D3DTEXF_LINEAR and SET_TSS(0, 0x11, 1) MINFILTER = D3DTEXF_POINT
        // (rain.c:444-445). MIPFILTER (0x12) is not set in that block, so the
        // D3D7 default D3DTEXF_NONE applies and no mip level is selected;
        // ADDRESSU/V are unset too and default to D3DTADDRESS_WRAP.
        // GL_RegisterTexture's shared defaults are mipmapped trilinear, so the
        // state is corrected here, every frame, because the handle is a GL name
        // the renderer's cache may reuse after a level switch.
        if (snowTexture != 0) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, snowTexture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glUniform1i(glGetUniformLocation(program, "u_texture"), 0);
            glUniform1i(glGetUniformLocation(program, "u_has_texture"), 1);
        } else {
            glUniform1i(glGetUniformLocation(program, "u_has_texture"), 0);
        }
        // rain.c:382-388 - the snow vertices are in the camera's axes, so the
        // per-cell world transform's rotation is the camera basis (rows: right,
        // down, forward). GLSL's mat3 is column-major, so the columns carry the
        // axes.
        const GLfloat basis[9] = {
            camera.right.x, camera.right.y, camera.right.z,
            -camera.up.x, -camera.up.y, -camera.up.z,
            camera.forward.x, camera.forward.y, camera.forward.z,
        };
        glUniformMatrix3fv(local_basis_loc, 1, GL_FALSE, basis);
    } else {
        // rain.c:389-391 - the rain vertices are already in world axes, so the
        // transform's rotation is the identity (the engine's pure scale).
        const GLfloat identity[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
        glUniformMatrix3fv(local_basis_loc, 1, GL_FALSE, identity);
    }

    // rain.c:412-429 - alpha blending SRCALPHA/INVSRCALPHA, z-test on, z-write
    // off (SET_RS(0x1C, 0)).
    GLboolean depth_mask_was;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask_was);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_weather_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, s.vertices.size() * sizeof(float), s.vertices.data());
    glBindVertexArray(vao_weather_);

    // Rain_Draw HOOK 0x498F10 (rain.c:280-303) walks the 9x9 window from the
    // camera's tile and draws the blocks whose four clipped words AND to exactly
    // 0x20. The engine queues them through SortRender_Add keyed on the block's
    // deepest cell (rain.c:293-296); the editor draws them in window order, with
    // the z-test and z-write-off state above - the sort is not transcribed.
    const igi::rain::GridPlacement grid = igi::rain::PlaceGrid(camera.pos.x, camera.pos.y);
    for (int i = 0; i < igi::rain::kDrawWindowTiles; ++i) {
        const double tile_x = igi::rain::DrawTileX(grid, i);
        for (int j = 0; j < igi::rain::kDrawWindowTiles; ++j) {
            const double tile_y = igi::rain::DrawTileY(grid, j);
            const WeatherCell& c00 = s.cells[i * kWindowStride + j];
            const WeatherCell& c01 = s.cells[i * kWindowStride + j + 1];
            const WeatherCell& c10 = s.cells[(i + 1) * kWindowStride + j];
            const WeatherCell& c11 = s.cells[(i + 1) * kWindowStride + j + 1];
            // rain.c:290 `if ((c[0].flags & c[1].flags & c[25].flags & c[24].flags) == 0x20)`.
            if ((c00.flags & c01.flags & c10.flags & c11.flags) != 0x20u) continue;

            const int group = igi::rain::CellVertexGroup(tile_x, tile_y);  // rain.c:404-408
            if (s.list_count[group] == 0) continue;

            // rain.c:402 - the block's highest ground point, relative to the
            // layer lattice, and rain.c:464-484 - the three layers above it.
            const double a = c10.z >= c00.z ? c10.z : c00.z;
            const double b = c11.z >= c01.z ? c11.z : c01.z;
            double h = igi::rain::LayerClipBase(a > b ? a : b, z);
            for (int layer = 0; layer < igi::rain::kLayerCount; ++layer) {
                // rain.c:466-467 - the loop runs k = 3, 2, 1 (lowest first) and
                // adds one kLayer to h each time.
                h += static_cast<double>(igi::rain::kLayerUnits);
                const int n = igi::rain::LayerClipVertexCount(
                    h, s.list_z[group], s.list_count[group]);
                if (n == 0) continue;

                // Rain_DrawCell's per-layer world translation (rain.c:462-468):
                // (cell x, cell y, it->z + kTop - k*kLayer), which is the camera
                // z plus LayerBaseZUnits' offset.
                glUniform3f(tile_origin_loc, static_cast<float>(tile_x),
                            static_cast<float>(tile_y),
                            camera.pos.z + igi::rain::LayerBaseZUnits(camera.pos.z, layer));
                glDrawArrays(primitive, group * group_stride, n * mult);
            }
        }
    }
    glBindVertexArray(0);

    glDepthMask(depth_mask_was);
    glDisable(GL_BLEND);
    if (snow) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glUseProgram(0);
    GL_UnbindForImmediateMode();
}

void Renderer_Rain::Draw(GLuint ubo_mats, const Camera& camera, bool cameraIsSheltered,
                         GLuint snowTexture) {
    WeatherState& s = Weather();

    // The editor's preview multiplier, carried on the state the update reads
    // (there is no engine field for it: rain.c:122-123,572).
    s.speed_scale = speed_scale_;

    // RainEffect_Run HOOK 0x436990 (fxobj/rain.c:98-105) re-evaluates the "Is
    // Active" expression every tick, calls Rain_SetFalling with it and then
    // Rain_Update once - whether or not the effect is falling.
    s.falling = active_;
    UpdateWeather(s);

    const bool selected_renderer_ready = (s.snow != 0) ? snow_program_ != 0 : rain_program_ != 0;
    if (!igi::ShouldDrawWeatherForFrame(active_ && weather_enabled_, selected_renderer_ready,
                                        cameraIsSheltered)) return;
    // Rain_Render rain.c:209-212 - nothing to draw at fade 0.
    if (s.fade == 0.0f) return;

    UpdateGrid(camera);                                        // rain.c:214,220-278
    FillWeatherVertices(s, camera);                            // rain.c:286,307-393
    const double z = igi::rain::SnapLayerZ(camera.pos.z);      // rain.c:283
    ClipCells(z, camera);                                      // rain.c:287,493-554
    DrawCells(ubo_mats, camera, z, snowTexture);               // rain.c:288-302,397-486
}
