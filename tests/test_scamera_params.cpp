/******************************************************************************
 * @file    test_scamera_params.cpp
 * @brief   SCamera QSC argument layout, against IGI-RE and the shipped retail
 *          level scripts.
 *
 * IGI-RE is the oracle. `SCamera`'s parameter table is built in
 * src/scamera/scamera.c:116-131 (HOOK 0x442730, ParamTable_New(15)) in exactly
 * this order, which the retail `objects.qsc` declaration repeats verbatim:
 *
 *   index  IGI-RE field      retail name                QSC arg
 *   -----  ---------------   -------------------------  -------
 *   0      place (ObjectPos) "Holder Position"          3,4,5
 *   1      yaw (Angle)       "Holder Gamma"             6
 *   2      model (String16)  "Holder Model"             7
 *   3      pitch (Angle)     "Camera Alpha"             8
 *   4      yaw_offset (Angle)"Camera Gamma"             9
 *   5      lens_model        "Camera Model"             10
 *   6      broken_model      "Camera Destroyed Model"   11
 *   7      yaw_min (Int16)   "Rotate Gamma Right (d)"   12
 *   8      yaw_max (Int16)   "Rotate Gamma Left (d)"    13
 *   9      turn_speed (Int16)"Rotate Gamma Speed (d/s)" 14
 *   10     wait (Real32)     "Gamma Delay (s)"          15
 *   11     fov_x (Int16)     "Viewcone Alpha (d)"       16
 *   12     fov_y (Int16)     "Viewcone Gamma (d)"       17
 *   13     range (Real32)    "Viewcone length (m)"      18
 *   14     script (VarString)"On Expression"            19
 *
 * Consequences the editor got wrong and this suite pins down:
 *   - arg 9 is an extra YAW (`Matrix_SetEuler(&l->o.matrix, l->pitch, 0,
 *     l->yaw + c->yaw + c->yaw_offset)`, scameralens.c:204), never roll, so the
 *     authored tuple has no roll at all.
 *   - arg 7 is the model the SCamera task itself draws: `FIELD(model)` is passed
 *     to `RigidDynCubeObj_SetModel(c, VirModel_Find(c->model))` (scamera.c:240)
 *     and placed at the authored spot (scamera.c:242). Its hard-coded default is
 *     "313_01_1" (scamera.c:181).
 *   - arg 10 is `FIELD(lens_model)` (default "313_02_1", scamera.c:184) and
 *     belongs to the separate SCameraLens child task (scameralens.c:204).
 *   - arg 11 is `FIELD(broken_model)` (default "313_03_1", scamera.c:185).
 *   - arg 12 is an Int16 yaw limit. The editor used to read it as a model
 *     string, which is where the "corrupted lens model" symptom came from.
 *
 * The last test replays every SCamera line in the installed retail scripts when
 * `IGI_GAME_PATH` (or the default install) is present; it skips otherwise.
 *****************************************************************************/

#include <gtest/gtest.h>

#include "level/level_objects.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace {

// The first SCamera in the shipped MISSIONS/location0/LEVEL1/objects.qsc, copied
// verbatim (only the trailing expression shortened). It is the strongest single
// piece of evidence in this file: its args 7/10/11 are "313_01_1" / "313_02_1" /
// "313_03_1", i.e. exactly IGI-RE's `model` / `lens_model` / `broken_model`
// defaults in scamera.c:181-185, in the declared order.
constexpr const char* kRetailLevel1Scamera =
    "Task_New(1301, \"SCamera\", \"\", 24653762.0, -56288568.0, 174435808.0, "
    "0.98391401767730713, \"313_01_1\", -0.10000000149011612, 0, \"313_02_1\", "
    "\"313_03_1\", 95, 1, 15, 4.0, 90, 55, 25.0, \"!LevelTimer_95.isRun\")";

std::string Unquote(const std::string& token) {
    if (token.size() >= 2 && token.front() == '"' && token.back() == '"')
        return token.substr(1, token.size() - 2);
    return token;
}

// Split a `Task_New(...)` body on top-level commas, honouring quoted strings.
std::vector<std::string> SplitArgs(const std::string& body) {
    std::vector<std::string> out;
    std::string current;
    bool in_string = false;
    bool escape = false;
    for (const char c : body) {
        if (escape) {
            current.push_back(c);
            escape = false;
            continue;
        }
        if (in_string && c == '\\') {
            current.push_back(c);
            escape = true;
            continue;
        }
        if (c == '"') {
            in_string = !in_string;
            current.push_back(c);
            continue;
        }
        if (c == ',' && !in_string) {
            out.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(c);
    }
    out.push_back(current);
    for (auto& token : out) {
        const std::size_t begin = token.find_first_not_of(" \t\r\n");
        const std::size_t end = token.find_last_not_of(" \t\r\n");
        token = (begin == std::string::npos) ? std::string() : token.substr(begin, end - begin + 1);
    }
    return out;
}

// Every `Task_New(...)` call text in a level script, quote- and paren-aware.
std::vector<std::string> ExtractTaskNewCalls(const std::string& text) {
    std::vector<std::string> calls;
    const std::string needle = "Task_New(";
    std::size_t pos = text.find(needle);
    while (pos != std::string::npos) {
        std::size_t index = pos + needle.size();
        int depth = 1;
        bool in_string = false;
        bool escape = false;
        for (; index < text.size(); ++index) {
            const char c = text[index];
            if (escape) {
                escape = false;
                continue;
            }
            if (in_string && c == '\\') {
                escape = true;
                continue;
            }
            if (c == '"') {
                in_string = !in_string;
                continue;
            }
            if (in_string) continue;
            if (c == '(') ++depth;
            if (c == ')' && --depth == 0) break;
        }
        if (index < text.size()) calls.push_back(text.substr(pos, index - pos + 1));
        pos = text.find(needle, index);
    }
    return calls;
}

std::filesystem::path GameRoot() {
    if (const char* env = std::getenv("IGI_GAME_PATH"); env != nullptr && *env != '\0')
        return std::filesystem::path(env);
    return std::filesystem::path("/Users/haseeb-mir/Downloads/IGI");
}

}  // namespace

// The shipped line must land on the documented layout, and its three model ids
// must be IGI-RE's own defaults for model / lens_model / broken_model.
TEST(ScameraParamsTest, RetailLevel1LineMatchesTheEngineLayout) {
    LevelObjects level;
    LevelObject obj;
    level.ParseTaskLine(kRetailLevel1Scamera, obj);

    ASSERT_EQ(obj.type, "SCamera");
    ASSERT_EQ(obj.argTokens.size(), 20u);

    EXPECT_DOUBLE_EQ(obj.pos.x, 24653762.0);
    EXPECT_DOUBLE_EQ(obj.pos.y, -56288568.0);
    EXPECT_DOUBLE_EQ(obj.pos.z, 174435808.0);

    // arg 6 = "Holder Gamma" (yaw), arg 8 = "Camera Alpha" (pitch), arg 9 = 0 here.
    EXPECT_NEAR(obj.rot.z, 0.98391401767730713, 1e-12);
    EXPECT_NEAR(obj.rot.x, -0.10000000149011612, 1e-12);
    EXPECT_DOUBLE_EQ(obj.yawOffset, 0.0);
    // No roll is ever authored for an SCamera.
    EXPECT_DOUBLE_EQ(obj.rot.y, 0.0);

    // arg 7 / 10 / 11 == IGI-RE's model / lens_model / broken_model defaults.
    EXPECT_EQ(obj.modelId, "313_01_1");
    EXPECT_EQ(obj.lensModelId, "313_02_1");
    EXPECT_EQ(obj.secondaryModelId, "313_03_1");
}

// Regression guard for the two old mis-reads: arg 9 was stored as roll and
// arg 12 (an Int16 yaw limit) was stored as the lens model.
TEST(ScameraParamsTest, Arg9IsYawOffsetAndArg12IsNotAModel) {
    LevelObjects level;
    LevelObject obj;
    level.ParseTaskLine(kRetailLevel1Scamera, obj);

    ASSERT_GT(obj.argTokens.size(), 12u);
    EXPECT_EQ(obj.argTokens[12], "95");  // "Rotate Gamma Right (d)" — an Int16
    EXPECT_NE(obj.modelId, "95");
    EXPECT_NE(obj.lensModelId, "95");
    EXPECT_NE(obj.secondaryModelId, "95");
    // arg 9 is a yaw offset, so it must never be written into rot.y.
    EXPECT_DOUBLE_EQ(obj.rot.y, 0.0);
}

// Display yaw = holder gamma + camera gamma, but the two authored args must stay
// separate on save so a load/save round trip is lossless.
TEST(ScameraParamsTest, YawOffsetIsFoldedForDisplayAndPreservedOnSave) {
    const std::string line =
        "Task_New(1301, \"SCamera\", \"\", 100.0, 200.0, 300.0, 1.0, \"313_01_1\", "
        "-0.25, 0.5, \"313_02_1\", \"313_03_1\", 95, 1, 15, 4.0, 90, 55, 25.0, \"\")";

    LevelObjects level;
    LevelObject obj;
    level.ParseTaskLine(line, obj);

    ASSERT_EQ(obj.type, "SCamera");
    EXPECT_DOUBLE_EQ(obj.rot.z, 1.5);      // 1.0 holder gamma + 0.5 camera gamma
    EXPECT_DOUBLE_EQ(obj.yawOffset, 0.5);
    EXPECT_DOUBLE_EQ(obj.rot.x, -0.25);

    // The user rotates the camera by 0.5 rad through the inspector.
    obj.rot.z += 0.5;
    obj.modified = true;
    level.UpdateCoordinatesInLine(obj);

    const std::string saved = level.GenerateTaskLine(obj);
    const auto tokens = SplitArgs(saved.substr(saved.find('(') + 1, saved.rfind(')') - saved.find('(') - 1));
    ASSERT_GE(tokens.size(), 20u);

    EXPECT_NEAR(std::stod(tokens[6]), 1.5, 1e-9);   // holder yaw = display - offset
    EXPECT_EQ(Unquote(tokens[7]), "313_01_1");      // the task's own model, untouched
    EXPECT_NEAR(std::stod(tokens[8]), -0.25, 1e-9);
    EXPECT_NEAR(std::stod(tokens[9]), 0.5, 1e-9);   // authored offset preserved
    EXPECT_EQ(Unquote(tokens[10]), "313_02_1");     // lens child model, untouched
    EXPECT_EQ(Unquote(tokens[11]), "313_03_1");     // destroyed model, untouched
    EXPECT_EQ(tokens[12], "95");                    // Int16 yaw limit, untouched

    // Re-parsing the saved line reproduces the same object state: the holder yaw
    // written back is display - offset (1.5), so the display yaw is 1.5 + 0.5 = 2.0.
    LevelObject reloaded;
    level.ParseTaskLine(saved, reloaded);
    EXPECT_DOUBLE_EQ(reloaded.rot.z, 2.0);
    EXPECT_DOUBLE_EQ(reloaded.yawOffset, 0.5);
    EXPECT_DOUBLE_EQ(reloaded.rot.x, -0.25);
    EXPECT_EQ(reloaded.modelId, "313_01_1");
    EXPECT_EQ(reloaded.lensModelId, "313_02_1");
    EXPECT_EQ(reloaded.secondaryModelId, "313_03_1");
}

// Replay every SCamera line in the installed retail scripts. Skips when the game
// is not installed, so the suite stays green on a bare checkout.
TEST(ScameraParamsTest, CorpusScameraLinesMatchTheDeclaredLayout) {
    const std::filesystem::path missions = GameRoot() / "MISSIONS";
    if (!std::filesystem::exists(missions)) {
        GTEST_SKIP() << "retail install not present at " << missions.string()
                     << " (set IGI_GAME_PATH to run this test)";
    }

    const std::regex model_id(R"(^\d{3}_\d{2}_\d$)");
    std::size_t cameras = 0;
    std::size_t scripts = 0;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(missions)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().filename() != "objects.qsc") continue;
        std::ifstream in(entry.path(), std::ios::binary);
        if (!in.is_open()) continue;
        std::stringstream buffer;
        buffer << in.rdbuf();
        ++scripts;

        for (const std::string& call : ExtractTaskNewCalls(buffer.str())) {
            LevelObjects level;
            LevelObject obj;
            level.ParseTaskLine(call, obj);
            if (obj.type != "SCamera") continue;
            ++cameras;

            ASSERT_EQ(obj.argTokens.size(), 20u) << entry.path() << "\n" << call;
            // Arg 7 is the drawn model, 10 the lens child's, 11 the destroyed one.
            EXPECT_TRUE(std::regex_match(obj.argTokens[7], model_id)) << call;
            EXPECT_TRUE(std::regex_match(obj.argTokens[10], model_id)) << call;
            EXPECT_TRUE(std::regex_match(obj.argTokens[11], model_id)) << call;
            EXPECT_EQ(obj.modelId, Unquote(obj.argTokens[7])) << call;
            EXPECT_EQ(obj.lensModelId, Unquote(obj.argTokens[10])) << call;
            EXPECT_EQ(obj.secondaryModelId, Unquote(obj.argTokens[11])) << call;

            // Arg 12 is the Int16 "Rotate Gamma Right (d)", so it must parse as an
            // integer — this is what proves it is not a model id.
            std::size_t consumed = 0;
            EXPECT_NO_THROW({ (void)std::stoi(obj.argTokens[12], &consumed); }) << call;
            EXPECT_EQ(consumed, obj.argTokens[12].size()) << call;

            // Args 6/8/9 are angles and always numeric.
            double yaw = 0.0, pitch = 0.0, offset = 0.0;
            EXPECT_NO_THROW({ yaw = std::stod(obj.argTokens[6]); }) << call;
            EXPECT_NO_THROW({ pitch = std::stod(obj.argTokens[8]); }) << call;
            EXPECT_NO_THROW({ offset = std::stod(obj.argTokens[9]); }) << call;
            EXPECT_NEAR(obj.rot.z, yaw + offset, 1e-9) << call;
            EXPECT_NEAR(obj.rot.x, pitch, 1e-9) << call;
            EXPECT_DOUBLE_EQ(obj.rot.y, 0.0) << call;
        }
    }

    EXPECT_GT(scripts, 0u) << "no objects.qsc found under " << missions.string();
    std::cout << "[  INFO  ] SCamera corpus: " << cameras << " Task_New lines across "
              << scripts << " level scripts" << std::endl;
}
