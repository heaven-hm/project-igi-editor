/******************************************************************************
 * @file    test_level_object_parse_paths.cpp
 * @brief   The two level-object parse paths must agree.
 *
 * LevelObjects has two parsers and they had drifted:
 *
 *   LoadRecursive        source/level/level_objects.cpp          (level loading)
 *   ParseTaskLine        source/level/level_objects_serialize.cpp (single line)
 *
 * Three defects came out of that drift, all found by testing rather than reading:
 *
 *   1. ParseTaskLine never set `isSplineWaypoint` and never read the waypoint's
 *      args 11-14, so an authored span lost its count and its Align/Flip/
 *      Automatic-Orientation flags.
 *   2. The save path wrote only args 3-10, so a save dropped the same parameters.
 *   3. `AIGraph` and `HumanAI` were handled by LoadRecursive only, so a line parsed
 *      through ParseTaskLine had no graph origin, no node count and no graph id.
 *
 * This file guards 3, and pins the argument layout of both types against the fields
 * LoadRecursive sets.  IGI-RE is the oracle throughout.
 ******************************************************************************/

#include <gtest/gtest.h>

#include "level/level_objects.h"

namespace {

// AIGraph: "Graph position" is args 3/4/5 -- the world origin the graph's node
// coordinates are local to -- and arg 7 is Graphdata's node_count (args 7/8/9 are
// node_count, capacity, edge_count).
constexpr const char* kAiGraph =
    "Task_New(5001, \"AIGraph\", \"graph5\", 1234.5, -6789.25, 4242.0, 0, 137, "
    "256, 400)";

// HumanAI: Task_New(id, "HumanAI", name, "AITYPE_*", graphId).
constexpr const char* kHumanAi =
    "Task_New(5002, \"HumanAI\", \"guard1\", \"AITYPE_PATROL\", 5001)";

}  // namespace

// The origin and node count must reach the object through ParseTaskLine, not only
// through the level-loading path.
TEST(LevelObjectParsePathsTest, AiGraphFieldsSurviveTheLineParser) {
    LevelObjects level;
    LevelObject obj;
    level.ParseTaskLine(kAiGraph, obj);

    ASSERT_EQ(obj.type, "AIGraph");
    EXPECT_DOUBLE_EQ(obj.pos.x, 1234.5);
    EXPECT_DOUBLE_EQ(obj.pos.y, -6789.25);
    EXPECT_DOUBLE_EQ(obj.pos.z, 4242.0);
    EXPECT_EQ(obj.graphNodeCount, 137);
    // The name is handled for every Task_New by the shared prologue.
    EXPECT_EQ(obj.name, "graph5");
}

// The graph id a HumanAI patrols must survive the same way.
TEST(LevelObjectParsePathsTest, HumanAiGraphIdSurvivesTheLineParser) {
    LevelObjects level;
    LevelObject obj;
    level.ParseTaskLine(kHumanAi, obj);

    ASSERT_EQ(obj.type, "HumanAI");
    EXPECT_EQ(obj.aiGraphTaskId, 5001);
    EXPECT_EQ(obj.name, "guard1");
}
