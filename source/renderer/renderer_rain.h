#pragma once
#include "../pch.h"
#include <glm/glm.hpp>

// Renders falling rain streaks for levels whose objects.qsc has an active
// RainEffect task (e.g. level3). Levels without that task (e.g. level2) never
// call SetParams(true, ...) and this stays a no-op. Procedural GPU-only
// particles: each "drop" is a 2-vertex line segment whose position is derived
// in the vertex shader from a per-drop random seed + elapsed time, wrapped in
// a box around the camera — no per-frame CPU update needed.
class Renderer_Rain {
public:
    bool Init();
    void Shutdown();

    // startMeters/endMeters come from RainEffect's "Traceline start"/"Traceline
    // end" fields (height above ground, in meters, where rain begins/ends falling).
    void SetParams(bool active, float startMeters, float endMeters, float alpha);

    void Draw(GLuint ubo_mats, const glm::vec3& cameraPos);

private:
    GLuint shader_program_ = 0;
    GLuint ubo_binding_point_ = 0;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    int num_drops_ = 0;

    bool active_ = false;
    float start_meters_ = 0.0f;
    float end_meters_ = 0.0f;
    float alpha_ = 0.5f;
};
