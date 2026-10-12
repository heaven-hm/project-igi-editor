// ai_events.cpp - AI Stimulus and Perception Event Queues implementation
#include "ai_events.h"

namespace igi {

AiEventQueue::AiEventQueue() = default;

void AiEventQueue::Post(const AiStimulusEvent& evt) {
    events_.push_back(evt);
}

void AiEventQueue::Pump(std::vector<AiStimulusEvent>& out_events) {
    out_events = std::move(events_);
    events_.clear();
}

void AiEventQueue::Clear() {
    events_.clear();
}

} // namespace igi
