// task_tree.cpp - Runtime Task Tree, Message Dispatching & Object Hierarchy implementation
#include "task_tree.h"
#include <algorithm>

namespace igi {

GameTask::GameTask(uint32_t task_id, uint16_t type_id, const std::string& name)
    : task_id_(task_id), type_id_(type_id), name_(name) {}

void GameTask::AppendChild(std::shared_ptr<GameTask> child) {
    if (!child) return;
    child->parent_ = this;
    children_.push_back(child);
}

void GameTask::RemoveChild(uint32_t child_id) {
    auto it = std::remove_if(children_.begin(), children_.end(),
        [child_id](const std::shared_ptr<GameTask>& task) {
            return task->GetId() == child_id;
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
    task_map_.clear();
    root_.reset();
}

void TaskTree::RegisterTask(std::shared_ptr<GameTask> task) {
    if (!task) return;
    task_map_[task->GetId()] = task;
    task->OnCreate();
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

    // Clean up marked tasks
    for (auto it = task_map_.begin(); it != task_map_.end();) {
        if (it->second && it->second->IsMarkedForDestruction()) {
            it->second->OnDestroy();
            it = task_map_.erase(it);
        } else {
            ++it;
        }
    }
}

void TaskTree::DispatchMessage(const RuntimeTaskMessage& msg) {
    if (root_) {
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

} // namespace igi
