#include "pch.h"
#include "renderer_splines.h"
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

void Renderer_Splines::Init() {}

void Renderer_Splines::ClearBentCache() {
    for (auto& entry : bent_gpu_cache_) {
        for (auto& sub : entry.second.subMeshes) {
            if (sub.VAO) glDeleteVertexArrays(1, &sub.VAO);
            if (sub.VBO) glDeleteBuffers(1, &sub.VBO);
        }
    }
    bent_gpu_cache_.clear();
}

size_t Renderer_Splines::BentSpanKeyHash::operator()(const BentSpanKey& key) const {
    size_t h = std::hash<std::string>{}(key.modelId);
    auto mix = [&](double value) {
        uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        h ^= static_cast<size_t>(bits) + 0x9e3779b9u + (h << 6) + (h >> 2);
    };
    mix(key.p0.x); mix(key.p0.y); mix(key.p0.z);
    mix(key.p1.x); mix(key.p1.y); mix(key.p1.z);
    mix(key.rot0.x); mix(key.rot0.y); mix(key.rot0.z);
    mix(key.rot1.x); mix(key.rot1.y); mix(key.rot1.z);
    h ^= static_cast<size_t>(key.linear) + 0x9e3779b9u + (h << 6) + (h >> 2);
    return h;
}

namespace {

std::vector<SubMesh> UploadBentSegmentMesh(
    const spline_geometry::BentSegmentMesh& bent,
    const Mesh& sourceMesh) {
    std::vector<SubMesh> uploaded;
    uploaded.reserve(bent.submeshVertexCounts.size());
    size_t offset = 0;
    size_t sourceSubmesh = 0;
    for (int vertexCount : bent.submeshVertexCounts) {
        if (vertexCount <= 0) continue;
        const size_t floatCount = static_cast<size_t>(vertexCount) * 10;
        if (offset + floatCount > bent.interleaved.size()) break;

        SubMesh sub;
        if (sourceSubmesh < sourceMesh.subMeshes.size()) {
            sub.textureID = sourceMesh.subMeshes[sourceSubmesh].textureID;
            sub.alphaMode = sourceMesh.subMeshes[sourceSubmesh].alphaMode;
            sub.baseColorFactor = sourceMesh.subMeshes[sourceSubmesh].baseColorFactor;
            sub.materialSlot = sourceMesh.subMeshes[sourceSubmesh].materialSlot;
        }
        sub.vertexCount = vertexCount;

        glGenVertexArrays(1, &sub.VAO);
        glGenBuffers(1, &sub.VBO);
        glBindVertexArray(sub.VAO);
        glBindBuffer(GL_ARRAY_BUFFER, sub.VBO);
        glBufferData(GL_ARRAY_BUFFER,
                     floatCount * sizeof(float),
                     bent.interleaved.data() + offset,
                     GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)(6 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)(8 * sizeof(float)));
        glEnableVertexAttribArray(3);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);

        uploaded.push_back(sub);
        offset += floatCount;
        ++sourceSubmesh;
    }
    return uploaded;
}

} // namespace

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

    GLint loc_model    = glGetUniformLocation(shader_program, "u_model");
    GLint loc_dirlight = glGetUniformLocation(shader_program, "u_dirlight");
    GLint loc_ambient  = glGetUniformLocation(shader_program, "u_ambient");
    GLint loc_useTex      = glGetUniformLocation(shader_program, "u_useTexture");
    GLint loc_tex         = glGetUniformLocation(shader_program, "u_texture");
    GLint loc_useLightmap = glGetUniformLocation(shader_program, "u_useLightmap");
    GLint loc_alpha       = glGetUniformLocation(shader_program, "u_alpha");
    GLint loc_baseColor   = glGetUniformLocation(shader_program, "u_baseColor");
    GLint loc_tint        = glGetUniformLocation(shader_program, "u_tint");
    GLint loc_glassMin    = glGetUniformLocation(shader_program, "u_glassMin");

    glUniform1i(loc_useLightmap, 0);
    glUniform1f(loc_alpha, 1.0f);
    glUniform4f(loc_baseColor, 1.0f, 1.0f, 1.0f, 1.0f);
    glUniform3f(loc_tint, 1.0f, 1.0f, 1.0f);
    glUniform1f(loc_glassMin, 0.0f);

    thread_local std::vector<int> children;
    for (const auto& obj : objects) {
        if (!obj.isSplineContainer || obj.deleted) continue;
        if (Renderer_Objects::IsSkippedModelId(obj.modelId)) continue;

        children.clear();
        children.reserve(obj.childrenIndices.size());
        for (int childIndex : obj.childrenIndices) {
            if (childIndex < 0 || childIndex >= static_cast<int>(objects.size())) continue;
            const LevelObject& child = objects[childIndex];
            if (!child.deleted && child.isSplineWaypoint) children.push_back(childIndex);
        }

        std::string_view fallbackSegmentModelId;
        for (int ci : children) {
            if (ci >= 0 && ci < (int)objects.size() && !objects[ci].segmentModelId.empty()) {
                fallbackSegmentModelId = objects[ci].segmentModelId;
                break;
            }
        }

        for (size_t i = 0; i + 1 < children.size(); ++i) {
            DrawSplineSegment(
                objects[children[i]], objects[children[i + 1]],
                obj, ubo_mats, shader_program,
                loc_model, loc_dirlight, loc_ambient, loc_useTex, loc_tex,
                loc_useLightmap, loc_alpha, loc_baseColor, loc_tint, loc_glassMin,
                fallbackSegmentModelId);
        }
    }

    glDisable(GL_CULL_FACE);
    glUseProgram(0);
}

void Renderer_Splines::DrawSplineSegment(
    const LevelObject& start,
    const LevelObject& end,
    const LevelObject& parent,
    GLuint ubo_mats,
    GLuint shader_program,
    GLint loc_model,
    GLint loc_dirlight,
    GLint loc_ambient,
    GLint loc_useTex,
    GLint loc_tex,
    GLint loc_useLightmap,
    GLint loc_alpha,
    GLint loc_baseColor,
    GLint loc_tint,
    GLint loc_glassMin,
    std::string_view fallbackSegmentModelId)
{
    (void)ubo_mats;
    (void)shader_program;
    (void)loc_useLightmap;
    (void)loc_baseColor;
    (void)loc_tint;
    (void)loc_glassMin;

    const std::string_view segModelId = spline_geometry::ResolveSegmentModel(
        start.segmentModelId, end.segmentModelId, fallbackSegmentModelId);
    if (segModelId.empty()) return;
    if (segModelId == "colbox" || segModelId == "colbox2" ||
        segModelId == "colbox4" || segModelId == "colbox66") return;

    const glm::dvec3 p0 = start.pos;
    const glm::dvec3 p1 = end.pos;
    const double intervalLen = glm::length(p1 - p0);
    if (!std::isfinite(intervalLen) || intervalLen <= 1e-9) return;

    bent_lookup_key_.modelId.assign(segModelId);
    bent_lookup_key_.p0 = p0;
    bent_lookup_key_.p1 = p1;
    bent_lookup_key_.rot0 = start.rot;
    bent_lookup_key_.rot1 = end.rot;
    bent_lookup_key_.linear = parent.linearSegments;
    auto cacheIt = bent_gpu_cache_.find(bent_lookup_key_);
    if (cacheIt == bent_gpu_cache_.end()) {
        const std::string modelId(segModelId);
        const ParsedGeometry* geometry = obj_renderer_.GetOrLoadParsedGeometry(modelId, false);
        if (!geometry) return;

        const glm::dvec3 tan0 = spline_geometry::MakeWaypointTangent(start.rot, intervalLen);
        const glm::dvec3 tan1 = spline_geometry::MakeWaypointTangent(end.rot, intervalLen);
        const spline_geometry::SplineSegment segment{p0, p1, tan0, tan1, parent.linearSegments};
        const auto bent = spline_geometry::BendSegmentMesh(*geometry, segment);
        if (!bent.has_value()) return;

        const Mesh& sourceMesh = obj_renderer_.GetOrLoadMesh(modelId, false);
        if (sourceMesh.vertexCount == 0) return;

        CachedBentSpanGpu cached;
        cached.subMeshes = UploadBentSegmentMesh(*bent, sourceMesh);
        if (cached.subMeshes.empty()) return;
        cacheIt = bent_gpu_cache_.emplace(bent_lookup_key_, std::move(cached)).first;
    }

    const glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(static_cast<float>(p0.x),
                                                                      static_cast<float>(p0.y),
                                                                      static_cast<float>(p0.z)))
                          * glm::scale(glm::mat4(1.0f), glm::vec3(40.96f));
    glUniformMatrix4fv(loc_model, 1, GL_FALSE, glm::value_ptr(model));

    for (const auto& sub : cacheIt->second.subMeshes) {
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
}
