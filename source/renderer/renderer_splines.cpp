#include "pch.h"
#include "renderer_splines.h"
#include "logger.h"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

void Renderer_Splines::Init() {
    // Initialization if needed
}

void Renderer_Splines::Draw(const std::vector<LevelObject>& objects, GLuint ubo_mats, GLuint shader_program) {
    if (!shader_program) return;
    
    // Bind shader and UBO
    glUseProgram(shader_program);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo_mats);
    
    // OpenGL state
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    for (const auto& obj : objects) {
        if (obj.isSplineContainer) {
            // Check if this spline should be skipped (e.g. wires)
            if (Renderer_Objects::IsSkippedModelId(obj.modelId)) {
                continue;
            }

            for (size_t i = 0; i < obj.childrenIndices.size(); ++i) {
                if (i + 1 < obj.childrenIndices.size()) {
                    int startIdx = obj.childrenIndices[i];
                    int endIdx = obj.childrenIndices[i+1];
                    int prevIdx = (i > 0) ? obj.childrenIndices[i-1] : startIdx;
                    int nextNextIdx = (i + 2 < obj.childrenIndices.size()) ? obj.childrenIndices[i+2] : endIdx;
                    
                    DrawSplineSegment(objects[startIdx], objects[endIdx], objects[prevIdx], objects[nextNextIdx], obj, ubo_mats, shader_program);
                }
            }
        }
    }
    
    // Cleanup state
    glUseProgram(0);
}

void Renderer_Splines::DrawSplineSegment(const LevelObject& start, const LevelObject& end, const LevelObject& prev, const LevelObject& nextNext, const LevelObject& parent, GLuint ubo_mats, GLuint shader_program) {
    if (start.segmentModelId.empty()) return;
    
    // Check if the segment model itself should be skipped
    if (Renderer_Objects::IsSkippedModelId(start.segmentModelId)) return;
    
    Mesh mesh = obj_renderer_.GetOrLoadMesh(start.segmentModelId, false);
    if (mesh.vertexCount == 0) return;

    GLint loc_model = glGetUniformLocation(shader_program, "u_model");
    GLint loc_dirlight = glGetUniformLocation(shader_program, "u_dirlight");
    GLint loc_ambient  = glGetUniformLocation(shader_program, "u_ambient");
    GLint loc_useTex   = glGetUniformLocation(shader_program, "u_useTexture");
    GLint loc_tex      = glGetUniformLocation(shader_program, "u_texture");

    float base_scale = 4.096f;
    
    glm::vec3 p0 = glm::vec3(start.pos);
    glm::vec3 p1 = glm::vec3(end.pos);
    glm::vec3 p_prev = glm::vec3(prev.pos);
    glm::vec3 p_next = glm::vec3(nextNext.pos);
    
    // Use Catmull-Rom tangents based on adjacent points
    // This perfectly prevents loops and wild spiraling.
    glm::vec3 t0 = (p1 - p_prev) * 0.5f;
    glm::vec3 t1 = (p_next - p0) * 0.5f;

    int steps = parent.splineSegmentCount;
    if (steps < 1) steps = 20;

    for (int i = 0; i < steps; ++i) {
        float t = (float)i / (float)steps;
        float next_t = (float)(i + 1) / (float)steps;
        
        glm::vec3 pos = CalculateSplinePoint(t, p0, p1, t0, t1);
        glm::vec3 nextPos = CalculateSplinePoint(next_t, p0, p1, t0, t1);
        
        // If curve collapsed to a point, use linear fallback
        if (glm::distance(pos, nextPos) < 0.001f) {
            pos = p0 + (p1 - p0) * t;
            nextPos = p0 + (p1 - p0) * next_t;
        }

        glm::vec3 dir = glm::normalize(nextPos - pos);
        
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, pos);
        
        glm::vec3 forward = dir;
        glm::vec3 up = glm::vec3(0, 0, 1);
        if (abs(glm::dot(forward, up)) > 0.99f) up = glm::vec3(0, 1, 0);
        
        glm::vec3 right = glm::normalize(glm::cross(forward, up));
        up = glm::normalize(glm::cross(right, forward));
        
        // The track model has its length along its local X axis.
        // Its height is along local Y axis (Y-up model).
        // Its width is along local Z axis.
        // So we map:
        // Local X -> forward
        // Local Y -> up
        // Local Z -> right
        
        glm::mat4 rot = glm::mat4(1.0f);
        rot[0] = glm::vec4(forward, 0); // Local X -> forward
        rot[1] = glm::vec4(up, 0);      // Local Y -> up
        rot[2] = glm::vec4(right, 0);   // Local Z -> right
        
        model = model * rot;
        
        // Local X (length) -> forward, Local Y (height) -> up, Local Z (width) -> right
        float length_scale = base_scale;
        float height_scale = base_scale * 10.0f; // Stretch massively to form the concrete foundation wall
        float width_scale = base_scale * 5.7f;
        
        model = glm::scale(model, glm::vec3(length_scale, height_scale, width_scale));
        // Removed the rotate90X because our rot matrix directly maps local Y to up!

        glUniformMatrix4fv(loc_model, 1, GL_FALSE, glm::value_ptr(model));
        
        for (const auto& sub : mesh.subMeshes) {
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
}

glm::vec3 Renderer_Splines::CalculateSplinePoint(float t, const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& t0, const glm::vec3& t1) {
    float t2 = t * t;
    float t3 = t2 * t;
    // Cubic Hermite Spline formula
    return (2.0f*t3 - 3.0f*t2 + 1.0f) * p0 + (t3 - 2.0f*t2 + t) * t0 + (-2.0f*t3 + 3.0f*t2) * p1 + (t3 - t2) * t1;
}
