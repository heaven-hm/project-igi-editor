// level_flow.cpp - Mission objective evaluation, flow state, and extraction logic implementation
#include "level_flow.h"

#include <utility>

// MissionTextMissingRendering: the engine's own "RM: %s" rendering for a mission
// text key that no mounted archive defines (StatusMessageScreen_AddMessage
// 0x486650, src/mission/statusmessage.c:660; ComputerObjectives_Draw 0x469110,
// src/compmap/objectives.c:151). Reused rather than re-spelled so the marker has
// exactly one definition in the editor.
#include "mission_text_resource.h"

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
        // The objective's `text` parameter is a bare resource name and the engine
        // resolves it with the same flat lookup it uses for a StatusMessage text
        // before drawing it (ComputerObjectives_Draw 0x469110,
        // src/compmap/objectives.c:148-149), rendering "RM: <key>" -- never the
        // raw key -- when no mounted archive defines it (:151). Resolved once here
        // so the per-frame display path does not re-read the archive.
        std::string display_text =
            ResolveObjectiveText(authored_objective.text_resource);

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

std::string LevelFlow::ResolveObjectiveText(const std::string& resource_key) const {
    if (objective_text_resolver_) {
        // The injected resolver is the engine's flat lookup: Resource_Find
        // 0x4B64E0 -> Resource_IsLoaded 0x4B5B90 over the one global resource
        // list (src/res/resource.c:124-147), and Resource_Load 0x4B5F00 for the
        // body (src/res/resource.c:247-269). On a genuine miss it returns the
        // engine's "RM: <key>" (MissionTextResources::Resolve).
        return objective_text_resolver_(resource_key);
    }

    // No resolver was injected -- LevelFlow() + InitializeMission(mission_number)
    // builds the fallback objective list (InitializeFallbackObjectives) and this
    // object owns no archive. The engine never displays a bare key, so the miss
    // rendering is the honest answer here as well.
    return MissionTextMissingRendering(resource_key);
}

std::string LevelFlow::EngineObjectiveStatusLine(
    const MissionObjective& objective) const {
    // ComputerObjectives_Draw 0x469110 (src/compmap/objectives.c:137-145):
    //
    //   crt_sprintf(text, "%s %d: ", Resource_Load("OBJECTIVE", 0), i + 1);
    //   if (obj->failed)        s = Resource_Load("FAILED", 0);
    //   else if (obj->complete) s = Resource_Load("COMPLETE", 0);
    //   else                    s = Resource_Load("INCOMPLETE", 0);
    //
    // OBJECTIVE, FAILED, COMPLETE and INCOMPLETE are flat names in
    // LANGUAGE:objectives.res -- the archive ComputerRes_Load 0x46B070 mounts
    // for the map computer (src/compmap/res.c:32). `i + 1` is the objective's
    // 1-based slot; MissionObjective::id carries the same 1-based position
    // (LoadAuthoredObjectiveSet sets it to objective_index + 1).
    //
    // The engine draws the objective's own text on the FOLLOWING line
    // (src/compmap/objectives.c:148-155, including its "RM: %s" miss case) --
    // that text is what GetObjectiveDisplayText() returns while the objective is
    // still pending, so the editor's one-line card shows one half of the
    // engine's two-line pair, chosen by whether the objective is still to do.
    const char* status_key = "INCOMPLETE";
    if (objective.state == ObjectiveState::Failed) {
        status_key = "FAILED";
    } else if (objective.state == ObjectiveState::Completed) {
        status_key = "COMPLETE";
    }

    return ResolveObjectiveText("OBJECTIVE") + " " +
        std::to_string(objective.id) + ": " + ResolveObjectiveText(status_key);
}

std::string LevelFlow::GetObjectiveDisplayText() const {
    // (a) Mission failure and (b) mission completion have no dedicated HUD
    // objective line in the engine: they are StatusMessages. The level's
    // objects.qsc sends one whose `text` parameter is the bare key
    // MISSION_FAILED / MISSION_COMPLETE, and wires the LevelFlow failure
    // expression to that message:
    //
    //   MISSIONS/location0/level1/objects.qsc:1208
    //       Task_New(1391, "StatusMessage", ..., "MISSION_FAILED", ...)
    //   MISSIONS/location0/level1/objects.qsc:1456
    //       Task_New(10, "LevelFlow", ..., "CutScene_1204.isFinished",
    //                "StatusMessage_1391.isSendt", ...)
    //   MISSIONS/location0/LEVEL14/OBJECTS.QVM
    //       StatusMessage "Mission Complete" -> text "MISSION_COMPLETE"
    //       StatusMessage "Mission Failed"   -> text "MISSION_FAILED"
    //
    // StatusMessageScreen_AddMessage 0x486650 (src/mission/statusmessage.c:653-660)
    // resolves that parameter with Resource_Find/Resource_Load -- a flat global
    // lookup, no archive named at the call site -- and StatusMessages_Load
    // 0x4868A0 mounts LANGUAGE:messages.res for it (src/mission/statusmessage.c:671).
    // Both keys are defined there in the shipped data ("Mission Complete!" /
    // "Mission Failed!"), not in objectives.res.
    if (status_ == MissionStatus::Failed) {
        return ResolveObjectiveText("MISSION_FAILED");
    }
    if (status_ == MissionStatus::Success) {
        return ResolveObjectiveText("MISSION_COMPLETE");
    }

    // (c) The objective the player is on. Its `text` parameter is a bare
    // resource name too and the engine resolves it the same flat way before
    // drawing it (ComputerObjectives_Draw 0x469110,
    // src/compmap/objectives.c:148-149); LoadAuthoredObjectiveSet already
    // resolved it once through the same resolver, so the display path does not
    // touch the archive again.
    //
    // ComputerObjectives_Draw is the engine's ONLY objective text display: a
    // search of IGI-RE for "objective" outside src/compmap, src/computer and
    // the StatusMessage keys finds only the module registration
    // (src/flow/modules.c:201-210, :368-372) and the symbol table (src/addr.c).
    // Retail has no in-world HUD objective line at all -- the HUD's top-left
    // column is StatusMessages (StatusMessageScreen_Draw 0x486020,
    // src/mission/statusmessage.c:441-463) -- so this card is the editor's
    // single-line projection of that one engine display.
    for (const auto& objective : objectives_) {
        if (objective.is_primary && objective.state == ObjectiveState::Pending) {
            return objective.description;
        }
    }

    // Nothing is pending. The engine has no generic "go to the extraction zone"
    // string: in retail that instruction is itself an authored objective with its
    // own key (e.g. M2_OBJ3 "Proceed to the extraction point." in
    // LANGUAGE/ENGLISH/objectives.res). What the engine does display for an
    // objective in this state is its own composed line, so compose that instead
    // of inventing a prompt. Both the composition and the key set are the
    // engine's (EngineObjectiveStatusLine, ComputerObjectives_Draw 0x469110).
    if (objectives_.empty()) {
        return std::string();
    }

    // The objective that resolved last is the last PRIMARY of the active set:
    // CompleteFirstPendingPrimaryObjective and
    // EvaluateAuthoredObjectiveExpressions both walk the set front-to-back, and
    // AdvanceAuthoredObjectiveSet replaces the set only when it has a successor.
    // A set that carries no primary at all (only the editor's fallback list does
    // that) falls back to its last entry.
    const MissionObjective* resolved_primary = nullptr;
    for (const MissionObjective& objective : objectives_) {
        if (objective.is_primary && objective.state != ObjectiveState::Pending) {
            resolved_primary = &objective;
        }
    }
    return EngineObjectiveStatusLine(
        resolved_primary ? *resolved_primary : objectives_.back());
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
