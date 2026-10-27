#include "renderer_objects_internal.h"
#include "igi_transform.h"
#include "object_lightmap.h"
#include "lightmap_ref_index.h"
#include "../lod/lod_select.h"
#include "../lod/mef_mesh_header.h"
#include "../capture_camera.h"





bool Renderer_Objects::IsSkippedModelId(const std::string& modelId) {
    if (modelId.empty()) return false;
    return modelId == "colbox" || modelId == "colbox2" || modelId == "colbox4" || modelId == "colbox66";
}

// Per-frame delta seconds feeder for continuous animations (e.g. helicopter rotor spin).
// Written by App::Frame before each Draw; consumed inside DrawAttachmentsRecursive via the
// elapsed_time_secs_ accumulator. Defined here so the extern reference in the draw path links.
float g_renderer_delta_secs = 0.0f;

void Renderer_Objects::SetLightmapForTask(const std::string& taskId, std::vector<GLuint> textures,
                                          const glm::dvec3& bakedPos, const glm::dvec3& bakedRot) {
    ClearLightmapForTask(taskId);
    lightmap_textures_by_task_[taskId] = std::move(textures);
    lightmap_bake_pose_by_task_[taskId] = { bakedPos, bakedRot, sun_dir_ };
}

void Renderer_Objects::ClearLightmapForTask(const std::string& taskId) {
    auto it = lightmap_textures_by_task_.find(taskId);
    if (it == lightmap_textures_by_task_.end()) return;
    for (GLuint tex : it->second) {
        if (tex != 0) glDeleteTextures(1, &tex);
    }
    lightmap_textures_by_task_.erase(it);
    lightmap_bake_pose_by_task_.erase(taskId);
}

const std::vector<GLuint>* Renderer_Objects::GetLightmapForTask(const std::string& taskId) const {
    auto it = lightmap_textures_by_task_.find(taskId);
    return it != lightmap_textures_by_task_.end() ? &it->second : nullptr;
}

bool Renderer_Objects::IsLightmapStale(const std::string& taskId, const glm::dvec3& curPos, const glm::dvec3& curRot) const {
    auto it = lightmap_bake_pose_by_task_.find(taskId);
    if (it == lightmap_bake_pose_by_task_.end()) return false;
    constexpr double kPosEpsilon = 1.0;    // world units
    constexpr double kRotEpsilon = 0.01;   // radians
    constexpr float  kSunEpsilon = 0.05f;  // angle change (dot-product threshold ~3 degrees)
    const auto& bp = it->second;
    if (glm::length(curPos - bp.pos) > kPosEpsilon) return true;
    if (glm::length(curRot - bp.rot) > kRotEpsilon) return true;
    // Sun direction change: if sun moved more than ~3° since bake, lightmap is stale.
    if (glm::length(sun_dir_) > 1e-6f && glm::length(bp.sun_dir) > 1e-6f) {
        float dot = glm::dot(glm::normalize(sun_dir_), glm::normalize(bp.sun_dir));
        if (dot < 1.0f - kSunEpsilon) return true;
    }
    return false;
}

// ─── Shader Sources ───────────────────────────────────────────────────────────
static const char* OBJ_VERT_SRC = R"(
#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec2 a_uv2;

layout(std140) uniform Matrices {
    mat4 u_unused1;
    mat4 u_unused2;
    mat4 u_mvp; // Proj * View * GlobalScale
};


uniform mat4 u_model;

out vec3 v_normal;
out vec2 v_uv;
out vec2 v_uv2;
out vec3 v_fragPos;

void main() {
    vec4 worldPos   = u_model * vec4(a_pos, 1.0);
    v_fragPos       = worldPos.xyz;
    v_normal        = mat3(u_model) * a_normal;
    v_uv            = a_uv;
    v_uv2           = a_uv2;
    gl_Position     = u_mvp * worldPos;
}

)";

static const char* OBJ_FRAG_SRC = R"(
#version 330 core
in vec3 v_normal;
in vec2 v_uv;
in vec2 v_uv2;
in vec3 v_fragPos;

uniform vec3  u_dirlight;      // directional light RGB (sun)
uniform vec3  u_ambient;       // ambient light RGB
uniform vec3  u_lightDir;      // direction toward the sun (world space)
uniform sampler2D u_texture;
uniform int   u_useTexture;
uniform sampler2D u_lightmap;
uniform int   u_useLightmap;   // 0 = no baked lightmap for this submesh
uniform vec3  u_lightmapScale; // live re-light scale when object was moved since bake
uniform int   u_lm_authentic;  // Baked mode: exact diffuse x lightmap multiply, like the original engine
uniform float u_alpha;
uniform vec4  u_baseColor;
uniform vec3  u_tint;
uniform float u_glassMin;
uniform float u_gamma;
out vec4 fragColor;

void main() {
    vec3 N = normalize(v_normal);
    vec3 lightDir = normalize(u_lightDir);

    // Hemisphere ambient: sky (up) is warmer/brighter, ground (down) is dimmer.
    // This ensures every face — including side walls — always gets meaningful light.
    float hemiT = dot(N, vec3(0.0, 0.0, 1.0)) * 0.5 + 0.5; // 0=down, 1=up
    vec3 hemi = mix(u_ambient * 0.75, u_ambient * 1.35, hemiT);

    // Sun: front faces bright, back faces get 40% warm fill (no black backs).
    float diff     = max(dot(N, lightDir), 0.0);
    float backFill = max(-dot(N, lightDir), 0.0) * 0.40;

    // Subtle specular
    vec3 viewDir = normalize(vec3(0.0, 1.0, 1.0));
    vec3 halfVec = normalize(lightDir + viewDir);
    float spec = pow(max(dot(N, halfVec), 0.0), 32.0) * 0.08;

    vec3 light = hemi + u_dirlight * (diff + backFill + spec);

    vec4 texColor = (u_useTexture != 0) ? texture(u_texture, v_uv) : u_baseColor;
    // Alpha of blended submeshes (glass, alpha-routed floors) must not decay
    // with mip level: glGenerateMipmap averages the alpha channel, so distant
    // fragments faded and then hit the cutout discard — whole building floors
    // washed out / vanished at range. Sample mip 0 for the blended case;
    // true opaque cutouts (u_alpha ~ 1.0) keep mip-filtered cutout behavior.
    float texA = (u_alpha >= 0.99) ? texColor.a
                                   : textureLod(u_texture, v_uv, 0.0).a;
    float finalAlpha = (u_useTexture != 0 ? texA : 1.0) * u_alpha;

    if (u_glassMin > 0.0) {
        finalAlpha = max(finalAlpha, u_glassMin);
        light += vec3(spec * 1.5);
    }

    vec3 litColor;
    if (u_useLightmap != 0) {
        vec2 olmUV = vec2(v_uv2.x, 1.0 - v_uv2.y);
        vec3 lm = texture(u_lightmap, olmUV).rgb * u_lightmapScale;
        if (u_lm_authentic == 0) {
            // Warm ambient floor: no lightmapped face should be black.
            // OLM stores absolute lighting; dark faces get lifted to warm minimum.
            lm = max(lm, u_ambient * 0.90);
        }
        litColor = texColor.rgb * lm * u_tint;
    } else {
        litColor = light * texColor.rgb * u_tint;
    }

    if (u_lm_authentic == 0) {
        litColor = pow(max(litColor, 0.0), vec3(u_gamma));
    }
    fragColor = vec4(litColor, finalAlpha);

    if (u_glassMin <= 0.0 && u_alpha >= 0.99 && texA < 0.75) discard;
    if (fragColor.a < 0.01) discard;
}
)";

// ─── Picking Shader Sources ───────────────────────────────────────────────────
static const char* PICK_VERT_SRC = R"(
#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;

layout(std140) uniform Matrices {
    mat4 u_unused1;
    mat4 u_unused2;
    mat4 u_mvp;
};

uniform mat4 u_model;

void main() {
    gl_Position = u_mvp * u_model * vec4(a_pos, 1.0);
}
)";

static const char* PICK_FRAG_SRC = R"(
#version 330 core
uniform int u_object_id;
out vec4 fragColor;
void main() {
    int id = u_object_id;
    fragColor = vec4(
        float((id >> 16) & 0xFF) / 255.0,
        float((id >>  8) & 0xFF) / 255.0,
        float( id        & 0xFF) / 255.0,
        1.0
    );
}
)";

// ─── GLSL 1.20 variants (legacy 2.1 context) ──────────────────────────────────
// Selected at runtime when g_gl_info.legacy_gl21_ is set.  These are language
// ports only: identical uniforms, identical arithmetic, identical varyings.
// The shared `Matrices` uniform block does not exist in GLSL 1.20 (and this
// context has no GL_ARB_uniform_buffer_object), so its third matrix becomes the
// plain uniform `u_mvp`, matching what gl_helper.cpp's UBO emulation uploads;
// u_unused1 and u_unused2 are never read.  Attribute indices come from
// GL_LegacyBindInlineAttribLocations() before glLinkProgram.
static const char* OBJ_VERT_SRC_120 = R"(
#version 120
attribute vec3 a_pos;
attribute vec3 a_normal;
attribute vec2 a_uv;
attribute vec2 a_uv2;

uniform mat4 u_mvp; // Proj * View * GlobalScale


uniform mat4 u_model;

varying vec3 v_normal;
varying vec2 v_uv;
varying vec2 v_uv2;
varying vec3 v_fragPos;

void main() {
    vec4 worldPos   = u_model * vec4(a_pos, 1.0);
    v_fragPos       = worldPos.xyz;
    v_normal        = mat3(u_model) * a_normal;
    v_uv            = a_uv;
    v_uv2           = a_uv2;
    gl_Position     = u_mvp * worldPos;
}

)";

static const char* OBJ_FRAG_SRC_120 = R"(
#version 120
// GLSL 1.20 has no textureLod(); explicit-LOD sampling needs this extension,
// which is core only from GLSL 1.30 on.  Everything else here is 1.20 core.
#extension GL_ARB_shader_texture_lod : enable
varying vec3 v_normal;
varying vec2 v_uv;
varying vec2 v_uv2;
varying vec3 v_fragPos;

uniform vec3  u_dirlight;      // directional light RGB (sun)
uniform vec3  u_ambient;       // ambient light RGB
uniform vec3  u_lightDir;      // direction toward the sun (world space)
uniform sampler2D u_texture;
uniform int   u_useTexture;
uniform sampler2D u_lightmap;
uniform int   u_useLightmap;   // 0 = no baked lightmap for this submesh
uniform vec3  u_lightmapScale; // live re-light scale when object was moved since bake
uniform int   u_lm_authentic;  // Baked mode: exact diffuse x lightmap multiply, like the original engine
uniform float u_alpha;
uniform vec4  u_baseColor;
uniform vec3  u_tint;
uniform float u_glassMin;
uniform float u_gamma;

void main() {
    vec3 N = normalize(v_normal);
    vec3 lightDir = normalize(u_lightDir);

    // Hemisphere ambient: sky (up) is warmer/brighter, ground (down) is dimmer.
    // This ensures every face — including side walls — always gets meaningful light.
    float hemiT = dot(N, vec3(0.0, 0.0, 1.0)) * 0.5 + 0.5; // 0=down, 1=up
    vec3 hemi = mix(u_ambient * 0.75, u_ambient * 1.35, hemiT);

    // Sun: front faces bright, back faces get 40% warm fill (no black backs).
    float diff     = max(dot(N, lightDir), 0.0);
    float backFill = max(-dot(N, lightDir), 0.0) * 0.40;

    // Subtle specular
    vec3 viewDir = normalize(vec3(0.0, 1.0, 1.0));
    vec3 halfVec = normalize(lightDir + viewDir);
    float spec = pow(max(dot(N, halfVec), 0.0), 32.0) * 0.08;

    vec3 light = hemi + u_dirlight * (diff + backFill + spec);

    vec4 texColor = (u_useTexture != 0) ? texture2D(u_texture, v_uv) : u_baseColor;
    // Alpha of blended submeshes (glass, alpha-routed floors) must not decay
    // with mip level: glGenerateMipmap averages the alpha channel, so distant
    // fragments faded and then hit the cutout discard — whole building floors
    // washed out / vanished at range. Sample mip 0 for the blended case;
    // true opaque cutouts (u_alpha ~ 1.0) keep mip-filtered cutout behavior.
    float texA = (u_alpha >= 0.99) ? texColor.a
                                   : texture2DLod(u_texture, v_uv, 0.0).a;
    float finalAlpha = (u_useTexture != 0 ? texA : 1.0) * u_alpha;

    if (u_glassMin > 0.0) {
        finalAlpha = max(finalAlpha, u_glassMin);
        light += vec3(spec * 1.5);
    }

    vec3 litColor;
    if (u_useLightmap != 0) {
        vec2 olmUV = vec2(v_uv2.x, 1.0 - v_uv2.y);
        vec3 lm = texture2D(u_lightmap, olmUV).rgb * u_lightmapScale;
        if (u_lm_authentic == 0) {
            // Warm ambient floor: no lightmapped face should be black.
            // OLM stores absolute lighting; dark faces get lifted to warm minimum.
            lm = max(lm, u_ambient * 0.90);
        }
        litColor = texColor.rgb * lm * u_tint;
    } else {
        litColor = light * texColor.rgb * u_tint;
    }

    if (u_lm_authentic == 0) {
        litColor = pow(max(litColor, 0.0), vec3(u_gamma));
    }
    gl_FragColor = vec4(litColor, finalAlpha);

    if (u_glassMin <= 0.0 && u_alpha >= 0.99 && texA < 0.75) discard;
    if (gl_FragColor.a < 0.01) discard;
}
)";

static const char* PICK_VERT_SRC_120 = R"(
#version 120
attribute vec3 a_pos;
attribute vec3 a_normal;
attribute vec2 a_uv;

uniform mat4 u_mvp;

uniform mat4 u_model;

void main() {
    gl_Position = u_mvp * u_model * vec4(a_pos, 1.0);
}
)";

static const char* PICK_FRAG_SRC_120 = R"(
#version 120
uniform int u_object_id;
void main() {
    // GLSL 1.20 has no bitwise operators (they arrive with GLSL 1.30), so the
    // shifts and masks become floor/mod arithmetic.  Pick ids are i+1 or
    // kAttaPickBase + n with kAttaPickBase = 0x800000, i.e. integers below
    // 2^24, which float represents exactly; every intermediate here is an
    // integer-valued float, so the bytes written are the same as the original
    // (verified numerically over the whole id domain).
    float id = float(u_object_id);
    gl_FragColor = vec4(
        mod(floor(id / 65536.0), 256.0) / 255.0,
        mod(floor(id /   256.0), 256.0) / 255.0,
        mod(           id       , 256.0) / 255.0,
        1.0
    );
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
    const char* vs_src = g_gl_info.legacy_gl21_ ? OBJ_VERT_SRC_120 : OBJ_VERT_SRC;
    const char* fs_src = g_gl_info.legacy_gl21_ ? OBJ_FRAG_SRC_120 : OBJ_FRAG_SRC;
    GLuint vert = CompileShader(GL_VERTEX_SHADER,   vs_src);
    GLuint frag = CompileShader(GL_FRAGMENT_SHADER, fs_src);
    if (!vert || !frag) return 0;

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    // GLSL 1.20 has no layout(location = N), so the attribute indices the 330
    // originals declare must be re-bound by name before linking.
    if (g_gl_info.legacy_gl21_) {
        GL_LegacyBindInlineAttribLocations(prog);
    }
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

static GLuint BuildPickShaderProgram() {
    const char* vs_src = g_gl_info.legacy_gl21_ ? PICK_VERT_SRC_120 : PICK_VERT_SRC;
    const char* fs_src = g_gl_info.legacy_gl21_ ? PICK_FRAG_SRC_120 : PICK_FRAG_SRC;
    GLuint vert = CompileShader(GL_VERTEX_SHADER,   vs_src);
    GLuint frag = CompileShader(GL_FRAGMENT_SHADER, fs_src);
    if (!vert || !frag) return 0;

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    if (g_gl_info.legacy_gl21_) {
        GL_LegacyBindInlineAttribLocations(prog);
    }
    glLinkProgram(prog);

    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(prog, 512, nullptr, log);
        std::cerr << "[PickShader] Link error: " << log << "\n";
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


    // Picking shader
    pick_shader_prog_ = BuildPickShaderProgram();
    if (!pick_shader_prog_) {
        Logger::Get().Log(LogLevel::WARNING, "[Renderer_Objects] Failed to build picking shader.");
    } else {
        GLuint blk = glGetUniformBlockIndex(pick_shader_prog_, "Matrices");
        if (blk != GL_INVALID_INDEX)
            glUniformBlockBinding(pick_shader_prog_, blk, ubo_binding_point_);
    }
    // FBO created on first use (size unknown at init time)

    Logger::Get().Log(LogLevel::INFO, "[Renderer_Objects] Init OK.");

    return true;
}

// ─── ClearCaches ──────────────────────────────────────────────────────────────
void Renderer_Objects::ClearCaches() {
    Logger::Get().Log(LogLevel::INFO, "[Renderer_Objects] Clearing all level caches... (meshes=" +
        std::to_string(mesh_cache_.size()) + " textures=" + std::to_string(texture_cache_.size()) +
        " attachments=" + std::to_string(attachment_cache_.size()) +
        " modelTexMap=" + std::to_string(model_texture_map_cache_.size()) +
        " texMapLevel=" + std::to_string(texture_map_level_) +
        " globalTexMapLoaded=" + std::to_string(global_texture_map_loaded_ ? 1 : 0) + ")");
    for (auto& pair : mesh_cache_) {
        destroyModel(pair.second);
    }
    mesh_cache_.clear();
    attachment_cache_.clear();
    occupancy_sig_ = 0;

    for (auto& pair : texture_cache_) {
        if (pair.second) {
            glDeleteTextures(1, &pair.second);
        }
    }
    texture_cache_.clear();
    model_texture_map_cache_.clear();
    
    // We explicitly DO NOT clear global_texture_map_, model_level_map_,
    // texture_level_map_, window_model_ids_, portal_distances_, deathzone_ids_,
    // or magicobj_ids_ here. These are populated from global sources (lod.qvm, names.qvm, 
    // or scanning all 14 levels' DAT files) and their definitions are static across 
    // level switches. Clearing them causes lazy re-loads that might fail if cache 
    // stamps trick the asset extractor into skipping partial directories.
    texture_map_level_ = -1;
    persistent_dat_ = DATFile{};
    persistent_dat_path_.clear();
    logged_draw_buildings_.clear();
    skin_geometry_cache_.clear();
    parsed_geometry_cache_.clear();

    // Lightmaps are keyed by taskId, which is only unique WITHIN a level — task
    // 1104 in level1 and task 1104 in level6 are unrelated. Without clearing
    // these on a level switch, a previous level's baked lightmap (and its bake
    // pose) would bind to the new level's same-numbered task.
    // indoor_ambient_by_task_ is also per-level (rebuilt from LightmapInfo tasks
    // on each level load in app_level.cpp), so clear it here too.
    ClearAllLightmaps();
    indoor_ambient_by_task_.clear();
    Logger::Get().Log(LogLevel::INFO, "[Renderer_Objects] Cleared per-task lightmap caches on level switch");
    ClearResCache();
}

void Renderer_Objects::ClearResCache() {
    res_tex_indexes_.clear();
    res_model_indexes_.clear();
    // LOD chains are built over the loaded .res model set (issue #62), so they
    // are stale the moment that set changes.
    lod_chain_cache_.clear();
    for (const auto& p : tmp_mef_paths_) {
        std::error_code ec;
        std::filesystem::remove(p, ec);
    }
    tmp_mef_paths_.clear();
    Logger::Get().Log(LogLevel::INFO, "[Renderer_Objects] ResCache cleared");
}

void Renderer_Objects::RefreshTextureResCache() {
    res_tex_indexes_.clear();
    res_model_indexes_.clear();
    LoadResCache(current_level_, Utils::GetIGIRootPath());
    Logger::Get().Log(LogLevel::INFO, "[Renderer_Objects] Refreshed texture and model .res index for level " +
        std::to_string(current_level_));
}

void Renderer_Objects::LoadResCache(int levelNo, const std::string& igi_path) {
    // APPEND-ONLY: never clear existing indexes. Cross-level lazy calls add new
    // .res archives without evicting the current level's already-indexed files.
    const std::string levelName = "level" + std::to_string(levelNo);
    const std::string missionDir = igi_path + "\\missions\\location0\\" + levelName;
    const std::string commonDir  = igi_path + "\\missions\\location0\\common";

    // Helper: only add a path if not already in the index list.
    auto addTex = [&](const std::string& p) {
        for (const auto& ri : res_tex_indexes_)
            if (ri.res_path == p) return; // already indexed
        ResIndex ri;
        ri.res_path = p;
        std::string err;
        if (RES_BuildIndex(p, ri.index, err)) {
            Logger::Get().Log(LogLevel::INFO, "[ResCache] Indexed " +
                std::to_string(ri.index.size()) + " entries from " + p);
            res_tex_indexes_.push_back(std::move(ri));
        } else if (!err.empty()) {
            Logger::Get().Log(LogLevel::WARNING, "[ResCache] Texture index failed: " + err);
        }
    };
    auto addModel = [&](const std::string& p) {
        for (const auto& ri : res_model_indexes_)
            if (ri.res_path == p) return; // already indexed
        ResIndex ri;
        ri.res_path = p;
        std::string err;
        if (RES_BuildIndex(p, ri.index, err)) {
            Logger::Get().Log(LogLevel::INFO, "[ResCache] Indexed " +
                std::to_string(ri.index.size()) + " entries from " + p);
            res_model_indexes_.push_back(std::move(ri));
        } else if (!err.empty()) {
            Logger::Get().Log(LogLevel::WARNING, "[ResCache] Model index failed: " + err);
        }
    };

    // Level-specific comes first so it wins over common on name collisions.
    addTex  (missionDir + "\\textures\\" + levelName + ".res");
    addTex  (commonDir  + "\\textures\\location0.res");
    addModel(missionDir + "\\models\\" + levelName + ".res");
    addModel(commonDir  + "\\models\\location0.res");

    // A lazy cross-level call can add model names that a previously built LOD
    // chain (issue #62) saw as absent, so drop the cached chains. They are
    // rebuilt on the next lookup; this runs once per level load.
    lod_chain_cache_.clear();

}

// Try to find texture bytes in the in-memory .res index.
std::vector<uint8_t> Renderer_Objects::FindTextureData(const std::string& textureId) const {
    std::vector<ModelSourceArchiveIndex> archives;
    archives.reserve(res_tex_indexes_.size());
    for (const auto& index : res_tex_indexes_) {
        archives.push_back({index.res_path, index.index});
    }
    return FindPreviewTextureEntry(archives, textureId,
        [](const std::string& resPath, const ResEntryInfo& info) {
            return RES_ReadEntry(resPath, info);
        });
}

std::vector<uint8_t> Renderer_Objects::FindTextureDataFromLevel(
    const std::string& textureId, int levelNo) const {
    const std::string igiRoot = Utils::GetIGIRootPath();
    const std::string levelRes = igiRoot +
        "\\missions\\location0\\level" + std::to_string(levelNo) +
        "\\textures\\level" + std::to_string(levelNo) + ".res";
    // Source textures that are shared across levels live in the common
    // archive, not in any level archive (e.g. several 001_02_1 textures are
    // only in location0.res). Without this second bundle entry, a lookup for
    // such a texture misses its own source level and falls through to
    // same-name bytes from an unrelated level's archive.
    const std::string commonRes = igiRoot +
        "\\missions\\location0\\common\\textures\\location0.res";
    const std::string bundleRes[2] = { levelRes, commonRes };
    auto equalsCI = [](const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a[i])) !=
                std::tolower(static_cast<unsigned char>(b[i]))) return false;
        }
        return true;
    };
    // bundle[0] is the selected level archive, bundle[1] is common, and the
    // search walks them in that order: the level archive wins. That is the
    // oracle-correct precedence — the game resolves a model's textures from the
    // single archive of the level that owns the model file, with no cross-level
    // search (`IGI-RE src/model/model.c:261,450-461,300-306`; measured on the
    // retail install, `001_02_1.tex` is a 128x32 strip in common and the
    // 128x128 sheet in level7). An earlier version of this comment claimed the
    // opposite order; the code was always right.
    // See docs/igi-re-parity/CHARACTER_TEXTURE_RESOLUTION.md.
    std::array<ModelSourceArchiveIndex, 2> bundle;
    for (size_t slot = 0; slot < 2; ++slot) {
        bundle[slot].resPath = bundleRes[slot];
        for (const auto& index : res_tex_indexes_) {
            if (!equalsCI(index.res_path, bundleRes[slot])) continue;
            bundle[slot].entries = index.index;
            break;
        }
    }
    return FindModelSourceEntry(bundle, textureId, true,
        [](const std::string& resPath, const ResEntryInfo& info) {
            return RES_ReadEntry(resPath, info);
        });
}

// Try to find mesh bytes in the in-memory .res index.
std::vector<uint8_t> Renderer_Objects::FindMeshData(const std::string& modelId) const {
    const std::string fname = modelId + ".mef";
    for (const auto& ri : res_model_indexes_) {
        auto it = ri.index.find(fname);
        if (it != ri.index.end())
            return RES_ReadEntry(ri.res_path, it->second);
    }
    return {};
}

std::vector<uint8_t> Renderer_Objects::FindMeshDataFromLevel(
    const std::string& modelId, int levelNo) const {
    const std::string igiRoot = Utils::GetIGIRootPath();
    const std::string levelRes = igiRoot +
        "\\missions\\location0\\level" + std::to_string(levelNo) +
        "\\models\\level" + std::to_string(levelNo) + ".res";
    // Shared models live in the common archive; without it a source-level
    // mesh lookup misses and falls through to a same-name mesh from an
    // unrelated level's archive.
    const std::string commonRes = igiRoot +
        "\\missions\\location0\\common\\models\\location0.res";
    const std::string bundleRes[2] = { levelRes, commonRes };
    auto equalsCI = [](const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a[i])) !=
                std::tolower(static_cast<unsigned char>(b[i]))) return false;
        }
        return true;
    };
    // Level-specific comes first so it wins over common on name collisions.
    std::array<ModelSourceArchiveIndex, 2> bundle;
    for (size_t slot = 0; slot < 2; ++slot) {
        bundle[slot].resPath = bundleRes[slot];
        for (const auto& index : res_model_indexes_) {
            if (!equalsCI(index.res_path, bundleRes[slot])) continue;
            bundle[slot].entries = index.index;
            break;
        }
    }
    return FindModelSourceEntry(bundle, modelId, false,
        [](const std::string& resPath, const ResEntryInfo& info) {
            return RES_ReadEntry(resPath, info);
        });
}

// Free every baked lightmap's GL textures and drop the per-task bake state.
// Used on level switch (above) and by the Escape-menu Lightmaps checkbox when
// toggled OFF (so OFF reverts to the default bright look).
void Renderer_Objects::ClearAllLightmaps() {
    for (auto& pair : lightmap_textures_by_task_) {
        for (GLuint tex : pair.second) {
            if (tex != 0) glDeleteTextures(1, &tex);
        }
    }
    lightmap_textures_by_task_.clear();
    lightmap_bake_pose_by_task_.clear();
}

// ─── Shutdown ─────────────────────────────────────────────────────────────────
void Renderer_Objects::Shutdown() {
    for (auto& pair : mesh_cache_)
        destroyModel(pair.second);
    mesh_cache_.clear();
    attachment_cache_.clear();
    occupancy_sig_ = 0;

    for (auto& pair : texture_cache_) {
        if (pair.second) {
            glDeleteTextures(1, &pair.second);
        }
    }
    texture_cache_.clear();
    model_texture_map_cache_.clear();
    texture_map_level_ = -1;
    persistent_dat_ = DATFile{};
    persistent_dat_path_.clear();
    skin_geometry_cache_.clear();
    parsed_geometry_cache_.clear();

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
    if (selection_shader_) {
        glDeleteProgram(selection_shader_);
        selection_shader_ = 0;
    }
    if (sphere_vao_) {
        glDeleteVertexArrays(1, &sphere_vao_);
        sphere_vao_ = 0;
    }
    if (sphere_vbo_) {
        glDeleteBuffers(1, &sphere_vbo_);
        sphere_vbo_ = 0;
    }
    if (pick_fbo_) {
        glDeleteFramebuffers(1, &pick_fbo_);
        pick_fbo_ = 0;
    }
    if (pick_color_tex_) {
        glDeleteTextures(1, &pick_color_tex_);
        pick_color_tex_ = 0;
    }
    if (pick_depth_rb_) {
        glDeleteRenderbuffers(1, &pick_depth_rb_);
        pick_depth_rb_ = 0;
    }
    if (pick_shader_prog_) {
        glDeleteProgram(pick_shader_prog_);
        pick_shader_prog_ = 0;
    }
    pick_fbo_w_ = 0;
    pick_fbo_h_ = 0;
}

// ─── InitPickingFBO ───────────────────────────────────────────────────────────
void Renderer_Objects::Draw(GLuint ubo_mats, bool overlay_wireframe,
                            const std::vector<LevelObject>& objects, int selected_object_index, int hover_object_index, int draw_parts,
                            const glm::vec3& camera_pos, bool show_magic_obj_spheres, const std::unordered_set<int>* skip_static_draw_indices,
                            const glm::vec3& camera_forward, bool fast_preview, float tan_half_fovx)
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

    EnsureWindowModelIdsLoaded();

    if (!shader_program_) return;

    // Rebuild EditRigidObj occupancy so any ATTA that has been promoted to (or is
    // duplicated by) a real EditRigidObj is suppressed in the attachment render —
    // otherwise the promoted object would draw on top of its original ATTA.
    if (!fast_preview) {
        uint64_t occSig = objects.size();
        for (const auto& o : objects) {
            occSig = occSig * 1099511628211ull + static_cast<uint64_t>(o.deleted);
            if (o.deleted || o.type != "EditRigidObj" || o.modelId.empty()) continue;
            union { double d; uint64_t u; } x{o.pos.x}, y{o.pos.y}, z{o.pos.z};
            occSig ^= x.u + y.u * 3ull + z.u * 7ull;
            occSig ^= static_cast<uint64_t>(reinterpret_cast<uintptr_t>(o.modelId.data()));
        }
        if (occSig != occupancy_sig_) {
            occupancy_sig_ = occSig;
            editrigid_occupancy_.clear();
            suppressed_atta_keys_.clear();
            for (const auto& o : objects) {
                if (o.deleted || o.type != "EditRigidObj" || o.modelId.empty()) continue;
                editrigid_occupancy_.insert(AttaOccupancyKey(o.modelId, glm::vec3(o.pos)));
                if (o.name.rfind("ATTA:", 0) == 0) {
                    suppressed_atta_keys_.insert(o.name.substr(5));
                }
            }
        }
    }

    // Bind our object shader
    glUseProgram(shader_program_);

    // Bind the shared UBO (same buffer terrain uses — contains proj + view)
    glBindBufferBase(GL_UNIFORM_BUFFER, ubo_binding_point_, ubo_mats);

    // OpenGL state
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE); // Disable backface culling to fix invisible interior walls
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

    static bool logged_light_once = false;
    if (!logged_light_once) {
        Logger::Get().Log(LogLevel::INFO, "[Renderer] Lighting params: front=(" + std::to_string(sun_front_color_.r) + "," + std::to_string(sun_front_color_.g) + "," + std::to_string(sun_front_color_.b) + ") back=(" + std::to_string(sun_back_color_.r) + "," + std::to_string(sun_back_color_.g) + "," + std::to_string(sun_back_color_.b) + ") gamma=" + std::to_string(global_gamma_));
        logged_light_once = true;
    }

    glUniform3f(loc_dirlight, sun_front_color_.x, sun_front_color_.y, sun_front_color_.z);
    glUniform3f(loc_ambient,  global_ambient_.x,  global_ambient_.y,  global_ambient_.z);
    // Use the level's actual sun direction so dynamic-lit (non-lightmapped) objects
    // face the same light source as the game. Falls back to a sensible default if
    // the level hasn't set a sun direction yet.
    GLint loc_lightDir = glGetUniformLocation(shader_program_, "u_lightDir");
    {
        glm::vec3 sdir = (glm::length(sun_dir_) > 0.01f)
            ? glm::normalize(sun_dir_) : glm::vec3(0.5f, 1.0f, 0.5f);
        glUniform3f(loc_lightDir, sdir.x, sdir.y, sdir.z);
    }
    GLint loc_gamma = glGetUniformLocation(shader_program_, "u_gamma");
    glUniform1f(loc_gamma, global_gamma_);
    GLint loc_lm_authentic = glGetUniformLocation(shader_program_, "u_lm_authentic");
    glUniform1i(loc_lm_authentic, 0);
    GLint loc_lightmap_scale = glGetUniformLocation(shader_program_, "u_lightmapScale");
    GLint loc_useTex   = glGetUniformLocation(shader_program_, "u_useTexture");
    GLint loc_tex      = glGetUniformLocation(shader_program_, "u_texture");
    GLint loc_lightmap    = glGetUniformLocation(shader_program_, "u_lightmap");
    GLint loc_useLightmap = glGetUniformLocation(shader_program_, "u_useLightmap");
    GLint loc_alpha    = glGetUniformLocation(shader_program_, "u_alpha");
    GLint loc_baseColor = glGetUniformLocation(shader_program_, "u_baseColor");
    GLint loc_tint     = glGetUniformLocation(shader_program_, "u_tint");
    loc_glass_min_ = glGetUniformLocation(shader_program_, "u_glassMin");
    glUniform1f(loc_alpha, 1.0f); // default: fully opaque
    glUniform1f(loc_glass_min_, 0.0f); // default: not glass
    glUniform4f(loc_baseColor, 1.0f, 1.0f, 1.0f, 1.0f); // default: white
    glUniform3f(loc_tint, 1.0f, 1.0f, 1.0f); // default: no tint

    // Fog is terrain-only — objects/buildings are never fogged.

    EnsurePortalDistancesLoaded();

    glm::vec3 cameraFwd(0.0f);
    const float cameraFwdLen = glm::length(camera_forward);
    if (cameraFwdLen > 0.01f) cameraFwd = camera_forward / cameraFwdLen;

    const bool skipSkinned = skip_static_draw_indices && !skip_static_draw_indices->empty();
    const auto lightmapMode = igi::ObjectLightmapManager::Get().GetRenderMode();
    const int passCount = 2;
    for (int pass = 0; pass < passCount; ++pass) {
        bool isTransparentPass = (pass == 1);
        if (isTransparentPass) {
            glDepthMask(GL_FALSE);
        } else {
            glDepthMask(GL_TRUE);
        }

        // Accumulate elapsed time for continuous animations (helicopter rotor spin).
        {
            extern float g_renderer_delta_secs;
            elapsed_time_secs_ += g_renderer_delta_secs;
            g_renderer_delta_secs = 0.0f;  // consume once per frame
        }

        for (const auto& obj : objects) {
            if (obj.deleted) continue;

            const int objIndex = static_cast<int>(&obj - &objects[0]);
            // This object's rigid mesh is being replaced by a live skinned/animated
            // draw elsewhere this frame (see Renderer::DrawSkinnedMesh) — skip it
            // here so the two don't render on top of each other.
            if (skipSkinned && skip_static_draw_indices->count(objIndex)) continue;

            // Reset tint to white at the start of EVERY iteration so a magenta tint
            // set for a previous object can never leak into this one, regardless of
            // which early-continue path this object takes (issue 2).
            glUniform3f(loc_tint, 1.0f, 1.0f, 1.0f);

            // Selective rendering logic
        bool shouldDraw = false;
        if (draw_parts & DRAW_OBJECTS) {
            shouldDraw = true; // Draw everything
        } else {
            if ((draw_parts & DRAW_BUILDINGS) && obj.isBuilding) shouldDraw = true;
            if ((draw_parts & DRAW_PROPS) && !obj.isBuilding) shouldDraw = true;
        }

        if (!shouldDraw) continue;

        // SplineObjWaypoints and SplineObj containers are rendered by renderer_splines.cpp
        if (obj.isSplineWaypoint || obj.isSplineContainer) continue;

        // Skip empty modelId and collision-only proxies
        if (obj.modelId.empty()) continue;
        if (obj.modelId == "colbox" || obj.name == "colbox" ||
            obj.modelId == "colbox2" || obj.name == "colbox2" ||
            obj.modelId == "colbox4" || obj.name == "colbox4" ||
            obj.modelId == "colbox66" || obj.name == "colbox66") {
            continue;
        }

        const bool keepVisible = objIndex == selected_object_index || objIndex == hover_object_index;
        if (!igi::ShouldResolveMissingSceneModel(
                obj.modelMissingInRes,
                objIndex == selected_object_index,
                objIndex == hover_object_index)) {
            continue;
        }

        // Per-frame LOD switch.  ModelObj_GetLOD (HOOK 0x4CFCB0, src/lod/mesh3d.c:209-227)
        // feeds lod_select (src/lod/mesh3d.c:161-208), and the draw then uses
        // vm->lods[lod] (src/render/render.c:957).  A negative result is the engine's
        // "culled this frame" (render.c:955-956), not an error.
        const Mesh* meshPtr = &GetOrLoadMesh(obj.modelId, obj.isBuilding);
        int drawnLod = 0;
        if (meshPtr->vertexCount != 0 && tan_half_fovx > 0.0f) {
            const igi::lod::MefMeshHeader lod0h =
                igi::lod::ReadMefMeshHeader(FindMeshData(obj.modelId));
            if (lod0h.valid) {
                igi::lod::RigidLodQuery q;
                q.hasModel = true;
                q.objPos[0] = obj.pos.x;
                q.objPos[1] = obj.pos.y;
                q.objPos[2] = obj.pos.z;
                q.camPos[0] = camera_pos.x;
                q.camPos[1] = camera_pos.y;
                q.camPos[2] = camera_pos.z;
                // gTransContext.matrix.m[6..8]: the view matrix's Z row.
                q.viewZ[0] = cameraFwd.x;
                q.viewZ[1] = cameraFwd.y;
                q.viewZ[2] = cameraFwd.z;
                q.lod0Center[0] = lod0h.center[0];
                q.lod0Center[1] = lod0h.center[1];
                q.lod0Center[2] = lod0h.center[2];
                q.lod0Sphere = lod0h.sphere;
                q.objScale[0] = q.objScale[1] = q.objScale[2] = static_cast<float>(obj.scale);
                q.fov = tan_half_fovx;
                const int lod = SelectLodForDistance(
                    obj.modelId, obj.isBuilding, tan_half_fovx,
                    static_cast<float>(igi::lod::RigidViewDepth(q)),
                    static_cast<float>(igi::lod::MaxScale(q.objScale) * lod0h.sphere), 1.0f);
                if (lod < 0) continue;  // render.c:955-956
                drawnLod = lod;
                const std::string lodName = GetLodMeshName(obj.modelId, obj.isBuilding, lod);
                if (!lodName.empty() && lodName != obj.modelId) {
                    meshPtr = &GetOrLoadMesh(lodName, obj.isBuilding);
                }
            }
        }
        const Mesh& mesh = *meshPtr;
        if (mesh.vertexCount == 0) continue;

        const glm::vec3 toObject = glm::vec3(obj.pos) - camera_pos;
        const float boundRadius = glm::length(mesh.halfExtents) * 40.96f * std::max(obj.scale, 0.001f);
        const float distSq = glm::dot(toObject, toObject);
        if (cameraFwdLen > 0.01f && !keepVisible) {
            const float facing = glm::dot(toObject, cameraFwd);
            if (facing < -boundRadius) continue;
            if (distSq > boundRadius * boundRadius && facing < std::sqrt(distSq) * 0.5f - boundRadius) continue;
        }

        // Flag objects whose model is absent from the level .res: tint magenta so the
        // user sees it will be invisible in-game (issue 2). Reset to white at the top
        // of the next iteration guarantees this never leaks to other objects.
        if (obj.modelMissingInRes)
            glUniform3f(loc_tint, 1.0f, 0.2f, 1.0f); // magenta: missing in level .res

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

        // 2. Apply the authored IGI rotation.
        // IGI-RE `Matrix_SetEuler` (HOOK 0x4B38E0, src/math/matrix.c) composes
        // Rz(az) * Ry(ay) * Rx(ax) for EVERY authored object — there is no
        // per-model rotation order in the game (`ParamReal32x9_Parse`,
        // HOOK 0x4EFBA0, feeds the QSC triple straight into it). See
        // renderer/igi_transform.h for the full evidence. The previous
        // Z->X->Y default plus the `IsZyxEulerModel` whitelist was the
        // mis-orientation reported in issue #37.
        model = model * igi::IgiEulerRotationMatrix(obj.rot);

        // RotatingObject preview: radar dishes etc. spin continuously in-game via
        // runtime; we preview the same motion in-editor by adding a live yaw spin
        // on top of the placed orientation (mirrors the Heli rotor preview above).
        if (obj.type == "RotatingObject") {
            model = glm::rotate(model, elapsed_time_secs_ * 1.0f, glm::vec3(0.0f, 0.0f, 1.0f));
        }

        // A 90-degree Z turn for weapon/ammo pickups used to be applied here, on the
        // theory that the authored model stands on its barrel.  It was invented: the
        // engine draws a rigid object with the object's own matrix and scale only --
        // scale_matrix(&rot, &o->matrix, o->scale) then
        // Direct3D_MakeMatrix(&mat, rot.m, pos) (Direct3DRender_DrawRigidMesh
        // HOOK 0x49DF60, src/render/render.c:965-966).  The +/-90-degree turn exists
        // only in GunPickup_New HOOK 0x46C980 (src/comprun/pickup.c:423-429), which is
        // the engine's *dropped* pickup path, not the placed one this draws.
        //
        // `isWeapon` is kept: it gates the attachment pass below, which pickups do not
        // take part in.
        const bool isWeapon = IsWeaponModel(obj.modelId) || obj.type == "GunPickup" ||
                              obj.type == "AmmoPickup" || obj.type == "GenericPickup";

        // 3. Scale
        // Wire props are stretched between their two authored anchors by the game
        // itself: Wire_ParamsChanged (0x43F3D0, IGI-RE src/props/wire.c:160-191)
        // puts the object at the anchor midpoint, points its local +Y axis along
        // end - start, and sets obj.scale[1] = |end-start| * kWireScale (1/4096).
        // Only Y is scaled; X/Z keep the authored thickness. Without this the
        // editor drew every wire as an unstretched stub at its start anchor — the
        // "Wire Width" symptom in docs/KNOWN_ISSUES.md.
        bool wire_placed = false;
        if (obj.isWire) {
            glm::dvec3 wa, wb;
            if (obj.WireAnchors(wa, wb)) {
                const TaskSchemaNS::Props::WireSpan span = TaskSchemaNS::Props::ComputeWireSpan(
                    TaskSchemaNS::Props::Vec3(wa.x, wa.y, wa.z),
                    TaskSchemaNS::Props::Vec3(wb.x, wb.y, wb.z));
                if (span.valid) {
                    const TaskSchemaNS::Props::Basis basis =
                        TaskSchemaNS::Props::YAxisBasis(span.dir);
                    const glm::mat3 frame(
                        glm::vec3((float)basis.x.x, (float)basis.x.y, (float)basis.x.z),
                        glm::vec3((float)basis.y.x, (float)basis.y.y, (float)basis.y.z),
                        glm::vec3((float)basis.z.x, (float)basis.z.y, (float)basis.z.z));
                    model = glm::translate(glm::mat4(1.0f),
                        glm::vec3((float)span.mid.x, (float)span.mid.y, (float)span.mid.z));
                    model = model * glm::mat4(frame);
                    // Y = length in metres (length * kWireScale) through the same
                    // 40.96 * scale convention as every other object, so
                    // obj.scale keeps its 1:1 meaning: 40.96 * length/4096 = length/100.
                    model = glm::scale(model, glm::vec3(
                        total_scale,
                        (float)(span.length * TaskSchemaNS::Props::kWireScale) * base_scale,
                        total_scale));
                    wire_placed = true;
                }
            }
        }
        if (!wire_placed)
            model = glm::scale(model, glm::vec3(total_scale));

        // Native MEF geometry is already authored in the game's coordinate space.
        // Do not apply the legacy OBJ Y-up -> Z-up correction here or buildings/AI
        // will stand on the wrong axis.

        // Upload model matrix
        glUniformMatrix4fv(loc_model, 1, GL_FALSE, glm::value_ptr(model));

        // Wire/zipline + machinegun interactable previews (GitHub issue #42).
        DrawPropsGizmos(obj, ubo_mats, ubo_binding_point_);

        // ── Lighting and Color ────────────────────────────────────────────────
        // Compute a hash-based fallback color for models/submeshes without textures.
        float r = 0.6f, g = 0.6f, b = 0.6f;

        // Hull buildings that have no textures of their own but carry ATTA sub-models are
        // underground container shells — skip rendering them entirely. The ATTA sub-models
        // supply all visible geometry; rendering a hash-colored hull on top obscures them.
        bool skipHullRender = false;
        // Skip any model built from collision fallback geometry (XTVC/ECFC — no XTRV/DNER
        // render vertices). These produce misshapen meshes with fabricated UVs and render
        // as flat-colored or wrongly-tiled boxes. Covers vehicle hulls, train cargo objects,
        // and any other collision-only model regardless of whether it has ATTA children.
        if (!mesh.fromRenderMesh) {
            skipHullRender = true;
        }
        // Underground/hollow building shells (no own textures, ATTA children) are
        // now rendered OPAQUE like the game (the user's reference shows solid walls).
        // The old semi-transparent treatment (0.25 / 0.65) made Level 12 look like a
        // see-through mess. Kept as a flag = false so the transparent-pass routing
        // below treats them as ordinary opaque geometry.
        bool isUndergroundContainer = false;

        // Is this a window/glass model? If so, render the whole mesh semi-transparent.
        const bool isWindowModel = window_model_ids_.count(obj.modelId) > 0;
        const bool isTransparentObject = isWindowModel || isUndergroundContainer;

        // Does this model have any ARGB sub-meshes (alphaMode==2)?  Those must
        // render in the transparent pass so they properly blend over the background
        // with depth-writes disabled. Opaque sub-meshes of the same model still
        // render in the opaque pass — mixed models (guard tower, fence posts) draw
        // in BOTH passes, skipping the wrong-pass sub-meshes each time.
        // Tree billboard meshes (900/902/905 series) use alpha-test discard, not
        // alpha-blend. Force them to the opaque pass so the shader's discard line
        // fires (u_alpha=1.0 ≥ 0.9) and transparent areas vanish instead of showing
        // as semi-transparent gray rectangles.
        const bool isTreeModel = (obj.modelId == "900_01_1" ||
                                  obj.modelId == "902_01_1" ||
                                  obj.modelId == "905_01_1");

        bool hasArgbSubMeshes = false;
        if (!isTreeModel) {
            for (const auto& sub : mesh.subMeshes) {
                if (sub.alphaMode == 2) { hasArgbSubMeshes = true; break; }
            }
        }
        // NOTE: no level-12 forced-opaque special case here anymore. It routed
        // the winch-house ARGB glass (463_0x_1) into the opaque pass, where the
        // shader's alpha cutout discarded every texel — the building rendered
        // invisible until an ATTA promotion redrew it as a normal object.
        // A model belongs in this pass if: it's a uniform transparent/opaque
        // object that matches the pass, OR it has mixed sub-meshes and we draw
        // it in both passes (filtering per-sub-mesh below).
        const bool drawInThisPass = !skipHullRender && (
            isTransparentObject == isTransparentPass ||
            (hasArgbSubMeshes && !isTransparentObject)
        );
        if (drawInThisPass) {
            if (isTransparentObject) {
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                // Window glass: 0.55 gives visible reflectivity while still see-through.
                // Underground shell: 0.65 makes it visibly semi-transparent.
                glUniform1f(loc_alpha, isUndergroundContainer ? 0.65f : 0.55f);
                // Clean (clear) glass panes get a sheen floor so they never vanish.
                if (isWindowModel) glUniform1f(loc_glass_min_, 0.30f);
            }

            // Pull hull surfaces slightly toward camera to prevent Z-fighting with terrain.
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(-1.0f, -1.0f);

            // Draw each submesh with its own texture and lighting
            if (!mesh.subMeshes.empty()) {
                // taskId="-1" marks a nested/non-addressable task (DirlightKeyframe,
                // LightmapInfo, ATTA proxies, etc. all use this literal string) — it is
                // NOT unique, so looking it up here would apply whichever unrelated
                // object's lightmap/indoor-ambient happened to be registered under "-1"
                // to every other "-1" object too (e.g. unrelated crates turning the same
                // flat blue as some other building's interior ambient).
                // Lightmaps are keyed by LightmapTaskKey (task id, or authored position
                // for non-unique "-1" tasks) — the same key the apply path stores under.
                // Display is NOT gated on the global "Lightmaps" checkbox: any baked
                // lightmap present is shown (so the per-object Calculate button works
                // even with the checkbox off). The checkbox governs bulk calculate (ON)
                // and bulk clear (OFF). A baked lightmap stays bound even after the
                // object is moved/rotated — never deleted — and is modulated LIVE per
                // submesh by u_lightmapScale (below), so lighting adjusts smoothly as you
                // drag an object, with no "stale" fallback and no subprocess.
                const std::string lmKey = LightmapTaskKey(obj);
                const std::vector<GLuint>* lightmaps = GetLightmapForTask(lmKey);
                bool hasWorkingLightmap = lightmaps && !lightmaps->empty();
                glm::dvec3 bakedPos, bakedRot;
                const bool haveBakePose = hasWorkingLightmap &&
                    GetLightmapBakePose(lmKey, bakedPos, bakedRot);

                // Default lighting matches the pre-lightmap look:
                // Exterior — neutral ambient (0.4) + full sun diffuse from QSC sun color.
                // Interior — no direct sun, dim ambient only (clearly darker than outside).
                glm::vec3 kDefDirlight = sun_front_color_;
                // Warm floor matching the game's feel — no harsh flat daylight
                // Match the pre-lightmap default: every face was at least 40% ambient.
                // That commit hardcoded ambient=0.4/dirlight=0.6 so nothing looked dark.
                glm::vec3 kDefAmbient  = glm::max(global_ambient_, glm::vec3(0.40f, 0.40f, 0.40f));
                {
                    const glm::vec3* indoorAmb = GetIndoorAmbientForTask(obj.taskId);
                    if (indoorAmb && !hasWorkingLightmap) {
                        kDefDirlight = glm::vec3(0.0f);
                        // Interior: use authored ambient, clamped well below exterior floor.
                        kDefAmbient = glm::clamp(*indoorAmb, glm::vec3(0.22f), glm::vec3(0.40f, 0.40f, 0.40f));
                    }
                }

                // Precompute the per-channel modulation lambda for lightmapped submeshes.
                auto blockScale = [&](const glm::vec3& nLocal) -> glm::vec3 {
                    if (!haveBakePose) return glm::vec3(1.0f);
                    auto eul = [](const glm::dvec3& e) {
                        return igi::IgiEulerRotationMat3(e);
                    };
                    glm::vec3 nO = glm::normalize(eul(bakedRot) * nLocal);
                    glm::vec3 nN = glm::normalize(eul(obj.rot)  * nLocal);
                    glm::vec3 sd = glm::length(sun_dir_) > 1e-6f ? glm::normalize(sun_dir_) : glm::vec3(0,0,1);
                    auto L = [&](const glm::vec3& n){ return global_ambient_ + sun_front_color_ * std::max(glm::dot(n, sd), 0.0f); };
                    glm::vec3 lo = L(nO), ln = L(nN);
                    const float eps = 1e-3f, maxF = 4.0f;
                    return glm::vec3(std::min(ln.x/std::max(lo.x,eps),maxF),
                                     std::min(ln.y/std::max(lo.y,eps),maxF),
                                     std::min(ln.z/std::max(lo.z,eps),maxF));
                };
                for (size_t si = 0; si < mesh.subMeshes.size(); ++si) {
                    const auto& sub = mesh.subMeshes[si];
                    if (sub.VAO == 0 || sub.vertexCount == 0) continue;

                    // For mixed models: ARGB sub-meshes only in transparent pass,
                    // opaque sub-meshes only in opaque pass (unless the whole model
                    // is a transparent object, in which case all sub-meshes go through).
                    if (!isTransparentObject && hasArgbSubMeshes) {
                        if (isTransparentPass && sub.alphaMode != 2) continue;
                        if (!isTransparentPass && sub.alphaMode == 2) continue;
                    }

                    bool isBlendSub = (sub.alphaMode == 2);
                    if (isTreeModel) isBlendSub = false;

                    if (isBlendSub) {
                        glEnable(GL_BLEND);
                        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                        float subAlpha = sub.baseColorFactor.a;
                        glUniform1f(loc_alpha, subAlpha);
                        // Per-sub-mesh depth-write in the transparent pass. Only genuinely
                        // see-through glass (low alpha) skips depth writes so it stays clear
                        // from every angle; alpha-tested foliage / near-opaque panes keep
                        // writing depth so leaves stay solid and don't blend into dark blobs.
                        // (A previous opaque frame sub-mesh could otherwise leave depth-writes
                        //  on for a following glass pane, making the front window opaque.)
                        if (isTransparentPass)
                            glDepthMask(subAlpha < 0.6f ? GL_FALSE : GL_TRUE);
                        else if (subAlpha >= 0.99f)
                            glDepthMask(GL_TRUE);
                    }

                    if (sub.textureID > 0) {
                        // Textured submesh: neutral lighting so the texture looks natural.
                        // Windows/glass keep their transparency (alpha 0.4 above) but render
                        // with the SAME normal lighting as everything else, so glass stays
                        // clear and see-through.
                        const auto mode = lightmapMode;
                        float dirI = (mode == igi::LightmapRenderMode::Baked) ? 0.15f : (mode == igi::LightmapRenderMode::Hybrid ? 0.6f : 0.8f);
                        float ambI = (mode == igi::LightmapRenderMode::Baked) ? 0.85f : (mode == igi::LightmapRenderMode::Hybrid ? 0.4f : 0.3f);
                        if (mode == igi::LightmapRenderMode::Off) { dirI = 0.6f; ambI = 0.6f; }

                        glUniform3f(loc_dirlight, dirI, dirI, dirI);
                        glUniform3f(loc_ambient,  ambI, ambI, ambI);
                        glUniform1i(loc_useTex, 1);
                        glActiveTexture(GL_TEXTURE0);
                        glBindTexture(GL_TEXTURE_2D, sub.textureID);
                        glUniform1i(loc_tex, 0);
                    } else {
                        // Untextured submesh: material baseColorFactor if set, else neutral
                        // GRAY (not the per-object hash color, which tinted buildings blue).
                        glm::vec3 color(sub.baseColorFactor.r, sub.baseColorFactor.g, sub.baseColorFactor.b);
                        if (color.r >= 0.99f && color.g >= 0.99f && color.b >= 0.99f) {
                            color = glm::vec3(0.6f, 0.6f, 0.6f);
                        }
                        const auto mode = lightmapMode;
                        float dirMult = (mode == igi::LightmapRenderMode::Baked) ? 0.15f : (mode == igi::LightmapRenderMode::Hybrid ? 0.6f : 0.8f);
                        float ambMult = (mode == igi::LightmapRenderMode::Baked) ? 0.85f : (mode == igi::LightmapRenderMode::Hybrid ? 0.4f : 0.3f);
                        if (mode == igi::LightmapRenderMode::Off) { dirMult = 0.6f; ambMult = 0.6f; }

                        glUniform3f(loc_dirlight, color.r * dirMult, color.g * dirMult, color.b * dirMult);
                        glUniform3f(loc_ambient,  color.r * ambMult, color.g * ambMult, color.b * ambMult);
                        glUniform1i(loc_useTex, 0);
                    }

                    // Lightmap: bind unit 1 if the DRAWN LOD has a baked lightmap and
                    // mode is not Off.  The index follows the drawn LOD, not the LOD-0
                    // submesh: fill_refs (src/sector/sector.c:544-563) gives the object
                    // one LightmapRef per LOD and the draw binds
                    // o->lightmaps[lod].lightmaps (src/render/render.c:1019,1033).  A LOD
                    // with no bake is drawn with no atlas (render.c:1052-1054) -- the
                    // engine never falls back to another LOD's.
                    const auto curMode = lightmapMode;
                    const int lmEntry = igi::lightmap::OwnerLightmapEntryForDrawnLod(drawnLod);
                    if (curMode != igi::LightmapRenderMode::Off && hasWorkingLightmap &&
                        lmEntry >= 0 && lmEntry < static_cast<int>(lightmaps->size()) &&
                        (*lightmaps)[lmEntry] != 0) {
                        // Baked mode reproduces the original engine exactly: the bake is
                        // used verbatim, with no live sun re-light modulation.
                        const bool authentic = (curMode == igi::LightmapRenderMode::Baked);
                        glm::vec3 scale = authentic ? glm::vec3(1.0f) : blockScale(sub.avgNormal);
                        glActiveTexture(GL_TEXTURE1);
                        glBindTexture(GL_TEXTURE_2D, (*lightmaps)[lmEntry]);
                        glUniform1i(loc_lightmap, 1);
                        glUniform1i(loc_useLightmap, 1);
                        glUniform1i(loc_lm_authentic, authentic ? 1 : 0);
                        glUniform3f(loc_lightmap_scale, scale.r, scale.g, scale.b);
                    } else {
                        glUniform1i(loc_useLightmap, 0);
                    }

                    glBindVertexArray(sub.VAO);
                    glDrawArrays(GL_TRIANGLES, 0, sub.vertexCount);

                    if (isBlendSub) {
                        glDisable(GL_BLEND);
                        glUniform1f(loc_alpha, 1.0f);
                    }
                }
                glBindVertexArray(0);
                glBindBuffer(GL_ARRAY_BUFFER, 0);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
            } else {
                // Legacy single-texture path (e.g. old OBJ models)
                const auto mode = lightmapMode;
                float dirI = (mode == igi::LightmapRenderMode::Baked) ? 0.15f : (mode == igi::LightmapRenderMode::Hybrid ? 0.6f : 0.8f);
                float ambI = (mode == igi::LightmapRenderMode::Baked) ? 0.85f : (mode == igi::LightmapRenderMode::Hybrid ? 0.4f : 0.3f);

                bool hasTexture = (mesh.textureID > 0);
                if (hasTexture) {
                    glUniform3f(loc_dirlight, dirI, dirI, dirI);
                    glUniform3f(loc_ambient,  ambI, ambI, ambI);
                } else {
                    glUniform3f(loc_dirlight, dirI, dirI, dirI);
                    glUniform3f(loc_ambient,  r * ambI, g * ambI, b * ambI);
                }
                if (mesh.textureID > 0) {
                    glUniform1i(loc_useTex, 1);
                    GL_BindTexture2D(0, mesh.textureID);
                    glUniform1i(loc_tex, 0);
                } else {
                    glUniform1i(loc_useTex, 0);
                }
                renderModel(mesh);
            }

            if (isTransparentObject) {
                glDisable(GL_BLEND);
                glUniform1f(loc_alpha, 1.0f);
                glUniform1f(loc_glass_min_, 0.0f); // clear glass sheen for next object
            }

            glDisable(GL_POLYGON_OFFSET_FILL);
        }

        // Only buildings with portal/ATTA sub-models need distance-based culling.
        // Non-buildings (AI, static props, containers) never have ATTA records.
        bool isCloseEnough = true;
        if (obj.isBuilding && Config::Get().enableLOD) {
            float distToCamera = std::sqrt(distSq);
            float portalDistance = 100.0f;
            auto pit = portal_distances_.find(obj.modelId);
            if (pit != portal_distances_.end()) {
                portalDistance = pit->second;
            }
            isCloseEnough = igi::ShouldDrawBuildingAttachments(
                distToCamera, boundRadius, portalDistance * WORLD_UNITS_PER_METER, Config::Get().enableLOD);
        } else if (isCloseEnough) {
            const float maxAtta = (80.0f * WORLD_UNITS_PER_METER) + boundRadius;
            isCloseEnough = distSq < maxAtta * maxAtta;
        }

        if (isCloseEnough && !isWeapon) {
            FillLevelModelKey(level_model_key_scratch_, current_level_, obj.isBuilding, obj.modelId);
            if (attachment_cache_.find(level_model_key_scratch_) != attachment_cache_.end()) {
                glEnable(GL_POLYGON_OFFSET_FILL);
                glPolygonOffset(-2.0f, -2.0f); // Prevent z-fighting with hull walls

                // Build the root object's unscaled world matrix (translate + rotate).
                // Rotation order is the game's Rz * Ry * Rx (see renderer/igi_transform.h).
                const glm::mat4 parentRot = igi::IgiEulerRotationMatrix(obj.rot);

                glm::mat4 rootWorldMat(1.0f);
                rootWorldMat = glm::translate(rootWorldMat, glm::vec3((float)obj.pos.x, (float)obj.pos.y, (float)obj.pos.z));
                rootWorldMat = rootWorldMat * parentRot;

                std::unordered_set<std::string> drawn;
                DrawAttachmentsRecursive(obj.modelId, obj.modelId, obj.isBuilding, rootWorldMat, isTransparentPass,
                                          loc_model, loc_dirlight, loc_ambient,
                                          loc_useTex, loc_tex, loc_alpha, drawn);

                // DrawAttachmentsRecursive may leave GL_BLEND enabled and
                // depth-writes disabled for ARGB (glass/alpha) sub-meshes.
                // Reset both so the next object in the outer loop renders correctly
                // (prevents tree leaves and other alpha-tested geometry from
                //  appearing transparent when a building attachment drew before them).
                glDisable(GL_BLEND);
                glDepthMask(GL_TRUE);
                glUniform1f(loc_alpha, 1.0f);
                // Window ATTA children leave u_glassMin raised (0.30), which
                // would make every later object skip the alpha cutout.
                glUniform1f(loc_glass_min_, 0.0f);
                glDisable(GL_POLYGON_OFFSET_FILL);
            }
        }

        // Companion parts (_XX_2.._XX_5) are NOT rendered alongside the main body.
        // Main body models (_XX_1) already contain a complete character mesh including
        // the face. Companion parts duplicate the head geometry at a different depth,
        // which produces a visible double-face overdraw artifact.
    }
    }

    // Always reset polygon mode after draw
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDepthFunc(GL_LEQUAL); // Restore global depth func changed above to GL_LESS
    glDisable(GL_POLYGON_OFFSET_FILL);

    // Unbind shader
    glUseProgram(0);

    if (show_magic_obj_spheres) {
        DrawMagicObjSpheres(objects, ubo_mats);
    }
}

bool Renderer_Objects::IsVehicleType(const std::string& type) {
    return type == "Car" || type == "Heli" || type == "Plane" || type == "Train";
}

// ─── GetOrLoadMesh ────────────────────────────────────────────────────────────
