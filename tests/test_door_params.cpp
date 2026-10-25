// Differential test for the `Door` argument layout against the shipped retail
// scripts.
//
// IGI-RE is the oracle. `src/door/door.c:257-269` registers the parameter table in
// this order, and `ParamReal32x9_Parse` (HOOK 0x4EFBA0,
// `src/physobj/paramops.c:423-436`) consumes exactly THREE arguments for the
// `Real32x9` orientation, not nine:
//
//     3,4,5  place (ObjectPos)      9,10,11  base (Real32x9 -> Euler triple)
//     6      slide[0]              12        model
//     7      slide[1]              13        openAngle (degrees)
//     8      slidePos              14        openTime (seconds)
//                                  15        pickable
//                                  16        pickTime
//                                  17        lockExpr
//                                  18        openExpr
//                                  19        closeExpr
//                                  20,21,22  open/close/move sound
//
// The last test replays every `Door` task in the installed retail scripts, so the
// assertions are made against real data rather than a hand-written fixture.

#include "../source/level/level_objects.h"
#include "../source/runtime/door_params.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cmath>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

// The editor's units per metre, as passed by App::SetupRuntimeDoors
// (`igi::PlayerController::WORLD_METER`, source/player_controller.h:60). It is
// also the engine's own scale: IGI-RE `kSlideScale` is aliased to `kMeter` and
// `k4096` at 0x5333B8 (`src/addr.c:25-29`), and `Door_Place` multiplies the slide
// by it (src/door/door.c:125-126). So the editor's conversion is the engine's.
constexpr float kUnitsPerMetre = 4096.0f;

igi::RuntimeDoorDefinition Parse(const std::string& line, LevelObjects& level) {
    LevelObject object;
    level.ParseTaskLine(line, object);
    igi::RuntimeDoorDefinition definition;
    igi::ApplyDoorArgumentLayout(object.argTokens, kUnitsPerMetre, definition);
    return definition;
}

// The first `Door` in the shipped MISSIONS/location0/LEVEL1/objects.qsc, copied
// verbatim. Its yaw is 1.570796 rad in argument 11 and its model is argument 12,
// which is what pins `Real32x9` to three arguments.
const char* kRetailDoorLine =
    "Task_New(-1, \"Door\", \"\", 24505586.0, -55955580.0, 174414224.0, -1, 0, 0, 0, 0, "
    "1.570796012878418, \"500_01_1\", 0, 1, FALSE, 4.0, \"\", \"\", \"\", \"door_open_1\", "
    "\"door_close_1\", \"door_slide_1\")";

// A door with the fields the retail corpus uses: a nonzero slide on X, an open
// expression on a switch, and a lock expression.
const char* kScriptedDoorLine =
    "Task_New(77, \"Door\", \"\", 1.0, 2.0, 3.0, -1, 2.0, 0, 0, 0, 4.7123889923095703, "
    "\"503_01_1\", 90.0, 2.0, TRUE, 4.0, \"1\", "
    "\"Switch_506.isLastPressed || Door_409.isOpen\", \"\", \"door_open_1\", "
    "\"door_close_1\", \"door_slide_1\")";

std::string FindRetailScriptsRoot() {
    const char* candidates[] = {std::getenv("IGI_GAME_PATH"), "/Users/haseeb-mir/Downloads/IGI",
                                nullptr};
    for (int i = 0; candidates[i] != nullptr; ++i) {
        const std::string root = candidates[i];
        if (root.empty()) {
            continue;
        }
        std::ifstream probe(root + "/MISSIONS/location0/LEVEL1/objects.qsc");
        if (probe.good()) {
            return root;
        }
    }
    return {};
}

std::vector<std::string> DoorLinesIn(const std::string& path) {
    std::vector<std::string> lines;
    std::ifstream file(path);
    if (!file.good()) {
        return lines;
    }
    std::string line;
    while (std::getline(file, line)) {
        // The task id is not always -1, so match the type argument rather than a
        // literal prefix.
        if (line.find("Task_New(") == std::string::npos) {
            continue;
        }
        if (line.find("\"Door\"") == std::string::npos) {
            continue;
        }
        lines.push_back(line);
    }
    return lines;
}

}  // namespace

TEST(DoorParamsTest, OrientationIsAnEulerTripleSoTheModelIsArgumentTwelve) {
    LevelObjects level;
    const igi::RuntimeDoorDefinition d = Parse(kRetailDoorLine, level);
    // Argument 12 is the model; if `Real32x9` consumed nine arguments the model
    // would sit at argument 18 and every later field would shift with it.
    EXPECT_FLOAT_EQ(d.slide_offset_units.x, -1.0f * kUnitsPerMetre);
    EXPECT_FLOAT_EQ(d.slide_offset_units.y, 0.0f);
    EXPECT_FLOAT_EQ(d.slide_offset_units.z, 0.0f);
    EXPECT_FLOAT_EQ(d.maximum_angle_degrees, 0.0f);
    EXPECT_FLOAT_EQ(d.open_time_seconds, 1.0f);
    EXPECT_FALSE(d.pickable);
    EXPECT_EQ(d.locked_expression, "");
    EXPECT_EQ(d.open_expression, "");
    EXPECT_EQ(d.close_expression, "");
    EXPECT_EQ(d.open_sound, "door_open_1");
    EXPECT_EQ(d.close_sound, "door_close_1");
    EXPECT_EQ(d.move_sound, "door_slide_1");
}

TEST(DoorParamsTest, SlideHasNoZComponentAndArgumentEightIsNotAnOffset) {
    LevelObjects level;
    // Argument 8 is `slidePos`, the initial open fraction. A door authored with a
    // nonzero slider must not gain a vertical component.
    const std::string line =
        "Task_New(-1, \"Door\", \"\", 0.0, 0.0, 0.0, 0.0, 0.0, 0.75, 0.0, 0.0, 0.0, "
        "\"500_01_1\", 0, 1, FALSE, 4.0, \"\", \"\", \"\", \"a\", \"b\", \"c\")";
    const igi::RuntimeDoorDefinition d = Parse(line, level);
    EXPECT_FLOAT_EQ(d.slide_offset_units.z, 0.0f);
    EXPECT_FLOAT_EQ(d.slide_offset_units.x, 0.0f);
    EXPECT_FLOAT_EQ(d.slide_offset_units.y, 0.0f);
}

TEST(DoorParamsTest, ExpressionsAreLockOpenCloseInThatOrder) {
    LevelObjects level;
    const igi::RuntimeDoorDefinition d = Parse(kScriptedDoorLine, level);
    // Reading these one slot early put the lock expression in `open_expression`
    // and the open expression in `close_expression`.
    EXPECT_EQ(d.locked_expression, "1");
    EXPECT_EQ(d.open_expression, "Switch_506.isLastPressed || Door_409.isOpen");
    EXPECT_EQ(d.close_expression, "");
    EXPECT_FLOAT_EQ(d.maximum_angle_degrees, 90.0f);
    EXPECT_FLOAT_EQ(d.open_time_seconds, 2.0f);
    EXPECT_TRUE(d.pickable);
    EXPECT_FLOAT_EQ(d.slide_offset_units.x, -1.0f * kUnitsPerMetre);
    EXPECT_FLOAT_EQ(d.slide_offset_units.y, 2.0f * kUnitsPerMetre);
}

TEST(DoorParamsTest, MissingArgumentsFallBackInsteadOfReadingGarbage) {
    LevelObjects level;
    const std::string truncated =
        "Task_New(-1, \"Door\", \"\", 1.0, 2.0, 3.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0)";
    const igi::RuntimeDoorDefinition d = Parse(truncated, level);
    EXPECT_FLOAT_EQ(d.maximum_angle_degrees, 0.0f);
    EXPECT_FLOAT_EQ(d.open_time_seconds, 1.0f);  // the engine's default
    EXPECT_FALSE(d.pickable);
    EXPECT_EQ(d.open_expression, "");
    EXPECT_EQ(d.move_sound, "");
}

TEST(DoorParamsTest, EveryRetailDoorTaskKeepsItsSoundsAndSlideInTheDocumentedSlots) {
    const std::string root = FindRetailScriptsRoot();
    if (root.empty()) {
        GTEST_SKIP() << "retail corpus not present (set IGI_GAME_PATH)";
    }

    LevelObjects level;
    int doors = 0;
    int with_lock_expression = 0;
    int with_open_expression = 0;
    int nonzero_slider = 0;
    int with_sounds = 0;
    for (int level_index = 1; level_index <= 14; ++level_index) {
        std::ostringstream path;
        path << root << "/MISSIONS/location0/LEVEL" << level_index << "/objects.qsc";
        for (const std::string& line : DoorLinesIn(path.str())) {
            LevelObject object;
            level.ParseTaskLine(line, object);
            ASSERT_GE(object.argTokens.size(), 23u) << line;
            igi::RuntimeDoorDefinition d;
            igi::ApplyDoorArgumentLayout(object.argTokens, kUnitsPerMetre, d);

            ++doors;
            // Argument 11 is the yaw: every retail door is axis-aligned to a
            // quarter turn, which only holds if the orientation triple starts at 9.
            const float yaw = std::stof(object.argTokens[11]);
            const float quarter_turn = 1.570796012878418f;
            const float remainder = std::fabs(yaw / quarter_turn -
                                              std::round(yaw / quarter_turn));
            EXPECT_LT(remainder, 1e-4f) << "unexpected door yaw " << yaw << " in " << line;

            // Argument 12 is the model, and it is a quoted name, not a number.
            EXPECT_EQ(object.argTokens[12].front(), '"') << line;

            // Sounds live at 20/21/22. Not every retail door names them -- some
            // carry three empty strings -- so the layout is pinned by the doors
            // that do name them, counted below rather than asserted per line.
            if (!d.open_sound.empty() || !d.close_sound.empty() || !d.move_sound.empty()) {
                ++with_sounds;
                EXPECT_FALSE(d.move_sound.empty()) << line;
            }

            // The offset has no vertical component, whatever the author wrote in
            // argument 8.
            EXPECT_FLOAT_EQ(d.slide_offset_units.z, 0.0f) << line;

            if (!d.locked_expression.empty()) ++with_lock_expression;
            if (!d.open_expression.empty()) ++with_open_expression;
            if (std::stof(object.argTokens[8]) != 0.0f) ++nonzero_slider;
        }
    }

    ASSERT_GT(doors, 0) << "no Door tasks found under " << root;
    // Measured on the installed corpus: 33 Door tasks, 6 carrying lockExpr = "1"
    // and 6 carrying a real openExpr. The point of the assertions is that the
    // mapping reads them from the documented slots.
    EXPECT_GE(with_lock_expression, 1);
    EXPECT_GE(with_open_expression, 1);
    EXPECT_GE(with_sounds, 1) << "no door named a sound; arguments 20/21/22 may have moved";
    EXPECT_EQ(nonzero_slider, 0) << "a retail door now uses argument 8; re-check the layout";
    // Measured on the installed corpus when this test was written: 33 Door tasks,
    // 6 with lockExpr = "1", 6 with an openExpr, 31 naming sounds, and argument 8
    // zero in all of them. The counts are reported so a corpus change is visible
    // rather than silently absorbed.
    std::cout << "retail Door tasks: " << doors << ", with lockExpr: " << with_lock_expression
              << ", with openExpr: " << with_open_expression
              << ", with sounds: " << with_sounds << "\n";
}
