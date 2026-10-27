#include "pch.h"
#include "renderer_rain.h"
#include "gl_helper.h"
#include "weather_math.h"
#include "../runtime/weather_visibility.h"
#include "../runtime/weather_particle_style.h"
#include "../runtime/rain_rules.h"
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

// Snow is not a separate system: Rain_New stores `r->snow = rain == 0`
// (IGI-RE src/dxinit/rain.c:153, HOOK 0x498960) and every snow difference is a
// branch on that flag. This shader draws the flakes the engine's snow branch
// builds: a camera-facing quad of six vertices (rain.c:337-364), opaque white
// (rain.c:353), placed by the tile/layer the cell walk selected (rain.c:461-468),
// textured with the game's "snow.tex" (rain.c:431-434).
static const char* SNOW_VERT_SRC = R"(
#version 330 core
layout(location = 0) in vec3 a_seed;   // flake position inside the drawn tile + layer
layout(location = 1) in vec2 a_corner; // quad corner sign, engine vertex order v0..v5

layout(std140) uniform Matrices { mat4 u_unused1; mat4 u_unused2; mat4 u_mvp; };
uniform vec3  u_tile_origin;   // world x/y of the drawn cell + layer base z
uniform vec3  u_camera_right;  // camera basis: the editor's gTransContext.matrix
uniform vec3  u_camera_up;
uniform float u_half_extent;   // fade * kFlakeSize (rain.c:309,341-342,685)

out vec2 v_uv;

void main() {
    // rain.c:338-342 expands vx +/- size and vy +/- size in the camera's own
    // axes, so the flake always faces the camera.
    vec3 worldPos = u_tile_origin + a_seed +
                    (u_camera_right * a_corner.x + u_camera_up * a_corner.y) * u_half_extent;
    gl_Position = u_mvp * vec4(worldPos, 1.0);
    // rain.c:344-361: the four corners are (x0,y0) (x1,y0) (x0,y1) (x1,y1) and
    // carry uv (0,0) (1,0) (0,1) (1,1), so u = (corner.x + 1) / 2 and
    // v = (corner.y + 1) / 2. v4 = v1 and v5 = v2 (rain.c:362-363) reuse them.
    v_uv = (a_corner + 1.0) * 0.5;
}
)";

static const char* SNOW_FRAG_SRC = R"(
#version 330 core
uniform vec4 u_diffuse;      // SnowFlakeDiffuse() == 0xFFFFFFFF (rain.c:353)
uniform sampler2D u_texture; // the game's snow.tex (fxobj/rain.c:82)
uniform int u_has_texture;
in vec2 v_uv;
out vec4 fragColor;
void main() {
    // Rain_DrawCell's stage 0 (rain.c:430-438). With r->texture set the snow
    // branch binds it and sets COLOROP/ALPHAOP to MODULATE with
    // COLORARG1/ALPHAARG1 = D3DTA_TEXTURE and COLORARG2/ALPHAARG2 = D3DTA_DIFFUSE
    // (TSS 1 = 4, 4 = 4, 2/5 = 2, 3/6 = 0). With r->texture NULL it selects
    // SELECTARG2 (TSS 1 = 3, 4 = 3), i.e. the vertex diffuse alone - which is
    // what texel = 1.0 reproduces here. Either way the result is the same
    // multiply, and the alpha comes from the texel, not from a boosted constant.
    vec4 texel = u_has_texture != 0 ? texture(u_texture, v_uv) : vec4(1.0);
    fragColor = u_diffuse * texel;
}
)";

// ─── GLSL 1.20 variants (legacy 2.1 context) ──────────────────────────────────
// Selected at runtime when g_gl_info.legacy_gl21_ is set.  These are language
// ports only: identical uniforms, identical arithmetic, identical varyings.
// The one structural difference is the shared `Matrices` uniform block, which
// GLSL 1.20 does not have (and which this context cannot have -- no
// GL_ARB_uniform_buffer_object).  Its third matrix becomes the plain uniform
// `u_mvp`, matching what gl_helper.cpp's UBO emulation uploads; u_unused1 and
// u_unused2 are never read.  Attribute indices come from
// GL_LegacyBindInlineAttribLocations() before glLinkProgram.
static const char* RAIN_VERT_SRC_120 = R"(
#version 120
attribute vec3 a_seed;   // per-drop random seed, x/y/z in [0,1)
attribute vec2 a_corner; // x = width side, y = bottom/top

uniform mat4 u_mvp;

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

static const char* RAIN_FRAG_SRC_120 = R"(
#version 120
uniform float u_alpha;

void main() {
    gl_FragColor = vec4(0.8, 0.85, 0.9, clamp(u_alpha * 1.25, 0.0, 0.28));
}
)";

static const char* SNOW_VERT_SRC_120 = R"(
#version 120
// Attribute names are load-bearing on this tier: GL_LegacyBindInlineAttribLocations
// binds a_seed -> 0 and a_corner -> 1 by name (gl_helper.cpp:193-194).
attribute vec3 a_seed;
attribute vec2 a_corner;
uniform mat4 u_mvp;
uniform vec3  u_tile_origin;
uniform vec3  u_camera_right;
uniform vec3  u_camera_up;
uniform float u_half_extent;
varying vec2 v_uv;
void main() {
    vec3 worldPos = u_tile_origin + a_seed +
                    (u_camera_right * a_corner.x + u_camera_up * a_corner.y) * u_half_extent;
    gl_Position = u_mvp * vec4(worldPos, 1.0);
    // rain.c:344-361 - the same corner -> uv mapping as the 330 tier.
    v_uv = (a_corner + 1.0) * 0.5;
}
)";

static const char* SNOW_FRAG_SRC_120 = R"(
#version 120
uniform vec4 u_diffuse;
uniform sampler2D u_texture;
uniform int u_has_texture;
varying vec2 v_uv;
void main() {
    // rain.c:430-438, the same stage-0 MODULATE / SELECTARG2 rule as the 330 tier.
    vec4 texel = u_has_texture != 0 ? texture2D(u_texture, v_uv) : vec4(1.0);
    gl_FragColor = u_diffuse * texel;
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

// ─── SNOW ─────────────────────────────────────────────────────────────────────
// IGI-RE has no snow system. Rain_New stores `r->snow = rain == 0`
// (src/dxinit/rain.c:153, HOOK 0x498960) and every snow difference is a branch
// on that one flag; the authored selector is the inverted `Is Rain` bool8
// (src/fxobj/rain.c:77,141). Everything below is the transcription of those
// branches and takes every constant from source/runtime/rain_rules.h, which
// cites IGI-RE for each one - no arithmetic is invented here.
//
// The engine advances the pool once per RainEffect_Run (src/fxobj/rain.c:104,
// HOOK 0x436990), i.e. once per tick; Renderer_Rain::Draw is the editor's tick.
// Rain_UpdateGrid's per-cell ground probe (rain.c:220-278) and Rain_ClipCells'
// clip/depth sort (rain.c:287,495+) are NOT transcribed - the editor has no
// level raycast or sort pass in this file. The camera lattice the drops are
// anchored to, the layer band and the cell parity test all come from the module
// (PlaceGrid / DrawTileX / DrawTileY / LayerBaseZUnits / CellVertexGroup).
namespace {

// One RainDrop (src/dxinit/rain.c:40-70). x/y are the tile-lattice coordinates
// Rain_Update wraps inside [0, kTileF) (rain.c:603-616) and z is the position
// inside the 6 m band that wraps at kLayer (rain.c:587-591).
struct SnowDrop {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float dx = 0.0f;
    float dy = 0.0f;
    int timer = 0;
    int kind = 0;
};

struct SnowState {
    SnowDrop drops[igi::rain::kDropCapacity];  // rain.c:65 `RainDrop drops[80]`
    int count = 0;                             // Rain::dropCount
    float spawn = 0.0f;                        // Rain::spawn (rain.c:619-636)
    float fade = 0.0f;                         // Rain::fade, 0 at Rain_New (rain.c:155)
    // Rain_Init seeds gDxRainRandom once (rain.c:184). The editor uses its own
    // generator, as the drop buffers above do; the draw ORDER and the ranges are
    // the engine's (rain.c:596-598,627-632), the stream is not.
    std::mt19937 rng{204800};
};

SnowState& Snow() {
    static SnowState state;
    return state;
}

float SnowRandomUnit(SnowState& s) {
    return std::uniform_real_distribution<float>(0.0f, 1.0f)(s.rng);
}

int SnowRandomInt(SnowState& s, int n) {
    return std::uniform_int_distribution<int>(0, n - 1)(s.rng);
}

// RainEffect_Changed frees and rebuilds the Rain object whenever anything but the
// alpha changes (src/fxobj/rain.c:133-142), so a switch to snow starts from an
// empty pool at fade 0 exactly as Rain_New leaves it (rain.c:149-155).
void SnowReset() {
    SnowState& s = Snow();
    s.count = 0;
    s.spawn = 0.0f;
    s.fade = 0.0f;
}

// Rain_Update, HOOK 0x499CF0 (src/dxinit/rain.c:565-637): the snow branches.
void SnowTick(bool falling) {
    SnowState& s = Snow();
    const float speed = igi::rain::SpeedUnitsPerTick(true);  // 204.8 u/tick (rain.c:572)

    // rain.c:573-585 - fade ramps one kFadeStep per tick and the update stops
    // dead at zero (Rain_Render skips the draw there too, rain.c:209-212).
    float f = falling ? s.fade + igi::rain::kFadeStep : s.fade - igi::rain::kFadeStep;
    if (falling) {
        if (!(f < 1.0f)) f = 1.0f;  // `if (!(f < kOne)) f = kOne`
    } else if (!(f > 0.0f)) {
        f = 0.0f;                   // `if (!(f > kZero)) f = kZero`
    }
    s.fade = f;
    if (s.fade == 0.0f) return;     // x87_eq(r->fade, kZero), rain.c:584-585

    for (int i = 0; i < s.count; ++i) {
        SnowDrop& d = s.drops[i];

        // rain.c:587-591 - `z = d->z - speed; if (z < 0) d->z = z + kLayer;`
        d.z = igi::rain::WrapZ(d.z, speed);

        // rain.c:595-602 - `if (d->timer-- < 0) { ... }`: the decrement happens
        // before the test, so the reload lands on an already-decremented timer
        // (SnowSwayTimerAfterReload).
        const int before_decrement = d.timer;
        d.timer = before_decrement - 1;
        if (before_decrement < 0) {
            d.timer += igi::rain::SnowSwayReloadTicks(
                SnowRandomInt(s, igi::rain::kSwayTicksMin));  // Random_Int(r, 30)
            d.dx = igi::rain::SnowSwayDisplacementUnits(SnowRandomUnit(s));  // rain.c:597
            d.dy = igi::rain::SnowSwayDisplacementUnits(SnowRandomUnit(s));  // rain.c:598
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
    const int spawned = igi::rain::SpawnCountForTick(speed, s.spawn, s.count);
    for (int i = 0; i < spawned && s.count < igi::rain::kDropCapacity; ++i) {
        SnowDrop& d = s.drops[s.count];

        const float x = SnowRandomUnit(s) * igi::rain::kTileUnitsF;  // rain.c:627
        d.y = SnowRandomUnit(s) * igi::rain::kTileUnitsF;            // rain.c:628
        d.x = x;
        d.timer = 0;                                                 // rain.c:630
        d.z = igi::rain::SpawnZUnits(true);                          // rain.c:631
        d.kind = SnowRandomInt(s, igi::rain::DropKindCount());       // rain.c:632
        // rain.c:626-632 never initialises dx/dy; the engine reads whatever the
        // slot held. The port pins them to 0 instead of reading uninitialised
        // memory. The first roll replaces them on the drop's second tick
        // (SnowSwayFirstRollTick).
        d.dx = 0.0f;
        d.dy = 0.0f;
        ++s.count;
    }
}

// Rain_FillVB, HOOK 0x499090 (src/dxinit/rain.c:337-364,382-388): six vertices
// per flake in the engine's own order - v0..v3 are the corners and v4 = v1,
// v5 = v2 (rain.c:362-363) - written into the group named by `kind`
// (rain.c:327-330,378). Corner signs only: the extent is rain_rules'
// SnowFlakeSizeUnits() times the fade (rain.c:309). The matching texture
// coordinates are (0,0) (1,0) (0,1) (1,1) (rain.c:354-361), which the vertex
// shader derives as (sign + 1) / 2, so the table below is also the UV order.
constexpr float kSnowCorners[6][2] = {  // 6 == SnowVerticesPerParticle() (rain.c:364,447)
    {-1.0f, -1.0f},  // v0 = (x0, y0)  rain.c:344-345
    {+1.0f, -1.0f},  // v1 = (x1, y0)  rain.c:346-347
    {-1.0f, +1.0f},  // v2 = (x0, y1)  rain.c:348-349
    {+1.0f, +1.0f},  // v3 = (x1, y1)  rain.c:350-351
    {+1.0f, -1.0f},  // v4 = v1        rain.c:362
    {-1.0f, +1.0f},  // v5 = v2        rain.c:363
};

}  // namespace

bool Renderer_Rain::Init() {
    const char* rain_vs = g_gl_info.legacy_gl21_ ? RAIN_VERT_SRC_120 : RAIN_VERT_SRC;
    const char* rain_fs = g_gl_info.legacy_gl21_ ? RAIN_FRAG_SRC_120 : RAIN_FRAG_SRC;
    const char* snow_vs = g_gl_info.legacy_gl21_ ? SNOW_VERT_SRC_120 : SNOW_VERT_SRC;
    const char* snow_fs = g_gl_info.legacy_gl21_ ? SNOW_FRAG_SRC_120 : SNOW_FRAG_SRC;
    shader_program_ = LinkWeatherProgram(rain_vs, rain_fs, ubo_binding_point_);
    snow_program_ = LinkWeatherProgram(snow_vs, snow_fs, ubo_binding_point_);
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

    // Snow flake buffer: the engine's pool is a fixed 80 drops (rain.c:65,624)
    // drawn as six vertices each (rain.c:364,447) into four vertex groups of 480
    // vertices - one per drop kind (rain.c:327-330,404-409). Rain_FillVB rewrites
    // the whole buffer every tick (rain.c:307-393), so it is GL_DYNAMIC_DRAW and
    // the contents are filled per frame by DrawSnow.
    num_snow_flakes_ = igi::rain::kDropCapacity;
    const int snow_group_stride = num_snow_flakes_ * igi::rain::SnowVerticesPerParticle();
    const int snow_vertex_count = snow_group_stride * igi::rain::DropKindCount();
    std::vector<float> snow_verts(static_cast<std::size_t>(snow_vertex_count) * 5, 0.0f);

    glGenVertexArrays(1, &vao_snow_);
    glGenBuffers(1, &vbo_snow_);
    glBindVertexArray(vao_snow_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_snow_);
    glBufferData(GL_ARRAY_BUFFER, snow_verts.size() * sizeof(float), snow_verts.data(), GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    // fxobj/rain.c:82 registers "snow.tex" once, for both modes, and Rain_DrawCell's
    // snow branch binds it (rain.c:430-434) while the rain branch binds NULL
    // (rain.c:449). The handle is resolved per frame by the caller, through the
    // engine's own identity and pack (Renderer_Objects::GetOrLoadGameTexture over
    // igi_res::GameTextureMemberName -> "LOCAL:common/textures/snow.tex", mounted
    // at src/game/game.c:430); a failed resolution passes 0, which is the engine's
    // null-texture branch (rain.c:435-438).
    Logger::Get().Log(LogLevel::INFO, std::string("[Renderer_Rain] Snow texture (fxobj/rain.c:82): ") +
                                          igi::rain::SnowTextureName() +
                                          " as LOCAL:common/textures/" + igi::rain::SnowTextureName());

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
    const bool snow_before = is_snow_;
    active_ = active;
    is_snow_ = is_snow;
    start_meters_ = startMeters;
    end_meters_ = endMeters;
    alpha_ = alpha;
    // A switch into snow rebuilds the engine's Rain object (fxobj/rain.c:133-142).
    if (is_snow_ && !snow_before) SnowReset();
    if (active_) {
        if (is_snow_) {
            Logger::Get().Log(LogLevel::INFO,
                "[Renderer_Rain] SNOW: IGI-RE Rain_Update snow branch - rain_rules speed/spawn-z/"
                "sway/wrap, 6-vertex camera-facing billboard (rain.c:565-637,337-364)");
        } else {
            Logger::Get().Log(LogLevel::INFO,
                "[Renderer_Rain] RAIN parity: drops=1200 length=0.080m width=0.012m speed=0.08..0.18-band/s "
                "alpha=clamp(authored*1.25,0.00,0.28)");
        }
    }
}

void Renderer_Rain::Draw(GLuint ubo_mats, const glm::vec3& cameraPos, const glm::vec3& cameraRight,
                         const glm::vec3& cameraUp, bool cameraIsSheltered, GLuint snowTexture) {
    // RainEffect_Run calls Rain_Update once per tick (fxobj/rain.c:104) whether or
    // not the effect is falling, so the snow pool is advanced here - before the
    // visibility gate - and the fade-out runs to zero like the engine's.
    if (is_snow_) SnowTick(active_);

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
        DrawSnow(ubo_mats, cameraPos, cameraRight, cameraUp, time_sec, snowTexture);
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

void Renderer_Rain::DrawSnow(GLuint ubo_mats, const glm::vec3& cameraPos, const glm::vec3& cameraRight,
                             const glm::vec3& cameraUp, float time_sec, GLuint snow_texture) {
    // The engine's snow has no time term: Rain_Update moves every drop by the same
    // fixed step per tick (rain.c:587-591) and the sway is re-rolled on a tick
    // timer (rain.c:595-598). SnowTick already advanced the pool for this frame.
    (void)time_sec;

    SnowState& s = Snow();
    // Rain_Render returns at fade 0 (rain.c:209-212); fade also scales the quad
    // (rain.c:309), so a zero fade has nothing to draw.
    if (s.fade == 0.0f || s.count == 0) return;

    // Rain_FillVB (rain.c:307-393): rebuild the four vertex groups from the pool.
    const int verts_per_drop = igi::rain::SnowVerticesPerParticle();          // 6 (rain.c:364,447)
    const int group_stride = igi::rain::kDropCapacity * verts_per_drop;       // 480 (rain.c:328)
    std::vector<float> verts(static_cast<std::size_t>(group_stride) *
                                 static_cast<std::size_t>(igi::rain::DropKindCount()) * 5,
                             0.0f);
    std::vector<int> group_count(igi::rain::DropKindCount(), 0);
    for (int i = 0; i < s.count; ++i) {
        const SnowDrop& d = s.drops[i];
        const int group = d.kind;  // 0..3; the wrap XORs keep it in range (rain.c:605,612)
        float* v = &verts[(static_cast<std::size_t>(group) * group_stride +
                           static_cast<std::size_t>(group_count[group])) *
                          5];
        for (int c = 0; c < verts_per_drop; ++c) {
            v[c * 5 + 0] = d.x;
            v[c * 5 + 1] = d.y;
            v[c * 5 + 2] = d.z;
            v[c * 5 + 3] = kSnowCorners[c][0];
            v[c * 5 + 4] = kSnowCorners[c][1];
        }
        ++group_count[group];
    }

    glBindBuffer(GL_ARRAY_BUFFER, vbo_snow_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, verts.size() * sizeof(float), verts.data());

    glUseProgram(snow_program_);
    glBindBufferBase(GL_UNIFORM_BUFFER, ubo_binding_point_, ubo_mats);

    // rain.c:430-438. The snow branch binds the game's snow.tex at stage 0 and
    // sets COLOROP/ALPHAOP to MODULATE of TEXTURE x DIFFUSE; when r->texture is
    // NULL it sets SELECTARG2, the diffuse alone. `snow_texture` is that texture
    // (Renderer_Objects::GetOrLoadGameTexture over the identity
    // GameTextureMemberName(SnowTextureName()), i.e. "LOCAL:common/textures/snow.tex"
    // from the pack the game mounts at src/game/game.c:430), so passing 0 here is
    // the engine's own null-texture branch, not a special case invented for it.
    if (snow_texture != 0) {
        // Rain_DrawCell's stage-0 sampler states: SET_TSS(0, 0x10, 2) MAGFILTER =
        // D3DTEXF_LINEAR and SET_TSS(0, 0x11, 1) MINFILTER = D3DTEXF_POINT
        // (rain.c:444-445). MIPFILTER (0x12) is not set in that block, so the D3D7
        // default D3DTEXF_NONE applies and no mip level is selected; ADDRESSU/V are
        // unset too and default to D3DTADDRESS_WRAP. GL_RegisterTexture's shared
        // defaults are mipmapped trilinear, so the state is corrected here. It is
        // re-applied every frame rather than latched, because the handle is a GL
        // name that the renderer's cache may reuse after a level switch.
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, snow_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glUniform1i(glGetUniformLocation(snow_program_, "u_texture"), 0);
        glUniform1i(glGetUniformLocation(snow_program_, "u_has_texture"), 1);
    } else {
        glUniform1i(glGetUniformLocation(snow_program_, "u_has_texture"), 0);
    }

    // Rain_FillVB expands the quad in the camera's own axes (gTransContext.matrix,
    // rain.c:338-342,382-388). The frame's basis arrives from the caller
    // (renderer_draw.cpp passes view_define_->right_/up_, the same vectors the
    // scene's view matrix is built from).
    glUniform3f(glGetUniformLocation(snow_program_, "u_camera_right"), cameraRight.x, cameraRight.y,
                cameraRight.z);
    glUniform3f(glGetUniformLocation(snow_program_, "u_camera_up"), cameraUp.x, cameraUp.y, cameraUp.z);
    // rain.c:309 `size = r->fade * kFlakeSize` is the quad's half extent, so the
    // flake grows out of the fade instead of fading its colour.
    glUniform1f(glGetUniformLocation(snow_program_, "u_half_extent"),
                s.fade * igi::rain::SnowFlakeSizeUnits());
    // rain.c:353 - all four corners carry 0xFFFFFFFF (opaque white). The domain is
    // the raw 8-bit D3DCOLOR of a D3DFVF_DIFFUSE vertex (rain.c:139), NOT the
    // 1/128 QVertex domain the menus use (gColorScale 0x534AB0 = 0.0078125f,
    // src/res/qvertex.c:189, consumed by the QVertex/Grid path via
    // math/draw.c:12 COLOR_SCALE). The weather particles never touch QVertex:
    // Rain_FillVB writes unsigned diffuse words straight into the VB, and
    // Rain_SetAlpha packs trunc(alpha * 255) into the top byte (rain.c:646-649).
    const std::uint32_t diffuse = igi::rain::SnowFlakeDiffuse();
    glUniform4f(glGetUniformLocation(snow_program_, "u_diffuse"),
                static_cast<float>((diffuse >> 16) & 0xFFu) / 255.0f,
                static_cast<float>((diffuse >> 8) & 0xFFu) / 255.0f,
                static_cast<float>(diffuse & 0xFFu) / 255.0f,
                static_cast<float>((diffuse >> 24) & 0xFFu) / 255.0f);

    GLboolean depth_mask_was;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask_was);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);

    // Rain_Draw walks the 9x9 window from the camera's tile (rain.c:282,288-289)
    // and Rain_DrawCell draws each cell's parity group - CellVertexGroup, rain.c:404-409 -
    // in three kLayer-tall layers (rain.c:465-484). A drop is therefore drawn only
    // by cells whose tile parity equals its kind, which is what makes the wrap's
    // kind XOR keep it in place.
    const igi::rain::GridPlacement grid = igi::rain::PlaceGrid(cameraPos.x, cameraPos.y);
    const GLint tile_origin_loc = glGetUniformLocation(snow_program_, "u_tile_origin");
    glBindVertexArray(vao_snow_);
    for (int i = 0; i < igi::rain::kDrawWindowTiles; ++i) {
        const double tile_x = igi::rain::DrawTileX(grid, i);
        for (int j = 0; j < igi::rain::kDrawWindowTiles; ++j) {
            const double tile_y = igi::rain::DrawTileY(grid, j);
            const int group = igi::rain::CellVertexGroup(tile_x, tile_y);
            if (group_count[group] == 0) continue;
            for (int layer = 0; layer < igi::rain::kLayerCount; ++layer) {
                // LayerBaseZUnits is the camera-relative base of each layer
                // (rain.c:283,464-468); the drop's own z sits inside the 6 m band.
                glUniform3f(tile_origin_loc, static_cast<float>(tile_x), static_cast<float>(tile_y),
                            cameraPos.z + igi::rain::LayerBaseZUnits(cameraPos.z, layer));
                // rain.c:446 D3DPT_TRIANGLELIST -> GL_TRIANGLES, `mult = 6`.
                glDrawArrays(GL_TRIANGLES, group * group_stride, group_count[group] * verts_per_drop);
            }
        }
    }
    glBindVertexArray(0);

    glDepthMask(depth_mask_was);
    glDisable(GL_BLEND);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    GL_UnbindForImmediateMode();
}
