#pragma once
#include <string>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <functional>

struct DebugCommand {
    std::string type;
    int level = -1;
    std::string modelId;
    bool has_pos = false;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

class App; // Forward declaration

class DebugCommandManager {
public:
    DebugCommandManager(App* app);
    ~DebugCommandManager();

    void Start();
    void Stop();
    void Update();

private:
    void WatcherThread();
    void ProcessCommand(const DebugCommand& cmd);
    void GotoModel(const DebugCommand& cmd);
    void CaptureModel(const DebugCommand& cmd);

    App* app_;
    std::thread watcher_thread_;
    std::atomic<bool> running_;
    std::mutex queue_mutex_;
    std::queue<DebugCommand> command_queue_;
    std::string commands_file_path_;
};
