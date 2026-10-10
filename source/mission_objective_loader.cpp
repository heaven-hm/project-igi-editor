#include "mission_objective_loader.h"

#include <charconv>
#include <cstddef>
#include <string_view>
#include <utility>

namespace igi {
namespace {

constexpr std::string_view kDefineComputerObjective = "DefineComputerObjective";
constexpr size_t kMinimumTaskArgumentCount = 4;
constexpr size_t kFirstObjectiveArgumentIndex = 4;
constexpr size_t kObjectiveSlotArgumentCount = 4;
constexpr size_t kObjectiveSlotCount = 6;

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

bool TryParseInteger(const std::string& token, int32_t& value) {
    const std::string unquoted_token = UnquoteToken(token);
    const char* begin = unquoted_token.data();
    const char* end = begin + unquoted_token.size();
    const auto result = std::from_chars(begin, end, value);
    return result.ec == std::errc() && result.ptr == end;
}

} // namespace

std::vector<AuthoredMissionObjectiveSet> LoadAuthoredMissionObjectiveDefinitions(
    const std::vector<MissionObjectiveTaskSource>& task_sources) {
    std::vector<AuthoredMissionObjectiveSet> definitions;

    for (const MissionObjectiveTaskSource& task_source : task_sources) {
        if (task_source.task_type != kDefineComputerObjective ||
            task_source.argument_tokens.size() < kMinimumTaskArgumentCount) {
            continue;
        }

        AuthoredMissionObjectiveSet objective_set;
        objective_set.valid_expression = UnquoteToken(task_source.argument_tokens[3]);

        for (size_t slot_index = 0; slot_index < kObjectiveSlotCount; ++slot_index) {
            const size_t argument_index =
                kFirstObjectiveArgumentIndex + slot_index * kObjectiveSlotArgumentCount;
            if (argument_index >= task_source.argument_tokens.size()) {
                break;
            }

            const std::string text_resource =
                UnquoteToken(task_source.argument_tokens[argument_index]);
            if (text_resource.empty()) {
                continue;
            }

            AuthoredMissionObjectiveDefinition objective;
            objective.text_resource = text_resource;

            if (argument_index + 1 < task_source.argument_tokens.size()) {
                TryParseInteger(
                    task_source.argument_tokens[argument_index + 1],
                    objective.link_task_id);
            }
            if (argument_index + 2 < task_source.argument_tokens.size()) {
                objective.completion_expression = UnquoteToken(
                    task_source.argument_tokens[argument_index + 2]);
            }
            if (argument_index + 3 < task_source.argument_tokens.size()) {
                objective.failure_expression = UnquoteToken(
                    task_source.argument_tokens[argument_index + 3]);
            }
            objective_set.objectives.push_back(std::move(objective));
        }

        if (!objective_set.objectives.empty()) {
            definitions.push_back(std::move(objective_set));
        }
    }

    return definitions;
}

} // namespace igi
