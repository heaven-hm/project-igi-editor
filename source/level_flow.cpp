// level_flow.cpp - Mission objective evaluation, flow state, and extraction logic implementation
#include "level_flow.h"

#include <utility>

namespace igi {

LevelFlow::LevelFlow() {
    InitializeMission(1);
}

void LevelFlow::InitializeMission(uint32_t mission_number) {
    mission_number_ = mission_number;
    objectives_.clear();
    status_ = MissionStatus::InProgress;
    authored_objective_sets_.clear();
    objective_text_resolver_ = {};
    objective_location_resolver_ = {};
    active_authored_objective_set_index_ = 0;
    authored_mission_flow_ = {};
    has_authored_mission_flow_ = false;
    mission_flow_tick_ = 0;

    InitializeFallbackObjectives(mission_number);
}

void LevelFlow::InitializeMission(
    uint32_t mission_number,
    const std::vector<AuthoredMissionObjectiveSet>& authored_objective_sets,
    MissionObjectiveTextResolver text_resolver,
    const AuthoredMissionFlowDefinition& authored_flow,
    MissionObjectiveLocationResolver location_resolver) {
    mission_number_ = mission_number;
    objectives_.clear();
    status_ = MissionStatus::InProgress;
    authored_objective_sets_ = authored_objective_sets;
    objective_text_resolver_ = std::move(text_resolver);
    objective_location_resolver_ = std::move(location_resolver);
    active_authored_objective_set_index_ = 0;
    authored_mission_flow_ = authored_flow;
    has_authored_mission_flow_ = authored_flow.has_level_flow ||
        !authored_flow.complete_expression.empty() ||
        !authored_flow.failure_expression.empty();
    mission_flow_tick_ = 0;

    if (authored_objective_sets_.empty()) {
        InitializeFallbackObjectives(mission_number);
        return;
    }

    LoadAuthoredObjectiveSet(active_authored_objective_set_index_);
    if (objectives_.empty()) {
        authored_objective_sets_.clear();
        InitializeFallbackObjectives(mission_number);
    }
}

void LevelFlow::InitializeFallbackObjectives(uint32_t mission_number) {

    switch (mission_number) {
        case 1:
            AddObjective(1, "Infiltrate the trainyard and download train schedule", true);
            AddObjective(2, "Get to the train depot and rendezvous with Anya", false);
            break;
        case 2:
            AddObjective(1, "Infiltrate the SAM base and disable the SAM sites", true);
            AddObjective(2, "Locate Josef Priboi and extract via helicopter", false);
            break;
        case 3:
            AddObjective(1, "Infiltrate the military airbase without raising alarms", true);
            AddObjective(2, "Locate and steal the MiG-29 fighter jet", false);
            break;
        case 4:
            AddObjective(1, "Survive the plane crash and evade pursuit patrols", true);
            AddObjective(2, "Reach the hilltop extraction helicopter", false);
            break;
        case 5:
            AddObjective(1, "Infiltrate the radar base and sabotage the dish", true);
            AddObjective(2, "Destroy communication arrays and reach the exit", false);
            break;
        case 6:
            AddObjective(1, "Locate Josef Priboi in the high-security bunker", true);
            AddObjective(2, "Capture Josef Priboi alive and escort to extraction", false);
            break;
        case 7:
            AddObjective(1, "Cross the heavily fortified border zone", true);
            AddObjective(2, "Avoid searchlights and border patrol towers", false);
            break;
        case 8:
            AddObjective(1, "Infiltrate the weapons facility and recover gear", true);
            AddObjective(2, "Sabotage heavy armory and escape undetected", false);
            break;
        case 9:
            AddObjective(1, "Locate the nuclear missile transport train", true);
            AddObjective(2, "Disable missile guidance computers on the train", false);
            break;
        case 10:
            AddObjective(1, "Defend Josef Priboi from enemy assault waves", true);
            AddObjective(2, "Eliminate all incoming enemy soldiers and snipers", false);
            break;
        case 11:
            AddObjective(1, "Infiltrate Jach Priboi's mountain castle fortress", true);
            AddObjective(2, "Disable exterior perimeter security systems", false);
            break;
        case 12:
            AddObjective(1, "Breach the inner sanctum and locate Jach Priboi", true);
            AddObjective(2, "Apprehend Jach Priboi and clear the helicopter LZ", false);
            break;
        case 13:
            AddObjective(1, "Infiltrate the underground nuclear warhead complex", true);
            AddObjective(2, "Locate the primary reactor core and disable cooling", false);
            break;
        case 14:
            AddObjective(1, "Track down Ekk in the nuclear launch silo", true);
            AddObjective(2, "Eliminate Ekk and disarm the nuclear bomb", false);
            break;
        default:
            AddObjective(1, "Complete mission objectives and extract safely", true);
            break;
    }
}

void LevelFlow::AddObjective(uint32_t id, const std::string& desc, bool is_primary) {
    MissionObjective obj;
    obj.id = id;
    obj.description = desc;
    obj.is_primary = is_primary;
    obj.state = ObjectiveState::Pending;
    objectives_.push_back(obj);
}

void LevelFlow::LoadAuthoredObjectiveSet(size_t authored_set_index) {
    objectives_.clear();
    if (authored_set_index >= authored_objective_sets_.size()) {
        return;
    }

    const AuthoredMissionObjectiveSet& objective_set =
        authored_objective_sets_[authored_set_index];
    for (size_t objective_index = 0;
         objective_index < objective_set.objectives.size();
         ++objective_index) {
        const AuthoredMissionObjectiveDefinition& authored_objective =
            objective_set.objectives[objective_index];
        std::string display_text = authored_objective.text_resource;
        if (objective_text_resolver_) {
            const std::string resolved_text =
                objective_text_resolver_(authored_objective.text_resource);
            if (!resolved_text.empty()) {
                display_text = resolved_text;
            }
        }

        MissionObjective objective;
        objective.id = static_cast<uint32_t>(objective_index + 1);
        objective.description = std::move(display_text);
        objective.text_resource = authored_objective.text_resource;
        objective.link_task_id = authored_objective.link_task_id;
        objective.completion_expression = authored_objective.completion_expression;
        objective.failure_expression = authored_objective.failure_expression;
        objective.is_primary = true;
        objective.state = ObjectiveState::Pending;
        if (objective.link_task_id >= 0 && objective_location_resolver_) {
            MissionObjectiveLocation location;
            if (objective_location_resolver_(objective.link_task_id, location)) {
                objective.has_location = true;
                objective.location = location;
            }
        }
        objectives_.push_back(std::move(objective));
    }
}

bool LevelFlow::AdvanceAuthoredObjectiveSet() {
    if (authored_objective_sets_.empty() ||
        active_authored_objective_set_index_ + 1 >= authored_objective_sets_.size()) {
        return false;
    }

    ++active_authored_objective_set_index_;
    LoadAuthoredObjectiveSet(active_authored_objective_set_index_);
    return !objectives_.empty();
}

void LevelFlow::SelectValidAuthoredObjectiveSet(
    const MissionExpressionEvaluator& expression_evaluator) {
    if (authored_objective_sets_.empty() || !expression_evaluator) {
        return;
    }

    size_t selected_definition_index = 0;
    bool has_valid_definition = false;
    for (size_t definition_index = 0;
         definition_index < authored_objective_sets_.size();
         ++definition_index) {
        const AuthoredMissionObjectiveSet& definition =
            authored_objective_sets_[definition_index];
        const bool is_valid = definition.valid_expression.empty() ||
            expression_evaluator(definition.valid_expression);
        if (!is_valid) {
            continue;
        }

        // Retail walks definitions in task order and leaves the last valid
        // definition published to the map computer.
        selected_definition_index = definition_index;
        has_valid_definition = true;
    }

    if (!has_valid_definition ||
        selected_definition_index == active_authored_objective_set_index_) {
        return;
    }

    active_authored_objective_set_index_ = selected_definition_index;
    LoadAuthoredObjectiveSet(active_authored_objective_set_index_);
}

void LevelFlow::SetObjectiveState(uint32_t id, ObjectiveState state) {
    bool objective_was_updated = false;
    for (MissionObjective& objective : objectives_) {
        if (objective.id == id) {
            objective.state = state;
            objective_was_updated = true;
            break;
        }
    }

    if (!objective_was_updated || state != ObjectiveState::Completed) {
        return;
    }

    for (const MissionObjective& objective : objectives_) {
        if (objective.is_primary && objective.state == ObjectiveState::Pending) {
            return;
        }
    }
    AdvanceAuthoredObjectiveSet();
}

bool LevelFlow::CompleteFirstPendingPrimaryObjective() {
    if (status_ != MissionStatus::InProgress) {
        return false;
    }

    bool objective_was_completed = false;
    for (MissionObjective& objective : objectives_) {
        if (!objective.is_primary || objective.state != ObjectiveState::Pending) {
            continue;
        }
        objective.state = ObjectiveState::Completed;
        objective_was_completed = true;
        break;
    }

    if (!objective_was_completed) {
        return false;
    }

    for (const MissionObjective& objective : objectives_) {
        if (objective.is_primary && objective.state == ObjectiveState::Pending) {
            return true;
        }
    }
    AdvanceAuthoredObjectiveSet();
    return true;
}

std::string LevelFlow::GetObjectiveDisplayText() const {
    if (status_ == MissionStatus::Failed) {
        return "MISSION FAILED";
    }
    if (status_ == MissionStatus::Success) {
        return "MISSION COMPLETE";
    }

    for (const auto& objective : objectives_) {
        if (objective.is_primary && objective.state == ObjectiveState::Pending) {
            return objective.description;
        }
    }
    return "Reach the extraction zone";
}

void LevelFlow::EvaluateAuthoredObjectiveExpressions(
    const MissionExpressionEvaluator& expression_evaluator) {
    if (!expression_evaluator || authored_objective_sets_.empty()) {
        return;
    }

    bool objective_failed = false;
    for (MissionObjective& objective : objectives_) {
        if (objective.state != ObjectiveState::Pending) {
            continue;
        }

        if (!objective.failure_expression.empty() &&
            expression_evaluator(objective.failure_expression)) {
            objective.state = ObjectiveState::Failed;
            objective_failed = true;
            continue;
        }

        if (!objective.completion_expression.empty() &&
            expression_evaluator(objective.completion_expression)) {
            objective.state = ObjectiveState::Completed;
        }
    }

    if (objective_failed) {
        return;
    }

    for (const MissionObjective& objective : objectives_) {
        if (objective.is_primary && objective.state == ObjectiveState::Pending) {
            return;
        }
    }
    AdvanceAuthoredObjectiveSet();
}

bool LevelFlow::EvaluateExpression(
    const std::string& expression,
    const MissionExpressionEvaluator& expression_evaluator) const {
    return !expression.empty() && expression_evaluator &&
        expression_evaluator(expression);
}

bool LevelFlow::EvaluateAuthoredMissionFlow(
    const MissionExpressionEvaluator& expression_evaluator) {
    if (!has_authored_mission_flow_) {
        return false;
    }

    // Retail evaluates failure before completion when both expressions are
    // true in the same fixed tick.
    if (EvaluateExpression(
            authored_mission_flow_.failure_expression,
            expression_evaluator)) {
        status_ = MissionStatus::Failed;
        return true;
    }
    if (EvaluateExpression(
            authored_mission_flow_.complete_expression,
            expression_evaluator)) {
        status_ = MissionStatus::Success;
        return true;
    }
    return false;
}

void LevelFlow::Update(
    bool player_alive,
    bool in_extraction_zone,
    MissionExpressionEvaluator expression_evaluator) {
    if (status_ != MissionStatus::InProgress) return;

    if (!player_alive) {
        status_ = MissionStatus::Failed;
        return;
    }

    SelectValidAuthoredObjectiveSet(expression_evaluator);
    EvaluateAuthoredObjectiveExpressions(expression_evaluator);

    // Check primary objectives
    bool all_primaries_complete = true;
    for (const auto& obj : objectives_) {
        if (obj.is_primary) {
            if (obj.state == ObjectiveState::Failed) {
                status_ = MissionStatus::Failed;
                return;
            }
            if (obj.state != ObjectiveState::Completed) {
                all_primaries_complete = false;
            }
        }
    }

    if (has_authored_mission_flow_) {
        EvaluateAuthoredMissionFlow(expression_evaluator);
        ++mission_flow_tick_;
        return;
    }

    if (all_primaries_complete && in_extraction_zone) {
        status_ = MissionStatus::Success;
    }

    ++mission_flow_tick_;
}

} // namespace igi
