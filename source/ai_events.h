// ai_events.h - AI Stimulus and Perception Event Queues
#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace igi {

enum class AiEventType {
    Gunshot,
    Footstep,
    GroundImpact,
    VisualAlert,
    AlarmTriggered
};

struct AiStimulusEvent {
    AiEventType type = AiEventType::Footstep;
    glm::vec3 position = glm::vec3(0.0f);
    float loudness = 1.0f;
    float hearing_radius_units = 0.0f;
    uint32_t originator_id = 0;
    uint64_t tick_timestamp = 0;
};

class AiEventQueue {
public:
    AiEventQueue();

    void Post(const AiStimulusEvent& evt);
    void Pump(std::vector<AiStimulusEvent>& out_events);
    void Clear();

    size_t PendingCount() const { return events_.size(); }

private:
    std::vector<AiStimulusEvent> events_;
};

} // namespace igi
