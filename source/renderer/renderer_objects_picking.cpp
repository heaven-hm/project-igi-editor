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
    std::unordered_set<std::string>& drawn)
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
                                      loc_model, loc_id, parentObjIndex, drawn);
        }
    }
}

// ─── DrawForPicking ───────────────────────────────────────────────────────────
void Renderer_Objects::DrawForPicking(GLuint ubo_mats,
                                      const std::vector<LevelObject>& objects,
                                      int draw_parts,
                                      const glm::vec3& camera_pos)
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

        Mesh mesh = GetOrLoadMesh(obj.modelId, obj.isBuilding);
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
                                      loc_model, loc_id, i, drawn);
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
                                          const glm::vec3& camera_pos)
{
    if (!pick_shader_prog_ || w <= 0 || h <= 0) return -1;

    // Resize FBO if window size changed
    if (w != pick_fbo_w_ || h != pick_fbo_h_) {
        InitPickingFBO(w, h);
    }
    if (!pick_fbo_) return -1;

    DrawForPicking(ubo_mats, objects, draw_parts, camera_pos);

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

// ─── Draw ─────────────────────────────────────────────────────────────────────
