/******************************************************************************
 * @file    renderer_objects_picking.cpp
 * @brief   Renderer_Objects: color-id picking FBO + pick draw/readback
 *          Split from renderer_objects.cpp; shares renderer_objects_internal.h.
 *****************************************************************************/
#include "renderer_objects_internal.h"


void Renderer_Objects::InitPickingFBO(int w, int h) {
    // Delete existing resources
    if (pick_fbo_)       { glDeleteFramebuffers(1,  &pick_fbo_);       pick_fbo_ = 0; }
    if (pick_color_tex_) { glDeleteTextures(1,       &pick_color_tex_); pick_color_tex_ = 0; }
    if (pick_depth_rb_)  { glDeleteRenderbuffers(1,  &pick_depth_rb_);  pick_depth_rb_ = 0; }

    // Color texture (RGB8 — ID encoded as RGB)
    glGenTextures(1, &pick_color_tex_);
    glBindTexture(GL_TEXTURE_2D, pick_color_tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Depth renderbuffer
    glGenRenderbuffers(1, &pick_depth_rb_);
    glBindRenderbuffer(GL_RENDERBUFFER, pick_depth_rb_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    // Framebuffer
    glGenFramebuffers(1, &pick_fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, pick_fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pick_color_tex_, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, pick_depth_rb_);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        Logger::Get().Log(LogLevel::ERR, "[Renderer_Objects] Picking FBO incomplete: " + std::to_string(status));
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    pick_fbo_w_ = w;
    pick_fbo_h_ = h;
}

// ─── DrawAttachmentsForPicking ────────────────────────────────────────────────
// Draws ATTA sub-models of parentModelId with the parent's pick ID so that
// clicking anywhere on an ATTA part selects the owning LevelObject.
// Mirrors DrawAttachmentsRecursive's transform calculation; skips transparency,
// lighting, and texture setup since the picking shader only cares about geometry.
void Renderer_Objects::DrawAttachmentsForPicking(
    const std::string& parentModelId, bool isBuilding,
    const glm::mat4& parentWorldMat, float parentScale,
    GLint loc_model, GLint loc_id, int parentObjIndex,
    std::unordered_set<std::string>& drawn,
    int selected_object_index)
{
    const std::string prefix = isBuilding ? "building:" : "object:";
    const std::string attKey = std::to_string(current_level_) + ":" + prefix + parentModelId;
    auto ait = attachment_cache_.find(attKey);
    if (ait == attachment_cache_.end()) return;

    const auto& atts = ait->second;
    for (size_t ri = 0; ri < atts.size(); ++ri) {
        const auto& att = atts[ri];
        // Find the attachment mesh (same cache lookup as DrawAttachmentsRecursive)
        std::string subKey = std::to_string(current_level_) + ":" + prefix + att.modelId;
        auto sit = mesh_cache_.find(subKey);
        if (sit == mesh_cache_.end() || sit->second.vertexCount == 0) {
            subKey = std::to_string(current_level_) + ":object:" + att.modelId;
            sit = mesh_cache_.find(subKey);
        }
        if (sit == mesh_cache_.end()) continue;
        const Mesh& subMesh = sit->second;

        // Build ATTA world transform (identical logic to DrawAttachmentsRecursive)
        glm::mat4 attLocalRot(
            att.r[0], att.r[1], att.r[2], 0.f,
            att.r[3], att.r[4], att.r[5], 0.f,
            att.r[6], att.r[7], att.r[8], 0.f,
            0.f,      0.f,      0.f,      1.f
        );
        glm::vec3 localOff(att.px, att.py, att.pz);
        glm::vec3 worldPos = glm::vec3(parentWorldMat * glm::vec4(localOff, 1.f));
        glm::mat4 parentRot = parentWorldMat;
        parentRot[3] = glm::vec4(0.f, 0.f, 0.f, 1.f);
        glm::mat4 childWorldMat(1.0f);
        childWorldMat = glm::translate(childWorldMat, worldPos);
        childWorldMat = childWorldMat * parentRot * attLocalRot;

        // Recurse regardless of whether this node has pick-able geometry
        std::string childKey = parentModelId + ">" + att.modelId;
        bool recurse = drawn.insert(childKey).second;

        // Skip ATTAs already promoted (by world-pos key or by direct record index).
        bool occupied = IsAttaPromoted(att.modelId, worldPos) ||
            promoted_atta_records_.count(parentModelId + ":" + std::to_string(ri)) > 0;

        if (subMesh.vertexCount > 0 && !occupied) {
            // Record this ATTA as a uniquely-pickable, promotable entry.
            int entry = (int)atta_pick_entries_.size();
            AttaPickEntry e;
            e.parentObjIndex         = parentObjIndex;
            e.modelId                = att.modelId;
            e.immediateParentModelId = parentModelId;
            e.worldPos               = worldPos;
            e.worldRot               = glm::mat3(childWorldMat);
            e.scale                  = parentScale;
            e.localPos               = glm::vec3(att.px, att.py, att.pz);
            e.recordIndex            = (int)ri;
            e.parentWorldMat         = parentWorldMat;
            atta_pick_entries_.push_back(e);

            glm::mat4 leafModel = glm::scale(childWorldMat, glm::vec3(40.96f * parentScale));
            glUniformMatrix4fv(loc_model, 1, GL_FALSE, glm::value_ptr(leafModel));
            
            glUniform1i(loc_id, kAttaPickBase + 1 + entry);

            if (!subMesh.subMeshes.empty()) {
                for (const auto& sub : subMesh.subMeshes) {
                    if (sub.VAO == 0 || sub.vertexCount == 0) continue;
                    glBindVertexArray(sub.VAO);
                    glDrawArrays(GL_TRIANGLES, 0, sub.vertexCount);
                }
            } else if (subMesh.VAO) {
                glBindVertexArray(subMesh.VAO);
                glDrawArrays(GL_TRIANGLES, 0, subMesh.vertexCount);
            }
        }

        if (recurse) {
            DrawAttachmentsForPicking(att.modelId, isBuilding, childWorldMat, parentScale,
                                      loc_model, loc_id, parentObjIndex, drawn, selected_object_index);
        }
    }
}

// ─── DrawForPicking ───────────────────────────────────────────────────────────
void Renderer_Objects::DrawForPicking(GLuint ubo_mats,
                                      const std::vector<LevelObject>& objects,
                                      int draw_parts,
                                      const glm::vec3& camera_pos,
                                      int selected_object_index)
{
    if (!pick_shader_prog_) return;

    constexpr float BASE_SCALE = 40.96f;
    const int DRAW_OBJECTS   = 4;
    const int DRAW_BUILDINGS = 16;
    const int DRAW_PROPS     = 32;

    // Reset the per-pass ATTA pick capture and rebuild EditRigidObj occupancy so
    // ATTAs already promoted to real tasks aren't offered for promotion again.
    atta_pick_entries_.clear();
    editrigid_occupancy_.clear();
    suppressed_atta_keys_.clear();
    for (const auto& o : objects) {
        if (o.deleted || o.type != "EditRigidObj" || o.modelId.empty()) continue;
        editrigid_occupancy_.insert(AttaOccupancyKey(o.modelId, glm::vec3(o.pos)));
        if (o.name.rfind("ATTA:", 0) == 0) {
            suppressed_atta_keys_.insert(o.name.substr(5));
        }
    }

    // Set picking render state
    glBindFramebuffer(GL_FRAMEBUFFER, pick_fbo_);
    glViewport(0, 0, pick_fbo_w_, pick_fbo_h_);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    // GL_LEQUAL so that QSC child tasks (drawn AFTER the parent hull + ATTA) can
    // overwrite pick IDs written by ATTA sub-models at the same depth. Combined
    // with culling OFF (below), every child EditRigidObj wins over the parent ATTA.
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    // Match the visual pass: culling OFF. With back-face culling ON, child objects
    // whose meshes have reversed/inconsistent winding (consoles, crates, lights in
    // buildings) rendered visibly but were absent from the pick buffer.
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    glUseProgram(pick_shader_prog_);
    glBindBufferBase(GL_UNIFORM_BUFFER, ubo_binding_point_, ubo_mats);

    GLint loc_model = glGetUniformLocation(pick_shader_prog_, "u_model");
    GLint loc_id    = glGetUniformLocation(pick_shader_prog_, "u_object_id");

    for (int i = 0; i < (int)objects.size(); ++i) {
        const auto& obj = objects[i];
        if (obj.deleted || obj.modelId.empty()) continue;
        if (obj.isSplineWaypoint || obj.isSplineContainer) continue;
        if (IsSkippedModelId(obj.modelId)) continue;

        // Selective rendering (mirrors Draw())
        bool shouldDraw = false;
        if (draw_parts & DRAW_OBJECTS) {
            shouldDraw = true;
        } else {
            if ((draw_parts & DRAW_BUILDINGS) && obj.isBuilding)  shouldDraw = true;
            if ((draw_parts & DRAW_PROPS)     && !obj.isBuilding) shouldDraw = true;
        }
        if (!shouldDraw) continue;

        // NOTE: We deliberately do NOT cull objects that sit inside buildings.
        // Every child / sub-object (trucks, soldiers, crates, lights inside a
        // garage or container) must be individually pickable. GPU depth testing
        // already gives correct occlusion: building hull walls and window glass
        // are rendered opaque into the pick buffer and occlude objects behind
        // them, while true openings (open doors) let the cursor reach the
        // interior object underneath. This is geometrically exact and order-
        // independent, so AABB-based interior occlusion is unnecessary.

        const Mesh& mesh = GetOrLoadMesh(obj.modelId, obj.isBuilding);
        if (mesh.vertexCount == 0) continue;
        // Allow collision-only meshes (fromRenderMesh==false) as fallback pick hitboxes
        // so vehicles, cargo, and any model without render vertices are still clickable.

        // Build model matrix (same convention as Draw())
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(obj.pos));
        model = glm::rotate(model, (float)obj.rot.z, glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::rotate(model, (float)obj.rot.x, glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, (float)obj.rot.y, glm::vec3(0.0f, 1.0f, 0.0f));
        if (IsWeaponModel(obj.modelId) || obj.type == "GunPickup" || obj.type == "AmmoPickup" || obj.type == "GenericPickup") {
            model = glm::rotate(model, glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        }
        model = glm::scale(model, glm::vec3(BASE_SCALE * obj.scale));

        glUniformMatrix4fv(loc_model, 1, GL_FALSE, glm::value_ptr(model));
        glUniform1i(loc_id, i + 1); // ID 0 = background

        // Draw hull submeshes
        if (!mesh.subMeshes.empty()) {
            for (const auto& sub : mesh.subMeshes) {
                if (sub.VAO == 0 || sub.vertexCount == 0) continue;
                glBindVertexArray(sub.VAO);
                glDrawArrays(GL_TRIANGLES, 0, sub.vertexCount);
            }
        } else if (mesh.VAO) {
            glBindVertexArray(mesh.VAO);
            glDrawArrays(GL_TRIANGLES, 0, mesh.vertexCount);
        }

        // Render ATTA sub-models into the pick buffer, each with its OWN unique
        // pick ID (kAttaPickBase + entry). Pure MEF attachments (ceiling lights,
        // wall art, panels, crates that exist only in the building model and NOT
        // in objects.qsc) become individually clickable — the app promotes them
        // into real, editable EditRigidObj tasks. ATTAs that already have a
        // matching EditRigidObj are skipped (occupancy check inside).
        {
            glm::mat4 parentWorldMat(1.0f);
            parentWorldMat = glm::translate(parentWorldMat, glm::vec3(obj.pos));
            parentWorldMat = glm::rotate(parentWorldMat, (float)obj.rot.z, glm::vec3(0.f, 0.f, 1.f));
            parentWorldMat = glm::rotate(parentWorldMat, (float)obj.rot.x, glm::vec3(1.f, 0.f, 0.f));
            parentWorldMat = glm::rotate(parentWorldMat, (float)obj.rot.y, glm::vec3(0.f, 1.f, 0.f));
            std::unordered_set<std::string> drawn;
            DrawAttachmentsForPicking(obj.modelId, obj.isBuilding, parentWorldMat, obj.scale,
                                      loc_model, loc_id, i, drawn, selected_object_index);
            glUseProgram(pick_shader_prog_);
            glBindBufferBase(GL_UNIFORM_BUFFER, ubo_binding_point_, ubo_mats);
        }
    }

    glBindVertexArray(0);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Restore state
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDisable(GL_CULL_FACE);
}

// ─── PickObjectAtScreen ───────────────────────────────────────────────────────
int Renderer_Objects::PickObjectAtScreen(int x, int y, int w, int h,
                                          GLuint ubo_mats,
                                          const std::vector<LevelObject>& objects,
                                          int draw_parts,
                                          const glm::vec3& camera_pos,
                                          int selected_object_index)
{
    if (!pick_shader_prog_ || w <= 0 || h <= 0) return -1;

    // Resize FBO if window size changed
    if (w != pick_fbo_w_ || h != pick_fbo_h_) {
        InitPickingFBO(w, h);
    }
    if (!pick_fbo_) return -1;

    DrawForPicking(ubo_mats, objects, draw_parts, camera_pos, selected_object_index);

    // Read back the single pixel under the cursor.
    // OpenGL origin is bottom-left; screen coords are top-left → flip Y.
    uint8_t pixel[3] = {0, 0, 0};
    glBindFramebuffer(GL_FRAMEBUFFER, pick_fbo_);
    glReadPixels(x, h - y - 1, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    int id = (static_cast<int>(pixel[0]) << 16) |
             (static_cast<int>(pixel[1]) <<  8) |
              static_cast<int>(pixel[2]);

    return (id == 0) ? -1 : id - 1;
}

int Renderer_Objects::CountObjectVisiblePixels(
    GLuint ubo_mats, const std::vector<LevelObject>& objects, int draw_parts,
    const glm::vec3& camera_pos, int selected_object_index,
    int target_object_index, int viewport_width, int viewport_height,
    const std::vector<float>& scene_depth, int* target_id_pixels) {
    if (!pick_shader_prog_ || target_object_index < 0 ||
        target_object_index >= static_cast<int>(objects.size()) ||
        viewport_width <= 0 || viewport_height <= 0 ||
        scene_depth.size() != static_cast<size_t>(viewport_width) * viewport_height) {
        return 0;
    }
    if (viewport_width != pick_fbo_w_ || viewport_height != pick_fbo_h_) {
        InitPickingFBO(viewport_width, viewport_height);
    }
    if (!pick_fbo_) return 0;

    DrawForPicking(ubo_mats, objects, draw_parts, camera_pos,
                   selected_object_index);

    std::vector<uint8_t> pixels(
        static_cast<size_t>(pick_fbo_w_) * static_cast<size_t>(pick_fbo_h_) * 3);
    std::vector<float> pick_depth(
        static_cast<size_t>(pick_fbo_w_) * static_cast<size_t>(pick_fbo_h_));
    glBindFramebuffer(GL_FRAMEBUFFER, pick_fbo_);
    glReadPixels(0, 0, pick_fbo_w_, pick_fbo_h_, GL_RGB, GL_UNSIGNED_BYTE,
                 pixels.data());
    glReadPixels(0, 0, pick_fbo_w_, pick_fbo_h_, GL_DEPTH_COMPONENT, GL_FLOAT,
                 pick_depth.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    const int target_id = target_object_index + 1;
    int id_pixels = 0;
    int visible_pixels = 0;
    for (size_t offset = 0; offset < pixels.size(); offset += 3) {
        const int id = (static_cast<int>(pixels[offset]) << 16) |
                       (static_cast<int>(pixels[offset + 1]) << 8) |
                        static_cast<int>(pixels[offset + 2]);
        const size_t pixel_index = offset / 3;
        if (id == target_id) {
            ++id_pixels;
            if (scene_depth[pixel_index] < 1.0f &&
                std::abs(scene_depth[pixel_index] - pick_depth[pixel_index]) <= 0.0001f) {
                ++visible_pixels;
            }
        }
    }
    if (target_id_pixels) *target_id_pixels = id_pixels;
    return visible_pixels;
}

Renderer_Objects::VisualEvidence Renderer_Objects::CaptureObjectVisualEvidence(
    GLuint ubo_mats, const std::vector<LevelObject>& objects, int target_object_index,
    int viewport_width, int viewport_height, const std::vector<float>& scene_depth) {
    VisualEvidence evidence;
    if (!pick_shader_prog_ || target_object_index < 0 ||
        target_object_index >= static_cast<int>(objects.size()) || viewport_width <= 0 ||
        viewport_height <= 0 || scene_depth.size() != static_cast<size_t>(viewport_width) * viewport_height) {
        return evidence;
    }
    if (viewport_width != pick_fbo_w_ || viewport_height != pick_fbo_h_) {
        InitPickingFBO(viewport_width, viewport_height);
    }
    if (!pick_fbo_) return evidence;

    const LevelObject& obj = objects[target_object_index];
    const std::string modelId = obj.modelId.empty() ? obj.segmentModelId : obj.modelId;
    const Mesh& mesh = GetOrLoadMesh(modelId, obj.isBuilding);
    if (mesh.vertexCount == 0 || !mesh.fromRenderMesh) return evidence;

    evidence.width = viewport_width;
    evidence.height = viewport_height;
    const size_t pixels = static_cast<size_t>(viewport_width) * viewport_height;
    evidence.targetMask.assign(pixels, 0);
    evidence.partIds.assign(pixels, 0);
    evidence.targetDepth.assign(pixels, 1.0f);

    std::unordered_map<GLuint, float> texture_chromatic_ratios;
    auto texture_chromatic_ratio = [&](GLuint texture) {
        if (texture == 0) return 0.0f;
        const auto cached = texture_chromatic_ratios.find(texture);
        if (cached != texture_chromatic_ratios.end()) return cached->second;
        GLint active_texture = GL_TEXTURE0;
        GLint prior_texture = 0;
        GLint width = 0;
        GLint height = 0;
        glGetIntegerv(GL_ACTIVE_TEXTURE, &active_texture);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prior_texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
        float ratio = 0.0f;
        if (width > 0 && height > 0) {
            const size_t texels = static_cast<size_t>(width) * height;
            std::vector<uint8_t> rgba(texels * 4);
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
            size_t chromatic = 0;
            for (size_t texel = 0; texel < texels; ++texel) {
                const size_t offset = texel * 4;
                const uint8_t lo = std::min({rgba[offset], rgba[offset + 1], rgba[offset + 2]});
                const uint8_t hi = std::max({rgba[offset], rgba[offset + 1], rgba[offset + 2]});
                if (hi - lo >= 16) ++chromatic;
            }
            ratio = static_cast<float>(chromatic) / static_cast<float>(texels);
        }
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prior_texture));
        glActiveTexture(static_cast<GLenum>(active_texture));
        texture_chromatic_ratios.emplace(texture, ratio);
        return ratio;
    };

    glBindFramebuffer(GL_FRAMEBUFFER, pick_fbo_);
    glViewport(0, 0, viewport_width, viewport_height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    glUseProgram(pick_shader_prog_);
    glBindBufferBase(GL_UNIFORM_BUFFER, ubo_binding_point_, ubo_mats);
    const GLint loc_model = glGetUniformLocation(pick_shader_prog_, "u_model");
    const GLint loc_id = glGetUniformLocation(pick_shader_prog_, "u_object_id");

    glm::mat4 model(1.0f);
    model = glm::translate(model, glm::vec3(obj.pos));
    model = glm::rotate(model, static_cast<float>(obj.rot.z), glm::vec3(0.f, 0.f, 1.f));
    if (IsZyxEulerModel(obj.modelId)) {
        model = glm::rotate(model, static_cast<float>(obj.rot.y), glm::vec3(0.f, 1.f, 0.f));
        model = glm::rotate(model, static_cast<float>(obj.rot.x), glm::vec3(1.f, 0.f, 0.f));
    } else {
        model = glm::rotate(model, static_cast<float>(obj.rot.x), glm::vec3(1.f, 0.f, 0.f));
        model = glm::rotate(model, static_cast<float>(obj.rot.y), glm::vec3(0.f, 1.f, 0.f));
    }
    if (IsWeaponModel(obj.modelId) || obj.type == "GunPickup" || obj.type == "AmmoPickup" || obj.type == "GenericPickup") {
        model = glm::rotate(model, glm::radians(90.0f), glm::vec3(0.f, 0.f, 1.f));
    }
    model = glm::scale(model, glm::vec3(40.96f * obj.scale));
    glUniformMatrix4fv(loc_model, 1, GL_FALSE, glm::value_ptr(model));

    int next_part_id = 1;
    auto draw_mesh_parts = [&](const Mesh& part_mesh, const glm::mat4& part_model,
                               bool strict_coverage) {
        glUniformMatrix4fv(loc_model, 1, GL_FALSE, glm::value_ptr(part_model));
        if (part_mesh.subMeshes.empty()) {
            if (part_mesh.VAO && part_mesh.vertexCount > 0) {
                const int part_id = next_part_id++;
                evidence.expectedPartIds.push_back(part_id);
                evidence.expectedParts.push_back({part_id, part_mesh.vertexCount,
                                                  part_mesh.vertexCount / 3, -1, 0,
                                                  part_mesh.textureName,
                                                  part_mesh.textureName.empty() || part_mesh.textureID != 0,
                                                  texture_chromatic_ratio(part_mesh.textureID),
                                                  part_mesh.center - part_mesh.halfExtents,
                                                  part_mesh.center + part_mesh.halfExtents});
                if (strict_coverage) evidence.strictPartIds.push_back(part_id);
                glUniform1i(loc_id, part_id);
                glBindVertexArray(part_mesh.VAO);
                glDrawArrays(GL_TRIANGLES, 0, part_mesh.vertexCount);
            }
            return;
        }
        for (const auto& sub : part_mesh.subMeshes) {
            if (sub.VAO == 0 || sub.vertexCount == 0) continue;
            const int part_id = next_part_id++;
            evidence.expectedPartIds.push_back(part_id);
            evidence.expectedParts.push_back({part_id, sub.vertexCount, sub.vertexCount / 3,
                                              sub.materialSlot, sub.alphaMode, sub.textureName,
                                              sub.textureName.empty() || sub.textureID != 0,
                                              texture_chromatic_ratio(sub.textureID),
                                              sub.localMin, sub.localMax});
            if (strict_coverage) evidence.strictPartIds.push_back(part_id);
            glUniform1i(loc_id, part_id);
            glBindVertexArray(sub.VAO);
            glDrawArrays(GL_TRIANGLES, 0, sub.vertexCount);
        }
    };
    // The visible hull path draws with this offset.  The diagnostic must use
    // the identical depth convention before comparing its projection with the
    // captured scene depth.
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.0f, -1.0f);
    draw_mesh_parts(mesh, model, false);

    // The source inventory includes recursive MEF attachments.  This is a
    // geometry pass, deliberately independent of the visible attachment draw
    // so an omission in that path cannot certify itself.  It nevertheless
    // excludes known non-visual authored data using the same data-derived
    // predicates as the renderer (death zones, collision fallbacks and large
    // untextured trigger slabs).
    // Attachments are drawn one offset step closer than their parent hull in
    // the visible renderer to prevent z-fighting with hull walls.
    glPolygonOffset(-2.0f, -2.0f);
    EnsureDeathZoneIdsLoaded();
    EnsureAiModelIdsLoaded();
    const std::string prefix = obj.isBuilding ? "building:" : "object:";
    std::function<void(const std::string&, const glm::mat4&, std::unordered_set<std::string>&)> draw_attachments;
    draw_attachments = [&](const std::string& parent_model, const glm::mat4& parent_world,
                           std::unordered_set<std::string>& ancestry) {
        const std::string cache_key = std::to_string(current_level_) + ":" + prefix + parent_model;
        const auto ait = attachment_cache_.find(cache_key);
        if (ait == attachment_cache_.end()) return;
        for (const auto& att : ait->second) {
            std::string mesh_key = std::to_string(current_level_) + ":" + prefix + att.modelId;
            auto sit = mesh_cache_.find(mesh_key);
            if (sit == mesh_cache_.end() || sit->second.vertexCount == 0) {
                mesh_key = std::to_string(current_level_) + ":object:" + att.modelId;
                sit = mesh_cache_.find(mesh_key);
            }
            if (sit == mesh_cache_.end()) continue;
            const Mesh& child = sit->second;
            glm::mat4 local_rotation(att.r[0], att.r[1], att.r[2], 0.f,
                                     att.r[3], att.r[4], att.r[5], 0.f,
                                     att.r[6], att.r[7], att.r[8], 0.f,
                                     0.f,      0.f,      0.f,      1.f);
            const glm::vec3 world_pos = glm::vec3(parent_world * glm::vec4(att.px, att.py, att.pz, 1.f));
            glm::mat4 parent_rotation = parent_world;
            parent_rotation[3] = glm::vec4(0.f, 0.f, 0.f, 1.f);
            glm::mat4 child_world = glm::translate(glm::mat4(1.f), world_pos) * parent_rotation * local_rotation;

            const bool is_death_zone = deathzone_ids_.count(att.modelId) > 0;
            bool has_texture = child.textureID > 0;
            for (const auto& sub : child.subMeshes) has_texture |= sub.textureID > 0;
            const float max_half_extent = std::max(child.halfExtents.x,
                std::max(child.halfExtents.y, child.halfExtents.z));
            const bool large_untextured_slab = !has_texture &&
                (!ai_model_ids_.count(obj.modelId) || max_half_extent >= 700.0f);
            if (!is_death_zone && child.fromRenderMesh && !large_untextured_slab)
                draw_mesh_parts(child, glm::scale(child_world, glm::vec3(40.96f * obj.scale)), true);

            if (ancestry.insert(att.modelId).second) {
                draw_attachments(att.modelId, child_world, ancestry);
                ancestry.erase(att.modelId);
            }
        }
    };
    std::unordered_set<std::string> ancestry{modelId};
    draw_attachments(modelId, glm::scale(model, glm::vec3(1.0f / (40.96f * obj.scale))), ancestry);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glBindVertexArray(0);
    glUseProgram(0);

    std::vector<uint8_t> color(pixels * 3);
    glReadPixels(0, 0, viewport_width, viewport_height, GL_RGB, GL_UNSIGNED_BYTE, color.data());
    glReadPixels(0, 0, viewport_width, viewport_height, GL_DEPTH_COMPONENT, GL_FLOAT,
                 evidence.targetDepth.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    for (size_t index = 0; index < pixels; ++index) {
        const size_t color_index = index * 3;
        const int part_id = (static_cast<int>(color[color_index]) << 16) |
                            (static_cast<int>(color[color_index + 1]) << 8) |
                            static_cast<int>(color[color_index + 2]);
        if (part_id == 0) continue;
        evidence.partIds[index] = part_id;
        const auto part = std::find_if(evidence.expectedParts.begin(), evidence.expectedParts.end(),
            [part_id](const VisualEvidence::PartInventory& value) { return value.id == part_id; });
        const bool is_transparent_part = part != evidence.expectedParts.end() && part->alphaMode == 2;
        if ((is_transparent_part &&
             scene_depth[index] >= evidence.targetDepth[index] - 0.0001f) ||
            (!is_transparent_part && scene_depth[index] < 1.0f &&
             std::abs(scene_depth[index] - evidence.targetDepth[index]) <= 0.0001f)) {
            evidence.targetMask[index] = 1;
        }
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    return evidence;
}

// ─── Draw ─────────────────────────────────────────────────────────────────────
