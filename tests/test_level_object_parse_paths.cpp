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
#include "level/ai_graph_ground.h"
#include "renderer/graph_writer.h"
#include "utils.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <string>

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

// Moving a graph must persist.  UpdateCoordinatesInLine has no generic fallback -- its
// chain simply ended -- so a type it does not name kept its original tokens, and a moved
// AIGraph silently reverted to its old origin on save.
TEST(LevelObjectParsePathsTest, MovedAiGraphPersistsItsOrigin) {
    LevelObjects level;
    LevelObject obj;
    level.ParseTaskLine(kAiGraph, obj);
    ASSERT_EQ(obj.type, "AIGraph");
    ASSERT_DOUBLE_EQ(obj.pos.x, 1234.5);

    obj.pos.x = 2000.0;
    obj.pos.y = -3000.0;
    obj.pos.z = 4000.0;
    level.UpdateCoordinatesInLine(obj);

    LevelObject reloaded;
    level.ParseTaskLine(level.GenerateTaskLine(obj), reloaded);
    EXPECT_DOUBLE_EQ(reloaded.pos.x, 2000.0);
    EXPECT_DOUBLE_EQ(reloaded.pos.y, -3000.0);
    EXPECT_DOUBLE_EQ(reloaded.pos.z, 4000.0);
    // The node count is not a coordinate and must be left alone.
    EXPECT_EQ(reloaded.graphNodeCount, 137);
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

// AnimTask and CutScene declare "Position" ObjectPos then "Orientation" Real32x9, so their
// rotation sits at args 6/7/8 exactly as the rotating-placement group's does -- and the
// load path read neither.  Retail declaration, MISSIONS/location0/level1/objects.qsc:
//   Task_DeclareParameters("AnimTask", "Position", "ObjectPos", "Orientation", "Real32x9",
//                          "TargetID", "Int32", ...);
TEST(LevelObjectParsePathsTest, LoadPathPlacesAnimTask) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "igi_anim_task.qsc";
    {
        std::ofstream out(path, std::ios::trunc);
        ASSERT_TRUE(out.is_open());
        out << "Task_New(7001, \"AnimTask\", \"walk\", 1.5, 2.5, 3.5, "
               "0.1, 0.2, 0.3, 42);\n";
    }

    QSC qsc;
    qsc.Load(path.string().c_str());

    LevelObjects objects;
    objects.Load(nullptr, &qsc);

    const LevelObject* found = nullptr;
    for (const LevelObject& o : objects.GetObjects()) {
        if (o.type == "AnimTask") found = &o;
    }
    std::filesystem::remove(path);
    ASSERT_NE(found, nullptr) << "the loader produced no AnimTask";

    EXPECT_DOUBLE_EQ(found->pos.x, 1.5);
    EXPECT_DOUBLE_EQ(found->pos.y, 2.5);
    EXPECT_DOUBLE_EQ(found->pos.z, 3.5);
    EXPECT_DOUBLE_EQ(found->rot.x, 0.1);
    EXPECT_DOUBLE_EQ(found->rot.y, 0.2);
    EXPECT_DOUBLE_EQ(found->rot.z, 0.3);
}

// ─── Elevator / moving platform (GitHub issue #40) ──────────────────────────
// `Elevator` is the retail moving platform. `Elevator_Init` HOOK 0x43CC70
// (src/elevator/elevator.c:44-83) declares, in registration order: "Position"
// ObjectPos, "Orientation" Real32x9, "Model" String16, "SplineObj taskid" Int32,
// "Linear" bool8, "Force orientation" bool8, "Pos" Real32, "Max speed" Real32,
// "Speed inertia" Real32, "Floor down"/"Floor up"/"Can start" VarString,
// "Wanted floor 0..9" VarString, "Start/Stop/Move sound" String32.
// Param_ObjectPos is 3 script tokens and Param_Real32x9 is 3
// (ParamReal32x9_Parse HOOK 0x4EFBA0, src/physobj/paramops.c:423-435), so the
// QSC indices are 3 + N: rail id 10, Linear 11, Force orientation 12, Pos 13.
//
// The fixture is the shipped LEVEL3 call, as the editor's own decompiler emits
// it, with rail 699 declared immediately above it:
//   Task_New(698, "Elevator", "", 24557880.0, -55710968.0, 174421536.0, 0, 0, 0,
//            "200_01_1", 699, TRUE, FALSE, 0, 5.0, 1, "", "", <expr>, <expr>,
//            <expr>, "", "", "", "", "", "", "", "", "elv_start_1", "elv_stop_1",
//            "elv_move_1")
constexpr const char* kRetailElevator =
    "Task_New(698, \"Elevator\", \"\", 24557880.0, -55710968.0, 174421536.0, "
    "0, 0, 0, \"200_01_1\", 699, TRUE, FALSE, 0, 5.0, 1, \"\", \"\", "
    "\"Door_689.isClosed && Door_690.isClosed\", "
    "\"(Elevator_698.vFloor == 1 && Switch_697.isLastPressed)\", "
    "\"(Elevator_698.vFloor == 0 && Switch_697.isLastPressed)\", "
    "\"\", \"\", \"\", \"\", \"\", \"\", \"\", \"\", "
    "\"elv_start_1\", \"elv_stop_1\", \"elv_move_1\")";

TEST(LevelObjectParsePathsTest, ElevatorRailAndFlagsSurviveTheLineParser) {
    LevelObjects level;
    LevelObject obj;
    level.ParseTaskLine(kRetailElevator, obj);

    ASSERT_EQ(obj.type, "Elevator");
    EXPECT_EQ(obj.splineTaskId, "699");
    EXPECT_TRUE(obj.elevatorLinear);
    EXPECT_FALSE(obj.elevatorForceOrientation);
    EXPECT_DOUBLE_EQ(obj.elevatorStartHeight, 0.0);
    // The ObjectPos/Orientation half must still come through unchanged.
    EXPECT_DOUBLE_EQ(obj.pos.x, 24557880.0);
    EXPECT_DOUBLE_EQ(obj.pos.y, -55710968.0);
    EXPECT_DOUBLE_EQ(obj.pos.z, 174421536.0);
}

TEST(LevelObjectParsePathsTest, ElevatorRailAndFlagsRoundTripThroughTheWriter) {
    LevelObjects level;
    LevelObject obj;
    level.ParseTaskLine(kRetailElevator, obj);
    ASSERT_EQ(obj.splineTaskId, "699");

    // Edit the three editable fields the way the inspector would, then save.
    obj.elevatorStartHeight = 12345.5;
    obj.elevatorLinear = false;
    obj.elevatorForceOrientation = true;
    level.UpdateCoordinatesInLine(obj);

    LevelObject reloaded;
    level.ParseTaskLine(level.GenerateTaskLine(obj), reloaded);
    EXPECT_EQ(reloaded.type, "Elevator");
    EXPECT_EQ(reloaded.splineTaskId, "699");
    EXPECT_FALSE(reloaded.elevatorLinear);
    EXPECT_TRUE(reloaded.elevatorForceOrientation);
    EXPECT_DOUBLE_EQ(reloaded.elevatorStartHeight, 12345.5);
    // A field the writer must not touch keeps its authored value.
    EXPECT_DOUBLE_EQ(reloaded.pos.z, 174421536.0);
}

TEST(LevelObjectParsePathsTest, LoadPathReadsTheElevatorRail) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "igi_elevator.qsc";
    {
        std::ofstream out(path, std::ios::trunc);
        ASSERT_TRUE(out.is_open());
        out << kRetailElevator << ";\n";
    }

    QSC qsc;
    qsc.Load(path.string().c_str());

    LevelObjects objects;
    objects.Load(nullptr, &qsc);

    const LevelObject* found = nullptr;
    for (const LevelObject& o : objects.GetObjects()) {
        if (o.type == "Elevator") found = &o;
    }
    std::filesystem::remove(path);
    ASSERT_NE(found, nullptr) << "the loader produced no Elevator";

    EXPECT_EQ(found->splineTaskId, "699");
    EXPECT_TRUE(found->elevatorLinear);
    EXPECT_FALSE(found->elevatorForceOrientation);
    EXPECT_DOUBLE_EQ(found->elevatorStartHeight, 0.0);
}

// ── The attached-graph AI placement, through the editor's own loaders ────────
//
// A shipped case: LEVEL4 objects.qsc:543-545 is
//
//   Building "Anya HQ"  Position (42171.9453125, -1427.2568359375, 0)
//                       Orientation (0, 0, 0)
//     AIGraph 6         Graph position (30065.96484375, -21388.2421875, 12842.060546875)
//     HumanSoldier      Position (68105.2265625, 432.6167907714844, 8105.5546875)
//       HumanAI         Graph ID 6
//
// and graph6.dat's only node is (2165.2885811546294, 2740.26705613601,
// -7582.960178162124).  Graph_GetNodePos's attached branch (HOOK 0x4F9770,
// src/graphnode/graph.c:345-362) computes
//
//   world.z = parent.pos.z + M(Orientation) * (graph.pos + node.pos)
//           = 0 + 12842.060546875 + (-7582.960178162124)
//           = 5259.100368712876
//
// so Human_ProbeGround (HOOK 0x462390, src/humanbase/human.c:1714-1717) moves
// the human 0.695 m down from its authored 8105.5546875.  This test proves the
// four numbers the editor actually feeds that rule come from the shipped files
// through LoadRecursive (not from hand-written constants).
TEST(LevelObjectParsePathsTest, AttachedBuildingGraphFeedsTheAiGroundRule) {
    const std::filesystem::path level_dir =
        std::filesystem::path(Utils::GetIGIRootPath()) / "missions" / "location0" / "LEVEL4";
    const std::filesystem::path qsc_path = level_dir / "objects.qsc";
    if (!std::filesystem::exists(qsc_path)) {
        GTEST_SKIP() << "retail LEVEL4 objects.qsc not installed: " << qsc_path;
    }

    QSC qsc;
    qsc.Load(qsc_path.string().c_str());
    LevelObjects objects;
    objects.Load(nullptr, &qsc);
    const std::vector<LevelObject>& list = objects.GetObjects();

    int building = -1, graph = -1, human_ai = -1;
    for (int i = 0; i < static_cast<int>(list.size()); ++i) {
        const LevelObject& o = list[static_cast<std::size_t>(i)];
        if (o.type == "Building" && o.name == "Anya HQ") building = i;
        if (o.type == "AIGraph" && o.taskId == "6") graph = i;
        if (o.type == "HumanAI" && o.aiGraphTaskId == 6) human_ai = i;
    }
    ASSERT_GE(building, 0) << "Building \"Anya HQ\" not found";
    ASSERT_GE(graph, 0) << "AIGraph 6 not found";
    ASSERT_GE(human_ai, 0) << "HumanAI with Graph ID 6 not found";

    // Graph_Create 0x4F90B0 attaches the graph when its DIRECT parent is the
    // RigidDynCubeObj (src/graphnode/graph.c:65-69): the AIGraph must be a
    // direct child of the Building, and the human likewise.
    ASSERT_EQ(list[static_cast<std::size_t>(graph)].parentIndex, building);
    ASSERT_GE(list[static_cast<std::size_t>(human_ai)].parentIndex, 0);
    const LevelObject& human =
        list[static_cast<std::size_t>(list[static_cast<std::size_t>(human_ai)].parentIndex)];
    ASSERT_EQ(human.type, "HumanSoldier");
    ASSERT_EQ(human.parentIndex, building);

    const LevelObject& b = list[static_cast<std::size_t>(building)];
    const LevelObject& g = list[static_cast<std::size_t>(graph)];
    EXPECT_DOUBLE_EQ(b.pos.x, 42171.9453125);
    EXPECT_DOUBLE_EQ(b.pos.y, -1427.2568359375);
    EXPECT_DOUBLE_EQ(b.pos.z, 0.0);
    EXPECT_DOUBLE_EQ(b.rot.x, 0.0);
    EXPECT_DOUBLE_EQ(b.rot.y, 0.0);
    EXPECT_DOUBLE_EQ(b.rot.z, 0.0);
    EXPECT_DOUBLE_EQ(g.pos.x, 30065.96484375);
    EXPECT_DOUBLE_EQ(g.pos.y, -21388.2421875);
    EXPECT_DOUBLE_EQ(g.pos.z, 12842.060546875);
    EXPECT_DOUBLE_EQ(human.pos.x, 68105.2265625);
    EXPECT_DOUBLE_EQ(human.pos.y, 432.6167907714844);
    EXPECT_DOUBLE_EQ(human.pos.z, 8105.5546875);

    std::filesystem::path dat = level_dir / "GRAPHS" / "GRAPH6.DAT";
    if (!std::filesystem::exists(dat)) dat = level_dir / "graphs" / "graph6.dat";
    if (!std::filesystem::exists(dat)) {
        GTEST_SKIP() << "retail graph6.dat not installed: " << dat;
    }
    const GraphFile parsed = GRAPH_Parse(dat.string());
    ASSERT_TRUE(parsed.valid) << parsed.error;

    const double origin[3] = {g.pos.x, g.pos.y, g.pos.z};
    const double parent_pos[3] = {b.pos.x, b.pos.y, b.pos.z};
    const double parent_euler[3] = {b.rot.x, b.rot.y, b.rot.z};
    const double human_pos[3] = {human.pos.x, human.pos.y, human.pos.z};
    const igi::AiGraphGround r =
        igi::AiGraphGroundZAttached(parsed, origin, parent_pos, parent_euler, human_pos);
    ASSERT_TRUE(r.found);
    EXPECT_EQ(r.node_id, 1);
    EXPECT_NEAR(r.world_z, 5259.100368712876, 1e-6);
    EXPECT_NEAR(r.world_z - human.pos.z, -2846.454318787124, 1e-6);
}

// The census behind the rule: in the three shipped levels whose objects.qsc is
// unpacked here, exactly 18 AI humans patrol an ATTACHED graph, and every one
// of those graphs has a Heli/Car/Train/Building direct parent:
//
//   level1   Heli 1;  Train 2, 3, 6;  Car 15      = 5
//   LEVEL2   Heli 17;  Car 15                      = 2
//   LEVEL4   Heli 8 (2), Heli 9 (3), Heli 12 (2),
//            Heli 13 (3), Building 6               = 11
//                                              total 18
//
// The editor evaluates the attached Z for Heli/Car/Building (15 of them) and
// keeps the authored Z for the 3 Train ones, whose pose is the spline at
// start_dist rather than an ObjectPos.
TEST(LevelObjectParsePathsTest, ShippedAttachedGraphCensus) {
    struct LevelCensus {
        const char* dir;
        int heli, car, train, building;
    };
    const LevelCensus levels[] = {
        {"level1", 1, 1, 3, 0},
        {"LEVEL2", 1, 1, 0, 0},
        {"LEVEL4", 10, 0, 0, 1},
    };

    int total_heli = 0, total_car = 0, total_train = 0, total_building = 0;
    int total_attached = 0;
    int evaluated = 0;
    for (const LevelCensus& level : levels) {
        const std::filesystem::path qsc_path =
            std::filesystem::path(Utils::GetIGIRootPath()) / "missions" / "location0" /
            level.dir / "objects.qsc";
        if (!std::filesystem::exists(qsc_path)) {
            GTEST_SKIP() << "retail objects.qsc not installed: " << qsc_path;
        }
        QSC qsc;
        qsc.Load(qsc_path.string().c_str());
        LevelObjects objects;
        objects.Load(nullptr, &qsc);
        const std::vector<LevelObject>& list = objects.GetObjects();

        // AIGraph task id -> its direct parent's type.
        std::map<std::string, std::string> graph_parent;
        for (const LevelObject& o : list) {
            if (o.type != "AIGraph") continue;
            const std::string parent_type =
                (o.parentIndex >= 0 && o.parentIndex < static_cast<int>(list.size()))
                    ? list[static_cast<std::size_t>(o.parentIndex)].type
                    : std::string();
            graph_parent[o.taskId] = parent_type;
        }

        int heli = 0, car = 0, train = 0, building = 0;
        for (const LevelObject& o : list) {
            if (o.type != "HumanAI" || o.aiGraphTaskId < 0) continue;
            auto it = graph_parent.find(std::to_string(o.aiGraphTaskId));
            if (it == graph_parent.end()) continue;
            const std::string& parent = it->second;
            if (igi::GraphParentIsContainer(parent)) continue;
            if (parent == "Heli") heli++;
            else if (parent == "Car") car++;
            else if (parent == "Train") train++;
            else if (parent == "Building") building++;
            if (igi::GraphParentPoseIsAuthoredAtProbe(parent)) evaluated++;
        }

        EXPECT_EQ(heli, level.heli) << level.dir;
        EXPECT_EQ(car, level.car) << level.dir;
        EXPECT_EQ(train, level.train) << level.dir;
        EXPECT_EQ(building, level.building) << level.dir;
        total_heli += heli;
        total_car += car;
        total_train += train;
        total_building += building;
        total_attached += heli + car + train + building;
    }

    EXPECT_EQ(total_attached, 18);
    EXPECT_EQ(total_heli, 12);
    EXPECT_EQ(total_car, 2);
    EXPECT_EQ(total_train, 3);
    EXPECT_EQ(total_building, 1);
    // Heli + Car + Building are evaluated; the 3 Train placements keep the
    // authored Z because a Train has no ObjectPos.
    EXPECT_EQ(evaluated, total_attached - total_train);
}
