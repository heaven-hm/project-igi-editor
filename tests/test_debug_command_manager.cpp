#include <gtest/gtest.h>
#include <fstream>
#include <thread>
#include <chrono>
#include "../source/debug_command_manager.h"
// Forward declare App to avoid needing full app.h in test
class App {
public:
    int GetCurLevelNo() const { return 1; }
    void LoadLevel(int) {}
};

// We subclass DebugCommandManager to expose some testing abilities,
// but since the queue is private, we will just test the file creation and thread startup.
TEST(DebugCommandManagerTest, TestThreadLifecycle) {
    App app;
    DebugCommandManager mgr(&app);
    
    // Start thread
    mgr.Start();
    
    // Write a dummy command
    {
        std::ofstream out("commands.txt");
        out << "goto level=2 model=999_99_9\n";
    }
    
    // Wait for it to be processed and cleared
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // The file should be empty after being processed
    bool file_empty = true;
    std::ifstream in("commands.txt");
    if (in.is_open()) {
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (!content.empty()) {
            file_empty = false;
        }
    }
    EXPECT_TRUE(file_empty);
    
    // Stop thread
    mgr.Stop();
}
