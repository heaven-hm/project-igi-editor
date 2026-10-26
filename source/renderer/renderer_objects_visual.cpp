/******************************************************************************
 * @file    renderer_objects_visual.cpp
 * @brief   Renderer_Objects: spheres, text/cube meshes, selection box, model preview
 *          Split from renderer_objects.cpp; shares renderer_objects_internal.h.
 *****************************************************************************/
#include "renderer_objects_internal.h"
#include "igi_transform.h"

void Renderer_Objects::InitSphereMesh() {
    const int LAT = 12;
    const int LON = 24;
    std::vector<float> verts;
    verts.reserve(LAT * LON * 6 * 8); // 2 triangles per quad, 3 verts each, 8 floats

    auto addVert = [&](float x, float y, float z) {
        verts.push_back(x); verts.push_back(y); verts.push_back(z);
        verts.push_back(x); verts.push_back(y); verts.push_back(z); // normal = position for unit sphere
        verts.push_back(0.0f); verts.push_back(0.0f);
    };

    for (int i = 0; i < LAT; ++i) {
        float lat0 = glm::pi<float>() * (-0.5f + (float)i / LAT);
        float lat1 = glm::pi<float>() * (-0.5f + (float)(i + 1) / LAT);
        float sin0 = std::sin(lat0), cos0 = std::cos(lat0);
        float sin1 = std::sin(lat1), cos1 = std::cos(lat1);

        for (int j = 0; j < LON; ++j) {
            float lon0 = 2.0f * glm::pi<float>() * (float)j / LON;
            float lon1 = 2.0f * glm::pi<float>() * (float)(j + 1) / LON;
            float clon0 = std::cos(lon0), slon0 = std::sin(lon0);
            float clon1 = std::cos(lon1), slon1 = std::sin(lon1);

            float x00 = cos0*clon0, y00 = cos0*slon0, z00 = sin0;
            float x01 = cos0*clon1, y01 = cos0*slon1, z01 = sin0;
            float x10 = cos1*clon0, y10 = cos1*slon0, z10 = sin1;
            float x11 = cos1*clon1, y11 = cos1*slon1, z11 = sin1;

            addVert(x00, y00, z00); addVert(x10, y10, z10); addVert(x11, y11, z11);
            addVert(x00, y00, z00); addVert(x11, y11, z11); addVert(x01, y01, z01);
        }
    }

    sphere_vertex_count_ = (int)(verts.size() / 8);
    glGenVertexArrays(1, &sphere_vao_);
    glGenBuffers(1, &sphere_vbo_);
    glBindVertexArray(sphere_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, sphere_vbo_);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

// ─── DrawMagicObjSpheres ──────────────────────────────────────────────────────
// Draws a solid red sphere at each MagicObject ATTA attachment position.
// Also hosts the ladder placement gizmos of GitHub issue #68 (see
// DrawLadderGizmoGeometry at the bottom of this file): this overlay is the
// editor's existing "Magic objects" view toggle (App::show_magic_obj_spheres_,
// flipped in app_input.cpp), so the ladder previews inherit an on/off switch and
// normal editing is untouched while it is off.
static void DrawLadderGizmoGeometry(const glm::mat3& frame, const glm::vec3& origin,
                                    const glm::vec3& reference, const glm::vec3& foot,
                                    const glm::vec3& head, const glm::vec3& bottom_mount,
                                    const glm::vec3& top_mount, int rung_count,
                                    GLuint ubo_mats, GLuint ubo_binding);

void Renderer_Objects::DrawMagicObjSpheres(const std::vector<LevelObject>& objects, GLuint ubo_mats) {
    EnsureMagicObjIdsLoaded();
    if (magicobj_ids_.empty()) return;

    if (sphere_vao_ == 0) InitSphereMesh();
    if (sphere_vao_ == 0) return;

    glUseProgram(shader_program_);
    glBindBufferBase(GL_UNIFORM_BUFFER, ubo_binding_point_, ubo_mats);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    GLint loc_model     = glGetUniformLocation(shader_program_, "u_model");
    GLint loc_dirlight  = glGetUniformLocation(shader_program_, "u_dirlight");
    GLint loc_ambient   = glGetUniformLocation(shader_program_, "u_ambient");
    GLint loc_useTex    = glGetUniformLocation(shader_program_, "u_useTexture");
    GLint loc_alpha     = glGetUniformLocation(shader_program_, "u_alpha");
    GLint loc_baseColor = glGetUniformLocation(shader_program_, "u_baseColor");

    glUniform1i(loc_useTex, 0);
    glUniform1f(loc_alpha, 1.0f);
    glUniform3f(loc_dirlight, 0.5f, 0.5f, 0.5f);
    glUniform3f(loc_ambient,  0.5f, 0.5f, 0.5f);
    glUniform4f(loc_baseColor, 1.0f, 0.0f, 0.0f, 1.0f); // red

    // Sphere radius = 0.3 meters in world-unit space
    const float SPHERE_SCALE = WORLD_UNITS_PER_METER * 0.3f;

    auto drawSphere = [&](const glm::vec3& worldPos) {
        glm::mat4 sphereMat = glm::translate(glm::mat4(1.0f), worldPos);
        sphereMat = glm::scale(sphereMat, glm::vec3(SPHERE_SCALE));
        glUniformMatrix4fv(loc_model, 1, GL_FALSE, glm::value_ptr(sphereMat));
        glBindVertexArray(sphere_vao_);
        glDrawArrays(GL_TRIANGLES, 0, sphere_vertex_count_);
    };

    for (const auto& obj : objects) {
        if (obj.deleted || obj.modelId.empty()) continue;

        glm::mat4 parentRot(1.0f);
        parentRot = glm::rotate(parentRot, (float)obj.rot.z, glm::vec3(0, 0, 1));
        parentRot = glm::rotate(parentRot, (float)obj.rot.x, glm::vec3(1, 0, 0));
        parentRot = glm::rotate(parentRot, (float)obj.rot.y, glm::vec3(0, 1, 0));

        glm::mat4 rootWorldMat(1.0f);
        rootWorldMat = glm::translate(rootWorldMat, glm::vec3((float)obj.pos.x, (float)obj.pos.y, (float)obj.pos.z));
        rootWorldMat = rootWorldMat * parentRot;

        // ── ATTA magic objects ───────────────────────────────────────────────
        std::string prefix = obj.isBuilding ? "building:" : "object:";
        std::string attKey = std::to_string(current_level_) + ":" + prefix + obj.modelId;
        auto ait = attachment_cache_.find(attKey);
        if (ait != attachment_cache_.end()) {
            for (const auto& att : ait->second) {
                if (!magicobj_ids_.count(att.modelId)) continue;
                glm::vec3 localOff(att.px, att.py, att.pz);
                glm::vec3 worldPos = glm::vec3(rootWorldMat * glm::vec4(localOff, 1.0f));
                drawSphere(worldPos);
            }
        }

        // ── XTVM magic vertices ──────────────────────────────────────────────
        // magicVertices are stored in mesh-local space (divided by 40.96).
        // Reconstruct world position using the full scaled model matrix.
        std::string meshKey = std::to_string(current_level_) + ":" + prefix + obj.modelId;
        auto mit = mesh_cache_.find(meshKey);
        if (mit != mesh_cache_.end()) {
            const Mesh& mesh = mit->second;
            if (!mesh.magicVertices.empty()) {
                glm::mat4 modelMat = rootWorldMat * glm::scale(glm::mat4(1.0f), glm::vec3(40.96f));
                for (const auto& mv : mesh.magicVertices) {
                    // skip null entries (all-zero position)
                    if (mv.x == 0.0f && mv.y == 0.0f && mv.z == 0.0f) continue;
                    glm::vec3 worldPos = glm::vec3(modelMat * glm::vec4(mv, 1.0f));
                    drawSphere(worldPos);
                }
            }
        }
    }

    glBindVertexArray(0);
    glUseProgram(0);

    // ── Ladder placement gizmos (GitHub issue #68) ───────────────────────────
    // A second pass, after the sphere pass, so the gizmo's own program/VAO can
    // never disturb the spheres' uniforms. Gated by this overlay plus its own
    // switch (igi::SetLadderGizmosEnabled).
    if (!igi::LadderGizmosEnabled()) return;
    for (const auto& obj : objects) {
        if (obj.deleted || obj.modelId.empty()) continue;
        if (!IsLadderMagicObject(obj.modelId)) continue;

        const Mesh& mesh = GetOrLoadMesh(obj.modelId, obj.isBuilding);
        // Magic points 1, 2 and 3 are the activation reference, the foot and the
        // head (IGI-RE src/props/ladder.c:86-87,155,164 and
        // src/weapon/humanplayer.c:300,308); point 0 is unused by the ladder
        // handlers, which is why the shipped ladder MEFs carry four XTVM points.
        if (mesh.magicVertices.size() < 4) continue;

        const glm::mat3 frame = igi::IgiEulerRotationMat3(obj.rot);
        const glm::vec3 origin((float)obj.pos.x, (float)obj.pos.y, (float)obj.pos.z);
        // Magic vertices are mesh-local (MEF units / 40.96, renderer/model.cpp:34)
        // and are drawn through scale(40.96), so the world point of magic point i
        // is origin + frame * (vertex * 40.96) — the same transform
        // App::AddRuntimeLadderIfPresent applies to the gameplay placements.
        auto point = [&](size_t index) {
            return origin + frame * (mesh.magicVertices[index] * 40.96f);
        };
        auto props_vec = [](const glm::vec3& v) {
            return TaskSchemaNS::Props::Vec3((double)v.x, (double)v.y, (double)v.z);
        };

        TaskSchemaNS::Props::Basis basis;
        basis.x = props_vec(frame[0]);
        basis.y = props_vec(frame[1]);
        basis.z = props_vec(frame[2]);

        const TaskSchemaNS::Props::LadderPlacement ladder =
            TaskSchemaNS::Props::ComputeLadderPlacement(
                props_vec(origin), props_vec(point(1)), props_vec(point(2)),
                props_vec(point(3)), basis);
        if (!ladder.valid) continue;

        DrawLadderGizmoGeometry(
            frame, origin, point(1), point(2), point(3),
            glm::vec3((float)ladder.bottomMount.x, (float)ladder.bottomMount.y,
                      (float)ladder.bottomMount.z),
            glm::vec3((float)ladder.topMount.x, (float)ladder.topMount.y,
                      (float)ladder.topMount.z),
            ladder.rungCount, ubo_mats, ubo_binding_point_);
    }
}

// METAL_DOOR_SLIDE_UP (model 506_xx) appears in levels 12/13/14 as an EditRigidObj
// carrying a genuine multi-axis Euler tuple. It bypasses the engine's special door
// transform and so needs a different Euler application order than other rigid objects.
void Renderer_Objects::DrawModelPreview(const std::string& modelId, GLuint ubo_mats,
                                        int vpX, int vpY, int vpW, int vpH,
                                        float rotX, float rotY) {
    if (!shader_program_ || modelId.empty() || vpW <= 0 || vpH <= 0) return;

    // Picker model IDs are almost all props; fall back to the building variant.
    const Mesh* loaded = &GetOrLoadMesh(modelId, false);
    if (loaded->subMeshes.empty() && loaded->vertexCount == 0)
        loaded = &GetOrLoadMesh(modelId, true);
    if (loaded->subMeshes.empty() && (loaded->VAO == 0 || loaded->vertexCount == 0)) return;
    const Mesh& mesh = *loaded;

    // Fit the model into a unit-ish sphere so any model frames the same.
    float maxExt = std::max(std::max(mesh.halfExtents.x, mesh.halfExtents.y),
                            std::max(mesh.halfExtents.z, 1.0f));
    float fit = 1.0f / maxExt;

    // Preview camera: IGI models are Z-up. View from -Y, slightly raised, at origin.
    glm::mat4 proj = glm::perspective(glm::radians(40.0f),
                                      (float)vpW / (float)vpH, 0.05f, 100.0f);
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, -3.2f, 1.2f),
                                 glm::vec3(0.0f, 0.0f, 0.0f),
                                 glm::vec3(0.0f, 0.0f, 1.0f));
    glm::mat4 mvp = proj * view;

    glm::mat4 model(1.0f);
    model = glm::rotate(model, rotY, glm::vec3(0.0f, 0.0f, 1.0f)); // horizontal spin (Z up)
    model = glm::rotate(model, rotX, glm::vec3(1.0f, 0.0f, 0.0f)); // vertical tumble
    model = glm::scale(model, glm::vec3(fit));
    model = glm::translate(model, -mesh.center);

    // Overwrite u_mvp (3rd mat4 in the shared UBO). The scene re-uploads the whole
    // UBO next frame, and the scene pass for this frame already finished, so this is safe.
    // GL_BufferSubData routes this into the emulated block on a 2.1 context.
    GL_BufferSubData(ubo_mats, GL_UNIFORM_BUFFER, 2 * sizeof(glm::mat4), sizeof(glm::mat4),
                     glm::value_ptr(mvp));

    // Isolated viewport with its own cleared depth so the preview never z-fights HUD.
    glViewport(vpX, vpY, vpW, vpH);
    glEnable(GL_SCISSOR_TEST);
    glScissor(vpX, vpY, vpW, vpH);
    glClear(GL_DEPTH_BUFFER_BIT);

    glUseProgram(shader_program_);
    glBindBufferBase(GL_UNIFORM_BUFFER, ubo_binding_point_, ubo_mats);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    GLint loc_model     = glGetUniformLocation(shader_program_, "u_model");
    GLint loc_dirlight  = glGetUniformLocation(shader_program_, "u_dirlight");
    GLint loc_ambient   = glGetUniformLocation(shader_program_, "u_ambient");
    GLint loc_useTex    = glGetUniformLocation(shader_program_, "u_useTexture");
    GLint loc_tex       = glGetUniformLocation(shader_program_, "u_texture");
    GLint loc_alpha     = glGetUniformLocation(shader_program_, "u_alpha");
    GLint loc_glass     = glGetUniformLocation(shader_program_, "u_glassMin");
    GLint loc_baseColor = glGetUniformLocation(shader_program_, "u_baseColor");
    glUniformMatrix4fv(loc_model, 1, GL_FALSE, glm::value_ptr(model));
    glUniform1f(loc_alpha, 1.0f);
    if (loc_glass >= 0)     glUniform1f(loc_glass, 0.0f);
    if (loc_baseColor >= 0) glUniform4f(loc_baseColor, 1.0f, 1.0f, 1.0f, 1.0f);

    // Reset uniforms that the scene pass may have left in a non-default state.
    // Without this, u_useLightmap=1 (set by a building draw) causes the preview
    // shader to sample texture unit 1 (stale lightmap) instead of the diffuse
    // texture, producing gray or black output regardless of texture content.
    {
        GLint loc_useLightmap = glGetUniformLocation(shader_program_, "u_useLightmap");
        GLint loc_tint        = glGetUniformLocation(shader_program_, "u_tint");
        GLint loc_gamma       = glGetUniformLocation(shader_program_, "u_gamma");
        GLint loc_lightDir    = glGetUniformLocation(shader_program_, "u_lightDir");
        GLint loc_fogFar      = glGetUniformLocation(shader_program_, "u_fogFar");
        GLint loc_fogColor    = glGetUniformLocation(shader_program_, "u_fogColor");
        GLint loc_camPos      = glGetUniformLocation(shader_program_, "u_cameraPos");
        if (loc_useLightmap >= 0) glUniform1i(loc_useLightmap, 0);
        if (loc_tint >= 0)        glUniform3f(loc_tint, 1.0f, 1.0f, 1.0f);
        if (loc_gamma >= 0)       glUniform1f(loc_gamma, 1.0f);
        if (loc_lightDir >= 0)    glUniform3f(loc_lightDir, 0.5f, 1.0f, 0.8f);
        if (loc_fogFar >= 0)      glUniform1f(loc_fogFar, 1e9f);
        if (loc_fogColor >= 0)    glUniform3f(loc_fogColor, 0.0f, 0.0f, 0.0f);
        if (loc_camPos >= 0)      glUniform3f(loc_camPos, 0.0f, -3.2f, 1.2f);
    }

    // Hash-based fallback color for untextured submeshes (matches the main draw).
    float r = 0.6f, g = 0.6f, b = 0.6f;
    {
        size_t h = std::hash<std::string>{}(modelId);
        r = 0.4f + (float)(h & 0xFF) / 255.0f * 0.4f;
        g = 0.4f + (float)((h >> 8) & 0xFF) / 255.0f * 0.4f;
        b = 0.4f + (float)((h >> 16) & 0xFF) / 255.0f * 0.4f;
    }

    auto drawSub = [&](GLuint vao, int vcount, GLuint texId, const glm::vec4& baseColor) {
        if (vao == 0 || vcount == 0) return;
        if (texId > 0) {
            glUniform3f(loc_dirlight, 0.7f, 0.7f, 0.7f);
            glUniform3f(loc_ambient,  0.45f, 0.45f, 0.45f);
            glUniform1i(loc_useTex, 1);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texId);
            glUniform1i(loc_tex, 0);
        } else {
            glm::vec3 c(baseColor.r, baseColor.g, baseColor.b);
            if (c.r >= 0.99f && c.g >= 0.99f && c.b >= 0.99f) c = glm::vec3(r, g, b);
            glUniform3f(loc_dirlight, c.r * 0.7f, c.g * 0.7f, c.b * 0.7f);
            glUniform3f(loc_ambient,  c.r * 0.45f, c.g * 0.45f, c.b * 0.45f);
            glUniform1i(loc_useTex, 0);
        }
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, vcount);
    };

    if (!mesh.subMeshes.empty()) {
        for (const auto& sub : mesh.subMeshes)
            drawSub(sub.VAO, sub.vertexCount, sub.textureID, sub.baseColorFactor);
    } else {
        drawSub(mesh.VAO, mesh.vertexCount, mesh.textureID, glm::vec4(1.0f));
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    for (int i = 0; i < 4; ++i) glDisableVertexAttribArray(static_cast<GLuint>(i));
    glDisable(GL_SCISSOR_TEST);
    // Caller restores full viewport and 2D HUD state.
}

// ─── DrawAttachedMesh ────────────────────────────────────────────────────────
// Draws one static prop mesh at an arbitrary world matrix, attached to a live
// bone (e.g. a weapon held in an AI's hand). Modeled on DrawModelPreview's
// shader-bind/uniform/per-submesh draw loop, but runs inside the NORMAL scene
// pass — it does not touch the viewport or override the shared MVP (the scene's
// own camera matrices, already bound this frame, are used as-is).
void Renderer_Objects::DrawAttachedMesh(const std::string& modelId, bool isBuilding, const glm::mat4& worldMat, GLuint ubo_mats) {
    if (!shader_program_ || modelId.empty()) return;

    const Mesh* loaded = &GetOrLoadMesh(modelId, isBuilding);
    if (loaded->subMeshes.empty() && (loaded->VAO == 0 || loaded->vertexCount == 0))
        loaded = &GetOrLoadMesh(modelId, !isBuilding);
    if (loaded->subMeshes.empty() && (loaded->VAO == 0 || loaded->vertexCount == 0)) return;
    const Mesh& mesh = *loaded;

    glUseProgram(shader_program_);
    // Bind the shared Matrices UBO so the vertex shader's u_mvp (Proj*View*GlobalScale)
    // is the live scene camera. Without this the weapon has no camera transform and
    // renders off-screen — the bug that made attached weapons invisible. The scene's
    // matrices are still resident here (DrawSkinnedMesh uses fixed-function and never
    // touches the UBO), so we bind without re-uploading anything.
    glBindBufferBase(GL_UNIFORM_BUFFER, ubo_binding_point_, ubo_mats);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    GLint loc_model     = glGetUniformLocation(shader_program_, "u_model");
    GLint loc_dirlight  = glGetUniformLocation(shader_program_, "u_dirlight");
    GLint loc_ambient   = glGetUniformLocation(shader_program_, "u_ambient");
    GLint loc_useTex    = glGetUniformLocation(shader_program_, "u_useTexture");
    GLint loc_tex       = glGetUniformLocation(shader_program_, "u_texture");
    GLint loc_alpha     = glGetUniformLocation(shader_program_, "u_alpha");
    GLint loc_glass     = glGetUniformLocation(shader_program_, "u_glassMin");
    GLint loc_baseColor = glGetUniformLocation(shader_program_, "u_baseColor");
    GLint loc_tint      = glGetUniformLocation(shader_program_, "u_tint");
    glUniformMatrix4fv(loc_model, 1, GL_FALSE, glm::value_ptr(worldMat));
    glUniform1f(loc_alpha, 1.0f);
    if (loc_glass >= 0)     glUniform1f(loc_glass, 0.0f);
    if (loc_baseColor >= 0) glUniform4f(loc_baseColor, 1.0f, 1.0f, 1.0f, 1.0f);
    if (loc_tint >= 0)      glUniform3f(loc_tint, 1.0f, 1.0f, 1.0f); // no magenta leak from missing-in-res tinting

    float r = 0.6f, g = 0.6f, b = 0.6f;
    {
        size_t h = std::hash<std::string>{}(modelId);
        r = 0.4f + (float)(h & 0xFF) / 255.0f * 0.4f;
        g = 0.4f + (float)((h >> 8) & 0xFF) / 255.0f * 0.4f;
        b = 0.4f + (float)((h >> 16) & 0xFF) / 255.0f * 0.4f;
    }

    auto drawSub = [&](GLuint vao, int vcount, GLuint texId, const glm::vec4& baseColor) {
        if (vao == 0 || vcount == 0) return;
        if (texId > 0) {
            glUniform3f(loc_dirlight, 0.7f, 0.7f, 0.7f);
            glUniform3f(loc_ambient,  0.45f, 0.45f, 0.45f);
            glUniform1i(loc_useTex, 1);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texId);
            glUniform1i(loc_tex, 0);
        } else {
            glm::vec3 c(baseColor.r, baseColor.g, baseColor.b);
            if (c.r >= 0.99f && c.g >= 0.99f && c.b >= 0.99f) c = glm::vec3(r, g, b);
            glUniform3f(loc_dirlight, c.r * 0.7f, c.g * 0.7f, c.b * 0.7f);
            glUniform3f(loc_ambient,  c.r * 0.45f, c.g * 0.45f, c.b * 0.45f);
            glUniform1i(loc_useTex, 0);
        }
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, vcount);
    };

    if (!mesh.subMeshes.empty()) {
        for (const auto& sub : mesh.subMeshes)
            drawSub(sub.VAO, sub.vertexCount, sub.textureID, sub.baseColorFactor);
    } else {
        drawSub(mesh.VAO, mesh.vertexCount, mesh.textureID, glm::vec4(1.0f));
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

// ─── LoadAttachmentsRecursive ────────────────────────────────────────────────
// Recursively scans the ATTA section of modelId's MEF file, caches each
// sub-model mesh AND its own attachment records, then recurses into children.
// The visited set prevents infinite loops on circular ATTA references.
Mesh Renderer_Objects::CreateTextMesh(const std::string& text) {
    Mesh m;
    std::vector<float> vertices;
    
    // Simple stroke font - each character defined as line segments
    // Scale factor for text - reduced to be less intrusive
    const float scale = 20.0f;
    const float charSpacing = 25.0f;
    const float startX = -((text.size() * charSpacing) / 2.0f);
    
    float cursorX = startX;
    
    for (char c : text) {
        AddCharacterVertices(vertices, c, cursorX, 0.0f, scale);
        cursorX += charSpacing;
    }
    
    if (vertices.empty()) {
        // Fallback to a simple cross if text generation fails
        vertices = {
            -50.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
             50.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
             0.0f, -50.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
             0.0f,  50.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f
        };
    }
    
    glGenVertexArrays(1, &m.VAO);
    glGenBuffers(1, &m.VBO);
    glBindVertexArray(m.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m.VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
    
    // Position (xyz)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // Normal (xyz) - use as color multiplier
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)12);
    glEnableVertexAttribArray(1);
    // TexCoord (uv) - unused
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)24);
    glEnableVertexAttribArray(2);
    
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    m.vertexCount = vertices.size() / 8;
    m.textureID = 0;
    m.vertexData = nullptr;
    return m;
}

void Renderer_Objects::AddCharacterVertices(std::vector<float>& vertices, char c, float x, float y, float scale) {
    // Simple stroke definitions for common characters - using triangle strips for each line
    // Each line segment becomes a thin quad (2 triangles)
    auto addLineQuad = [&](float x1, float y1, float x2, float y2) {
        float thickness = 0.1f;
        float nx = 1.0f, ny = 0.0f, nz = 0.0f; // Red color
        
        // Calculate perpendicular offset for thickness
        float dx = x2 - x1;
        float dy = y2 - y1;
        float len = sqrt(dx*dx + dy*dy);
        if (len < 0.0001f) return;
        float ox = -dy / len * thickness;
        float oy = dx / len * thickness;
        
        // Quad vertices (2 triangles)
        // Triangle 1
        vertices.push_back(x + x1 * scale + ox); vertices.push_back(y + y1 * scale + oy); vertices.push_back(0.0f);
        vertices.push_back(nx); vertices.push_back(ny); vertices.push_back(nz);
        vertices.push_back(0.0f); vertices.push_back(0.0f);
        
        vertices.push_back(x + x2 * scale + ox); vertices.push_back(y + y2 * scale + oy); vertices.push_back(0.0f);
        vertices.push_back(nx); vertices.push_back(ny); vertices.push_back(nz);
        vertices.push_back(0.0f); vertices.push_back(0.0f);
        
        vertices.push_back(x + x1 * scale - ox); vertices.push_back(y + y1 * scale - oy); vertices.push_back(0.0f);
        vertices.push_back(nx); vertices.push_back(ny); vertices.push_back(nz);
        vertices.push_back(0.0f); vertices.push_back(0.0f);
        
        // Triangle 2
        vertices.push_back(x + x1 * scale - ox); vertices.push_back(y + y1 * scale - oy); vertices.push_back(0.0f);
        vertices.push_back(nx); vertices.push_back(ny); vertices.push_back(nz);
        vertices.push_back(0.0f); vertices.push_back(0.0f);
        
        vertices.push_back(x + x2 * scale + ox); vertices.push_back(y + y2 * scale + oy); vertices.push_back(0.0f);
        vertices.push_back(nx); vertices.push_back(ny); vertices.push_back(nz);
        vertices.push_back(0.0f); vertices.push_back(0.0f);
        
        vertices.push_back(x + x2 * scale - ox); vertices.push_back(y + y2 * scale - oy); vertices.push_back(0.0f);
        vertices.push_back(nx); vertices.push_back(ny); vertices.push_back(nz);
        vertices.push_back(0.0f); vertices.push_back(0.0f);
    };
    
    c = toupper(c);
    
    switch (c) {
        case 'M':
            addLineQuad(0, 1, 0, -1);
            addLineQuad(0, -1, 0.5, 0);
            addLineQuad(0.5, 0, 1, -1);
            addLineQuad(1, -1, 1, 1);
            break;
        case 'I':
            addLineQuad(0, 1, 0, -1);
            addLineQuad(-0.3, 1, 0.3, 1);
            addLineQuad(-0.3, -1, 0.3, -1);
            break;
        case 'S':
            addLineQuad(0.3, 1, 0, 1);
            addLineQuad(0, 1, 0, 0);
            addLineQuad(0, 0, 1, 0);
            addLineQuad(1, 0, 1, -1);
            addLineQuad(1, -1, 0.7, -1);
            break;
        case 'N':
            addLineQuad(0, -1, 0, 1);
            addLineQuad(0, 1, 1, -1);
            addLineQuad(1, -1, 1, 1);
            break;
        case 'G':
            addLineQuad(0.7, 1, 0, 1);
            addLineQuad(0, 1, 0, -1);
            addLineQuad(0, -1, 0.7, -1);
            addLineQuad(0.7, -1, 1, -1);
            addLineQuad(1, -1, 1, 0);
            addLineQuad(1, 0, 0.7, 0);
            break;
        case ':':
            addLineQuad(-0.1f, 0.3f, 0.1f, 0.3f);
            addLineQuad(-0.1f, -0.3f, 0.1f, -0.3f);
            break;
        case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
            // Simple box for numbers with slash
            addLineQuad(0, 1, 0, -1);
            addLineQuad(0, -1, 1, -1);
            addLineQuad(1, -1, 1, 1);
            addLineQuad(1, 1, 0, 1);
            addLineQuad(0.2, -0.8, 0.8, -0.2);
            break;
        case '_':
            addLineQuad(0, -1, 1, -1);
            break;
        default:
            // Default to a box for unknown characters
            addLineQuad(0, 1, 0, -1);
            addLineQuad(0, -1, 1, -1);
            addLineQuad(1, -1, 1, 1);
            addLineQuad(1, 1, 0, 1);
            break;
    }
}

Mesh Renderer_Objects::CreateCubeMesh() {
    Mesh m;
    std::vector<float> v = {
        -500.0f,-500.0f,-500.0f, 0,0,-1, 0,0,  500.0f, 500.0f,-500.0f, 0,0,-1, 1,1,  500.0f,-500.0f,-500.0f, 0,0,-1, 1,0,
        -500.0f,-500.0f,-500.0f, 0,0,-1, 0,0, -500.0f, 500.0f,-500.0f, 0,0,-1, 0,1,  500.0f, 500.0f,-500.0f, 0,0,-1, 1,1,
        -500.0f,-500.0f, 500.0f, 0,0, 1, 0,0,  500.0f,-500.0f, 500.0f, 0,0, 1, 1,0,  500.0f, 500.0f, 500.0f, 0,0, 1, 1,1,
        -500.0f,-500.0f, 500.0f, 0,0, 1, 0,0,  500.0f, 500.0f, 500.0f, 0,0, 1, 1,1, -500.0f, 500.0f, 500.0f, 0,0, 1, 0,1,
        -500.0f, 500.0f, 500.0f,-1,0, 0, 1,0, -500.0f, 500.0f,-500.0f,-1,0, 0, 1,1, -500.0f,-500.0f,-500.0f,-1,0, 0, 0,1,
        -500.0f, 500.0f, 500.0f,-1,0, 0, 1,0, -500.0f,-500.0f,-500.0f,-1,0, 0, 0,1, -500.0f,-500.0f, 500.0f,-1,0, 0, 0,0,
         500.0f, 500.0f, 500.0f, 1,0, 0, 1,0,  500.0f,-500.0f,-500.0f, 1,0, 0, 0,1,  500.0f, 500.0f,-500.0f, 1,0, 0, 1,1,
         500.0f, 500.0f, 500.0f, 1,0, 0, 1,0,  500.0f,-500.0f, 500.0f, 1,0, 0, 0,0,  500.0f,-500.0f,-500.0f, 1,0, 0, 0,1,
        -500.0f,-500.0f,-500.0f, 0,-1,0, 0,1,  500.0f,-500.0f,-500.0f, 0,-1,0, 1,1,  500.0f,-500.0f, 500.0f, 0,-1,0, 1,0,
        -500.0f,-500.0f,-500.0f, 0,-1,0, 0,1,  500.0f,-500.0f, 500.0f, 0,-1,0, 1,0, -500.0f,-500.0f, 500.0f, 0,-1,0, 0,0,
        -500.0f, 500.0f,-500.0f, 0, 1,0, 0,1,  500.0f, 500.0f, 500.0f, 0, 1,0, 1,0,  500.0f, 500.0f,-500.0f, 0, 1,0, 1,1,
        -500.0f, 500.0f,-500.0f, 0, 1,0, 0,1, -500.0f, 500.0f, 500.0f, 0, 1,0, 0,0,  500.0f, 500.0f, 500.0f, 0, 1,0, 1,0
    };
    glGenVertexArrays(1, &m.VAO); glGenBuffers(1, &m.VBO);
    glBindVertexArray(m.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m.VBO);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (void*)0); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, (void*)12); glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, (void*)24); glEnableVertexAttribArray(2);
    m.vertexCount = 36; m.textureID = 0; m.vertexData = nullptr;
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0); return m;
}

void Renderer_Objects::InitSelectionBox() {
    float h = WORLD_UNITS_PER_METER; // half-size of 2m box
    // 8 corners
    float c[8][3] = {
        {-h,-h, h},{h,-h, h},{h, h, h},{-h, h, h}, // front 0-3
        {-h,-h,-h},{h,-h,-h},{h, h,-h},{-h, h,-h}  // back  4-7
    };
    // 12 edges pre-expanded as 24 line endpoints for glDrawArrays(GL_LINES)
    int edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0}, // front face
        {4,5},{5,6},{6,7},{7,4}, // back face
        {0,4},{1,5},{2,6},{3,7}  // connecting edges
    };
    float verts[24 * 3];
    for (int i = 0; i < 12; ++i) {
        float* a = c[edges[i][0]]; float* b = c[edges[i][1]];
        verts[i*6+0]=a[0]; verts[i*6+1]=a[1]; verts[i*6+2]=a[2];
        verts[i*6+3]=b[0]; verts[i*6+4]=b[1]; verts[i*6+5]=b[2];
    }
    glGenVertexArrays(1, &selection_vao_);
    glGenBuffers(1, &selection_vbo_);
    glBindVertexArray(selection_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, selection_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

// ─── DrawSelectionBox ─────────────────────────────────────────────────────────
void Renderer_Objects::DrawSelectionBox(const LevelObject& obj, GLuint ubo_mats, const glm::vec4& color) {
    if (selection_vao_ == 0)
        InitSelectionBox();

    if (selection_shader_ == 0) {
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
uniform vec4 u_color;
out vec4 fragColor;
void main() {
    fragColor = u_color;
}
)";
        // GLSL 1.20 variants (legacy 2.1 context): no layout(), no uniform
        // blocks.  The Matrices block's third matrix is the plain uniform
        // `u_mvp` that gl_helper.cpp's UBO emulation uploads; u_unused1 and
        // u_unused2 are never read.  Attribute indices are bound by
        // GL_LegacyBindInlineAttribLocations() before glLinkProgram.
        static const char* simple_vert_120 = R"(
#version 120
uniform mat4 u_mvp;
uniform mat4 u_model;
attribute vec3 a_pos;
void main() {
    gl_Position = u_mvp * u_model * vec4(a_pos, 1.0);
}
)";
        static const char* simple_frag_120 = R"(
#version 120
uniform vec4 u_color;
void main() {
    gl_FragColor = u_color;
}
)";
        const char* simple_vert_src = g_gl_info.legacy_gl21_ ? simple_vert_120 : simple_vert;
        const char* simple_frag_src = g_gl_info.legacy_gl21_ ? simple_frag_120 : simple_frag;
        GLint ok;
        GLuint vert = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vert, 1, &simple_vert_src, nullptr);
        glCompileShader(vert);
        glGetShaderiv(vert, GL_COMPILE_STATUS, &ok);
        if (!ok) { glDeleteShader(vert); return; }

        GLuint frag = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(frag, 1, &simple_frag_src, nullptr);
        glCompileShader(frag);
        glGetShaderiv(frag, GL_COMPILE_STATUS, &ok);
        if (!ok) { glDeleteShader(vert); glDeleteShader(frag); return; }

        selection_shader_ = glCreateProgram();
        glAttachShader(selection_shader_, vert);
        glAttachShader(selection_shader_, frag);
        if (g_gl_info.legacy_gl21_) {
            GL_LegacyBindInlineAttribLocations(selection_shader_);
        }
        glLinkProgram(selection_shader_);
        glDeleteShader(vert);
        glDeleteShader(frag);
        glGetProgramiv(selection_shader_, GL_LINK_STATUS, &ok);
        if (!ok) { glDeleteProgram(selection_shader_); selection_shader_ = 0; return; }
    }

    glUseProgram(selection_shader_);
    glBindBufferBase(GL_UNIFORM_BUFFER, ubo_binding_point_, ubo_mats);

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(obj.pos));
    // Authored IGI rotation: Rz * Ry * Rx, like the game (renderer/igi_transform.h).
    model = model * igi::IgiEulerRotationMatrix(obj.rot);

    bool isWeapon = IsWeaponModel(obj.modelId) || obj.type == "GunPickup" || obj.type == "AmmoPickup" || obj.type == "GenericPickup";
    if (isWeapon)
        model = glm::rotate(model, glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));

    model = glm::scale(model, glm::vec3(obj.scale * 1.2f));

    glUniformMatrix4fv(glGetUniformLocation(selection_shader_, "u_model"), 1, GL_FALSE, glm::value_ptr(model));
    glUniform4fv(glGetUniformLocation(selection_shader_, "u_color"), 1, glm::value_ptr(color));

    glBindVertexArray(selection_vao_);
    glDisable(GL_CULL_FACE);
    glDrawArrays(GL_LINES, 0, 24);
    glEnable(GL_CULL_FACE);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
}

// ─── DrawPropsGizmos ─────────────────────────────────────────────────────────
// Viewport previews for the interactable props of GitHub issue #42.
//
// Deliberately a free function rather than a Renderer_Objects member: the
// renderer's own header is outside this change's write scope, so the entry point
// is declared in level_objects.h (which this TU already includes through
// renderer_objects_internal.h). It is otherwise self-contained — it owns its
// shader/VAO/VBO in function-local statics and takes the Matrices UBO handles
// from the caller — so it never reads or writes Renderer_Objects state.
//
// Everything drawn here is traceable to IGI-RE; the only presentation choices are
// the line lengths/radii noted inline (the game has no gizmo, so it defines no
// such constant).
namespace {

struct GzVert { float x, y, z; };

// A small unlit line-list batch builder.
struct GzBatch {
    std::vector<GzVert> v;
    void line(const glm::vec3& p, const glm::vec3& q) {
        v.push_back({p.x, p.y, p.z});
        v.push_back({q.x, q.y, q.z});
    }
    size_t count() const { return v.size(); }
};

// Lazily built shader + dynamic line VBO. GL state is process-global and this
// editor uses a single context, so file-local statics are safe here.
GLuint EnsureGizmoShader() {
    static GLuint prog = 0;
    static bool attempted = false;
    if (attempted) return prog;   // never retry a failed compile per object/frame
    attempted = true;
    static const char* vs = R"(
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
    static const char* fs = R"(
#version 330 core
uniform vec4 u_color;
out vec4 fragColor;
void main() {
    fragColor = u_color;
}
)";
    // GLSL 1.20 variants (legacy 2.1 context): no layout(), no uniform blocks.
    // See the selection-box pair above for the u_mvp / Matrices rationale.
    static const char* vs_120 = R"(
#version 120
uniform mat4 u_mvp;
uniform mat4 u_model;
attribute vec3 a_pos;
void main() {
    gl_Position = u_mvp * u_model * vec4(a_pos, 1.0);
}
)";
    static const char* fs_120 = R"(
#version 120
uniform vec4 u_color;
void main() {
    gl_FragColor = u_color;
}
)";
    const char* vs_src = g_gl_info.legacy_gl21_ ? vs_120 : vs;
    const char* fs_src = g_gl_info.legacy_gl21_ ? fs_120 : fs;
    GLint ok = 0;
    GLuint vert = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vert, 1, &vs_src, nullptr);
    glCompileShader(vert);
    glGetShaderiv(vert, GL_COMPILE_STATUS, &ok);
    if (!ok) { glDeleteShader(vert); return 0; }

    GLuint frag = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(frag, 1, &fs_src, nullptr);
    glCompileShader(frag);
    glGetShaderiv(frag, GL_COMPILE_STATUS, &ok);
    if (!ok) { glDeleteShader(vert); glDeleteShader(frag); return 0; }

    GLuint p = glCreateProgram();
    glAttachShader(p, vert);
    glAttachShader(p, frag);
    if (g_gl_info.legacy_gl21_) {
        GL_LegacyBindInlineAttribLocations(p);
    }
    glLinkProgram(p);
    glDeleteShader(vert);
    glDeleteShader(frag);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { glDeleteProgram(p); return 0; }
    prog = p;
    return prog;
}

// Flush a batch. `geometryEnd` splits it into two coloured passes (span/anchors
// first, then the derived ride/aim preview) — 0 means "one colour".
void FlushGizmo(const GzBatch& b, GLuint prog, GLuint ubo_mats, GLuint ubo_binding,
                size_t geometryEnd, const glm::vec4& c0, const glm::vec4& c1) {
    static GLuint vao = 0, vbo = 0;
    if (vao == 0) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GzVert), (void*)0);
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    // Save the caller's program.  This helper is called from inside
    // Renderer_Objects::Draw's per-object loop, which has its object shader
    // bound; ending on glUseProgram(0) unconditionally would leave the rest of
    // that loop with no program, so every later glUniform* raised
    // GL_INVALID_OPERATION and the objects drew through the fixed-function
    // pipeline instead of their shader.  Restoring the previous program is
    // still correct when the caller really was in fixed-function state, because
    // then the saved value is 0.
    GLint prev_prog = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prev_prog);

    glUseProgram(prog);
    const GLuint blk = glGetUniformBlockIndex(prog, "Matrices");
    if (blk != GL_INVALID_INDEX) glUniformBlockBinding(prog, blk, ubo_binding);
    glBindBufferBase(GL_UNIFORM_BUFFER, ubo_binding, ubo_mats);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(b.v.size() * sizeof(GzVert)), b.v.data(), GL_STREAM_DRAW);

    const glm::mat4 ident(1.0f);
    glUniformMatrix4fv(glGetUniformLocation(prog, "u_model"), 1, GL_FALSE, glm::value_ptr(ident));

    const GLboolean cull_was_enabled = glIsEnabled(GL_CULL_FACE);
    if (cull_was_enabled) glDisable(GL_CULL_FACE);
    // Default line width only: glLineWidth > 1 is not portable (core profile
    // allows just 1.0), which is why the wire gizmo is a line list rather than a
    // thick quad strip. See docs/igi-re-parity/PROPS_WIRE_INTERACTABLES.md.
    if (geometryEnd == 0) {
        glUniform4fv(glGetUniformLocation(prog, "u_color"), 1, glm::value_ptr(c0));
        glDrawArrays(GL_LINES, 0, (GLsizei)b.count());
    } else {
        glUniform4fv(glGetUniformLocation(prog, "u_color"), 1, glm::value_ptr(c0));
        glDrawArrays(GL_LINES, 0, (GLsizei)geometryEnd);
        if (b.count() > geometryEnd) {
            glUniform4fv(glGetUniformLocation(prog, "u_color"), 1, glm::value_ptr(c1));
            glDrawArrays(GL_LINES, (GLint)geometryEnd, (GLsizei)(b.count() - geometryEnd));
        }
    }
    glEnable(GL_CULL_FACE);
    if (!cull_was_enabled) glDisable(GL_CULL_FACE);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram((GLuint)prev_prog);
}

// Draw a planar arc (fan outline) around `origin`, in the plane spanned by the
// unit vectors `u`/`v`, sweeping `a0`..`a1` radians, at `radius`.
void Arc(GzBatch& b, const glm::vec3& origin, const glm::vec3& u, const glm::vec3& v,
         float a0, float a1, float radius, int segments) {
    glm::vec3 prev = origin + (u * std::cos(a0) + v * std::sin(a0)) * radius;
    for (int i = 1; i <= segments; ++i) {
        const float a = a0 + (a1 - a0) * (float)i / (float)segments;
        const glm::vec3 p = origin + (u * std::cos(a) + v * std::sin(a)) * radius;
        b.line(prev, p);
        prev = p;
    }
}

// Display-only reference length for the gun gizmos. The game defines no such
// constant: its fire/detection geometry comes from the weapon model's muzzle
// point (StationaryGun_Aim 0x474420 via Model_GetPoint, src/statgun/statgun.c:249).
constexpr float kGizmoRefLen = 4096.0f;   // 1 metre at 4096 units/m (wire.c:320)

}  // namespace

void DrawPropsGizmos(const LevelObject& obj, GLuint ubo_mats, GLuint ubo_binding) {
    const GLuint prog = EnsureGizmoShader();
    if (prog == 0) return;

    const glm::vec3 origin((float)obj.pos.x, (float)obj.pos.y, (float)obj.pos.z);
    // Authored IGI rotation: Rz * Ry * Rx (IGI-RE src/math/matrix.c:99-110
    // Matrix_SetEuler, HOOK 0x4B38E0; the QSC Real32x9 block is parsed as three
    // Euler angles by ParamReal32x9_Parse, HOOK 0x4EFBA0).
    const glm::mat3 frame = glm::mat3(igi::IgiEulerRotationMatrix(obj.rot));
    const glm::vec3 ax = frame[0], ay = frame[1], az = frame[2];

    // ── Wire / zipline ───────────────────────────────────────────────────────
    if (obj.isWire) {
        glm::dvec3 a, e;
        if (!obj.WireAnchors(a, e)) return;
        const TaskSchemaNS::Props::WireSpan span = TaskSchemaNS::Props::ComputeWireSpan(
            TaskSchemaNS::Props::Vec3(a.x, a.y, a.z), TaskSchemaNS::Props::Vec3(e.x, e.y, e.z));
        if (!span.valid) return;

        const glm::vec3 A((float)a.x, (float)a.y, (float)a.z);
        const glm::vec3 B((float)e.x, (float)e.y, (float)e.z);
        const glm::vec3 dir((float)span.dir.x, (float)span.dir.y, (float)span.dir.z);
        const float len = (float)span.length;

        // Anchor cross arms: 1/16 of the span, clamped so both a 1 m and a 100 m
        // wire show a usable marker (presentation choice).
        const float arm = glm::clamp(len * 0.0625f, 32.0f, 1024.0f);
        glm::vec3 ref(0.0f, 0.0f, 1.0f);
        if (std::fabs(glm::dot(ref, dir)) > 0.99f) ref = glm::vec3(1.0f, 0.0f, 0.0f);
        const glm::vec3 side = glm::normalize(glm::cross(dir, ref));
        const glm::vec3 norm = glm::normalize(glm::cross(side, dir));

        GzBatch b;
        for (const glm::vec3& c : {A, B}) {
            b.line(c - side * arm, c + side * arm);
            b.line(c - norm * arm, c + norm * arm);
            b.line(c - dir * arm, c + dir * arm);
        }
        b.line(A, B);   // the span Wire_ParamsChanged builds (wire.c:160-191)
        const size_t geometry = b.count();

        // A sloped Wire is a zipline: Wire_IsSloped (0x43F8A0, wire.c:304-317) is
        // what HumanPlayer_StartAction case 9 tests to choose ziplineEnter
        // (IGI-RE src/humanstate/humanstate.c:958-963).
        if (span.isZipline) {
            const TaskSchemaNS::Props::Vec3 rd = TaskSchemaNS::Props::ZiplineRideDirection(
                TaskSchemaNS::Props::Vec3(a.x, a.y, a.z), TaskSchemaNS::Props::Vec3(e.x, e.y, e.z));
            glm::vec3 d((float)rd.x, (float)rd.y, (float)rd.z);
            if (glm::length(d) > 0.0f) {
                d = glm::normalize(d);
                // The first kWireEnd (0.75 m) of an anchor is not rideable and the
                // rider hangs kZiplineDown (0.9 m) below the wire
                // (IGI-RE src/weapon/humanplayer.c:136-138).
                const glm::vec3 enter = A + d * (float)TaskSchemaNS::Props::kWireEnd;
                b.line(enter, enter - glm::vec3(0.0f, 0.0f, (float)TaskSchemaNS::Props::kZiplineDown));
                // Downhill ride arrow (HumanPlayer_UpdateZipline flips d when
                // d.z > 0, humanplayer.c:562-567).
                const glm::vec3 mid = (A + B) * 0.5f;
                const float shaft = std::min(len * 0.25f, 2048.0f);
                const glm::vec3 tip = mid + d * shaft;
                glm::vec3 pu(0.0f, 0.0f, 1.0f);
                if (std::fabs(glm::dot(pu, d)) > 0.99f) pu = glm::vec3(1.0f, 0.0f, 0.0f);
                const glm::vec3 ps = glm::normalize(glm::cross(d, pu));
                const float head = shaft * 0.25f;
                b.line(mid, tip);
                b.line(tip, tip - d * head + ps * head * 0.5f);
                b.line(tip, tip - d * head - ps * head * 0.5f);
            }
        }
        FlushGizmo(b, prog, ubo_mats, ubo_binding, geometry,
                   glm::vec4(0.95f, 0.75f, 0.15f, 1.0f),   // wire + anchors
                   glm::vec4(0.30f, 0.85f, 1.00f, 1.0f));  // downhill ride preview
        return;
    }

    // ── StationaryGun (player-mannable machinegun) ────────────────────────────
    // basePos args 3-5, baseMatrix args 6-8, maxPitch@11, minPitch@12,
    // yawRange@13 (IGI-RE src/statgun/statgun.c:142-150).
    if (obj.type == "StationaryGun") {
        const TaskSchemaNS::Props::StationaryGunParams gp = obj.StatGunParams();
        if (!gp.valid) return;
        constexpr double kDeg = 0.017453292519943295;
        // Yaw is a rotation about the base's local Z: StationaryGun_Aim does
        // Matrix_SetRotZ(&o->obj.matrix, o->outYaw) (statgun.c:241), and
        // StationaryGun_Clamp clamps yaw to +/- yawRange while yawRange < 360 deg
        // (statgun.c:288-299).
        const float yaw = (float)(gp.yawRange * kDeg);
        // Pitch is the X-Euler of the base frame (StationaryGun_Aim composes
        // Matrix_SetEuler(&e, o->outPitch, 0, PI), statgun.c:255), clamped to
        // [minPitch, maxPitch] (statgun.c:275-286).
        const float pMin = (float)(gp.minPitch * kDeg);
        const float pMax = (float)(gp.maxPitch * kDeg);

        GzBatch b;
        const float arm = kGizmoRefLen * 0.25f;
        // Base frame triad.
        b.line(origin, origin + ax * arm);
        b.line(origin, origin + ay * arm);
        b.line(origin, origin + az * arm);
        const size_t geometry = b.count();

        // Yaw sweep in the base local XY plane (bounded by the yawRange clamp).
        if (yaw < glm::two_pi<float>()) {
            Arc(b, origin, ax, ay, -yaw, yaw, kGizmoRefLen, 24);
            b.line(origin, origin + (ax * std::cos(-yaw) + ay * std::sin(-yaw)) * kGizmoRefLen);
            b.line(origin, origin + (ax * std::cos(yaw) + ay * std::sin(yaw)) * kGizmoRefLen);
        } else {
            Arc(b, origin, ax, ay, 0.0f, glm::two_pi<float>(), kGizmoRefLen, 48);
        }
        // Pitch limits: the X-Euler rotates local +Y towards local +Z.
        b.line(origin, origin + (ay * std::cos(pMin) + az * std::sin(pMin)) * kGizmoRefLen);
        b.line(origin, origin + (ay * std::cos(pMax) + az * std::sin(pMax)) * kGizmoRefLen);

        FlushGizmo(b, prog, ubo_mats, ubo_binding, geometry,
                   glm::vec4(0.45f, 0.85f, 0.45f, 1.0f),   // base frame
                   glm::vec4(0.95f, 0.45f, 0.25f, 1.0f));  // yaw/pitch limits
        return;
    }

    // ── AIStationaryGunHolder ────────────────────────────────────────────────
    // GunHolder_Init maps the authored args straight onto the AI view cone:
    //   view.maxPitch = angleX * kDegToRad   (arg@10)
    //   view.maxYaw   = angleY * kDegToRad   (arg@11)
    //   view.range    = range  * kRangeScale (arg@12, kRangeScale = 4096 units/m)
    // (IGI-RE src/comprun/gunholder.c:228-230, table at gunholder.c:157-165).
    if (obj.type == "AIStationaryGunHolder") {
        const TaskSchemaNS::Props::GunHolderParams gp = obj.GunHolderParams();
        if (!gp.valid) return;
        constexpr double kDeg = 0.017453292519943295;
        const float maxPitch = (float)(gp.angleX * kDeg);
        const float maxYaw   = (float)(gp.angleY * kDeg);
        const float range    = (float)(gp.range * 4096.0);   // gunholder.c:102,230

        GzBatch b;
        const float arm = kGizmoRefLen * 0.25f;
        b.line(origin, origin + ax * arm);
        b.line(origin, origin + ay * arm);
        b.line(origin, origin + az * arm);
        const size_t geometry = b.count();

        const float yaw = std::min(maxYaw, glm::pi<float>());
        Arc(b, origin, ax, ay, -yaw, yaw, range, 32);
        b.line(origin, origin + (ax * std::cos(-yaw) + ay * std::sin(-yaw)) * range);
        b.line(origin, origin + (ax * std::cos(yaw) + ay * std::sin(yaw)) * range);
        const float pitch = std::min(maxPitch, glm::half_pi<float>());
        b.line(origin, origin + (ax * std::cos(pitch) + az * std::sin(pitch)) * range);
        b.line(origin, origin + (ax * std::cos(pitch) - az * std::sin(pitch)) * range);

        FlushGizmo(b, prog, ubo_mats, ubo_binding, geometry,
                   glm::vec4(0.45f, 0.85f, 0.45f, 1.0f),   // base frame
                   glm::vec4(0.85f, 0.45f, 0.95f, 1.0f));  // AI detection cone
        return;
    }
}

namespace igi {

namespace {
bool g_ladder_gizmos_enabled = true;
}  // namespace

void SetLadderGizmosEnabled(bool enabled) { g_ladder_gizmos_enabled = enabled; }
bool LadderGizmosEnabled() { return g_ladder_gizmos_enabled; }

}  // namespace igi

// ─── DrawLadderGizmoGeometry ─────────────────────────────────────────────────
// Viewport previews for the ladder contract of GitHub issue #68. The game draws
// nothing of the sort: every line below is geometry that Ladder_Use (HOOK
// 0x4404C0, IGI-RE src/props/ladder.c:122-175) and HumanPlayer_UpdateLadder
// (HOOK 0x4113E0, IGI-RE src/weapon/humanplayer.c:273-340) actually act on,
// resolved by TaskSchemaNS::Props::ComputeLadderPlacement (task_schema.h), which
// carries the per-constant citations. The caller passes world-space points: the
// activation reference, foot and head are magic points 1, 2 and 3.
//
// A file-local helper rather than a Renderer_Objects member: the renderer header
// is outside this change's write scope, so the entry point is the existing
// DrawMagicObjSpheres overlay (see the call site there). Like DrawPropsGizmos it
// draws only lines, because the game defines no filled volume — the line width
// stays at GL's default for the same reason.
static void DrawLadderGizmoGeometry(const glm::mat3& frame, const glm::vec3& origin,
                                    const glm::vec3& reference, const glm::vec3& foot,
                                    const glm::vec3& head, const glm::vec3& bottom_mount,
                                    const glm::vec3& top_mount, int rung_count,
                                    GLuint ubo_mats, GLuint ubo_binding) {
    const GLuint prog = EnsureGizmoShader();
    if (prog == 0) return;

    const glm::vec3 ax = frame[0], ay = frame[1];
    const float half_width =
        (float)TaskSchemaNS::Props::kLadderHalfWidthUnits;        // 2867.2
    const float depth =
        (float)TaskSchemaNS::Props::kLadderActivateRadiusUnits;   // 8192

    // ── Activation volume ────────────────────────────────────────────────────
    // { |localX| < 2867.2, |localY| < 8192, origin.z < worldZ < reference.z }
    // (IGI-RE src/props/ladder.c:133-138,157). The Z gate is a *world* Z test,
    // not a local one, so the four corners of the local rectangle are extruded
    // straight up and clipped to that slab.
    GzBatch volume;
    {
        const glm::vec3 corners[4] = {
            origin + ax * half_width + ay * depth,
            origin - ax * half_width + ay * depth,
            origin - ax * half_width - ay * depth,
            origin + ax * half_width - ay * depth,
        };
        const float z0 = origin.z;
        const float z1 = reference.z;
        for (int i = 0; i < 4; ++i) {
            const glm::vec3 lo(corners[i].x, corners[i].y, z0);
            const glm::vec3 hi(corners[i].x, corners[i].y, z1);
            const glm::vec3 n = corners[(i + 1) % 4];
            volume.line(lo, hi);
            volume.line(lo, glm::vec3(n.x, n.y, z0));
            volume.line(hi, glm::vec3(n.x, n.y, z1));
        }
    }

    // ── Climb line ───────────────────────────────────────────────────────────
    // The authored foot -> head axis, the mount-to-mount path the player
    // actually travels (IGI-RE src/weapon/humanplayer.c:304-314), and one tick
    // per rung at kLadderRungSpacingUnits (src/props/ladder.c:52,86-91).
    GzBatch climb;
    climb.line(foot, head);
    climb.line(bottom_mount, top_mount);
    {
        const glm::vec3 axis = head - foot;
        const int rungs = rung_count;
        const float tick = half_width * 0.25f;
        for (int i = 1; i < rungs; ++i) {
            const glm::vec3 p = foot + axis * ((float)i / (float)rungs);
            climb.line(p - ax * tick, p + ax * tick);
        }
    }

    // ── Markers ──────────────────────────────────────────────────────────────
    const float arm = kGizmoRefLen * 0.25f;
    auto cross = [&](GzBatch& b, const glm::vec3& p) {
        b.line(p - glm::vec3(arm, 0.0f, 0.0f), p + glm::vec3(arm, 0.0f, 0.0f));
        b.line(p - glm::vec3(0.0f, arm, 0.0f), p + glm::vec3(0.0f, arm, 0.0f));
        b.line(p - glm::vec3(0.0f, 0.0f, arm), p + glm::vec3(0.0f, 0.0f, arm));
    };
    GzBatch mounts;
    cross(mounts, bottom_mount);
    cross(mounts, top_mount);
    GzBatch points;
    cross(points, foot);
    cross(points, head);
    cross(points, reference);

    FlushGizmo(volume, prog, ubo_mats, ubo_binding, 0,
               glm::vec4(0.20f, 0.75f, 1.00f, 1.0f), glm::vec4(0.0f));
    FlushGizmo(climb, prog, ubo_mats, ubo_binding, 0,
               glm::vec4(0.35f, 1.00f, 0.45f, 1.0f), glm::vec4(0.0f));
    FlushGizmo(mounts, prog, ubo_mats, ubo_binding, 0,
               glm::vec4(1.00f, 0.35f, 0.35f, 1.0f), glm::vec4(0.0f));
    FlushGizmo(points, prog, ubo_mats, ubo_binding, 0,
               glm::vec4(1.00f, 0.85f, 0.20f, 1.0f), glm::vec4(0.0f));
}

// ─── DrawAttachmentsForSpline ─────────────────────────────────────────────────
// Public entry point called by Renderer_Splines once per SEGMENT INTERVAL.
// unscaledWorldMat = translate + rotate at the segment midpoint (no scale).
// leafScale.x = intervalLen (stretch ATTA to cover the full segment in X).
// leafScale.y/z = 40.96 (normal world scale for width/height).

