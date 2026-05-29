#include "pch.h"
#include "renderer_splines.h"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

void Renderer_Splines::Init() {}

glm::vec3 Renderer_Splines::HermitePoint(float t,
    const glm::vec3& p0, const glm::vec3& p1,
    const glm::vec3& t0, const glm::vec3& t1)
{
    float t2 = t * t, t3 = t2 * t;
    return (2.f*t3 - 3.f*t2 + 1.f)*p0
         + (t3 - 2.f*t2 + t)*t0
         + (-2.f*t3 + 3.f*t2)*p1
         + (t3 - t2)*t1;
}

void Renderer_Splines::Draw(
    const std::vector<LevelObject>& objects,
    GLuint ubo_mats,
    GLuint shader_program)
{
    if (!shader_program) return;

    glUseProgram(shader_program);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo_mats);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    for (const auto& obj : objects) {
        if (!obj.isSplineContainer || obj.deleted) continue;
        if (Renderer_Objects::IsSkippedModelId(obj.modelId)) continue;

        const auto& children = obj.childrenIndices;

        // Find the first non-empty segmentModelId as fallback for waypoints that leave it blank.
        std::string fallbackSegmentModelId;
        for (int ci : children) {
            if (ci >= 0 && ci < (int)objects.size() && !objects[ci].segmentModelId.empty()) {
                fallbackSegmentModelId = objects[ci].segmentModelId;
                break;
            }
        }

        for (size_t i = 0; i + 1 < children.size(); ++i) {
            int si = children[i];
            int ei = children[i + 1];
            int pi = (i > 0) ? children[i - 1] : si;
            int ni = (i + 2 < children.size()) ? children[i + 2] : ei;

            if (si < 0 || si >= (int)objects.size()) continue;
            if (ei < 0 || ei >= (int)objects.size()) continue;
            if (pi < 0 || pi >= (int)objects.size()) pi = si;
            if (ni < 0 || ni >= (int)objects.size()) ni = ei;

            if (objects[si].deleted || objects[ei].deleted) continue;

            DrawSplineSegment(
                objects[si], objects[ei],
                objects[pi], objects[ni],
                obj, ubo_mats, shader_program,
                fallbackSegmentModelId);
        }
    }

    glDisable(GL_CULL_FACE);
    glUseProgram(0);
}

void Renderer_Splines::DrawSplineSegment(
    const LevelObject& start,
    const LevelObject& end,
    const LevelObject& prev,
    const LevelObject& nextNext,
    const LevelObject& parent,
    GLuint ubo_mats,
    GLuint shader_program,
    const std::string& fallbackSegmentModelId)
{
    const std::string& segModelId = start.segmentModelId.empty() ? fallbackSegmentModelId : start.segmentModelId;
    if (segModelId.empty()) return;
    if (Renderer_Objects::IsSkippedModelId(segModelId)) return;

    Mesh mesh = obj_renderer_.GetOrLoadMesh(segModelId, false);
    if (mesh.vertexCount == 0) return;

    GLint loc_model    = glGetUniformLocation(shader_program, "u_model");
    GLint loc_dirlight = glGetUniformLocation(shader_program, "u_dirlight");
    GLint loc_ambient  = glGetUniformLocation(shader_program, "u_ambient");
    GLint loc_useTex   = glGetUniformLocation(shader_program, "u_useTexture");
    GLint loc_tex      = glGetUniformLocation(shader_program, "u_texture");

    glm::vec3 p0     = glm::vec3(start.pos);
    glm::vec3 p1     = glm::vec3(end.pos);
    glm::vec3 p_prev = glm::vec3(prev.pos);
    glm::vec3 p_next = glm::vec3(nextNext.pos);

    glm::vec3 tan0 = (p1 - p_prev) * 0.5f;
    glm::vec3 tan1 = (p_next - p0) * 0.5f;

    // Clamp tangent magnitude to interval length to prevent overshoot at transitions.
    float intervalLen = glm::length(p1 - p0);
    float t0len = glm::length(tan0);
    float t1len = glm::length(tan1);
    if (t0len > intervalLen) tan0 *= intervalLen / t0len;
    if (t1len > intervalLen) tan1 *= intervalLen / t1len;

    // Tile geometry span in model X. minX is the model's near edge; localLen is the
    // full X extent. A natural (unstretched) tile spans localLen * 40.96 in world units.
    const float LENGTH_SCALE = 40.96f;
    float minX     = mesh.center.x - mesh.halfExtents.x;
    float localLen = mesh.halfExtents.x * 2.0f;
    if (localLen < 1.f) localLen = 1.f;
    float tileWorldLen = localLen * LENGTH_SCALE;

    // Choose the tile count that lands closest to the natural tile length, then stretch
    // each tile in X to fill its share of the segment exactly. This guarantees tiles
    // butt end-to-end with no gaps or overlaps (the game's "Snap Length" behavior).
    int steps = std::max(1, (int)std::lround(intervalLen / tileWorldLen));
    steps = std::min(steps, 64);

    const glm::vec3 worldZ(0.f, 0.f, 1.f);

    for (int i = 0; i < steps; ++i) {
        float ta = (float)i       / (float)steps;
        float tb = (float)(i + 1) / (float)steps;

        // Tile endpoints sampled on the Hermite curve.
        glm::vec3 a = HermitePoint(ta, p0, p1, tan0, tan1);
        glm::vec3 b = HermitePoint(tb, p0, p1, tan0, tan1);

        // Terrain snap both endpoints: flat track rides up onto terrain when the curve
        // dips below it; elevated/bridge tiles (curve already above terrain) are untouched.
        // Snapping the actual endpoints — not a single sample — keeps adjacent tiles joined.
        if (terrain_z_fn_) {
            float tz = 0.f;
            if (terrain_z_fn_((double)a.x, (double)a.y, tz) && tz > a.z) a.z = tz;
            if (terrain_z_fn_((double)b.x, (double)b.y, tz) && tz > b.z) b.z = tz;
        }

        // Orient the tile along the chord a→b. Pitch comes from the actual placed
        // endpoints, so the tile follows terrain/grade and connects seamlessly to its
        // neighbours. (Chord direction, not the instantaneous tangent, prevents the
        // curve overshoot that made earlier builds look near-vertical.)
        glm::vec3 chord = b - a;
        float chordLen = glm::length(chord);
        if (chordLen < 0.001f) continue;
        glm::vec3 forward = chord / chordLen;

        glm::vec3 right = glm::cross(worldZ, forward);
        if (glm::length(right) < 0.001f)               // near-vertical fallback
            right = glm::cross(glm::vec3(1.f, 0.f, 0.f), forward);
        right = glm::normalize(right);
        glm::vec3 up = glm::cross(forward, right);

        glm::mat4 rotMat(1.f);
        rotMat[0] = glm::vec4(forward, 0.f);           // local X → track direction
        rotMat[1] = glm::vec4(right,   0.f);           // local Y → track width
        rotMat[2] = glm::vec4(up,      0.f);           // local Z → track up

        // Stretch X so the tile's localLen exactly covers chordLen; Y/Z keep their
        // natural scale so the cross-section (width/height) is undistorted.
        float sx = chordLen / localLen;

        // Place so the model's near edge (local minX) lands on point a.
        glm::vec3 pos = a - forward * (sx * minX);

        glm::mat4 unscaledModel = glm::translate(glm::mat4(1.f), pos) * rotMat;
        glm::mat4 model = glm::scale(unscaledModel, glm::vec3(sx, LENGTH_SCALE, LENGTH_SCALE));

        glUniformMatrix4fv(loc_model, 1, GL_FALSE, glm::value_ptr(model));

        for (const auto& sub : mesh.subMeshes) {
            if (sub.VAO == 0 || sub.vertexCount == 0) continue;
            if (sub.textureID > 0) {
                glUniform3f(loc_dirlight, 0.6f, 0.6f, 0.6f);
                glUniform3f(loc_ambient,  0.4f, 0.4f, 0.4f);
                glUniform1i(loc_useTex, 1);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, sub.textureID);
                glUniform1i(loc_tex, 0);
            } else {
                glUniform3f(loc_dirlight, 0.7f, 0.7f, 0.7f);
                glUniform3f(loc_ambient,  0.2f, 0.2f, 0.2f);
                glUniform1i(loc_useTex, 0);
            }
            glBindVertexArray(sub.VAO);
            glDrawArrays(GL_TRIANGLES, 0, sub.vertexCount);
        }
        glBindVertexArray(0);

        // Rails/details (ATTA) use the same orientation and the same X stretch so they
        // stay aligned with the stretched deck tile.
        obj_renderer_.DrawAttachmentsForSpline(segModelId, /*isBuilding=*/false, unscaledModel, ubo_mats,
                                               glm::vec3(sx, LENGTH_SCALE, LENGTH_SCALE));
        glUseProgram(shader_program);
        glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo_mats);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
}

