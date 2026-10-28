#pragma once
#include "../pch.h"
#include <glm/glm.hpp>
#include <functional>

// The engine's weather particles: ONE `Rain` task, ONE pool of 80 drops, ONE
// camera-anchored tile lattice. There is no separate snow system - Rain_New
// HOOK 0x498960 stores `r->snow = rain == 0` (src/dxinit/rain.c:153) and every
// difference is a branch on that flag: the speed and sway (rain.c:572,592-617),
// the spawn z (rain.c:631), Rain_FillVB's 2-vertex LINE versus 6-vertex
// camera-facing quad (rain.c:337-372) and Rain_DrawCell's primitive, texture and
// stage-0 operator (rain.c:430-459). Everything else - the pool, the spawn
// accumulator, the 6 m wrap band, the 18 m draw band, the ground probe, the
// fade, the vertex groups and the cell clipping - is shared.
//
// This class is the transcription of that task. It owns the pool, the cells and
// the vertex groups; every constant comes from source/runtime/rain_rules.h,
// which cites IGI-RE for each one.
class Renderer_Rain {
public:
    // The engine's per-cell ground probe (Rain_UpdateGrid, HOOK 0x498BD0,
    // rain.c:258-265) drops each cell's point down `fallDist` against the
    // collision world with Coll_DropPointsMat. The editor's level supplies the
    // same answer through Level::GetTerrainZ. Returns false when there is no
    // ground to report; the cell is then left unprobed, which is the engine's
    // own state for a cell Rain_UpdateGrid skipped (rain.c:255-266) and is
    // never drawn (rain.c:290).
    using GroundQuery = std::function<bool(double x, double y, float& z)>;

    // The frame's camera, in the engine's terms. gTransContext.matrix's rows
    // (rain.c:508-534) are the camera's right, DOWN and forward axes - the
    // engine's projection negates the vertical axis (m._22 = -1/fovY,
    // src/transctx/transctx.c:143) while TransContext_ProjectView adds
    // +scaleY*view_y (transctx.c:276-277), so +view_y is down the screen.
    struct Camera {
        glm::vec3 pos{0.0f};
        glm::vec3 right{1.0f, 0.0f, 0.0f};
        glm::vec3 up{0.0f, 0.0f, 1.0f};
        glm::vec3 forward{0.0f, 1.0f, 0.0f};
        // TransContext_Create's scaleX/scaleY/centerX/centerY
        // (src/transctx/transctx.c:198-201) and the viewport the engine reads
        // as Display_GetMode()->width/height (rain.c:498-499).
        double scale_x = 1.0;
        double scale_y = 1.0;
        double center_x = 0.0;
        double center_y = 0.0;
        double viewport_width = 1.0;
        double viewport_height = 1.0;

        // Rain_FillVB's `vx/vy/vz` (rain.c:337-340): the drop in view space.
        glm::vec3 ToView(const glm::vec3& d) const {
            return glm::vec3(glm::dot(right, d), -glm::dot(up, d), glm::dot(forward, d));
        }
    };

    bool Init();
    void Shutdown();

    void SetGroundQuery(GroundQuery query) { ground_query_ = std::move(query); }

    // RainEffect's five authored parameters (fxobj/rain.c:60-84). startMeters /
    // endMeters are "Traceline start"/"Traceline end": RainEffect_Changed scales
    // both by 4096 (fxobj/rain.c:138-139) and Rain_New stores height = start,
    // fallDist = start + end (rain.c:159-160), which Rain_UpdateGrid uses only
    // to size the ground ray. `is_snow` is the inverted "Is Rain" bool8.
    void SetParams(bool active, bool is_snow, float startMeters, float endMeters, float alpha);
    void SetWeatherEnabled(bool enabled) { weather_enabled_ = enabled; }

    // The editor's preview speed multiplier (see igi::WeatherSpeed,
    // source/runtime/weather_mode.h). The engine has no such control: its speed
    // is `speed = r->snow ? kSnowSpeed : kRainSpeed` (Rain_Update, HOOK
    // 0x499CF0, src/dxinit/rain.c:572) with the constants at rain.c:122-123.
    // The scale multiplies that one value, so the fall, the wrap, the spawn
    // accumulator and the spawn z all stay consistent with each other.
    // Not a Rain_New argument, so it never rebuilds the object.
    void SetSpeedScale(float scale) { speed_scale_ = scale; }
    float SpeedScale() const { return speed_scale_; }
    void Draw(GLuint ubo_mats, const Camera& camera, bool cameraIsSheltered,
              GLuint snowTexture);
    bool IsActive() const { return active_ && weather_enabled_; }
    bool IsSnow() const { return is_snow_; }
    bool IsWeatherEnabled() const { return weather_enabled_; }

private:
    // Rain_Render (HOOK 0x498B80, rain.c:207-217): the grid update, the vertex
    // fill, the clip and the 9x9 cell walk, split into the engine's functions.
    void UpdateGrid(const Camera& camera);
    void ClipCells(double z, const Camera& camera);
    void DrawCells(GLuint ubo_mats, const Camera& camera, double z, GLuint snowTexture);

    GLuint rain_program_ = 0;
    GLuint snow_program_ = 0;
    GLuint ubo_binding_point_ = 0;
    GLuint vao_weather_ = 0;
    GLuint vbo_weather_ = 0;

    GroundQuery ground_query_;

    bool active_ = false;
    bool weather_enabled_ = true;
    bool is_snow_ = false;
    // The editor's preview multiplier on the engine's own fall speed. 1.0 is the
    // engine's rate; there is no engine value to diverge from (rain.c:122-123).
    float speed_scale_ = 1.0f;
    // RainEffect_Changed rebuilds the Rain object whenever anything but the
    // alpha changes (fxobj/rain.c:131-146), so the object exists as soon as a
    // level has weather and the fields below are its construction arguments.
    bool object_exists_ = false;
    // The authored Traceline fields in the engine's units: Rain_New's `height`
    // and `fallDist` (rain.c:159-160).
    float height_units_ = 0.0f;
    float fall_dist_units_ = 0.0f;
};
