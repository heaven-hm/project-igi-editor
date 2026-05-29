#pragma once
#include "../pch.h"
#include "../level/level_objects.h"
#include "renderer_objects.h"
#include <functional>

class Renderer_Splines {
public:
    Renderer_Splines(Renderer_Objects& obj_renderer) : obj_renderer_(obj_renderer) {}

    void Init();
    void Draw(const std::vector<LevelObject>& objects, GLuint ubo_mats, GLuint shader_program);

    // Optional terrain height callback — when set, tile Z positions are snapped to
    // max(hermite_z, terrain_z) so flat track sits on terrain and elevated sections
    // stay above it. Signature: (world_x, world_y, out_z) → true if terrain found.
    void SetTerrainQuery(std::function<bool(double, double, float&)> fn) {
        terrain_z_fn_ = std::move(fn);
    }

private:
    Renderer_Objects& obj_renderer_;
    std::function<bool(double, double, float&)> terrain_z_fn_;

    void DrawSplineSegment(
        const LevelObject& start,
        const LevelObject& end,
        const LevelObject& prev,
        const LevelObject& nextNext,
        const LevelObject& parent,
        GLuint ubo_mats,
        GLuint shader_program,
        const std::string& fallbackSegmentModelId = "");

    static glm::vec3 HermitePoint(float t,
        const glm::vec3& p0, const glm::vec3& p1,
        const glm::vec3& t0, const glm::vec3& t1);
};
