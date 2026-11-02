// simulation_scheduler.h - Fixed-step simulation runner decoupled from frame render rate
#pragma once

#include <cstdint>
#include <functional>
#include <utility>

#include "../game_clock.h"
#include "runtime_world.h"
#include "window_input_router.h"

namespace igi {

class SimulationScheduler {
public:
    using GameplayInputModifier = std::function<void(uint64_t, PlayerInputCmd&)>;

    SimulationScheduler(RuntimeWorld& world, WindowInputRouter& router);

    void SetGameplayInputModifier(GameplayInputModifier input_modifier) {
        input_modifier_ = std::move(input_modifier);
    }

    void Update(int64_t now_milliseconds);
    void Reset();

    GameClock& GetClock() { return clock_; }
    const GameClock& GetClock() const { return clock_; }

private:
    RuntimeWorld& world_;
    WindowInputRouter& router_;
    GameClock clock_;
    GameplayInputModifier input_modifier_;
};

} // namespace igi
