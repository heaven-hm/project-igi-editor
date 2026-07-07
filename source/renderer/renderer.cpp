#include "renderer_internal.h"

/*
================================================================================
 Editor bitmap font (editor/qed/editor.fnt) for HUD text
================================================================================
*/

/*
================================================================================
 Renderer
================================================================================
*/
Renderer::Renderer() : ubo_mats_(0), ubo_fog_(0), splines_(objects_) {
}

Renderer::~Renderer() { Shutdown(); }

#include <sstream>
#include <iomanip>
#include <unordered_map>




bool Renderer::Init() {
  ubo_mats_ = GL_CreateBuffer(GL_UNIFORM_BUFFER, sizeof(ubo_mats_s), nullptr,
                              GL_DYNAMIC_DRAW);
  // Initialize UBO with safe defaults so terrain fog shader never sees g_fog_far=0
  // (divide-by-zero → NaN → black overlay before SetupFog is called on first level load).
  {
    ubo_fog_s safe_fog = {};
    safe_fog.color_ = glm::vec4(0.15f, 0.15f, 0.15f, 1.0f);
    safe_fog.far_   = 1e9f; // enormous distance = no visible fog until level loads
    safe_fog.intensity_ = 0.10f;
    ubo_fog_ = GL_CreateBuffer(GL_UNIFORM_BUFFER, sizeof(ubo_fog_s), &safe_fog,
                               GL_STATIC_DRAW);
  }

  if (!skydome_.Init()) {
    return false;
  }

  if (!flat_sky_layers_.Init()) {
    return false;
  }

  if (!terrain_.Init()) {
    return false;
  }

  if (!objects_.Init()) {
    return false;
  }

  if (!rain_.Init()) {
    return false;
  }

  // init default state
  glClearColor(0.1f, 0.1f, 0.1f, 1.0f);

  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glDepthMask(GL_TRUE);
  glDepthRangef(RENDER_DEPTH_MIN, RENDER_DEPTH_MAX);

  glEnable(GL_CULL_FACE);
  glFrontFace(GL_CCW);
  glCullFace(GL_BACK);

#if defined(_WIN32)
  glPolygonOffset(-1.0f, -1.0f);
#else
  glPolygonOffset(-64.0f, -64.0f); // tune this
#endif
  glDisable(GL_POLYGON_OFFSET_LINE);

  GL_TryEnableVSync();

  return true;
}

void Renderer::Shutdown() {
  terrain_.Shutdown();
  objects_.Shutdown();
  rain_.Shutdown();
  flat_sky_layers_.Shutdown();

  skydome_.Shutdown();

  GL_DeleteBuffer(ubo_fog_);
  GL_DeleteBuffer(ubo_mats_);
}

void Renderer::BeginLoadLevel() {
  flat_sky_layers_.UnloadAllTexs();
  terrain_.UnloadAllTexs();
  objects_.ClearCaches();
  // Disable rain until the new level's QSC is parsed — prevents rain from a
  // prior level bleeding into a level that has no RainEffect task.
  rain_.SetParams(false, 0.0f, 0.0f, 0.0f);
  graph_overlay_ = GraphFile{};
  graph_overlay_visible_ = false;
  graph_overlay_dirty_ = false;
  graph_overlay_selected_ = -1;
  graph_overlay_path_.clear();
  graph_overlay_taskid_.clear();
  graph_overlay_area_.clear();
}

void Renderer::SetupClearColor(const glm::vec4 &color) {
  glClearColor(color.r, color.g, color.b, 1.0f);
}

void Renderer::SetupFog(const glm::vec4 &color, float fog_far) {
  ubo_fog_s ubo_fog;

  ubo_fog.color_ = color;
  ubo_fog.far_ = fog_far;
  ubo_fog.pad0_ = ubo_fog.pad1_ = ubo_fog.pad2_ = 0.0f;

  // Preserve prior user-set intensity (from pause menu) across level fog loads.
  // Read existing intensity at std140 offset 32 if UBO already exists; else default 10%.
  float prev_intensity = 0.10f;
  if (ubo_fog_) {
    glBindBuffer(GL_UNIFORM_BUFFER, ubo_fog_);
    glGetBufferSubData(GL_UNIFORM_BUFFER, 32, sizeof(float), &prev_intensity);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
  }
  ubo_fog.intensity_ = prev_intensity;

  GL_BufferData(ubo_fog_, GL_UNIFORM_BUFFER, sizeof(ubo_fog), &ubo_fog,
                GL_STATIC_DRAW);

  // Propagate fog to object shader (warm atmospheric haze) and skydome
  // (blend horizon with fog color so sky matches the game's hazy look).
  objects_.SetFogParams(glm::vec3(color), fog_far);
  skydome_.SetFogColor(glm::vec3(color));
}

void Renderer::SetFogIntensity(int pct) {
  // Update only the intensity field in the UBO (std140: after vec4+3 floats padding = 32).
  float intensity = static_cast<float>(pct) / 100.0f; // 0.0-2.0
  glBindBuffer(GL_UNIFORM_BUFFER, ubo_fog_);
  glBufferSubData(GL_UNIFORM_BUFFER, 32, sizeof(float), &intensity);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void Renderer::SetupSkydome(const skydome_define_s &d) {
  skydome_.UpdateVertices(d);
}

void Renderer::LoadFlatSkyLayerTex(int layer_no, const pic_s *pic) {
  flat_sky_layers_.LoadLayerTex(layer_no, pic);
}

void Renderer::LoadTerrainMatTex(const pic_s *pic) { terrain_.LoadMatTex(pic); }

void Renderer::LoadTerrainLMPTex(const pic_s *pic) { terrain_.LoadLMPTex(pic); }

vert_flat_sky_layer_s *Renderer::MapFlatSkyLayersVB() {
  return flat_sky_layers_.MapVB();
}

void Renderer::UnmapFlatSkyLayersVB() { flat_sky_layers_.UnmapVB(); }

vert_pos_a_uv_s *Renderer::MapTerrainVB() { return terrain_.MapVB(); }

void Renderer::UnmapTerrainVB() { terrain_.UnmapVB(); }

uint32_t *Renderer::MapTerrainIB() { return terrain_.MapIB(); }

void Renderer::UnmapTerrainIB() { terrain_.UnmapIB(); }

render_chunk_s *Renderer::GetTerrainRenderChunckBuffer() {
  return terrain_.GetRenderChunckBuffer();
}

// Reads the depth buffer at screen (mx, my_topdown) and unprojects to a world-space
// point using the full world->clip matrix (proj*view*scale). Returns false if nothing
// was drawn there (cursor over sky / cleared depth). (issue 3)
void Renderer::SetupUBOMats(const view_define_s &vd) {
  glm::mat4 mat_proj_persp = glm::perspective(
      vd.fovy_, (float)vd.viewport_width_ / vd.viewport_height_,
      vd.render_z_near_, vd.render_z_far_);

  glm::vec3 scaled_down_pos = vd.pos_ * RENDERER_MODEL_SCALE_DOWN;
  glm::mat4 mat_view = glm::lookAt(
      scaled_down_pos, scaled_down_pos + vd.forward_ * WORLD_UNITS_PER_METER,
      vd.up_);
  glm::mat4 mat_follow_view = glm::lookAt(VEC3_ORIGIN, vd.forward_, vd.up_);
  glm::mat4 mat_scale =
      glm::scale(glm::mat4(1.0f), glm::vec3(RENDERER_MODEL_SCALE_DOWN));

  mat_proj_ = mat_proj_persp;
  mat_view_ = mat_view;

  // setup uniform buffer

  ubo_mats_s ubo_mats;

  // skydome
  ubo_mats.mvp_mat_follow_view_ = mat_proj_persp * mat_follow_view * mat_scale;

  // flat sky layer
  ubo_mats.mvp_flat_sky_layer_ =
      glm::ortho(0.0f, (float)vd.viewport_width_, 0.0f,
                 (float)vd.viewport_height_, -1.0f, 1.0f);

  // terrain
  ubo_mats.mvp_objects_ = mat_proj_persp * mat_view * mat_scale;

  // flush ubo buffer
  GL_BufferData(ubo_mats_, GL_UNIFORM_BUFFER, sizeof(ubo_mats), &ubo_mats,
                GL_DYNAMIC_DRAW);
}

void Renderer::DrawAnimSkeleton(const std::vector<glm::mat4>& boneWorldTransforms,
                                const std::vector<int>& boneParents,
                                const glm::mat4& objWorldMat) {
    if (boneWorldTransforms.empty()) return;

    GLint prevProg;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    glUseProgram(0);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glm::mat4 mvp = mat_proj_ * mat_view_ * glm::scale(glm::mat4(1.0f), glm::vec3(RENDERER_MODEL_SCALE_DOWN));
    glLoadMatrixf(glm::value_ptr(mvp));

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_LIGHTING);  // must disable so glColor4f applies, not the lit material (which renders black)
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    // Helper: compute world-space bone position. boneWorldTransforms already
    // carries the BEF skeleton's absolute (animated) position — see
    // DrawSkinnedMesh for why no extra MEF root offset is added here.
    auto boneWorldPos = [&](size_t i) -> glm::vec4 {
        glm::vec3 posInBefSpace = glm::vec3(boneWorldTransforms[i] * glm::vec4(0.f, 0.f, 0.f, 1.f));
        return objWorldMat * glm::vec4(posInBefSpace, 1.f);
    };

    glLineWidth(3.0f);
    glBegin(GL_LINES);
    for (size_t i = 0; i < boneWorldTransforms.size(); ++i) {
        if (i >= boneParents.size()) break;
        int p = boneParents[i];
        if (p < 0 || (size_t)p >= boneWorldTransforms.size()) continue;
        glm::vec4 childPos  = boneWorldPos(i);
        glm::vec4 parentPos = boneWorldPos((size_t)p);
        glColor4f(0.2f, 0.9f, 0.2f, 0.7f);
        glVertex4f(parentPos.x, parentPos.y, parentPos.z, parentPos.w);
        glVertex4f(childPos.x,  childPos.y,  childPos.z,  childPos.w);
    }
    glEnd();

    glPointSize(5.0f);
    glColor4f(0.6f, 1.0f, 0.6f, 1.0f);
    glBegin(GL_POINTS);
    for (size_t i = 0; i < boneWorldTransforms.size(); ++i) {
        glm::vec4 wp = boneWorldPos(i);
        glVertex4f(wp.x, wp.y, wp.z, wp.w);
    }
    glEnd();

    glLineWidth(1.0f);
    glPointSize(1.0f);

    glEnable(GL_LIGHTING);
    glEnable(GL_TEXTURE_2D);

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();

    if (prevProg) glUseProgram((GLuint)prevProg);
}

void Renderer::DrawSkinnedMesh(const std::string& modelId, bool isBuilding,
                                const std::vector<glm::mat4>& boneWorldTransforms,
                                const glm::mat4& objWorldMat) {
    if (boneWorldTransforms.empty()) return;
    const ParsedGeometry* geo = objects_.GetOrLoadSkinGeometry(modelId, isBuilding);
    if (!geo || geo->vertices.empty() || geo->triangles.empty()) return;

    // Rest-pose bone world positions (raw units) — same convention the static
    // mesh cache used to bake geo->vertices[i].pos in the first place.
    static std::map<std::string, std::vector<glm::vec3>> s_restPosCache;
    auto rpIt = s_restPosCache.find(modelId);
    if (rpIt == s_restPosCache.end()) {
        rpIt = s_restPosCache.emplace(modelId, ComputeBoneWorldPositionsPublic(geo->bones)).first;
    }
    const std::vector<glm::vec3>& boneRestPos = rpIt->second;
    if (boneRestPos.empty()) return;

    // boneWorldTransforms (from the BEF clip) already carry the skeleton's
    // absolute animated position — see TranslationKeyFrameData in the .BEF,
    // which stores the root bone's full world height, not a local offset from
    // zero. So at rest, boneWorldTransforms[b] == boneRestPos[b]*kMefNativeScale
    // and deformedPos reduces to exactly rv.pos (no extra root shift needed;
    // adding one here previously double-counted the root height).
    std::vector<glm::vec3> deformedPos(geo->vertices.size());
    std::vector<glm::vec3> deformedNormal(geo->vertices.size());
    for (size_t i = 0; i < geo->vertices.size(); ++i) {
        const RenderVertex& rv = geo->vertices[i];
        uint16_t b = rv.boneIndex;
        if (b >= boneRestPos.size() || b >= boneWorldTransforms.size()) {
            deformedPos[i] = rv.pos;
            deformedNormal[i] = rv.normal;
            continue;
        }
        glm::vec3 localOffset = rv.pos - boneRestPos[b] * kMefNativeScale;
        deformedPos[i] = glm::vec3(boneWorldTransforms[b] * glm::vec4(localOffset, 1.0f));
        deformedNormal[i] = glm::mat3(boneWorldTransforms[b]) * rv.normal;
    }

    // Resolve the same per-material textures the static (rigid) draw uses, so
    // the live-skinned mesh looks identical when not moving instead of a flat
    // debug color. Cheap: GetOrLoadMesh hits the cache (the model is already
    // loaded for the rigid draw elsewhere).
    Mesh mesh = objects_.GetOrLoadMesh(modelId, isBuilding);
    std::unordered_map<int, GLuint> slotToTexture;
    for (const auto& sub : mesh.subMeshes) slotToTexture[sub.materialSlot] = sub.textureID;
    const GLuint fallbackTexture = mesh.textureID;

    GLint prevProg;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    glUseProgram(0);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glm::mat4 mvp = mat_proj_ * mat_view_ * glm::scale(glm::mat4(1.0f), glm::vec3(RENDERER_MODEL_SCALE_DOWN));
    glLoadMatrixf(glm::value_ptr(mvp));

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    // The static (rigid) object draw never culls ("Disable backface culling
    // to fix invisible interior walls" — see Renderer_Objects::Draw). Match
    // that here: culling against the deformed mesh risks the wrong winding
    // after the bone transform and silently dropping faces (looked like a
    // broken/incomplete blob instead of the full body).
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    // Fixed world-space light direction (biased upward/forward so characters are
    // well-lit from above, matching a simple sky-light approximation).
    // NOTE: deformedNormal[i] is already in bone-local/world space (it was
    // transformed by boneWorldTransforms[b] during skinning above). Do NOT
    // multiply it by normalMat(objWorldMat) again — that is a double-transform
    // that makes normals point in wrong directions and renders the mesh black.
    const glm::vec3 lightDir = glm::normalize(glm::vec3(0.3f, 0.5f, 0.8f));
    const float kAmbient = 0.40f;  // minimum brightness so no face is fully black

    // Disable legacy GL lighting so glColor4f/glTexCoord2f actually apply.
    glDisable(GL_LIGHTING);

    auto litColor = [&](uint32_t vi) {
        // deformedNormal is already transformed by the bone matrix, use it directly.
        glm::vec3 n = deformedNormal[vi];
        float nLen = glm::length(n);
        float diff = kAmbient;  // default: ambient only for degenerate normals
        if (nLen > 1e-5f) {
            float d = glm::dot(n / nLen, lightDir);
            if (!std::isfinite(d)) d = 0.0f;
            diff = kAmbient + (1.0f - kAmbient) * glm::clamp(d, 0.0f, 1.0f);
        }
        glColor4f(diff, diff, diff, 1.0f);
    };
    auto emitVertex = [&](uint32_t vi) {
        litColor(vi);
        glTexCoord2f(geo->vertices[vi].uv.x, 1.0f - geo->vertices[vi].uv.y);
        glm::vec4 worldPos = objWorldMat * glm::vec4(deformedPos[vi], 1.0f);
        glVertex4f(worldPos.x, worldPos.y, worldPos.z, worldPos.w);
    };

    // Draw per render-block, binding each block's own material texture — same
    // triangleStart/triangleCount ranges into geo->triangles that the static
    // (rigid) mesh build in model.cpp uses, so multi-material characters
    // (skin/clothes/face/etc.) get their correct textures instead of one
    // flat body texture for the whole mesh.
    auto drawRange = [&](size_t start, size_t count, GLuint tex) {
        if (tex != 0) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, tex);
        } else {
            glDisable(GL_TEXTURE_2D);
        }
        glBegin(GL_TRIANGLES);
        for (size_t t = start; t < start + count && t < geo->triangles.size(); ++t) {
            const auto& tri = geo->triangles[t];
            for (int k = 0; k < 3; ++k) {
                uint32_t vi = tri[k];
                if (vi < deformedPos.size()) emitVertex(vi);
            }
        }
        glEnd();
    };

    if (!geo->renderBlocks.empty()) {
        for (const auto& block : geo->renderBlocks) {
            if (block.triangleCount == 0) continue;
            auto it = slotToTexture.find(block.materialSlot);
            GLuint tex = (it != slotToTexture.end() && it->second != 0) ? it->second : fallbackTexture;
            drawRange(block.triangleStart, block.triangleCount, tex);
        }
    } else {
        drawRange(0, geo->triangles.size(), fallbackTexture);
    }

    glEnable(GL_LIGHTING);   // restore legacy lighting state disabled above
    glEnable(GL_TEXTURE_2D); // restore in case caller relies on it

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();

    if (prevProg) glUseProgram((GLuint)prevProg);
}

