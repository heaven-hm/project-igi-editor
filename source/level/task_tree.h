// task_tree.h - Runtime Task Tree, Message Dispatching & Object Hierarchy
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>

namespace igi {

struct RuntimeTaskMessage {
    uint32_t message_id = 0;
    uint32_t sender_id = 0;
    uint32_t target_id = 0; // zero broadcasts; otherwise delivered to one task
    void* payload = nullptr;
    uint64_t timestamp_tick = 0;
};

class GameTask {
public:
    GameTask(uint32_t task_id, uint16_t type_id, const std::string& name = "");
    virtual ~GameTask() = default;

    uint32_t GetId() const { return task_id_; }
    uint16_t GetTypeId() const { return type_id_; }
    const std::string& GetName() const { return name_; }

    virtual void OnCreate() {}
    virtual void OnUpdate(double delta_seconds) {}
    virtual void OnMessage(const RuntimeTaskMessage& msg) {}
    virtual void OnDestroy() {}

    // Hierarchy management
    bool AppendChild(std::shared_ptr<GameTask> child);
    void RemoveChild(uint32_t child_id);
    const std::vector<std::shared_ptr<GameTask>>& GetChildren() const { return children_; }
    GameTask* GetParent() const { return parent_; }

    // Active state
    bool IsActive() const { return is_active_; }
    void SetActive(bool active) { is_active_ = active; }

    bool IsMarkedForDestruction() const { return marked_for_destruction_; }
    void MarkForDestruction() { marked_for_destruction_ = true; }
    bool HasReceivedCreate() const { return created_; }
    bool HasReceivedDestroy() const { return destroyed_; }

protected:
    friend class TaskTree;

    uint32_t task_id_ = 0;
    uint16_t type_id_ = 0;
    std::string name_;
    GameTask* parent_ = nullptr;
    std::vector<std::shared_ptr<GameTask>> children_;
    bool is_active_ = true;
    bool marked_for_destruction_ = false;
    bool created_ = false;
    bool destroyed_ = false;
};

class ContainerTask : public GameTask {
public:
    ContainerTask(uint32_t task_id, uint16_t type_id, const std::string& name = "")
        : GameTask(task_id, type_id, name) {}

    void OnUpdate(double delta_seconds) override;
    void OnMessage(const RuntimeTaskMessage& msg) override;
};

class TaskTree {
public:
    TaskTree();
    ~TaskTree();

    void Clear();
    void RegisterTask(std::shared_ptr<GameTask> task);
    std::shared_ptr<GameTask> FindTask(uint32_t task_id) const;

    void Update(double delta_seconds);
    void DispatchMessage(const RuntimeTaskMessage& msg);
    void QueueMessage(const RuntimeTaskMessage& msg);
    void FlushMessageQueue();

    bool SetRoot(std::shared_ptr<GameTask> root);
    std::shared_ptr<GameTask> GetRoot() const { return root_; }
    const std::string& GetLastError() const { return last_error_; }

private:
    void DestroyTask(const std::shared_ptr<GameTask>& task);

    std::shared_ptr<GameTask> root_;
    std::unordered_map<uint32_t, std::shared_ptr<GameTask>> task_map_;
    std::vector<RuntimeTaskMessage> message_queue_;
    std::string last_error_;
};

} // namespace igi
