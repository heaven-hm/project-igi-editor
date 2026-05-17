#pragma once
#include "../pch.h"
#include "../level/level_objects.h"
#include "renderer_objects.h"

class Renderer_Splines {
public:
    Renderer_Splines(Renderer_Objects& obj_renderer) : obj_renderer_(obj_renderer) {}

    void Init();
    void Draw(const std::vector<LevelObject>& objects, GLuint ubo_mats, GLuint shader_program);

private:
    Renderer_Objects& obj_renderer_;
    
    void DrawSplineSegment(const LevelObject& start, const LevelObject& end, const LevelObject& prev, const LevelObject& nextNext, const LevelObject& parent, GLuint ubo_mats, GLuint shader_program);
    glm::vec3 CalculateSplinePoint(float t, const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& t0, const glm::vec3& t1);
};
