#include <gtest/gtest.h>

#include "debug_command_queue.h"

#include <thread>

TEST(DebugCommandQueueTest, IsEmptyUntilPushed) {
    DebugCommandQueue queue;
    DebugCommand command;
    EXPECT_FALSE(queue.TryPop(command));
}

TEST(DebugCommandQueueTest, PreservesFifoOrder) {
    DebugCommandQueue queue;
    queue.Push(DebugCommand{"first"});
    queue.Push(DebugCommand{"second"});
    DebugCommand command;
    ASSERT_TRUE(queue.TryPop(command));
    EXPECT_EQ(command.type, "first");
    ASSERT_TRUE(queue.TryPop(command));
    EXPECT_EQ(command.type, "second");
    EXPECT_FALSE(queue.TryPop(command));
}

TEST(DebugCommandQueueTest, PreservesOrderWithinEachProducer) {
    DebugCommandQueue queue;
    std::thread first([&] {
        for (int value = 0; value < 100; ++value)
            queue.Push(DebugCommand{"first", -1, value});
    });
    std::thread second([&] {
        for (int value = 0; value < 100; ++value)
            queue.Push(DebugCommand{"second", -1, value});
    });
    first.join();
    second.join();

    int next_first = 0;
    int next_second = 0;
    DebugCommand command;
    int count = 0;
    while (queue.TryPop(command)) {
        if (command.type == "first") EXPECT_EQ(command.val, next_first++);
        else if (command.type == "second") EXPECT_EQ(command.val, next_second++);
        else ADD_FAILURE() << "unexpected producer " << command.type;
        ++count;
    }
    EXPECT_EQ(count, 200);
    EXPECT_EQ(next_first, 100);
    EXPECT_EQ(next_second, 100);
}
