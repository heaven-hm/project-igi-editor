// simulation_scheduler.cpp - Fixed-step simulation runner decoupled from frame render rate implementation
#include "simulation_scheduler.h"

namespace igi {

SimulationScheduler::SimulationScheduler(RuntimeWorld& world, WindowInputRouter& router)
    : world_(world), router_(router) {}

void SimulationScheduler::Reset() {
    clock_.Reset();
}

void SimulationScheduler::Update(int64_t now_milliseconds) {
    clock_.Update(now_milliseconds);

    while (clock_.IsTickDue()) {
        PlayerInputCmd input = router_.ConsumeGameplayInput();
        world_.UpdateSimulationTick(clock_.GetTickCount(), input);
        clock_.CompleteTick();
    }
}

} // namespace igi
