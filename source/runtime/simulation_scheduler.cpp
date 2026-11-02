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
        if (input_modifier_) {
            input_modifier_(clock_.GetTickCount(), input);
        }
        world_.UpdateSimulationTick(clock_.GetTickCount(), input);
        clock_.CompleteTick();
        // The reference loop renders between the first startup ticks; emulate
        // that here so a single host frame can still run the guarded startup
        // batch instead of advancing one tick per frame until the guard lifts.
        if (clock_.GetTickCount() < GameClock::GUARDED_STARTUP_TICKS) {
            clock_.CompleteRender();
        }
    }

    // The scheduler is called once per presented host frame. Rendering is a
    // presentation boundary, not a simulation step, so refill the clock's
    // one-render latch after the batch. A long stall can therefore run the
    // bounded catch-up burst and then resume from a fresh render boundary.
    clock_.CompleteRender();
}

} // namespace igi
