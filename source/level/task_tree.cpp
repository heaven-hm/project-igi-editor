// task_tree.cpp - Runtime Task Tree, Message Dispatching & Object Hierarchy implementation
#include "task_tree.h"
#include <algorithm>

namespace igi {

GameTask::GameTask(uint32_t task_id, uint16_t type_id, const std::string& name)
    : task_id_(task_id), type_id_(type_id), name_(name) {}

bool GameTask::AppendChild(std::shared_ptr<GameTask> child) {
    if (!child || child.get() == this || child->parent_ != nullptr) return false;

    // Reject a cycle even when the candidate currently has no parent. The
    // parent chain is the authoritative ownership path for this tree.
    for (GameTask* ancestor = this; ancestor != nullptr; ancestor = ancestor->parent_) {
        if (ancestor == child.get()) return false;
    }
    for (const auto& existing : children_) {
        if (existing && existing->GetId() == child->GetId()) return false;
    }

    child->parent_ = this;
    children_.push_back(child);
    return true;
}

void GameTask::RemoveChild(uint32_t child_id) {
    auto it = std::remove_if(children_.begin(), children_.end(),
        [child_id](const std::shared_ptr<GameTask>& task) {
            if (!task || task->GetId() != child_id) return false;
            task->parent_ = nullptr;
            return true;
        });
    children_.erase(it, children_.end());
}

void ContainerTask::OnUpdate(double delta_seconds) {
    for (auto& child : children_) {
        if (child && child->IsActive() && !child->IsMarkedForDestruction()) {
            child->OnUpdate(delta_seconds);
        }
    }
}

void ContainerTask::OnMessage(const RuntimeTaskMessage& msg) {
    for (auto& child : children_) {
        if (child && child->IsActive() && !child->IsMarkedForDestruction()) {
            child->OnMessage(msg);
        }
    }
}

TaskTree::TaskTree() = default;
TaskTree::~TaskTree() { Clear(); }

void TaskTree::Clear() {
    message_queue_.clear();
    if (root_) {
        DestroyTask(root_);
    }
    task_map_.clear();
    root_.reset();
    last_error_.clear();
}

void TaskTree::DestroyTask(const std::shared_ptr<GameTask>& task) {
    if (!task || task->destroyed_) return;

    // Children are torn down before their parent, matching the task tree's
    // ownership contract and preventing callbacks from observing dead parents.
    for (const auto& child : task->children_) {
        DestroyTask(child);
    }
    if (!task->destroyed_) {
        task->OnDestroy();
        task->destroyed_ = true;
    }
}

void TaskTree::RegisterTask(std::shared_ptr<GameTask> task) {
    if (!task) {
        last_error_ = "Cannot register a null runtime task";
        return;
    }

    auto [it, inserted] = task_map_.emplace(task->GetId(), task);
    if (!inserted && it->second.get() != task.get()) {
        last_error_ = "Duplicate runtime task id " + std::to_string(task->GetId());
        return;
    }
    if (!task->created_) {
        task->OnCreate();
        task->created_ = true;
    }
}

std::shared_ptr<GameTask> TaskTree::FindTask(uint32_t task_id) const {
    auto it = task_map_.find(task_id);
    if (it != task_map_.end()) {
        return it->second;
    }
    return nullptr;
}

void TaskTree::Update(double delta_seconds) {
    FlushMessageQueue();

    if (root_ && root_->IsActive() && !root_->IsMarkedForDestruction()) {
        root_->OnUpdate(delta_seconds);
    }

    // Clean up marked tasks. Unlink before dropping the map's ownership so the
    // parent never retains a stale child pointer during the next tick.
    for (auto it = task_map_.begin(); it != task_map_.end();) {
        if (it->second && it->second->IsMarkedForDestruction()) {
            if (GameTask* parent = it->second->GetParent()) {
                parent->RemoveChild(it->second->GetId());
            }
            DestroyTask(it->second);
            it = task_map_.erase(it);
        } else {
            ++it;
        }
    }
}

void TaskTree::DispatchMessage(const RuntimeTaskMessage& msg) {
    if (msg.target_id != 0) {
        auto target = FindTask(msg.target_id);
        if (target && target->IsActive() && !target->IsMarkedForDestruction()) {
            target->OnMessage(msg);
        }
        return;
    }
    if (root_ && root_->IsActive() && !root_->IsMarkedForDestruction()) {
        root_->OnMessage(msg);
    }
}

void TaskTree::QueueMessage(const RuntimeTaskMessage& msg) {
    message_queue_.push_back(msg);
}

void TaskTree::FlushMessageQueue() {
    std::vector<RuntimeTaskMessage> pending = std::move(message_queue_);
    message_queue_.clear();

    for (const auto& msg : pending) {
        DispatchMessage(msg);
    }
}

bool TaskTree::SetRoot(std::shared_ptr<GameTask> root) {
    if (!root) {
        last_error_ = "Cannot set a null runtime task root";
        return false;
    }
    if (root_ && root_ != root) {
        last_error_ = "Runtime task root is already assigned";
        return false;
    }
    root_ = std::move(root);
    RegisterTask(root_);
    return task_map_.find(root_->GetId()) != task_map_.end();
}

} // namespace igi
