#pragma once

#include <glm/glm.hpp>

#include <string>

namespace igi {

// Authored Door parameters are kept in the units used by the renderer and the
// mission files: angles are degrees, open time is seconds, and slide offsets
// are already converted to world units.
struct RuntimeDoorDefinition {
    int object_index = -1;
    std::string task_id;
    glm::dvec3 closed_position_units = glm::dvec3(0.0);
    float closed_rotation_radians = 0.0f;
    float maximum_angle_degrees = 0.0f;
    float open_time_seconds = 1.0f;
    glm::vec3 slide_offset_units = glm::vec3(0.0f);
    std::string open_expression;
    std::string close_expression;
    std::string locked_expression;
    std::string open_sound;
    std::string close_sound;
    std::string move_sound;
    bool pickable = false;
};

struct RuntimeDoorSnapshot {
    int object_index = -1;
    std::string task_id;
    glm::dvec3 closed_position_units = glm::dvec3(0.0);
    float closed_rotation_radians = 0.0f;
    float angle_radians = 0.0f;
    float slide_fraction = 0.0f;
    glm::vec3 slide_offset_units = glm::vec3(0.0f);
    bool is_fully_open = false;
    bool is_fully_closed = true;
    bool was_fully_open = false;
    bool was_fully_closed = true;
    bool is_locked = false;
    bool is_picked = false;
    int ticks_open = 0;
};

enum class RuntimeDoorUseState {
    Open,
    Closed,
    Opening,
    Closing,
};

// Fixed-step Door motion port. The state machine deliberately has no renderer
// or audio dependency; both systems consume the same state snapshot owned by
// RuntimeWorld.
class RuntimeDoorState final {
public:
    explicit RuntimeDoorState(RuntimeDoorDefinition definition = {});

    void Toggle();
    void CommandOpen();
    void CommandClosed();
    void Tick();

    float GetAngleRadians() const;
    float GetSlideFraction() const { return slide_fraction_; }
    glm::vec3 GetSlideOffsetUnits() const;

    bool IsFullyOpen() const { return is_fully_open_; }
    bool IsFullyClosed() const { return is_fully_closed_; }
    bool WasFullyOpen() const { return was_fully_open_; }
    bool WasFullyClosed() const { return was_fully_closed_; }
    bool IsMoving() const { return !is_fully_open_ && !is_fully_closed_; }
    int GetTicksOpen() const { return ticks_open_; }
    RuntimeDoorUseState GetUseState() const;

    const RuntimeDoorDefinition& GetDefinition() const { return definition_; }

private:
    RuntimeDoorDefinition definition_;
    float angle_degrees_ = 0.0f;
    float slide_fraction_ = 0.0f;
    bool open_latch_ = false;
    bool is_fully_open_ = false;
    bool is_fully_closed_ = true;
    bool was_fully_open_ = false;
    bool was_fully_closed_ = true;
    int ticks_open_ = 0;
};

} // namespace igi
