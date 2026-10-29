// test_rail_source_corpus.cpp — the rail overlay's resolver over the SHIPPED
// level data (GitHub issue #59, acceptance criterion 2).
//
// tests/test_rail_polyline.cpp pins the resolver on hand-built `LevelObject`
// lists, which proves the maths but not the loader. This suite closes that gap by
// running the chain the editor actually runs:
//
//   MISSIONS/location0/LEVEL1/objects.qsc
//     -> QSC::Load            (source/level/qsc_parser.cpp)
//     -> LevelObjects::Load   (source/level/level_objects.cpp:673-684, which
//                              pushes each nested FUNC child into its parent's
//                              `childrenIndices` in source order)
//     -> igi::BuildRailSource (source/runtime/rail_source.cpp)
//     -> spline_geometry::RailPolyline
//
// and requiring the result to be the rail the placement pass uses: rail 1500,
// the `SplineObj` every LEVEL1 `Train` names, 12 waypoint children, 11 Hermite
// spans, the same 4 461 592.905424-unit length the placement tests pin
// (tests/test_train_rail.cpp), and a polyline whose joint count is that curve's
// joint count.
//
// WINDOWS LANE ONLY: `LevelObjects::Load` cannot be linked by the host-native
// lane (the host runner reports this suite under "not host-portable"). It
// self-skips when IGI_GAME_PATH is unset or the retail file is absent, so it
// never fails for a missing corpus.

#include <gtest/gtest.h>

#include "level/level_common.h"
#include "level/level_objects.h"
#include "renderer/spline_geometry.h"
#include "runtime/rail_source.h"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace {

// The rail task every LEVEL1 Train names, and the level's only ridden rail.
constexpr const char* kLevel1RailTaskId = "1500";
// 11 spans x the container's "Number of Matrices / Segment" (arg 7 = 20) + 1,
// the editor's documented substitution for a waypoint's own NumAreas.
constexpr size_t kLevel1RailJoints = 11u * 20u + 1u;
// Pinned by TrainRailPlacement.UniformSegmentCountIsANegligibleApproximation
// (tests/test_train_rail.cpp) for the same container-wide-20 construction.
constexpr double kLevel1RailLength = 4461592.905424;

std::filesystem::path Level1ObjectsQsc() {
    const char* root = std::getenv("IGI_GAME_PATH");
    if (root == nullptr || *root == '\0') return {};
    return std::filesystem::path(root) / "MISSIONS" / "location0" / "LEVEL1" / "objects.qsc";
}

}  // namespace

TEST(RailSourceCorpus, RetailLevel1RailIsTheRailTheTrainsRide) {
    const std::filesystem::path qsc_path = Level1ObjectsQsc();
    if (qsc_path.empty() || !std::filesystem::exists(qsc_path)) {
        GTEST_SKIP() << "retail LEVEL1 objects.qsc not found (set IGI_GAME_PATH)";
    }

    QSC qsc;
    qsc.Load(qsc_path.string().c_str());
    ASSERT_FALSE(qsc.HadOverflow()) << "the QSC parser aborted on a pool limit";

    LevelObjects level;
    level.Load(nullptr, &qsc);
    const std::vector<LevelObject>& objects = level.GetObjects();
    ASSERT_FALSE(objects.empty()) << "the loader produced no objects";

    // The rail container, found by the id the level's Trains name - the same
    // lookup `igi::BuildTaskIndex` performs.
    const int rail_index = igi::FindLastTaskIndexById(objects, kLevel1RailTaskId);
    ASSERT_GE(rail_index, 0) << "LEVEL1 declares no task " << kLevel1RailTaskId;
    const LevelObject& rail_object = objects[rail_index];
    EXPECT_TRUE(rail_object.isSplineContainer);
    EXPECT_EQ(rail_object.type, "SplineObj");
    EXPECT_EQ(rail_object.splineSegmentCount, 20);
    EXPECT_FALSE(rail_object.linearSegments);

    // The children are the container's waypoints, in the order the QSC declares
    // them (LevelObjects::LoadRecursive walks the FUNC arguments in order and
    // pushes each child as it parses it).
    ASSERT_EQ(rail_object.childrenIndices.size(), 12u);
    for (int child : rail_object.childrenIndices) {
        ASSERT_GE(child, 0);
        ASSERT_LT(child, static_cast<int>(objects.size()));
        EXPECT_TRUE(objects[child].isSplineWaypoint);
        EXPECT_EQ(objects[child].type, "SplineObjWaypoint");
    }

    const igi::RailSource rail = igi::BuildRailSource(objects, rail_index);
    ASSERT_TRUE(rail.valid);
    EXPECT_EQ(rail.container_index, rail_index);
    EXPECT_EQ(rail.path.joints.size(), kLevel1RailJoints);
    EXPECT_EQ(rail.path.arc.size(), kLevel1RailJoints);
    EXPECT_NEAR(rail.path.length, kLevel1RailLength, 0.01);

    // The endpoints are the AUTHORED first and last waypoint points, which is
    // what `SplineObj_GetWaypointPos` returns and the Elevator "Linear" branch
    // interpolates.
    EXPECT_EQ(rail.first_waypoint, objects[rail_object.childrenIndices.front()].pos);
    EXPECT_EQ(rail.last_waypoint, objects[rail_object.childrenIndices.back()].pos);

    // The drawable polyline is that curve, joint for joint.
    const std::vector<glm::dvec3> polyline = spline_geometry::RailPolyline(rail.path);
    ASSERT_EQ(polyline.size(), kLevel1RailJoints);
    for (size_t i = 0; i < polyline.size(); ++i) {
        EXPECT_EQ(polyline[i], rail.path.joints[i].pos) << "joint " << i;
    }
    double walked = 0.0;
    for (size_t i = 0; i + 1 < polyline.size(); ++i) {
        walked += glm::length(polyline[i + 1] - polyline[i]);
    }
    EXPECT_NEAR(walked, kLevel1RailLength, 0.01);

    // The overlay offers exactly the rails the level's own tasks ride: every
    // LEVEL1 Train names 1500, so nothing else appears - the five rigid
    // `SplineObj` road containers carry task id -1 and no rider.
    const std::vector<int> shown = igi::RailContainersToShow(objects, -1);
    EXPECT_EQ(shown, (std::vector<int>{rail_index}));

    // ...and selecting one of those trains highlights the rail it rides.
    int train_index = -1;
    for (int i = 0; i < static_cast<int>(objects.size()); ++i) {
        if (!objects[i].deleted && objects[i].type == "Train" &&
            objects[i].splineTaskId == kLevel1RailTaskId) {
            train_index = i;
            break;
        }
    }
    ASSERT_GE(train_index, 0) << "LEVEL1 declares no Train riding rail " << kLevel1RailTaskId;
    EXPECT_EQ(igi::SelectedRailContainer(objects, train_index), rail_index);
}
