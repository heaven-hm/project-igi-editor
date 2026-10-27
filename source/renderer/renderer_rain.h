#pragma once
#include "../pch.h"
#include <glm/glm.hpp>

// Renders falling rain streaks or snow flakes matching OpenIGI.
class Renderer_Rain {
public:
    bool Init();
    void Shutdown();

    // startMeters/endMeters come from RainEffect's "Traceline start"/"Traceline
    // end" fields (height above ground, in meters, where rain begins/ends falling).
    void SetParams(bool active, bool is_snow, float startMeters, float endMeters, float alpha);
    void SetWeatherEnabled(bool enabled) { weather_enabled_ = enabled; }

    // cameraRight/cameraUp are the frame's camera basis. Rain_FillVB expands the
    // flake quad in the camera's own axes (gTransContext.matrix, rain.c:338-340,
    // :382-388), so the basis has to come from the frame being drawn.
    //
    // snowTexture is the game's `snow.tex` as the engine loads it
    // (Game_AddTexture("snow.tex"), fxobj/rain.c:82; bound only by the snow branch,
    // rain.c:431-434). Pass 0 when it could not be resolved: Rain_DrawCell's
    // null-texture branch (rain.c:435-438) is then reproduced exactly.
    void Draw(GLuint ubo_mats, const glm::vec3& cameraPos, const glm::vec3& cameraRight,
              const glm::vec3& cameraUp, bool cameraIsSheltered, GLuint snowTexture);
    bool IsActive() const { return active_ && weather_enabled_; }
    bool IsSnow() const { return is_snow_; }
    bool IsWeatherEnabled() const { return weather_enabled_; }

private:
    void DrawSnow(GLuint ubo_mats, const glm::vec3& cameraPos, const glm::vec3& cameraRight,
                  const glm::vec3& cameraUp, float time_sec, GLuint snow_texture);

    GLuint shader_program_ = 0;
    GLuint snow_program_ = 0;
    GLuint ubo_binding_point_ = 0;
    GLuint vao_rain_ = 0;
    GLuint vbo_rain_ = 0;
    GLuint vao_snow_ = 0;
    GLuint vbo_snow_ = 0;
    int num_rain_drops_ = 1200;
    int num_snow_flakes_ = 900;

    bool active_ = false;
    bool weather_enabled_ = true;
    bool is_snow_ = false;
    float start_meters_ = 10.0f;
    float end_meters_ = 2.0f;
    float alpha_ = 0.5f;
};
