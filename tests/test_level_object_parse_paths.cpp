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

#include "level/level_common.h"
#include "level/level_objects.h"

#include <filesystem>
#include <fstream>

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

// The LOAD path must place a member of the rotating-placement group, not just the line
// parser.  Every member of that group opens with ObjectPos at args 3/4/5 then Real32x9 at
// 6/7/8: RotatingObject src/fxobj/rotating.c:81-82, AlarmLight src/fxobj/alarm.c:109-110,
// Elevator src/elevator/elevator.c:63-64, GenericPickup src/comprun/pickup.c:269-270, and
// Generator/GenericTBA/Radio/Siren through ExplodeObject's base table
// (src/camera/explodeobject.c:204-205).
TEST(LevelObjectParsePathsTest, LoadPathPlacesTheRotatingPlacementGroup) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "igi_rotating_group.qsc";
    {
        std::ofstream out(path, std::ios::trunc);
        ASSERT_TRUE(out.is_open());
        out << "Task_New(6001, \"RotatingObject\", \"fan\", 10.5, -20.25, 30.0, "
               "0.25, 0.5, 0.75, \"fan_model\", \"fan_sound\");\n";
    }

    QSC qsc;
    qsc.Load(path.string().c_str());

    LevelObjects objects;
    objects.Load(nullptr, &qsc);

    const LevelObject* found = nullptr;
    for (const LevelObject& o : objects.GetObjects()) {
        if (o.type == "RotatingObject") found = &o;
    }
    std::filesystem::remove(path);
    ASSERT_NE(found, nullptr) << "the loader produced no RotatingObject";

    EXPECT_DOUBLE_EQ(found->pos.x, 10.5);
    EXPECT_DOUBLE_EQ(found->pos.y, -20.25);
    EXPECT_DOUBLE_EQ(found->pos.z, 30.0);
    // The load path used to read only the generic position candidate, so the authored
    // rotation was lost.
    EXPECT_DOUBLE_EQ(found->rot.x, 0.25);
    EXPECT_DOUBLE_EQ(found->rot.y, 0.5);
    EXPECT_DOUBLE_EQ(found->rot.z, 0.75);
}
