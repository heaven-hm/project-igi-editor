/******************************************************************************
 * @file    test_spline_waypoint_params.cpp
 * @brief   SplineObjWaypoint QSC argument layout, against IGI-RE and the shipped
 *          retail level scripts.
 *
 * IGI-RE is the oracle.  `SplineObj_Open` (HOOK 0x502250, src/editobj/spline.c:
 * 184-225) registers the waypoint parameter table at :215 with eight entries, in
 * this order, and the retail `objects.qsc` declaration repeats it verbatim:
 *
 *   IGI-RE field       retail name              QSC arg
 *   ----------------   ------------------------  -------
 *   w.rot   Real32x9   "Orientation"             3,4,5
 *   w.point ObjectPos  "Position"                6,7,8
 *   w.modelName        "Waypoint Model"          9
 *   w.segModel         "Segment Model"           10
 *   w.segments Int32   "NumAreas"                11
 *   w.straight Bool8   "Align"                   12
 *   w.reversed Bool8   "Flip"                    13
 *   w.autoOrient Bool8 "Automatic Orientation"   14
 *
 * (src/editobj/editobjtypes.h:6-25 gives the fields.)  SplineObjWaypoint_Changed
 * (HOOK 0x500EF0, src/tagitem/spline.c:769-772) then derives
 * `rigid = container.rigid || straight` and caps the count at the container's
 * maxSegments and at 20.
 *
 * Why the bools need their own cases: every shipped waypoint authors
 * FALSE, FALSE, FALSE, so the retail corpus alone cannot tell Align from Flip.
 * The synthetic rows below set them one at a time, which is the only way to pin
 * the mapping.
 ******************************************************************************/

#include <gtest/gtest.h>

#include "level/level_objects.h"

namespace {

// The first SplineObjWaypoint in the shipped
// MISSIONS/location0/level1/objects.qsc, copied verbatim.
constexpr const char* kRetailLevel1Waypoint =
    "Task_New(-1, \"SplineObjWaypoint\", \"\", 0, 0, 3.1415929794311523, "
    "25287152.0, -55948040.0, 174410960.0, \"waypoint\", \"368_01_1\", 20, "
    "FALSE, FALSE, FALSE)";

// Same layout, with the three trailing bools set one at a time.
constexpr const char* kAlignOnly =
    "Task_New(-1, \"SplineObjWaypoint\", \"\", 0, 0, 0, 0, 0, 0, \"waypoint\", "
    "\"368_01_1\", 10, TRUE, FALSE, FALSE)";
constexpr const char* kFlipOnly =
    "Task_New(-1, \"SplineObjWaypoint\", \"\", 0, 0, 0, 0, 0, 0, \"waypoint\", "
    "\"368_01_1\", 10, FALSE, TRUE, FALSE)";
constexpr const char* kAutoOrientOnly =
    "Task_New(-1, \"SplineObjWaypoint\", \"\", 0, 0, 0, 0, 0, 0, \"waypoint\", "
    "\"368_01_1\", 10, FALSE, FALSE, TRUE)";

}  // namespace

// The shipped line must land on the documented layout.
TEST(SplineWaypointParamsTest, RetailLevel1LineMatchesTheEngineLayout) {
    LevelObjects level;
    LevelObject obj;
    level.ParseTaskLine(kRetailLevel1Waypoint, obj);

    ASSERT_EQ(obj.type, "SplineObjWaypoint");
    EXPECT_TRUE(obj.isSplineWaypoint);

    // arg 11 "NumAreas" -> SplineObjWaypoint::segments.
    EXPECT_EQ(obj.splineSegments, 20);
    // arg 9 / 10 -> modelName / segModel.
    EXPECT_EQ(obj.modelId, "");
    EXPECT_EQ(obj.segmentModelId, "368_01_1");
    // The retail row authors all three bools FALSE.
    EXPECT_FALSE(obj.splineStraight);
    EXPECT_FALSE(obj.splineReversed);
    EXPECT_FALSE(obj.splineAutoOrient);
}

// arg 12 "Align" is `straight` -- not `reversed`, which sits next to it in both
// the ParamTable and the declaration.
TEST(SplineWaypointParamsTest, Arg12IsStraightNotReversed) {
    LevelObjects level;
    LevelObject obj;
    level.ParseTaskLine(kAlignOnly, obj);

    EXPECT_TRUE(obj.splineStraight);
    EXPECT_FALSE(obj.splineReversed);
    EXPECT_FALSE(obj.splineAutoOrient);
    EXPECT_EQ(obj.splineSegments, 10);
}

// arg 13 "Flip" is `reversed`.
TEST(SplineWaypointParamsTest, Arg13IsReversed) {
    LevelObjects level;
    LevelObject obj;
    level.ParseTaskLine(kFlipOnly, obj);

    EXPECT_FALSE(obj.splineStraight);
    EXPECT_TRUE(obj.splineReversed);
    EXPECT_FALSE(obj.splineAutoOrient);
}

// arg 14 "Automatic Orientation" is `autoOrient`.
TEST(SplineWaypointParamsTest, Arg14IsAutoOrient) {
    LevelObjects level;
    LevelObject obj;
    level.ParseTaskLine(kAutoOrientOnly, obj);

    EXPECT_FALSE(obj.splineStraight);
    EXPECT_FALSE(obj.splineReversed);
    EXPECT_TRUE(obj.splineAutoOrient);
}
