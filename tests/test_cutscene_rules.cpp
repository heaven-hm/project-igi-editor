// test_cutscene_rules.cpp - the authored cutscene camera timeline, pure-logic
// port (#80 "Intro/outro cutscenes").
//
// The oracle for every number here is IGI-RE (read-only). Key citations:
//   cutscene/cutscene.c:139-147   kPi/kNegPi/kTwoPi/kHalf/kUnits/kShakeAngle
//   cutscene/cutscene.c:163-180   CutScene_CollectKeys 0x4F4880
//   cutscene/cutscene.c:184-191   CutScene_GetKey 0x4F4910
//   cutscene/cutscene.c:193-213   CutScene_UnwrapAngles 0x4F4940
//   cutscene/cutscene.c:249-266   CutScene_Open 0x4F4B00: the 14-parameter table
//   cutscene/cutscene.c:269-297   CutScene_CreateHandler: the defaults
//   cutscene/cutscene.c:417-463   CutScene_Update 0x4F5400: tick -> t, the key
//                                 scan, the finished snap, the smooth collapse
//   cutscene/cutscene.c:473-588   CutScene_Interpolate 0x4F55D0: the unk98
//                                 substitution, the colour mix, fade, screen
//   cutscene/cutscene.c:631-654   CutScene_Spline 0x4F6290
//   editcam/editcamera.c:83-107   EditCamera_Init 0x507F10: the 16-row table
//   editcam/editcamera.c:112-132  EditCamera_Create 0x508290: the defaults
//   terrdraw/flare.c:46-61        Hermite_Init 0x518130, Hermite_Eval 0x518190
//   physobj/paramops.c:398-435    Param_Three 0x4EFB00, ParamReal32x9_Parse
//                                 0x4EFBA0 -- "Orientation" is THREE args
//   game/game.c:465               CutScene_SetTicksPerSecond(30)
//
// The corpus half reads the retail OBJECTS.QVM with the editor's own QVM_Parse,
// decompiles it with QVM_DecompileToString, and checks the authored declarations
// and the nine authored cutscenes' timelines -- cross-checking level1 against the
// human-readable objects.qsc that ships beside it.

#include "../source/level/qvm_decompiler.h"
#include "../source/level/qvm_parser.h"
#include "../source/runtime/cutscene_rules.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

fs::path GameRoot() {
    if (const char* env = std::getenv("IGI_GAME_PATH"); env != nullptr && *env != '\0')
        return fs::path(env);
    return fs::path("/Users/haseeb-mir/Downloads/IGI");
}

fs::path LevelDir(int level) {
    return GameRoot() / "MISSIONS" / "location0" / ("LEVEL" + std::to_string(level));
}

std::string ReadFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// ---------------------------------------------------------------------------
// The decompiled text: a nesting-aware argument-list reader
// ---------------------------------------------------------------------------

std::string Trim(const std::string& text) {
    std::size_t begin = 0;
    while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\n' ||
                                   text[begin] == '\r')) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\n' ||
                           text[end - 1] == '\r')) {
        --end;
    }
    return text.substr(begin, end - begin);
}

// The first and last character are the quotes; the payload is de-escaped.
std::string Unquote(const std::string& text) {
    if (text.size() < 2 || text.front() != '"' || text.back() != '"') return text;
    std::string out;
    bool escaped = false;
    for (std::size_t i = 1; i + 1 < text.size(); ++i) {
        const char c = text[i];
        if (escaped) {
            out.push_back(c);
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else {
            out.push_back(c);
        }
    }
    return out;
}

// Splits the top-level arguments of the call whose '(' is at text[open].
// Quoted strings (with backslash escapes) and nested parentheses are respected,
// so a nested call arrives as one argument. `offsets`, when given, receives each
// argument's absolute start index in `text` (needed to recurse into a nested
// call without searching a substring copy). Writes the index of the matching ')'
// to `end` when non-null.
bool ReadCallArguments(const std::string& text, std::size_t open, std::vector<std::string>* args,
                       std::vector<std::size_t>* offsets, std::size_t* end) {
    if (open >= text.size() || text[open] != '(') return false;
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    std::size_t arg_begin = open + 1;

    const auto emit = [&](std::size_t stop) {
        const std::string raw = text.substr(arg_begin, stop - arg_begin);
        const std::string trimmed = Trim(raw);
        if (args != nullptr) args->push_back(trimmed);
        if (offsets != nullptr) {
            std::size_t lead = 0;
            while (lead < raw.size() && (raw[lead] == ' ' || raw[lead] == '\t' || raw[lead] == '\n' ||
                                         raw[lead] == '\r')) {
                ++lead;
            }
            offsets->push_back(arg_begin + lead);
        }
    };

    for (std::size_t i = open; i < text.size(); ++i) {
        const char c = text[i];
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
            continue;
        }
        if (c == '(') {
            ++depth;
            continue;
        }
        if (c == ')') {
            --depth;
            if (depth == 0) {
                if (args != nullptr || offsets != nullptr) emit(i);
                if (end != nullptr) *end = i;
                return true;
            }
            continue;
        }
        if (c == ',' && depth == 1) {
            if (args != nullptr || offsets != nullptr) emit(i);
            arg_begin = i + 1;
        }
    }
    return false;
}

// One authored `Task_New(...)` call from decompiled text.
struct AuthoredCall {
    std::string type;                    // argument 1, unquoted
    std::vector<std::string> args;       // top-level arguments, raw text
    std::vector<AuthoredCall> children;  // arguments that are themselves Task_New calls
};

// Parses the Task_New call whose '(' is at text[open].
bool ParseTaskNewCall(const std::string& text, std::size_t open, AuthoredCall* out) {
    std::vector<std::string> args;
    std::vector<std::size_t> offsets;
    std::size_t end = 0;
    if (!ReadCallArguments(text, open, &args, &offsets, &end)) return false;
    if (args.size() < 3) return false;

    out->args = args;
    out->type = Unquote(args[1]);
    out->children.clear();
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i].compare(0, 9, "Task_New(") != 0) continue;
        // Resolve the callee's '(' in the ORIGINAL text, not in the argument copy.
        const std::size_t callee_open = text.find('(', offsets[i]);
        AuthoredCall child;
        if (callee_open != std::string::npos && ParseTaskNewCall(text, callee_open, &child)) {
            out->children.push_back(std::move(child));
        }
    }
    return true;
}

// Every Task_New call of `type` in the text, in file order. Nested calls are
// found too, because the scan is over the raw text.
std::vector<AuthoredCall> FindTaskNewCalls(const std::string& text, const std::string& type) {
    std::vector<AuthoredCall> out;
    const std::string needle = "Task_New(";
    std::size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        AuthoredCall call;
        if (ParseTaskNewCall(text, pos + needle.size() - 1, &call) && call.type == type) {
            out.push_back(std::move(call));
        }
        pos += needle.size();
    }
    return out;
}

// The parameters a `Task_DeclareParameters("Type", ...)` call declares, in
// order, as alternating name/type strings.
std::vector<std::string> DeclaredParameters(const std::string& text, const std::string& type) {
    std::vector<std::string> out;
    const std::string needle = "Task_DeclareParameters(";
    std::vector<std::string> args;
    std::size_t end = 0;
    std::size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        if (ReadCallArguments(text, pos + needle.size() - 1, &args, nullptr, &end) && args.size() >= 3 &&
            Unquote(args[0]) == type) {
            for (std::size_t i = 1; i < args.size(); ++i) out.push_back(Unquote(args[i]));
            return out;
        }
        args.clear();
        pos = end > pos ? end : pos + needle.size();
    }
    return out;
}

// ---------------------------------------------------------------------------
// Building the timeline from authored text
// ---------------------------------------------------------------------------

igi::CutSceneKeyNode NodeFromCall(const AuthoredCall& call) {
    igi::CutSceneKeyNode node;
    EXPECT_TRUE(igi::ParseCutSceneKey(call.args, &node.params));
    for (const AuthoredCall& child : call.children) {
        if (child.type == "EditCamera") node.children.push_back(NodeFromCall(child));
    }
    return node;
}

// The authored children of a CutScene call, as the engine's Task_FindChild would
// find them.
std::vector<igi::CutSceneKeyNode> CutSceneChildren(const AuthoredCall& cut_scene) {
    std::vector<igi::CutSceneKeyNode> children;
    for (const AuthoredCall& child : cut_scene.children) {
        if (child.type == "EditCamera") children.push_back(NodeFromCall(child));
    }
    return children;
}

// One retail cutscene with its resolved timeline.
struct RetailCutScene {
    std::string task_id;
    igi::CutSceneScreenParams screen;
    std::vector<igi::CutSceneKey> keys;
    float end = 0.0f;
};

std::vector<RetailCutScene> BuildCutScenes(const std::string& text) {
    std::vector<RetailCutScene> out;
    for (const AuthoredCall& call : FindTaskNewCalls(text, "CutScene")) {
        RetailCutScene scene;
        if (!igi::ParseCutSceneScreen(call.args, &scene.screen)) continue;
        scene.task_id = Unquote(call.args[0]);
        scene.keys = igi::CollectCutSceneKeys(CutSceneChildren(call), scene.screen.time_scale, &scene.end);
        igi::UnwrapCutSceneAngles(&scene.keys);
        out.push_back(std::move(scene));
    }
    return out;
}

// A minimal authored EditCamera argument list, so the unit tests do not depend
// on the corpus. Indices follow the 23-token layout.
std::vector<std::string> KeyTokens(double x, double y, double z, float ax, float ay, float az, float fov,
                                  float duration, int pos_attach, bool pos_follow, int rot_attach,
                                  bool rot_follow, bool smooth, float unk98, float level, float shake) {
    const auto flag = [](bool value) { return std::string(value ? "TRUE" : "FALSE"); };
    return {
        "-1", "\"EditCamera\"", "\"key\"",
        std::to_string(x), std::to_string(y), std::to_string(z),
        std::to_string(ax), std::to_string(ay), std::to_string(az),
        std::to_string(fov), std::to_string(duration),
        std::to_string(pos_attach), flag(pos_follow),
        std::to_string(rot_attach), flag(rot_follow),
        flag(smooth), std::to_string(unk98),
        "\"CAMERAFILTER_TYPE_NONE\"", std::to_string(level),
        "0", "0", "0", std::to_string(shake),
    };
}

// A minimal authored CutScene argument list (21 tokens).
std::vector<std::string> ScreenTokens(float time_scale, float screen_size, float fade_in, float fade_out,
                                      float time_of_day) {
    return {
        "1200", "\"CutScene\"", "\"scene\"",
        "0", "0", "0", "0", "0", "0",
        "\"!CutScene_1200.isFinished\"", "\"\"", "\"\"",
        "0", "FALSE", std::to_string(time_scale),
        std::to_string(screen_size), std::to_string(fade_in), std::to_string(fade_out),
        std::to_string(time_of_day), "\"\"", "\"\"",
    };
}

igi::CutSceneKeyNode MakeNode(double x, float duration, bool smooth = true) {
    igi::CutSceneKeyNode node;
    node.params.position[0] = x;
    node.params.duration = duration;
    node.params.smooth = smooth;
    return node;
}

}  // namespace

// ---------------------------------------------------------------------------
// The authored parameter tables
// ---------------------------------------------------------------------------

TEST(CutSceneRulesTest, KeyParameterTableCoversEveryAuthoredTokenOnce) {
    const igi::CutSceneParamDesc* table = igi::CutSceneKeyParamTable();
    ASSERT_EQ(igi::CutSceneKeyParamCount(), 16);
    int next = 3;
    for (int i = 0; i < igi::CutSceneKeyParamCount(); ++i) {
        EXPECT_EQ(table[i].token_index, next) << "slot " << i;
        EXPECT_GT(table[i].token_count, 0) << "slot " << i;
        next += table[i].token_count;
    }
    // 20 declared arguments, so the last token is 3 + 20 - 1 = 22.
    EXPECT_EQ(next, igi::kCutSceneKeyTokenCount);
}

TEST(CutSceneRulesTest, ScreenParameterTableCoversEveryAuthoredTokenOnce) {
    const igi::CutSceneParamDesc* table = igi::CutSceneScreenParamTable();
    ASSERT_EQ(igi::CutSceneScreenParamCount(), 14);
    int next = 3;
    for (int i = 0; i < igi::CutSceneScreenParamCount(); ++i) {
        EXPECT_EQ(table[i].token_index, next) << "slot " << i;
        next += table[i].token_count;
    }
    EXPECT_EQ(next, igi::kCutSceneScreenTokenCount);
}

TEST(CutSceneRulesTest, OrientationConsumesThreeArgumentsLikeTheEngine) {
    // ParamReal32x9_Parse (physobj/paramops.c:423-435) reads three Euler angles
    // and builds the matrix with Matrix_SetEuler, and Param_Three (paramops.c:398)
    // is its argument count. So "Orientation" is three script arguments, not
    // nine -- which is why Run sits at token 9 in the authored data.
    const igi::CutSceneParamDesc* table = igi::CutSceneScreenParamTable();
    EXPECT_STREQ(table[1].type_name, "Real32x9");
    EXPECT_EQ(table[1].token_count, 3);
    EXPECT_EQ(table[2].token_index, 9);
}

// ---------------------------------------------------------------------------
// Parsing an authored key and an authored header
// ---------------------------------------------------------------------------

TEST(CutSceneRulesTest, ParsesARealRetailEditCameraKey) {
    // level1/objects.qsc:1094, the first key of cutscene 1200.
    const std::vector<std::string> tokens = {
        "-1", "\"EditCamera\"", "\"Heli tale cam1\"",
        "22664462.0", "-60994444.0", "174919168.0",
        "-1.7802360057830811", "0", "0",
        "1", "6.0", "1502", "TRUE", "-1", "TRUE", "TRUE", "-1",
        "\"CAMERAFILTER_TYPE_NONE\"", "0.5", "0", "0", "0", "0",
    };
    igi::CutSceneKeyParams key;
    ASSERT_TRUE(igi::ParseCutSceneKey(tokens, &key));

    EXPECT_DOUBLE_EQ(key.position[0], 22664462.0);
    EXPECT_DOUBLE_EQ(key.position[1], -60994444.0);
    EXPECT_DOUBLE_EQ(key.position[2], 174919168.0);
    EXPECT_FLOAT_EQ(key.angles[0], -1.7802360057830811f);
    EXPECT_FLOAT_EQ(key.angles[1], 0.0f);
    EXPECT_FLOAT_EQ(key.angles[2], 0.0f);
    EXPECT_FLOAT_EQ(key.fov, 1.0f);
    EXPECT_FLOAT_EQ(key.duration, 6.0f);
    EXPECT_EQ(key.pos_attach, 1502);
    EXPECT_TRUE(key.pos_follow);
    EXPECT_EQ(key.rot_attach, -1);
    EXPECT_TRUE(key.rot_follow);
    EXPECT_TRUE(key.smooth);
    EXPECT_FLOAT_EQ(key.time_of_day_override, -1.0f);
    EXPECT_EQ(key.filter_name, "CAMERAFILTER_TYPE_NONE");
    EXPECT_FLOAT_EQ(key.filter_level, 0.5f);
    EXPECT_FLOAT_EQ(key.shake, 0.0f);
}

TEST(CutSceneRulesTest, AKeyThatTargetsAnotherTaskReadsTheTargetSlot) {
    // level1/objects.qsc:1099, the "Heli flyby" key: posAttach -1, rotAttach 1502.
    std::vector<std::string> tokens = KeyTokens(1.0, 2.0, 3.0, 0.0f, 0.0f, 0.0f, 1.0f, 2.6f, -1, true, 1502,
                                               true, false, -1.0f, 0.5f, 0.0f);
    igi::CutSceneKeyParams key;
    ASSERT_TRUE(igi::ParseCutSceneKey(tokens, &key));
    EXPECT_EQ(key.pos_attach, -1);
    EXPECT_EQ(key.rot_attach, 1502);
    EXPECT_FALSE(key.smooth);
}

TEST(CutSceneRulesTest, AKeyShorterThanTheTableIsRejectedNotDefaulted) {
    std::vector<std::string> tokens = KeyTokens(1.0, 2.0, 3.0, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, -1, true, -1,
                                                true, true, -1.0f, 0.5f, 0.0f);
    tokens.pop_back();  // 22 tokens
    igi::CutSceneKeyParams key;
    EXPECT_FALSE(igi::ParseCutSceneKey(tokens, &key));
}

TEST(CutSceneRulesTest, ParsesARealRetailCutSceneHeader) {
    // level1/objects.qsc:1093, cutscene 1200.
    const std::vector<std::string> tokens = {
        "1200", "\"CutScene\"", "\"Insertion helicopter\"",
        "22424126.0", "-58423556.0", "174687648.0", "0", "0", "0",
        "\"!CutScene_1200.isFinished\"", "\"\"", "\"\"",
        "0", "FALSE", "1", "0.69999998807907104", "0", "0", "0", "\"\"", "\"\"",
    };
    igi::CutSceneScreenParams screen;
    ASSERT_TRUE(igi::ParseCutSceneScreen(tokens, &screen));

    EXPECT_EQ(screen.run_expression, "!CutScene_1200.isFinished");
    EXPECT_TRUE(screen.reset_expression.empty());
    EXPECT_TRUE(screen.time_delta_expression.empty());
    EXPECT_FLOAT_EQ(screen.start_time_seconds, 0.0f);
    EXPECT_FALSE(screen.initial_run);
    EXPECT_FLOAT_EQ(screen.time_scale, 1.0f);
    EXPECT_FLOAT_EQ(screen.screen_size, 0.69999998807907104f);
    EXPECT_FLOAT_EQ(screen.fade_in_seconds, 0.0f);
    EXPECT_FLOAT_EQ(screen.fade_out_seconds, 0.0f);
    EXPECT_FLOAT_EQ(screen.time_of_day, 0.0f);
    EXPECT_TRUE(screen.start_expression.empty());
    EXPECT_TRUE(screen.stop_expression.empty());
}

TEST(CutSceneRulesTest, AHeaderShorterThanTheTableIsRejected) {
    std::vector<std::string> tokens = ScreenTokens(1.0f, 1.0f, 0.0f, 0.0f, 0.0f);
    tokens.pop_back();
    igi::CutSceneScreenParams screen;
    EXPECT_FALSE(igi::ParseCutSceneScreen(tokens, &screen));
}

// ---------------------------------------------------------------------------
// CutScene_CollectKeys
// ---------------------------------------------------------------------------

TEST(CutSceneRulesTest, CollectKeysAccumulatesScaledDurations) {
    // cutscene.c:170-172: each key starts where the previous one ended, and the
    // step is `scale * duration`.
    std::vector<igi::CutSceneKeyNode> children = {
        MakeNode(0.0, 6.0f),
        MakeNode(1.0, 3.0f),
        MakeNode(2.0, 2.5f),
    };
    float end = 0.0f;
    const std::vector<igi::CutSceneKey> keys = igi::CollectCutSceneKeys(children, 0.7f, &end);

    ASSERT_EQ(keys.size(), 3u);
    EXPECT_FLOAT_EQ(keys[0].start_time, 0.0f);
    EXPECT_NEAR(keys[1].start_time, 0.7f * 6.0f, 1e-5);
    EXPECT_NEAR(keys[2].start_time, 0.7f * 9.0f, 1e-5);
    // `end` includes the last key's own duration.
    EXPECT_NEAR(end, 0.7f * 11.5f, 1e-5);
}

TEST(CutSceneRulesTest, CollectKeysVisitsAChildSubtreeBeforeTheNextSibling) {
    // cutscene.c:174-177 recurses into the key's first EditCamera child BEFORE
    // stepping to the next sibling, and that child's own siblings are consumed
    // by the recursion. Order: parent, child, child's sibling, parent's sibling.
    igi::CutSceneKeyNode parent = MakeNode(0.0, 1.0f);
    igi::CutSceneKeyNode child = MakeNode(10.0, 2.0f);
    igi::CutSceneKeyNode child_sibling = MakeNode(20.0, 3.0f);
    parent.children = {child, child_sibling};

    std::vector<igi::CutSceneKeyNode> children = {parent, MakeNode(30.0, 4.0f)};
    float end = 0.0f;
    const std::vector<igi::CutSceneKey> keys = igi::CollectCutSceneKeys(children, 1.0f, &end);

    ASSERT_EQ(keys.size(), 4u);
    EXPECT_DOUBLE_EQ(keys[0].params.position[0], 0.0);
    EXPECT_DOUBLE_EQ(keys[1].params.position[0], 10.0);
    EXPECT_DOUBLE_EQ(keys[2].params.position[0], 20.0);
    EXPECT_DOUBLE_EQ(keys[3].params.position[0], 30.0);
    EXPECT_FLOAT_EQ(keys[1].start_time, 1.0f);
    EXPECT_FLOAT_EQ(keys[2].start_time, 3.0f);
    EXPECT_FLOAT_EQ(keys[3].start_time, 6.0f);
    EXPECT_FLOAT_EQ(end, 10.0f);
}

TEST(CutSceneRulesTest, CollectKeysRefusesToOverrunTheEnginesKeyArray) {
    // `CutKey keys[100]` (cutscene.c:419) with no bounds check on the write.
    std::vector<igi::CutSceneKeyNode> children;
    for (int i = 0; i < igi::kCutSceneKeyCapacity; ++i) children.push_back(MakeNode(i, 1.0f));

    float end = 0.0f;
    EXPECT_EQ(igi::CollectCutSceneKeys(children, 1.0f, &end).size(),
              static_cast<std::size_t>(igi::kCutSceneKeyCapacity));

    children.push_back(MakeNode(100.0, 1.0f));
    float refused_end = 123.0f;
    EXPECT_TRUE(igi::CollectCutSceneKeys(children, 1.0f, &refused_end).empty());
    EXPECT_FLOAT_EQ(refused_end, 0.0f);
}

TEST(CutSceneRulesTest, KeyAtClampsAtBothEnds) {
    std::vector<igi::CutSceneKeyNode> children = {MakeNode(0.0, 1.0f), MakeNode(1.0, 1.0f),
                                                  MakeNode(2.0, 1.0f)};
    float end = 0.0f;
    const std::vector<igi::CutSceneKey> keys = igi::CollectCutSceneKeys(children, 1.0f, &end);

    ASSERT_NE(igi::CutSceneKeyAt(keys, -5), nullptr);
    EXPECT_DOUBLE_EQ(igi::CutSceneKeyAt(keys, -5)->params.position[0], 0.0);
    EXPECT_DOUBLE_EQ(igi::CutSceneKeyAt(keys, 99)->params.position[0], 2.0);
    EXPECT_EQ(igi::CutSceneKeyAt(keys, -5), igi::CutSceneKeyAt(keys, 0));
    EXPECT_EQ(igi::CutSceneKeyAt(keys, 99), igi::CutSceneKeyAt(keys, 2));
    EXPECT_EQ(igi::CutSceneKeyAt({}, 0), nullptr);
}

// ---------------------------------------------------------------------------
// CutScene_UnwrapAngles
// ---------------------------------------------------------------------------

TEST(CutSceneRulesTest, UnwrapKeepsEachAngleWithinPiOfThePreviousKey) {
    std::vector<igi::CutSceneKeyNode> children = {MakeNode(0.0, 1.0f), MakeNode(1.0, 1.0f),
                                                  MakeNode(2.0, 1.0f)};
    children[1].params.angles[0] = 6.0f;    // 6.0 - 0.2 > pi, so -2pi
    children[2].params.angles[0] = -6.0f;   // vs the unwrapped previous

    float end = 0.0f;
    std::vector<igi::CutSceneKey> keys = igi::CollectCutSceneKeys(children, 1.0f, &end);
    keys[0].params.angles[0] = 0.2f;
    igi::UnwrapCutSceneAngles(&keys);

    EXPECT_FLOAT_EQ(keys[0].params.angles[0], 0.2f);  // the first key is never touched
    EXPECT_NEAR(keys[1].params.angles[0], 6.0f - static_cast<float>(igi::kCutSceneTwoPi), 1e-5);
    for (std::size_t i = 1; i < keys.size(); ++i) {
        const double delta = static_cast<double>(keys[i].params.angles[0]) -
                             static_cast<double>(keys[i - 1].params.angles[0]);
        EXPECT_LE(delta, igi::kCutScenePi);
        EXPECT_GE(delta, igi::kCutSceneNegPi);
    }
}

TEST(CutSceneRulesTest, UnwrapLeavesANonFiniteAngleAlone) {
    // DIVERGENCE: cutscene.c:207-208 cannot terminate on a NaN -- the engine's
    // own comment says "a NaN hangs, like the original". A preview must not hang,
    // so the authored value is kept instead.
    std::vector<igi::CutSceneKeyNode> children = {MakeNode(0.0, 1.0f), MakeNode(1.0, 1.0f)};
    float end = 0.0f;
    std::vector<igi::CutSceneKey> keys = igi::CollectCutSceneKeys(children, 1.0f, &end);
    keys[1].params.angles[1] = std::nanf("");
    igi::UnwrapCutSceneAngles(&keys);
    EXPECT_TRUE(std::isnan(keys[1].params.angles[1]));
}

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------

TEST(CutSceneRulesTest, TimeIsTicksOverThirty) {
    EXPECT_FLOAT_EQ(igi::CutSceneTimeFromTick(0), 0.0f);
    EXPECT_FLOAT_EQ(igi::CutSceneTimeFromTick(30), 1.0f);
    EXPECT_FLOAT_EQ(igi::CutSceneTimeFromTick(45), 1.5f);
    EXPECT_FLOAT_EQ(igi::CutSceneTimeFromTick(-30), -1.0f);
}

TEST(CutSceneRulesTest, TickFromTimeRoundsToNearestEvenLikeFistpll) {
    EXPECT_EQ(igi::CutSceneTickFromTime(0.0), 0);
    EXPECT_EQ(igi::CutSceneTickFromTime(1.0), 30);
    EXPECT_EQ(igi::CutSceneTickFromTime(13.02), 391);   // 390.6 -> 391
    EXPECT_EQ(igi::CutSceneTickFromTime(0.5 / 30.0), 0);   // 0.5 -> 0 (even)
    EXPECT_EQ(igi::CutSceneTickFromTime(1.5 / 30.0), 2);   // 1.5 -> 2 (even)
}

// ---------------------------------------------------------------------------
// CutScene_Update's key selection
// ---------------------------------------------------------------------------

TEST(CutSceneRulesTest, SelectKeysPicksTheLastKeyAtOrBeforeTheTime) {
    std::vector<igi::CutSceneKeyNode> children = {MakeNode(0.0, 1.0f), MakeNode(1.0, 1.0f),
                                                  MakeNode(2.0, 1.0f)};
    float end = 0.0f;
    const std::vector<igi::CutSceneKey> keys = igi::CollectCutSceneKeys(children, 1.0f, &end);

    EXPECT_EQ(igi::SelectCutSceneKeys(keys, end, -1.0f).active_key, -1);
    EXPECT_EQ(igi::SelectCutSceneKeys(keys, end, 0.0f).active_key, 0);
    EXPECT_EQ(igi::SelectCutSceneKeys(keys, end, 0.5f).active_key, 0);
    EXPECT_EQ(igi::SelectCutSceneKeys(keys, end, 1.0f).active_key, 1);
    EXPECT_EQ(igi::SelectCutSceneKeys(keys, end, 2.5f).active_key, 2);
    EXPECT_TRUE(igi::SelectCutSceneKeys(keys, end, 3.5f).finished);
    EXPECT_FALSE(igi::SelectCutSceneKeys(keys, end, 3.0f).finished);
}

TEST(CutSceneRulesTest, SelectKeysCollapsesBothTangentsOfANonSmoothMiddleKey) {
    // cutscene.c:455-460, where the third test reads k[2] as rewritten by the
    // second: a non-smooth middle key becomes its own left AND right tangent.
    std::vector<igi::CutSceneKeyNode> children = {MakeNode(0.0, 1.0f), MakeNode(1.0, 1.0f),
                                                  MakeNode(2.0, 1.0f), MakeNode(3.0, 1.0f)};
    children[1].params.smooth = false;
    float end = 0.0f;
    const std::vector<igi::CutSceneKey> keys = igi::CollectCutSceneKeys(children, 1.0f, &end);

    const igi::CutSceneKeyWindow window = igi::SelectCutSceneKeys(keys, end, 1.0f);
    ASSERT_EQ(window.active_key, 1);
    EXPECT_EQ(window.indices[1], 1);
    EXPECT_EQ(window.indices[2], 1);  // collapsed from 2 by the second test
    EXPECT_EQ(window.indices[3], 1);  // and k[3] follows the rewritten k[2]
}

TEST(CutSceneRulesTest, SelectKeysLeavesASmoothWindowIntact) {
    std::vector<igi::CutSceneKeyNode> children = {MakeNode(0.0, 1.0f), MakeNode(1.0, 1.0f),
                                                  MakeNode(2.0, 1.0f), MakeNode(3.0, 1.0f)};
    float end = 0.0f;
    const std::vector<igi::CutSceneKey> keys = igi::CollectCutSceneKeys(children, 1.0f, &end);

    const igi::CutSceneKeyWindow window = igi::SelectCutSceneKeys(keys, end, 1.5f);
    ASSERT_EQ(window.active_key, 1);
    EXPECT_EQ(window.indices[0], 0);
    EXPECT_EQ(window.indices[1], 1);
    EXPECT_EQ(window.indices[2], 2);
    EXPECT_EQ(window.indices[3], 3);
}

// ---------------------------------------------------------------------------
// CutScene_Spline and Hermite
// ---------------------------------------------------------------------------

TEST(CutSceneRulesTest, SplineHitsBothEndPointsExactly) {
    // Hermite_Eval at u = 0 is h[1] = p1 and at u = 1 is p2 (flare.c:55-61).
    EXPECT_DOUBLE_EQ(static_cast<double>(igi::CutSceneSpline(0.0, 5.0, 9.0, 12.0, 0.0, 1.0, 2.0, 3.0, 1.0)),
                     5.0);
    EXPECT_DOUBLE_EQ(static_cast<double>(igi::CutSceneSpline(0.0, 5.0, 9.0, 12.0, 0.0, 1.0, 2.0, 3.0, 2.0)),
                     9.0);
}

TEST(CutSceneRulesTest, SplineIsLinearWhenBothTangentsVanish) {
    // m1 is 0 when t2 == t0 and m2 is 0 when t3 == t1 (cutscene.c:640-648), which
    // collapses the Hermite to the straight line p1 -> p2.
    const double t = 1.5;
    const double value =
        static_cast<double>(igi::CutSceneSpline(5.0, 5.0, 9.0, 9.0, 2.0, 1.0, 2.0, 1.0, t));
    EXPECT_NEAR(value, 5.0 + (9.0 - 5.0) * (t - 1.0) / (2.0 - 1.0), 1e-12);
}

TEST(CutSceneRulesTest, SplineTangentsAreScaledForUnevenKeyTimes) {
    // The closed form of cutscene.c:640-654 with p = (0, 1, 4, 8) at
    // t = (0, 1, 3, 4), evaluated at u = 0.5 of the 1 -> 3 span.
    const double p0 = 0.0, p1 = 1.0, p2 = 4.0, p3 = 8.0;
    const double t0 = 0.0, t1 = 1.0, t2 = 3.0, t3 = 4.0;
    const double t = 2.0;

    const double half = (p3 - p1) * 0.5;
    const double m1 = (t1 - t0) * 2 / (t2 - t0) * ((p2 - p0) * 0.5);
    const double m2 = (t2 - t1) * 2 / (t3 - t1) * half;
    const double u = (t - t1) / (t2 - t1);
    const double h0 = m1;
    const double h1 = p1;
    const double h2 = p1 + p1 - (p2 + p2) + m1 + m2;
    const double h3 = p2 * 3.0 - p1 * 3.0 - (m1 + m1) - m2;
    const double expected = u * h0 + (u * u * u * h2 + u * u * h3) + h1;

    EXPECT_NEAR(static_cast<double>(igi::CutSceneSpline(p0, p1, p2, p3, t0, t1, t2, t3, t)),
                expected, 1e-12);
}

TEST(CutSceneRulesTest, SplineTreatsAZeroTimeSpanAsZeroRatherThanDividing) {
    // Every guard is `x87_eq(d, 0)`, which is also true for a NaN difference.
    EXPECT_TRUE(std::isfinite(static_cast<double>(
        igi::CutSceneSpline(1.0, 2.0, 3.0, 4.0, 5.0, 5.0, 5.0, 5.0, 5.0))));
    EXPECT_DOUBLE_EQ(
        static_cast<double>(igi::CutSceneSpline(1.0, 2.0, 3.0, 4.0, 5.0, 5.0, 5.0, 5.0, 5.0)), 2.0);
}

// ---------------------------------------------------------------------------
// Fade and shake
// ---------------------------------------------------------------------------

TEST(CutSceneRulesTest, FadeMixRampsInAndOutAroundTheScene) {
    igi::CutSceneScreenParams screen;
    screen.screen_size = 1.0f;
    screen.fade_in_seconds = 2.0f;
    screen.fade_out_seconds = 4.0f;
    const float end = 10.0f;

    EXPECT_FLOAT_EQ(igi::CutSceneFadeMix(screen, 0.0f, end).mix, 0.0f);
    EXPECT_FLOAT_EQ(igi::CutSceneFadeMix(screen, 1.0f, end).mix, 0.5f);
    EXPECT_FLOAT_EQ(igi::CutSceneFadeMix(screen, 2.0f, end).mix, 1.0f);
    EXPECT_FLOAT_EQ(igi::CutSceneFadeMix(screen, 8.0f, end).mix, 0.5f);
    EXPECT_FLOAT_EQ(igi::CutSceneFadeMix(screen, 10.0f, end).mix, 0.0f);
    EXPECT_FLOAT_EQ(igi::CutSceneFadeMix(screen, 11.0f, end).mix, 0.0f);
}

TEST(CutSceneRulesTest, NoFadesMeansAFullyOpaqueFrame) {
    igi::CutSceneScreenParams screen;
    screen.fade_in_seconds = 0.0f;
    screen.fade_out_seconds = 0.0f;
    const igi::CutSceneFade fade = igi::CutSceneFadeMix(screen, 3.0f, 10.0f);
    EXPECT_FLOAT_EQ(fade.mix, 1.0f);
    EXPECT_FLOAT_EQ(fade.screen_factor, 1.0f);
}

TEST(CutSceneRulesTest, TheLetterboxFactorIsTheFadeMixTowardsTheAuthoredHeight) {
    // cutscene.c:587: screen = (1 - mix) + mix * c->screenSize.
    igi::CutSceneScreenParams screen;
    screen.screen_size = 0.7f;
    screen.fade_in_seconds = 2.0f;
    screen.fade_out_seconds = 0.0f;

    EXPECT_FLOAT_EQ(igi::CutSceneFadeMix(screen, 0.0f, 10.0f).screen_factor, 1.0f);
    EXPECT_FLOAT_EQ(igi::CutSceneFadeMix(screen, 1.0f, 10.0f).screen_factor, 0.5f + 0.5f * 0.7f);
    EXPECT_FLOAT_EQ(igi::CutSceneFadeMix(screen, 2.0f, 10.0f).screen_factor, 0.7f);
}

TEST(CutSceneRulesTest, ShakeUsesTheEnginesDrawOrderAndScales) {
    // cutscene.c:569-575: three position draws in x/y/z order, then s4/s5/s6 for
    // the Z/Y/X angles, then Matrix_SetEuler(&shk, s6, s5, s4).
    const long double draws[6] = {0.0L, 0.5L, 1.0L, 0.0L, 0.5L, 1.0L};
    double pos[3] = {0.0, 0.0, 0.0};
    double angles[3] = {0.0, 0.0, 0.0};
    igi::CutSceneShakeDelta(1.0f, draws, pos, angles);

    EXPECT_DOUBLE_EQ(pos[0], 0.5 * igi::kCutSceneUnits);
    EXPECT_DOUBLE_EQ(pos[1], 0.0);
    EXPECT_DOUBLE_EQ(pos[2], -0.5 * igi::kCutSceneUnits);
    EXPECT_DOUBLE_EQ(angles[0], -0.5 * igi::kCutSceneShakeAngle);
    EXPECT_DOUBLE_EQ(angles[1], 0.0);
    EXPECT_DOUBLE_EQ(angles[2], 0.5 * igi::kCutSceneShakeAngle);

    igi::CutSceneShakeDelta(0.0f, draws, pos, angles);
    for (int axis = 0; axis < 3; ++axis) {
        EXPECT_DOUBLE_EQ(pos[axis], 0.0);
        EXPECT_DOUBLE_EQ(angles[axis], 0.0);
    }
}

// ---------------------------------------------------------------------------
// The whole preview rule
// ---------------------------------------------------------------------------

TEST(CutSceneRulesTest, EvaluateHitsEachKeyExactlyAtItsStartTime) {
    std::vector<igi::CutSceneKeyNode> children = {MakeNode(0.0, 1.0f), MakeNode(1.0, 2.0f),
                                                  MakeNode(2.0, 3.0f)};
    children[0].params.position[1] = 100.0;
    children[1].params.position[1] = 200.0;
    children[2].params.position[1] = 300.0;
    children[1].params.angles[2] = 0.75f;

    float end = 0.0f;
    std::vector<igi::CutSceneKey> keys = igi::CollectCutSceneKeys(children, 1.0f, &end);
    igi::UnwrapCutSceneAngles(&keys);

    igi::CutSceneScreenParams screen;
    for (std::size_t i = 0; i < keys.size(); ++i) {
        igi::CutSceneCameraState state;
        ASSERT_TRUE(igi::EvaluateCutScene(keys, end, screen, keys[i].start_time, false, &state))
            << "key " << i;
        EXPECT_EQ(state.active_key, static_cast<int>(i));
        EXPECT_DOUBLE_EQ(state.position[0], keys[i].params.position[0]) << "key " << i;
        EXPECT_DOUBLE_EQ(state.position[1], keys[i].params.position[1]) << "key " << i;
        EXPECT_FLOAT_EQ(state.angles[2], keys[i].params.angles[2]) << "key " << i;
    }
}

TEST(CutSceneRulesTest, EvaluateRejectsAnEmptyTimeline) {
    igi::CutSceneScreenParams screen;
    igi::CutSceneCameraState state;
    EXPECT_FALSE(igi::EvaluateCutScene({}, 0.0f, screen, 0.0f, false, &state));
}

TEST(CutSceneRulesTest, EvaluateInheritsTheScenesTimeOfDayWhenTheKeySaysMinusOne) {
    // cutscene.c:488-492: `w[i] = k[i]->key->unk98; if (!(w[i] >= 0.0f)) w[i] = c->unk210;`
    std::vector<igi::CutSceneKeyNode> children = {MakeNode(0.0, 1.0f), MakeNode(1.0, 1.0f)};
    children[0].params.time_of_day_override = -1.0f;  // inherit
    children[1].params.time_of_day_override = 0.25f;  // its own

    float end = 0.0f;
    std::vector<igi::CutSceneKey> keys = igi::CollectCutSceneKeys(children, 1.0f, &end);

    igi::CutSceneScreenParams screen;
    screen.time_of_day = 0.8f;

    igi::CutSceneCameraState state;
    ASSERT_TRUE(igi::EvaluateCutScene(keys, end, screen, 0.0f, false, &state));
    EXPECT_FLOAT_EQ(state.time_of_day, 0.8f);

    ASSERT_TRUE(igi::EvaluateCutScene(keys, end, screen, 1.0f, false, &state));
    EXPECT_FLOAT_EQ(state.time_of_day, 0.25f);
}

TEST(CutSceneRulesTest, EvaluateSplinesTheShakeAmplitudeToo) {
    // cutscene.c:501: `shake = SPLINE(shake);` — the amplitude that feeds the six
    // Random_Float draws is interpolated across the window like any other field,
    // and a key with no shake must damp its neighbours rather than hold.
    std::vector<igi::CutSceneKeyNode> children = {MakeNode(0.0, 1.0f), MakeNode(1.0, 2.0f),
                                                  MakeNode(2.0f, 1.0f)};
    children[0].params.shake = 0.0f;
    children[1].params.shake = 0.0f;
    children[2].params.shake = 0.0f;

    float end = 0.0f;
    std::vector<igi::CutSceneKey> keys = igi::CollectCutSceneKeys(children, 1.0f, &end);

    igi::CutSceneScreenParams screen;
    igi::CutSceneCameraState state;
    ASSERT_TRUE(igi::EvaluateCutScene(keys, end, screen, 0.0f, false, &state));
    EXPECT_FLOAT_EQ(state.shake, 0.0f);

    // At a key's own start time the spline returns that key's value exactly, so
    // an authored 0.4 is what the preview shakes with.
    children[1].params.shake = 0.4f;
    keys = igi::CollectCutSceneKeys(children, 1.0f, &end);
    ASSERT_TRUE(igi::EvaluateCutScene(keys, end, screen, 1.0f, false, &state));
    EXPECT_FLOAT_EQ(state.shake, 0.4f);
}

TEST(CutSceneRulesTest, EvaluateMixesColourBetweenTheSurroundingKeys) {
    // cutscene.c:522-526: colour[i] = (k[2] - k[1]) * f + k[1], f = (t - t1)/span.
    // The mix runs from key 1 (t = 1) to key 2 (t = 3), a span of 2.
    std::vector<igi::CutSceneKeyNode> children = {MakeNode(0.0, 1.0f), MakeNode(1.0, 2.0f),
                                                  MakeNode(2.0, 1.0f)};
    children[0].params.colour[0] = 0.0f;
    children[1].params.colour[0] = 0.0f;
    children[2].params.colour[0] = 1.0f;

    float end = 0.0f;
    std::vector<igi::CutSceneKey> keys = igi::CollectCutSceneKeys(children, 1.0f, &end);

    igi::CutSceneScreenParams screen;
    igi::CutSceneCameraState state;
    ASSERT_TRUE(igi::EvaluateCutScene(keys, end, screen, 1.0f, false, &state));
    EXPECT_FLOAT_EQ(state.colour[0], 0.0f);
    ASSERT_TRUE(igi::EvaluateCutScene(keys, end, screen, 2.0f, false, &state));
    EXPECT_FLOAT_EQ(state.colour[0], 0.5f);
    // At the last key the span is zero, so the colour is that key's outright.
    ASSERT_TRUE(igi::EvaluateCutScene(keys, end, screen, 3.0f, false, &state));
    EXPECT_FLOAT_EQ(state.colour[0], 1.0f);
}

TEST(CutSceneRulesTest, TheBandFilterBranchIsImplementedRatherThanAssumed) {
    // cutscene.c:512-521: with a live band filter and a splined level of exactly
    // 0, the fraction is (t - t1) / (span - 0.5) clamped, not (t - t1) / span.
    std::vector<igi::CutSceneKeyNode> children = {MakeNode(0.0, 1.0f), MakeNode(1.0, 2.0f),
                                                  MakeNode(2.0, 1.0f)};
    children[0].params.colour[0] = 0.0f;
    children[0].params.filter_level = 0.0f;
    children[1].params.colour[0] = 0.0f;
    children[1].params.filter_level = 0.0f;
    children[2].params.colour[0] = 1.0f;
    children[2].params.filter_level = 0.0f;

    float end = 0.0f;
    std::vector<igi::CutSceneKey> keys = igi::CollectCutSceneKeys(children, 1.0f, &end);

    igi::CutSceneScreenParams screen;
    igi::CutSceneCameraState with_filter;
    igi::CutSceneCameraState without_filter;
    ASSERT_TRUE(igi::EvaluateCutScene(keys, end, screen, 2.0f, true, &with_filter));
    ASSERT_TRUE(igi::EvaluateCutScene(keys, end, screen, 2.0f, false, &without_filter));

    // span is 2.0, so with a band filter f = (2 - 1) / 1.5.
    EXPECT_FLOAT_EQ(with_filter.colour[0], 1.0f / 1.5f);
    EXPECT_FLOAT_EQ(without_filter.colour[0], 0.5f);
}

TEST(CutSceneRulesTest, EvaluateReportsFinishedPastTheEnd) {
    std::vector<igi::CutSceneKeyNode> children = {MakeNode(0.0, 1.0f), MakeNode(1.0, 1.0f)};
    float end = 0.0f;
    std::vector<igi::CutSceneKey> keys = igi::CollectCutSceneKeys(children, 1.0f, &end);

    igi::CutSceneScreenParams screen;
    igi::CutSceneCameraState state;
    ASSERT_TRUE(igi::EvaluateCutScene(keys, end, screen, end + 0.5f, false, &state));
    EXPECT_TRUE(state.finished);
    // The engine does not move `cur` when it marks the scene finished, so the
    // finished pose is still the last key's window.
    EXPECT_EQ(state.active_key, 1);
}

// ---------------------------------------------------------------------------
// The retail corpus
// ---------------------------------------------------------------------------

namespace {

std::string DecompiledObjects(int level) {
    const fs::path path = LevelDir(level) / "OBJECTS.QVM";
    if (!fs::is_regular_file(path)) return {};
    const QVMFile qvm = QVM_Parse(path.string());
    if (!qvm.valid) return {};
    return QVM_DecompileToString(qvm);
}

bool CorpusMissing(int level) {
    return !fs::is_regular_file(LevelDir(level) / "OBJECTS.QVM");
}

// Whether two values agree at the precision the authored QSC format carries.
//
// The format's own numbers are 7 significant digits: ArgTokenFromArg
// re-serialises a line with %.7g "to match the source format"
// (source/level/fog_qsc.h:52).  The two installed artifacts this suite compares
// are NOT bit-identical.  Measured on the installed tree: objects.qsc:1120
// holds 14.299994468688965 (float bits 0x4164CCC7) for level 1's first scene
// duration, while OBJECTS.QVM+157138 holds 0x4164CCC2 =
// 14.299989700317383 -- five float ULPs apart, beyond EXPECT_FLOAT_EQ's
// four-ULP window.  (The .qsc carries the exact decimal of a float, i.e. it is
// a decompilation, and its mtime is older than the .qvm's.)  Comparing at
// authored precision keeps the check meaningful -- a misaligned token moves a
// value by whole units -- where bit equality is not achievable from the data.
bool SameAuthoredNumber(float left, float right) {
    char left_text[32];
    char right_text[32];
    std::snprintf(left_text, sizeof(left_text), "%.7g", static_cast<double>(left));
    std::snprintf(right_text, sizeof(right_text), "%.7g", static_cast<double>(right));
    return std::strcmp(left_text, right_text) == 0;
}

}  // namespace

TEST(CutSceneRulesCorpus, TheRetailCutSceneDeclarationMatchesTheParameterTable) {
    if (CorpusMissing(2)) GTEST_SKIP() << "retail IGI not found at " << GameRoot().string();
    const std::string text = DecompiledObjects(2);
    ASSERT_FALSE(text.empty());

    const std::vector<std::string> declared = DeclaredParameters(text, "CutScene");
    ASSERT_EQ(declared.size(), static_cast<std::size_t>(igi::CutSceneScreenParamCount()) * 2u);

    const igi::CutSceneParamDesc* table = igi::CutSceneScreenParamTable();
    for (int i = 0; i < igi::CutSceneScreenParamCount(); ++i) {
        EXPECT_EQ(declared[static_cast<std::size_t>(i) * 2], table[i].authored_name) << "slot " << i;
        EXPECT_EQ(declared[static_cast<std::size_t>(i) * 2 + 1], table[i].type_name) << "slot " << i;
    }
}

TEST(CutSceneRulesCorpus, TheRetailEditCameraDeclarationMatchesTheParameterTable) {
    if (CorpusMissing(2)) GTEST_SKIP() << "retail IGI not found at " << GameRoot().string();
    const std::string text = DecompiledObjects(2);
    ASSERT_FALSE(text.empty());

    const std::vector<std::string> declared = DeclaredParameters(text, "EditCamera");
    ASSERT_EQ(declared.size(), static_cast<std::size_t>(igi::CutSceneKeyParamCount()) * 2u);

    const igi::CutSceneParamDesc* table = igi::CutSceneKeyParamTable();
    for (int i = 0; i < igi::CutSceneKeyParamCount(); ++i) {
        EXPECT_EQ(declared[static_cast<std::size_t>(i) * 2], table[i].authored_name) << "slot " << i;
        EXPECT_EQ(declared[static_cast<std::size_t>(i) * 2 + 1], table[i].type_name) << "slot " << i;
    }
}

TEST(CutSceneRulesCorpus, EveryAuthoredEditCameraHasExactlyTheTablesTokens) {
    if (CorpusMissing(1)) GTEST_SKIP() << "retail IGI not found at " << GameRoot().string();
    const std::string text = DecompiledObjects(1);
    ASSERT_FALSE(text.empty());

    const std::vector<AuthoredCall> keys = FindTaskNewCalls(text, "EditCamera");
    ASSERT_FALSE(keys.empty());
    for (std::size_t i = 0; i < keys.size(); ++i) {
        EXPECT_EQ(keys[i].args.size(), static_cast<std::size_t>(igi::kCutSceneKeyTokenCount))
            << "key " << i;
        EXPECT_TRUE(keys[i].children.empty()) << "key " << i;
        igi::CutSceneKeyParams params;
        EXPECT_TRUE(igi::ParseCutSceneKey(keys[i].args, &params)) << "key " << i;
    }
}

TEST(CutSceneRulesCorpus, EveryAuthoredCutSceneHasTheHeaderTokensAndOnlyEditCameraChildren) {
    if (CorpusMissing(1)) GTEST_SKIP() << "retail IGI not found at " << GameRoot().string();
    const std::string text = DecompiledObjects(1);
    ASSERT_FALSE(text.empty());

    const std::vector<AuthoredCall> scenes = FindTaskNewCalls(text, "CutScene");
    ASSERT_FALSE(scenes.empty());
    for (std::size_t i = 0; i < scenes.size(); ++i) {
        // The call's own argument count is the 21 header tokens plus one per
        // nested child call (Qvm_OpCall, qvm.c:641-689).
        EXPECT_EQ(scenes[i].args.size(),
                  static_cast<std::size_t>(igi::kCutSceneScreenTokenCount) + scenes[i].children.size())
            << "scene " << i;
        for (const AuthoredCall& child : scenes[i].children) {
            EXPECT_EQ(child.type, "EditCamera") << "scene " << i;
        }
        igi::CutSceneScreenParams screen;
        EXPECT_TRUE(igi::ParseCutSceneScreen(scenes[i].args, &screen)) << "scene " << i;
    }
}

TEST(CutSceneRulesCorpus, TheAuthoredTimelinesMatchTheirDurations) {
    // Measured on the retail data: nine cutscenes over three levels, 120 keys.
    struct Expected {
        int level;
        std::size_t key_count;
    };
    const std::vector<Expected> expected = {
        {1, 7}, {1, 2}, {1, 30}, {1, 3}, {1, 6},
        {2, 22}, {2, 9},
        {4, 36}, {4, 5},
    };

    std::vector<std::size_t> measured;
    for (int level : {1, 2, 4}) {
        if (CorpusMissing(level)) GTEST_SKIP() << "retail IGI not found at " << GameRoot().string();
        const std::string text = DecompiledObjects(level);
        ASSERT_FALSE(text.empty()) << "LEVEL" << level;

        for (const RetailCutScene& scene : BuildCutScenes(text)) {
            measured.push_back(scene.keys.size());

            // Every start time is the running sum of `timeScale * duration`
            // (cutscene.c:170-172), recomputed here in double.
            double accumulated = 0.0;
            for (const igi::CutSceneKey& key : scene.keys) {
                EXPECT_NEAR(key.start_time, accumulated, 1e-3)
                    << "LEVEL" << level << " cutscene " << scene.task_id;
                accumulated += static_cast<double>(scene.screen.time_scale) * key.params.duration;
            }
            EXPECT_NEAR(scene.end, accumulated, 1e-3)
                << "LEVEL" << level << " cutscene " << scene.task_id;
            EXPECT_GT(scene.end, 0.0f) << "LEVEL" << level << " cutscene " << scene.task_id;
        }
    }
    EXPECT_EQ(measured, std::vector<std::size_t>({7, 2, 30, 3, 6, 22, 9, 36, 5}));

    std::size_t total = 0;
    for (const std::size_t count : measured) total += count;
    EXPECT_EQ(total, 120u);
}

TEST(CutSceneRulesCorpus, TheQvmAndTheDecompiledQscAgreeOnLevelOnesTimeline) {
    // level1 ships both OBJECTS.QVM and a human-readable objects.qsc, i.e. two
    // renderings of the same authored data. Comparing them proves the QVM token
    // reader is aligned with the argument list rather than off by a token.
    const fs::path dir = LevelDir(1);
    if (CorpusMissing(1) || !fs::is_regular_file(dir / "objects.qsc")) {
        GTEST_SKIP() << "retail IGI (with objects.qsc) not found at " << GameRoot().string();
    }

    const std::vector<RetailCutScene> from_qvm = BuildCutScenes(DecompiledObjects(1));
    const std::vector<RetailCutScene> from_qsc = BuildCutScenes(ReadFile(dir / "objects.qsc"));

    ASSERT_EQ(from_qvm.size(), from_qsc.size());
    ASSERT_EQ(from_qvm.size(), 5u);
    for (std::size_t i = 0; i < from_qvm.size(); ++i) {
        EXPECT_EQ(from_qvm[i].task_id, from_qsc[i].task_id);
        ASSERT_EQ(from_qvm[i].keys.size(), from_qsc[i].keys.size());
        EXPECT_FLOAT_EQ(from_qvm[i].screen.time_scale, from_qsc[i].screen.time_scale);
        EXPECT_FLOAT_EQ(from_qvm[i].screen.screen_size, from_qsc[i].screen.screen_size);
        EXPECT_NEAR(from_qvm[i].end, from_qsc[i].end, 1e-4);
        for (std::size_t key = 0; key < from_qvm[i].keys.size(); ++key) {
            EXPECT_NEAR(from_qvm[i].keys[key].start_time, from_qsc[i].keys[key].start_time, 1e-4);
            EXPECT_DOUBLE_EQ(from_qvm[i].keys[key].params.position[0],
                             from_qsc[i].keys[key].params.position[0]);
            EXPECT_TRUE(SameAuthoredNumber(from_qvm[i].keys[key].params.duration,
                                           from_qsc[i].keys[key].params.duration))
                << "scene " << from_qvm[i].task_id << " key " << key << " qvm "
                << from_qvm[i].keys[key].params.duration << " qsc "
                << from_qsc[i].keys[key].params.duration;
        }
    }
}

TEST(CutSceneRulesCorpus, EveryRetailKeyHoldsItsAuthoredPoseAtItsStartTime) {
    // At t == keys[i].start_time the selected window's u is exactly 0, so
    // Hermite_Eval returns p1 unchanged (flare.c:55-61) whatever the smooth flags
    // do to the tangent neighbours. This is the rule a preview scrub depends on.
    //
    // A zero-duration key shares its start time with the next one, and the scan
    // (cutscene.c:436-441) takes the LAST key whose time is <= t, so the later
    // key is the one that holds the pose. Those keys are skipped, and that the
    // engine resolves them that way is itself asserted.
    int zero_duration_keys = 0;
    for (int level : {1, 2, 4}) {
        if (CorpusMissing(level)) GTEST_SKIP() << "retail IGI not found at " << GameRoot().string();
        const std::string text = DecompiledObjects(level);
        ASSERT_FALSE(text.empty()) << "LEVEL" << level;

        for (const RetailCutScene& scene : BuildCutScenes(text)) {
            for (std::size_t i = 0; i < scene.keys.size(); ++i) {
                igi::CutSceneCameraState state;
                ASSERT_TRUE(igi::EvaluateCutScene(scene.keys, scene.end, scene.screen,
                                                  scene.keys[i].start_time, false, &state))
                    << "LEVEL" << level << " cutscene " << scene.task_id << " key " << i;

                if (i + 1 < scene.keys.size() &&
                    scene.keys[i + 1].start_time <= scene.keys[i].start_time) {
                    ++zero_duration_keys;
                    EXPECT_EQ(state.active_key, static_cast<int>(i) + 1)
                        << "LEVEL" << level << " cutscene " << scene.task_id << " key " << i;
                    continue;
                }

                EXPECT_EQ(state.active_key, static_cast<int>(i))
                    << "LEVEL" << level << " cutscene " << scene.task_id << " key " << i;
                EXPECT_DOUBLE_EQ(state.position[0], scene.keys[i].params.position[0]);
                EXPECT_DOUBLE_EQ(state.position[1], scene.keys[i].params.position[1]);
                EXPECT_DOUBLE_EQ(state.position[2], scene.keys[i].params.position[2]);
                EXPECT_FLOAT_EQ(state.angles[0], scene.keys[i].params.angles[0]);
                EXPECT_FLOAT_EQ(state.angles[1], scene.keys[i].params.angles[1]);
                EXPECT_FLOAT_EQ(state.angles[2], scene.keys[i].params.angles[2]);
                EXPECT_FLOAT_EQ(state.fov, scene.keys[i].params.fov);
            }
        }
    }
    // The retail data really does contain hold keys, so this branch is exercised.
    EXPECT_GT(zero_duration_keys, 0);
}

TEST(CutSceneRulesCorpus, TheFirstRetailCutSceneResolvesToItsAuthoredStart) {
    // level1/objects.qsc:1093-1100, cutscene 1200: seven keys, Time scale 1 and
    // Viewport height factor 0.7, durations 6.0 + 3.0 + 2.5 + 0 + 4.5 + 0 + 2.6,
    // so the timeline is 18.6 seconds long and the letterbox eases to 0.7.
    if (CorpusMissing(1)) GTEST_SKIP() << "retail IGI not found at " << GameRoot().string();
    const std::vector<RetailCutScene> scenes = BuildCutScenes(DecompiledObjects(1));
    ASSERT_GE(scenes.size(), 1u);
    const RetailCutScene& scene = scenes.front();

    EXPECT_EQ(scene.task_id, "1200");
    EXPECT_FLOAT_EQ(scene.screen.time_scale, 1.0f);
    EXPECT_FLOAT_EQ(scene.screen.screen_size, 0.69999998807907104f);
    EXPECT_FALSE(scene.screen.initial_run);
    EXPECT_EQ(scene.screen.run_expression, "!CutScene_1200.isFinished");
    ASSERT_EQ(scene.keys.size(), 7u);
    EXPECT_NEAR(scene.end, 18.6f, 1e-4f);
    EXPECT_FLOAT_EQ(scene.keys[0].start_time, 0.0f);
    EXPECT_NEAR(scene.keys[1].start_time, 6.0f, 1e-4f);
    EXPECT_NEAR(scene.keys[4].start_time, 11.5f, 1e-4f);

    // The authored first key (objects.qsc:1094).
    EXPECT_DOUBLE_EQ(scene.keys[0].params.position[0], 22664462.0);
    EXPECT_FLOAT_EQ(scene.keys[0].params.duration, 6.0f);
    EXPECT_EQ(scene.keys[0].params.pos_attach, 1502);
}
