#include "mission_flow_loader.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <string>
#include <utility>

namespace igi {
namespace {

constexpr size_t kStartTimeArgumentIndex = 9;
constexpr size_t kCompleteArgumentIndex = 10;
constexpr size_t kFailureArgumentIndex = 11;
constexpr size_t kInterfaceTimerArgumentIndex = 12;
constexpr size_t kMaximumPlayTimeArgumentIndex = 13;

std::string UnquoteToken(std::string token) {
    if (token.size() < 2 || token.front() != '"' || token.back() != '"') {
        return token;
    }

    token = token.substr(1, token.size() - 2);
    std::string unquoted;
    unquoted.reserve(token.size());
    bool escaped = false;
    for (const char character : token) {
        if (escaped) {
            unquoted.push_back(character);
            escaped = false;
        } else if (character == '\\') {
            escaped = true;
        } else {
            unquoted.push_back(character);
        }
    }
    if (escaped) {
        unquoted.push_back('\\');
    }
    return unquoted;
}

bool TryParseReal(const std::string& token, double& value) {
    const std::string unquoted_token = UnquoteToken(token);
    if (unquoted_token.empty()) {
        return false;
    }

    char* end = nullptr;
    errno = 0;
    const double parsed_value = std::strtod(unquoted_token.c_str(), &end);
    if (errno == ERANGE || end == unquoted_token.c_str() || *end != '\0' ||
        !std::isfinite(parsed_value)) {
        return false;
    }

    value = parsed_value;
    return true;
}

bool TryParseBoolean(const std::string& token, bool& value) {
    const std::string unquoted_token = UnquoteToken(token);
    if (unquoted_token == "TRUE" || unquoted_token == "true" ||
        unquoted_token == "1") {
        value = true;
        return true;
    }
    if (unquoted_token == "FALSE" || unquoted_token == "false" ||
        unquoted_token == "0") {
        value = false;
        return true;
    }
    return false;
}

} // namespace

std::vector<AuthoredMissionFlowDefinition> LoadAuthoredMissionFlowDefinitions(
    const std::vector<MissionFlowTaskSource>& task_sources) {
    std::vector<AuthoredMissionFlowDefinition> definitions;

    for (const MissionFlowTaskSource& task_source : task_sources) {
        if (task_source.task_type != "LevelFlow" ||
            task_source.argument_tokens.size() <= kMaximumPlayTimeArgumentIndex) {
            continue;
        }

        AuthoredMissionFlowDefinition definition;
        definition.has_level_flow = true;
        if (!TryParseReal(
                task_source.argument_tokens[kStartTimeArgumentIndex],
                definition.start_time_seconds) ||
            !TryParseReal(
                task_source.argument_tokens[kMaximumPlayTimeArgumentIndex],
                definition.maximum_level_play_time_seconds) ||
            !TryParseBoolean(
                task_source.argument_tokens[kInterfaceTimerArgumentIndex],
                definition.interface_timer_enabled)) {
            continue;
        }

        definition.complete_expression = UnquoteToken(
            task_source.argument_tokens[kCompleteArgumentIndex]);
        definition.failure_expression = UnquoteToken(
            task_source.argument_tokens[kFailureArgumentIndex]);
        definition.maximum_level_play_time_seconds = std::max(
            0.0,
            definition.maximum_level_play_time_seconds);
        definitions.push_back(std::move(definition));
    }

    return definitions;
}

} // namespace igi
