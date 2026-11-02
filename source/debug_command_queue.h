#pragma once

#include "debug_command.h"

#include <mutex>
#include <queue>
#include <utility>

class DebugCommandQueue {
public:
    void Push(DebugCommand command) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(command));
    }

    bool TryPop(DebugCommand& command) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return false;
        command = std::move(queue_.front());
        queue_.pop();
        return true;
    }

private:
    std::mutex mutex_;
    std::queue<DebugCommand> queue_;
};
