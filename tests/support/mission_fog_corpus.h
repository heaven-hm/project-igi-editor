#pragma once

// Shared retail-corpus helpers for the mission-fog suites
// (tests/test_mission_fog.cpp, tests/test_mission_fog_wiring.cpp).
//
// A retail OBJECTS.QVM has no readable GlobalLightKeyframe text, so both suites
// recover the authored arguments by walking the bytecode: the type string is
// pushed, then the instance name string, then the 51 declared parameters as a
// flat push sequence. Keeping the walk in one place matters because two copies
// would eventually disagree about the token layout the resolver is fed.
//
// The walk is cross-checked against the two levels that DO ship a decompiled
// objects.qsc (LEVEL1, LEVEL4) by MissionFogCorpusTest.Level1AndLevel4MatchTheDecompiledText.

#include "../../source/level/qvm_parser.h"
#include "../../source/runtime/mission_fog.h"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace test_support {

namespace fs = std::filesystem;

constexpr float kFixed = 4096.0f;  // src/math/mathvars.h:7 - game units per metre

inline fs::path GameRoot() {
    if (const char* env = std::getenv("IGI_GAME_PATH"); env != nullptr && *env != '\0')
        return fs::path(env);
    return fs::path("/Users/haseeb-mir/Downloads/IGI");
}

inline fs::path LevelDir(int level) {
    return GameRoot() / "MISSIONS" / "location0" / ("LEVEL" + std::to_string(level));
}

// ---------------------------------------------------------------------------
// QVM bytecode -> GlobalLightKeyframe arg tokens
// ---------------------------------------------------------------------------

inline bool IsStringPush(QVMOpType type) {
    return type == QVMOpType::PUSHS || type == QVMOpType::PUSHSI ||
           type == QVMOpType::PUSHSIB || type == QVMOpType::PUSHSIW;
}

inline bool IsIntPush(QVMOpType type) {
    return type == QVMOpType::PUSHI || type == QVMOpType::PUSHII ||
           type == QVMOpType::PUSHIIB || type == QVMOpType::PUSHIIW ||
           type == QVMOpType::PUSHB || type == QVMOpType::PUSHW;
}

// PUSHIIB/PUSHIIW/PUSHII index the QVM identifier table, which for a retail
// OBJECTS.QVM is exactly ["Task_DeclareParameters","Task_New","TRUE","FALSE"].
// That is how a boolean literal reaches the task: `PUSHIIB 3` is FALSE.
inline bool IsIdentifierPush(QVMOpType type) {
    return type == QVMOpType::PUSHII || type == QVMOpType::PUSHIIB ||
           type == QVMOpType::PUSHIIW;
}

inline std::string FormatFloat(float value) {
    char buffer[64];
    // %.9g round-trips a float exactly; the token is re-parsed with strtof by
    // mission_fog.cpp, so this keeps the byte-for-byte value.
    std::snprintf(buffer, sizeof(buffer), "%.9g", value);
    return buffer;
}

// Walks one authored GlobalLightKeyframe call and returns its arguments in the
// editor's argTokens layout: [0]=task id, [1]=type, [2]=name, [3]=parameter 0.
//
// The declaration call (`Task_DeclareParameters("GlobalLightKeyframe", ...)`)
// also pushes the type string, so the two are told apart structurally: after the
// type comes the instance NAME string, and after that a declaration has another
// parameter-name string while an instance has a value.
inline std::vector<std::string> ReadKeyframeCall(const QVMFile& qvm, std::size_t type_index) {
    std::vector<std::string> tokens;
    tokens.push_back("-1");                      // task id, see below
    tokens.push_back("GlobalLightKeyframe");     // type

    const std::size_t kExpected = 2 + 1 + static_cast<std::size_t>(igi::kGlobalLightKeyframeParams);

    for (std::size_t i = type_index + 1; i < qvm.instructions.size() && tokens.size() < kExpected; ++i) {
        const QVMInstruction& in = qvm.instructions[i];
        if (in.type == QVMOpType::BRK) continue;

        if (IsStringPush(in.type)) {
            tokens.push_back(in.operand < qvm.strings.size() ? qvm.strings[in.operand]
                                                             : std::string());
            continue;
        }
        if (IsIntPush(in.type)) {
            if (IsIdentifierPush(in.type) && in.operand < qvm.identifiers.size()) {
                tokens.push_back(qvm.identifiers[in.operand]);
            } else {
                tokens.push_back(std::to_string(static_cast<int>(in.operand)));
            }
            continue;
        }
        if (in.type == QVMOpType::PUSH0) { tokens.push_back("0"); continue; }
        if (in.type == QVMOpType::PUSH1) { tokens.push_back("1"); continue; }
        if (in.type == QVMOpType::PUSHF) { tokens.push_back(FormatFloat(in.operand_float)); continue; }
        if (in.type == QVMOpType::MINUS) {
            if (!tokens.empty()) tokens.back() = "-" + tokens.back();
            continue;
        }
        // Anything else ends the call's argument list.
        break;
    }
    return tokens;
}

// Every authored GlobalLightKeyframe in the file, in file order.
inline std::vector<std::vector<std::string>> ExtractKeyframes(const QVMFile& qvm) {
    std::vector<std::vector<std::string>> out;
    for (std::size_t i = 0; i < qvm.instructions.size(); ++i) {
        const QVMInstruction& in = qvm.instructions[i];
        if (!IsStringPush(in.type)) continue;
        if (in.operand >= qvm.strings.size()) continue;
        if (qvm.strings[in.operand] != "GlobalLightKeyframe") continue;

        std::size_t name = i + 1;
        while (name < qvm.instructions.size() && qvm.instructions[name].type == QVMOpType::BRK) ++name;
        if (name >= qvm.instructions.size() || !IsStringPush(qvm.instructions[name].type)) continue;

        std::size_t first = name + 1;
        while (first < qvm.instructions.size() && qvm.instructions[first].type == QVMOpType::BRK) ++first;
        if (first >= qvm.instructions.size() || IsStringPush(qvm.instructions[first].type)) continue;

        out.push_back(ReadKeyframeCall(qvm, i));
    }
    return out;
}

// Builds a keyframe token vector with 51 parameters in the declared order.
inline std::vector<std::string> KeyframeTokens(float fog_density, float time,
                                               float fog_r = 0.5f, float fog_g = 0.5f,
                                               float fog_b = 0.5f) {
    std::vector<std::string> t;
    t.push_back("-1");
    t.push_back("GlobalLightKeyframe");
    t.push_back("");
    t.push_back("FALSE");                 // 0  Link All Sliders
    for (int set = 0; set < 5; ++set) {
        t.push_back("0");                 // ambient R
        t.push_back("0");                 // ambient G
        t.push_back("0");                 // ambient B
        t.push_back(std::to_string(fog_r));
        t.push_back(std::to_string(fog_g));
        t.push_back(std::to_string(fog_b));
        t.push_back(std::to_string(fog_density));
        t.push_back(std::to_string(set - 1));  // link
    }
    t.push_back("0.5"); t.push_back("0.55"); t.push_back("0.6");  // 41..43 Sky color
    t.push_back("0"); t.push_back("0"); t.push_back("0");         // 44..46 Water ambient
    t.push_back("1"); t.push_back("1"); t.push_back("1");         // 47..49 Water color
    t.push_back(std::to_string(time));                            // 50 Time
    return t;
}

// A token vector in the shape mission_fog consumes.
inline std::vector<igi::FogTaskObject> KeyframesAsFogTasks(
    const std::vector<std::vector<std::string>>& keyframes) {
    std::vector<igi::FogTaskObject> objects;
    objects.reserve(keyframes.size());
    for (const std::vector<std::string>& tokens : keyframes) {
        objects.push_back({"GlobalLightKeyframe", tokens});
    }
    return objects;
}

// ---------------------------------------------------------------------------
// objects.qsc text -> the same tokens, for the cross-check
// ---------------------------------------------------------------------------

inline std::string ReadFile(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return {};
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

// Splits the argument list of every `Task_New(..., "GlobalLightKeyframe", ...)`
// call in a decompiled .qsc, honouring quoted strings and nested parentheses.
inline std::vector<std::vector<std::string>> ParseKeyframeCallsFromText(const std::string& text) {
    std::vector<std::vector<std::string>> out;
    const std::string needle = "Task_New(";
    std::size_t pos = text.find(needle);

    while (pos != std::string::npos) {
        const std::size_t open = pos + needle.size() - 1;
        std::size_t index = open + 1;
        int depth = 1;
        bool in_string = false;
        bool escape = false;
        for (; index < text.size(); ++index) {
            const char c = text[index];
            if (escape) { escape = false; continue; }
            if (in_string && c == '\\') { escape = true; continue; }
            if (c == '"') { in_string = !in_string; continue; }
            if (in_string) continue;
            if (c == '(') ++depth;
            if (c == ')' && --depth == 0) break;
        }
        if (index >= text.size()) break;

        const std::string body = text.substr(open + 1, index - open - 1);
        std::vector<std::string> args;
        std::string current;
        depth = 0;
        in_string = false;
        escape = false;
        for (char c : body) {
            if (escape) { current.push_back(c); escape = false; continue; }
            if (in_string && c == '\\') { current.push_back(c); escape = true; continue; }
            if (c == '"') { in_string = !in_string; current.push_back(c); continue; }
            if (!in_string && c == '(') ++depth;
            if (!in_string && c == ')') --depth;
            if (!in_string && depth == 0 && c == ',') { args.push_back(current); current.clear(); continue; }
            current.push_back(c);
        }
        if (!current.empty()) args.push_back(current);

        for (std::string& arg : args) {
            const std::size_t b = arg.find_first_not_of(" \t\r\n");
            const std::size_t e = arg.find_last_not_of(" \t\r\n");
            arg = (b == std::string::npos) ? std::string() : arg.substr(b, e - b + 1);
            if (arg.size() >= 2 && arg.front() == '"' && arg.back() == '"') {
                arg = arg.substr(1, arg.size() - 2);
            }
        }

        if (args.size() > 1 && args[1] == "GlobalLightKeyframe") out.push_back(args);
        // The decompiled .qsc is a nested Task_New tree, so a parent call's body
        // contains its children. Resume just past this call's own name rather
        // than past its matching paren, otherwise every nested call is skipped.
        pos = text.find(needle, pos + needle.size());
    }
    return out;
}

// True when both tokens denote the same authored value: equal as float when both
// parse as numbers, equal as strings otherwise (TRUE/FALSE, names).
inline bool SameToken(const std::string& a, const std::string& b) {
    char* end_a = nullptr;
    char* end_b = nullptr;
    const float fa = std::strtof(a.c_str(), &end_a);
    const float fb = std::strtof(b.c_str(), &end_b);
    const bool numeric_a = end_a != a.c_str() && *end_a == '\0';
    const bool numeric_b = end_b != b.c_str() && *end_b == '\0';
    if (numeric_a && numeric_b) return fa == fb;
    return a == b;
}

}  // namespace test_support
